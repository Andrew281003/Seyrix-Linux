#include "seyrix.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>

#include <wlr/types/wlr_data_device.h>
#include <wlr/util/log.h>

#include "decoration.h"
#include "geometry.h"
#include "shell.h"
#include "window-state.h"

static void remove_listener(struct wl_listener *listener) {
	if (listener->link.prev != NULL && listener->link.next != NULL) {
		wl_list_remove(&listener->link);
		wl_list_init(&listener->link);
	}
}

static void update_seat_capabilities(struct seyrix_server *server) {
	uint32_t capabilities = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, capabilities);
}

static bool consume_shortcut_release(struct seyrix_keyboard *keyboard,
		uint32_t keycode) {
	for (size_t i = 0; i < keyboard->consumed_keycode_count; i++) {
		if (keyboard->consumed_keycodes[i] == keycode) {
			keyboard->consumed_keycodes[i] = keyboard->consumed_keycodes[
				--keyboard->consumed_keycode_count];
			return true;
		}
	}
	return false;
}

static bool shortcut_key_is_down(struct seyrix_keyboard *keyboard,
		uint32_t keycode) {
	for (size_t i = 0; i < keyboard->consumed_keycode_count; i++) {
		if (keyboard->consumed_keycodes[i] == keycode) {
			return true;
		}
	}
	return false;
}

static void remember_shortcut_press(struct seyrix_keyboard *keyboard,
		uint32_t keycode) {
	if (keyboard->consumed_keycode_count <
			sizeof(keyboard->consumed_keycodes) / sizeof(keyboard->consumed_keycodes[0])) {
		keyboard->consumed_keycodes[keyboard->consumed_keycode_count++] = keycode;
	}
}

static bool handle_shortcut(struct seyrix_keyboard *keyboard,
		const struct wlr_keyboard_key_event *event, xkb_keysym_t sym) {
	struct seyrix_server *server = keyboard->server;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if ((modifiers & WLR_MODIFIER_LOGO) == 0 ||
			event->state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return false;
	}

	bool handled = true;
	if (sym == XKB_KEY_Return) {
		if (!seyrix_spawn_terminal(server)) {
			wlr_log(WLR_ERROR, "Super+Return could not start weston-terminal");
		}
	} else if ((sym == XKB_KEY_q || sym == XKB_KEY_Q) &&
			(modifiers & WLR_MODIFIER_SHIFT) != 0) {
		wlr_log(WLR_INFO, "Compositor exit requested by Super+Shift+Q");
		wl_display_terminate(server->display);
	} else if ((sym == XKB_KEY_q || sym == XKB_KEY_Q) &&
			server->focused_window != NULL &&
			seyrix_workspace_window_is_visible(server->focused_window)) {
		wlr_log(WLR_INFO, "Close requested for focused application window");
		wlr_xdg_toplevel_send_close(server->focused_window->xdg_toplevel);
	} else if ((sym == XKB_KEY_m || sym == XKB_KEY_M) &&
			(modifiers & WLR_MODIFIER_SHIFT) != 0) {
		seyrix_window_state_restore_minimized(server);
	} else if ((sym == XKB_KEY_m || sym == XKB_KEY_M) &&
			server->focused_window != NULL) {
		seyrix_window_state_minimize(server->focused_window);
	} else if ((sym == XKB_KEY_m || sym == XKB_KEY_M) &&
			seyrix_window_state_restore_minimized(server)) {
		/* Super+M restores the most recently listed minimized window if none is focused. */
	} else if ((modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL)) == 0 &&
			(sym == XKB_KEY_Left || sym == XKB_KEY_Right)) {
		int direction = sym == XKB_KEY_Left ? -1 : 1;
		if ((modifiers & WLR_MODIFIER_SHIFT) != 0) {
			seyrix_workspace_move_focused(server, direction);
		} else {
			seyrix_workspace_switch_relative(server, direction);
		}
	} else if ((modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL |
			WLR_MODIFIER_SHIFT)) == 0 && sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
		size_t index = (size_t)(sym - XKB_KEY_1);
		/* Only the four initial workspaces exist in this phase. */
		if (index < SEYRIX_WORKSPACE_COUNT) {
			seyrix_workspace_switch(server, index);
		}
	} else {
		handled = false;
	}
	return handled;
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
	struct seyrix_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = keyboard->server->seat;

	if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED &&
			consume_shortcut_release(keyboard, event->keycode)) {
		return;
	}
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
			shortcut_key_is_down(keyboard, event->keycode)) {
		return;
	}

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms = NULL;
	int count = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state,
		keycode, &syms);
	for (int i = 0; i < count; i++) {
		if (handle_shortcut(keyboard, event, syms[i])) {
			remember_shortcut_press(keyboard, event->keycode);
			return;
		}
	}

	if (keyboard->server->focused_window != NULL &&
			!seyrix_workspace_window_is_visible(keyboard->server->focused_window)) {
		seyrix_focus_window(keyboard->server,
			seyrix_workspace_focus_candidate(keyboard->server, NULL));
	}
	wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
		event->state);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	struct seyrix_server *server = keyboard->server;
	bool was_active = wlr_seat_get_keyboard(server->seat) == keyboard->wlr_keyboard;

	remove_listener(&keyboard->modifiers);
	remove_listener(&keyboard->key);
	remove_listener(&keyboard->destroy);
	wl_list_remove(&keyboard->link);
	wl_list_init(&keyboard->link);
	free(keyboard);

	if (was_active) {
		if (wl_list_empty(&server->keyboards)) {
			wlr_seat_set_keyboard(server->seat, NULL);
			wlr_seat_keyboard_notify_clear_focus(server->seat);
		} else {
			struct seyrix_keyboard *replacement =
				wl_container_of(server->keyboards.next, replacement, link);
			wlr_seat_set_keyboard(server->seat, replacement->wlr_keyboard);
		}
	}
	update_seat_capabilities(server);
}

