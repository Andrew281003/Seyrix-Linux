#ifndef SEYRIX_COMPOSITOR_H
#define SEYRIX_COMPOSITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <xkbcommon/xkbcommon.h>

enum seyrix_cursor_mode {
	SEYRIX_CURSOR_PASSTHROUGH,
	SEYRIX_CURSOR_MOVE,
	SEYRIX_CURSOR_RESIZE,
};

struct seyrix_window;
struct seyrix_window_state;
struct seyrix_decoration;
struct seyrix_shell;

#define SEYRIX_WORKSPACE_COUNT 4U

struct seyrix_workspace {
	struct wl_list windows;
	struct wlr_scene_tree *scene_tree;
	struct seyrix_window *focused_window;
};

struct seyrix_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_output_layout *output_layout;
	struct wlr_output_manager_v1 *output_manager;
	struct wlr_scene *scene;
	struct wlr_scene_tree *background_layer;
	struct wlr_scene_tree *layer_background;
	struct wlr_scene_tree *layer_bottom;
	struct wlr_scene_tree *window_layer;
	struct wlr_scene_tree *layer_top;
	struct wlr_scene_tree *layer_overlay;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_xdg_decoration_manager_v1 *decoration_manager;
	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_manager;
	struct wlr_seat *seat;
	struct wlr_layer_shell_v1 *layer_shell;
	struct seyrix_shell *shell;

	struct wl_list outputs;
	struct wl_list windows;
	struct wl_list keyboards;
	struct wl_list children;
	struct wl_list layer_surfaces;
	struct wl_list popups;
	struct seyrix_workspace workspaces[SEYRIX_WORKSPACE_COUNT];

	struct wl_listener new_output;
	struct wl_listener output_layout_change;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;
	struct wl_listener new_input;
	struct wl_listener new_toplevel;
	struct wl_listener new_popup;
	struct wl_listener new_decoration;
	struct wl_listener new_layer_surface;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;

	struct wl_event_source *sigint_source;
	struct wl_event_source *sigterm_source;
	struct wl_event_source *sigchld_source;

	enum seyrix_cursor_mode cursor_mode;
	struct seyrix_window *focused_window;
	struct seyrix_window *grabbed_window;
	size_t active_workspace_index;
	double grab_x;
	double grab_y;
	struct wlr_box grab_box;
	uint32_t resize_edges;
	uint32_t consumed_pointer_button;
	uint64_t started_at_ms;
	bool backend_started;
	bool output_manager_applying;
};

struct seyrix_output {
	struct wl_list link;
	struct seyrix_server *server;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *background;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener commit;
	struct wl_listener destroy;
	int width;
	int height;
	struct wlr_box usable_area;
};

struct seyrix_window {
	struct wl_list link;
	struct wl_list workspace_link;
	struct seyrix_server *server;
	struct seyrix_workspace *workspace;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *surface_tree;
	struct seyrix_window_state *window_state;
	struct seyrix_decoration *decoration;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	int last_width;
	int last_height;
	bool mapped;
	bool minimized;
};

struct seyrix_keyboard {
	struct wl_list link;
	struct seyrix_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
	uint32_t consumed_keycodes[8];
	size_t consumed_keycode_count;
};

struct seyrix_child {
	struct wl_list link;
	pid_t pid;
};

bool seyrix_server_init(struct seyrix_server *server);
void seyrix_server_run(struct seyrix_server *server);
void seyrix_server_finish(struct seyrix_server *server);
bool seyrix_spawn_terminal(struct seyrix_server *server);

bool seyrix_output_init(struct seyrix_server *server);
void seyrix_output_finish(struct seyrix_server *server);

bool seyrix_input_init(struct seyrix_server *server);
void seyrix_input_finish(struct seyrix_server *server);

/* Call after creating window_layer; finish before destroying the scene. */
bool seyrix_workspaces_init(struct seyrix_server *server);
void seyrix_workspaces_finish(struct seyrix_server *server);
/* Attach before creating the window scene node; forget on failure/destruction. */
struct wlr_scene_tree *seyrix_workspace_attach_window(
	struct seyrix_server *server, struct seyrix_window *window);
