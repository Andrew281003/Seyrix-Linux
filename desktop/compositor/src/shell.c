#define _POSIX_C_SOURCE 200809L

#include "shell.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <wlr/util/log.h>

struct seyrix_shell {
	pid_t process;
	int status_socket;
	struct wl_event_source *status_source;
};

enum seyrix_shell_message_type {
	SEYRIX_SHELL_ACTIVE_WORKSPACE = 1,
	SEYRIX_SHELL_SELECT_WORKSPACE = 2,
};

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

static int handle_shell_message(int fd, uint32_t mask, void *data) {
	struct seyrix_server *server = data;
	if (server->shell == NULL || server->shell->status_socket != fd) {
		return 0;
	}
	if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) != 0) {
		close_status_channel(server);
		wlr_log(WLR_INFO, "Seyrix panel channel closed; compositor remains available");
		return 0;
	}
	for (;;) {
		uint8_t message[2];
		ssize_t received = recv(fd, message, sizeof(message), MSG_DONTWAIT);
		if (received == 0) {
			close_status_channel(server);
			wlr_log(WLR_INFO, "Seyrix panel exited; compositor remains available");
			return 0;
		}
		if (received < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return 0;
			}
			close_status_channel(server);
			wlr_log(WLR_INFO, "Seyrix panel channel failed: %s", strerror(errno));
			return 0;
		}
		if (received == (ssize_t)sizeof(message) &&
				message[0] == SEYRIX_SHELL_SELECT_WORKSPACE &&
				message[1] < SEYRIX_WORKSPACE_COUNT) {
			seyrix_workspace_switch(server, message[1]);
		}
	}
}

static bool send_workspace(struct seyrix_server *server) {
	if (server->shell == NULL || server->shell->status_socket < 0) {
		return false;
	}
	uint8_t message[2] = {
		SEYRIX_SHELL_ACTIVE_WORKSPACE,
		(uint8_t)server->active_workspace_index,
	};
	ssize_t sent = send(server->shell->status_socket, message,
		sizeof(message), MSG_DONTWAIT | MSG_NOSIGNAL);
	if (sent == (ssize_t)sizeof(message)) {
		return true;
	}
	if (errno == EAGAIN || errno == EWOULDBLOCK) {
		return true;
	}
	close_status_channel(server);
	wlr_log(WLR_INFO, "Seyrix panel exited; workspace indicator updates stopped");
	return false;
}

bool seyrix_shell_init(struct seyrix_server *server) {
	struct seyrix_shell *shell = calloc(1, sizeof(*shell));
	if (shell == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate Seyrix panel process state");
		return false;
	}
	shell->process = -1;
	shell->status_socket = -1;
	server->shell = shell;
	return true;
}

bool seyrix_shell_start(struct seyrix_server *server) {
	if (server->shell == NULL) {
		return false;
	}
	struct seyrix_child *child = calloc(1, sizeof(*child));
	if (child == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate Seyrix panel process record");
		return false;
	}
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK,
			0, sockets) != 0) {
		wlr_log(WLR_ERROR, "Cannot create Seyrix panel status channel: %s",
			strerror(errno));
		free(child);
		return false;
	}
	pid_t pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "Cannot fork Seyrix panel process: %s", strerror(errno));
		close(sockets[0]);
		close(sockets[1]);
		free(child);
		return false;
	}
	if (pid == 0) {
		close(sockets[0]);
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
	wl_list_insert(&server->children, &child->link);
	server->shell->process = pid;
	server->shell->status_socket = sockets[0];
	server->shell->status_source = wl_event_loop_add_fd(
		wl_display_get_event_loop(server->display), sockets[0],
		WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
		handle_shell_message, server);
	if (server->shell->status_source == NULL) {
		wlr_log(WLR_ERROR, "Cannot add Seyrix panel channel to compositor event loop");
		close_status_channel(server);
		(void)kill(pid, SIGTERM);
		return false;
	}
	(void)send_workspace(server);
	wlr_log(WLR_INFO, "Started isolated Seyrix layer-shell panel pid=%ld",
		(long)pid);
	return true;
}

void seyrix_shell_child_exited(struct seyrix_server *server, pid_t pid) {
	if (server->shell == NULL || server->shell->process != pid) {
		return;
	}
	server->shell->process = -1;
	close_status_channel(server);
	wlr_log(WLR_INFO, "Seyrix panel exited; compositor remains available");
}

void seyrix_shell_update_active_workspace(struct seyrix_server *server) {
	(void)send_workspace(server);
}

void seyrix_shell_outputs_changed(struct seyrix_server *server) {
	(void)server;
}

void seyrix_shell_finish(struct seyrix_server *server) {
	if (server->shell == NULL) {
		return;
	}
	close_status_channel(server);
	free(server->shell);
	server->shell = NULL;
}
