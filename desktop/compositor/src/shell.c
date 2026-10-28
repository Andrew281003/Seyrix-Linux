#define _POSIX_C_SOURCE 200809L

#include "shell.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "../../common/shell-ipc.h"
#include "window-state.h"

static void close_status_channel(struct seyrix_server *server);
static void hide_overlay(struct seyrix_server *server, bool restore);
static void disconnect_shell_client(struct seyrix_server *server);
static void schedule_restart(struct seyrix_server *server);

static uint64_t monotonic_ms(void) {
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

enum overlay_kind {
	OVERLAY_NONE,
	OVERLAY_LAUNCHER,
	OVERLAY_SWITCHER,
	OVERLAY_MANUAL,
};

struct seyrix_shell {
	pid_t process;
	int status_socket;
	struct wl_event_source *status_source;
	struct wl_event_source *restart_timer;
	struct seyrix_window *restore_focus;
	enum overlay_kind overlay;
	uint64_t process_started_ms;
	unsigned int restart_attempts;
	bool restart_scheduled;
	bool shutting_down;
};

static struct seyrix_shell_packet_header packet_header(uint8_t type,
		size_t size) {
	return (struct seyrix_shell_packet_header){
		.version = SEYRIX_SHELL_IPC_VERSION,
		.type = type,
		.size = (uint16_t)size,
	};
}

static bool send_packet(struct seyrix_server *server, const void *packet,
		size_t size) {
	if (server->shell == NULL || server->shell->status_socket < 0 ||
			size > UINT16_MAX) {
		return false;
	}
	int send_error = 0;
	enum seyrix_shell_ipc_send_result result = seyrix_shell_ipc_send_packet(
		server->shell->status_socket, packet, size, &send_error);
	if (result == SEYRIX_SHELL_IPC_SENT) {
		return true;
	}
	if (result == SEYRIX_SHELL_IPC_WOULD_BLOCK) {
		wlr_log(WLR_ERROR, "Shell IPC send queue is full; restarting shell client");
	} else {
		wlr_log(WLR_ERROR, "Shell IPC send failed: %s", strerror(send_error));
	}
	disconnect_shell_client(server);
	errno = send_error;
	return false;
}

static void close_status_channel(struct seyrix_server *server) {
	if (server->shell->status_source != NULL) {
		wl_event_source_remove(server->shell->status_source);
		server->shell->status_source = NULL;
	}
	if (server->shell->status_socket >= 0) {
		close(server->shell->status_socket);
		server->shell->status_socket = -1;
	}
}

static void disconnect_shell_client(struct seyrix_server *server) {
	if (server->shell == NULL) return;
	close_status_channel(server);
	hide_overlay(server, true);
	if (server->shell->process > 0) {
		if (kill(server->shell->process, SIGTERM) != 0 && errno != ESRCH) {
			wlr_log(WLR_ERROR, "Cannot stop failed Seyrix shell client: %s",
				strerror(errno));
		}
		return;
	}
	schedule_restart(server);
}

static struct seyrix_window *window_by_shell_id(struct seyrix_server *server,
		uint32_t id) {
	struct seyrix_window *window;
	wl_list_for_each(window, &server->windows, link) {
		if (window->shell_id == id && window->mapped) {
			return window;
		}
	}
	return NULL;
}

static bool send_workspace(struct seyrix_server *server) {
	struct seyrix_shell_packet_workspace packet = {
		.header = packet_header(SEYRIX_SHELL_ACTIVE_WORKSPACE, sizeof(packet)),
		.workspace = (uint8_t)server->active_workspace_index,
	};
	return send_packet(server, &packet, sizeof(packet));
}

static bool send_action(struct seyrix_server *server, uint8_t action);

static bool send_window(struct seyrix_window *window) {
	if (window == NULL || !window->mapped || window->workspace == NULL) {
		return true;
	}
	struct seyrix_server *server = window->server;
	struct seyrix_shell_packet_window packet = {
		.header = packet_header(SEYRIX_SHELL_WINDOW_UPSERT, sizeof(packet)),
		.id = window->shell_id,
		.workspace = (uint8_t)(window->workspace - server->workspaces),
	};
	if (window->mapped) packet.flags |= SEYRIX_SHELL_WINDOW_MAPPED;
	if (window->minimized) packet.flags |= SEYRIX_SHELL_WINDOW_MINIMIZED;
	if (server->focused_window == window) packet.flags |= SEYRIX_SHELL_WINDOW_FOCUSED;
	if (window->xdg_toplevel->current.maximized) {
		packet.flags |= SEYRIX_SHELL_WINDOW_MAXIMIZED;
	}
	if (window->xdg_toplevel->current.fullscreen) {
		packet.flags |= SEYRIX_SHELL_WINDOW_FULLSCREEN;
	}
	const char *app_id = window->xdg_toplevel->app_id != NULL ?
		window->xdg_toplevel->app_id : "";
	const char *title = window->xdg_toplevel->title != NULL ?
		window->xdg_toplevel->title : "";
	size_t app_id_length = strnlen(app_id, sizeof(packet.app_id) - 1);
	size_t title_length = strnlen(title, sizeof(packet.title) - 1);
	memcpy(packet.app_id, app_id, app_id_length);
	memcpy(packet.title, title, title_length);
	return send_packet(server, &packet, sizeof(packet));
}

static void send_snapshot(struct seyrix_server *server) {
	if (!send_action(server, SEYRIX_SHELL_SNAPSHOT_BEGIN) ||
			!send_workspace(server)) {
		return;
	}
	struct seyrix_window *windows[SEYRIX_SHELL_IPC_MAX_WINDOWS];
	size_t count = 0;
	struct seyrix_window *window;
	wl_list_for_each(window, &server->windows, link) {
		if (count == SEYRIX_SHELL_IPC_MAX_WINDOWS) {
			break;
		}
		windows[count++] = window;
	}
	/* The compositor list is MRU-first; upserts assign increasing MRU order. */
	while (count > 0) {
		if (!send_window(windows[--count])) {
			return;
		}
	}
}

static bool send_action(struct seyrix_server *server, uint8_t action) {
	struct seyrix_shell_packet_action packet = {
		.header = packet_header(action, sizeof(packet)),
	};
	return send_packet(server, &packet, sizeof(packet));
}

static void restore_focus(struct seyrix_server *server) {
	struct seyrix_window *window = server->shell->restore_focus;
	server->shell->restore_focus = NULL;
	if (window != NULL && window->mapped && !window->minimized &&
			seyrix_workspace_window_is_visible(window)) {
		seyrix_focus_window(server, window);
		return;
	}
	window = seyrix_workspace_focus_candidate(server, NULL);
	seyrix_focus_window(server, window);
}

static void hide_overlay(struct seyrix_server *server, bool restore) {
	if (server->shell == NULL || server->shell->overlay == OVERLAY_NONE) {
		return;
	}
	server->shell->overlay = OVERLAY_NONE;
	if (restore) {
		restore_focus(server);
	} else {
		server->shell->restore_focus = NULL;
	}
}

static void activate_window(struct seyrix_server *server, uint32_t id) {
	struct seyrix_window *window = window_by_shell_id(server, id);
	if (window == NULL) {
		if (server->shell != NULL &&
				server->shell->overlay == OVERLAY_SWITCHER) {
			hide_overlay(server, true);
			(void)send_action(server, SEYRIX_SHELL_CLOSE_OVERLAY);
		}
		return;
	}
	if (!seyrix_workspace_window_is_visible(window)) {
		seyrix_workspace_switch(server,
			(size_t)(window->workspace - server->workspaces));
	}
	if (window->minimized) {
		seyrix_window_state_restore_minimized_window(window);
	} else {
		seyrix_focus_window(server, window);
	}
	hide_overlay(server, false);
	(void)send_action(server, SEYRIX_SHELL_CLOSE_OVERLAY);
}

static bool start_overlay(struct seyrix_server *server, enum overlay_kind kind,
		uint8_t message) {
	if (server->shell == NULL || server->shell->process <= 0 ||
			server->shell->status_socket < 0) {
		return false;
	}
	if (server->shell->overlay == kind) {
		if (kind == OVERLAY_SWITCHER) {
			return send_action(server, SEYRIX_SHELL_SHOW_SWITCHER);
		}
		hide_overlay(server, true);
		(void)send_action(server, SEYRIX_SHELL_CLOSE_OVERLAY);
		return true;
	}
	if (server->shell->overlay == OVERLAY_NONE) {
		server->shell->restore_focus = server->focused_window;
	}
	server->shell->overlay = kind;
	if (!send_action(server, message)) {
		hide_overlay(server, true);
		return false;
	}
	return true;
}

static int restart_shell_client(void *data) {
	struct seyrix_server *server = data;
	if (server->shell != NULL) {
		server->shell->restart_scheduled = false;
	}
	if (server->shell == NULL || server->shell->shutting_down ||
			server->shell->process > 0) {
		return 0;
	}
	if (!seyrix_shell_start(server)) {
		schedule_restart(server);
	} else {
		wlr_log(WLR_INFO, "Seyrix shell client restarted after failure");
	}
	return 0;
}

static void schedule_restart(struct seyrix_server *server) {
	if (server->shell == NULL || server->shell->shutting_down ||
			server->shell->restart_scheduled) {
		return;
	}
	if (server->shell->restart_attempts >= 5) {
		wlr_log(WLR_ERROR, "Seyrix shell restart limit reached for this failure burst");
		return;
	}
	if (server->shell->restart_timer == NULL) {
		server->shell->restart_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->display), restart_shell_client, server);
	}
	if (server->shell->restart_timer == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate shell restart timer");
		return;
	}
	unsigned int delay = 1000U << server->shell->restart_attempts;
	if (wl_event_source_timer_update(server->shell->restart_timer,
			(int)delay) < 0) {
		wlr_log(WLR_ERROR, "Cannot schedule isolated shell restart");
		return;
	}
	server->shell->restart_attempts++;
	server->shell->restart_scheduled = true;
	wlr_log(WLR_INFO, "Scheduling isolated shell restart attempt %u in %u ms",
		server->shell->restart_attempts, delay);
}

