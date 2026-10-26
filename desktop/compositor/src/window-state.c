#include "window-state.h"

#include <stdlib.h>

#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "decoration.h"
#include "layer-shell.h"
#include "popup.h"

struct seyrix_window_state {
	struct seyrix_window *window;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener request_minimize;
	struct wlr_box normal_box;
	struct wlr_output *output;
	bool normal_box_valid;
	bool desired_maximized;
	bool desired_fullscreen;
	bool minimized;
	bool capabilities_sent;
};

struct wlr_output *seyrix_window_state_output(struct seyrix_window *window) {
	if (window == NULL || window->window_state == NULL) {
		return NULL;
	}
	return window->window_state->output;
}

static bool current_window_box(struct seyrix_window *window,
	struct wlr_box *box);

static void remove_listener(struct wl_listener *listener) {
	if (listener->link.prev != NULL && listener->link.next != NULL) {
		wl_list_remove(&listener->link);
		wl_list_init(&listener->link);
	}
}

static bool output_is_live(struct seyrix_server *server,
		struct wlr_output *candidate) {
	if (candidate == NULL) {
		return false;
	}
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output == candidate) {
			return true;
		}
	}
	return false;
}

static struct wlr_output *first_output(struct seyrix_server *server) {
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output->enabled) {
			return output->wlr_output;
		}
	}
	return NULL;
}

static struct wlr_output *window_output(struct seyrix_window *window) {
	struct seyrix_window_state *state = window->window_state;
	struct seyrix_server *server = window->server;
	if (state->desired_fullscreen &&
			output_is_live(server, window->xdg_toplevel->requested.fullscreen_output)) {
		return window->xdg_toplevel->requested.fullscreen_output;
	}
	if (output_is_live(server, state->output)) {
		return state->output;
	}

	struct wlr_box box = {0};
	if (!current_window_box(window, &box)) {
		box.x = window->scene_tree->node.x;
		box.y = window->scene_tree->node.y;
	}
	double center_x = box.x + box.width / 2.0;
	double center_y = box.y + box.height / 2.0;
	struct wlr_output *output = wlr_output_layout_output_at(
		server->output_layout, center_x, center_y);
	return output != NULL ? output : first_output(server);
}

static bool current_window_box(struct seyrix_window *window,
		struct wlr_box *box) {
	struct wlr_box geometry = window->xdg_toplevel->base->geometry;
	if (geometry.width <= 0 || geometry.height <= 0) {
		return false;
	}
	box->x = window->scene_tree->node.x;
	box->y = window->scene_tree->node.y;
	box->width = geometry.width;
	box->height = geometry.height;
	return true;
}

static void save_normal_box(struct seyrix_window *window) {
	struct seyrix_window_state *state = window->window_state;
	struct wlr_box box;
	if (current_window_box(window, &box)) {
		state->normal_box = box;
		state->normal_box_valid = true;
	}
}

static void constrain_normal_box(struct seyrix_window *window) {
	struct seyrix_server *server = window->server;
	struct wlr_box all = {0};
	wlr_output_layout_get_box(server->output_layout, NULL, &all);
	if (all.width <= 0 || all.height <= 0 || !window->window_state->normal_box_valid) {
		return;
	}
	struct wlr_box *box = &window->window_state->normal_box;
	if (wlr_output_layout_output_at(server->output_layout,
			box->x + box->width / 2.0, box->y + box->height / 2.0) != NULL) {
		return;
	}
	struct wlr_output *output = first_output(server);
	struct wlr_box target = {0};
	if (output != NULL) {
		wlr_output_layout_get_box(server->output_layout, output, &target);
	}
	if (target.width <= 0 || target.height <= 0) {
		target = all;
	}
	int visible_width = box->width < target.width ? box->width : target.width;
	int visible_height = box->height < target.height ? box->height : target.height;
	box->x = target.x + (target.width - visible_width) / 2;
	box->y = target.y + (target.height - visible_height) / 2;
}

