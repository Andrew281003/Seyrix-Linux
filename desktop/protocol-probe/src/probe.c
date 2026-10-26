#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"

enum surface_kind { SURFACE_TOPLEVEL, SURFACE_POPUP, SURFACE_NESTED_POPUP };

struct probe;
struct probe_surface {
	struct probe *probe;
	enum surface_kind kind;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct xdg_popup *popup;
	struct zxdg_toplevel_decoration_v1 *decoration;
	int width;
	int height;
	struct probe_surface *parent;
};

struct probe_buffer {
	struct wl_list link;
	struct wl_buffer *buffer;
	void *mapping;
	size_t size;
	int fd;
};

struct probe {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct zxdg_decoration_manager_v1 *decoration_manager;
	struct wl_list buffers;
	struct probe_surface toplevel;
	struct probe_surface *root_popup;
	struct probe_surface *nested_popup;
	struct probe_surface *pointer_surface;
	FILE *log;
	int cycle_step;
	int initial_width;
	int initial_height;
	int pointer_x;
	int pointer_y;
	bool cycle;
	bool rapid_state;
	bool destroy_after_initial;
	bool decoration_check;
	bool request_client_decoration;
	bool decoration_server_side;
	bool decoration_client_side;
	bool resize_check;
	bool watch_pointer;
	bool resize_requested;
	bool resize_saw_active;
	bool observed_resizing;
	bool min_size_set;
	bool max_size_set;
	uint32_t resize_edges;
	const char *resize_edge_name;
	const char *title;
	const char *app_id;
	int min_width;
	int min_height;
	int max_width;
	int max_height;
	int resize_before_width;
	int resize_before_height;
	unsigned int pointer_enter_count;
	unsigned int pointer_motion_count;
	unsigned int pointer_button_count;
	char generated_title[128];
	char generated_app_id[96];
	bool started;
	bool done;
	bool failed;
};

static volatile sig_atomic_t stop_requested;

static void handle_stop_signal(int signal_number) {
	(void)signal_number;
	stop_requested = 1;
}

static void log_event(struct probe *probe, const char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(probe->log, format, args);
	fputc('\n', probe->log);
	fflush(probe->log);
	va_end(args);
}

static const char *surface_name(enum surface_kind kind) {
	switch (kind) {
	case SURFACE_TOPLEVEL: return "toplevel";
	case SURFACE_POPUP: return "popup";
	case SURFACE_NESTED_POPUP: return "nested-popup";
	}
	return "unknown";
}

static bool parse_size(const char *value, int *width, int *height) {
	char *width_end = NULL;
	char *height_end = NULL;
	errno = 0;
	long parsed_width = strtol(value, &width_end, 10);
	if (errno != 0 || width_end == value ||
			(*width_end != 'x' && *width_end != 'X')) {
		return false;
	}
	errno = 0;
	long parsed_height = strtol(width_end + 1, &height_end, 10);
	if (errno != 0 || height_end == width_end + 1 || *height_end != '\0' ||
			parsed_width < 1 || parsed_height < 1 ||
			parsed_width > 8192 || parsed_height > 8192) {
		return false;
	}
	*width = (int)parsed_width;
	*height = (int)parsed_height;
	return true;
}

static bool parse_resize_edge(const char *value, uint32_t *edges,
		const char **name) {
	if (strcmp(value, "top") == 0) {
		*edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP;
	} else if (strcmp(value, "bottom") == 0) {
		*edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
	} else if (strcmp(value, "left") == 0) {
		*edges = XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
	} else if (strcmp(value, "right") == 0) {
		*edges = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
	} else if (strcmp(value, "top-left") == 0) {
		*edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
	} else if (strcmp(value, "top-right") == 0) {
		*edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
	} else if (strcmp(value, "bottom-left") == 0) {
		*edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
	} else if (strcmp(value, "bottom-right") == 0) {
		*edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
	} else {
		return false;
	}
	*name = value;
	return true;
}

static int constrain_dimension(int value, int minimum, int maximum) {
	if (minimum > 0 && value < minimum) {
		value = minimum;
	}
	if (maximum > 0 && value > maximum) {
		value = maximum;
	}
	return value;
}

static bool prepare_initial_size(struct probe *probe) {
	if (probe->initial_width <= 0) {
		probe->initial_width = 440;
	}
	if (probe->initial_height <= 0) {
		probe->initial_height = 320;
	}
	if ((probe->min_size_set && probe->max_size_set) &&
			(probe->min_width > probe->max_width ||
			probe->min_height > probe->max_height)) {
		return false;
	}
	probe->initial_width = constrain_dimension(probe->initial_width,
		probe->min_size_set ? probe->min_width : 0,
		probe->max_size_set ? probe->max_width : 0);
	probe->initial_height = constrain_dimension(probe->initial_height,
		probe->min_size_set ? probe->min_height : 0,
		probe->max_size_set ? probe->max_height : 0);
	return true;
}

static bool has_state(const struct wl_array *states, uint32_t wanted) {
	uint32_t *state;
	wl_array_for_each(state, states) {
		if (*state == wanted) return true;
	}
	return false;
}