static bool configure_keyboard(struct seyrix_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
	struct seyrix_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (keyboard == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate keyboard state for %s", device->name);
		return false;
	}

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context == NULL) {
		wlr_log(WLR_ERROR, "Cannot create XKB context for %s", device->name);
		free(keyboard);
		return false;
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL) {
		wlr_log(WLR_ERROR, "Cannot create XKB keymap for %s", device->name);
		xkb_context_unref(context);
		free(keyboard);
		return false;
	}

	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	keyboard->modifiers.notify = handle_keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = handle_keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = handle_keyboard_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);
	wl_list_insert(&server->keyboards, &keyboard->link);
	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wlr_log(WLR_INFO, "Keyboard discovered: %s", device->name);
	return true;
}

static void handle_new_input(struct wl_listener *listener, void *data) {
	struct seyrix_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		configure_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		wlr_cursor_attach_input_device(server->cursor, device);
		wlr_log(WLR_INFO, "Pointer discovered: %s", device->name);
		break;
	default:
		break;
	}
	update_seat_capabilities(server);
}

static void handle_request_cursor(struct wl_listener *listener, void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	if (server->seat->pointer_state.focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

static void handle_pointer_focus_change(struct wl_listener *listener,
		void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, pointer_focus_change);
	struct wlr_seat_pointer_focus_change_event *event = data;
	if (event->new_surface == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
	}
}

static void handle_request_set_selection(struct wl_listener *listener,
		void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct seyrix_window *window_at(struct seyrix_server *server,
		double x, double y, struct wlr_surface **surface,
		double *surface_x, double *surface_y) {
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
		x, y, surface_x, surface_y);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(
		wlr_scene_buffer_from_node(node));
	if (scene_surface == NULL) {
		return NULL;
	}
	*surface = scene_surface->surface;

	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return tree != NULL ? tree->node.data : NULL;
}

static void window_identity(struct seyrix_window *window, pid_t *pid,
		uint32_t *surface_id) {
	*pid = 0;
	*surface_id = 0;
	if (window == NULL || window->xdg_toplevel == NULL ||
			window->xdg_toplevel->base == NULL) {
		return;
	}
	struct wlr_xdg_surface *surface = window->xdg_toplevel->base;
	if (surface->resource != NULL) {
		*surface_id = wl_resource_get_id(surface->resource);
	}
	if (surface->client != NULL && surface->client->client != NULL) {
		wl_client_get_credentials(surface->client->client, pid, NULL, NULL);
	}
}

static void process_cursor_resize(struct seyrix_server *server) {
	struct seyrix_window *window = server->grabbed_window;
	if (window == NULL || !window->mapped) {
		seyrix_reset_cursor_mode(server);
		return;
	}

	struct wlr_xdg_toplevel *toplevel = window->xdg_toplevel;
	struct seyrix_resize_geometry resize;
	if (!seyrix_calculate_resize(&server->grab_box,
			server->cursor->x, server->cursor->y,
			server->grab_x, server->grab_y, server->resize_edges,
			toplevel->current.min_width, toplevel->current.min_height,
			toplevel->current.max_width, toplevel->current.max_height,
			window->xdg_toplevel->base->geometry.x,
			window->xdg_toplevel->base->geometry.y,
			seyrix_decoration_titlebar_height(window), &resize)) {
		return;
	}
	wlr_scene_node_set_position(&window->scene_tree->node,
		resize.scene_x, resize.scene_y);
	wlr_xdg_toplevel_set_size(toplevel, resize.width, resize.height);
}

static void process_cursor_motion(struct seyrix_server *server,
		uint32_t time_msec) {
	if (server->cursor_mode == SEYRIX_CURSOR_MOVE) {
		if (server->grabbed_window != NULL && server->grabbed_window->mapped) {
			pid_t pid;
			uint32_t surface_id;
			window_identity(server->grabbed_window, &pid, &surface_id);
			int x = (int)(server->cursor->x - server->grab_x);
			int y = (int)(server->cursor->y - server->grab_y);
			wlr_scene_node_set_position(
				&server->grabbed_window->scene_tree->node, x, y);
			wlr_log(WLR_INFO,
				"Window move update: pid=%ld surface=%u cursor=%.1f,%.1f position=%d,%d",
				(long)pid, surface_id, server->cursor->x, server->cursor->y,
				x, y);
		}
		return;
	}
	if (server->cursor_mode == SEYRIX_CURSOR_RESIZE) {
		process_cursor_resize(server);
		return;
	}

	struct wlr_surface *surface = NULL;
	double sx = 0.0;
	double sy = 0.0;
	struct seyrix_window *window = NULL;
	if (!seyrix_layer_shell_surface_at(server, server->cursor->x,
			server->cursor->y, &surface, &sx, &sy)) {
		window = window_at(server, server->cursor->x,
			server->cursor->y, &surface, &sx, &sy);
	}
	if (window == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
	}
	if (surface != NULL) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
	} else {
		wlr_seat_pointer_notify_clear_focus(server->seat);
	}
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
		void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	if (!seyrix_cursor_warp_absolute(server->cursor, &event->pointer->base,
			event->x, event->y)) {
		wlr_log(WLR_ERROR,
			"Ignored invalid absolute pointer coordinates device=%s x=%.5f y=%.5f",
			event->pointer->base.name, event->x, event->y);
		return;
	}
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	uint32_t modifiers = keyboard != NULL ?
		wlr_keyboard_get_modifiers(keyboard) : 0;
	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
			(event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
		wlr_log(WLR_INFO, "Pointer button press button=%u modifiers=0x%x cursor=%.1f,%.1f",
			event->button, modifiers, server->cursor->x, server->cursor->y);
	}
	if (event->button == BTN_RIGHT &&
			event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		wlr_log(WLR_INFO, "Right pointer press modifiers=0x%x cursor=%.1f,%.1f",
			modifiers, server->cursor->x, server->cursor->y);
	}

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
			server->cursor_mode != SEYRIX_CURSOR_PASSTHROUGH) {
		seyrix_reset_cursor_mode(server);
		if (server->consumed_pointer_button == event->button) {
			server->consumed_pointer_button = 0;
		}
		return;
	}
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
			server->consumed_pointer_button == event->button) {
		server->consumed_pointer_button = 0;
		return;
	}
	if (seyrix_decoration_pointer_button(server, server->cursor->x,
			server->cursor->y, event->button, event->state)) {
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
			server->consumed_pointer_button = event->button;
		}
		return;
	}

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
			(modifiers & WLR_MODIFIER_ALT) != 0 &&
			(event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
		struct wlr_surface *surface = NULL;
		double sx = 0.0;
		double sy = 0.0;
		struct seyrix_window *window = window_at(server, server->cursor->x,
			server->cursor->y, &surface, &sx, &sy);
		wlr_log(WLR_INFO, "Alt pointer gesture button=%u cursor=%.1f,%.1f target_app_id=%s",
			event->button, server->cursor->x, server->cursor->y,
			window != NULL && window->xdg_toplevel->app_id != NULL ?
				window->xdg_toplevel->app_id : "none");
		if (window != NULL) {
			seyrix_focus_window(server, window);
			if (event->button == BTN_LEFT) {
				seyrix_begin_interactive(window, SEYRIX_CURSOR_MOVE, 0);
			} else {
				seyrix_begin_interactive(window, SEYRIX_CURSOR_RESIZE,
					WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM);
			}
			return;
		}
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
		event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		struct wlr_surface *surface = NULL;
		double sx = 0.0;
		double sy = 0.0;
		if (seyrix_layer_shell_surface_at(server, server->cursor->x,
				server->cursor->y, &surface, &sx, &sy)) {
			seyrix_layer_shell_focus_keyboard(server, surface);
		} else {
			struct seyrix_window *window = window_at(server, server->cursor->x,
				server->cursor->y, &surface, &sx, &sy);
			seyrix_focus_window(server, window);
		}
	}
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
		event->orientation, event->delta, event->delta_discrete,
		event->source, event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_server *server =
		wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

