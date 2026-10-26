#include "seyrix.h"

#include <limits.h>
#include <stdlib.h>

#include <wlr/util/log.h>

#include "decoration.h"
#include "layer-shell.h"
#include "window-state.h"

static struct seyrix_workspace *active_workspace(
		struct seyrix_server *server) {
	if (server == NULL || server->active_workspace_index >=
			SEYRIX_WORKSPACE_COUNT) {
		return NULL;
	}
	return &server->workspaces[server->active_workspace_index];
}

static bool output_is_live(struct seyrix_server *server,
		struct wlr_output *candidate) {
	if (candidate == NULL) {
		return false;
	}
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output == candidate && candidate->enabled) {
			return true;
		}
	}
	return false;
}

static size_t enabled_output_count(struct seyrix_server *server) {
	size_t count = 0;
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output->enabled) {
			count++;
		}
	}
	return count;
}

static struct wlr_output *enabled_output_at(struct seyrix_server *server,
		size_t index) {
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (!output->wlr_output->enabled) {
			continue;
		}
		if (index == 0) {
			return output->wlr_output;
		}
		index--;
	}
	return NULL;
}

static bool window_should_tile(const struct seyrix_window *window) {
	if (window == NULL || !window->mapped || window->workspace == NULL ||
			window->minimized || window->layout_suspended) {
		return false;
	}
	switch (window->workspace->preferences.mode) {
	case SEYRIX_LAYOUT_FLOATING:
		return false;
	case SEYRIX_LAYOUT_TILING:
		return true;
	case SEYRIX_LAYOUT_HYBRID:
		return window->classification == SEYRIX_WINDOW_TILED;
	}
	return false;
}

bool seyrix_wm_window_is_tiled(const struct seyrix_window *window) {
	return window != NULL && window->layout_active && window->workspace != NULL &&
		seyrix_layout_contains(window->workspace->layout_root, window);
}

static struct seyrix_layout_limits window_limits(
		const struct seyrix_window *window) {
	struct seyrix_layout_limits limits = {0};
	if (window == NULL || window->xdg_toplevel == NULL) {
		return limits;
	}
	int32_t titlebar = seyrix_decoration_titlebar_height(window);
	limits.min_width = window->xdg_toplevel->current.min_width;
	limits.min_height = window->xdg_toplevel->current.min_height;
	if (limits.min_height > INT32_MAX - titlebar) {
		limits.min_height = INT32_MAX;
	} else {
		limits.min_height += titlebar;
	}
	limits.max_width = window->xdg_toplevel->current.max_width;
	limits.max_height = window->xdg_toplevel->current.max_height;
	if (limits.max_height > 0) {
		if (limits.max_height > INT32_MAX - titlebar) {
			limits.max_height = INT32_MAX;
		} else {
			limits.max_height += titlebar;
		}
	}
	return limits;
}

static bool current_normal_box(struct seyrix_window *window,
		struct wlr_box *box) {
	if (window == NULL || box == NULL || window->scene_tree == NULL ||
			window->xdg_toplevel == NULL || window->xdg_toplevel->base == NULL) {
		return false;
	}
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

static void capture_current_floating_box(struct seyrix_window *window) {
	if (window == NULL || window->minimized || window->layout_suspended) {
		return;
	}
	struct wlr_box box;
	if (current_normal_box(window, &box)) {
		window->floating_box = box;
		window->floating_box_valid = true;
	}
}

void seyrix_wm_capture_floating(struct seyrix_window *window) {
	if (window == NULL || seyrix_wm_window_is_tiled(window)) {
		return;
	}
	capture_current_floating_box(window);
}

static void restore_floating_box(struct seyrix_window *window) {
	if (window == NULL || !window->floating_box_valid ||
			window->scene_tree == NULL || window->xdg_toplevel == NULL ||
			window->layout_suspended || window->minimized) {
		return;
	}
	const struct wlr_box *box = &window->floating_box;
	wlr_scene_node_set_position(&window->scene_tree->node, box->x, box->y);
	wlr_xdg_toplevel_set_size(window->xdg_toplevel, box->width, box->height);
}

static void set_scene_layer(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL || window->scene_tree == NULL) {
		return;
	}
	struct wlr_scene_tree *target = window->layout_active ?
		window->workspace->tiled_layer : window->workspace->floating_layer;
	if (target != NULL && window->scene_tree->node.parent != target) {
		wlr_scene_node_reparent(&window->scene_tree->node, target);
	}
}

