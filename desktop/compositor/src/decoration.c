#include "decoration.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>
#include <wlr/util/log.h>

#include "window-state.h"

enum { TITLEBAR_HEIGHT = 26, CONTROL_SIZE = 16, RESIZE_BORDER = 5 };

static const float focused_titlebar_color[4] = {0.19f, 0.27f, 0.36f, 1.0f};
static const float unfocused_titlebar_color[4] = {0.15f, 0.17f, 0.20f, 1.0f};
static const float control_color[4] = {0.30f, 0.33f, 0.38f, 1.0f};
static const float close_color[4] = {0.48f, 0.27f, 0.27f, 1.0f};
static const float title_text_color[4] = {0.92f, 0.93f, 0.95f, 1.0f};

struct seyrix_decoration {
	struct seyrix_window *window;
	struct wlr_xdg_toplevel_decoration_v1 *protocol;
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *titlebar;
	struct wlr_scene_rect *close_control;
	struct wlr_scene_rect *maximize_control;
	struct wlr_scene_tree *glyphs;
	struct wl_listener request_mode;
	struct wl_listener destroy;
	struct wl_listener title;
	struct wl_listener commit;
	int applied_titlebar_height;
	int width;
	bool fullscreen;
	bool focused;
};

static void remove_listener(struct wl_listener *listener) {
	if (listener->link.prev != NULL && listener->link.next != NULL) {
		wl_list_remove(&listener->link);
		wl_list_init(&listener->link);
	}
}

static struct seyrix_window *window_from_toplevel(
		struct wlr_xdg_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->base == NULL || toplevel->base->data == NULL) {
		return NULL;
	}
	struct wlr_scene_tree *tree = toplevel->base->data;
	while (tree != NULL) {
		if (tree->node.data != NULL) {
			return tree->node.data;
		}
		tree = tree->node.parent;
	}
	return NULL;
}

