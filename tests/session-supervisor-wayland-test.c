#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../desktop/session-supervisor/wayland-probe.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

static uint64_t monotonic_ms(void) {
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static bool read_exact(int fd, void *buffer, size_t length) {
	uint8_t *bytes = buffer;
	size_t offset = 0;
	while (offset < length) {
		ssize_t result = read(fd, bytes + offset, length - offset);
		if (result > 0) {
			offset += (size_t)result;
			continue;
		}
		if (result < 0 && errno == EINTR) {
			continue;
		}
		if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			struct pollfd poll_fd = {.fd = fd, .events = POLLIN};
			if (poll(&poll_fd, 1, 1000) > 0) {
				continue;
			}
		}
		return false;
	}
	return true;
}

static bool write_exact(int fd, const void *buffer, size_t length) {
	const uint8_t *bytes = buffer;
	size_t offset = 0;
	while (offset < length) {
		ssize_t result = write(fd, bytes + offset, length - offset);
		if (result > 0) {
			offset += (size_t)result;
			continue;
		}
		if (result < 0 && errno == EINTR) {
			continue;
		}
		if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			struct pollfd poll_fd = {.fd = fd, .events = POLLOUT};
			if (poll(&poll_fd, 1, 1000) > 0) {
				continue;
			}
		}
		return false;
	}
	return true;
}

static bool child_exited_successfully(pid_t child) {
	int status = 0;
	pid_t result;
	do {
		result = waitpid(child, &status, 0);
	} while (result < 0 && errno == EINTR);
	return result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int respond_to_sync(int fd) {
	for (;;) {
		uint32_t header[2];
		if (!read_exact(fd, header, sizeof(header))) {
			return 1;
		}
		uint32_t size = header[1] >> 16;
		uint32_t opcode = header[1] & 0xffffU;
		if (size < sizeof(header) || size > 4096) {
			return 1;
		}
		uint8_t payload[4088] = {0};
		size_t payload_size = size - sizeof(header);
		if (!read_exact(fd, payload, payload_size)) {
			return 1;
		}
		if (header[0] == 1 && opcode == 0 && payload_size >= sizeof(uint32_t)) {
			uint32_t reply[3];
			memcpy(&reply[0], payload, sizeof(reply[0]));
			reply[1] = (12U << 16);
			reply[2] = 1;
			return write_exact(fd, reply, sizeof(reply)) ? 0 : 1;
		}
	}
}

struct control_state {
	int fd;
	unsigned int calls;
};

static bool stop_on_control(void *data) {
	struct control_state *state = data;
	char command = 0;
	state->calls++;
	return read(state->fd, &command, sizeof(command)) != sizeof(command);
}

static int test_silent_peer_times_out(void) {
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
		return 1;
	}
	struct wl_display *display = wl_display_connect_to_fd(sockets[0]);
	if (display == NULL) {
		close(sockets[1]);
		return 1;
	}
	uint64_t started = monotonic_ms();
	bool ready = seyrix_wayland_sync_wait(display, -1, 100, NULL, NULL);
	uint64_t elapsed = monotonic_ms() - started;
	wl_display_disconnect(display);
	close(sockets[1]);
	return ready || elapsed < 50 || elapsed > 1000;
}

static int test_responsive_peer_completes_sync(void) {
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
		return 1;
	}
	pid_t server = fork();
	if (server < 0) {
		close(sockets[0]);
		close(sockets[1]);
		return 1;
	}
	if (server == 0) {
		close(sockets[0]);
		_exit(respond_to_sync(sockets[1]));
	}
	close(sockets[1]);
	struct wl_display *display = wl_display_connect_to_fd(sockets[0]);
	if (display == NULL) {
		(void)kill(server, SIGKILL);
		(void)waitpid(server, NULL, 0);
		return 1;
	}
	bool ready = seyrix_wayland_sync_wait(display, -1, 1000, NULL, NULL);
	wl_display_disconnect(display);
	return !ready || !child_exited_successfully(server);
}

static int test_control_is_serviced_during_sync_wait(void) {
	int wayland_sockets[2];
	int control_sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0,
			wayland_sockets) != 0 ||
		socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0,
			control_sockets) != 0) {
		return 1;
	}
	pid_t sender = fork();
	if (sender < 0) {
		return 1;
	}
	if (sender == 0) {
		close(wayland_sockets[0]);
		close(wayland_sockets[1]);
		close(control_sockets[0]);
		struct timespec pause = {.tv_sec = 0, .tv_nsec = 20000000};
		nanosleep(&pause, NULL);
		char command = 'S';
		_exit(write(control_sockets[1], &command, sizeof(command)) ==
			sizeof(command) ? 0 : 1);
	}
	close(control_sockets[1]);
	struct wl_display *display = wl_display_connect_to_fd(wayland_sockets[0]);
	if (display == NULL) {
		(void)kill(sender, SIGKILL);
		(void)waitpid(sender, NULL, 0);
		return 1;
	}
	struct control_state state = {.fd = control_sockets[0]};
	bool ready = seyrix_wayland_sync_wait(display, control_sockets[0], 1000,
		stop_on_control, &state);
	wl_display_disconnect(display);
	close(wayland_sockets[1]);
	close(control_sockets[0]);
	return ready || state.calls != 1 || !child_exited_successfully(sender);
}

int main(void) {
	return test_silent_peer_times_out() ||
		test_responsive_peer_completes_sync() ||
		test_control_is_serviced_during_sync_wait();
}