static void apply_requested_state(struct seyrix_window *window) {
	if (window == NULL || window->window_state == NULL ||
			window->xdg_toplevel == NULL || window->scene_tree == NULL) {
		return;
	}
	struct seyrix_window_state *state = window->window_state;
	struct wlr_xdg_toplevel *toplevel = window->xdg_toplevel;
	struct wlr_xdg_surface *xdg_surface = toplevel->base;
	bool fullscreen = state->desired_fullscreen;
	bool maximized = state->desired_maximized && !fullscreen;
	bool special = fullscreen || maximized;

	if (special && !toplevel->current.fullscreen && !toplevel->current.maximized &&
			!state->normal_box_valid) {
		save_normal_box(window);
	}

	if (special) {
		struct wlr_output *output = window_output(window);
		struct wlr_box box = {0};
		if (output != NULL) {
			wlr_output_layout_get_box(window->server->output_layout, output, &box);
		}
		if (box.width <= 0 || box.height <= 0) {
			wlr_output_layout_get_box(window->server->output_layout, NULL, &box);
		}
		state->output = output;
		if (maximized && output != NULL) {
			seyrix_layer_shell_output_work_area(window->server, output, &box);
		}
		int titlebar = fullscreen ? 0 :
			seyrix_decoration_titlebar_height(window);
		int client_height = box.height - titlebar;
		if (client_height < 1) {
			client_height = box.height;
		}
		wlr_scene_node_set_position(&window->scene_tree->node, box.x, box.y);
		wlr_xdg_toplevel_set_size(toplevel, box.width, client_height);
	} else if (state->normal_box_valid) {
		constrain_normal_box(window);
		wlr_scene_node_set_position(&window->scene_tree->node,
			state->normal_box.x, state->normal_box.y);
		wlr_xdg_toplevel_set_size(toplevel, state->normal_box.width,
			state->normal_box.height);
		state->output = wlr_output_layout_output_at(window->server->output_layout,
			state->normal_box.x + state->normal_box.width / 2.0,
			state->normal_box.y + state->normal_box.height / 2.0);
	} else {
		state->output = window_output(window);
		wlr_xdg_toplevel_set_size(toplevel, 0, 0);
	}

	seyrix_decoration_set_fullscreen(window, fullscreen);
	wlr_xdg_toplevel_set_maximized(toplevel, maximized);
	wlr_xdg_toplevel_set_fullscreen(toplevel, fullscreen);
	if (xdg_surface->initialized) {
		wlr_log(WLR_INFO,
			"Window state requested: app_id=%s maximized=%d fullscreen=%d minimized=%d",
			toplevel->app_id != NULL ? toplevel->app_id : "",
			maximized, fullscreen, state->minimized);
	}
}

void seyrix_window_state_set_maximized(struct seyrix_window *window, bool value) {
	if (window == NULL || window->window_state == NULL) {
		return;
	}
	window->window_state->desired_maximized = value;
	apply_requested_state(window);
}

void seyrix_window_state_toggle_maximized(struct seyrix_window *window) {
	if (window == NULL || window->window_state == NULL) {
		return;
	}
	struct seyrix_window_state *state = window->window_state;
	seyrix_window_state_set_maximized(window,
		!state->desired_maximized && !state->desired_fullscreen);
}

void seyrix_window_state_set_fullscreen(struct seyrix_window *window, bool value) {
	if (window == NULL || window->window_state == NULL) {
		return;
	}
	window->window_state->desired_fullscreen = value;
	if (value && window->xdg_toplevel->requested.fullscreen_output != NULL) {
		window->window_state->output =
			window->xdg_toplevel->requested.fullscreen_output;
	}
	apply_requested_state(window);
}

static void handle_request_maximize(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_window_state *state =
		wl_container_of(listener, state, request_maximize);
	state->desired_maximized = state->window->xdg_toplevel->requested.maximized;
	apply_requested_state(state->window);
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_window_state *state =
		wl_container_of(listener, state, request_fullscreen);
	state->desired_fullscreen = state->window->xdg_toplevel->requested.fullscreen;
	apply_requested_state(state->window);
}

void seyrix_window_state_minimize(struct seyrix_window *window) {
	if (window == NULL || window->window_state == NULL ||
			!window->mapped || window->minimized) {
		return;
	}
	struct seyrix_server *server = window->server;
	seyrix_popup_dismiss_window(window);
	window->window_state->minimized = true;
	window->minimized = true;
	wlr_scene_node_set_enabled(&window->scene_tree->node, false);
	if (server->seat != NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
	}
	if (server->grabbed_window == window) {
		seyrix_reset_cursor_mode(server);
	}
	if (server->focused_window == window) {
		seyrix_focus_window(server,
			seyrix_workspace_focus_candidate(server, window));
	}
	wlr_log(WLR_INFO, "Window minimized into compositor hidden state");
}

static void handle_request_minimize(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_window_state *state =
		wl_container_of(listener, state, request_minimize);
	if (state->window->xdg_toplevel->requested.minimized) {
		seyrix_window_state_minimize(state->window);
	} else if (state->minimized) {
		state->minimized = false;
		state->window->minimized = false;
		wlr_scene_node_set_enabled(&state->window->scene_tree->node, true);
		if (seyrix_workspace_window_is_visible(state->window)) {
			seyrix_focus_window(state->window->server, state->window);
		}
	}
}

