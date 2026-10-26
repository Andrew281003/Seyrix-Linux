#ifndef SEYRIX_LAYER_SHELL_H
#define SEYRIX_LAYER_SHELL_H

#include "seyrix.h"

bool seyrix_layer_shell_init(struct seyrix_server *server);
void seyrix_layer_shell_finish(struct seyrix_server *server);
void seyrix_layer_shell_outputs_changed(struct seyrix_server *server);
void seyrix_layer_shell_output_removed(struct seyrix_server *server,
	struct wlr_output *output);
void seyrix_layer_shell_output_work_area(struct seyrix_server *server,
	struct wlr_output *output, struct wlr_box *work_area);
bool seyrix_layer_shell_surface_at(struct seyrix_server *server,
	double x, double y, struct wlr_surface **surface,
	double *surface_x, double *surface_y);
void seyrix_layer_shell_focus_keyboard(struct seyrix_server *server,
	struct wlr_surface *surface);

#endif
