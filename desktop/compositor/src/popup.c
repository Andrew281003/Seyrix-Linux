#include "popup.h"

#include <stdlib.h>

#include <wlr/util/log.h>

#include "window-state.h"

struct seyrix_popup {
	struct wl_list link;
	struct seyrix_server *server;
	struct wlr_xdg_popup *xdg_popup;
	struct seyrix_window *window;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener commit;
	struct wl_listener reposition;
	struct wl_listener scene_destroy;
	struct wl_listener destroy;
};

static void remove_listener(struct wl_listener *listener) {
	if (listener->link.prev != NULL && listener->link.next != NULL) {
		wl_list_remove(&listener->link);
		wl_list_init(&listener->link);
	}
}

static struct seyrix_window *window_for_xdg_surface(
		struct wlr_xdg_surface *surface) {
	for (unsigned int depth = 0; surface != NULL && depth < 32; depth++) {
		if (surface->role == WLR_XDG_SURFACE_ROLE_POPUP &&
				surface->popup != NULL) {
			surface = wlr_xdg_surface_try_from_wlr_surface(surface->popup->parent);
			continue;
		}
		if (surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL &&
				surface->data != NULL) {
			struct wlr_scene_tree *tree = surface->data;
			while (tree != NULL) {
				if (tree->node.data != NULL) {
					return tree->node.data;
				}
				tree = tree->node.parent;
			}
		}
		return NULL;
	}
	return NULL;
}

static void unconstrain_popup(struct seyrix_popup *popup) {
	struct seyrix_window *window = popup->window;
	if (popup->server->output_layout == NULL) {
		return;
	}
	if (window == NULL) {
		if (popup->layer_surface == NULL || popup->scene_tree == NULL) {
			return;
		}
		struct wlr_output *output = popup->layer_surface->output;
		struct wlr_box output_box = {0};
		if (output != NULL) {
			wlr_output_layout_get_box(popup->server->output_layout, output,
				&output_box);
		}
		struct wlr_scene_tree *parent_tree = popup->layer_surface->data;
		if (output_box.width <= 0 || output_box.height <= 0 || parent_tree == NULL) {
			return;
		}
		struct wlr_box constraint = {
			.x = output_box.x - parent_tree->node.x,
			.y = output_box.y - parent_tree->node.y,
			.width = output_box.width,
			.height = output_box.height,
		};
		wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &constraint);
		return;
	}
	struct wlr_output *output = seyrix_window_state_output(window);
	struct wlr_box current_output = {0};
	if (output != NULL) {
		wlr_output_layout_get_box(window->server->output_layout, output,
			&current_output);
	}
	if (output == NULL || current_output.width <= 0 || current_output.height <= 0) {
		output = wlr_output_layout_output_at(window->server->output_layout,
			window->scene_tree->node.x, window->scene_tree->node.y);
	}
	struct wlr_box output_box = {0};
	if (output != NULL) {
		wlr_output_layout_get_box(window->server->output_layout, output, &output_box);
	}
	if (output_box.width <= 0 || output_box.height <= 0) {
		wlr_output_layout_get_box(window->server->output_layout, NULL, &output_box);
	}
	if (output_box.width <= 0 || output_box.height <= 0) {
		return;
	}

	struct wlr_xdg_surface *toplevel = window->xdg_toplevel->base;
	int surface_origin_x = window->surface_tree->node.x - toplevel->geometry.x;
	int surface_origin_y = window->surface_tree->node.y - toplevel->geometry.y;
	struct wlr_box constraint = {
		.x = output_box.x - window->scene_tree->node.x - surface_origin_x,
		.y = output_box.y - window->scene_tree->node.y - surface_origin_y,
		.width = output_box.width,
		.height = output_box.height,
	};
	wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &constraint);
}

static void handle_popup_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_popup *popup = wl_container_of(listener, popup, commit);
	if (!popup->xdg_popup->base->initial_commit) {
		return;
	}
	if (popup->scene_tree == NULL) {
		wlr_log(WLR_ERROR, "Rejecting unparented xdg popup at its initial commit");
		wlr_xdg_popup_destroy(popup->xdg_popup);
		return;
	}
	unconstrain_popup(popup);
	wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	struct seyrix_window *window = popup->window;
	wlr_log(WLR_INFO, "xdg popup initial configure parent_app_id=%s",
		window != NULL && window->xdg_toplevel->app_id != NULL ?
		window->xdg_toplevel->app_id : "");
}