static void buffer_release(void *data, struct wl_buffer *buffer) {
	struct probe_buffer *entry = data;
	wl_buffer_destroy(buffer);
	wl_list_remove(&entry->link);
	munmap(entry->mapping, entry->size);
	close(entry->fd);
	free(entry);
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release,
};

static uint32_t color_for(enum surface_kind kind, int x, int y) {
	uint32_t base = kind == SURFACE_TOPLEVEL ? 0x00304050 :
		kind == SURFACE_POPUP ? 0x00505030 : 0x00305050;
	if (y < 24) return 0x007090b0;
	if (kind == SURFACE_TOPLEVEL && x >= 20 && x < 180 && y >= 48 && y < 112) {
		return ((x / 8 + y / 8) & 1) ? 0x00a06040 : 0x00d09050;
	}
	if (kind != SURFACE_TOPLEVEL && x < 36 && y < 36) {
		return ((x / 6 + y / 6) & 1) ? 0x00b06040 : 0x00e09050;
	}
	return base;
}

static bool attach_buffer(struct probe_surface *target, int width, int height) {
	struct probe *probe = target->probe;
	if (width <= 0) width = target->width > 0 ? target->width :
		(target->kind == SURFACE_TOPLEVEL ? 440 : 180);
	if (height <= 0) height = target->height > 0 ? target->height :
		(target->kind == SURFACE_TOPLEVEL ? 320 : 120);
	if (width < 1 || height < 1 || width > 8192 || height > 8192) return false;

	size_t size = (size_t)width * (size_t)height * sizeof(uint32_t);
	char path[] = "/tmp/seyrix-probe-XXXXXX";
	int fd = mkstemp(path);
	if (fd < 0) return false;
	unlink(path);
	if (ftruncate(fd, (off_t)size) != 0) {
		close(fd);
		return false;
	}
	void *mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		close(fd);
		return false;
	}
	uint32_t *pixels = mapping;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			pixels[(size_t)y * (size_t)width + (size_t)x] =
				color_for(target->kind, x, y);
		}
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(probe->shm, fd, (int32_t)size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
		width * (int)sizeof(uint32_t), WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	if (buffer == NULL) {
		munmap(mapping, size);
		close(fd);
		return false;
	}
	struct probe_buffer *entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		wl_buffer_destroy(buffer);
		munmap(mapping, size);
		close(fd);
		return false;
	}
	entry->buffer = buffer;
	entry->mapping = mapping;
	entry->size = size;
	entry->fd = fd;
	wl_buffer_add_listener(buffer, &buffer_listener, entry);
	wl_list_insert(&probe->buffers, &entry->link);
	wl_surface_attach(target->surface, buffer, 0, 0);
	wl_surface_damage_buffer(target->surface, 0, 0, width, height);
	wl_surface_commit(target->surface);
	target->width = width;
	target->height = height;
	log_event(probe, "COMMIT surface=%s width=%d height=%d",
		surface_name(target->kind), width, height);
	return true;
}

static bool observed_maximized;
static bool observed_fullscreen;
static bool observed_state;

static void request_rapid_state_transition(struct probe *probe) {
	struct xdg_toplevel *top = probe->toplevel.toplevel;
	switch (probe->cycle_step) {
	case 1:
		log_event(probe, "RAPID_REQUEST_MAXIMIZE=1");
		xdg_toplevel_set_maximized(top);
		break;
	case 2:
		log_event(probe, "RAPID_REQUEST_MAXIMIZE=0");
		xdg_toplevel_unset_maximized(top);
		break;
	case 3:
		log_event(probe, "RAPID_REQUEST_FULLSCREEN=1");
		xdg_toplevel_set_fullscreen(top, NULL);
		break;
	case 4:
		log_event(probe, "RAPID_REQUEST_FULLSCREEN=0");
		xdg_toplevel_unset_fullscreen(top);
		break;
	default:
		return;
	}
	wl_surface_commit(probe->toplevel.surface);
}

static void drive_rapid_state_after_configure(struct probe *probe) {
	if (!probe->rapid_state || probe->done || !probe->started ||
			!observed_state || probe->cycle_step < 1 || probe->cycle_step > 4) {
		return;
	}
	bool expected_maximized = probe->cycle_step == 1;
	bool expected_fullscreen = probe->cycle_step == 3;
	if (observed_maximized != expected_maximized ||
			observed_fullscreen != expected_fullscreen) {
		log_event(probe,
			"RAPID_STATE_STALE_CONFIGURE step=%d maximized=%d fullscreen=%d",
			probe->cycle_step, observed_maximized, observed_fullscreen);
		observed_state = false;
		return;
	}
	log_event(probe,
		"RAPID_STATE_CONFIGURE_ACK_COMMIT=passed step=%d maximized=%d fullscreen=%d width=%d height=%d",
		probe->cycle_step, observed_maximized, observed_fullscreen,
		probe->toplevel.width, probe->toplevel.height);
	if (probe->cycle_step == 4) {
		log_event(probe, "RAPID_STATE_CYCLE=passed");
		probe->done = true;
	} else {
		probe->cycle_step++;
		request_rapid_state_transition(probe);
	}
	observed_state = false;
}

