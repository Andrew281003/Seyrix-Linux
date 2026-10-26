#ifndef SEYRIX_DECORATION_H
#define SEYRIX_DECORATION_H

#include "seyrix.h"

bool seyrix_decoration_init(struct seyrix_server *server);
void seyrix_decoration_finish(struct seyrix_server *server);
void seyrix_decoration_window_destroy(struct seyrix_window *window);
int seyrix_decoration_titlebar_height(const struct seyrix_window *window);
void seyrix_decoration_set_fullscreen(struct seyrix_window *window, bool value);
void seyrix_decoration_set_focused(struct seyrix_window *window, bool focused);
bool seyrix_decoration_pointer_button(struct seyrix_server *server,
	double x, double y, uint32_t button, uint32_t state);

#endif