static int handle_shell_message(int fd, uint32_t mask, void *data) {
	struct seyrix_server *server = data;
	if (server->shell == NULL || server->shell->status_socket != fd) {
		return 0;
	}
	if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) != 0) {
		disconnect_shell_client(server);
		wlr_log(WLR_INFO, "Seyrix shell channel closed; compositor remains available");
		return 0;
	}
	for (;;) {
		union {
			struct seyrix_shell_packet_header header;
			struct seyrix_shell_packet_workspace workspace;
			struct seyrix_shell_packet_id id;
			struct seyrix_shell_packet_action action;
	} packet;
		ssize_t received = recv(fd, &packet, sizeof(packet),
			MSG_DONTWAIT | MSG_TRUNC);
		if (received == 0) {
			disconnect_shell_client(server);
			return 0;
		}
		if (received < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
			disconnect_shell_client(server);
			return 0;
		}
		struct seyrix_shell_request request;
		if (received < 0 || (size_t)received > sizeof(packet) ||
				!seyrix_shell_ipc_decode_request(&packet, (size_t)received,
					(uint8_t)SEYRIX_WORKSPACE_COUNT, &request)) {
			goto invalid_packet;
		}
		switch (request.type) {
		case SEYRIX_SHELL_SELECT_WORKSPACE:
			seyrix_workspace_switch(server, request.workspace);
			break;
		case SEYRIX_SHELL_REQUEST_FOCUS:
			activate_window(server, request.window_id);
			break;
		case SEYRIX_SHELL_REQUEST_MANUAL:
			(void)start_overlay(server, OVERLAY_MANUAL, SEYRIX_SHELL_SHOW_MANUAL);
			break;
		case SEYRIX_SHELL_REQUEST_LAUNCHER:
			(void)start_overlay(server, OVERLAY_LAUNCHER,
				SEYRIX_SHELL_SHOW_LAUNCHER);
			break;
		case SEYRIX_SHELL_REQUEST_SNAPSHOT:
			send_snapshot(server);
			break;
		case SEYRIX_SHELL_ACTIVATE_WINDOW:
			activate_window(server, request.window_id);
			break;
		case SEYRIX_SHELL_CLOSE_OVERLAY:
			hide_overlay(server, true);
			break;
		case SEYRIX_SHELL_DISMISS_OVERLAY:
			hide_overlay(server, false);
			break;
		default:
			goto invalid_packet;
		}
	}

