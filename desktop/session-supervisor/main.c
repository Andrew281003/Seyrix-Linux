#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "policy.h"
#include "wayland-probe.h"

#include "../common/settings-ipc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

enum {
	CONTROL_TIMEOUT_MS = 1000,
	DISPLAY_POLL_MS = 250,
	DISPLAY_READY_TIMEOUT_MS = 120000,
	WAYLAND_SYNC_TIMEOUT_MS = 1000,
	STABLE_RUNTIME_MS = 60000,
	CHILD_STOP_TIMEOUT_MS = 2000,
};

struct supervisor_paths {
	char runtime[PATH_MAX];
	char socket[PATH_MAX];
	char pid_file[PATH_MAX];
	char lock_file[PATH_MAX];
	char settings_socket[PATH_MAX];
	char wayland_trace_log[PATH_MAX];
};

struct supervisor {
	struct supervisor_paths paths;
	int listener_fd;
	int lock_fd;
	int trace_fd;
	FILE *wayland_trace_stream;
	struct stat socket_stat;
	struct stat pid_stat;
	pid_t settings_pid;
	pid_t trace_pid;
	uint64_t settings_started_ms;
	uint64_t retry_at_ms;
	uint64_t readiness_deadline_ms;
	uint64_t next_display_probe_ms;
	unsigned int attempts;
	char display_name[NAME_MAX + 1];
	char trace_line[8192];
	size_t trace_line_size;
	bool open_requested;
	bool attempt_started;
	bool stopping;
	bool trace_enabled;
	bool ipc_trace_enabled;
	bool wayland_debug_enabled;
};

static volatile sig_atomic_t stop_requested;

static void stop_signal(int signal_number) {
	(void)signal_number;
	stop_requested = 1;
}

static uint64_t monotonic_ms(void) {
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static uint64_t monotonic_ns(void) {
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000000000U + (uint64_t)now.tv_nsec;
}

static void trace_emit_line(struct supervisor *supervisor) {
	if (supervisor->trace_line_size == 0) {
		return;
	}
	FILE *stream = stderr;
	if (supervisor->ipc_trace_enabled && supervisor->wayland_debug_enabled &&
			supervisor->wayland_trace_stream != NULL &&
			supervisor->trace_line[0] == '[') {
		stream = supervisor->wayland_trace_stream;
	}
	char output[sizeof(supervisor->trace_line) + 128];
	int prefix_size = snprintf(output, sizeof(output),
		"SEYRIX_SETTINGS_TRACE monotonic_ns=%" PRIu64
		" source=client-stderr pid=%ld ", monotonic_ns(),
		(long)supervisor->trace_pid);
	if (prefix_size <= 0 || (size_t)prefix_size >= sizeof(output) ||
		supervisor->trace_line_size > sizeof(output) - (size_t)prefix_size - 1) {
		supervisor->trace_line_size = 0;
		return;
	}
	size_t output_size = (size_t)prefix_size;
	memcpy(output + output_size, supervisor->trace_line,
		supervisor->trace_line_size);
	output_size += supervisor->trace_line_size;
	if (supervisor->trace_line[supervisor->trace_line_size - 1] != '\n') {
		output[output_size++] = '\n';
	}
	int output_fd = stream == stderr ? STDERR_FILENO : fileno(stream);
	size_t written = 0;
	/* One write keeps short client traces atomic with compositor log records. */
	while (written < output_size) {
		ssize_t count = write(output_fd, output + written, output_size - written);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			break;
		}
		written += (size_t)count;
	}
	supervisor->trace_line_size = 0;
}

static void trace_finish(struct supervisor *supervisor) {
	trace_emit_line(supervisor);
	if (supervisor->trace_fd >= 0) {
		close(supervisor->trace_fd);
		supervisor->trace_fd = -1;
	}
}

static void trace_drain(struct supervisor *supervisor) {
	if (supervisor->trace_fd < 0) {
		return;
	}
	char bytes[2048];
	for (;;) {
		ssize_t size = read(supervisor->trace_fd, bytes, sizeof(bytes));
		if (size == 0) {
			trace_finish(supervisor);
			return;
		}
		if (size < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return;
			}
			trace_finish(supervisor);
			return;
		}
		for (ssize_t i = 0; i < size; i++) {
			if (bytes[i] == '\n') {
				trace_emit_line(supervisor);
				continue;
			}
			supervisor->trace_line[supervisor->trace_line_size++] = bytes[i];
			if (supervisor->trace_line_size == sizeof(supervisor->trace_line)) {
				trace_emit_line(supervisor);
			}
		}
	}
}

