#ifndef SEYRIX_SESSION_SUPERVISOR_WAYLAND_PROBE_H
#define SEYRIX_SESSION_SUPERVISOR_WAYLAND_PROBE_H

#include <stdbool.h>

struct wl_display;

typedef bool (*seyrix_wayland_control_handler)(void *data);

bool seyrix_wayland_sync_wait(struct wl_display *display, int control_fd,
		unsigned int timeout_ms,
		seyrix_wayland_control_handler control_handler, void *control_data);

#endif