invalid_packet:
	wlr_log(WLR_ERROR, "Rejected invalid shell IPC packet; restarting shell client");
	disconnect_shell_client(server);
	return 0;
}

bool seyrix_shell_init(struct seyrix_server *server) {
	seyrix_shell_config_defaults(&server->shell_config);
	char config_error[256];
	if (!seyrix_shell_config_load_user(&server->shell_config,
			config_error, sizeof(config_error))) {
		wlr_log(WLR_INFO, "Ignoring invalid shell configuration: %s", config_error);
		seyrix_shell_config_defaults(&server->shell_config);
	}
	if (server->shell_config.auto_hide) {
		wlr_log(WLR_INFO,
			"Shell auto-hide is configured but incomplete; panel remains visible");
	}
	struct seyrix_shell *shell = calloc(1, sizeof(*shell));
	if (shell == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate Seyrix shell process state");
		return false;
	}
	shell->process = -1;
	shell->status_socket = -1;
	server->shell = shell;
	return true;
}

bool seyrix_shell_start(struct seyrix_server *server) {
	if (server->shell == NULL || server->shell->shutting_down ||
			server->shell->process > 0) {
		return false;
	}
	struct seyrix_child *child = calloc(1, sizeof(*child));
	if (child == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate Seyrix shell child record");
		schedule_restart(server);
		return false;
	}
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK,
			0, sockets) != 0) {
		wlr_log(WLR_ERROR, "Cannot create Seyrix shell IPC channel: %s",
			strerror(errno));
		free(child);
		schedule_restart(server);
		return false;
	}
	pid_t pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "Cannot fork Seyrix shell client: %s", strerror(errno));
		close(sockets[0]);
		close(sockets[1]);
		free(child);
		schedule_restart(server);
		return false;
	}
	if (pid == 0) {
		close(sockets[0]);
		if (server->session_process_group <= 0 ||
				setpgid(0, server->session_process_group) != 0) {
			_exit(126);
		}
		int flags = fcntl(sockets[1], F_GETFD);
		if (flags < 0 || fcntl(sockets[1], F_SETFD, flags & ~FD_CLOEXEC) < 0) {
			_exit(126);
		}
		char descriptor[32];
		(void)snprintf(descriptor, sizeof(descriptor), "%d", sockets[1]);
		execl("/usr/bin/seyrix-shell", "seyrix-shell", "--status-fd",
			descriptor, (char *)NULL);
		_exit(127);
	}

	close(sockets[1]);
	child->pid = pid;
	(void)setpgid(pid, server->session_process_group);
	wl_list_insert(&server->children, &child->link);
	server->shell->process = pid;
	server->shell->status_socket = sockets[0];
	server->shell->status_source = wl_event_loop_add_fd(
		wl_display_get_event_loop(server->display), sockets[0],
		WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
		handle_shell_message, server);
	if (server->shell->status_source == NULL) {
		wlr_log(WLR_ERROR, "Cannot register shell IPC channel");
		close_status_channel(server);
		(void)kill(pid, SIGTERM);
		return false;
	}
	server->shell->process_started_ms = monotonic_ms();
	wlr_log(WLR_INFO, "Started isolated Seyrix desktop shell pid=%ld", (long)pid);
	send_snapshot(server);
	return true;
}