static bool path_set(char *destination, size_t capacity,
		const char *directory, const char *name) {
	int length = snprintf(destination, capacity, "%s/%s", directory, name);
	return length > 0 && (size_t)length < capacity;
}

static bool runtime_dir_valid(const char *path) {
	struct stat status;
	return path != NULL && path[0] == '/' && lstat(path, &status) == 0 &&
		S_ISDIR(status.st_mode) && status.st_uid == geteuid() &&
		(status.st_mode & 0077) == 0;
}

static bool supervisor_paths_init(struct supervisor_paths *paths) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir_valid(runtime) || strlen(runtime) >= sizeof(paths->runtime)) {
		fprintf(stderr, "seyrix-session-supervisor: unsafe XDG_RUNTIME_DIR\n");
		return false;
	}
	memcpy(paths->runtime, runtime, strlen(runtime) + 1);
	if (!path_set(paths->socket, sizeof(paths->socket), runtime,
			"seyrix-settings-supervisor.sock") ||
		!path_set(paths->pid_file, sizeof(paths->pid_file), runtime,
			"seyrix-settings-supervisor.pid") ||
		!path_set(paths->lock_file, sizeof(paths->lock_file), runtime,
			"seyrix-settings-supervisor.lock") ||
		!path_set(paths->settings_socket, sizeof(paths->settings_socket), runtime,
			"seyrix/settings-v1.sock") ||
		!path_set(paths->wayland_trace_log, sizeof(paths->wayland_trace_log),
			runtime, "seyrix-settings-wayland-trace.log")) {
		fprintf(stderr, "seyrix-session-supervisor: runtime path is too long\n");
		return false;
	}
	return true;
}

static bool wayland_trace_log_start(struct supervisor *supervisor) {
	int fd = open(supervisor->paths.wayland_trace_log,
		O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,
		S_IRUSR | S_IWUSR);
	if (fd < 0) {
		return false;
	}
	struct stat status;
	if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
			status.st_uid != geteuid() || status.st_nlink != 1 ||
			(status.st_mode & 0077) != 0) {
		close(fd);
		errno = EPERM;
		return false;
	}
	if (fchmod(fd, S_IRUSR | S_IWUSR) != 0 || ftruncate(fd, 0) != 0) {
		close(fd);
		return false;
	}
	supervisor->wayland_trace_stream = fdopen(fd, "w");
	if (supervisor->wayland_trace_stream == NULL) {
		close(fd);
		return false;
	}
	return true;
}

static bool owned_private_socket(const char *path, struct stat *status) {
	return lstat(path, status) == 0 && S_ISSOCK(status->st_mode) &&
		status->st_uid == geteuid() && (status->st_mode & 0077) == 0;
}

