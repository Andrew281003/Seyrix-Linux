#include "seyrix.h"

#include <stdlib.h>

#include <wlr/util/log.h>

static void remove_listener(struct wl_listener *listener) {
	if (listener->link.prev != NULL && listener->link.next != NULL) {
		wl_list_remove(&listener->link);
		wl_list_init(&listener->link);
	}
}

void seyrix_focus_window(struct seyrix_server *server,
		struct seyrix_window *window) {
	if (server->seat == NULL) {
		return;
	}

	struct wlr_surface *previous = server->seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = window != NULL ?
		window->xdg_toplevel->base->surface : NULL;
	if (previous != surface && previous != NULL) {
		struct wlr_xdg_toplevel *previous_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(previous);
		if (previous_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(previous_toplevel, false);
		}
	}

	if (window == NULL || !window->mapped) {
		server->focused_window = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		return;
	}

	server->focused_window = window;
	wlr_scene_node_raise_to_top(&window->scene_tree->node);
	if (window->link.prev != NULL && window->link.next != NULL) {
		wl_list_remove(&window->link);
		wl_list_insert(&server->windows, &window->link);
	}
	wlr_xdg_toplevel_set_activated(window->xdg_toplevel, true);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(server->seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

void seyrix_focus_next_window(struct seyrix_server *server,
		struct seyrix_window *exclude) {
	struct seyrix_window *window;
	wl_list_for_each(window, &server->windows, link) {
		if (window != exclude && window->mapped) {
			seyrix_focus_window(server, window);
			return;
		}
	}
	seyrix_focus_window(server, NULL);
}

void seyrix_reset_cursor_mode(struct seyrix_server *server) {
	if (server->cursor_mode == SEYRIX_CURSOR_RESIZE &&
			server->grabbed_window != NULL) {
		wlr_xdg_toplevel_set_resizing(
			server->grabbed_window->xdg_toplevel, false);
		wlr_log(WLR_INFO, "Resize gesture ended at %dx%d",
			server->grabbed_window->xdg_toplevel->base->geometry.width,
			server->grabbed_window->xdg_toplevel->base->geometry.height);
	} else if (server->cursor_mode == SEYRIX_CURSOR_MOVE &&
			server->grabbed_window != NULL) {
		wlr_log(WLR_INFO, "Window moved to %d,%d",
			server->grabbed_window->scene_tree->node.x,
			server->grabbed_window->scene_tree->node.y);
	}
	server->cursor_mode = SEYRIX_CURSOR_PASSTHROUGH;
	server->grabbed_window = NULL;
}

void seyrix_begin_interactive(struct seyrix_window *window,
		enum seyrix_cursor_mode mode, uint32_t edges) {
	if (window == NULL || !window->mapped) {
		return;
	}
	struct seyrix_server *server = window->server;
	server->grabbed_window = window;
	server->cursor_mode = mode;
	if (mode == SEYRIX_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - window->scene_tree->node.x;
		server->grab_y = server->cursor->y - window->scene_tree->node.y;
		return;
	}

	struct wlr_box geometry = window->xdg_toplevel->base->geometry;
	double right = window->scene_tree->node.x + geometry.x + geometry.width;
	double bottom = window->scene_tree->node.y + geometry.y + geometry.height;
	server->grab_x = server->cursor->x -
		((edges & WLR_EDGE_RIGHT) != 0 ? right :
			window->scene_tree->node.x + geometry.x);
	server->grab_y = server->cursor->y -
		((edges & WLR_EDGE_BOTTOM) != 0 ? bottom :
			window->scene_tree->node.y + geometry.y);
	server->grab_box = geometry;
	server->grab_box.x += window->scene_tree->node.x;
	server->grab_box.y += window->scene_tree->node.y;
	server->resize_edges = edges;
	wlr_xdg_toplevel_set_resizing(window->xdg_toplevel, true);
}

static void handle_window_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_window *window = wl_container_of(listener, window, map);
	if (window->mapped) {
		return;
	}
	window->mapped = true;
	wl_list_insert(&window->server->windows, &window->link);
	seyrix_focus_window(window->server, window);
	wlr_log(WLR_INFO, "Window mapped: app_id=%s title=%s",
		window->xdg_toplevel->app_id != NULL ? window->xdg_toplevel->app_id : "",
		window->xdg_toplevel->title != NULL ? window->xdg_toplevel->title : "");
}

static void unmap_window(struct seyrix_window *window) {
	struct seyrix_server *server = window->server;
	if (server->grabbed_window == window) {
		seyrix_reset_cursor_mode(server);
	}
	if (window->mapped) {
		window->mapped = false;
		wl_list_remove(&window->link);
		wl_list_init(&window->link);
	}
	if (server->focused_window == window) {
		server->focused_window = NULL;
		seyrix_focus_next_window(server, window);
	}
}

static void handle_window_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_window *window = wl_container_of(listener, window, unmap);
	unmap_window(window);
	wlr_log(WLR_INFO, "Window unmapped");
}

