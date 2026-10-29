#include "settings-client.h"
#include "settings-window.h"

#include <gtk/gtk.h>

struct settings_application {
	struct seyrix_settings_client client;
	struct seyrix_settings_window *window;
};

static void refresh_window(void *data) {
	struct settings_application *settings = data;
	seyrix_settings_window_refresh(settings->window);
}

static void on_activate(GtkApplication *application, gpointer data) {
	struct settings_application *settings = data;
	if (settings->window == NULL) {
		settings->window = seyrix_settings_window_create(application,
			&settings->client);
		seyrix_settings_client_start(&settings->client);
	}
	seyrix_settings_window_present(settings->window);
}

int main(int argc, char **argv) {
	struct settings_application settings = {0};
	seyrix_settings_client_init(&settings.client, refresh_window, &settings);
	GtkApplication *application = gtk_application_new("org.seyrix.settings",
		G_APPLICATION_NON_UNIQUE);
	g_signal_connect(application, "activate", G_CALLBACK(on_activate),
		&settings);
	int status = g_application_run(G_APPLICATION(application), argc, argv);
	seyrix_settings_client_finish(&settings.client);
	seyrix_settings_window_destroy(settings.window);
	g_object_unref(application);
	return status;
}
