#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

enum { PANEL_HEIGHT = 30, BUTTON_SIZE = 26, BUTTON_GAP = 7, PANEL_LEFT = 12 };

enum seyrix_shell_message_type {
	SEYRIX_SHELL_ACTIVE_WORKSPACE = 1,
	SEYRIX_SHELL_SELECT_WORKSPACE = 2,
};

struct shell_buffer {
	struct wl_list link;
	struct wl_buffer *buffer;
	void *mapping;
	size_t size;
};

struct shell_state {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_list buffers;
	int status_fd;
	int width;
	int height;
	uint8_t active_workspace;
	double pointer_x;
	double pointer_y;
	bool pointer_entered;
	bool configured;
	bool running;
};

static void fill_rect(uint32_t *pixels, int width, int height,
		int x, int y, int rect_width, int rect_height, uint32_t color) {
	int left = x < 0 ? 0 : x;
	int top = y < 0 ? 0 : y;
	int right = x + rect_width > width ? width : x + rect_width;
	int bottom = y + rect_height > height ? height : y + rect_height;
	for (int row = top; row < bottom; row++) {
		for (int column = left; column < right; column++) {
			pixels[(size_t)row * (size_t)width + (size_t)column] = color;
		}
	}
}

static void draw_digit(uint32_t *pixels, int width, int height,
		int digit_x, int digit_y, uint8_t digit) {
	static const uint8_t glyphs[4][5] = {
		{0x2, 0x6, 0x2, 0x2, 0x7},
		{0x7, 0x1, 0x7, 0x4, 0x7},
		{0x7, 0x1, 0x7, 0x1, 0x7},
		{0x5, 0x5, 0x7, 0x1, 0x1},
	};
	if (digit < 1 || digit > 4) {
		return;
	}
	for (int row = 0; row < 5; row++) {
		for (int column = 0; column < 3; column++) {
			if ((glyphs[digit - 1][row] & (1U << (2 - column))) != 0) {
				fill_rect(pixels, width, height, digit_x + column * 2,
					digit_y + row * 2, 2, 2, 0xffe8edf2);
			}
		}
	}
}

static void release_buffer(void *data, struct wl_buffer *buffer) {
	struct shell_buffer *record = data;
	wl_list_remove(&record->link);
	wl_buffer_destroy(buffer);
	munmap(record->mapping, record->size);
	free(record);
}

static const struct wl_buffer_listener buffer_listener = {
	.release = release_buffer,
};

static void draw_panel(struct shell_state *state) {
	if (!state->configured || state->width <= 0 || state->height <= 0) {
		return;
	}
	size_t size = (size_t)state->width * (size_t)state->height * sizeof(uint32_t);
	if (size > INT32_MAX) {
		return;
	}
	int fd = memfd_create("seyrix-panel", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
		if (fd >= 0) {
			close(fd);
		}
		return;
	}
	void *mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		close(fd);
		return;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, (int32_t)size);
	if (pool == NULL) {
		munmap(mapping, size);
		close(fd);
		return;
	}
	int32_t stride = state->width * (int32_t)sizeof(uint32_t);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, state->width,
		state->height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	if (buffer == NULL) {
		munmap(mapping, size);
		return;
	}
	struct shell_buffer *record = calloc(1, sizeof(*record));
	if (record == NULL) {
		wl_buffer_destroy(buffer);
		munmap(mapping, size);
		return;
	}
	record->buffer = buffer;
	record->mapping = mapping;
	record->size = size;
	wl_list_insert(&state->buffers, &record->link);
	if (wl_buffer_add_listener(buffer, &buffer_listener, record) != 0) {
		wl_list_remove(&record->link);
		wl_buffer_destroy(buffer);
		munmap(mapping, size);
		free(record);
		return;
	}

	uint32_t *pixels = mapping;
	fill_rect(pixels, state->width, state->height, 0, 0,
		state->width, state->height, 0xff1c2229);
	for (uint8_t index = 0; index < 4; index++) {
		int x = PANEL_LEFT + (int)index * (BUTTON_SIZE + BUTTON_GAP);
		fill_rect(pixels, state->width, state->height, x, 2,
			BUTTON_SIZE, BUTTON_SIZE,
			index == state->active_workspace ? 0xff35678d : 0xff39424d);
		draw_digit(pixels, state->width, state->height, x + 10, 12,
			(uint8_t)(index + 1));
	}
	wl_surface_attach(state->surface, buffer, 0, 0);
	wl_surface_damage(state->surface, 0, 0, state->width, state->height);
	wl_surface_commit(state->surface);
}

static void handle_layer_configure(void *data,
		struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	struct shell_state *state = data;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
	if (width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX) {
		return;
	}
	state->width = (int)width;
	state->height = (int)height;
	state->configured = true;
	draw_panel(state);
}

