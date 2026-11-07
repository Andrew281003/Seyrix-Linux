#define _GNU_SOURCE

#include "settings-client.h"
#include "settings-window.h"

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

struct settings_application {
	struct seyrix_settings_client client;
	GtkApplication *application;
	struct seyrix_settings_window *window;
	struct seyrix_settings_window_state state;
	char *state_path;
	guint sigint_source;
	guint sigterm_source;
	gboolean state_restored;
	gboolean display_lost;
	gboolean application_held;
	int exit_status;
};

static void refresh_window(void *data) {
	struct settings_application *settings = data;
	if (settings->window != NULL) {
		seyrix_settings_window_refresh(settings->window);
	}
}

static char *settings_state_path(void) {
	char *directory = g_build_filename(g_get_user_state_dir(), "seyrix", NULL);
	if (g_mkdir_with_parents(directory, 0700) != 0) {
		g_warning("Cannot create Settings state directory: %s", directory);
		g_free(directory);
		return NULL;
	}
	char *path = g_build_filename(directory, "settings.ini", NULL);
	g_free(directory);
	return path;
}

static gboolean load_window_state(struct settings_application *settings) {
	settings->state = (struct seyrix_settings_window_state){
		.category = 0,
		.width = 720,
		.height = 560,
		.paned_position = 160,
		.maximized = FALSE,
	};
	settings->state_path = settings_state_path();
	if (settings->state_path == NULL) {
		return FALSE;
	}
	struct stat status;
	if (g_lstat(settings->state_path, &status) != 0) {
		return FALSE;
	}
	if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
			(status.st_mode & 077) != 0) {
		g_warning("Ignoring unsafe Settings state file: %s", settings->state_path);
		return FALSE;
	}
	GKeyFile *file = g_key_file_new();
	GError *error = NULL;
	gboolean loaded = g_key_file_load_from_file(file, settings->state_path,
		G_KEY_FILE_NONE, &error);
	if (!loaded) {
		g_warning("Ignoring invalid Settings state file: %s",
			error != NULL ? error->message : "parse failed");
		g_clear_error(&error);
		g_key_file_unref(file);
		return FALSE;
	}
	struct seyrix_settings_window_state state = settings->state;
	state.category = (guint)g_key_file_get_integer(file, "window", "category",
		&error);
	if (error == NULL) state.width = g_key_file_get_integer(file,
		"window", "width", &error);
	if (error == NULL) state.height = g_key_file_get_integer(file,
		"window", "height", &error);
	if (error == NULL) state.paned_position = g_key_file_get_integer(file,
		"window", "paned-position", &error);
	if (error == NULL) state.maximized = g_key_file_get_boolean(file,
		"window", "maximized", &error);
	g_key_file_unref(file);
	if (error != NULL || state.category >= 6 || state.width < 400 ||
			state.width > 8192 || state.height < 300 || state.height > 8192 ||
			state.paned_position < 100 || state.paned_position > 2048) {
		g_warning("Ignoring invalid Settings window state%s%s",
			error != NULL ? ": " : "",
			error != NULL ? error->message : "");
		g_clear_error(&error);
		return FALSE;
	}
	settings->state = state;
	return TRUE;
}

static void save_window_state(struct settings_application *settings) {
	if (settings->window == NULL || settings->state_path == NULL) {
		return;
	}
	struct seyrix_settings_window_state previous = settings->state;
	seyrix_settings_window_get_state(settings->window, &settings->state);
	gboolean changed = previous.category != settings->state.category ||
		previous.width != settings->state.width ||
		previous.height != settings->state.height ||
		previous.paned_position != settings->state.paned_position ||
		previous.maximized != settings->state.maximized;
	GKeyFile *file = g_key_file_new();
	g_key_file_set_integer(file, "window", "category",
		(gint)settings->state.category);
	g_key_file_set_integer(file, "window", "width", settings->state.width);
	g_key_file_set_integer(file, "window", "height", settings->state.height);
	g_key_file_set_integer(file, "window", "paned-position",
		settings->state.paned_position);
	g_key_file_set_boolean(file, "window", "maximized",
		settings->state.maximized);
	gsize length = 0;
	gchar *contents = g_key_file_to_data(file, &length, NULL);
	g_key_file_unref(file);
	GError *error = NULL;
	if (contents == NULL || !g_file_set_contents_full(settings->state_path,
			contents, (gssize)length, G_FILE_SET_CONTENTS_CONSISTENT, 0600,
			&error)) {
		g_warning("Could not save Settings state: %s",
			error != NULL ? error->message : "serialization failed");
		g_clear_error(&error);
	} else if (changed) {
		g_message("SEYRIX_SETTINGS event=state-saved pid=%ld category=%s width=%d height=%d paned_position=%d maximized=%d",
			(long)getpid(), seyrix_settings_window_category_id(settings->window),
			settings->state.width, settings->state.height,
			settings->state.paned_position, settings->state.maximized);
	}
	g_free(contents);
}

