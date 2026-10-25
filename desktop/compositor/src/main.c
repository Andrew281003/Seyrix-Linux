#define _POSIX_C_SOURCE 200809L

#include "seyrix.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/util/log.h>

static uint64_t monotonic_ms(void) {
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static bool runtime_directory_is_safe(void) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir == NULL || runtime_dir[0] == '\0') {
		wlr_log(WLR_ERROR, "XDG_RUNTIME_DIR is not set; start from a logind session");
		return false;
	}

	struct stat status;
	if (stat(runtime_dir, &status) != 0 || !S_ISDIR(status.st_mode) ||
			status.st_uid != getuid() || (status.st_mode & 0077) != 0) {
		wlr_log(WLR_ERROR, "XDG_RUNTIME_DIR must be a private directory owned by this user");
		return false;
	}
	return true;
}

static int handle_signal(int signal_number, void *data) {
	(void)signal_number;
	struct seyrix_server *server = data;
	if (server->display != NULL) {
		wl_display_terminate(server->display);
	}
	return 0;
}

static void reap_children(struct seyrix_server *server) {
	int status;
	pid_t pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		struct seyrix_child *child, *temporary;
		wl_list_for_each_safe(child, temporary, &server->children, link) {
			if (child->pid == pid) {
				wl_list_remove(&child->link);
				free(child);
				break;
			}
		}
	}
}

static int handle_child_signal(int signal_number, void *data) {
	(void)signal_number;
	reap_children(data);
	return 0;
}

static void wait_for_children(struct seyrix_server *server, uint64_t timeout_ms) {
	uint64_t deadline = monotonic_ms() + timeout_ms;
	struct timespec pause = {.tv_sec = 0, .tv_nsec = 10000000};
	while (!wl_list_empty(&server->children) && monotonic_ms() < deadline) {
		reap_children(server);
		nanosleep(&pause, NULL);
	}
}

static void stop_children(struct seyrix_server *server) {
	wait_for_children(server, 500);

	struct seyrix_child *child;
	wl_list_for_each(child, &server->children, link) {
		kill(child->pid, SIGTERM);
	}
	wait_for_children(server, 500);

	struct seyrix_child *temporary;
	wl_list_for_each_safe(child, temporary, &server->children, link) {
		kill(child->pid, SIGKILL);
		while (waitpid(child->pid, NULL, 0) < 0 && errno == EINTR) {
		}
		wl_list_remove(&child->link);
		free(child);
	}
}

bool seyrix_spawn_terminal(struct seyrix_server *server) {
	struct seyrix_child *child = calloc(1, sizeof(*child));
	if (child == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate terminal process record");
		return false;
	}

	pid_t pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "Cannot fork weston-terminal");
		free(child);
		return false;
	}
	if (pid == 0) {
		execl("/usr/bin/weston-terminal", "weston-terminal", (char *)NULL);
		perror("seyrix-compositor: exec weston-terminal");
		_exit(127);
	}

	child->pid = pid;
	wl_list_insert(&server->children, &child->link);
	wlr_log(WLR_INFO, "Started weston-terminal with pid %ld", (long)pid);
	return true;
}