struct wlr_scene_tree *seyrix_wm_scene_parent(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL) {
		return NULL;
	}
	return window->layout_active ? window->workspace->tiled_layer :
		window->workspace->floating_layer;
}

static enum seyrix_layout_axis insertion_axis(
		struct seyrix_workspace *workspace, const void *target) {
	if (workspace->preferences.split_axis == SEYRIX_SPLIT_HORIZONTAL) {
		return SEYRIX_LAYOUT_HORIZONTAL;
	}
	if (workspace->preferences.split_axis == SEYRIX_SPLIT_VERTICAL) {
		return SEYRIX_LAYOUT_VERTICAL;
	}
	struct seyrix_layout_rect box = {0};
	if (target != NULL && seyrix_layout_get_rect(workspace->layout_root,
			target, &box)) {
		return box.width >= box.height ? SEYRIX_LAYOUT_HORIZONTAL :
			SEYRIX_LAYOUT_VERTICAL;
	}
	if (workspace->output != NULL) {
		struct wlr_box usable = {0};
		seyrix_layer_shell_output_work_area(
			workspace->server, workspace->output, &usable);
		if (usable.width > 0 && usable.height > 0) {
			return usable.width >= usable.height ? SEYRIX_LAYOUT_HORIZONTAL :
				SEYRIX_LAYOUT_VERTICAL;
		}
	}
	return SEYRIX_LAYOUT_HORIZONTAL;
}

static bool ensure_layout_leaf(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL || !window->mapped) {
		return false;
	}
	struct seyrix_workspace *workspace = window->workspace;
	if (seyrix_layout_contains(workspace->layout_root, window)) {
		return true;
	}
	void *target = NULL;
	if (workspace->focused_window != NULL &&
			seyrix_layout_contains(workspace->layout_root,
				workspace->focused_window)) {
		target = workspace->focused_window;
	} else {
		target = seyrix_layout_largest_leaf(workspace->layout_root, true);
	}
	enum seyrix_layout_axis axis = insertion_axis(workspace, target);
	struct seyrix_layout_limits limits = window_limits(window);
	if (seyrix_layout_insert(&workspace->layout_root, target, window, axis,
			workspace->preferences.split_ratio, &limits, NULL) == NULL) {
		wlr_log(WLR_ERROR,
			"Cannot add window to workspace %zu layout; keeping its current geometry",
			(size_t)(workspace - window->server->workspaces) + 1);
		return false;
	}
	return true;
}

static void apply_tile_geometry(void *owner,
		const struct seyrix_layout_rect *cell, void *data) {
	(void)data;
	struct seyrix_window *window = owner;
	if (window == NULL || cell == NULL || !window->layout_active ||
			window->xdg_toplevel == NULL || window->scene_tree == NULL) {
		return;
	}
	int32_t titlebar = seyrix_decoration_titlebar_height(window);
	struct seyrix_layout_limits limits = window_limits(window);
	struct seyrix_layout_rect client;
	if (!seyrix_layout_fit_client(*cell, &limits, titlebar, &client)) {
		return;
	}
	wlr_scene_node_set_position(&window->scene_tree->node, client.x, client.y);
	wlr_xdg_toplevel_set_size(window->xdg_toplevel,
		client.width, client.height);
}

static void persist_preferences(struct seyrix_server *server,
		struct seyrix_workspace *workspace) {
	if (server == NULL || workspace == NULL) {
		return;
	}
	size_t index = (size_t)(workspace - server->workspaces);
	if (index >= SEYRIX_WORKSPACE_COUNT) {
		return;
	}
	server->wm_config.workspaces[index] = workspace->preferences;
	char error[160];
	if (!seyrix_wm_config_save(&server->wm_config, error, sizeof(error))) {
		wlr_log(WLR_ERROR, "Cannot save window-management configuration: %s", error);
	}
}

