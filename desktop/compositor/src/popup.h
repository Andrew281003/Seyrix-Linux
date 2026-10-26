#ifndef SEYRIX_POPUP_H
#define SEYRIX_POPUP_H

#include "seyrix.h"

bool seyrix_popup_init(struct seyrix_server *server);
void seyrix_popup_finish(struct seyrix_server *server);
void seyrix_popup_dismiss_window(struct seyrix_window *window);
void seyrix_popup_dismiss_layer_surface(struct wlr_layer_surface_v1 *surface);
void seyrix_popup_attach_layer_surface(struct seyrix_server *server,
	struct wlr_layer_surface_v1 *surface, struct wlr_xdg_popup *popup);

#endif