static bool settings_endpoint_ready(const struct supervisor_paths *paths) {
	char settings_directory[PATH_MAX];
	const char *slash = strrchr(paths->settings_socket, '/');
	if (slash == NULL || (size_t)(slash - paths->settings_socket) >=
			sizeof(settings_directory)) {
		return false;
	}
	size_t directory_length = (size_t)(slash - paths->settings_socket);
	memcpy(settings_directory, paths->settings_socket, directory_length);
	settings_directory[directory_length] = '\0';
	struct stat directory_status;
	if (lstat(settings_directory, &directory_status) != 0 ||
			!S_ISDIR(directory_status.st_mode) ||
			directory_status.st_uid != geteuid() ||
			(directory_status.st_mode & 0077) != 0) {
		return false;
	}
	struct stat socket_status;
	if (!owned_private_socket(paths->settings_socket, &socket_status)) {
		return false;
	}
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		return false;
	}
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	if (strlen(paths->settings_socket) >= sizeof(address.sun_path)) {
		close(fd);
		return false;
	}
	memcpy(address.sun_path, paths->settings_socket,
		strlen(paths->settings_socket) + 1);
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
		close(fd);
		return false;
	}
	struct ucred peer;
	socklen_t peer_size = sizeof(peer);
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0 ||
			peer_size != sizeof(peer) || peer.uid != geteuid() || peer.pid <= 0 ||
			getsid(peer.pid) != getsid(0)) {
		close(fd);
		return false;
	}
	uint8_t packet[SEYRIX_SETTINGS_HEADER_SIZE + 4];
	static const uint8_t versions[4] = {1, 1, 0, 0};
	bool ready = seyrix_settings_encode_header(packet, sizeof(packet),
		SEYRIX_SETTINGS_OP_HELLO, 1, sizeof(versions), 0);
	if (ready) {
		memcpy(packet + SEYRIX_SETTINGS_HEADER_SIZE, versions, sizeof(versions));
		ssize_t sent = send(fd, packet, sizeof(packet), MSG_NOSIGNAL);
		ready = sent == (ssize_t)sizeof(packet);
	}
	if (ready) {
		struct pollfd poll_fd = {.fd = fd, .events = POLLIN};
		int result;
		do {
			result = poll(&poll_fd, 1, CONTROL_TIMEOUT_MS);
		} while (result < 0 && errno == EINTR);
		uint8_t response_storage[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
		ssize_t received = result > 0 ? recv(fd, response_storage,
			sizeof(response_storage), 0) : -1;
		struct seyrix_settings_frame response;
		ready = received > 0 && seyrix_settings_decode_frame(response_storage,
			(size_t)received, &response) &&
			response.opcode == SEYRIX_SETTINGS_OP_RESPONSE &&
			response.request_id == 1 && response.payload_size == 6 &&
			seyrix_settings_read_u16(response.payload) ==
				SEYRIX_SETTINGS_ERROR_OK &&
			seyrix_settings_read_u16(response.payload + 2) ==
				SEYRIX_SETTINGS_OP_HELLO &&
			response.payload[4] == 1 && response.payload[5] == 0;
	}
	close(fd);
	return ready;
}

static void accept_control_requests(struct supervisor *supervisor);

static bool service_control_during_probe(void *data) {
	struct supervisor *supervisor = data;
	accept_control_requests(supervisor);
	return !supervisor->stopping && !stop_requested;
}

static bool wayland_display_ready(struct supervisor *supervisor,
		const char *name, uint64_t readiness_deadline_ms) {
	struct wl_display *display = wl_display_connect(name);
	if (display == NULL) {
		return false;
	}
	uint64_t now = monotonic_ms();
	if (now >= readiness_deadline_ms) {
		wl_display_disconnect(display);
		return false;
	}
	uint64_t remaining = readiness_deadline_ms - now;
	unsigned int timeout_ms = remaining < WAYLAND_SYNC_TIMEOUT_MS ?
		(unsigned int)remaining : WAYLAND_SYNC_TIMEOUT_MS;
	bool ready = seyrix_wayland_sync_wait(display, supervisor->listener_fd,
		timeout_ms, service_control_during_probe, supervisor);
	wl_display_disconnect(display);
	return ready && !supervisor->stopping && !stop_requested;
}

static bool find_ready_display(struct supervisor *supervisor,
		char *display_name, size_t capacity) {
	DIR *directory = opendir(supervisor->paths.runtime);
	if (directory == NULL) {
		return false;
	}
	struct dirent *entry;
	while ((entry = readdir(directory)) != NULL) {
		if (supervisor->stopping || stop_requested) {
			break;
		}
		if (strncmp(entry->d_name, "wayland-", sizeof("wayland-") - 1) != 0 ||
				strlen(entry->d_name) >= capacity) {
			continue;
		}
		char socket_path[PATH_MAX];
		if (!path_set(socket_path, sizeof(socket_path), supervisor->paths.runtime,
				entry->d_name)) {
			continue;
		}
		struct stat status;
		if (lstat(socket_path, &status) != 0 || !S_ISSOCK(status.st_mode) ||
				status.st_uid != geteuid() ||
				!wayland_display_ready(supervisor, entry->d_name,
					supervisor->readiness_deadline_ms) ||
				!settings_endpoint_ready(&supervisor->paths)) {
			continue;
		}
		memcpy(display_name, entry->d_name, strlen(entry->d_name) + 1);
		closedir(directory);
		return true;
	}
	closedir(directory);
	return false;
}