static void handle_layer_closed(void *data,
		struct zwlr_layer_surface_v1 *layer_surface) {
	(void)layer_surface;
	struct shell_state *state = data;
	state->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = handle_layer_configure,
	.closed = handle_layer_closed,
};

static void send_workspace_selection(struct shell_state *state, uint8_t index) {
	if (state->status_fd < 0 || index >= 4) {
		return;
	}
	uint8_t message[2] = {SEYRIX_SHELL_SELECT_WORKSPACE, index};
	ssize_t sent = send(state->status_fd, message, sizeof(message),
		MSG_DONTWAIT | MSG_NOSIGNAL);
	if (sent != (ssize_t)sizeof(message) && errno != EAGAIN &&
			errno != EWOULDBLOCK) {
		close(state->status_fd);
		state->status_fd = -1;
	}
}

static void handle_pointer_enter(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	(void)pointer;
	(void)serial;
	struct shell_state *state = data;
	state->pointer_entered = surface == state->surface;
	state->pointer_x = wl_fixed_to_double(surface_x);
	state->pointer_y = wl_fixed_to_double(surface_y);
}

static void handle_pointer_leave(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface) {
	(void)pointer;
	(void)serial;
	(void)surface;
	((struct shell_state *)data)->pointer_entered = false;
}

static void handle_pointer_motion(void *data, struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	(void)pointer;
	(void)time;
	struct shell_state *state = data;
	state->pointer_x = wl_fixed_to_double(surface_x);
	state->pointer_y = wl_fixed_to_double(surface_y);
}

static void handle_pointer_button(void *data, struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t button_state) {
	(void)pointer;
	(void)serial;
	(void)time;
	struct shell_state *state = data;
	if (!state->pointer_entered || button != BTN_LEFT ||
			button_state != WL_POINTER_BUTTON_STATE_PRESSED ||
			state->pointer_y < 2 || state->pointer_y >= PANEL_HEIGHT - 2) {
		return;
	}
	for (uint8_t index = 0; index < 4; index++) {
		int left = PANEL_LEFT + (int)index * (BUTTON_SIZE + BUTTON_GAP);
		if (state->pointer_x >= left && state->pointer_x < left + BUTTON_SIZE) {
			send_workspace_selection(state, index);
			return;
		}
	}
}

