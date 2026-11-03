#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "../../common/shell-ipc.h"
#include "app-catalog.h"
#include "shell-config.h"
#include "shell-state.h"
#include "shell-ui.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

enum { PANEL_MIN_HEIGHT = 42, MANUAL_MAX_BYTES = 65536 };

struct shell_client;
struct shell_output;

struct shell_buffer {
	struct wl_list link;
	struct wl_buffer *buffer;
	void *mapping;
	size_t size;
};

struct shell_surface {
	struct shell_client *client;
	struct shell_output *output;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	enum seyrix_shell_view_kind kind;
	int width;
	int height;
	int scale;
	bool configured;
	double pointer_x;
	double pointer_y;
	struct seyrix_shell_hit_area hits[SEYRIX_SHELL_HIT_MAX];
	size_t hit_count;
	size_t application_offset;
};

struct shell_output {
	struct wl_list link;
	struct shell_client *client;
	uint32_t registry_name;
	struct wl_output *output;
	int mode_width;
	int mode_height;
	int scale;
	struct shell_surface *desktop;
	struct shell_surface *panel;
};

struct shell_client {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_registry *registry;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_list outputs;
	struct wl_list buffers;
	struct shell_output *active_output;
	struct shell_surface *pointer_surface;
	struct shell_surface *overlay;
	struct wl_surface *keyboard_surface;
	int status_fd;
	uint8_t active_workspace;
	struct seyrix_shell_state model;
	struct seyrix_shell_config config;
	seyrix_app_catalog *catalog;
	GPtrArray *search_results;
	char query[512];
	size_t selected_index;
	char *manual_text;
	int manual_scroll;
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;
	bool running;
};

static const struct zwlr_layer_surface_v1_listener layer_listener;
static const struct wl_seat_listener seat_listener;

static struct seyrix_shell_packet_header make_header(uint8_t type,
		size_t size) {
	return (struct seyrix_shell_packet_header){
		.version = SEYRIX_SHELL_IPC_VERSION,
		.type = type,
		.size = (uint16_t)size,
	};
}

static bool send_packet(struct shell_client *client, const void *packet,
		size_t size) {
	if (client->status_fd < 0) return false;
	ssize_t sent = send(client->status_fd, packet, size,
		MSG_DONTWAIT | MSG_NOSIGNAL);
	if (sent == (ssize_t)size) return true;
	if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
		(void)close(client->status_fd);
		client->status_fd = -1;
	}
	return false;
}

static bool send_action(struct shell_client *client, uint8_t action) {
	struct seyrix_shell_packet_action packet = {
		.header = make_header(action, sizeof(packet)),
	};
	return send_packet(client, &packet, sizeof(packet));
}

static bool send_workspace(struct shell_client *client, uint8_t workspace) {
	if (workspace >= SEYRIX_SHELL_WORKSPACE_COUNT) return false;
	struct seyrix_shell_packet_workspace packet = {
		.header = make_header(SEYRIX_SHELL_SELECT_WORKSPACE, sizeof(packet)),
		.workspace = workspace,
	};
	return send_packet(client, &packet, sizeof(packet));
}

static bool send_window_id(struct shell_client *client, uint8_t type,
		uint32_t id) {
	if (id == 0) return false;
	struct seyrix_shell_packet_id packet = {
		.header = make_header(type, sizeof(packet)),
		.id = id,
	};
	return send_packet(client, &packet, sizeof(packet));
}

static void buffer_release(void *data, struct wl_buffer *buffer) {
	struct shell_buffer *record = data;
	wl_list_remove(&record->link);
	wl_buffer_destroy(buffer);
	munmap(record->mapping, record->size);
	free(record);
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release,
};

static struct shell_surface *surface_for_wl_surface(struct shell_client *client,
		struct wl_surface *surface) {
	struct shell_output *output;
	wl_list_for_each(output, &client->outputs, link) {
		if (output->desktop != NULL && output->desktop->surface == surface)
			return output->desktop;
		if (output->panel != NULL && output->panel->surface == surface)
			return output->panel;
	}
	if (client->overlay != NULL && client->overlay->surface == surface)
		return client->overlay;
	return NULL;
}

static void destroy_surface(struct shell_surface *surface) {
	if (surface == NULL) return;
	struct shell_client *client = surface->client;
	if (client->pointer_surface == surface) client->pointer_surface = NULL;
	if (client->keyboard_surface == surface->surface) client->keyboard_surface = NULL;
	if (surface->output != NULL) {
		if (surface->output->desktop == surface) surface->output->desktop = NULL;
		if (surface->output->panel == surface) surface->output->panel = NULL;
	}
	if (client->overlay == surface) {
		client->overlay = NULL;
		client->keyboard_surface = NULL;
	}
	if (surface->layer_surface != NULL)
		zwlr_layer_surface_v1_destroy(surface->layer_surface);
	if (surface->surface != NULL) wl_surface_destroy(surface->surface);
	free(surface);
}

static const seyrix_app_entry *catalog_entry_by_id(
		const struct shell_client *client, const char *desktop_id) {
	if (client->catalog == NULL || desktop_id == NULL) return NULL;
	for (gsize index = 0; index < seyrix_app_catalog_count(client->catalog); index++) {
		const seyrix_app_entry *entry = seyrix_app_catalog_get(client->catalog, index);
		const char *id = seyrix_app_entry_id(entry);
		if (id != NULL && strcmp(id, desktop_id) == 0) return entry;
	}
	return NULL;
}

static void update_search(struct shell_client *client) {
	if (client->search_results != NULL) g_ptr_array_unref(client->search_results);
	client->search_results = seyrix_app_catalog_search(client->catalog,
		client->query);
	if (client->selected_index >= client->search_results->len)
		client->selected_index = 0;
}