static int lock_manager(struct supervisor *supervisor) {
	int fd = open(supervisor->paths.lock_file,
		O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		return -1;
	}
	struct stat status;
	if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
			status.st_uid != geteuid() || (status.st_mode & 0077) != 0) {
		close(fd);
		errno = EPERM;
		return -1;
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		int saved_errno = errno;
		close(fd);
		if (saved_errno == EWOULDBLOCK || saved_errno == EAGAIN) {
			return 0;
		}
		errno = saved_errno;
		return -1;
	}
	supervisor->lock_fd = fd;
	return 1;
}

static bool remove_owned_stale_socket(const char *path) {
	struct stat status;
	if (lstat(path, &status) != 0) {
		return errno == ENOENT;
	}
	if (!S_ISSOCK(status.st_mode) || status.st_uid != geteuid()) {
		errno = EPERM;
		return false;
	}
	return unlink(path) == 0;
}

static bool write_pid_file(struct supervisor *supervisor) {
	int fd = open(supervisor->paths.pid_file,
		O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		return false;
	}
	struct stat status;
	char contents[32];
	int length = snprintf(contents, sizeof(contents), "%ld\n", (long)getpid());
	bool success = fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
		status.st_uid == geteuid() && fchmod(fd, 0600) == 0 && length > 0 &&
		(size_t)length < sizeof(contents) && write(fd, contents,
		(size_t)length) == length && fsync(fd) == 0;
	if (success) {
		supervisor->pid_stat = status;
	}
	close(fd);
	return success;
}

static bool server_start(struct supervisor *supervisor) {
	if (!remove_owned_stale_socket(supervisor->paths.socket)) {
		perror("seyrix-session-supervisor: clear stale control socket");
		return false;
	}
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0) {
		return false;
	}
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	if (strlen(supervisor->paths.socket) >= sizeof(address.sun_path)) {
		close(fd);
		errno = ENAMETOOLONG;
		return false;
	}
	memcpy(address.sun_path, supervisor->paths.socket,
		strlen(supervisor->paths.socket) + 1);
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
			chmod(supervisor->paths.socket, 0600) != 0 || listen(fd, 8) != 0 ||
			!owned_private_socket(supervisor->paths.socket,
				&supervisor->socket_stat) || !write_pid_file(supervisor)) {
		int saved_errno = errno;
		close(fd);
		(void)remove_owned_stale_socket(supervisor->paths.socket);
		errno = saved_errno;
		return false;
	}
	supervisor->listener_fd = fd;
	fprintf(stderr,
		"SEYRIX_SETTINGS_SUPERVISOR event=ready pid=%ld pgid=%ld sid=%ld socket=%s\n",
		(long)getpid(), (long)getpgrp(), (long)getsid(0),
		supervisor->paths.socket);
	return true;
}

static bool unlink_if_same_inode(const char *path,
		const struct stat *expected) {
	struct stat current;
	if (lstat(path, &current) != 0) {
		return errno == ENOENT;
	}
	if (current.st_dev != expected->st_dev || current.st_ino != expected->st_ino ||
			current.st_uid != geteuid()) {
		return false;
	}
	return unlink(path) == 0;
}

static void server_finish(struct supervisor *supervisor) {
	if (supervisor->listener_fd >= 0) {
		close(supervisor->listener_fd);
		supervisor->listener_fd = -1;
		(void)unlink_if_same_inode(supervisor->paths.socket,
			&supervisor->socket_stat);
	}
	(void)unlink_if_same_inode(supervisor->paths.pid_file,
		&supervisor->pid_stat);
	if (supervisor->lock_fd >= 0) {
		close(supervisor->lock_fd);
		supervisor->lock_fd = -1;
	}
	if (supervisor->wayland_trace_stream != NULL) {
		fclose(supervisor->wayland_trace_stream);
		supervisor->wayland_trace_stream = NULL;
	}
}