bool seyrix_window_state_attach(struct seyrix_window *window) {
	struct seyrix_window_state *state = calloc(1, sizeof(*state));
	if (state == NULL) {
		return false;
	}
	state->window = window;
	state->desired_maximized = window->xdg_toplevel->requested.maximized;
	state->desired_fullscreen = window->xdg_toplevel->requested.fullscreen;
	state->output = window->xdg_toplevel->requested.fullscreen_output;
	window->window_state = state;

	state->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&window->xdg_toplevel->events.request_maximize,
		&state->request_maximize);
	state->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&window->xdg_toplevel->events.request_fullscreen,
		&state->request_fullscreen);
	state->request_minimize.notify = handle_request_minimize;
	wl_signal_add(&window->xdg_toplevel->events.request_minimize,
		&state->request_minimize);

	return true;
}

void seyrix_window_state_initial_commit(struct seyrix_window *window) {
	if (window == NULL || window->window_state == NULL) {
		return;
	}
	struct seyrix_window_state *state = window->window_state;
	struct wlr_xdg_surface *surface = window->xdg_toplevel->base;
	/* WM capabilities schedule a configure, so send them after initial commit. */
	if (!state->capabilities_sent && surface->initialized) {
		if (surface->client->shell->version >= 5) {
			wlr_xdg_toplevel_set_wm_capabilities(window->xdg_toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE |
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN |
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE);
		}
		state->capabilities_sent = true;
	}
	if (window->window_state->desired_maximized ||
			window->window_state->desired_fullscreen) {
		apply_requested_state(window);
	}
}

void seyrix_window_state_commit(struct seyrix_window *window) {
	if (window == NULL || window->window_state == NULL) {
		return;
	}
	struct seyrix_window_state *state = window->window_state;
	if (window->xdg_toplevel->current.maximized ||
			window->xdg_toplevel->current.fullscreen) {
		return;
	}
	save_normal_box(window);
	if (window->mapped && !window->minimized &&
			seyrix_workspace_window_is_visible(window)) {
		wlr_scene_node_set_enabled(&window->scene_tree->node, true);
	}
	state->output = window_output(window);
}

void seyrix_window_state_capture_normal(struct seyrix_window *window) {
	if (window == NULL || window->window_state == NULL ||
			window->xdg_toplevel->current.maximized ||
			window->xdg_toplevel->current.fullscreen) {
		return;
	}
	save_normal_box(window);
}

void seyrix_window_state_reconfigure(struct seyrix_window *window) {
	apply_requested_state(window);
}

bool seyrix_window_state_restore_minimized(struct seyrix_server *server) {
	struct seyrix_window *window;
	wl_list_for_each(window, &server->windows, link) {
		if (!window->mapped || !window->minimized ||
				window->workspace !=
					&server->workspaces[server->active_workspace_index]) {
			continue;
		}
		window->minimized = false;
		window->window_state->minimized = false;
		wlr_scene_node_set_enabled(&window->scene_tree->node, true);
		seyrix_focus_window(server, window);
		wlr_log(WLR_INFO, "Restored minimized window");
		return true;
	}
	return false;
}

void seyrix_window_state_destroy(struct seyrix_window *window) {
	if (window == NULL || window->window_state == NULL) {
		return;
	}
	struct seyrix_window_state *state = window->window_state;
	remove_listener(&state->request_maximize);
	remove_listener(&state->request_fullscreen);
	remove_listener(&state->request_minimize);
	free(state);
	window->window_state = NULL;
}

void seyrix_window_state_outputs_changed(struct seyrix_server *server) {
	for (size_t workspace_index = 0;
			workspace_index < SEYRIX_WORKSPACE_COUNT; workspace_index++) {
		struct seyrix_window *window;
		wl_list_for_each(window, &server->workspaces[workspace_index].windows,
				workspace_link) {
			if (window->window_state == NULL) {
				continue;
			}
			if (!window->mapped) {
				if (!output_is_live(server, window->window_state->output)) {
					window->window_state->output = NULL;
				}
			if (!output_is_live(server,
						window->xdg_toplevel->requested.fullscreen_output)) {
					window->xdg_toplevel->requested.fullscreen_output = NULL;
				}
				continue;
			}
			if (window->window_state->normal_box_valid &&
					!window->xdg_toplevel->current.maximized &&
					!window->xdg_toplevel->current.fullscreen) {
				constrain_normal_box(window);
			}
			apply_requested_state(window);
		}
	}
}

void seyrix_window_state_output_removed(struct seyrix_server *server,
		struct wlr_output *output) {
	for (size_t workspace_index = 0;
			workspace_index < SEYRIX_WORKSPACE_COUNT; workspace_index++) {
		struct seyrix_window *window;
		wl_list_for_each(window, &server->workspaces[workspace_index].windows,
				workspace_link) {
			if (window->window_state == NULL ||
					(window->window_state->output != output &&
					window->xdg_toplevel->requested.fullscreen_output != output)) {
				continue;
			}
			window->window_state->output = NULL;
			if (window->xdg_toplevel->requested.fullscreen_output == output) {
				window->xdg_toplevel->requested.fullscreen_output = NULL;
			}
		}
	}
	seyrix_window_state_outputs_changed(server);
}