static char *load_manual(void) {
	const char *path = "/usr/share/seyrix/Manual.txt";
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return strdup("Manual.txt is not installed.\n\nThe shell could not open the local manual resource.");
	struct stat status;
	if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
			status.st_size < 0 || status.st_size > MANUAL_MAX_BYTES) {
		close(fd);
		return strdup("Manual.txt could not be read: the file is not a regular, bounded resource.");
	}
	size_t capacity = (size_t)status.st_size;
	char *contents = malloc(capacity + 1);
	if (contents == NULL) {
		close(fd);
		return strdup("Manual.txt could not be read: insufficient memory.");
	}
	size_t used = 0;
	while (used < capacity) {
		ssize_t count = read(fd, contents + used, capacity - used);
		if (count < 0 && errno == EINTR) continue;
		if (count <= 0) break;
		used += (size_t)count;
	}
	close(fd);
	contents[used] = '\0';
	return contents;
}

static struct shell_surface *create_layer_surface(struct shell_client *client,
		struct shell_output *output, enum seyrix_shell_view_kind kind) {
	struct shell_surface *surface = calloc(1, sizeof(*surface));
	if (surface == NULL) return NULL;
	surface->client = client;
	surface->output = output;
	surface->kind = kind;
	surface->scale = output != NULL ? output->scale : 1;
	if (surface->scale < 1 || surface->scale > 4) surface->scale = 1;
	surface->surface = wl_compositor_create_surface(client->compositor);
	if (surface->surface == NULL) {
		free(surface);
		return NULL;
	}
	enum zwlr_layer_shell_v1_layer layer;
	const char *namespace_name;
	uint32_t anchor;
	uint32_t desired_height = 0;
	int32_t exclusive_zone = 0;
	uint32_t keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
	switch (kind) {
	case SEYRIX_SHELL_VIEW_DESKTOP:
		layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
		namespace_name = "seyrix-desktop";
		anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		break;
	case SEYRIX_SHELL_VIEW_PANEL:
		layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
		namespace_name = "seyrix-panel";
		anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		desired_height = (uint32_t)(client->config.icon_size + 18);
		if (client->config.position == SEYRIX_SHELL_POSITION_TOP) {
			anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
		} else {
			anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
		}
		exclusive_zone = (int32_t)desired_height;
		break;
	case SEYRIX_SHELL_VIEW_LAUNCHER:
		layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
		namespace_name = "seyrix-launcher";
		anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		exclusive_zone = -1;
		keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
		break;
	case SEYRIX_SHELL_VIEW_SWITCHER:
		layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
		namespace_name = "seyrix-switcher";
		anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		exclusive_zone = -1;
		keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
		break;
	case SEYRIX_SHELL_VIEW_MANUAL:
		layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
		namespace_name = "seyrix-manual";
		anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		exclusive_zone = -1;
		keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
		break;
	default:
		free(surface);
		return NULL;
	}
	surface->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		client->layer_shell, surface->surface,
		output != NULL ? output->output : NULL, layer, namespace_name);
	if (surface->layer_surface == NULL || zwlr_layer_surface_v1_add_listener(
			surface->layer_surface, &layer_listener, surface) != 0) {
		destroy_surface(surface);
		return NULL;
	}
	zwlr_layer_surface_v1_set_anchor(surface->layer_surface, anchor);
	zwlr_layer_surface_v1_set_size(surface->layer_surface, 0, desired_height);
	zwlr_layer_surface_v1_set_exclusive_zone(surface->layer_surface, exclusive_zone);
	zwlr_layer_surface_v1_set_keyboard_interactivity(surface->layer_surface,
		keyboard);
	wl_surface_commit(surface->surface);
	return surface;
}

static void set_surface_input_region(struct shell_surface *surface) {
	if (surface == NULL || surface->surface == NULL) return;
	struct wl_region *region = wl_compositor_create_region(surface->client->compositor);
	if (region == NULL) return;
	if (surface->kind == SEYRIX_SHELL_VIEW_LAUNCHER ||
			surface->kind == SEYRIX_SHELL_VIEW_SWITCHER ||
			surface->kind == SEYRIX_SHELL_VIEW_MANUAL) {
		wl_region_add(region, 0, 0, surface->width, surface->height);
		} else {
		for (size_t index = 0; index < surface->hit_count; index++) {
		const struct seyrix_shell_hit_area *hit = &surface->hits[index];
		wl_region_add(region, hit->x, hit->y, hit->width, hit->height);
	}
		}
	wl_surface_set_input_region(surface->surface, region);
	wl_region_destroy(region);
}