static bool control_request(const struct supervisor_paths *paths,
		const char *command, bool missing_is_success) {
	struct stat socket_status;
	if (!owned_private_socket(paths->socket, &socket_status)) {
		return missing_is_success && errno == ENOENT;
	}
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		return false;
	}
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	if (strlen(paths->socket) >= sizeof(address.sun_path)) {
		close(fd);
		return false;
	}
	memcpy(address.sun_path, paths->socket, strlen(paths->socket) + 1);
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
		int saved_errno = errno;
		close(fd);
		return missing_is_success && (saved_errno == ENOENT ||
			saved_errno == ECONNREFUSED);
	}
	struct ucred peer = {0};
	socklen_t peer_size = sizeof(peer);
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0 ||
			peer_size != sizeof(peer) || peer.pid <= 0 ||
			!seyrix_supervisor_peer_authorized(peer.uid, peer.pid,
				getsid(peer.pid), geteuid(), getsid(0))) {
		close(fd);
		return false;
	}
	const char *expected_reply = strcmp(command, "PING") == 0 ? "READY" :
		(strcmp(command, "OPEN") == 0 || strcmp(command, "STOP") == 0 ?
			"OK" : NULL);
	if (expected_reply == NULL) {
		close(fd);
		return false;
	}
	ssize_t sent = send(fd, command, strlen(command), MSG_NOSIGNAL);
	struct pollfd poll_fd = {.fd = fd, .events = POLLIN};
	int ready = sent == (ssize_t)strlen(command) ?
		poll(&poll_fd, 1, CONTROL_TIMEOUT_MS) : -1;
	char response[16] = {0};
	ssize_t received = ready > 0 ? recv(fd, response, sizeof(response) - 1, 0) : -1;
	close(fd);
	return received > 0 && seyrix_supervisor_packet_matches(response,
		(size_t)received, expected_reply);
}

static void schedule_next_attempt(struct supervisor *supervisor,
		uint64_t now) {
	if (!seyrix_settings_attempt_allowed(supervisor->attempts)) {
		supervisor->open_requested = false;
		supervisor->attempt_started = false;
		fprintf(stderr,
			"SEYRIX_SETTINGS_SUPERVISOR event=restart-limit pid=%ld attempts=%u\n",
			(long)getpid(), supervisor->attempts);
		return;
	}
	supervisor->attempt_started = false;
	supervisor->readiness_deadline_ms = 0;
	unsigned int delay_seconds = seyrix_settings_retry_delay_seconds(
		supervisor->attempts > 0 ? supervisor->attempts - 1 : 0);
	supervisor->retry_at_ms = now + (uint64_t)delay_seconds * 1000U;
	fprintf(stderr,
		"SEYRIX_SETTINGS_SUPERVISOR event=retry-scheduled completed_attempt=%u next_attempt=%u delay_ms=%u scheduled_ms=%llu retry_at_ms=%llu\n",
		supervisor->attempts, supervisor->attempts + 1,
		delay_seconds * 1000U, (unsigned long long)now,
		(unsigned long long)supervisor->retry_at_ms);
}

static void stop_settings_child(struct supervisor *supervisor) {
	if (supervisor->settings_pid <= 0) {
		return;
	}
	(void)kill(supervisor->settings_pid, SIGTERM);
	uint64_t deadline = monotonic_ms() + CHILD_STOP_TIMEOUT_MS;
	int status = 0;
	while (monotonic_ms() < deadline) {
		pid_t result = waitpid(supervisor->settings_pid, &status, WNOHANG);
		if (result == supervisor->settings_pid ||
				(result < 0 && errno == ECHILD)) {
			supervisor->settings_pid = 0;
			return;
		}
		struct timespec pause = {.tv_sec = 0, .tv_nsec = 20000000};
		nanosleep(&pause, NULL);
	}
	(void)kill(supervisor->settings_pid, SIGKILL);
	while (waitpid(supervisor->settings_pid, &status, 0) < 0 && errno == EINTR) {
	}
	supervisor->settings_pid = 0;
}

