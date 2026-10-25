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
#include <wlr/types/wlr_output.h>
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

struct seyrix_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_output_layout *output_layout;
	struct wlr_scene *scene;
	struct wlr_scene_tree *background_layer;
	struct wlr_scene_tree *window_layer;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_manager;
	struct wlr_seat *seat;

	struct wl_list outputs;
	struct wl_list windows;
	struct wl_list keyboards;
	struct wl_list children;

	struct wl_listener new_output;
	struct wl_listener output_layout_change;
	struct wl_listener new_input;
	struct wl_listener new_toplevel;
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
	double grab_x;
	double grab_y;
	struct wlr_box grab_box;
	uint32_t resize_edges;
	size_t next_window_offset;
	uint64_t started_at_ms;
	bool backend_started;
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
};

struct seyrix_window {
	struct wl_list link;
	struct seyrix_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	int last_width;
	int last_height;
	bool mapped;
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

bool seyrix_window_init(struct seyrix_server *server);
void seyrix_window_finish(struct seyrix_server *server);
void seyrix_focus_window(struct seyrix_server *server,
	struct seyrix_window *window);
void seyrix_focus_next_window(struct seyrix_server *server,
	struct seyrix_window *exclude);
void seyrix_begin_interactive(struct seyrix_window *window,
	enum seyrix_cursor_mode mode, uint32_t edges);
void seyrix_reset_cursor_mode(struct seyrix_server *server);

#endif