static void handle_popup_reposition(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_popup *popup = wl_container_of(listener, popup, reposition);
	if (popup->scene_tree == NULL) {
		return;
	}
	unconstrain_popup(popup);
	wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
}

static void handle_popup_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_popup *popup = wl_container_of(listener, popup, destroy);
	struct wlr_surface *focused = popup->xdg_popup->base->surface;
	if (popup->server->seat != NULL) {
		if (popup->server->seat->pointer_state.focused_surface == focused) {
			wlr_seat_pointer_notify_clear_focus(popup->server->seat);
		}
		if (popup->server->seat->keyboard_state.focused_surface == focused) {
			wlr_seat_keyboard_notify_clear_focus(popup->server->seat);
		}
	}
	remove_listener(&popup->commit);
	remove_listener(&popup->reposition);
	remove_listener(&popup->scene_destroy);
	if (popup->xdg_popup->base->data == popup->scene_tree) {
		popup->xdg_popup->base->data = NULL;
	}
	if (popup->scene_tree != NULL) {
		wlr_scene_node_destroy(&popup->scene_tree->node);
	}
	remove_listener(&popup->destroy);
	wl_list_remove(&popup->link);
	wl_list_init(&popup->link);
	free(popup);
}

static void handle_popup_scene_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_popup *popup =
		wl_container_of(listener, popup, scene_destroy);
	remove_listener(&popup->scene_destroy);
	if (popup->xdg_popup->base->data == popup->scene_tree) {
		popup->xdg_popup->base->data = NULL;
	}
	popup->scene_tree = NULL;
}

static struct seyrix_popup *find_popup(struct seyrix_server *server,
		struct wlr_xdg_popup *xdg_popup) {
	struct seyrix_popup *popup;
	wl_list_for_each(popup, &server->popups, link) {
		if (popup->xdg_popup == xdg_popup) {
			return popup;
		}
	}
	return NULL;
}

static struct seyrix_popup *create_popup_record(struct seyrix_server *server,
		struct wlr_xdg_popup *xdg_popup) {
	struct seyrix_popup *popup = calloc(1, sizeof(*popup));
	if (popup == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate xdg popup state");
		return NULL;
	}
	popup->xdg_popup = xdg_popup;
	popup->server = server;
	wl_list_insert(&server->popups, &popup->link);
	popup->commit.notify = handle_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
	popup->reposition.notify = handle_popup_reposition;
	wl_signal_add(&xdg_popup->events.reposition, &popup->reposition);
	popup->destroy.notify = handle_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
	return popup;
}

static bool attach_popup(struct seyrix_popup *popup,
		struct wlr_scene_tree *parent_tree, struct seyrix_window *window,
		struct wlr_layer_surface_v1 *layer_surface) {
	if (popup->scene_tree != NULL) {
		return true;
	}
	if (parent_tree == NULL) {
		return false;
	}
	popup->window = window;
	popup->layer_surface = layer_surface;
	popup->scene_tree = wlr_scene_xdg_surface_create(
		parent_tree, popup->xdg_popup->base);
	if (popup->scene_tree == NULL) {
		wlr_log(WLR_ERROR, "Cannot add xdg popup to the parent scene tree");
		return false;
	}
	/* xdg popup descendants use this node as their parent scene tree. */
	popup->xdg_popup->base->data = popup->scene_tree;
	popup->scene_destroy.notify = handle_popup_scene_destroy;
	wl_signal_add(&popup->scene_tree->node.events.destroy,
		&popup->scene_destroy);
	return true;
}

