#include "layer-shell.h"

#include <stdlib.h>

#include <wlr/util/log.h>

#include "popup.h"
#include "window-state.h"

struct seyrix_layer_surface {
	struct wl_list link;
	struct seyrix_server *server;
	struct wlr_layer_surface_v1 *surface;
	struct wlr_scene_layer_surface_v1 *scene_surface;
	struct wlr_output *output;
	enum zwlr_layer_shell_v1_layer layer;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener new_popup;
	struct wl_listener destroy;
};

static void remove_listener(struct wl_listener *listener) {
	if (listener->link.prev != NULL && listener->link.next != NULL) {
		wl_list_remove(&listener->link);
		wl_list_init(&listener->link);
	}
}

static struct wlr_output *first_output(struct seyrix_server *server,
		struct wlr_output *exclude) {
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output != exclude && output->wlr_output->enabled) {
			return output->wlr_output;
		}
	}
	return NULL;
}

static struct wlr_scene_tree *scene_layer(struct seyrix_server *server,
		enum zwlr_layer_shell_v1_layer layer) {
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		return server->layer_background;
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		return server->layer_bottom;
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		return server->layer_top;
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
	default:
		return server->layer_overlay;
	}
}

static void arrange_output(struct seyrix_server *server,
		struct wlr_output *output) {
	if (output == NULL || server->output_layout == NULL) {
		return;
	}
	struct wlr_box full_area = {0};
	wlr_output_layout_get_box(server->output_layout, output, &full_area);
	if (full_area.width <= 0 || full_area.height <= 0) {
		return;
	}
	struct wlr_box usable_area = full_area;
	static const enum zwlr_layer_shell_v1_layer order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
	};
	for (size_t pass = 0; pass < 2; pass++) {
		for (size_t layer = 0; layer < sizeof(order) / sizeof(order[0]); layer++) {
			struct seyrix_layer_surface *surface;
			wl_list_for_each(surface, &server->layer_surfaces, link) {
				bool exclusive = surface->surface->current.exclusive_zone > 0;
				if (surface->output == output && surface->layer == order[layer] &&
						surface->scene_surface != NULL &&
						surface->surface->initialized && exclusive == (pass == 0)) {
					wlr_scene_layer_surface_v1_configure(surface->scene_surface,
						&full_area, &usable_area);
				}
			}
		}
	}
	struct seyrix_output *record;
	wl_list_for_each(record, &server->outputs, link) {
		if (record->wlr_output == output) {
			record->usable_area = usable_area;
			break;
		}
	}
}

void seyrix_layer_shell_output_work_area(struct seyrix_server *server,
		struct wlr_output *output, struct wlr_box *work_area) {
	if (output == NULL || work_area == NULL) {
		return;
	}
	wlr_output_layout_get_box(server->output_layout, output, work_area);
	struct seyrix_output *record;
	wl_list_for_each(record, &server->outputs, link) {
		if (record->wlr_output == output && record->usable_area.width > 0 &&
				record->usable_area.height > 0) {
			*work_area = record->usable_area;
			return;
		}
	}
}

void seyrix_layer_shell_outputs_changed(struct seyrix_server *server) {
	struct seyrix_layer_surface *surface;
	wl_list_for_each(surface, &server->layer_surfaces, link) {
		if (surface->output != NULL && surface->output->enabled) {
			continue;
		}
		struct wlr_output *replacement = first_output(server, surface->output);
		if (replacement != surface->output) {
			surface->output = replacement;
			surface->surface->output = replacement;
			wlr_log(WLR_INFO, "Layer surface %s moved to output %s",
				surface->surface->namespace != NULL ?
					surface->surface->namespace : "",
				replacement != NULL ? replacement->name : "none");
		}
	}
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		arrange_output(server, output->wlr_output);
	}
}

static void handle_layer_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_layer_surface *record =
		wl_container_of(listener, record, commit);
	uint32_t layout_fields = WLR_LAYER_SURFACE_V1_STATE_DESIRED_SIZE |
		WLR_LAYER_SURFACE_V1_STATE_ANCHOR |
		WLR_LAYER_SURFACE_V1_STATE_EXCLUSIVE_ZONE |
		WLR_LAYER_SURFACE_V1_STATE_MARGIN |
		WLR_LAYER_SURFACE_V1_STATE_LAYER |
		WLR_LAYER_SURFACE_V1_STATE_EXCLUSIVE_EDGE;
	bool layout_changed =
		(record->surface->current.committed & layout_fields) != 0;
	if (record->surface->current.layer != record->layer) {
		record->layer = record->surface->current.layer;
		wlr_scene_node_reparent(&record->scene_surface->tree->node,
			scene_layer(record->server, record->layer));
		layout_changed = true;
	}
	if (!layout_changed) {
		return;
	}
	struct seyrix_output *output;
	struct wlr_box previous_usable_area = {0};
	wl_list_for_each(output, &record->server->outputs, link) {
		if (output->wlr_output == record->output) {
			previous_usable_area = output->usable_area;
			break;
		}
	}
	arrange_output(record->server, record->output);
	wl_list_for_each(output, &record->server->outputs, link) {
		if (output->wlr_output == record->output &&
				(previous_usable_area.x != output->usable_area.x ||
				previous_usable_area.y != output->usable_area.y ||
				previous_usable_area.width != output->usable_area.width ||
				previous_usable_area.height != output->usable_area.height)) {
			seyrix_window_state_outputs_changed(record->server);
			seyrix_wm_outputs_changed(record->server);
			break;
		}
	}
}