void seyrix_workspace_forget_window(struct seyrix_window *window);
void seyrix_workspace_window_mapped(struct seyrix_window *window);
void seyrix_workspace_window_unmapped(struct seyrix_window *window);
void seyrix_workspace_record_focus(struct seyrix_server *server,
	struct seyrix_window *window);
struct seyrix_window *seyrix_workspace_focus_candidate(
	struct seyrix_server *server, struct seyrix_window *exclude);
bool seyrix_workspace_window_is_visible(const struct seyrix_window *window);
void seyrix_workspace_switch(struct seyrix_server *server, size_t index);
void seyrix_workspace_switch_relative(struct seyrix_server *server,
	int direction);
void seyrix_workspace_move_focused(struct seyrix_server *server,
	int direction);

bool seyrix_window_init(struct seyrix_server *server);
void seyrix_window_finish(struct seyrix_server *server);
void seyrix_focus_window(struct seyrix_server *server,
	struct seyrix_window *window);
void seyrix_focus_next_window(struct seyrix_server *server,
	struct seyrix_window *exclude);
void seyrix_begin_interactive(struct seyrix_window *window,
	enum seyrix_cursor_mode mode, uint32_t edges);
void seyrix_reset_cursor_mode(struct seyrix_server *server);

bool seyrix_popup_init(struct seyrix_server *server);
void seyrix_popup_finish(struct seyrix_server *server);

bool seyrix_window_state_attach(struct seyrix_window *window);
void seyrix_window_state_destroy(struct seyrix_window *window);
void seyrix_window_state_initial_commit(struct seyrix_window *window);
void seyrix_window_state_commit(struct seyrix_window *window);
void seyrix_window_state_reconfigure(struct seyrix_window *window);
void seyrix_window_state_capture_normal(struct seyrix_window *window);
void seyrix_window_state_set_maximized(struct seyrix_window *window, bool value);
void seyrix_window_state_toggle_maximized(struct seyrix_window *window);
void seyrix_window_state_set_fullscreen(struct seyrix_window *window, bool value);
void seyrix_window_state_minimize(struct seyrix_window *window);
bool seyrix_window_state_restore_minimized(struct seyrix_server *server);
void seyrix_window_state_outputs_changed(struct seyrix_server *server);
void seyrix_window_state_output_removed(struct seyrix_server *server,
	struct wlr_output *output);
struct wlr_output *seyrix_window_state_output(struct seyrix_window *window);

int seyrix_decoration_titlebar_height(const struct seyrix_window *window);
void seyrix_decoration_set_fullscreen(struct seyrix_window *window, bool value);
void seyrix_decoration_window_destroy(struct seyrix_window *window);
void seyrix_decoration_finish(struct seyrix_server *server);
bool seyrix_decoration_pointer_button(struct seyrix_server *server,
	double x, double y, uint32_t button, uint32_t state);

bool seyrix_shell_init(struct seyrix_server *server);
bool seyrix_shell_start(struct seyrix_server *server);
void seyrix_shell_child_exited(struct seyrix_server *server, pid_t pid);
void seyrix_shell_finish(struct seyrix_server *server);
void seyrix_shell_outputs_changed(struct seyrix_server *server);
void seyrix_shell_update_active_workspace(struct seyrix_server *server);

bool seyrix_layer_shell_init(struct seyrix_server *server);
void seyrix_layer_shell_finish(struct seyrix_server *server);
void seyrix_layer_shell_outputs_changed(struct seyrix_server *server);
void seyrix_layer_shell_output_removed(struct seyrix_server *server,
	struct wlr_output *output);
bool seyrix_layer_shell_surface_at(struct seyrix_server *server,
	double x, double y, struct wlr_surface **surface,
	double *surface_x, double *surface_y);
void seyrix_layer_shell_focus_keyboard(struct seyrix_server *server,
	struct wlr_surface *surface);

#endif