static struct wlr_layer_surface_v1 *layer_parent_for_xdg_surface(
		struct wlr_xdg_surface *surface) {
	for (unsigned int depth = 0; surface != NULL && depth < 32; depth++) {
		if (surface->role != WLR_XDG_SURFACE_ROLE_POPUP ||
				surface->popup == NULL) {
			return NULL;
		}
		struct wlr_surface *parent_surface = surface->popup->parent;
		struct wlr_layer_surface_v1 *layer_parent =
			wlr_layer_surface_v1_try_from_wlr_surface(parent_surface);
		if (layer_parent != NULL) {
			return layer_parent;
		}
		surface = wlr_xdg_surface_try_from_wlr_surface(parent_surface);
	}
	return NULL;
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct seyrix_server *server = wl_container_of(listener, server, new_popup);
	struct wlr_xdg_popup *xdg_popup = data;
	struct seyrix_popup *popup = find_popup(server, xdg_popup);
	if (popup == NULL) {
		popup = create_popup_record(server, xdg_popup);
	}
	if (popup == NULL) {
		wlr_xdg_popup_destroy(xdg_popup);
		return;
	}
	if (popup->scene_tree != NULL) {
		return;
	}
	if (xdg_popup->parent == NULL) {
		wlr_log(WLR_DEBUG,
			"Deferring xdg popup parenting until its layer surface associates it");
		return;
	}
	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	struct wlr_scene_tree *parent_tree = parent != NULL ? parent->data : NULL;
	struct wlr_layer_surface_v1 *layer_parent = NULL;
	if (parent_tree == NULL) {
		layer_parent = wlr_layer_surface_v1_try_from_wlr_surface(xdg_popup->parent);
		if (layer_parent != NULL) {
			parent_tree = layer_parent->data;
		}
	}
	if (parent_tree == NULL) {
		wlr_log(WLR_ERROR, "Rejecting xdg popup with no managed parent scene tree");
		wlr_xdg_popup_destroy(xdg_popup);
		return;
	}
	if (layer_parent == NULL && parent != NULL) {
		layer_parent = layer_parent_for_xdg_surface(parent);
	}
	if (!attach_popup(popup, parent_tree,
			parent != NULL ? window_for_xdg_surface(parent) : NULL, layer_parent)) {
		wlr_xdg_popup_destroy(xdg_popup);
	}
}

void seyrix_popup_attach_layer_surface(struct seyrix_server *server,
		struct wlr_layer_surface_v1 *surface, struct wlr_xdg_popup *xdg_popup) {
	struct seyrix_popup *popup = find_popup(server, xdg_popup);
	if (popup == NULL) {
		popup = create_popup_record(server, xdg_popup);
	}
	if (popup == NULL || !attach_popup(popup, surface->data, NULL, surface)) {
		wlr_xdg_popup_destroy(xdg_popup);
	}
}

static void dismiss_popup_descendants(struct wlr_xdg_surface *surface,
		unsigned int depth) {
	if (surface == NULL || depth >= 32) {
		return;
	}
	while (!wl_list_empty(&surface->popups)) {
		struct wlr_xdg_popup *popup = wl_container_of(surface->popups.next,
			popup, link);
		dismiss_popup_descendants(popup->base, depth + 1);
		wlr_xdg_popup_destroy(popup);
	}
}

void seyrix_popup_dismiss_window(struct seyrix_window *window) {
	if (window == NULL || window->xdg_toplevel == NULL ||
			window->xdg_toplevel->base == NULL) {
		return;
	}
	struct wlr_xdg_surface *surface = window->xdg_toplevel->base;
	if (!wl_list_empty(&surface->popups)) {
		dismiss_popup_descendants(surface, 0);
		wlr_log(WLR_INFO, "Dismissed popup descendants before hiding toplevel");
	}
}

bool seyrix_popup_init(struct seyrix_server *server) {
	wl_list_init(&server->popups);
	server->new_popup.notify = handle_new_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_popup);
	return true;
}

void seyrix_popup_dismiss_layer_surface(struct wlr_layer_surface_v1 *surface) {
	if (surface == NULL) {
		return;
	}
	while (!wl_list_empty(&surface->popups)) {
		struct wlr_xdg_popup *popup = wl_container_of(surface->popups.next,
			popup, link);
		dismiss_popup_descendants(popup->base, 1);
		wlr_xdg_popup_destroy(popup);
	}
}

void seyrix_popup_finish(struct seyrix_server *server) {
	remove_listener(&server->new_popup);
	while (!wl_list_empty(&server->popups)) {
		struct seyrix_popup *popup = wl_container_of(server->popups.next,
			popup, link);
		wlr_xdg_popup_destroy(popup->xdg_popup);
	}
}