bool seyrix_input_init(struct seyrix_server *server) {
	server->cursor = wlr_cursor_create();
	if (server->cursor == NULL) {
		wlr_log(WLR_ERROR, "Cannot create compositor cursor");
		return false;
	}
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
	server->cursor_manager = wlr_xcursor_manager_create(NULL, 24);
	if (server->cursor_manager == NULL ||
			!wlr_xcursor_manager_load(server->cursor_manager, 1)) {
		wlr_log(WLR_ERROR, "Cannot load default cursor theme");
		return false;
	}
	wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");

	server->seat = wlr_seat_create(server->display, "seat0");
	if (server->seat == NULL) {
		wlr_log(WLR_ERROR, "Cannot create input seat");
		return false;
	}

	server->cursor_motion.notify = handle_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = handle_cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute,
		&server->cursor_motion_absolute);
	server->cursor_button.notify = handle_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = handle_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = handle_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	server->new_input.notify = handle_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);
	server->request_cursor.notify = handle_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor,
		&server->request_cursor);
	server->pointer_focus_change.notify = handle_pointer_focus_change;
	wl_signal_add(&server->seat->pointer_state.events.focus_change,
		&server->pointer_focus_change);
	server->request_set_selection.notify = handle_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection,
		&server->request_set_selection);
	server->cursor_mode = SEYRIX_CURSOR_PASSTHROUGH;
	return true;
}

void seyrix_input_finish(struct seyrix_server *server) {
	remove_listener(&server->new_input);
	remove_listener(&server->cursor_motion);
	remove_listener(&server->cursor_motion_absolute);
	remove_listener(&server->cursor_button);
	remove_listener(&server->cursor_axis);
	remove_listener(&server->cursor_frame);
	remove_listener(&server->request_cursor);
	remove_listener(&server->pointer_focus_change);
	remove_listener(&server->request_set_selection);
}