static void drive_resize_after_configure(struct probe *probe,
		uint32_t serial) {
	if (!probe->resize_check || probe->done || !probe->resize_requested ||
			!observed_state) {
		return;
	}
	log_event(probe,
		"RESIZE_CONFIGURE_ACK_COMMIT serial=%u resizing=%d width=%d height=%d",
		serial, probe->observed_resizing, probe->toplevel.width,
		probe->toplevel.height);
	observed_state = false;
	if (probe->observed_resizing) {
		probe->resize_saw_active = true;
		return;
	}
	if (!probe->resize_saw_active) {
		return;
	}
	probe->resize_saw_active = false;
	bool changed = probe->resize_before_width != probe->toplevel.width ||
		probe->resize_before_height != probe->toplevel.height;
	log_event(probe,
		"RESIZE_GEOMETRY edge=%s before=%dx%d after=%dx%d changed=%d",
		probe->resize_edge_name, probe->resize_before_width,
		probe->resize_before_height, probe->toplevel.width,
		probe->toplevel.height, changed);
	probe->done = true;
	if (probe->toplevel.width <= 0 || probe->toplevel.height <= 0 ||
			(!changed && !probe->min_size_set && !probe->max_size_set)) {
		log_event(probe,
			"RESIZE_COMPLETE=failed edge=%s reason=%s",
			probe->resize_edge_name,
			changed ? "invalid-committed-geometry" : "geometry-unchanged");
		probe->failed = true;
		return;
	}
	log_event(probe, "RESIZE_COMPLETE=passed edge=%s",
		probe->resize_edge_name);
}

static void drive_after_configure(struct probe *probe) {
	if (!probe->cycle || probe->done || !probe->started) return;
	struct xdg_toplevel *top = probe->toplevel.toplevel;
	switch (probe->cycle_step) {
	case 1:
		if (observed_state && observed_maximized) {
			probe->cycle_step = 2;
			log_event(probe, "REQUEST_MAXIMIZE=0");
			xdg_toplevel_unset_maximized(top);
			wl_surface_commit(probe->toplevel.surface);
		}
		break;
	case 2:
		if (observed_state && !observed_maximized) {
			if (probe->toplevel.width != probe->initial_width ||
					probe->toplevel.height != probe->initial_height) {
					log_event(probe, "RESTORE_GEOMETRY=failed initial=%dx%d restored=%dx%d",
						probe->initial_width, probe->initial_height,
						probe->toplevel.width, probe->toplevel.height);
					probe->failed = true;
					probe->done = true;
					return;
			}
			log_event(probe, "RESTORE_GEOMETRY=passed width=%d height=%d",
					probe->toplevel.width, probe->toplevel.height);
			probe->cycle_step = 3;
			log_event(probe, "REQUEST_FULLSCREEN=1");
			xdg_toplevel_set_fullscreen(top, NULL);
			wl_surface_commit(probe->toplevel.surface);
		}
		break;
	case 3:
		if (observed_state && observed_fullscreen) {
			probe->cycle_step = 4;
			log_event(probe, "REQUEST_FULLSCREEN=0");
			xdg_toplevel_unset_fullscreen(top);
			wl_surface_commit(probe->toplevel.surface);
		}
		break;
	case 4:
		if (observed_state && !observed_fullscreen) {
			if (probe->toplevel.width != probe->initial_width ||
					probe->toplevel.height != probe->initial_height) {
				log_event(probe, "RESTORE_AFTER_FULLSCREEN=failed initial=%dx%d restored=%dx%d",
					probe->initial_width, probe->initial_height,
					probe->toplevel.width, probe->toplevel.height);
				probe->failed = true;
				probe->done = true;
				return;
			}
			log_event(probe, "RESTORE_AFTER_FULLSCREEN=passed width=%d height=%d",
				probe->toplevel.width, probe->toplevel.height);
			if (!probe->decoration_server_side) {
				log_event(probe, "DECORATION_ASSERTION=failed expected=server_side");
				probe->failed = true;
				probe->done = true;
				return;
			}
			log_event(probe, "DECORATION_ASSERTION=passed mode=server_side");
			log_event(probe, "STATE_CYCLE=passed");
			probe->done = true;
		}
		break;
	default:
		break;
	}
	observed_state = false;
}

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	struct probe *probe = data;
	observed_maximized = has_state(states, XDG_TOPLEVEL_STATE_MAXIMIZED);
	observed_fullscreen = has_state(states, XDG_TOPLEVEL_STATE_FULLSCREEN);
	probe->observed_resizing = has_state(states, XDG_TOPLEVEL_STATE_RESIZING);
	observed_state = true;
	log_event(probe,
		"CONFIGURE_TOPLEVEL width=%d height=%d maximized=%d fullscreen=%d resizing=%d",
		width, height, observed_maximized, observed_fullscreen,
		probe->observed_resizing);
	if (width > 0) {
		probe->toplevel.width = constrain_dimension(width,
			probe->min_size_set ? probe->min_width : 0,
			probe->max_size_set ? probe->max_width : 0);
	}
	if (height > 0) {
		probe->toplevel.height = constrain_dimension(height,
			probe->min_size_set ? probe->min_height : 0,
			probe->max_size_set ? probe->max_height : 0);
	}
	(void)toplevel;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
	struct probe *probe = data;
	log_event(probe, "CLOSE_REQUEST=received");
	if (probe->resize_check && !probe->done) {
		log_event(probe, "RESIZE_COMPLETE=failed reason=close-request");
		probe->failed = true;
	}
	probe->done = true;
	(void)toplevel;
}

