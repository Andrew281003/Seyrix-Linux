#ifndef SEYRIX_SETTINGS_WINDOW_H
#define SEYRIX_SETTINGS_WINDOW_H

#include <gtk/gtk.h>

#include "settings-client.h"

struct seyrix_settings_window;

struct seyrix_settings_window *seyrix_settings_window_create(
	GtkApplication *application, struct seyrix_settings_client *client);
void seyrix_settings_window_present(struct seyrix_settings_window *window);
void seyrix_settings_window_refresh(struct seyrix_settings_window *window);
void seyrix_settings_window_destroy(struct seyrix_settings_window *window);

#endif
