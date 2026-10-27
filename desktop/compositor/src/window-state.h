#ifndef SEYRIX_WINDOW_STATE_H
#define SEYRIX_WINDOW_STATE_H

#include "seyrix.h"

bool seyrix_window_state_attach(struct seyrix_window *window);
void seyrix_window_state_destroy(struct seyrix_window *window);
void seyrix_window_state_initial_commit(struct seyrix_window *window);
void seyrix_window_state_commit(struct seyrix_window *window);
void seyrix_window_state_reconfigure(struct seyrix_window *window);
void seyrix_window_state_set_maximized(struct seyrix_window *window, bool value);
void seyrix_window_state_toggle_maximized(struct seyrix_window *window);
void seyrix_window_state_set_fullscreen(struct seyrix_window *window, bool value);
void seyrix_window_state_minimize(struct seyrix_window *window);
bool seyrix_window_state_restore_minimized(struct seyrix_server *server);
bool seyrix_window_state_restore_minimized_window(struct seyrix_window *window);
void seyrix_window_state_outputs_changed(struct seyrix_server *server);
void seyrix_window_state_output_removed(struct seyrix_server *server,
	struct wlr_output *output);

#endif