static void xdg_toplevel_configure_bounds(void *data,
		struct xdg_toplevel *toplevel, int32_t width, int32_t height) {
	struct probe *probe = data;
	log_event(probe, "CONFIGURE_BOUNDS width=%d height=%d", width, height);
	(void)toplevel;
}

static void xdg_toplevel_wm_capabilities(void *data,
		struct xdg_toplevel *toplevel, struct wl_array *capabilities) {
	struct probe *probe = data;
	uint32_t *capability;
	wl_array_for_each(capability, capabilities) {
		log_event(probe, "WM_CAPABILITY=%u", *capability);
	}
	(void)toplevel;
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
	.configure_bounds = xdg_toplevel_configure_bounds,
	.wm_capabilities = xdg_toplevel_wm_capabilities,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
		uint32_t serial) {
	struct probe_surface *target = data;
	struct probe *probe = target->probe;
	xdg_surface_ack_configure(surface, serial);
	log_event(probe, "ACK_CONFIGURE surface=%s serial=%u",
		surface_name(target->kind), serial);
	if (!attach_buffer(target, target->width, target->height)) {
		log_event(probe, "BUFFER_COMMIT=failed surface=%s", surface_name(target->kind));
		probe->failed = true;
		probe->done = true;
		return;
	}
	if (target->kind == SURFACE_TOPLEVEL && observed_state) {
		log_event(probe,
			"TOPLEVEL_ACK_COMMIT=passed serial=%u maximized=%d fullscreen=%d width=%d height=%d",
			serial, observed_maximized, observed_fullscreen,
			target->width, target->height);
	}

	if (target->kind == SURFACE_TOPLEVEL && !probe->started) {
		probe->started = true;
		if (probe->destroy_after_initial) {
			log_event(probe, "DESTROY_DURING_TRANSITION=begin");
			xdg_toplevel_set_maximized(target->toplevel);
			wl_surface_commit(target->surface);
			xdg_toplevel_destroy(target->toplevel);
			xdg_surface_destroy(target->xdg_surface);
			wl_surface_destroy(target->surface);
			target->toplevel = NULL;
			target->xdg_surface = NULL;
			target->surface = NULL;
			log_event(probe, "DESTROY_DURING_TRANSITION=sent");
			probe->done = true;
			return;
		}
		if (probe->cycle) {
			probe->initial_width = target->width;
			probe->initial_height = target->height;
			probe->cycle_step = 1;
			log_event(probe, "REQUEST_MAXIMIZE=1");
			xdg_toplevel_set_maximized(target->toplevel);
			wl_surface_commit(target->surface);
		} else if (probe->resize_check) {
			probe->resize_before_width = target->width;
			probe->resize_before_height = target->height;
			log_event(probe,
				"RESIZE_READY=1 pid=%ld surface=%u app_id=%s title=%s initial_size=%dx%d edge=%s",
				(long)getpid(), wl_proxy_get_id((struct wl_proxy *)target->xdg_surface),
				probe->app_id, probe->title, target->width, target->height,
				probe->resize_edge_name);
		} else if (probe->watch_pointer) {
			log_event(probe,
				"WATCH_POINTER_READY pid=%ld surface=%u app_id=%s title=%s initial_size=%dx%d",
				(long)getpid(), wl_proxy_get_id((struct wl_proxy *)target->xdg_surface),
				probe->app_id, probe->title, target->width, target->height);
		} else if (probe->rapid_state) {
			log_event(probe, "RAPID_STATE_REQUESTS=begin");
			observed_state = false;
			probe->cycle_step = 1;
			request_rapid_state_transition(probe);
		} else if (probe->decoration_check) {
			bool matches = probe->request_client_decoration ?
				probe->decoration_client_side : probe->decoration_server_side;
			if (matches) {
				log_event(probe, "DECORATION_ASSERTION=passed mode=%s",
					probe->request_client_decoration ? "client_side" : "server_side");
				probe->done = true;
			}
		}
	} else if (target->kind == SURFACE_TOPLEVEL && probe->cycle) {
		drive_after_configure(probe);
	} else if (target->kind == SURFACE_TOPLEVEL && probe->rapid_state) {
		drive_rapid_state_after_configure(probe);
	} else if (target->kind == SURFACE_TOPLEVEL && probe->resize_check) {
		drive_resize_after_configure(probe, serial);
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void decoration_configure(void *data,
		struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode) {
	struct probe *probe = data;
	const char *name = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE ?
		"server_side" : mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE ?
		"client_side" : "none";
	log_event(probe, "DECORATION_MODE=%s", name);
	probe->decoration_server_side =
		mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	probe->decoration_client_side =
		mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	if (probe->decoration_check && probe->started) {
		bool matches = probe->request_client_decoration ?
			probe->decoration_client_side : probe->decoration_server_side;
		if (matches) {
			log_event(probe, "DECORATION_ASSERTION=passed mode=%s",
				probe->request_client_decoration ? "client_side" : "server_side");
			probe->done = true;
		} else {
			log_event(probe, "DECORATION_ASSERTION=failed requested=%s",
				probe->request_client_decoration ? "client_side" : "server_side");
			probe->failed = true;
			probe->done = true;
		}
	}
	(void)decoration;
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
	.configure = decoration_configure,
};

static void popup_configure(void *data, struct xdg_popup *popup, int32_t x,
		int32_t y, int32_t width, int32_t height) {
	struct probe_surface *target = data;
	target->width = width;
	target->height = height;
	log_event(target->probe,
		"POPUP_CONFIGURE kind=%s x=%d y=%d width=%d height=%d",
		surface_name(target->kind), x, y, width, height);
	(void)popup;
}

static void popup_done(void *data, struct xdg_popup *popup) {
	struct probe_surface *target = data;
	struct probe *probe = target->probe;
	log_event(probe, "POPUP_DONE kind=%s", surface_name(target->kind));
	if (probe->pointer_surface == target) probe->pointer_surface = NULL;
	if (probe->nested_popup == target) probe->nested_popup = NULL;
	if (probe->root_popup == target) probe->root_popup = NULL;
	xdg_popup_destroy(popup);
	xdg_surface_destroy(target->xdg_surface);
	wl_surface_destroy(target->surface);
	free(target);
}

static void popup_repositioned(void *data, struct xdg_popup *popup,
		uint32_t token) {
	struct probe_surface *target = data;
	log_event(target->probe, "POPUP_REPOSITIONED kind=%s token=%u",
		surface_name(target->kind), token);
	(void)popup;
}

static const struct xdg_popup_listener popup_listener = {
	.configure = popup_configure,
	.popup_done = popup_done,
	.repositioned = popup_repositioned,
};

static struct probe_surface *create_popup(struct probe *probe,
		struct probe_surface *parent, enum surface_kind kind,
		int anchor_x, int anchor_y, uint32_t serial) {
	struct probe_surface *target = calloc(1, sizeof(*target));
	if (target == NULL) return NULL;
	target->probe = probe;
	target->kind = kind;
	target->parent = parent;
	target->surface = wl_compositor_create_surface(probe->compositor);
	target->xdg_surface = xdg_wm_base_get_xdg_surface(probe->wm_base,
		target->surface);
	if (target->surface == NULL || target->xdg_surface == NULL) {
		free(target);
		return NULL;
	}
	wl_surface_set_user_data(target->surface, target);
	xdg_surface_add_listener(target->xdg_surface, &xdg_surface_listener, target);
	struct xdg_positioner *positioner = xdg_wm_base_create_positioner(probe->wm_base);
	int popup_width = kind == SURFACE_POPUP ? 180 : 132;
	int popup_height = kind == SURFACE_POPUP ? 120 : 84;
	xdg_positioner_set_size(positioner, popup_width, popup_height);
	xdg_positioner_set_anchor_rect(positioner, anchor_x, anchor_y, 28, 24);
	xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
	xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
	xdg_positioner_set_constraint_adjustment(positioner,
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y |
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
	target->popup = xdg_surface_get_popup(target->xdg_surface,
		parent->xdg_surface, positioner);
	xdg_positioner_destroy(positioner);
	if (target->popup == NULL) {
		xdg_surface_destroy(target->xdg_surface);
		wl_surface_destroy(target->surface);
		free(target);
		return NULL;
	}
	xdg_popup_add_listener(target->popup, &popup_listener, target);
	xdg_popup_grab(target->popup, probe->seat, serial);
	wl_surface_commit(target->surface);
	log_event(probe, "POPUP_CREATE kind=%s serial=%u",
		surface_name(kind), serial);
	return target;
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	struct probe *probe = data;
	probe->pointer_surface = wl_surface_get_user_data(surface);
	probe->pointer_x = wl_fixed_to_int(sx);
	probe->pointer_y = wl_fixed_to_int(sy);
	if (probe->watch_pointer && probe->pointer_surface == &probe->toplevel) {
		probe->pointer_enter_count++;
		log_event(probe,
			"WATCH_POINTER_ENTER count=%u serial=%u surface=toplevel x=%d y=%d",
			probe->pointer_enter_count, serial, probe->pointer_x,
			probe->pointer_y);
	}
	(void)pointer;
	(void)sx;
	(void)sy;
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface) {
	struct probe *probe = data;
	if (probe->watch_pointer && probe->pointer_surface == &probe->toplevel) {
		log_event(probe, "WATCH_POINTER_LEAVE serial=%u surface=toplevel", serial);
	}
	probe->pointer_surface = NULL;
	(void)pointer;
	(void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
	struct probe *probe = data;
	probe->pointer_x = wl_fixed_to_int(sx);
	probe->pointer_y = wl_fixed_to_int(sy);
	if (probe->watch_pointer && probe->pointer_surface == &probe->toplevel) {
		probe->pointer_motion_count++;
		log_event(probe,
			"WATCH_POINTER_MOTION count=%u surface=toplevel x=%d y=%d",
			probe->pointer_motion_count, probe->pointer_x,
			probe->pointer_y);
	}
	(void)pointer;
	(void)time;
	(void)sx;
	(void)sy;
}

static void pointer_button(void *data, struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct probe *probe = data;
	if (probe->watch_pointer && probe->pointer_surface == &probe->toplevel) {
		probe->pointer_button_count++;
		log_event(probe,
			"WATCH_POINTER_BUTTON count=%u surface=toplevel serial=%u button=%u state=%s x=%d y=%d",
			probe->pointer_button_count, serial, button,
			state == WL_POINTER_BUTTON_STATE_PRESSED ? "pressed" : "released",
			probe->pointer_x, probe->pointer_y);
	}
	if (probe->resize_check && !probe->resize_requested &&
			probe->pointer_surface == &probe->toplevel &&
			button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (probe->seat == NULL || probe->toplevel.toplevel == NULL) {
			log_event(probe, "RESIZE_REQUEST=failed reason=missing-seat-or-toplevel");
			probe->failed = true;
			probe->done = true;
			return;
		}
		probe->resize_requested = true;
		log_event(probe,
			"RESIZE_POINTER_PRESS serial=%u surface=toplevel x=%d y=%d",
			serial, probe->pointer_x, probe->pointer_y);
		log_event(probe, "RESIZE_REQUEST edge=%s serial=%u",
			probe->resize_edge_name, serial);
		xdg_toplevel_resize(probe->toplevel.toplevel, probe->seat,
			serial, probe->resize_edges);
		return;
	}
	if (probe->watch_pointer) {
		return;
	}
	if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED ||
			probe->pointer_surface == NULL || probe->seat == NULL) return;
	struct probe_surface *target = probe->pointer_surface;
	if (target->kind == SURFACE_TOPLEVEL && probe->root_popup == NULL) {
		probe->root_popup = create_popup(probe, target, SURFACE_POPUP,
			probe->pointer_x, probe->pointer_y, serial);
	} else if (target->kind == SURFACE_POPUP && probe->nested_popup == NULL) {
		probe->nested_popup = create_popup(probe, target, SURFACE_NESTED_POPUP,
		probe->pointer_x, probe->pointer_y, serial);
	}
	(void)pointer;
	(void)time;
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
	(void)data;
	(void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
		uint32_t source) {
	(void)data;
	(void)pointer;
	(void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
		uint32_t time, uint32_t axis) {
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
		uint32_t axis, int32_t discrete) {
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer,
		uint32_t axis, int32_t value120) {
	(void)data;
	(void)pointer;
	(void)axis;
	(void)value120;
}

static void pointer_axis_relative_direction(void *data,
		struct wl_pointer *pointer, uint32_t axis, uint32_t direction) {
	(void)data;
	(void)pointer;
	(void)axis;
	(void)direction;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
	.axis_value120 = pointer_axis_value120,
	.axis_relative_direction = pointer_axis_relative_direction,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
		uint32_t capabilities) {
	struct probe *probe = data;
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && probe->pointer == NULL) {
		probe->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(probe->pointer, &pointer_listener, probe);
	} else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0 && probe->pointer != NULL) {
		wl_pointer_destroy(probe->pointer);
		probe->pointer = NULL;
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void wm_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_listener = {
	.ping = wm_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct probe *probe = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		probe->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, version < 4 ? version : 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		probe->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		probe->wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, version < 6 ? version : 6);
		xdg_wm_base_add_listener(probe->wm_base, &wm_listener, probe);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		probe->seat = wl_registry_bind(registry, name, &wl_seat_interface,
			version < 7 ? version : 7);
		wl_seat_add_listener(probe->seat, &seat_listener, probe);
	} else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
		probe->decoration_manager = wl_registry_bind(registry, name,
			&zxdg_decoration_manager_v1_interface,
			version < 1 ? version : 1);
	}
}

static void registry_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_remove,
};

static bool create_toplevel(struct probe *probe) {
	struct probe_surface *target = &probe->toplevel;
	target->probe = probe;
	target->kind = SURFACE_TOPLEVEL;
	target->width = probe->initial_width;
	target->height = probe->initial_height;
	target->surface = wl_compositor_create_surface(probe->compositor);
	if (target->surface == NULL) return false;
	wl_surface_set_user_data(target->surface, target);
	target->xdg_surface = xdg_wm_base_get_xdg_surface(probe->wm_base,
		target->surface);
	if (target->xdg_surface == NULL) return false;
	xdg_surface_add_listener(target->xdg_surface, &xdg_surface_listener, target);
	target->toplevel = xdg_surface_get_toplevel(target->xdg_surface);
	if (target->toplevel == NULL) return false;
	xdg_toplevel_add_listener(target->toplevel, &toplevel_listener, probe);
	const char *title = probe->title != NULL ? probe->title :
		"Seyrix Phase 2C Protocol Probe";
	const char *app_id = probe->app_id != NULL ? probe->app_id :
		"seyrix.phase2c-probe";
	xdg_toplevel_set_title(target->toplevel, title);
	xdg_toplevel_set_app_id(target->toplevel, app_id);
	if (probe->min_size_set) {
		xdg_toplevel_set_min_size(target->toplevel, probe->min_width,
			probe->min_height);
	}
	if (probe->max_size_set) {
		xdg_toplevel_set_max_size(target->toplevel, probe->max_width,
			probe->max_height);
	}
	if (probe->decoration_manager != NULL) {
		target->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
			probe->decoration_manager, target->toplevel);
		zxdg_toplevel_decoration_v1_add_listener(target->decoration,
			&decoration_listener, probe);
		uint32_t mode = probe->request_client_decoration ?
			ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE :
			ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
		zxdg_toplevel_decoration_v1_set_mode(target->decoration, mode);
		log_event(probe, "DECORATION_REQUEST=%s",
			probe->request_client_decoration ? "client_side" : "server_side");
	} else {
		log_event(probe, "DECORATION_REQUEST=unavailable");
	}
	wl_surface_commit(target->surface);
	log_event(probe, "TOPLEVEL_CREATED=1");
	return true;
}