void seyrix_shell_child_exited(struct seyrix_server *server, pid_t pid) {
	if (server->shell == NULL || server->shell->process != pid) {
		return;
	}
	uint64_t now = monotonic_ms();
	bool stable = server->shell->process_started_ms != 0 && now >=
		server->shell->process_started_ms && now -
		server->shell->process_started_ms >= 60000U;
	server->shell->process = -1;
	server->shell->process_started_ms = 0;
	close_status_channel(server);
	hide_overlay(server, true);
	wlr_log(WLR_INFO, "Seyrix shell exited; compositor remains available");
	if (stable) {
		server->shell->restart_attempts = 0;
		wlr_log(WLR_INFO,
			"Reset Seyrix shell restart budget after 60-second shell process lifetime");
	}
	schedule_restart(server);
}

void seyrix_shell_window_changed(struct seyrix_window *window) {
	if (window == NULL || window->server == NULL || !window->mapped) {
		return;
	}
	struct seyrix_server *server = window->server;
	if (!send_window(window)) {
		close_status_channel(server);
	}
}

void seyrix_shell_window_removed(struct seyrix_server *server, uint32_t id) {
	if (server == NULL || server->shell == NULL) {
		return;
	}
	if (server->shell->restore_focus != NULL &&
			server->shell->restore_focus->shell_id == id) {
		server->shell->restore_focus = NULL;
	}
	struct seyrix_shell_packet_id packet = {
		.header = packet_header(SEYRIX_SHELL_WINDOW_REMOVE, sizeof(packet)),
		.id = id,
	};
	if (!send_packet(server, &packet, sizeof(packet))) {
		close_status_channel(server);
	}
}