static bool launch_settings(struct supervisor *supervisor) {
	int trace_pipe[2] = {-1, -1};
	if (supervisor->trace_enabled && pipe2(trace_pipe, O_CLOEXEC) == 0) {
		int flags = fcntl(trace_pipe[0], F_GETFL);
		if (flags < 0 || fcntl(trace_pipe[0], F_SETFL, flags | O_NONBLOCK) != 0) {
			close(trace_pipe[0]);
			close(trace_pipe[1]);
			trace_pipe[0] = -1;
			trace_pipe[1] = -1;
			fprintf(stderr,
				"SEYRIX_SETTINGS_SUPERVISOR event=trace-unavailable reason=pipe-flags\n");
		}
	} else if (supervisor->trace_enabled) {
		fprintf(stderr,
			"SEYRIX_SETTINGS_SUPERVISOR event=trace-unavailable reason=pipe\n");
	}
	pid_t pid = fork();
	if (pid < 0) {
		if (trace_pipe[0] >= 0) close(trace_pipe[0]);
		if (trace_pipe[1] >= 0) close(trace_pipe[1]);
		perror("seyrix-session-supervisor: fork Settings");
		return false;
	}
	if (pid == 0) {
		if (trace_pipe[0] >= 0) {
			close(trace_pipe[0]);
			if (dup2(trace_pipe[1], STDERR_FILENO) < 0) {
				_exit(126);
			}
			close(trace_pipe[1]);
		}
		struct sigaction action = {.sa_handler = SIG_DFL};
		sigemptyset(&action.sa_mask);
		(void)sigaction(SIGTERM, &action, NULL);
		(void)sigaction(SIGINT, &action, NULL);
		(void)sigaction(SIGCHLD, &action, NULL);
		if (setpgid(0, 0) != 0 || setenv("WAYLAND_DISPLAY",
				supervisor->display_name, 1) != 0 ||
			setenv("SEYRIX_SETTINGS_SUPERVISED", "1", 1) != 0) {
			_exit(126);
		}
		if (supervisor->wayland_debug_enabled &&
				setenv("WAYLAND_DEBUG", "client", 1) != 0) {
			_exit(126);
		}
		execl("/usr/bin/seyrix-settings", "seyrix-settings", (char *)NULL);
		perror("seyrix-session-supervisor: exec Settings");
		_exit(127);
	}
	if (trace_pipe[1] >= 0) close(trace_pipe[1]);
	if (trace_pipe[0] >= 0) {
		if (supervisor->trace_fd >= 0) {
			trace_drain(supervisor);
			if (supervisor->trace_fd >= 0) trace_finish(supervisor);
		}
		supervisor->trace_fd = trace_pipe[0];
		supervisor->trace_pid = pid;
		supervisor->trace_line_size = 0;
	}
	supervisor->settings_pid = pid;
	supervisor->settings_started_ms = monotonic_ms();
	fprintf(stderr,
		"SEYRIX_SETTINGS_SUPERVISOR event=launch pid=%ld attempt=%u display=%s\n",
		(long)pid, supervisor->attempts, supervisor->display_name);
	return true;
}

static void reap_settings_child(struct supervisor *supervisor) {
	if (supervisor->settings_pid <= 0) {
		return;
	}
	int status = 0;
	pid_t result = waitpid(supervisor->settings_pid, &status, WNOHANG);
	if (result == 0 || (result < 0 && errno == EINTR)) {
		return;
	}
	pid_t exited_pid = supervisor->settings_pid;
	supervisor->settings_pid = 0;
	unsigned int runtime_ms = (unsigned int)(monotonic_ms() -
		supervisor->settings_started_ms);
	if (result < 0) {
		status = SIGKILL;
	}
	if (WIFEXITED(status)) {
		fprintf(stderr,
			"SEYRIX_SETTINGS_SUPERVISOR event=exit pid=%ld status=%d runtime_ms=%u\n",
			(long)exited_pid, WEXITSTATUS(status), runtime_ms);
	} else if (WIFSIGNALED(status)) {
		fprintf(stderr,
			"SEYRIX_SETTINGS_SUPERVISOR event=exit pid=%ld signal=%d runtime_ms=%u\n",
			(long)exited_pid, WTERMSIG(status), runtime_ms);
	}
	if (seyrix_settings_exit_is_intentional(status)) {
		supervisor->open_requested = false;
		supervisor->attempts = 0;
		supervisor->attempt_started = false;
		return;
	}
	if (!supervisor->open_requested || supervisor->stopping) {
		return;
	}
	if (runtime_ms >= STABLE_RUNTIME_MS) {
		supervisor->attempts = 0;
	}
	schedule_next_attempt(supervisor, monotonic_ms());
}