void seyrix_wm_relayout_workspace(struct seyrix_workspace *workspace) {
	if (workspace == NULL || workspace->windows.next == NULL ||
			workspace->windows.prev == NULL) {
		return;
	}
	struct seyrix_window *window;
	wl_list_for_each(window, &workspace->windows, workspace_link) {
		(void)ensure_layout_leaf(window);
	}
	wl_list_for_each(window, &workspace->windows, workspace_link) {
		bool has_leaf = seyrix_layout_contains(workspace->layout_root, window);
		bool should_tile = has_leaf && window_should_tile(window);
		if (should_tile && !window->layout_active &&
				!window->floating_box_valid) {
			capture_current_floating_box(window);
		}
		if (!should_tile && window->layout_active &&
				!window->layout_suspended && !window->minimized) {
			restore_floating_box(window);
		}
		window->layout_active = should_tile;
		if (has_leaf) {
			(void)seyrix_layout_set_active(workspace->layout_root, window,
				should_tile);
			struct seyrix_layout_limits limits = window_limits(window);
			(void)seyrix_layout_set_limits(workspace->layout_root, window,
				&limits);
		}
		set_scene_layer(window);
	}

	bool have_tiled_window = false;
	wl_list_for_each(window, &workspace->windows, workspace_link) {
		if (window->layout_active) {
			have_tiled_window = true;
			break;
		}
	}
	if (!have_tiled_window || workspace->output == NULL ||
			!workspace->output->enabled) {
		return;
	}
	struct seyrix_server *server = workspace->server;
	struct wlr_box usable = {0};
	seyrix_layer_shell_output_work_area(server, workspace->output, &usable);
	struct seyrix_layout_rect area = {
		.x = usable.x,
		.y = usable.y,
		.width = usable.width,
		.height = usable.height,
	};
	if (!seyrix_layout_reflow(workspace->layout_root, area,
			workspace->preferences.outer_gap,
			workspace->preferences.inner_gap, apply_tile_geometry, NULL)) {
		if (!workspace->layout_constraint_blocked) {
			wlr_log(WLR_ERROR,
				"Workspace tiling retained last valid geometry because client minimum sizes exceed usable output area");
			workspace->layout_constraint_blocked = true;
		}
	} else if (workspace->layout_constraint_blocked) {
		wlr_log(WLR_INFO, "Workspace tiling resumed after usable area recovered");
		workspace->layout_constraint_blocked = false;
	}
}

static void apply_workspace_mode(struct seyrix_workspace *workspace,
		enum seyrix_layout_mode mode) {
	if (workspace == NULL || (mode != SEYRIX_LAYOUT_FLOATING &&
			mode != SEYRIX_LAYOUT_TILING && mode != SEYRIX_LAYOUT_HYBRID)) {
		return;
	}
	workspace->preferences.mode = mode;
	seyrix_wm_relayout_workspace(workspace);
}

bool seyrix_wm_init(struct seyrix_server *server) {
	seyrix_wm_config_defaults(&server->wm_config);
	char error[160];
	if (!seyrix_wm_config_load(&server->wm_config, error, sizeof(error))) {
		wlr_log(WLR_ERROR, "Window-management configuration ignored: %s", error);
	}
	wlr_log(WLR_INFO, "Window-management default mode: %s",
		server->wm_config.defaults.mode == SEYRIX_LAYOUT_FLOATING ? "floating" :
		server->wm_config.defaults.mode == SEYRIX_LAYOUT_TILING ? "tiling" :
		"hybrid");
	return true;
}

void seyrix_wm_finish(struct seyrix_server *server) {
	if (server != NULL) {
		seyrix_wm_config_finish(&server->wm_config);
	}
}