bool seyrix_shell_toggle_launcher(struct seyrix_server *server) {
	return start_overlay(server, OVERLAY_LAUNCHER, SEYRIX_SHELL_SHOW_LAUNCHER);
}

bool seyrix_shell_toggle_switcher(struct seyrix_server *server) {
	return start_overlay(server, OVERLAY_SWITCHER, SEYRIX_SHELL_SHOW_SWITCHER);
}

void seyrix_shell_commit_switcher(struct seyrix_server *server) {
	if (server != NULL && server->shell != NULL &&
			server->shell->overlay == OVERLAY_SWITCHER) {
		(void)send_action(server, SEYRIX_SHELL_COMMIT_SWITCHER);
	}
}

bool seyrix_shell_is_client(struct seyrix_server *server, pid_t pid) {
	return server != NULL && server->shell != NULL &&
		server->shell->process > 0 && server->shell->process == pid;
}

bool seyrix_shell_overlay_matches(struct seyrix_server *server, pid_t pid,
		const char *namespace_name) {
	if (!seyrix_shell_is_client(server, pid) || namespace_name == NULL ||
			server->shell->overlay == OVERLAY_NONE) {
		return false;
	}
	switch (server->shell->overlay) {
	case OVERLAY_LAUNCHER:
		return strcmp(namespace_name, "seyrix-launcher") == 0;
	case OVERLAY_SWITCHER:
		return strcmp(namespace_name, "seyrix-switcher") == 0;
	case OVERLAY_MANUAL:
		return strcmp(namespace_name, "seyrix-manual") == 0;
	case OVERLAY_NONE:
	default:
		return false;
	}
}

void seyrix_shell_overlay_destroyed(struct seyrix_server *server) {
	if (server != NULL && server->shell != NULL) {
		hide_overlay(server, true);
	}
}

void seyrix_shell_update_active_workspace(struct seyrix_server *server) {
	if (server != NULL && server->shell != NULL) {
		send_snapshot(server);
	}
}

void seyrix_shell_outputs_changed(struct seyrix_server *server) {
	(void)server;
}

void seyrix_shell_finish(struct seyrix_server *server) {
	if (server->shell == NULL) {
		return;
	}
	server->shell->shutting_down = true;
	if (server->shell->restart_timer != NULL) {
		wl_event_source_remove(server->shell->restart_timer);
		server->shell->restart_timer = NULL;
	}
	server->shell->restart_scheduled = false;
	close_status_channel(server);
	free(server->shell);
	server->shell = NULL;
}