static void handle_window_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_window *window = wl_container_of(listener, window, commit);
	if (window->xdg_toplevel->base->initial_commit) {
		struct wlr_box output_box = {0};
		wlr_output_layout_get_box(window->server->output_layout, NULL,
			&output_box);
		int width = output_box.width > 128 ? output_box.width - 128 : output_box.width;
		int height = output_box.height > 144 ? output_box.height - 144 : output_box.height;
		if (width > 800) {
			width = 800;
		}
		if (height > 600) {
			height = 600;
		}
		if (width < 1) {
			width = 640;
		}
		if (height < 1) {
			height = 480;
		}
		wlr_xdg_toplevel_set_size(window->xdg_toplevel, width, height);
	}
	if (window->mapped) {
		struct wlr_box *geometry = &window->xdg_toplevel->base->geometry;
		if (geometry->width > 0 && geometry->height > 0 &&
				(geometry->width != window->last_width ||
				geometry->height != window->last_height)) {
			window->last_width = geometry->width;
			window->last_height = geometry->height;
			wlr_log(WLR_INFO, "Window geometry committed: %dx%d",
				geometry->width, geometry->height);
		}
	}
}

static void handle_window_move(struct wl_listener *listener, void *data) {
	struct seyrix_window *window = wl_container_of(listener, window, request_move);
	struct wlr_xdg_toplevel_move_event *event = data;
	if (event->seat == window->server->seat->pointer_state.focused_client &&
			wlr_seat_validate_pointer_grab_serial(window->server->seat,
				window->xdg_toplevel->base->surface, event->serial)) {
		seyrix_begin_interactive(window, SEYRIX_CURSOR_MOVE, 0);
	}
}

static void handle_window_resize(struct wl_listener *listener, void *data) {
	struct seyrix_window *window = wl_container_of(listener, window, request_resize);
	struct wlr_xdg_toplevel_resize_event *event = data;
	if (event->seat == window->server->seat->pointer_state.focused_client &&
			wlr_seat_validate_pointer_grab_serial(window->server->seat,
				window->xdg_toplevel->base->surface, event->serial)) {
		seyrix_begin_interactive(window, SEYRIX_CURSOR_RESIZE, event->edges);
	}
}

static void handle_window_maximize(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_window *window =
		wl_container_of(listener, window, request_maximize);
	if (window->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(window->xdg_toplevel->base);
	}
}

static void handle_window_fullscreen(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_window *window =
		wl_container_of(listener, window, request_fullscreen);
	if (window->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(window->xdg_toplevel->base);
	}
}

static void handle_window_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_window *window = wl_container_of(listener, window, destroy);
	unmap_window(window);

	remove_listener(&window->map);
	remove_listener(&window->unmap);
	remove_listener(&window->commit);
	remove_listener(&window->destroy);
	remove_listener(&window->request_move);
	remove_listener(&window->request_resize);
	remove_listener(&window->request_maximize);
	remove_listener(&window->request_fullscreen);
	if (window->scene_tree != NULL) {
		window->scene_tree->node.data = NULL;
	}
	wlr_log(WLR_INFO, "Window destroyed");
	free(window);
}

static void handle_new_toplevel(struct wl_listener *listener, void *data) {
	struct seyrix_server *server = wl_container_of(listener, server, new_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;
	struct seyrix_window *window = calloc(1, sizeof(*window));
	if (window == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate xdg-shell window state");
		wlr_xdg_toplevel_send_close(xdg_toplevel);
		return;
	}
	window->server = server;
	window->xdg_toplevel = xdg_toplevel;
	window->scene_tree = wlr_scene_xdg_surface_create(server->window_layer,
		xdg_toplevel->base);
	if (window->scene_tree == NULL) {
		wlr_log(WLR_ERROR, "Cannot add xdg-shell window to scene");
		wlr_xdg_toplevel_send_close(xdg_toplevel);
		free(window);
		return;
	}
	window->scene_tree->node.data = window;
	xdg_toplevel->base->data = window->scene_tree;
	unsigned int offset = (unsigned int)(server->next_window_offset++ % 8U);
	wlr_scene_node_set_position(&window->scene_tree->node,
		48 + (int)offset * 24, 56 + (int)offset * 20);

	window->map.notify = handle_window_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &window->map);
	window->unmap.notify = handle_window_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &window->unmap);
	window->commit.notify = handle_window_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &window->commit);
	window->destroy.notify = handle_window_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &window->destroy);
	window->request_move.notify = handle_window_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &window->request_move);
	window->request_resize.notify = handle_window_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &window->request_resize);
	window->request_maximize.notify = handle_window_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize,
		&window->request_maximize);
	window->request_fullscreen.notify = handle_window_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen,
		&window->request_fullscreen);
}

bool seyrix_window_init(struct seyrix_server *server) {
	server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
	if (server->xdg_shell == NULL) {
		wlr_log(WLR_ERROR, "Cannot create xdg-shell protocol global");
		return false;
	}
	server->new_toplevel.notify = handle_new_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel,
		&server->new_toplevel);
	return true;
}

void seyrix_window_finish(struct seyrix_server *server) {
	remove_listener(&server->new_toplevel);
}
