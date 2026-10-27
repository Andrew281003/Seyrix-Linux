#include "seyrix.h"

#include <wlr/util/log.h>

#include "popup.h"

static struct seyrix_workspace *active_workspace(struct seyrix_server *server) {
	if (server->active_workspace_index >= SEYRIX_WORKSPACE_COUNT) {
		return NULL;
	}
	return &server->workspaces[server->active_workspace_index];
}

static bool has_active_popup_grab(struct seyrix_server *server) {
	if (server->xdg_shell == NULL || server->seat == NULL) {
		return false;
	}
	struct wlr_xdg_popup_grab *grab;
	wl_list_for_each(grab, &server->xdg_shell->popup_grabs, link) {
		if (grab->seat == server->seat && !wl_list_empty(&grab->popups)) {
			return true;
		}
	}
	return false;
}

static struct seyrix_window *window_from_parent_toplevel(
		struct wlr_xdg_toplevel *parent) {
	if (parent == NULL || parent->base == NULL || parent->base->data == NULL) {
		return NULL;
	}
	struct wlr_scene_tree *tree = parent->base->data;
	while (tree != NULL) {
		if (tree->node.data != NULL) {
			return tree->node.data;
		}
		tree = tree->node.parent;
	}
	return NULL;
}

bool seyrix_workspaces_init(struct seyrix_server *server) {
	if (server->window_layer == NULL) {
		wlr_log(WLR_ERROR, "Cannot initialize workspaces without a window scene layer");
		return false;
	}
	server->active_workspace_index = 0;
	for (size_t i = 0; i < SEYRIX_WORKSPACE_COUNT; i++) {
		struct seyrix_workspace *workspace = &server->workspaces[i];
		workspace->server = server;
		wl_list_init(&workspace->windows);
		workspace->focused_window = NULL;
		workspace->preferences = server->wm_config.workspaces[i];
		workspace->scene_tree = wlr_scene_tree_create(server->window_layer);
		if (workspace->scene_tree == NULL) {
			wlr_log(WLR_ERROR, "Cannot create scene tree for workspace %zu", i + 1);
			seyrix_workspaces_finish(server);
			return false;
		}
		workspace->tiled_layer = wlr_scene_tree_create(workspace->scene_tree);
		workspace->floating_layer = wlr_scene_tree_create(workspace->scene_tree);
		if (workspace->tiled_layer == NULL || workspace->floating_layer == NULL) {
			wlr_log(WLR_ERROR, "Cannot create window layers for workspace %zu", i + 1);
			seyrix_workspaces_finish(server);
			return false;
		}
		wlr_scene_node_set_enabled(&workspace->scene_tree->node, i == 0);
	}
	wlr_log(WLR_INFO, "Initialized %u workspaces", SEYRIX_WORKSPACE_COUNT);
	return true;
}

void seyrix_workspaces_finish(struct seyrix_server *server) {
	for (size_t i = 0; i < SEYRIX_WORKSPACE_COUNT; i++) {
		struct seyrix_workspace *workspace = &server->workspaces[i];
		struct seyrix_window *window, *temporary;
		if (workspace->windows.next != NULL && workspace->windows.prev != NULL) {
			wl_list_for_each_safe(window, temporary, &workspace->windows,
					workspace_link) {
				wl_list_remove(&window->workspace_link);
				wl_list_init(&window->workspace_link);
				window->workspace = NULL;
			}
		}
		if (workspace->scene_tree != NULL && server->scene != NULL) {
			wlr_scene_node_destroy(&workspace->scene_tree->node);
		}
		workspace->scene_tree = NULL;
		workspace->tiled_layer = NULL;
		workspace->floating_layer = NULL;
		seyrix_layout_destroy(&workspace->layout_root, NULL);
		workspace->output = NULL;
		workspace->server = NULL;
		workspace->focused_window = NULL;
		if (workspace->windows.next != NULL && workspace->windows.prev != NULL) {
			wl_list_init(&workspace->windows);
		}
	}
	server->active_workspace_index = 0;
}

struct wlr_scene_tree *seyrix_workspace_attach_window(
		struct seyrix_server *server, struct seyrix_window *window) {
	if (window == NULL || server->active_workspace_index >= SEYRIX_WORKSPACE_COUNT) {
		return NULL;
	}

	struct seyrix_workspace *workspace = NULL;
	struct wlr_xdg_toplevel *parent = window->xdg_toplevel != NULL ?
		window->xdg_toplevel->parent : NULL;
	struct seyrix_window *parent_window = window_from_parent_toplevel(parent);
	if (parent_window != NULL) {
		workspace = parent_window->workspace;
	}
	if (workspace == NULL) {
		workspace = active_workspace(server);
	}
	if (workspace == NULL || workspace->scene_tree == NULL) {
		wlr_log(WLR_ERROR, "Cannot attach window without an initialized workspace");
		return NULL;
	}

	window->workspace = workspace;
	wl_list_init(&window->workspace_link);
	wl_list_insert(&workspace->windows, &window->workspace_link);
	return workspace->scene_tree;
}

void seyrix_workspace_forget_window(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL) {
		return;
	}
	struct seyrix_workspace *workspace = window->workspace;
	if (workspace->focused_window == window) {
		workspace->focused_window = NULL;
	}
	if (window->workspace_link.next != NULL &&
			window->workspace_link.prev != NULL) {
		wl_list_remove(&window->workspace_link);
		wl_list_init(&window->workspace_link);
	}
	window->workspace = NULL;
}

void seyrix_workspace_window_mapped(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL ||
			window->workspace_link.next == NULL ||
			window->workspace_link.prev == NULL) {
		return;
	}
	/* A newly mapped surface becomes the topmost window in its workspace. */
	wl_list_remove(&window->workspace_link);
	wl_list_insert(&window->workspace->windows, &window->workspace_link);
}