static void destroy_roles(struct probe_surface *target) {
	if (target->popup != NULL) {
		xdg_popup_destroy(target->popup);
		target->popup = NULL;
	}
	if (target->decoration != NULL) {
		zxdg_toplevel_decoration_v1_destroy(target->decoration);
		target->decoration = NULL;
	}
	if (target->toplevel != NULL) {
		xdg_toplevel_destroy(target->toplevel);
		target->toplevel = NULL;
	}
	if (target->xdg_surface != NULL) {
		xdg_surface_destroy(target->xdg_surface);
		target->xdg_surface = NULL;
	}
	if (target->surface != NULL) {
		wl_surface_destroy(target->surface);
		target->surface = NULL;
	}
}

static void usage(const char *program) {
	fprintf(stderr,
		"Usage: %s [--state-cycle | --rapid-state | --client-decoration | --destroy-during-transition | --resize EDGE | --watch-pointer] [--initial-size WxH] [--min-size WxH] [--max-size WxH] [--title TEXT] [--app-id ID] [--log PATH]\n",
		program);
}

static void prepare_identity(struct probe *probe) {
	if (probe->title == NULL && probe->resize_check) {
		snprintf(probe->generated_title, sizeof(probe->generated_title),
			"Seyrix Resize Probe %s", probe->resize_edge_name);
		probe->title = probe->generated_title;
	} else if (probe->title == NULL && probe->watch_pointer) {
		probe->title = "Seyrix Pointer Watch Probe";
	}
	if (probe->app_id == NULL && probe->resize_check) {
		snprintf(probe->generated_app_id, sizeof(probe->generated_app_id),
			"seyrix.resize-probe.%s", probe->resize_edge_name);
		probe->app_id = probe->generated_app_id;
	} else if (probe->app_id == NULL && probe->watch_pointer) {
		probe->app_id = "seyrix.pointer-watch-probe";
	}
}