bool seyrix_server_init(struct seyrix_server *server) {
	server->started_at_ms = monotonic_ms();
	wl_list_init(&server->outputs);
	wl_list_init(&server->windows);
	wl_list_init(&server->keyboards);
	wl_list_init(&server->children);

	if (!runtime_directory_is_safe()) {
		return false;
	}

	server->display = wl_display_create();
	if (server->display == NULL) {
		wlr_log(WLR_ERROR, "Cannot create Wayland display");
		return false;
	}

	struct wl_event_loop *event_loop = wl_display_get_event_loop(server->display);
	server->backend = wlr_backend_autocreate(event_loop, NULL);
	if (server->backend == NULL) {
		wlr_log(WLR_ERROR, "Cannot initialize Wayland backend");
		return false;
	}

	server->renderer = wlr_renderer_autocreate(server->backend);
	if (server->renderer == NULL) {
		wlr_log(WLR_ERROR, "Cannot initialize renderer");
		return false;
	}
	server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
	if (server->allocator == NULL) {
		wlr_log(WLR_ERROR, "Cannot initialize output allocator");
		return false;
	}
	if (!wlr_renderer_init_wl_display(server->renderer, server->display)) {
		wlr_log(WLR_ERROR, "Cannot initialize Wayland renderer globals");
		return false;
	}

	server->output_layout = wlr_output_layout_create(server->display);
	server->scene = wlr_scene_create();
	if (server->output_layout == NULL || server->scene == NULL) {
		wlr_log(WLR_ERROR, "Cannot create output layout or scene");
		return false;
	}
	server->background_layer = wlr_scene_tree_create(&server->scene->tree);
	server->window_layer = wlr_scene_tree_create(&server->scene->tree);
	if (server->background_layer == NULL || server->window_layer == NULL) {
		wlr_log(WLR_ERROR, "Cannot create scene layers");
		return false;
	}
	server->scene_layout = wlr_scene_attach_output_layout(server->scene,
		server->output_layout);
	if (server->scene_layout == NULL) {
		wlr_log(WLR_ERROR, "Cannot attach scene to output layout");
		return false;
	}

	if (wlr_compositor_create(server->display, 5, server->renderer) == NULL ||
			wlr_subcompositor_create(server->display) == NULL ||
			wlr_data_device_manager_create(server->display) == NULL) {
		wlr_log(WLR_ERROR, "Cannot create core Wayland globals");
		return false;
	}

	if (!seyrix_output_init(server) || !seyrix_window_init(server) ||
			!seyrix_input_init(server)) {
		return false;
	}

	server->sigint_source = wl_event_loop_add_signal(event_loop, SIGINT,
	handle_signal, server);
	server->sigterm_source = wl_event_loop_add_signal(event_loop, SIGTERM,
		handle_signal, server);
	server->sigchld_source = wl_event_loop_add_signal(event_loop, SIGCHLD,
		handle_child_signal, server);
	if (server->sigint_source == NULL || server->sigterm_source == NULL ||
			server->sigchld_source == NULL) {
		wlr_log(WLR_ERROR, "Cannot install compositor signal handlers");
		return false;
	}

	const char *socket = wl_display_add_socket_auto(server->display);
	if (socket == NULL) {
		wlr_log(WLR_ERROR, "Cannot create Wayland socket");
		return false;
	}
	if (setenv("WAYLAND_DISPLAY", socket, 1) != 0 ||
			setenv("XDG_SESSION_TYPE", "wayland", 1) != 0 ||
			setenv("XDG_CURRENT_DESKTOP", "Seyrix", 1) != 0) {
		wlr_log(WLR_ERROR, "Cannot export Wayland session environment");
		return false;
	}

	if (!wlr_backend_start(server->backend)) {
		wlr_log(WLR_ERROR, "Cannot start Wayland backend");
		return false;
	}
	server->backend_started = true;
	if (wl_list_empty(&server->outputs)) {
		wlr_log(WLR_ERROR, "No active outputs found; compositor needs a display");
		return false;
	}

	const char *renderer_request = getenv("WLR_RENDERER");
	wlr_log(WLR_INFO, "Renderer request: %s",
		renderer_request != NULL ? renderer_request : "wlroots automatic selection");
	wlr_log(WLR_INFO, "Running Seyrix compositor on WAYLAND_DISPLAY=%s", socket);
	wlr_log(WLR_INFO, "Shortcuts: Super+Return opens terminal; Super+Q closes focused window; Super+Shift+Q exits");
	if (!seyrix_spawn_terminal(server)) {
		wlr_log(WLR_ERROR, "Startup terminal failed; use Super+Return to retry");
	}
	wlr_log(WLR_INFO, "SEYRIX_COMPOSITOR_READY=1 startup_ms=%llu",
		(unsigned long long)(monotonic_ms() - server->started_at_ms));
	return true;
}

void seyrix_server_run(struct seyrix_server *server) {
	wl_display_run(server->display);
}

void seyrix_server_finish(struct seyrix_server *server) {
	if (server->display != NULL) {
		wl_display_destroy_clients(server->display);
	}
	seyrix_window_finish(server);
	seyrix_output_finish(server);
	seyrix_input_finish(server);

	if (server->sigint_source != NULL) {
		wl_event_source_remove(server->sigint_source);
		server->sigint_source = NULL;
	}
	if (server->sigterm_source != NULL) {
		wl_event_source_remove(server->sigterm_source);
		server->sigterm_source = NULL;
	}
	if (server->sigchld_source != NULL) {
		wl_event_source_remove(server->sigchld_source);
		server->sigchld_source = NULL;
	}

	if (server->cursor_manager != NULL) {
		wlr_xcursor_manager_destroy(server->cursor_manager);
		server->cursor_manager = NULL;
	}
	if (server->cursor != NULL) {
		wlr_cursor_destroy(server->cursor);
		server->cursor = NULL;
	}
	if (server->backend != NULL) {
		wlr_backend_destroy(server->backend);
		server->backend = NULL;
	}

	if (server->display != NULL) {
		wl_display_destroy_clients(server->display);
	}
	stop_children(server);

	if (server->scene != NULL) {
		wlr_scene_node_destroy(&server->scene->tree.node);
		server->scene = NULL;
	}
	if (server->output_layout != NULL) {
		wlr_output_layout_destroy(server->output_layout);
		server->output_layout = NULL;
	}
	if (server->allocator != NULL) {
		wlr_allocator_destroy(server->allocator);
		server->allocator = NULL;
	}
	if (server->renderer != NULL) {
		wlr_renderer_destroy(server->renderer);
		server->renderer = NULL;
	}
	if (server->display != NULL) {
		wl_display_destroy(server->display);
		server->display = NULL;
	}
	wlr_log(WLR_INFO, "Seyrix compositor stopped cleanly");
}

int main(void) {
	wlr_log_init(WLR_INFO, NULL);
	struct seyrix_server server = {0};
	if (!seyrix_server_init(&server)) {
		seyrix_server_finish(&server);
		return 1;
	}
	seyrix_server_run(&server);
	seyrix_server_finish(&server);
	return 0;
}