static void glyph_rows(char input, uint8_t rows[7]) {
	static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
	static const uint8_t fallback[7] = {14, 17, 1, 2, 4, 0, 4};
	static const uint8_t letters[26][7] = {
		{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
		{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
		{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
		{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
		{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
		{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
		{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
		{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
		{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
	};
	static const uint8_t numbers[10][7] = {
		{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
		{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
		{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
		{14,17,17,15,1,1,14},
	};
	unsigned char c = (unsigned char)toupper((unsigned char)input);
	if (c >= 'A' && c <= 'Z') {
		memcpy(rows, letters[c - 'A'], sizeof(letters[0]));
	} else if (c >= '0' && c <= '9') {
		memcpy(rows, numbers[c - '0'], sizeof(numbers[0]));
	} else if (c == ' ') {
		memcpy(rows, blank, sizeof(blank));
	} else if (c == '-') {
		uint8_t dash[7] = {0, 0, 0, 31, 0, 0, 0};
		memcpy(rows, dash, sizeof(dash));
	} else if (c == '.') {
		uint8_t dot[7] = {0, 0, 0, 0, 0, 6, 6};
		memcpy(rows, dot, sizeof(dot));
	} else {
		memcpy(rows, fallback, sizeof(fallback));
	}
}

static void draw_title(struct seyrix_decoration *decoration) {
	if (decoration->glyphs != NULL) {
		wlr_scene_node_destroy(&decoration->glyphs->node);
		decoration->glyphs = NULL;
	}
	decoration->glyphs = wlr_scene_tree_create(decoration->tree);
	if (decoration->glyphs == NULL) {
		return;
	}
	const char *title = decoration->window->xdg_toplevel->title;
	if (title == NULL || title[0] == '\0') {
		title = "Seyrix window";
	}
	int max_chars = (decoration->width - 66) / 6;
	if (max_chars < 0) {
		max_chars = 0;
	}
	int x = 10;
	for (int index = 0; title[index] != '\0' && index < max_chars; index++) {
		uint8_t rows[7];
		glyph_rows(title[index], rows);
		for (int row = 0; row < 7; row++) {
			for (int column = 0; column < 5; column++) {
				if ((rows[row] & (1U << (4 - column))) == 0) {
					continue;
				}
				struct wlr_scene_rect *pixel = wlr_scene_rect_create(
					decoration->glyphs, 1, 1, title_text_color);
				if (pixel != NULL) {
					wlr_scene_node_set_position(&pixel->node,
						x + column, 9 + row);
				}
			}
		}
		x += 6;
	}
	wlr_scene_node_raise_to_top(&decoration->glyphs->node);
}

static int effective_titlebar_height(const struct seyrix_decoration *decoration) {
	return decoration != NULL && !decoration->fullscreen &&
		decoration->protocol->current.mode ==
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE ? TITLEBAR_HEIGHT : 0;
}

static void layout_decoration(struct seyrix_decoration *decoration) {
	struct seyrix_window *window = decoration->window;
	int width = window->xdg_toplevel->base->geometry.width;
	if (width <= 0) {
		width = window->last_width;
	}
	if (width < 1) {
		width = 1;
	}
	decoration->width = width;
	wlr_scene_rect_set_color(decoration->titlebar,
		decoration->focused ? focused_titlebar_color : unfocused_titlebar_color);
	wlr_scene_rect_set_size(decoration->titlebar, width, TITLEBAR_HEIGHT);
	wlr_scene_node_set_position(&decoration->close_control->node,
		width - CONTROL_SIZE - 4, 5);
	wlr_scene_node_set_position(&decoration->maximize_control->node,
		width - CONTROL_SIZE * 2 - 8, 5);
	wlr_scene_node_set_enabled(&decoration->tree->node,
		effective_titlebar_height(decoration) != 0);
	draw_title(decoration);
}

static void update_effective_mode(struct seyrix_decoration *decoration) {
	struct seyrix_window *window = decoration->window;
	int previous = decoration->applied_titlebar_height;
	int current = effective_titlebar_height(decoration);
	decoration->applied_titlebar_height = current;
	if (window->surface_tree != NULL) {
		wlr_scene_node_set_position(&window->surface_tree->node, 0, current);
	}
	if (previous != current && !window->xdg_toplevel->current.maximized &&
			!window->xdg_toplevel->current.fullscreen && !decoration->fullscreen) {
		wlr_scene_node_set_position(&window->scene_tree->node,
			window->scene_tree->node.x,
			window->scene_tree->node.y + previous - current);
		seyrix_window_state_capture_normal(window);
	} else if (previous != current) {
		seyrix_window_state_reconfigure(window);
	}
	layout_decoration(decoration);
}

static void set_requested_mode(struct seyrix_decoration *decoration) {
	enum wlr_xdg_toplevel_decoration_v1_mode requested =
		decoration->protocol->requested_mode;
	enum wlr_xdg_toplevel_decoration_v1_mode mode =
		requested == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE ?
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE :
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	wlr_xdg_toplevel_decoration_v1_set_mode(decoration->protocol, mode);
}

static void handle_decoration_request_mode(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct seyrix_decoration *decoration =
		wl_container_of(listener, decoration, request_mode);
	set_requested_mode(decoration);
}

static void handle_decoration_title(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_decoration *decoration =
		wl_container_of(listener, decoration, title);
	layout_decoration(decoration);
}

static void handle_decoration_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_decoration *decoration =
		wl_container_of(listener, decoration, commit);
	int current = effective_titlebar_height(decoration);
	if (current != decoration->applied_titlebar_height) {
		update_effective_mode(decoration);
	} else {
		layout_decoration(decoration);
	}
}

static void destroy_decoration(struct seyrix_decoration *decoration) {
	remove_listener(&decoration->request_mode);
	remove_listener(&decoration->destroy);
	remove_listener(&decoration->title);
	remove_listener(&decoration->commit);
	if (decoration->protocol != NULL && decoration->protocol->data == decoration) {
		decoration->protocol->data = NULL;
	}
	if (decoration->window != NULL && decoration->window->decoration == decoration) {
		decoration->window->decoration = NULL;
	}
	if (decoration->tree != NULL && decoration->window != NULL &&
			decoration->window->server->scene != NULL) {
		wlr_scene_node_destroy(&decoration->tree->node);
	}
	free(decoration);
}

static void handle_decoration_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_decoration *decoration =
		wl_container_of(listener, decoration, destroy);
	destroy_decoration(decoration);
}

static void handle_new_decoration(struct wl_listener *listener, void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, new_decoration);
	struct wlr_xdg_toplevel_decoration_v1 *protocol = data;
	struct seyrix_window *window = window_from_toplevel(protocol->toplevel);
	if (window == NULL) {
		wlr_log(WLR_ERROR, "Ignoring xdg-decoration request without a managed toplevel");
		wlr_xdg_toplevel_decoration_v1_set_mode(protocol,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		return;
	}
	struct seyrix_decoration *decoration = calloc(1, sizeof(*decoration));
	if (decoration == NULL) {
		wlr_xdg_toplevel_decoration_v1_set_mode(protocol,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		return;
	}
	decoration->window = window;
	decoration->protocol = protocol;
	decoration->tree = wlr_scene_tree_create(window->scene_tree);
	if (decoration->tree == NULL) {
		free(decoration);
		wlr_xdg_toplevel_decoration_v1_set_mode(protocol,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		return;
	}
	decoration->titlebar = wlr_scene_rect_create(decoration->tree, 1,
		TITLEBAR_HEIGHT, unfocused_titlebar_color);
	decoration->close_control = wlr_scene_rect_create(decoration->tree,
		CONTROL_SIZE, CONTROL_SIZE, close_color);
	decoration->maximize_control = wlr_scene_rect_create(decoration->tree,
		CONTROL_SIZE, CONTROL_SIZE, control_color);
	if (decoration->titlebar == NULL || decoration->close_control == NULL ||
			decoration->maximize_control == NULL) {
		wlr_scene_node_destroy(&decoration->tree->node);
		free(decoration);
		wlr_xdg_toplevel_decoration_v1_set_mode(protocol,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		return;
	}
	window->decoration = decoration;
	protocol->data = decoration;
	decoration->focused = window->server->focused_window == window;
	decoration->request_mode.notify = handle_decoration_request_mode;
	wl_signal_add(&protocol->events.request_mode, &decoration->request_mode);
	decoration->destroy.notify = handle_decoration_destroy;
	wl_signal_add(&protocol->events.destroy, &decoration->destroy);
	decoration->title.notify = handle_decoration_title;
	wl_signal_add(&protocol->toplevel->events.set_title, &decoration->title);
	decoration->commit.notify = handle_decoration_commit;
	wl_signal_add(&protocol->toplevel->base->surface->events.commit,
		&decoration->commit);
	set_requested_mode(decoration);
	update_effective_mode(decoration);
	wlr_log(WLR_INFO, "Negotiating xdg-decoration mode requested=%d",
		protocol->requested_mode);
}

bool seyrix_decoration_init(struct seyrix_server *server) {
	server->decoration_manager =
		wlr_xdg_decoration_manager_v1_create(server->display);
	if (server->decoration_manager == NULL) {
		wlr_log(WLR_ERROR, "Cannot create xdg-decoration protocol global");
		return false;
	}
	server->new_decoration.notify = handle_new_decoration;
	wl_signal_add(&server->decoration_manager->events.new_toplevel_decoration,
		&server->new_decoration);
	return true;
}

void seyrix_decoration_finish(struct seyrix_server *server) {
	remove_listener(&server->new_decoration);
	server->decoration_manager = NULL;
}

void seyrix_decoration_window_destroy(struct seyrix_window *window) {
	if (window != NULL && window->decoration != NULL) {
		destroy_decoration(window->decoration);
	}
}

int seyrix_decoration_titlebar_height(const struct seyrix_window *window) {
	if (window == NULL || window->decoration == NULL) {
		return 0;
	}
	return effective_titlebar_height(window->decoration);
}

void seyrix_decoration_set_fullscreen(struct seyrix_window *window, bool value) {
	if (window == NULL || window->decoration == NULL) {
		return;
	}
	struct seyrix_decoration *decoration = window->decoration;
	int previous = decoration->applied_titlebar_height;
	decoration->fullscreen = value;
	int current = effective_titlebar_height(decoration);
	decoration->applied_titlebar_height = current;
	wlr_scene_node_set_position(&window->surface_tree->node, 0, current);
	wlr_scene_node_set_enabled(&decoration->tree->node, current != 0);
	(void)previous;
}

void seyrix_decoration_set_focused(struct seyrix_window *window, bool focused) {
	if (window == NULL || window->decoration == NULL) {
		return;
	}
	window->decoration->focused = focused;
	wlr_scene_rect_set_color(window->decoration->titlebar,
		focused ? focused_titlebar_color : unfocused_titlebar_color);
}

bool seyrix_decoration_pointer_button(struct seyrix_server *server,
		double x, double y, uint32_t button, uint32_t state) {
	if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED) {
		return false;
	}
	struct seyrix_window *window;
	wl_list_for_each(window, &server->windows, link) {
		if (!window->mapped || window->minimized || window->decoration == NULL ||
				!seyrix_workspace_window_is_visible(window) ||
				seyrix_decoration_titlebar_height(window) == 0) {
			continue;
		}
		struct wlr_box geometry = window->xdg_toplevel->base->geometry;
		int width = geometry.width;
		int height = geometry.height + TITLEBAR_HEIGHT;
		double local_x = x - window->scene_tree->node.x;
		double local_y = y - window->scene_tree->node.y;
		if (local_x < 0 || local_x >= width || local_y < 0 || local_y >= height) {
			continue;
		}
		seyrix_focus_window(server, window);
		if (local_y < TITLEBAR_HEIGHT) {
			if (local_x >= width - CONTROL_SIZE - 4) {
				wlr_xdg_toplevel_send_close(window->xdg_toplevel);
			} else if (local_x >= width - CONTROL_SIZE * 2 - 8 &&
					local_x < width - CONTROL_SIZE - 4) {
				seyrix_window_state_toggle_maximized(window);
			} else {
			seyrix_begin_interactive(window, SEYRIX_CURSOR_MOVE, 0);
			}
			return true;
		}
		uint32_t edges = 0;
		if (local_x < RESIZE_BORDER) {
			edges |= WLR_EDGE_LEFT;
		} else if (local_x >= width - RESIZE_BORDER) {
			edges |= WLR_EDGE_RIGHT;
		}
		if (local_y >= height - RESIZE_BORDER) {
			edges |= WLR_EDGE_BOTTOM;
		}
		if (edges != 0) {
			seyrix_begin_interactive(window, SEYRIX_CURSOR_RESIZE, edges);
			return true;
		}
		return false;
	}
	return false;
}