void seyrix_workspace_window_unmapped(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL) {
		return;
	}
	if (window->workspace->focused_window == window) {
		window->workspace->focused_window = NULL;
	}
	seyrix_wm_window_unmapped(window);
}

void seyrix_workspace_record_focus(struct seyrix_server *server,
		struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL ||
			window->workspace != active_workspace(server) || !window->mapped) {
		return;
	}
	window->workspace->focused_window = window;
	if (window->workspace_link.next != NULL &&
			window->workspace_link.prev != NULL) {
		wl_list_remove(&window->workspace_link);
		wl_list_insert(&window->workspace->windows, &window->workspace_link);
	}
}

struct seyrix_window *seyrix_workspace_focus_candidate(
		struct seyrix_server *server, struct seyrix_window *exclude) {
	struct seyrix_workspace *workspace = active_workspace(server);
	if (workspace == NULL) {
		return NULL;
	}
	if (workspace->focused_window != NULL &&
			workspace->focused_window != exclude &&
			workspace->focused_window->mapped &&
			!workspace->focused_window->minimized) {
		return workspace->focused_window;
	}
	struct seyrix_window *window;
	wl_list_for_each(window, &workspace->windows, workspace_link) {
		if (window != exclude && window->mapped && !window->minimized) {
			return window;
		}
	}
	return NULL;
}

bool seyrix_workspace_window_is_visible(const struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL || window->server == NULL ||
			window->minimized) {
		return false;
	}
	if (window->server->active_workspace_index >= SEYRIX_WORKSPACE_COUNT) {
		return false;
	}
	return window->workspace ==
		&window->server->workspaces[window->server->active_workspace_index];
}

void seyrix_workspace_switch(struct seyrix_server *server, size_t index) {
	if (index >= SEYRIX_WORKSPACE_COUNT ||
			index == server->active_workspace_index) {
		return;
	}
	struct seyrix_workspace *source = active_workspace(server);
	if (source != NULL) {
		struct seyrix_window *window;
		wl_list_for_each(window, &source->windows, workspace_link) {
			seyrix_popup_dismiss_window(window);
		}
	}
	if (has_active_popup_grab(server)) {
		wlr_log(WLR_INFO, "Workspace switch deferred while an unrelated popup grab is active");
		return;
	}

	if (server->grabbed_window != NULL &&
			seyrix_workspace_window_is_visible(server->grabbed_window)) {
		seyrix_reset_cursor_mode(server);
	}
	for (size_t i = 0; i < SEYRIX_WORKSPACE_COUNT; i++) {
		struct seyrix_workspace *workspace = &server->workspaces[i];
		if (workspace->scene_tree != NULL) {
			struct seyrix_window *window;
			wl_list_for_each(window, &workspace->windows, workspace_link) {
				if (window->scene_tree != NULL) {
					wlr_scene_node_set_enabled(&window->scene_tree->node,
						!window->minimized);
				}
			}
			wlr_scene_node_set_enabled(&workspace->scene_tree->node, i == index);
		}
	}
	server->active_workspace_index = index;
	if (server->seat != NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
	}
	struct seyrix_window *window = seyrix_workspace_focus_candidate(server, NULL);
	seyrix_focus_window(server, window);
	seyrix_shell_update_active_workspace(server);
	wlr_log(WLR_INFO, "Switched to workspace %zu", index + 1);
}

void seyrix_workspace_switch_relative(struct seyrix_server *server,
		int direction) {
	if (direction == 0) {
		return;
	}
	size_t index = server->active_workspace_index;
	if (direction < 0) {
		if (index == 0) {
			return;
		}
		index--;
	} else {
		if (index + 1 >= SEYRIX_WORKSPACE_COUNT) {
			return;
		}
		index++;
	}
	seyrix_workspace_switch(server, index);
}

void seyrix_workspace_move_focused(struct seyrix_server *server,
		int direction) {
	struct seyrix_window *window = server->focused_window;
	if (window == NULL || !window->mapped ||
			!seyrix_workspace_window_is_visible(window) || direction == 0) {
		return;
	}
	struct seyrix_workspace *source = active_workspace(server);
	if (source == NULL) {
		return;
	}
	seyrix_popup_dismiss_window(window);
	if (has_active_popup_grab(server)) {
		wlr_log(WLR_INFO, "Window move deferred while an unrelated popup grab is active");
		return;
	}
	size_t destination_index = server->active_workspace_index;
	if (direction < 0) {
		if (destination_index == 0) {
			return;
		}
		destination_index--;
	} else {
		if (destination_index + 1 >= SEYRIX_WORKSPACE_COUNT) {
			return;
		}
		destination_index++;
	}
	struct seyrix_workspace *destination = &server->workspaces[destination_index];
	if (source == destination || destination->scene_tree == NULL ||
			window->scene_tree == NULL) {
		return;
	}
	if (server->grabbed_window == window) {
		seyrix_reset_cursor_mode(server);
	}
	seyrix_wm_detach_window(window);
	if (source->focused_window == window) {
		source->focused_window = NULL;
	}
	wl_list_remove(&window->workspace_link);
	wl_list_insert(&destination->windows, &window->workspace_link);
	window->workspace = destination;
	seyrix_wm_relayout_workspace(source);
	seyrix_wm_attach_window(window, destination);
	if (server->seat != NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
	}
	seyrix_focus_window(server, seyrix_workspace_focus_candidate(server, window));
	seyrix_shell_window_changed(window);
	wlr_log(WLR_INFO, "Moved focused window to workspace %zu", destination_index + 1);
}
