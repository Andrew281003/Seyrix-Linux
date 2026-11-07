#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../desktop/common/settings-ipc.h"
#include "../desktop/settings/src/settings-client.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <glib.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

struct client_fixture {
	int listener_fd;
	int active_fd;
	char runtime_directory[128];
	char private_directory[160];
	char socket_path[192];
	pthread_t server_thread;
	struct seyrix_settings_client client;
	GMainLoop *loop;
	unsigned int timer_source;
	unsigned int timeout_source;
	bool saw_disconnect;
	bool timed_out;
	int server_error;
	volatile gint connections;
	volatile gint close_first_connection;
	volatile gint stop_server;
	size_t connected_fd_count;
};

static size_t open_fd_count(void) {
	DIR *directory = opendir("/proc/self/fd");
	g_assert_nonnull(directory);
	size_t count = 0;
	struct dirent *entry;
	while ((entry = readdir(directory)) != NULL) {
		if (entry->d_name[0] != '.') count++;
	}
	g_assert_cmpint(closedir(directory), ==, 0);
	return count;
}

static void add_tlv(uint8_t *buffer, size_t capacity, size_t *used,
		uint16_t type, const uint8_t *value, size_t length) {
	g_assert_cmpuint(*used + 4 + length, <=, capacity);
	seyrix_settings_write_u16(buffer + *used, type);
	seyrix_settings_write_u16(buffer + *used + 2, (uint16_t)length);
	if (length > 0) {
		memcpy(buffer + *used + 4, value, length);
	}
	*used += 4 + length;
}

static void add_u8(uint8_t *buffer, size_t capacity, size_t *used,
		uint16_t type, uint8_t value) {
	add_tlv(buffer, capacity, used, type, &value, sizeof(value));
}

static void add_u16(uint8_t *buffer, size_t capacity, size_t *used,
		uint16_t type, uint16_t value) {
	uint8_t encoded[2];
	seyrix_settings_write_u16(encoded, value);
	add_tlv(buffer, capacity, used, type, encoded, sizeof(encoded));
}

static void add_workspace(uint8_t *buffer, size_t capacity, size_t *used,
		uint8_t index) {
	uint8_t encoded[16] = {index, 1, 0, 0};
	seyrix_settings_write_u32(encoded + 4, 8);
	seyrix_settings_write_u32(encoded + 8, 16);
	seyrix_settings_write_u32(encoded + 12, 500);
	add_tlv(buffer, capacity, used, SEYRIX_SETTINGS_TLV_WM_WORKSPACE,
		encoded, sizeof(encoded));
}

static size_t build_snapshot(uint8_t *buffer, size_t capacity) {
	size_t used = 0;
	add_u8(buffer, capacity, &used, SEYRIX_SETTINGS_TLV_ACTIVE_WORKSPACE, 1);
	add_u8(buffer, capacity, &used, SEYRIX_SETTINGS_TLV_WORKSPACE_COUNT, 4);
	add_u8(buffer, capacity, &used, SEYRIX_SETTINGS_TLV_SHELL_MODE, 0);
	add_u8(buffer, capacity, &used, SEYRIX_SETTINGS_TLV_SHELL_POSITION, 1);
	add_u8(buffer, capacity, &used, SEYRIX_SETTINGS_TLV_SHELL_ALIGNMENT, 1);
	add_u8(buffer, capacity, &used, SEYRIX_SETTINGS_TLV_SHELL_ICON_SIZE, 32);
	add_u8(buffer, capacity, &used, SEYRIX_SETTINGS_TLV_SHELL_AUTO_HIDE, 0);
	const uint8_t wallpaper[] = "";
	add_tlv(buffer, capacity, &used, SEYRIX_SETTINGS_TLV_SHELL_WALLPAPER,
		wallpaper, 0);
	const uint8_t color[] = "#101820";
	add_tlv(buffer, capacity, &used,
		SEYRIX_SETTINGS_TLV_SHELL_BACKGROUND_COLOR, color, 7);
	const uint8_t launcher[] = "Super_L";
	add_tlv(buffer, capacity, &used,
		SEYRIX_SETTINGS_TLV_LAUNCHER_SHORTCUT, launcher, 7);
	const uint8_t switcher[] = "Alt+Tab";
	add_tlv(buffer, capacity, &used,
		SEYRIX_SETTINGS_TLV_SWITCHER_SHORTCUT, switcher, 7);
	add_u8(buffer, capacity, &used,
		SEYRIX_SETTINGS_TLV_SWITCHER_ACTIVE_WORKSPACE_ONLY, 0);
	for (uint8_t index = 0; index <= 4; index++) {
		add_workspace(buffer, capacity, &used, index);
	}
	add_u16(buffer, capacity, &used,
		SEYRIX_SETTINGS_TLV_WM_APP_RULE_COUNT, 0);
	add_u16(buffer, capacity, &used,
		SEYRIX_SETTINGS_TLV_KEYBOARD_COUNT, 1);
	return used;
}