static void handle_layer_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_layer_surface *record = wl_container_of(listener, record, map);
	arrange_output(record->server, record->output);
	seyrix_window_state_outputs_changed(record->server);
	seyrix_wm_outputs_changed(record->server);
	wlr_log(WLR_INFO, "Layer shell surface mapped namespace=%s",
		record->surface->namespace != NULL ? record->surface->namespace : "");
}

static void handle_layer_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_layer_surface *record = wl_container_of(listener, record, unmap);
	arrange_output(record->server, record->output);
	seyrix_window_state_outputs_changed(record->server);
	seyrix_wm_outputs_changed(record->server);
}

static void handle_layer_new_popup(struct wl_listener *listener, void *data) {
	struct seyrix_layer_surface *record =
		wl_container_of(listener, record, new_popup);
	seyrix_popup_attach_layer_surface(record->server, record->surface, data);
}

static void handle_layer_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_layer_surface *record =
		wl_container_of(listener, record, destroy);
	struct seyrix_server *server = record->server;
	seyrix_popup_dismiss_layer_surface(record->surface);
	remove_listener(&record->commit);
	remove_listener(&record->map);
	remove_listener(&record->unmap);
	remove_listener(&record->new_popup);
	remove_listener(&record->destroy);
	record->surface->data = NULL;
	wl_list_remove(&record->link);
	wl_list_init(&record->link);
	struct wlr_output *output = record->output;
	free(record);
	arrange_output(server, output);
	seyrix_window_state_outputs_changed(server);
	seyrix_wm_outputs_changed(server);
}

static void handle_new_layer_surface(struct wl_listener *listener, void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *surface = data;
	if (surface->output == NULL) {
		surface->output = first_output(server, NULL);
	}
	if (surface->output == NULL) {
		wlr_log(WLR_ERROR, "Closing layer surface because no output is available");
		wlr_layer_surface_v1_destroy(surface);
		return;
	}
	struct seyrix_layer_surface *record = calloc(1, sizeof(*record));
	if (record == NULL) {
		wlr_layer_surface_v1_destroy(surface);
		return;
	}
	record->server = server;
	record->surface = surface;
	record->output = surface->output;
	record->layer = surface->current.layer;
	record->scene_surface = wlr_scene_layer_surface_v1_create(
		scene_layer(server, record->layer), surface);
	if (record->scene_surface == NULL) {
		free(record);
		wlr_layer_surface_v1_destroy(surface);
		return;
	}
	record->scene_surface->tree->node.data = record;
	surface->data = record->scene_surface->tree;
	wl_list_insert(&server->layer_surfaces, &record->link);
	record->commit.notify = handle_layer_commit;
	wl_signal_add(&surface->surface->events.commit, &record->commit);
	record->map.notify = handle_layer_map;
	wl_signal_add(&surface->surface->events.map, &record->map);
	record->unmap.notify = handle_layer_unmap;
	wl_signal_add(&surface->surface->events.unmap, &record->unmap);
	record->new_popup.notify = handle_layer_new_popup;
	wl_signal_add(&surface->events.new_popup, &record->new_popup);
	record->destroy.notify = handle_layer_destroy;
	wl_signal_add(&surface->events.destroy, &record->destroy);
	arrange_output(server, record->output);
}

bool seyrix_layer_shell_surface_at(struct seyrix_server *server,
		double x, double y, struct wlr_surface **surface,
		double *surface_x, double *surface_y) {
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
		x, y, surface_x, surface_y);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return false;
	}
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(
		wlr_scene_buffer_from_node(node));
	if (scene_surface == NULL) {
		return false;
	}
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL) {
		struct seyrix_layer_surface *record;
		wl_list_for_each(record, &server->layer_surfaces, link) {
			if (tree->node.data == record) {
				*surface = scene_surface->surface;
				return true;
			}
		}
		tree = tree->node.parent;
	}
	return false;
}

void seyrix_layer_shell_focus_keyboard(struct seyrix_server *server,
		struct wlr_surface *surface) {
	struct wlr_layer_surface_v1 *layer = NULL;
	struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(surface);
	for (unsigned int depth = 0; xdg != NULL && depth < 32; depth++) {
		if (xdg->role == WLR_XDG_SURFACE_ROLE_POPUP && xdg->popup != NULL) {
			layer = wlr_layer_surface_v1_try_from_wlr_surface(xdg->popup->parent);
			if (layer != NULL) {
				break;
			}
			xdg = wlr_xdg_surface_try_from_wlr_surface(xdg->popup->parent);
			continue;
		}
		break;
	}
	if (layer == NULL) {
		layer = wlr_layer_surface_v1_try_from_wlr_surface(surface);
	}
	if (layer == NULL ||
			layer->current.keyboard_interactive ==
				ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
		return;
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(server->seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

bool seyrix_layer_shell_init(struct seyrix_server *server) {
	server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
	if (server->layer_shell == NULL) {
		wlr_log(WLR_ERROR, "Cannot create wlr-layer-shell protocol global");
		return false;
	}
	wl_list_init(&server->layer_surfaces);
	server->new_layer_surface.notify = handle_new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface,
		&server->new_layer_surface);
	return true;
}

void seyrix_layer_shell_output_removed(struct seyrix_server *server,
		struct wlr_output *output) {
	struct seyrix_layer_surface *surface;
	wl_list_for_each(surface, &server->layer_surfaces, link) {
		if (surface->output != output) {
			continue;
		}
		struct wlr_output *replacement = first_output(server, output);
		surface->output = replacement;
		surface->surface->output = replacement;
	}
	seyrix_layer_shell_outputs_changed(server);
}

void seyrix_layer_shell_finish(struct seyrix_server *server) {
	remove_listener(&server->new_layer_surface);
	server->layer_shell = NULL;
}