int main(int argc, char **argv) {
	struct probe probe = {0};
	const char *log_path = getenv("SEYRIX_PROBE_LOG");
	if (log_path == NULL) log_path = "/tmp/seyrix-protocol-probe.log";
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--state-cycle") == 0) {
			probe.cycle = true;
		} else if (strcmp(argv[i], "--rapid-state") == 0) {
			probe.rapid_state = true;
		} else if (strcmp(argv[i], "--client-decoration") == 0) {
			probe.decoration_check = true;
			probe.request_client_decoration = true;
		} else if (strcmp(argv[i], "--destroy-during-transition") == 0) {
			probe.destroy_after_initial = true;
		} else if (strcmp(argv[i], "--resize") == 0 && i + 1 < argc) {
			probe.resize_check = true;
			if (!parse_resize_edge(argv[++i], &probe.resize_edges,
					&probe.resize_edge_name)) {
				usage(argv[0]);
				return 2;
			}
		} else if (strcmp(argv[i], "--watch-pointer") == 0) {
			probe.watch_pointer = true;
		} else if (strcmp(argv[i], "--initial-size") == 0 && i + 1 < argc) {
			if (!parse_size(argv[++i], &probe.initial_width,
					&probe.initial_height)) {
				usage(argv[0]);
				return 2;
			}
		} else if (strcmp(argv[i], "--min-size") == 0 && i + 1 < argc) {
			probe.min_size_set = true;
			if (!parse_size(argv[++i], &probe.min_width, &probe.min_height)) {
				usage(argv[0]);
				return 2;
			}
		} else if (strcmp(argv[i], "--max-size") == 0 && i + 1 < argc) {
			probe.max_size_set = true;
			if (!parse_size(argv[++i], &probe.max_width, &probe.max_height)) {
				usage(argv[0]);
				return 2;
			}
		} else if (strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
			probe.title = argv[++i];
		} else if (strcmp(argv[i], "--app-id") == 0 && i + 1 < argc) {
			probe.app_id = argv[++i];
		} else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
			log_path = argv[++i];
		} else {
			usage(argv[0]);
			return 2;
		}
	}
	int mode_count = (int)probe.cycle + (int)probe.rapid_state +
		(int)probe.destroy_after_initial + (int)probe.decoration_check +
		(int)probe.resize_check + (int)probe.watch_pointer;
	if (mode_count > 1 || !prepare_initial_size(&probe)) {
		usage(argv[0]);
		return 2;
	}
	prepare_identity(&probe);
	probe.log = fopen(log_path, "w");
	if (probe.log == NULL) {
		fprintf(stderr, "cannot open %s: %s\n", log_path, strerror(errno));
		return 1;
	}
	setvbuf(probe.log, NULL, _IOLBF, 0);
	wl_list_init(&probe.buffers);
	const char *mode = probe.cycle ? "state-cycle" : probe.rapid_state ?
		"rapid-state" : probe.decoration_check ? "decoration" :
		probe.destroy_after_initial ? "destroy-during-transition" :
		probe.resize_check ? "resize" : probe.watch_pointer ?
		"watch-pointer" : "interactive";
	log_event(&probe, "PROBE_START mode=%s", mode);
	if (probe.watch_pointer) {
		signal(SIGTERM, handle_stop_signal);
	}

	probe.display = wl_display_connect(NULL);
	if (probe.display == NULL) {
		log_event(&probe, "CONNECT=failed");
		fclose(probe.log);
		return 1;
	}
	probe.registry = wl_display_get_registry(probe.display);
	wl_registry_add_listener(probe.registry, &registry_listener, &probe);
	if (wl_display_roundtrip(probe.display) < 0 || probe.compositor == NULL ||
			probe.shm == NULL || probe.wm_base == NULL ||
			((probe.resize_check || probe.watch_pointer) &&
			(probe.seat == NULL || probe.pointer == NULL))) {
		log_event(&probe, "GLOBALS=failed");
		probe.failed = true;
		probe.done = true;
	} else {
		log_event(&probe, "GLOBALS=ready decoration=%d seat=%d",
			probe.decoration_manager != NULL, probe.seat != NULL);
		if (!create_toplevel(&probe)) {
			log_event(&probe, "TOPLEVEL_CREATED=failed");
			probe.failed = true;
			probe.done = true;
		}
	}

	uint64_t deadline_ms = 0;
	if (probe.cycle || probe.rapid_state || probe.decoration_check ||
			probe.destroy_after_initial || probe.resize_check) {
		deadline_ms = 120000;
	}
	uint64_t elapsed_ms = 0;
	while (!probe.done && !(probe.watch_pointer && stop_requested)) {
		if (wl_display_dispatch_pending(probe.display) < 0) {
			log_event(&probe, "WAYLAND_DISCONNECT=dispatch-pending");
			probe.failed = true;
			break;
		}
		if (wl_display_flush(probe.display) < 0 && errno != EAGAIN) {
			log_event(&probe, "WAYLAND_DISCONNECT=flush errno=%d", errno);
			probe.failed = true;
			break;
		}
		struct pollfd pfd = {.fd = wl_display_get_fd(probe.display), .events = POLLIN};
		int timeout = 1000;
		int ready = poll(&pfd, 1, timeout);
		elapsed_ms += timeout;
		if (ready < 0 && errno != EINTR) {
			log_event(&probe, "POLL_FAILED errno=%d", errno);
			probe.failed = true;
			break;
		}
		if (ready > 0 && wl_display_dispatch(probe.display) < 0) {
			log_event(&probe, "WAYLAND_DISCONNECT=dispatch");
			probe.failed = true;
			break;
		}
		if (deadline_ms != 0 && elapsed_ms >= deadline_ms) {
			log_event(&probe, "PROBE_TIMEOUT=1");
			probe.failed = true;
			break;
		}
	}
	if (probe.watch_pointer && stop_requested) {
		log_event(&probe, "WATCH_POINTER_STOPPED=1");
		probe.done = true;
	}
	if (probe.resize_check && !probe.done) {
		log_event(&probe, "RESIZE_COMPLETE=failed reason=%s",
			probe.failed ? "timeout-or-protocol-error" : "display-disconnected");
		probe.failed = true;
	}

	/* Active popup roles are defunct only after popup_done; disconnect cleans
	 * any still-live popup proxies without sending invalid destroy requests. */
	destroy_roles(&probe.toplevel);
	if (probe.pointer != NULL) wl_pointer_destroy(probe.pointer);
	if (probe.seat != NULL) wl_seat_destroy(probe.seat);
	if (probe.decoration_manager != NULL) zxdg_decoration_manager_v1_destroy(probe.decoration_manager);
	if (probe.wm_base != NULL) xdg_wm_base_destroy(probe.wm_base);
	if (probe.shm != NULL) wl_shm_destroy(probe.shm);
	if (probe.compositor != NULL) wl_compositor_destroy(probe.compositor);
	if (probe.registry != NULL) wl_registry_destroy(probe.registry);
	if (probe.display != NULL) {
		wl_display_flush(probe.display);
		wl_display_disconnect(probe.display);
	}
	free(probe.root_popup);
	free(probe.nested_popup);
	log_event(&probe, "PROBE_EXIT result=%s", probe.failed ? "failed" : "passed");
	fclose(probe.log);
	return probe.failed ? 1 : 0;
}