static bool receive_request(int fd, struct seyrix_settings_frame *frame) {
	struct pollfd poll_fd = {.fd = fd, .events = POLLIN | POLLHUP | POLLERR};
	int ready;
	do {
		ready = poll(&poll_fd, 1, 3000);
	} while (ready < 0 && errno == EINTR);
	if (ready <= 0 || (poll_fd.revents & POLLIN) == 0) {
		return false;
	}
	uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	ssize_t received = recv(fd, packet, sizeof(packet), MSG_TRUNC);
	return received > 0 && (size_t)received <= sizeof(packet) &&
		seyrix_settings_decode_frame(packet, (size_t)received, frame);
}

static bool send_response(int fd, const struct seyrix_settings_frame *request,
		uint16_t error, uint64_t revision,
		const uint8_t *body, size_t body_size) {
	uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	uint8_t payload[SEYRIX_SETTINGS_MAX_PACKET_SIZE -
		SEYRIX_SETTINGS_HEADER_SIZE];
	if (body_size + 4 > sizeof(payload)) {
		return false;
	}
	seyrix_settings_write_u16(payload, error);
	seyrix_settings_write_u16(payload + 2, request->opcode);
	if (body_size > 0) {
		memcpy(payload + 4, body, body_size);
	}
	uint32_t payload_size = (uint32_t)(body_size + 4);
	if (!seyrix_settings_encode_header(packet, sizeof(packet),
			SEYRIX_SETTINGS_OP_RESPONSE, request->request_id,
			payload_size, revision)) {
		return false;
	}
	memcpy(packet + SEYRIX_SETTINGS_HEADER_SIZE, payload, payload_size);
	return send(fd, packet, SEYRIX_SETTINGS_HEADER_SIZE + payload_size,
		MSG_NOSIGNAL) == (ssize_t)(SEYRIX_SETTINGS_HEADER_SIZE + payload_size);
}

static bool serve_client(int fd) {
	uint8_t snapshot[SEYRIX_SETTINGS_MAX_PACKET_SIZE -
		SEYRIX_SETTINGS_HEADER_SIZE];
	size_t snapshot_size = build_snapshot(snapshot, sizeof(snapshot));
	for (unsigned int request_index = 0; request_index < 3; request_index++) {
		struct seyrix_settings_frame request;
		if (!receive_request(fd, &request) || request.request_id == 0 ||
				request.opcode != (request_index == 0 ? SEYRIX_SETTINGS_OP_HELLO :
					request_index == 1 ? SEYRIX_SETTINGS_OP_GET_SNAPSHOT :
					SEYRIX_SETTINGS_OP_SUBSCRIBE)) {
			return false;
		}
		const uint8_t version[] = {1, 0};
		const uint8_t *body = request.opcode == SEYRIX_SETTINGS_OP_HELLO ?
			version : snapshot;
		size_t body_size = request.opcode == SEYRIX_SETTINGS_OP_HELLO ?
			sizeof(version) : snapshot_size;
		if (!send_response(fd, &request, SEYRIX_SETTINGS_ERROR_OK,
				1, body, body_size)) {
			return false;
		}
	}
	return true;
}