static void handle_pointer_axis(void *data, struct wl_pointer *pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

static void handle_pointer_frame(void *data, struct wl_pointer *pointer) {
	(void)data;
	(void)pointer;
}

static void handle_pointer_axis_source(void *data, struct wl_pointer *pointer,
		uint32_t axis_source) {
	(void)data;
	(void)pointer;
	(void)axis_source;
}

static void handle_pointer_axis_stop(void *data, struct wl_pointer *pointer,
		uint32_t time, uint32_t axis) {
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

static void handle_pointer_axis_discrete(void *data, struct wl_pointer *pointer,
		uint32_t axis, int32_t discrete) {
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

static void handle_pointer_axis_value120(void *data, struct wl_pointer *pointer,
		uint32_t axis, int32_t value120) {
	(void)data;
	(void)pointer;
	(void)axis;
	(void)value120;
}

static void handle_pointer_axis_relative_direction(void *data,
		struct wl_pointer *pointer, uint32_t axis, uint32_t direction) {
	(void)data;
	(void)pointer;
	(void)axis;
	(void)direction;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = handle_pointer_enter,
	.leave = handle_pointer_leave,
	.motion = handle_pointer_motion,
	.button = handle_pointer_button,
	.axis = handle_pointer_axis,
	.frame = handle_pointer_frame,
	.axis_source = handle_pointer_axis_source,
	.axis_stop = handle_pointer_axis_stop,
	.axis_discrete = handle_pointer_axis_discrete,
	.axis_value120 = handle_pointer_axis_value120,
	.axis_relative_direction = handle_pointer_axis_relative_direction,
};

static void handle_seat_capabilities(void *data, struct wl_seat *seat,
		uint32_t capabilities) {
	struct shell_state *state = data;
	bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
	if (has_pointer && state->pointer == NULL) {
		state->pointer = wl_seat_get_pointer(seat);
		if (state->pointer == NULL || wl_pointer_add_listener(state->pointer,
				&pointer_listener, state) != 0) {
			if (state->pointer != NULL) {
				wl_pointer_destroy(state->pointer);
				state->pointer = NULL;
			}
		}
	} else if (!has_pointer && state->pointer != NULL) {
		wl_pointer_destroy(state->pointer);
		state->pointer = NULL;
		state->pointer_entered = false;
	}
}

static void handle_seat_name(void *data, struct wl_seat *seat, const char *name) {
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = handle_seat_capabilities,
	.name = handle_seat_name,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct shell_state *state = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0 &&
			state->compositor == NULL) {
		state->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, version < 4 ? version : 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0 && state->shm == NULL) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0 && state->seat == NULL) {
		state->seat = wl_registry_bind(registry, name, &wl_seat_interface,
			version < 4 ? version : 4);
		if (state->seat != NULL) {
			(void)wl_seat_add_listener(state->seat, &seat_listener, state);
		}
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0 &&
			state->layer_shell == NULL) {
		state->layer_shell = wl_registry_bind(registry, name,
			&zwlr_layer_shell_v1_interface, version < 4 ? version : 4);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static void destroy_buffers(struct shell_state *state);

static int run_client(struct shell_state *state) {
	state->display = wl_display_connect(NULL);
	if (state->display == NULL) {
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(state->display);
	if (registry == NULL || wl_registry_add_listener(registry,
			&registry_listener, state) != 0 || wl_display_roundtrip(state->display) < 0 ||
			state->compositor == NULL || state->shm == NULL ||
			state->layer_shell == NULL || state->seat == NULL) {
		return 1;
	}
	state->surface = wl_compositor_create_surface(state->compositor);
	if (state->surface == NULL) {
		return 1;
	}
	state->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		state->layer_shell, state->surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP, "seyrix-panel");
	if (state->layer_surface == NULL || zwlr_layer_surface_v1_add_listener(
			state->layer_surface, &layer_listener, state) != 0) {
		return 1;
	}
	zwlr_layer_surface_v1_set_anchor(state->layer_surface,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(state->layer_surface, 0, PANEL_HEIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(state->layer_surface, PANEL_HEIGHT);
	zwlr_layer_surface_v1_set_keyboard_interactivity(state->layer_surface,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	wl_surface_commit(state->surface);
	if (wl_display_roundtrip(state->display) < 0) {
		return 1;
	}

	state->running = true;
	int wayland_fd = wl_display_get_fd(state->display);
	while (state->running) {
		while (wl_display_prepare_read(state->display) != 0) {
			if (wl_display_dispatch_pending(state->display) < 0) {
				state->running = false;
				break;
			}
		}
		if (!state->running) {
			break;
		}
		if (wl_display_flush(state->display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(state->display);
			break;
		}
		struct pollfd fds[2] = {
			{.fd = wayland_fd, .events = POLLIN},
			{.fd = state->status_fd, .events = POLLIN},
		};
		nfds_t count = state->status_fd >= 0 ? 2 : 1;
		int result = poll(fds, count, -1);
		if (result < 0) {
			wl_display_cancel_read(state->display);
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if ((fds[0].revents & POLLIN) != 0) {
			if (wl_display_read_events(state->display) < 0 ||
					wl_display_dispatch_pending(state->display) < 0) {
				break;
			}
		} else {
			wl_display_cancel_read(state->display);
		}
		if (count == 2 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
			uint8_t message[2];
			ssize_t received = recv(state->status_fd, message,
				sizeof(message), MSG_DONTWAIT);
			if (received == (ssize_t)sizeof(message) &&
					message[0] == SEYRIX_SHELL_ACTIVE_WORKSPACE && message[1] < 4) {
				state->active_workspace = message[1];
				draw_panel(state);
			} else if (received == 0 ||
					(received < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
				close(state->status_fd);
				state->status_fd = -1;
			}
		}
		if ((fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
			break;
		}
	}
	destroy_buffers(state);
	if (state->pointer != NULL) {
		wl_pointer_destroy(state->pointer);
	}
	if (state->seat != NULL) {
		wl_seat_destroy(state->seat);
	}
	if (state->layer_surface != NULL) {
		zwlr_layer_surface_v1_destroy(state->layer_surface);
	}
	if (state->surface != NULL) {
		wl_surface_destroy(state->surface);
	}
	if (state->layer_shell != NULL) {
		zwlr_layer_shell_v1_destroy(state->layer_shell);
	}
	if (state->shm != NULL) {
		wl_shm_destroy(state->shm);
	}
	if (state->compositor != NULL) {
		wl_compositor_destroy(state->compositor);
	}
	wl_display_disconnect(state->display);
	return 0;
}

static void destroy_buffers(struct shell_state *state) {
	struct shell_buffer *buffer, *temporary;
	wl_list_for_each_safe(buffer, temporary, &state->buffers, link) {
		wl_list_remove(&buffer->link);
		wl_buffer_destroy(buffer->buffer);
		munmap(buffer->mapping, buffer->size);
		free(buffer);
	}
}

int main(int argc, char **argv) {
	if (argc != 3 || strcmp(argv[1], "--status-fd") != 0) {
		fprintf(stderr, "Usage: seyrix-shell --status-fd FD\n");
		return 2;
	}
	char *end = NULL;
	long descriptor = strtol(argv[2], &end, 10);
	if (end == argv[2] || *end != '\0' || descriptor < 0 || descriptor > INT32_MAX) {
		return 2;
	}
	struct shell_state state = {.status_fd = (int)descriptor};
	wl_list_init(&state.buffers);
	int result = run_client(&state);
	if (state.status_fd >= 0) {
		close(state.status_fd);
	}
	return result;
}