void seyrix_wm_initialize_window(struct seyrix_window *window) {
	if (window == NULL || window->classification_initialized ||
			window->xdg_toplevel == NULL) {
		return;
	}
	enum seyrix_window_classification classification;
	if (seyrix_wm_config_get_app_class(&window->server->wm_config,
			window->xdg_toplevel->app_id, &classification)) {
		window->classification = classification;
		window->classification_override = true;
		window->classification_from_rule = true;
	} else if (window->xdg_toplevel->parent != NULL) {
		window->classification = SEYRIX_WINDOW_FLOATING;
	} else {
		window->classification = SEYRIX_WINDOW_TILED;
	}
	window->classification_initialized = true;
}

void seyrix_wm_window_mapped(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL) {
		return;
	}
	seyrix_wm_initialize_window(window);
	seyrix_wm_relayout_workspace(window->workspace);
}

void seyrix_wm_window_unmapped(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL) {
		return;
	}
	window->layout_active = false;
	if (seyrix_layout_contains(window->workspace->layout_root, window)) {
		(void)seyrix_layout_set_active(window->workspace->layout_root, window, false);
	}
	seyrix_wm_relayout_workspace(window->workspace);
}

void seyrix_wm_window_destroyed(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL) {
		return;
	}
	struct seyrix_workspace *workspace = window->workspace;
	(void)seyrix_layout_remove(&workspace->layout_root, window, NULL);
	window->layout_active = false;
	seyrix_wm_relayout_workspace(workspace);
}

void seyrix_wm_window_committed(struct seyrix_window *window) {
	if (window == NULL || !window->mapped || window->workspace == NULL) {
		return;
	}
	if (!seyrix_wm_window_is_tiled(window) && !window->layout_suspended &&
			!window->minimized) {
		capture_current_floating_box(window);
	}
	int min_width = window->xdg_toplevel->current.min_width;
	int min_height = window->xdg_toplevel->current.min_height;
	int max_width = window->xdg_toplevel->current.max_width;
	int max_height = window->xdg_toplevel->current.max_height;
	if (min_width != window->last_min_width ||
			min_height != window->last_min_height ||
			max_width != window->last_max_width ||
			max_height != window->last_max_height) {
		window->last_min_width = min_width;
		window->last_min_height = min_height;
		window->last_max_width = max_width;
		window->last_max_height = max_height;
		seyrix_wm_relayout_workspace(window->workspace);
	}
}

void seyrix_wm_detach_window(struct seyrix_window *window) {
	if (window == NULL || window->workspace == NULL) {
		return;
	}
	struct seyrix_workspace *workspace = window->workspace;
	(void)seyrix_layout_remove(&workspace->layout_root, window, NULL);
	window->layout_active = false;
}

void seyrix_wm_attach_window(struct seyrix_window *window,
		struct seyrix_workspace *workspace) {
	if (window == NULL || workspace == NULL) {
		return;
	}
	window->workspace = workspace;
	if (window->scene_tree != NULL) {
		struct wlr_scene_tree *parent = seyrix_wm_scene_parent(window);
		if (parent != NULL) {
			wlr_scene_node_reparent(&window->scene_tree->node, parent);
		}
	}
	seyrix_wm_relayout_workspace(workspace);
}

void seyrix_wm_set_layout_suspended(struct seyrix_window *window,
		bool suspended) {
	if (window == NULL || window->layout_suspended == suspended) {
		return;
	}
	window->layout_suspended = suspended;
	if (window->workspace != NULL) {
		seyrix_wm_relayout_workspace(window->workspace);
	}
}

void seyrix_wm_outputs_changed(struct seyrix_server *server) {
	if (server == NULL) {
		return;
	}
	size_t output_count = enabled_output_count(server);
	bool redistribute = output_count != server->wm_output_count;
	server->wm_output_count = output_count;
	for (size_t i = 0; i < SEYRIX_WORKSPACE_COUNT; i++) {
		struct seyrix_workspace *workspace = &server->workspaces[i];
		if (redistribute || !output_is_live(server, workspace->output)) {
			struct wlr_output *target = output_count > 0 ?
				enabled_output_at(server, i % output_count) : NULL;
			if (workspace->output != target) {
				workspace->output = target;
				wlr_log(WLR_INFO, "Workspace %zu assigned output %s",
					i + 1, target != NULL ? target->name : "none");
			}
		}
		seyrix_wm_relayout_workspace(workspace);
	}
}