static bool wait_for_listener(struct client_fixture *fixture) {
	struct pollfd poll_fd = {
		.fd = fixture->listener_fd,
		.events = POLLIN | POLLHUP | POLLERR,
	};
	int ready;
	do {
		ready = poll(&poll_fd, 1, 200);
	} while (ready < 0 && errno == EINTR);
	return ready > 0 && (poll_fd.revents & POLLIN) != 0;
}

static void *fake_server(void *data) {
	struct client_fixture *fixture = data;
	for (int connection = 1; connection <= 2; connection++) {
		while (!g_atomic_int_get(&fixture->stop_server) &&
				!wait_for_listener(fixture)) {
		}
		if (g_atomic_int_get(&fixture->stop_server)) break;
		int fd = accept4(fixture->listener_fd, NULL, NULL, SOCK_CLOEXEC);
		if (fd < 0) {
			fixture->server_error = errno;
			break;
		}
		fixture->active_fd = fd;
		if (!serve_client(fd)) {
			fixture->server_error = EPROTO;
			close(fd);
			fixture->active_fd = -1;
			break;
		}
		g_atomic_int_set(&fixture->connections, connection);
		if (connection == 1) {
			while (!g_atomic_int_get(&fixture->close_first_connection) &&
					!g_atomic_int_get(&fixture->stop_server)) {
				g_usleep(10000);
			}
			close(fd);
			fixture->active_fd = -1;
			continue;
		}
		struct pollfd poll_fd = {.fd = fd, .events = POLLIN | POLLHUP | POLLERR};
		while (!g_atomic_int_get(&fixture->stop_server)) {
			int ready = poll(&poll_fd, 1, 200);
			if (ready > 0 && (poll_fd.revents & (POLLHUP | POLLERR)) != 0) break;
			if (ready > 0 && (poll_fd.revents & POLLIN) != 0) {
				uint8_t discard[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
				if (recv(fd, discard, sizeof(discard), MSG_DONTWAIT) <= 0) break;
			}
		}
		close(fd);
		fixture->active_fd = -1;
	}
	return NULL;
}

static void client_changed(void *data) {
	(void)data;
}

static gboolean observe_reconnect(gpointer data) {
	struct client_fixture *fixture = data;
	int connections = g_atomic_int_get(&fixture->connections);
	if (connections == 1 &&
			fixture->client.state == SEYRIX_SETTINGS_CLIENT_CONNECTED &&
			fixture->client.has_snapshot && fixture->client.subscribed) {
		fixture->connected_fd_count = open_fd_count();
		g_atomic_int_set(&fixture->close_first_connection, 1);
	}
	if (connections >= 1 &&
			fixture->client.state == SEYRIX_SETTINGS_CLIENT_OFFLINE) {
		fixture->saw_disconnect = true;
	}
	if (connections >= 2 && fixture->saw_disconnect &&
			fixture->client.state == SEYRIX_SETTINGS_CLIENT_CONNECTED &&
			fixture->client.has_snapshot && fixture->client.subscribed) {
		g_main_loop_quit(fixture->loop);
		fixture->timer_source = 0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean timeout_test(gpointer data) {
	struct client_fixture *fixture = data;
	fixture->timed_out = true;
	g_main_loop_quit(fixture->loop);
	fixture->timeout_source = 0;
	return G_SOURCE_REMOVE;
}

static void test_client_reconnects_and_keeps_snapshot(void) {
	struct client_fixture fixture = {.listener_fd = -1, .active_fd = -1};
	g_strlcpy(fixture.runtime_directory,
		"/tmp/seyrix-settings-client-XXXXXX",
		sizeof(fixture.runtime_directory));
	g_assert_nonnull(mkdtemp(fixture.runtime_directory));
	g_snprintf(fixture.private_directory, sizeof(fixture.private_directory),
		"%s/seyrix", fixture.runtime_directory);
	g_assert_cmpint(mkdir(fixture.private_directory, 0700), ==, 0);
	g_snprintf(fixture.socket_path, sizeof(fixture.socket_path),
		"%s/settings-v1.sock", fixture.private_directory);
	g_assert_cmpint(setenv("XDG_RUNTIME_DIR", fixture.runtime_directory, 1), ==, 0);
	fixture.listener_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	g_assert_cmpint(fixture.listener_fd, >=, 0);
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	g_strlcpy(address.sun_path, fixture.socket_path, sizeof(address.sun_path));
	g_assert_cmpint(bind(fixture.listener_fd, (struct sockaddr *)&address,
		sizeof(address)), ==, 0);
	g_assert_cmpint(chmod(fixture.socket_path, 0600), ==, 0);
	g_assert_cmpint(listen(fixture.listener_fd, 4), ==, 0);
	g_assert_cmpint(pthread_create(&fixture.server_thread, NULL,
		fake_server, &fixture), ==, 0);
	seyrix_settings_client_init(&fixture.client, client_changed, &fixture);
	fixture.loop = g_main_loop_new(NULL, FALSE);
	fixture.timer_source = g_timeout_add(10, observe_reconnect, &fixture);
	fixture.timeout_source = g_timeout_add_seconds(12, timeout_test, &fixture);
	seyrix_settings_client_start(&fixture.client);
	g_main_loop_run(fixture.loop);
	if (fixture.timer_source != 0) {
		g_source_remove(fixture.timer_source);
		fixture.timer_source = 0;
	}
	if (fixture.timeout_source != 0) {
		g_source_remove(fixture.timeout_source);
		fixture.timeout_source = 0;
	}
	g_assert_false(fixture.timed_out);
	g_assert_true(fixture.saw_disconnect);
	g_assert_cmpint(g_atomic_int_get(&fixture.connections), ==, 2);
	g_assert_cmpuint(fixture.client.connection_attempts, ==, 2);
	g_assert_cmpint(fixture.client.state, ==,
		SEYRIX_SETTINGS_CLIENT_CONNECTED);
	g_assert_true(fixture.client.has_snapshot);
	g_assert_true(fixture.client.subscribed);
	g_assert_cmpuint(fixture.client.snapshot.workspace_count, ==, 4);
	g_assert_cmpuint(open_fd_count(), ==, fixture.connected_fd_count);
	g_assert_cmpstr(fixture.client.status, ==,
		"Connected to Seyrix compositor; read-only mode");
	int client_fd = fixture.client.fd;
	g_assert_cmpint(client_fd, >=, 0);
	seyrix_settings_client_finish(&fixture.client);
	g_assert_cmpint(fixture.client.fd, ==, -1);
	g_assert_cmpuint(fixture.client.io_source, ==, 0);
	g_assert_cmpuint(fixture.client.retry_source, ==, 0);
	errno = 0;
	int closed_fd_result = fcntl(client_fd, F_GETFD);
	int closed_fd_errno = errno;
	g_assert_cmpint(closed_fd_result, ==, -1);
	g_assert_cmpint(closed_fd_errno, ==, EBADF);
	g_assert_cmpuint(open_fd_count(), <, fixture.connected_fd_count);
	g_atomic_int_set(&fixture.stop_server, 1);
	shutdown(fixture.listener_fd, SHUT_RDWR);
	close(fixture.listener_fd);
	pthread_join(fixture.server_thread, NULL);
	g_assert_cmpint(fixture.server_error, ==, 0);
	g_assert_cmpuint(open_fd_count(), <, fixture.connected_fd_count);
	g_main_loop_unref(fixture.loop);
	unlink(fixture.socket_path);
	rmdir(fixture.private_directory);
	rmdir(fixture.runtime_directory);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/settings-client/reconnects-after-disconnect",
		test_client_reconnects_and_keeps_snapshot);
	return g_test_run();
}