static void accept_control_requests(struct supervisor *supervisor) {
	for (unsigned int count = 0; count < 16; count++) {
		int fd = accept4(supervisor->listener_fd, NULL, NULL,
			SOCK_CLOEXEC | SOCK_NONBLOCK);
		if (fd < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return;
			}
			if (errno == EINTR) {
				count--;
				continue;
			}
			return;
		}
		struct ucred peer = {0};
		socklen_t peer_size = sizeof(peer);
		pid_t peer_session = -1;
		if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) == 0 &&
				peer_size == sizeof(peer) && peer.pid > 0) {
			peer_session = getsid(peer.pid);
		}
		if (!seyrix_supervisor_peer_authorized(peer.uid, peer.pid, peer_session,
				geteuid(), getsid(0))) {
			close(fd);
			continue;
		}
		char command[16] = {0};
		struct pollfd client_fd = {.fd = fd, .events = POLLIN};
		int client_ready;
		do {
			client_ready = poll(&client_fd, 1, CONTROL_TIMEOUT_MS);
		} while (client_ready < 0 && errno == EINTR);
		ssize_t received = client_ready > 0 ?
			recv(fd, command, sizeof(command) - 1, 0) : -1;
		const char *reply = "NO";
		if (received > 0 && seyrix_supervisor_packet_matches(command,
				(size_t)received, "PING")) {
			reply = "READY";
		} else if (received > 0 && seyrix_supervisor_packet_matches(command,
				(size_t)received, "OPEN")) {
			/* Reap a just-closed child before deciding whether this is a reopen. */
			reap_settings_child(supervisor);
			if (!supervisor->open_requested && supervisor->settings_pid <= 0) {
				supervisor->open_requested = true;
				supervisor->attempts = 0;
				supervisor->retry_at_ms = monotonic_ms();
				supervisor->readiness_deadline_ms = 0;
				supervisor->attempt_started = false;
				fprintf(stderr,
					"SEYRIX_SETTINGS_SUPERVISOR event=open-request pid=%ld\n",
					(long)getpid());
			}
			reply = "OK";
		} else if (received > 0 && seyrix_supervisor_packet_matches(command,
				(size_t)received, "STOP")) {
			supervisor->stopping = true;
			reply = "OK";
			fprintf(stderr,
				"SEYRIX_SETTINGS_SUPERVISOR event=session-end pid=%ld\n",
				(long)getpid());
		}
		(void)send(fd, reply, strlen(reply), MSG_NOSIGNAL);
		close(fd);
	}
}

static void manager_step(struct supervisor *supervisor, uint64_t now) {
	reap_settings_child(supervisor);
	if (!supervisor->open_requested || supervisor->settings_pid > 0 ||
			now < supervisor->retry_at_ms) {
		return;
	}
	if (!supervisor->attempt_started) {
		if (!seyrix_settings_attempt_allowed(supervisor->attempts)) {
			schedule_next_attempt(supervisor, now);
			return;
		}
		supervisor->attempts++;
		supervisor->attempt_started = true;
		supervisor->readiness_deadline_ms = now + DISPLAY_READY_TIMEOUT_MS;
		supervisor->next_display_probe_ms = now;
		fprintf(stderr,
			"SEYRIX_SETTINGS_SUPERVISOR event=attempt attempt=%u pid=%ld monotonic_ms=%llu\n",
			supervisor->attempts, (long)getpid(), (unsigned long long)now);
	}
	if (now < supervisor->next_display_probe_ms) {
		return;
	}
	if (find_ready_display(supervisor, supervisor->display_name,
			sizeof(supervisor->display_name))) {
		fprintf(stderr,
			"SEYRIX_SETTINGS_SUPERVISOR event=wayland-ready display=%s attempt=%u\n",
			supervisor->display_name, supervisor->attempts);
		supervisor->attempt_started = false;
		supervisor->readiness_deadline_ms = 0;
		if (!launch_settings(supervisor)) {
			schedule_next_attempt(supervisor, now);
		}
		return;
	}
	if (supervisor->stopping || stop_requested) {
		return;
	}
	if (now >= supervisor->readiness_deadline_ms) {
		fprintf(stderr,
			"SEYRIX_SETTINGS_SUPERVISOR event=readiness-timeout attempt=%u\n",
			supervisor->attempts);
		schedule_next_attempt(supervisor, now);
		return;
	}
	supervisor->next_display_probe_ms = now + DISPLAY_POLL_MS;
}