static void on_window_state_changed(gpointer data) {
	save_window_state(data);
}

static void on_display_closed(GdkDisplay *display, gboolean is_error,
		gpointer data) {
	(void)display;
	(void)is_error;
	struct settings_application *settings = data;
	if (settings->display_lost || g_object_get_data(
			G_OBJECT(settings->application), "seyrix-user-close") != NULL) {
		return;
	}
	settings->display_lost = TRUE;
	save_window_state(settings);
	g_message("SEYRIX_SETTINGS event=display-lost pid=%ld exit_status=75",
		(long)getpid());
	settings->exit_status = 75;
	g_application_quit(G_APPLICATION(settings->application));
}

static gboolean on_session_signal(gpointer data) {
	struct settings_application *settings = data;
	if (!settings->display_lost && g_object_get_data(G_OBJECT(settings->application),
			"seyrix-user-close") == NULL) {
		settings->display_lost = TRUE;
		save_window_state(settings);
		g_message("SEYRIX_SETTINGS event=display-lost pid=%ld signal=TERM exit_status=75",
			(long)getpid());
		settings->exit_status = 75;
		g_application_quit(G_APPLICATION(settings->application));
	}
	return G_SOURCE_CONTINUE;
}

static void on_activate(GtkApplication *application, gpointer data) {
	struct settings_application *settings = data;
	if (settings->window == NULL) {
		if (!settings->application_held) {
			g_application_hold(G_APPLICATION(application));
			settings->application_held = TRUE;
		}
		GdkDisplay *display = gdk_display_get_default();
		if (display != NULL) {
			g_signal_connect(display, "closed", G_CALLBACK(on_display_closed),
				settings);
		}
		settings->window = seyrix_settings_window_create(application,
			&settings->client, &settings->state, on_window_state_changed,
			settings);
		seyrix_settings_client_start(&settings->client);
		seyrix_settings_window_present(settings->window);
		save_window_state(settings);
		g_message("SEYRIX_SETTINGS event=start pid=%ld category=%s width=%d height=%d paned_position=%d maximized=%d state_restored=%d",
			(long)getpid(), seyrix_settings_window_category_id(settings->window),
			settings->state.width, settings->state.height,
			settings->state.paned_position, settings->state.maximized,
			settings->state_restored);
	}
	seyrix_settings_window_present(settings->window);
}

int main(int argc, char **argv) {
	if (g_strcmp0(g_getenv("SEYRIX_SETTINGS_SUPERVISED"), "1") != 0) {
		execl("/usr/local/bin/seyrix-settings-launch",
			"seyrix-settings-launch", (char *)NULL);
		g_warning("Could not route Settings through the Seyrix session supervisor");
		return 127;
	}
	/* shortcut: use Cairo for stale GtkStack pages on the software-GL desktop. */
	g_setenv("GSK_RENDERER", "cairo", TRUE);
	if (setpgid(0, 0) != 0) {
		g_warning("Could not isolate Settings process group");
		return 1;
	}
	struct settings_application settings = {0};
	settings.state_restored = load_window_state(&settings);
	seyrix_settings_client_init(&settings.client, refresh_window, &settings);
	GtkApplication *application = gtk_application_new("org.seyrix.settings",
		G_APPLICATION_DEFAULT_FLAGS);
	settings.application = application;
	g_signal_connect(application, "activate", G_CALLBACK(on_activate),
		&settings);
	settings.sigint_source = g_unix_signal_add(SIGINT, on_session_signal,
		&settings);
	settings.sigterm_source = g_unix_signal_add(SIGTERM, on_session_signal,
		&settings);
	int status = g_application_run(G_APPLICATION(application), argc, argv);
	int requested_status = GPOINTER_TO_INT(g_object_get_data(
		G_OBJECT(application), "seyrix-exit-status"));
	if (requested_status != 0) {
		status = requested_status;
	}
	if (settings.exit_status != 0) {
		status = settings.exit_status;
	}
	if (settings.application_held) {
		g_application_release(G_APPLICATION(application));
	}
	if (settings.sigint_source != 0) g_source_remove(settings.sigint_source);
	if (settings.sigterm_source != 0) g_source_remove(settings.sigterm_source);
	seyrix_settings_client_finish(&settings.client);
	seyrix_settings_window_destroy(settings.window);
	g_free(settings.state_path);
	g_object_unref(application);
	return status;
}