static bool draw_surface(struct shell_surface *surface) {
	if (surface == NULL || !surface->configured || surface->width <= 0 ||
			surface->height <= 0) return false;
	if (surface->scale < 1 || surface->scale > 4) surface->scale = 1;
	if (surface->width > INT32_MAX / surface->scale ||
			surface->height > INT32_MAX / surface->scale) return false;
	int pixel_width = surface->width * surface->scale;
	int pixel_height = surface->height * surface->scale;
	if (pixel_width > INT32_MAX / 4) return false;
	int32_t stride = pixel_width * 4;
	if ((size_t)stride > SIZE_MAX / (size_t)pixel_height) return false;
	size_t size = (size_t)stride * (size_t)pixel_height;
	if (size > 256U * 1024U * 1024U || size > INT32_MAX) return false;
	int fd = memfd_create("seyrix-shell", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
		if (fd >= 0) close(fd);
		return false;
	}
	void *mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		close(fd);
		return false;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(surface->client->shm,
		fd, (int32_t)size);
	if (pool == NULL) {
		munmap(mapping, size);
		close(fd);
		return false;
	}
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, pixel_width,
		pixel_height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	if (buffer == NULL) {
		munmap(mapping, size);
		return false;
	}
	struct shell_buffer *record = calloc(1, sizeof(*record));
	if (record == NULL) {
		wl_buffer_destroy(buffer);
		munmap(mapping, size);
		return false;
	}
	record->buffer = buffer;
	record->mapping = mapping;
	record->size = size;
	wl_list_insert(&surface->client->buffers, &record->link);
	if (wl_buffer_add_listener(buffer, &buffer_listener, record) != 0) {
		wl_list_remove(&record->link);
		wl_buffer_destroy(buffer);
		munmap(mapping, size);
		free(record);
		return false;
	}
	cairo_surface_t *image = cairo_image_surface_create_for_data(mapping,
		CAIRO_FORMAT_ARGB32, pixel_width, pixel_height, stride);
	if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(image);
		wl_list_remove(&record->link);
		wl_buffer_destroy(buffer);
		munmap(mapping, size);
		free(record);
		return false;
	}
	cairo_t *cr = cairo_create(image);
	if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
		cairo_destroy(cr);
		cairo_surface_destroy(image);
		wl_list_remove(&record->link);
		wl_buffer_destroy(buffer);
		munmap(mapping, size);
		free(record);
		return false;
	}
	struct shell_client *client = surface->client;
	struct seyrix_shell_view view = {
		.kind = surface->kind,
		.width = surface->width,
		.height = surface->height,
		.scale = surface->scale,
		.config = &client->config,
		.state = &client->model,
		.catalog = client->catalog,
		.query = client->query,
		.search_results = client->search_results,
		.selected_index = client->selected_index,
		.manual_text = client->manual_text,
		.manual_scroll = client->manual_scroll,
		.application_offset = surface->application_offset,
	};
	seyrix_shell_ui_draw(&view, cr, surface->hits,
		SEYRIX_SHELL_HIT_MAX, &surface->hit_count);
	cairo_destroy(cr);
	cairo_surface_flush(image);
	cairo_surface_destroy(image);
	set_surface_input_region(surface);
	if (wl_proxy_get_version((struct wl_proxy *)surface->surface) >= 3)
		wl_surface_set_buffer_scale(surface->surface, surface->scale);
	wl_surface_attach(surface->surface, buffer, 0, 0);
	wl_surface_damage(surface->surface, 0, 0, surface->width, surface->height);
	wl_surface_commit(surface->surface);
	return true;
}

static void handle_layer_configure(void *data,
		struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	struct shell_surface *surface = data;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
	if (width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX) return;
	surface->width = (int)width;
	surface->height = (int)height;
	surface->configured = true;
	(void)draw_surface(surface);
}

static void handle_layer_closed(void *data,
		struct zwlr_layer_surface_v1 *layer_surface) {
	(void)layer_surface;
	destroy_surface(data);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = handle_layer_configure,
	.closed = handle_layer_closed,
};

static void draw_output(struct shell_output *output) {
	if (output->desktop != NULL) (void)draw_surface(output->desktop);
	if (output->panel != NULL) (void)draw_surface(output->panel);
}

static void create_output_surfaces(struct shell_output *output) {
	struct shell_client *client = output->client;
	if (output->desktop == NULL) {
		output->desktop = create_layer_surface(client, output,
			SEYRIX_SHELL_VIEW_DESKTOP);
	}
	if (output->panel == NULL) {
		output->panel = create_layer_surface(client, output,
			SEYRIX_SHELL_VIEW_PANEL);
	}
}

static void output_geometry(void *data, struct wl_output *proxy,
		int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
		int32_t subpixel, const char *make, const char *model, int32_t transform) {
	(void)proxy; (void)x; (void)y; (void)physical_width; (void)physical_height;
	(void)subpixel; (void)make; (void)model; (void)transform;
	(void)data;
}

static void output_mode(void *data, struct wl_output *proxy, uint32_t flags,
		int32_t width, int32_t height, int32_t refresh) {
	(void)proxy; (void)refresh;
	struct shell_output *output = data;
	if ((flags & WL_OUTPUT_MODE_CURRENT) != 0 && width > 0 && height > 0) {
		output->mode_width = width;
		output->mode_height = height;
	}
}

static void output_done(void *data, struct wl_output *proxy) {
	(void)proxy;
	draw_output(data);
}

static void output_scale(void *data, struct wl_output *proxy, int32_t factor) {
	(void)proxy;
	struct shell_output *output = data;
	output->scale = factor >= 1 && factor <= 4 ? factor : 1;
	if (output->desktop != NULL) output->desktop->scale = output->scale;
	if (output->panel != NULL) output->panel->scale = output->scale;
	draw_output(output);
}

static void output_name(void *data, struct wl_output *proxy, const char *name) {
	(void)data; (void)proxy; (void)name;
}

static void output_description(void *data, struct wl_output *proxy,
		const char *description) {
	(void)data; (void)proxy; (void)description;
}

static const struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
	.name = output_name,
	.description = output_description,
};