static int run_manager(struct supervisor *supervisor) {
	struct sigaction action = {.sa_handler = stop_signal};
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGTERM, &action, NULL) != 0 ||
			sigaction(SIGINT, &action, NULL) != 0) {
		return 1;
	}
	while (!stop_requested && !supervisor->stopping) {
		manager_step(supervisor, monotonic_ms());
		struct pollfd poll_fds[2] = {
			{
				.fd = supervisor->listener_fd,
				.events = POLLIN,
			},
		};
		nfds_t poll_count = 1;
		if (supervisor->trace_fd >= 0) {
			poll_fds[poll_count++] = (struct pollfd) {
				.fd = supervisor->trace_fd,
				.events = POLLIN,
			};
		}
		int result = poll(poll_fds, poll_count, 100);
		if (result < 0 && errno == EINTR) {
			continue;
		}
		if (result < 0 || (poll_fds[0].revents &
				(POLLERR | POLLHUP | POLLNVAL)) != 0) {
			return 1;
		}
		if (result > 0 && (poll_fds[0].revents & POLLIN) != 0) {
			accept_control_requests(supervisor);
		}
		if (poll_count == 2 && poll_fds[1].revents != 0) {
			trace_drain(supervisor);
		}
	}
	if (supervisor->settings_pid > 0) {
		fprintf(stderr,
			"SEYRIX_SETTINGS_SUPERVISOR event=stop-settings pid=%ld\n",
			(long)supervisor->settings_pid);
		stop_settings_child(supervisor);
	}
	trace_drain(supervisor);
	return 0;
}

static int serve(void) {
	if (setpgid(0, 0) != 0) {
		perror("seyrix-session-supervisor: setpgid");
		return 1;
	}
	struct supervisor supervisor = {
		.listener_fd = -1,
		.lock_fd = -1,
		.trace_fd = -1,
		.wayland_trace_stream = NULL,
	};
	const char *wayland_debug = getenv("WAYLAND_DEBUG");
	const char *ipc_trace = getenv("SEYRIX_SETTINGS_IPC_TRACE");
	supervisor.ipc_trace_enabled = ipc_trace != NULL &&
		strcmp(ipc_trace, "1") == 0;
	supervisor.wayland_debug_enabled = wayland_debug != NULL &&
		strcmp(wayland_debug, "client") == 0;
	supervisor.trace_enabled = supervisor.wayland_debug_enabled ||
		supervisor.ipc_trace_enabled;
	if (supervisor.wayland_debug_enabled) {
		(void)unsetenv("WAYLAND_DEBUG");
	}
	if (!supervisor_paths_init(&supervisor.paths)) {
		return 1;
	}
	int locked = lock_manager(&supervisor);
	if (locked == 0) {
		return 0;
	}
	if (locked < 0) {
		perror("seyrix-session-supervisor: manager lock");
		return 1;
	}
	if (!server_start(&supervisor)) {
		perror("seyrix-session-supervisor: start control socket");
		server_finish(&supervisor);
		return 1;
	}
	if (supervisor.ipc_trace_enabled && supervisor.wayland_debug_enabled &&
			!wayland_trace_log_start(&supervisor)) {
		perror("seyrix-session-supervisor: capture Wayland trace");
		server_finish(&supervisor);
		return 1;
	}
	int status = run_manager(&supervisor);
	server_finish(&supervisor);
	return status;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr,
			"Usage: seyrix-session-supervisor --serve|--ping|--open|--stop\n");
		return 2;
	}
	struct supervisor_paths paths;
	if (!supervisor_paths_init(&paths)) {
		return 1;
	}
	if (strcmp(argv[1], "--serve") == 0) {
		return serve();
	}
	if (strcmp(argv[1], "--ping") == 0) {
		return control_request(&paths, "PING", false) ? 0 : 1;
	}
	if (strcmp(argv[1], "--open") == 0) {
		return control_request(&paths, "OPEN", false) ? 0 : 1;
	}
	if (strcmp(argv[1], "--stop") == 0) {
		return control_request(&paths, "STOP", true) ? 0 : 1;
	}
	fprintf(stderr,
		"Usage: seyrix-session-supervisor --serve|--ping|--open|--stop\n");
	return 2;
}