void seyrix_wm_set_mode(struct seyrix_server *server,
		enum seyrix_layout_mode mode) {
	struct seyrix_workspace *workspace = active_workspace(server);
	if (workspace == NULL || workspace->preferences.mode == mode) {
		return;
	}
	if (server->grabbed_window != NULL &&
			server->grabbed_window->workspace == workspace) {
		seyrix_reset_cursor_mode(server);
	}
	apply_workspace_mode(workspace, mode);
	persist_preferences(server, workspace);
	wlr_log(WLR_INFO, "Workspace %zu layout mode changed to %s",
		server->active_workspace_index + 1,
		mode == SEYRIX_LAYOUT_FLOATING ? "floating" :
		mode == SEYRIX_LAYOUT_TILING ? "tiling" : "hybrid");
}

void seyrix_wm_toggle_floating(struct seyrix_server *server) {
	struct seyrix_window *window = server != NULL ? server->focused_window : NULL;
	if (window == NULL || !window->mapped || window->workspace == NULL ||
			!seyrix_workspace_window_is_visible(window)) {
		return;
	}
	struct seyrix_workspace *workspace = window->workspace;
	bool was_tiled = seyrix_wm_window_is_tiled(window);
	enum seyrix_window_classification classification = was_tiled ?
		SEYRIX_WINDOW_FLOATING : SEYRIX_WINDOW_TILED;
	if (workspace->preferences.mode != SEYRIX_LAYOUT_HYBRID) {
		if (server->grabbed_window == window) {
			seyrix_reset_cursor_mode(server);
		}
		apply_workspace_mode(workspace, SEYRIX_LAYOUT_HYBRID);
	}
	window->classification = classification;
	window->classification_override = true;
	window->classification_from_rule = false;
	const char *app_id = window->xdg_toplevel->app_id;
	if (app_id != NULL && app_id[0] != '\0' &&
			seyrix_wm_config_set_app_class(&server->wm_config, app_id,
				classification)) {
		window->classification_from_rule = true;
		char error[160];
		if (!seyrix_wm_config_save(&server->wm_config, error, sizeof(error))) {
			wlr_log(WLR_ERROR, "Cannot save window classification: %s", error);
		}
	}
	seyrix_wm_relayout_workspace(workspace);
	wlr_log(WLR_INFO, "Focused window classification changed to %s",
		classification == SEYRIX_WINDOW_FLOATING ? "floating" : "tiled");
}

void seyrix_wm_focus_tiled_neighbor(struct seyrix_server *server,
		int direction) {
	struct seyrix_window *window = server != NULL ? server->focused_window : NULL;
	if (window == NULL || !seyrix_wm_window_is_tiled(window)) {
		return;
	}
	struct seyrix_window *neighbor = seyrix_layout_find_neighbor(
		window->workspace->layout_root, window, direction);
	if (neighbor != NULL) {
		seyrix_focus_window(server, neighbor);
	}
}

void seyrix_wm_move_tiled_neighbor(struct seyrix_server *server,
		int direction) {
	struct seyrix_window *window = server != NULL ? server->focused_window : NULL;
	if (window == NULL || !seyrix_wm_window_is_tiled(window) ||
			!seyrix_layout_swap_adjacent(window->workspace->layout_root,
				window, direction)) {
		return;
	}
	seyrix_wm_relayout_workspace(window->workspace);
	wlr_log(WLR_INFO, "Moved tiled window within workspace layout");
}