static void destroy_output(struct shell_output *output) {
	struct shell_client *client = output->client;
	if (client->overlay != NULL && client->overlay->output == output) {
		destroy_surface(client->overlay);
		(void)send_action(client, SEYRIX_SHELL_CLOSE_OVERLAY);
	}
	if (output->desktop != NULL) destroy_surface(output->desktop);
	if (output->panel != NULL) destroy_surface(output->panel);
	if (client->active_output == output) client->active_output = NULL;
	wl_list_remove(&output->link);
	wl_output_destroy(output->output);
	free(output);
}

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct shell_client *client = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0 &&
			client->compositor == NULL) {
		client->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, version < 4 ? version : 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0 && client->shm == NULL) {
		client->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0 && client->seat == NULL) {
		client->seat = wl_registry_bind(registry, name, &wl_seat_interface,
			version < 4 ? version : 4);
		if (client->seat != NULL)
			(void)wl_seat_add_listener(client->seat, &seat_listener, client);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0 &&
			client->layer_shell == NULL) {
		client->layer_shell = wl_registry_bind(registry, name,
			&zwlr_layer_shell_v1_interface, version < 4 ? version : 4);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct shell_output *output = calloc(1, sizeof(*output));
		if (output == NULL) return;
		output->client = client;
		output->registry_name = name;
		output->scale = 1;
		output->output = wl_registry_bind(registry, name, &wl_output_interface,
			version < 4 ? version : 4);
		if (output->output == NULL || wl_output_add_listener(output->output,
				&output_listener, output) != 0) {
			if (output->output != NULL) wl_output_destroy(output->output);
			free(output);
			return;
		}
		wl_list_insert(&client->outputs, &output->link);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	(void)registry;
	struct shell_client *client = data;
	struct shell_output *output, *temporary;
	wl_list_for_each_safe(output, temporary, &client->outputs, link) {
		if (output->registry_name == name) {
			destroy_output(output);
			break;
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
	(void)pointer; (void)serial;
	struct shell_client *client = data;
	client->pointer_surface = surface_for_wl_surface(client, surface);
	if (client->pointer_surface != NULL) {
		client->active_output = client->pointer_surface->output;
		client->pointer_surface->pointer_x = wl_fixed_to_double(x);
		client->pointer_surface->pointer_y = wl_fixed_to_double(y);
	}
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface) {
	(void)pointer; (void)serial; (void)surface;
	((struct shell_client *)data)->pointer_surface = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	(void)pointer; (void)time;
	struct shell_client *client = data;
	if (client->pointer_surface != NULL) {
		client->pointer_surface->pointer_x = wl_fixed_to_double(x);
		client->pointer_surface->pointer_y = wl_fixed_to_double(y);
	}
}

static void launch_entry(struct shell_client *client, const char *desktop_id) {
	const seyrix_app_entry *entry = catalog_entry_by_id(client, desktop_id);
	if (entry == NULL) return;
	GError *error = NULL;
	if (!seyrix_app_entry_launch(entry, NULL, NULL, &error)) {
		g_warning("Could not launch %s: %s", seyrix_app_entry_name(entry),
			error != NULL ? error->message : "desktop entry launch failed");
		g_clear_error(&error);
		return;
	}
	if (client->overlay != NULL)
		(void)send_action(client, SEYRIX_SHELL_CLOSE_OVERLAY);
}

static void close_overlay_from_user(struct shell_client *client);

static const struct seyrix_shell_hit_area *hit_at(
		const struct shell_surface *surface, double x, double y) {
	for (size_t index = 0; index < surface->hit_count; index++) {
		const struct seyrix_shell_hit_area *hit = &surface->hits[index];
		if (x >= hit->x && x < hit->x + hit->width &&
				y >= hit->y && y < hit->y + hit->height) return hit;
	}
	return NULL;
}

static void pointer_button(void *data, struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	(void)pointer; (void)serial; (void)time;
	struct shell_client *client = data;
	struct shell_surface *surface = client->pointer_surface;
	if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED ||
			surface == NULL) return;
	const struct seyrix_shell_hit_area *hit = hit_at(surface,
		surface->pointer_x, surface->pointer_y);
	if (hit == NULL) {
		if (surface == client->overlay) close_overlay_from_user(client);
		return;
	}
	switch (hit->kind) {
	case SEYRIX_SHELL_HIT_MANUAL:
		(void)send_action(client, SEYRIX_SHELL_REQUEST_MANUAL);
		break;
	case SEYRIX_SHELL_HIT_WORKSPACE:
		(void)send_workspace(client, (uint8_t)hit->id);
		break;
	case SEYRIX_SHELL_HIT_LAUNCHER:
		(void)send_action(client, SEYRIX_SHELL_REQUEST_LAUNCHER);
		break;
	case SEYRIX_SHELL_HIT_APPLICATION:
		launch_entry(client, hit->desktop_id);
		break;
	case SEYRIX_SHELL_HIT_WINDOW:
		(void)send_window_id(client, surface->kind == SEYRIX_SHELL_VIEW_SWITCHER ?
			SEYRIX_SHELL_ACTIVATE_WINDOW : SEYRIX_SHELL_REQUEST_FOCUS, hit->id);
		break;
	case SEYRIX_SHELL_HIT_OVERFLOW: {
		int slot = client->config.mode == SEYRIX_SHELL_MODE_DOCK ?
			client->config.icon_size + 18 : 152;
		int page = (surface->width - 240) / (slot > 0 ? slot : 1);
		if (page < 1) page = 1;
		surface->application_offset += (size_t)page;
		(void)draw_surface(surface);
		break;
	}
	case SEYRIX_SHELL_HIT_NONE:
	default:
		break;
	}
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	(void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
	(void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
		uint32_t source) {
	(void)data; (void)pointer; (void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
		uint32_t time, uint32_t axis) {
	(void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
		uint32_t axis, int32_t discrete) {
	(void)data; (void)pointer; (void)axis; (void)discrete;
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer,
		uint32_t axis, int32_t value120) {
	(void)data; (void)pointer; (void)axis; (void)value120;
}

#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
static void pointer_axis_relative_direction(void *data,
		struct wl_pointer *pointer, uint32_t axis, uint32_t direction) {
	(void)data; (void)pointer; (void)axis; (void)direction;
}
#endif

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
#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
	.axis_relative_direction = pointer_axis_relative_direction,
#endif
};

static void close_overlay_from_user(struct shell_client *client) {
	if (client->overlay != NULL) {
		(void)send_action(client, SEYRIX_SHELL_CLOSE_OVERLAY);
	}
}

static size_t switcher_ids(struct shell_client *client, uint32_t *ids,
		size_t capacity) {
	if (client->config.switcher_active_workspace_only)
		return seyrix_shell_state_switcher_windows(&client->model, ids, capacity);
	bool used[SEYRIX_SHELL_MAX_WINDOWS] = {false};
	size_t count = 0;
	while (count < capacity && count < client->model.window_count) {
		size_t best = SIZE_MAX;
		for (size_t index = 0; index < client->model.window_count; index++) {
			const struct seyrix_shell_window *window = &client->model.windows[index];
			if (used[index] || (window->flags & SEYRIX_SHELL_WINDOW_MAPPED) == 0 ||
					(window->flags & SEYRIX_SHELL_WINDOW_MINIMIZED) != 0) continue;
			if (best == SIZE_MAX || window->mru_order >
					client->model.windows[best].mru_order) best = index;
		}
		if (best == SIZE_MAX) break;
		used[best] = true;
		ids[count++] = client->model.windows[best].id;
	}
	return count;
}

static uint32_t switcher_selected_id(struct shell_client *client) {
	uint32_t ids[SEYRIX_SHELL_MAX_WINDOWS];
	size_t count = switcher_ids(client, ids, SEYRIX_SHELL_MAX_WINDOWS);
	if (count == 0) return 0;
	if (client->selected_index >= count) client->selected_index = 0;
	return ids[client->selected_index];
}

static void commit_switcher(struct shell_client *client) {
	uint32_t selected_id = switcher_selected_id(client);
	if (selected_id == 0) {
		(void)send_action(client, SEYRIX_SHELL_CLOSE_OVERLAY);
		return;
	}
	(void)send_window_id(client, SEYRIX_SHELL_ACTIVATE_WINDOW, selected_id);
}

static void refresh_visible(void *data) {
	struct shell_client *client = data;
	if (client->overlay != NULL) (void)draw_surface(client->overlay);
	struct shell_output *output;
	wl_list_for_each(output, &client->outputs, link) {
		if (output->panel != NULL) (void)draw_surface(output->panel);
	}
}

static void handle_keyboard_keymap(void *data, struct wl_keyboard *keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
	(void)keyboard;
	struct shell_client *client = data;
	if (client->xkb_context == NULL ||
			format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
		close(fd);
		return;
	}
	char *mapping = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (mapping == MAP_FAILED) return;
	struct xkb_keymap *keymap = xkb_keymap_new_from_string(client->xkb_context,
		mapping, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(mapping, size);
	if (keymap == NULL) return;
	struct xkb_state *key_state = xkb_state_new(keymap);
	if (key_state == NULL) {
		xkb_keymap_unref(keymap);
		return;
	}
	if (client->xkb_state != NULL) xkb_state_unref(client->xkb_state);
	if (client->xkb_keymap != NULL) xkb_keymap_unref(client->xkb_keymap);
	client->xkb_keymap = keymap;
	client->xkb_state = key_state;
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	(void)keyboard; (void)serial; (void)keys;
	struct shell_client *client = data;
	client->keyboard_surface = surface;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, struct wl_surface *surface) {
	(void)keyboard; (void)serial;
	struct shell_client *client = data;
	if (client->overlay != NULL && client->overlay->surface == surface) {
		/* Focus moved to another surface. Discard the saved restore target. */
		(void)send_action(client, SEYRIX_SHELL_DISMISS_OVERLAY);
		destroy_surface(client->overlay);
	}
	if (client->keyboard_surface == surface) client->keyboard_surface = NULL;
}

static void append_utf8(char *buffer, size_t capacity, const char *text_value) {
	size_t current = strlen(buffer);
	size_t length = strlen(text_value);
	if (current + length >= capacity) return;
	memcpy(buffer + current, text_value, length + 1);
}

static void query_backspace(char *query) {
	size_t length = strlen(query);
	if (length == 0) return;
	length--;
	while (length > 0 && (((unsigned char)query[length] & 0xc0U) == 0x80U)) length--;
	query[length] = '\0';
}

static void keyboard_press(struct shell_client *client,
		struct xkb_keymap *keymap, xkb_keysym_t symbol, uint32_t key) {
	(void)keymap;
	if (client->overlay == NULL) return;
	if (symbol == XKB_KEY_Escape) {
		close_overlay_from_user(client);
		return;
	}
	switch (client->overlay->kind) {
	case SEYRIX_SHELL_VIEW_LAUNCHER:
		if (symbol == XKB_KEY_Return || symbol == XKB_KEY_KP_Enter) {
			if (client->search_results != NULL &&
					client->selected_index < client->search_results->len) {
				const seyrix_app_entry *entry = g_ptr_array_index(
					client->search_results, (guint)client->selected_index);
				GError *error = NULL;
				if (!seyrix_app_entry_launch(entry, NULL, NULL, &error)) {
					g_warning("Could not launch %s: %s", seyrix_app_entry_name(entry),
						error != NULL ? error->message : "desktop entry launch failed");
					g_clear_error(&error);
				} else {
					close_overlay_from_user(client);
				}
			}
			return;
		}
		if (symbol == XKB_KEY_Up) {
			if (client->selected_index > 0) client->selected_index--;
			refresh_visible(client);
			return;
		}
		if (symbol == XKB_KEY_Down) {
			if (client->search_results != NULL &&
					client->selected_index + 1 < client->search_results->len)
				client->selected_index++;
			refresh_visible(client);
			return;
		}
		if (symbol == XKB_KEY_BackSpace) {
			query_backspace(client->query);
			client->selected_index = 0;
			update_search(client);
			refresh_visible(client);
			return;
		}
		break;
	case SEYRIX_SHELL_VIEW_SWITCHER: {
		if (symbol == XKB_KEY_Return || symbol == XKB_KEY_KP_Enter) {
			commit_switcher(client);
			return;
		}
		uint32_t ids[SEYRIX_SHELL_MAX_WINDOWS];
		size_t count = switcher_ids(client, ids, SEYRIX_SHELL_MAX_WINDOWS);
		bool backwards = client->xkb_state != NULL &&
			xkb_state_mod_name_is_active(client->xkb_state, XKB_MOD_NAME_SHIFT,
				XKB_STATE_MODS_EFFECTIVE) > 0;
		if (symbol == XKB_KEY_Tab || symbol == XKB_KEY_Down ||
			symbol == XKB_KEY_Right) {
			if (count > 0) client->selected_index = backwards ?
				(client->selected_index == 0 ? count - 1 : client->selected_index - 1) :
				(client->selected_index + 1) % count;
			refresh_visible(client);
			return;
		}
		if (symbol == XKB_KEY_Up || symbol == XKB_KEY_Left) {
			if (count > 0) client->selected_index = client->selected_index == 0 ?
				count - 1 : client->selected_index - 1;
			refresh_visible(client);
			return;
		}
		(void)key;
		return;
	}
	case SEYRIX_SHELL_VIEW_MANUAL:
		if (symbol == XKB_KEY_Page_Down || symbol == XKB_KEY_Down)
			client->manual_scroll += 360;
		else if (symbol == XKB_KEY_Page_Up || symbol == XKB_KEY_Up)
			client->manual_scroll = client->manual_scroll > 360 ?
				client->manual_scroll - 360 : 0;
		else if (symbol == XKB_KEY_Home)
			client->manual_scroll = 0;
		else if (symbol == XKB_KEY_End)
			client->manual_scroll += 2000;
		refresh_visible(client);
		return;
	case SEYRIX_SHELL_VIEW_DESKTOP:
	case SEYRIX_SHELL_VIEW_PANEL:
	default:
		return;
	}
	char utf8[16];
	int length = xkb_keysym_to_utf8(symbol, utf8, sizeof(utf8));
	if (length > 1 && (unsigned char)utf8[0] >= 0x20U && utf8[0] != 0x7f) {
		append_utf8(client->query, sizeof(client->query), utf8);
		client->selected_index = 0;
		update_search(client);
		refresh_visible(client);
	}
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	(void)keyboard; (void)serial; (void)time;
	struct shell_client *client = data;
	if (client->xkb_state == NULL) return;
	xkb_keycode_t code = key + 8;
	xkb_state_update_key(client->xkb_state, code,
		state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED || client->overlay == NULL ||
			client->keyboard_surface != client->overlay->surface) return;
	xkb_keysym_t symbol = xkb_state_key_get_one_sym(client->xkb_state, code);
	keyboard_press(client, client->xkb_keymap, symbol, key);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t depressed, uint32_t latched,
		uint32_t locked, uint32_t group) {
	(void)keyboard; (void)serial;
	struct shell_client *client = data;
	if (client->xkb_state != NULL)
		(void)xkb_state_update_mask(client->xkb_state, depressed, latched,
			locked, 0, 0, group);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
		int32_t rate, int32_t delay) {
	(void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = handle_keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
		uint32_t capabilities) {
	struct shell_client *client = data;
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && client->pointer == NULL) {
		client->pointer = wl_seat_get_pointer(seat);
		if (client->pointer == NULL || wl_pointer_add_listener(client->pointer,
				&pointer_listener, client) != 0) {
			if (client->pointer != NULL) wl_pointer_destroy(client->pointer);
			client->pointer = NULL;
		}
	} else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0 &&
			client->pointer != NULL) {
		wl_pointer_destroy(client->pointer);
		client->pointer = NULL;
		client->pointer_surface = NULL;
	}
	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 &&
			client->keyboard == NULL) {
		client->keyboard = wl_seat_get_keyboard(seat);
		if (client->keyboard == NULL || wl_keyboard_add_listener(client->keyboard,
				&keyboard_listener, client) != 0) {
			if (client->keyboard != NULL) wl_keyboard_destroy(client->keyboard);
			client->keyboard = NULL;
		}
	} else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0 &&
			client->keyboard != NULL) {
		wl_keyboard_destroy(client->keyboard);
		client->keyboard = NULL;
		client->keyboard_surface = NULL;
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
	(void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static bool valid_header(const struct seyrix_shell_packet_header *header,
		ssize_t size, size_t expected) {
	return size == (ssize_t)expected && header->version == SEYRIX_SHELL_IPC_VERSION &&
		header->size == expected;
}

static bool open_overlay(struct shell_client *client,
		enum seyrix_shell_view_kind kind) {
	if (client->overlay != NULL) destroy_surface(client->overlay);
	struct shell_output *output = client->active_output;
	if (output == NULL && !wl_list_empty(&client->outputs))
		output = wl_container_of(client->outputs.next, output, link);
	if (kind == SEYRIX_SHELL_VIEW_LAUNCHER) {
		client->query[0] = '\0';
		client->selected_index = 0;
		update_search(client);
	} else if (kind == SEYRIX_SHELL_VIEW_SWITCHER) {
		uint32_t ids[SEYRIX_SHELL_MAX_WINDOWS];
		size_t count = switcher_ids(client, ids, SEYRIX_SHELL_MAX_WINDOWS);
		client->selected_index = count > 1 ? 1 : 0;
	} else if (kind == SEYRIX_SHELL_VIEW_MANUAL && client->manual_text == NULL) {
		client->manual_text = load_manual();
	}
	client->manual_scroll = 0;
	client->overlay = create_layer_surface(client, output, kind);
	return client->overlay != NULL;
}

static void ipc_packet(struct shell_client *client) {
	union {
		struct seyrix_shell_packet_header header;
		struct seyrix_shell_packet_workspace workspace;
		struct seyrix_shell_packet_window window;
		struct seyrix_shell_packet_id id;
		struct seyrix_shell_packet_action action;
	} packet;
	ssize_t size = recv(client->status_fd, &packet, sizeof(packet),
		MSG_DONTWAIT | MSG_TRUNC);
	if (size == 0 || (size < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
		errno != EINTR)) {
		close(client->status_fd);
		client->status_fd = -1;
		client->running = false;
		return;
	}
	if (size < 0) return;
	if (size > (ssize_t)sizeof(packet) || size < (ssize_t)sizeof(packet.header)) {
		g_warning("Ignoring malformed compositor shell packet (%ld bytes)", (long)size);
		return;
	}
	switch (packet.header.type) {
	case SEYRIX_SHELL_ACTIVE_WORKSPACE:
		if (!valid_header(&packet.header, size, sizeof(packet.workspace)) ||
				packet.workspace.workspace >= SEYRIX_SHELL_WORKSPACE_COUNT) return;
		client->active_workspace = packet.workspace.workspace;
		(void)seyrix_shell_state_set_active_workspace(&client->model,
			client->active_workspace);
		refresh_visible(client);
		break;
	case SEYRIX_SHELL_SNAPSHOT_BEGIN:
		if (!valid_header(&packet.header, size, sizeof(packet.action))) return;
		seyrix_shell_state_init(&client->model);
		client->active_workspace = 0;
		break;
	case SEYRIX_SHELL_WINDOW_UPSERT:
		if (!valid_header(&packet.header, size, sizeof(packet.window)) ||
				memchr(packet.window.app_id, '\0', sizeof(packet.window.app_id)) == NULL ||
				memchr(packet.window.title, '\0', sizeof(packet.window.title)) == NULL) return;
		if (!seyrix_shell_state_upsert(&client->model, packet.window.id,
				packet.window.workspace, packet.window.flags,
				packet.window.app_id, packet.window.title)) {
			g_warning("Ignoring invalid or out-of-capacity shell window snapshot");
			return;
		}
		refresh_visible(client);
		break;
	case SEYRIX_SHELL_WINDOW_REMOVE:
		if (!valid_header(&packet.header, size, sizeof(packet.id))) return;
		(void)seyrix_shell_state_remove(&client->model, packet.id.id);
		refresh_visible(client);
		break;
	case SEYRIX_SHELL_SHOW_LAUNCHER:
		if (!valid_header(&packet.header, size, sizeof(packet.action))) return;
		(void)open_overlay(client, SEYRIX_SHELL_VIEW_LAUNCHER);
		break;
	case SEYRIX_SHELL_SHOW_SWITCHER:
		if (!valid_header(&packet.header, size, sizeof(packet.action))) return;
		if (client->overlay != NULL &&
				client->overlay->kind == SEYRIX_SHELL_VIEW_SWITCHER) {
			uint32_t ids[SEYRIX_SHELL_MAX_WINDOWS];
			size_t count = switcher_ids(client, ids, SEYRIX_SHELL_MAX_WINDOWS);
			if (count > 0) client->selected_index =
				(client->selected_index + 1) % count;
			refresh_visible(client);
		} else {
			(void)open_overlay(client, SEYRIX_SHELL_VIEW_SWITCHER);
		}
		break;
	case SEYRIX_SHELL_COMMIT_SWITCHER:
		if (!valid_header(&packet.header, size, sizeof(packet.action))) return;
		if (client->overlay != NULL &&
				client->overlay->kind == SEYRIX_SHELL_VIEW_SWITCHER) {
			commit_switcher(client);
		}
		break;
	case SEYRIX_SHELL_SHOW_MANUAL:
		if (!valid_header(&packet.header, size, sizeof(packet.action))) return;
		(void)open_overlay(client, SEYRIX_SHELL_VIEW_MANUAL);
		break;
	case SEYRIX_SHELL_CLOSE_OVERLAY:
		if (!valid_header(&packet.header, size, sizeof(packet.action))) return;
		if (client->overlay != NULL) destroy_surface(client->overlay);
		break;
	default:
		g_warning("Ignoring unknown compositor shell packet type %u",
			(unsigned int)packet.header.type);
		break;
	}
}

static void destroy_buffers(struct shell_client *client) {
	struct shell_buffer *buffer, *temporary;
	wl_list_for_each_safe(buffer, temporary, &client->buffers, link) {
		wl_list_remove(&buffer->link);
		wl_buffer_destroy(buffer->buffer);
		munmap(buffer->mapping, buffer->size);
		free(buffer);
	}
}

static void destroy_outputs(struct shell_client *client) {
	struct shell_output *output, *temporary;
	wl_list_for_each_safe(output, temporary, &client->outputs, link)
		destroy_output(output);
}

static int run_client(struct shell_client *client) {
	client->display = wl_display_connect(NULL);
	if (client->display == NULL) goto cleanup;
	client->registry = wl_display_get_registry(client->display);
	if (client->registry == NULL || wl_registry_add_listener(client->registry,
			&registry_listener, client) != 0 || wl_display_roundtrip(client->display) < 0 ||
			client->compositor == NULL || client->shm == NULL || client->seat == NULL ||
			client->layer_shell == NULL) goto cleanup;
	if (wl_display_roundtrip(client->display) < 0) goto cleanup;
	if (client->catalog == NULL) client->catalog = seyrix_app_catalog_new();
	if (client->catalog == NULL) goto cleanup;
	struct shell_output *output;
	wl_list_for_each(output, &client->outputs, link) create_output_surfaces(output);
	if (wl_display_roundtrip(client->display) < 0) goto cleanup;
	(void)send_action(client, SEYRIX_SHELL_REQUEST_SNAPSHOT);
	client->running = true;
	int wayland_fd = wl_display_get_fd(client->display);
	while (client->running) {
		while (wl_display_prepare_read(client->display) != 0) {
			if (wl_display_dispatch_pending(client->display) < 0) {
				client->running = false;
				break;
			}
		}
		if (!client->running) break;
		short wayland_events = POLLIN;
		if (wl_display_flush(client->display) < 0) {
			if (errno != EAGAIN) {
				wl_display_cancel_read(client->display);
				break;
			}
			wayland_events |= POLLOUT;
		}
		struct pollfd fds[2] = {
			{.fd = wayland_fd, .events = wayland_events},
			{.fd = client->status_fd, .events = POLLIN},
		};
		nfds_t count = client->status_fd >= 0 ? 2 : 1;
		int result = poll(fds, count, -1);
		if (result < 0) {
			wl_display_cancel_read(client->display);
			if (errno == EINTR) continue;
			break;
		}
		if ((fds[0].revents & POLLIN) != 0) {
			if (wl_display_read_events(client->display) < 0 ||
					wl_display_dispatch_pending(client->display) < 0) break;
		} else {
			wl_display_cancel_read(client->display);
		}
		if ((fds[0].revents & POLLOUT) != 0) (void)wl_display_flush(client->display);
		if (count == 2 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
			ipc_packet(client);
		if ((fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) break;
	}
	client->running = false;
	/* A compositor disconnect is a normal terminal condition for this child. */
	goto normal_exit;

cleanup:
	client->running = false;
	if (client->overlay != NULL) destroy_surface(client->overlay);
	destroy_outputs(client);
	destroy_buffers(client);
	if (client->keyboard != NULL) wl_keyboard_destroy(client->keyboard);
	if (client->pointer != NULL) wl_pointer_destroy(client->pointer);
	if (client->seat != NULL) wl_seat_destroy(client->seat);
	if (client->layer_shell != NULL) zwlr_layer_shell_v1_destroy(client->layer_shell);
	if (client->shm != NULL) wl_shm_destroy(client->shm);
	if (client->compositor != NULL) wl_compositor_destroy(client->compositor);
	if (client->registry != NULL) wl_registry_destroy(client->registry);
	if (client->xkb_state != NULL) xkb_state_unref(client->xkb_state);
	if (client->xkb_keymap != NULL) xkb_keymap_unref(client->xkb_keymap);
	if (client->xkb_context != NULL) xkb_context_unref(client->xkb_context);
	if (client->search_results != NULL) g_ptr_array_unref(client->search_results);
	if (client->catalog != NULL) seyrix_app_catalog_free(client->catalog);
	free(client->manual_text);
	if (client->display != NULL) wl_display_disconnect(client->display);
	return 1;

normal_exit:
	if (client->overlay != NULL) destroy_surface(client->overlay);
	destroy_outputs(client);
	destroy_buffers(client);
	if (client->keyboard != NULL) wl_keyboard_destroy(client->keyboard);
	if (client->pointer != NULL) wl_pointer_destroy(client->pointer);
	if (client->seat != NULL) wl_seat_destroy(client->seat);
	if (client->layer_shell != NULL) zwlr_layer_shell_v1_destroy(client->layer_shell);
	if (client->shm != NULL) wl_shm_destroy(client->shm);
	if (client->compositor != NULL) wl_compositor_destroy(client->compositor);
	if (client->registry != NULL) wl_registry_destroy(client->registry);
	if (client->xkb_state != NULL) xkb_state_unref(client->xkb_state);
	if (client->xkb_keymap != NULL) xkb_keymap_unref(client->xkb_keymap);
	if (client->xkb_context != NULL) xkb_context_unref(client->xkb_context);
	if (client->search_results != NULL) g_ptr_array_unref(client->search_results);
	if (client->catalog != NULL) seyrix_app_catalog_free(client->catalog);
	free(client->manual_text);
	wl_display_disconnect(client->display);
	return 0;
}

int main(int argc, char **argv) {
	if (argc != 3 || strcmp(argv[1], "--status-fd") != 0) {
		fprintf(stderr, "Usage: seyrix-shell --status-fd FD\n");
		return 2;
	}
	char *end = NULL;
	long descriptor = strtol(argv[2], &end, 10);
	if (end == argv[2] || *end != '\0' || descriptor < 0 || descriptor > INT32_MAX)
		return 2;
	int descriptor_flags = fcntl((int)descriptor, F_GETFD);
	if (descriptor_flags < 0 || fcntl((int)descriptor, F_SETFD,
			descriptor_flags | FD_CLOEXEC) < 0) {
		fprintf(stderr, "Could not secure the compositor shell IPC descriptor\n");
		return 2;
	}
	struct shell_client client = {.status_fd = (int)descriptor};
	wl_list_init(&client.outputs);
	wl_list_init(&client.buffers);
	seyrix_shell_state_init(&client.model);
	seyrix_shell_config_defaults(&client.config);
	char error[256];
	if (!seyrix_shell_config_load_user(&client.config, error, sizeof(error))) {
		g_warning("Ignoring invalid shell configuration: %s", error);
		seyrix_shell_config_defaults(&client.config);
	}
	if (client.config.auto_hide)
		g_warning("Shell auto-hide is configured but incomplete; panel remains visible");
	client.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	int result = run_client(&client);
	if (client.status_fd >= 0) close(client.status_fd);
	return result;
}
