#ifndef SEYRIX_SETTINGS_WINDOW_H
#define SEYRIX_SETTINGS_WINDOW_H

#include <gtk/gtk.h>

#include "settings-client.h"

struct seyrix_settings_window;

struct seyrix_settings_window_state {
	guint category;
	int width;
	int height;
	int paned_position;
	gboolean maximized;
};

typedef void (*seyrix_settings_window_state_changed)(gpointer data);

struct seyrix_settings_window *seyrix_settings_window_create(
	GtkApplication *application, struct seyrix_settings_client *client,
	const struct seyrix_settings_window_state *state,
	seyrix_settings_window_state_changed state_changed, gpointer state_data);
void seyrix_settings_window_present(struct seyrix_settings_window *window);
void seyrix_settings_window_refresh(struct seyrix_settings_window *window);
void seyrix_settings_window_get_state(struct seyrix_settings_window *window,
	struct seyrix_settings_window_state *state);
const char *seyrix_settings_window_category_id(
	struct seyrix_settings_window *window);
void seyrix_settings_window_destroy(struct seyrix_settings_window *window);

#endif