void seyrix_wm_adjust_split_ratio(struct seyrix_server *server,
		enum seyrix_layout_axis axis, int delta) {
	struct seyrix_window *window = server != NULL ? server->focused_window : NULL;
	if (window == NULL || !seyrix_wm_window_is_tiled(window) ||
			!seyrix_layout_adjust_ratio(window->workspace->layout_root,
				window, axis, delta)) {
		return;
	}
	struct seyrix_workspace *workspace = window->workspace;
	uint16_t ratio;
	if (seyrix_layout_get_ratio(workspace->layout_root, window, axis, &ratio)) {
		workspace->preferences.split_ratio = ratio;
		seyrix_wm_relayout_workspace(workspace);
		persist_preferences(server, workspace);
		wlr_log(WLR_INFO, "Workspace split ratio adjusted to %u/%u",
			ratio, SEYRIX_LAYOUT_RATIO_SCALE);
	}
}

void seyrix_wm_toggle_split_axis(struct seyrix_server *server) {
	struct seyrix_workspace *workspace = active_workspace(server);
	if (workspace == NULL) {
		return;
	}
	switch (workspace->preferences.split_axis) {
	case SEYRIX_SPLIT_AUTO:
		workspace->preferences.split_axis = SEYRIX_SPLIT_HORIZONTAL;
		break;
	case SEYRIX_SPLIT_HORIZONTAL:
		workspace->preferences.split_axis = SEYRIX_SPLIT_VERTICAL;
		break;
	case SEYRIX_SPLIT_VERTICAL:
		workspace->preferences.split_axis = SEYRIX_SPLIT_AUTO;
		break;
	}
	struct seyrix_window *focused = server->focused_window;
	if (focused != NULL && focused->workspace == workspace &&
			seyrix_wm_window_is_tiled(focused)) {
		enum seyrix_layout_axis axis =
			workspace->preferences.split_axis == SEYRIX_SPLIT_HORIZONTAL ?
				SEYRIX_LAYOUT_HORIZONTAL :
			workspace->preferences.split_axis == SEYRIX_SPLIT_VERTICAL ?
				SEYRIX_LAYOUT_VERTICAL : insertion_axis(workspace, focused);
		(void)seyrix_layout_set_parent_axis(workspace->layout_root, focused, axis);
		seyrix_wm_relayout_workspace(workspace);
	}
	persist_preferences(server, workspace);
	wlr_log(WLR_INFO,
		"Workspace split orientation preference changed; focused parent split updated when available");
}

bool seyrix_wm_reload_config(struct seyrix_server *server) {
	if (server == NULL) {
		return false;
	}
	struct seyrix_wm_config loaded = {0};
	seyrix_wm_config_defaults(&loaded);
	char error[160];
	if (!seyrix_wm_config_load(&loaded, error, sizeof(error))) {
		seyrix_wm_config_finish(&loaded);
		wlr_log(WLR_ERROR, "Window-management configuration reload failed: %s", error);
		return false;
	}
	seyrix_wm_config_finish(&server->wm_config);
	server->wm_config = loaded;
	for (size_t i = 0; i < SEYRIX_WORKSPACE_COUNT; i++) {
		struct seyrix_workspace *workspace = &server->workspaces[i];
		workspace->preferences = server->wm_config.workspaces[i];
		(void)seyrix_layout_set_all_ratios(workspace->layout_root,
			workspace->preferences.split_ratio);
		struct seyrix_window *window;
		wl_list_for_each(window, &workspace->windows, workspace_link) {
			enum seyrix_window_classification classification;
			if (seyrix_wm_config_get_app_class(&server->wm_config,
					window->xdg_toplevel->app_id, &classification)) {
				window->classification = classification;
				window->classification_override = true;
				window->classification_from_rule = true;
			} else if (window->classification_from_rule) {
				window->classification = window->xdg_toplevel->parent != NULL ?
					SEYRIX_WINDOW_FLOATING : SEYRIX_WINDOW_TILED;
				window->classification_override = false;
				window->classification_from_rule = false;
			}
		}
		seyrix_wm_relayout_workspace(workspace);
	}
	wlr_log(WLR_INFO, "Window-management configuration reloaded");
	return true;
}
