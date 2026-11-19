#define _POSIX_C_SOURCE 200809L

#include "app-catalog.h"

#include <string.h>
#include <unistd.h>

struct seyrix_app_entry {
	GDesktopAppInfo *app_info;
	gchar *id;
	gchar *name;
	gchar *icon;
	gchar *folded_id;
	gchar *folded_name;
};

struct seyrix_app_catalog {
	GPtrArray *entries;
};

static void app_entry_free(gpointer data) {
	seyrix_app_entry *entry = data;
	if (entry == NULL) {
		return;
	}
	g_clear_object(&entry->app_info);
	g_free(entry->id);
	g_free(entry->name);
	g_free(entry->icon);
	g_free(entry->folded_id);
	g_free(entry->folded_name);
	g_free(entry);
}

static gboolean app_has_available_try_exec(GDesktopAppInfo *app_info) {
	gchar *try_exec = g_desktop_app_info_get_string(app_info, "TryExec");
	if (try_exec == NULL || *try_exec == '\0') {
		g_free(try_exec);
		return TRUE;
	}
	/* g_find_program_in_path handles absolute paths and PATH lookup. */
	gchar *resolved = g_find_program_in_path(try_exec);
	g_free(try_exec);
	if (resolved == NULL) {
		return FALSE;
	}
	g_free(resolved);
	return TRUE;
}

static gchar *icon_description(GIcon *icon) {
	if (icon == NULL) {
		return NULL;
	}
	if (G_IS_THEMED_ICON(icon)) {
		const gchar * const *names = g_themed_icon_get_names(
			G_THEMED_ICON(icon));
		if (names != NULL && names[0] != NULL && *names[0] != '\0') {
			return g_strdup(names[0]);
		}
	}
	if (G_IS_FILE_ICON(icon)) {
		GFile *file = g_file_icon_get_file(G_FILE_ICON(icon));
		return g_file_get_path(file);
	}
	return g_icon_to_string(icon);
}

static seyrix_app_entry *app_entry_new(GDesktopAppInfo *app_info) {
	if (app_info == NULL || !g_app_info_should_show(G_APP_INFO(app_info)) ||
			!app_has_available_try_exec(app_info)) {
		return NULL;
	}

	const gchar *id = g_app_info_get_id(G_APP_INFO(app_info));
	const gchar *name = g_app_info_get_display_name(G_APP_INFO(app_info));
	const gchar *executable = g_app_info_get_executable(G_APP_INFO(app_info));
	if (id == NULL || *id == '\0' || name == NULL || *name == '\0' ||
			executable == NULL || *executable == '\0') {
		return NULL;
	}

	seyrix_app_entry *entry = g_new0(seyrix_app_entry, 1);
	entry->app_info = g_object_ref(app_info);
	entry->id = g_strdup(id);
	entry->name = g_utf8_make_valid(name, -1);
	entry->icon = icon_description(g_app_info_get_icon(G_APP_INFO(app_info)));
	entry->folded_id = g_utf8_casefold(entry->id, -1);
	entry->folded_name = g_utf8_casefold(entry->name, -1);
	if (entry->folded_id == NULL || entry->folded_name == NULL) {
		app_entry_free(entry);
		return NULL;
	}
	return entry;
}

static gint compare_entries(gconstpointer left, gconstpointer right) {
	const seyrix_app_entry *a = *(seyrix_app_entry * const *)left;
	const seyrix_app_entry *b = *(seyrix_app_entry * const *)right;
	gint name_order = g_utf8_collate(a->folded_name, b->folded_name);
	return name_order != 0 ? name_order : strcmp(a->id, b->id);
}

seyrix_app_catalog *seyrix_app_catalog_new(void) {
	seyrix_app_catalog *catalog = g_new0(seyrix_app_catalog, 1);
	catalog->entries = g_ptr_array_new_with_free_func(app_entry_free);
	GList *all_apps = g_app_info_get_all();
	for (GList *link = all_apps; link != NULL; link = link->next) {
		if (!G_IS_DESKTOP_APP_INFO(link->data)) {
			continue;
		}
		seyrix_app_entry *entry = app_entry_new(
			G_DESKTOP_APP_INFO(link->data));
		if (entry != NULL) {
			g_ptr_array_add(catalog->entries, entry);
		}
	}
	g_list_free_full(all_apps, g_object_unref);
	g_ptr_array_sort(catalog->entries, compare_entries);
	return catalog;
}

void seyrix_app_catalog_free(seyrix_app_catalog *catalog) {
	if (catalog == NULL) {
		return;
	}
	g_ptr_array_unref(catalog->entries);
	g_free(catalog);
}

gsize seyrix_app_catalog_count(const seyrix_app_catalog *catalog) {
	return catalog != NULL ? catalog->entries->len : 0;
}

const seyrix_app_entry *seyrix_app_catalog_get(
		const seyrix_app_catalog *catalog, gsize index) {
	if (catalog == NULL || index >= catalog->entries->len) {
		return NULL;
	}
	return g_ptr_array_index(catalog->entries, index);
}

const gchar *seyrix_app_entry_id(const seyrix_app_entry *entry) {
	return entry != NULL ? entry->id : NULL;
}

const gchar *seyrix_app_entry_name(const seyrix_app_entry *entry) {
	return entry != NULL ? entry->name : NULL;
}

const gchar *seyrix_app_entry_icon(const seyrix_app_entry *entry) {
	return entry != NULL ? entry->icon : NULL;
}

GPtrArray *seyrix_app_catalog_search(const seyrix_app_catalog *catalog,
		const gchar *query) {
	GPtrArray *matches = g_ptr_array_new();
	if (catalog == NULL) {
		return matches;
	}
	if (query == NULL || *query == '\0') {
		for (guint i = 0; i < catalog->entries->len; i++) {
			g_ptr_array_add(matches, g_ptr_array_index(catalog->entries, i));
		}
		return matches;
	}

	gchar *valid_query = g_utf8_make_valid(query, -1);
	gchar *folded_query = g_utf8_casefold(valid_query, -1);
	g_free(valid_query);
	if (folded_query == NULL) {
		return matches;
	}
	for (guint i = 0; i < catalog->entries->len; i++) {
		seyrix_app_entry *entry = g_ptr_array_index(catalog->entries, i);
		if (strstr(entry->folded_name, folded_query) != NULL ||
				strstr(entry->folded_id, folded_query) != NULL) {
			g_ptr_array_add(matches, entry);
		}
	}
	g_free(folded_query);
	return matches;
}

gboolean seyrix_app_entry_launch(const seyrix_app_entry *entry,
		GList *files, GAppLaunchContext *context, GError **error) {
	if (entry == NULL || entry->app_info == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"No desktop application was selected");
		return FALSE;
	}
	if (geteuid() == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"Refusing to launch desktop applications as root");
		return FALSE;
	}
	if (!g_app_info_should_show(G_APP_INFO(entry->app_info)) ||
			!app_has_available_try_exec(entry->app_info)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"The desktop application is hidden or its TryExec is unavailable");
		return FALSE;
	}
	/* GAppInfo parses and launches the desktop entry; never invoke a shell. */
	return g_app_info_launch(G_APP_INFO(entry->app_info), files, context, error);
}

