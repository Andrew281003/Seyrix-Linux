#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "settings-client.h"

#include "../../common/settings-ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <glib-unix.h>

enum { CLIENT_MAX_MESSAGES_PER_DISPATCH = 32 };

static void client_trace(const struct seyrix_settings_client *client,
		const char *event, const char *format, ...) {
	if (g_strcmp0(g_getenv("SEYRIX_SETTINGS_IPC_TRACE"), "1") != 0) {
		return;
	}
	struct timespec now = {0};
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	uint64_t monotonic_ns = (uint64_t)now.tv_sec * 1000000000U +
		(uint64_t)now.tv_nsec;
	fprintf(stderr,
		"SETTINGS_IPC_CLIENT mono_ns=%llu pid=%ld attempt=%u fd=%d event=%s",
		(unsigned long long)monotonic_ns, (long)getpid(),
		client != NULL ? client->connection_attempts : 0,
		client != NULL ? client->fd : -1, event);
	if (format != NULL) {
		va_list args;
		va_start(args, format);
		vfprintf(stderr, format, args);
		va_end(args);
	}
	fputc('\n', stderr);
	fflush(stderr);
}

static void client_trace_connection(struct seyrix_settings_client *client) {
	if (g_strcmp0(g_getenv("SEYRIX_SETTINGS_IPC_TRACE"), "1") != 0) {
		return;
	}
	struct stat status = {0};
	struct ucred peer = {0};
	socklen_t peer_size = sizeof(peer);
	int stat_result = fstat(client->fd, &status);
	int stat_errno = stat_result == 0 ? 0 : errno;
	int peer_result = getsockopt(client->fd, SOL_SOCKET, SO_PEERCRED,
		&peer, &peer_size);
	int peer_errno = peer_result == 0 ? 0 : errno;
	client_trace(client, "connected",
		" inode=%llu stat_errno=%d server_pid=%ld server_uid=%ld peer_errno=%d",
		(unsigned long long)(stat_result == 0 ? status.st_ino : 0),
		stat_errno, (long)(peer_result == 0 ? peer.pid : -1),
		(long)(peer_result == 0 ? peer.uid : (uid_t)-1), peer_errno);
}

static void client_notify(struct seyrix_settings_client *client) {
	if (client->changed != NULL) {
		client->changed(client->changed_data);
	}
}

static void client_set_status(struct seyrix_settings_client *client,
		const char *status) {
	g_strlcpy(client->status, status, sizeof(client->status));
	client_notify(client);
}

static gboolean client_io(gint fd, GIOCondition condition, gpointer data);
static gboolean client_retry(gpointer data);
static bool client_add_real_io_source(struct seyrix_settings_client *client,
	GIOCondition condition);
static void client_schedule_retry_safe(struct seyrix_settings_client *client);
static void client_disconnect(struct seyrix_settings_client *client,
	const char *reason);

static bool client_pending_add(struct seyrix_settings_client *client,
		uint32_t request_id, uint16_t opcode) {
	if (client->pending_count >= G_N_ELEMENTS(client->pending)) {
		return false;
	}
	client->pending[client->pending_count].request_id = request_id;
	client->pending[client->pending_count].opcode = opcode;
	client->pending_count++;
	return true;
}

static bool client_pending_take(struct seyrix_settings_client *client,
		uint32_t request_id, uint16_t *opcode) {
	for (unsigned int i = 0; i < client->pending_count; i++) {
		if (client->pending[i].request_id == request_id) {
			*opcode = client->pending[i].opcode;
			client->pending[i] = client->pending[--client->pending_count];
			return true;
		}
	}
	return false;
}

static uint32_t client_allocate_request_id(
		struct seyrix_settings_client *client) {
	for (unsigned int attempt = 0; attempt <= G_N_ELEMENTS(client->pending);
			attempt++) {
		uint32_t candidate = ++client->next_request_id;
		if (candidate == 0) {
			candidate = ++client->next_request_id;
		}
		bool in_use = false;
		for (unsigned int i = 0; i < client->pending_count; i++) {
			if (client->pending[i].request_id == candidate) {
				in_use = true;
				break;
			}
		}
		if (!in_use) {
			return candidate;
		}
	}
	return 0;
}

static bool client_send_request(struct seyrix_settings_client *client,
		uint16_t opcode, const uint8_t *payload, size_t payload_size,
		uint64_t revision) {
	if (client->fd < 0 || payload_size > SEYRIX_SETTINGS_MAX_PACKET_SIZE -
			SEYRIX_SETTINGS_HEADER_SIZE) {
		client_trace(client, "serialize-failed", " opcode=%u reason=arguments",
			opcode);
		return false;
	}
	uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	uint32_t request_id = client_allocate_request_id(client);
	if (request_id == 0 || !seyrix_settings_encode_header(packet,
			sizeof(packet), opcode, request_id, (uint32_t)payload_size,
			revision) || !client_pending_add(client, request_id, opcode)) {
		client_trace(client, "serialize-failed", " opcode=%u request=%u",
			opcode, request_id);
		return false;
	}
	if (payload_size > 0) {
		memcpy(packet + SEYRIX_SETTINGS_HEADER_SIZE, payload, payload_size);
	}
	size_t packet_size = SEYRIX_SETTINGS_HEADER_SIZE + payload_size;
	if (g_strcmp0(g_getenv("SEYRIX_SETTINGS_IPC_TRACE"), "1") == 0) {
		char wire_hex[65] = {0};
		for (size_t i = 0; i < packet_size && i < (sizeof(wire_hex) - 1) / 2;
				i++) {
			(void)snprintf(wire_hex + i * 2, sizeof(wire_hex) - i * 2,
				"%02x", packet[i]);
		}
		client_trace(client, "serialized",
			" opcode=%u request=%u revision=%llu payload=%zu bytes=%zu wire_hex=%s",
			opcode, request_id, (unsigned long long)revision, payload_size,
			packet_size, wire_hex);
	}
	client_trace(client, "write-attempt", " opcode=%u request=%u bytes=%zu",
		opcode, request_id, packet_size);
	ssize_t sent = send(client->fd, packet, packet_size,
		MSG_DONTWAIT | MSG_NOSIGNAL);
	int send_errno = sent < 0 ? errno : 0;
	client_trace(client, "write-result",
		" opcode=%u request=%u sent=%ld expected=%zu errno=%d",
		opcode, request_id, (long)sent, packet_size, send_errno);
	if (sent < 0 || (size_t)sent != packet_size) {
		uint16_t ignored;
		(void)client_pending_take(client, request_id, &ignored);
		return false;
	}
	return true;
}

static void client_request_snapshot(struct seyrix_settings_client *client) {
	if (!client->negotiated) {
		return;
	}
	for (unsigned int i = 0; i < client->pending_count; i++) {
		if (client->pending[i].opcode == SEYRIX_SETTINGS_OP_GET_SNAPSHOT ||
				client->pending[i].opcode == SEYRIX_SETTINGS_OP_SUBSCRIBE) {
			client->refresh_pending = true;
			return;
		}
	}
	client->snapshot_pending = true;
	if (!client_send_request(client, SEYRIX_SETTINGS_OP_GET_SNAPSHOT,
			NULL, 0, client->revision)) {
		client_disconnect(client, "Could not request compositor state");
	}
}

static void client_send_hello(struct seyrix_settings_client *client) {
	static const uint8_t versions[4] = {1, 1, 0, 0};
	client_trace(client, "hello-begin", " versions=1.1.0.0");
	client->state = SEYRIX_SETTINGS_CLIENT_NEGOTIATING;
	client_set_status(client, "Connected; negotiating the read-only protocol…");
	if (!client_send_request(client, SEYRIX_SETTINGS_OP_HELLO,
			versions, sizeof(versions), 0)) {
		client_disconnect(client, "Could not negotiate the compositor protocol");
	}
}

static void client_drop_source(struct seyrix_settings_client *client) {
	if (client->io_source != 0) {
		g_source_remove(client->io_source);
		client->io_source = 0;
	}
}

static bool client_add_real_io_source(struct seyrix_settings_client *client,
		GIOCondition condition) {
	client_drop_source(client);
	client->io_source = g_unix_fd_add_full(G_PRIORITY_DEFAULT, client->fd,
		condition, client_io, client, NULL);
	client_trace(client, "io-source",
		" result=%s source=%u condition=0x%x", client->io_source != 0 ?
		"installed" : "failed", client->io_source, condition);
	return client->io_source != 0;
}

static void client_schedule_retry_safe(struct seyrix_settings_client *client) {
	if (client->retry_source != 0) {
		return;
	}
	client->state = SEYRIX_SETTINGS_CLIENT_OFFLINE;
	unsigned int delay = client->retry_delay_ms != 0 ?
		client->retry_delay_ms : 250;
	client->retry_delay_ms = delay < 8000 ? delay * 2 : 8000;
	client->retry_source = g_timeout_add(delay, client_retry, client);
}

static void client_disconnect(struct seyrix_settings_client *client,
		const char *reason) {
	client_trace(client, "disconnect", " reason=%s",
		reason != NULL ? reason : "unspecified");
	client_drop_source(client);
	if (client->fd >= 0) {
		close(client->fd);
		client->fd = -1;
	}
	client->negotiated = false;
	client->subscribed = false;
	client->snapshot_pending = false;
	client->refresh_pending = false;
	client->pending_count = 0;
	client->state = SEYRIX_SETTINGS_CLIENT_OFFLINE;
	client_schedule_retry_safe(client);
	client_set_status(client, reason != NULL ? reason :
		"Compositor disconnected; showing last-known state");
}

static bool client_begin_connected(struct seyrix_settings_client *client) {
	client->retry_delay_ms = 250;
	client->state = SEYRIX_SETTINGS_CLIENT_NEGOTIATING;
	client_trace_connection(client);
	if (!client_add_real_io_source(client, G_IO_IN | G_IO_HUP | G_IO_ERR)) {
		return false;
	}
	client_send_hello(client);
	return client->fd >= 0;
}

static gboolean client_io(gint fd, GIOCondition condition, gpointer data) {
	struct seyrix_settings_client *client = data;
	if (client == NULL || client->fd != fd) {
		return G_SOURCE_REMOVE;
	}
	client_trace(client, "io-dispatch", " condition=0x%x state=%d",
		condition, client->state);
	if (client->state == SEYRIX_SETTINGS_CLIENT_CONNECTING) {
		if ((condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) != 0) {
			int socket_error = 0;
		socklen_t size = sizeof(socket_error);
			int error_result = getsockopt(fd, SOL_SOCKET, SO_ERROR,
				&socket_error, &size);
			int socket_errno = error_result == 0 ? 0 : errno;
			client_trace(client, "connect-ready",
				" result=%d socket_error=%d errno=%d", error_result,
				socket_error, socket_errno);
			if (error_result != 0 ||
				socket_error != 0) {
				client->io_source = 0;
				client_disconnect(client, "Seyrix compositor is unavailable");
				return G_SOURCE_REMOVE;
			}
		}
		if ((condition & G_IO_OUT) != 0) {
			int socket_error = 0;
			socklen_t size = sizeof(socket_error);
			int error_result = getsockopt(fd, SOL_SOCKET, SO_ERROR,
				&socket_error, &size);
			int socket_errno = error_result == 0 ? 0 : errno;
			client_trace(client, "connect-ready",
				" result=%d socket_error=%d errno=%d", error_result,
				socket_error, socket_errno);
			if (error_result != 0 ||
				socket_error != 0) {
				client->io_source = 0;
				client_disconnect(client, "Seyrix compositor is unavailable");
				return G_SOURCE_REMOVE;
			}
			client->io_source = 0;
			if (!client_begin_connected(client)) {
				client_disconnect(client, "Could not start compositor communication");
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}
	if ((condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) != 0) {
		client->io_source = 0;
		client_disconnect(client, "Compositor disconnected; showing last-known state");
		return G_SOURCE_REMOVE;
	}
	for (unsigned int handled = 0;
			handled < CLIENT_MAX_MESSAGES_PER_DISPATCH; handled++) {
		uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
		char control[CMSG_SPACE(sizeof(int) * 4)];
		struct iovec iov = {.iov_base = packet, .iov_len = sizeof(packet)};
		struct msghdr message = {
			.msg_iov = &iov,
			.msg_iovlen = 1,
			.msg_control = control,
			.msg_controllen = sizeof(control),
		};
		ssize_t received = recvmsg(fd, &message, MSG_DONTWAIT | MSG_TRUNC |
			MSG_CMSG_CLOEXEC);
		int receive_errno = received < 0 ? errno : 0;
		client_trace(client, "read-result",
			" received=%ld errno=%d flags=0x%x control_len=%zu",
			(long)received, receive_errno, message.msg_flags,
			(size_t)message.msg_controllen);
		if (received == 0) {
			client->io_source = 0;
			client_disconnect(client,
				"Compositor disconnected; showing last-known state");
			return G_SOURCE_REMOVE;
		}
		if (received < 0) {
			if (receive_errno == EINTR) {
				handled--;
				continue;
			}
			if (receive_errno == EAGAIN || receive_errno == EWOULDBLOCK) {
				return G_SOURCE_CONTINUE;
			}
			client->io_source = 0;
			client_disconnect(client, "Settings connection failed");
			return G_SOURCE_REMOVE;
		}
		bool has_control = CMSG_FIRSTHDR(&message) != NULL;
		for (struct cmsghdr *header = CMSG_FIRSTHDR(&message);
				header != NULL; header = CMSG_NXTHDR(&message, header)) {
			if (header->cmsg_level == SOL_SOCKET &&
					header->cmsg_type == SCM_RIGHTS &&
				header->cmsg_len >= CMSG_LEN(sizeof(int))) {
				size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
				int *descriptors = (int *)CMSG_DATA(header);
				for (size_t i = 0; i < count; i++) close(descriptors[i]);
			}
		}
		struct seyrix_settings_frame frame;
		bool decoded = !has_control &&
			(message.msg_flags & MSG_TRUNC) == 0 &&
			(size_t)received <= sizeof(packet) &&
			seyrix_settings_decode_frame(packet, (size_t)received, &frame) &&
			frame.frame_version == SEYRIX_SETTINGS_FRAME_VERSION &&
			frame.flags == 0;
		if (!decoded) {
			client_trace(client, "parse-rejected",
				" received=%ld control=%d flags=0x%x", (long)received,
				has_control, message.msg_flags);
		}
		if (has_control || (message.msg_flags & MSG_TRUNC) != 0 ||
				(size_t)received > sizeof(packet) ||
			!decoded) {
			client->io_source = 0;
			client_disconnect(client, "Compositor sent an invalid protocol message");
			return G_SOURCE_REMOVE;
		}
		client_trace(client, "frame-decoded",
			" opcode=%u request=%u payload=%u revision=%llu",
			frame.opcode, frame.request_id, frame.payload_size,
			(unsigned long long)frame.revision);
		if (frame.opcode == SEYRIX_SETTINGS_OP_CHANGED) {
			if (frame.request_id != 0 || frame.payload_size != 0) {
				client->io_source = 0;
				client_disconnect(client, "Compositor sent an invalid state event");
				return G_SOURCE_REMOVE;
			}
			if (frame.revision > client->revision) {
				client_request_snapshot(client);
			}
			continue;
		}
		if (frame.opcode != SEYRIX_SETTINGS_OP_RESPONSE ||
				frame.request_id == 0 || frame.payload_size < 4) {
			client->io_source = 0;
			client_disconnect(client, "Compositor sent an unexpected response");
			return G_SOURCE_REMOVE;
		}
		uint16_t request_opcode = 0;
		if (!client_pending_take(client, frame.request_id, &request_opcode) ||
				seyrix_settings_read_u16(frame.payload + 2) != request_opcode) {
			client->io_source = 0;
			client_disconnect(client, "Compositor response did not match a request");
			return G_SOURCE_REMOVE;
		}
		enum seyrix_settings_error error =
			(enum seyrix_settings_error)seyrix_settings_read_u16(frame.payload);
		if (error > SEYRIX_SETTINGS_ERROR_INTERNAL) {
			client->io_source = 0;
			client_disconnect(client, "Compositor returned an unknown protocol error");
			return G_SOURCE_REMOVE;
		}
		const uint8_t *body = frame.payload + 4;
		size_t body_size = frame.payload_size - 4;
		if (request_opcode == SEYRIX_SETTINGS_OP_HELLO) {
			if (error != SEYRIX_SETTINGS_ERROR_OK || body_size != 2 ||
					body[0] != 1 || body[1] != 0) {
				client->io_source = 0;
				client_disconnect(client, "Unsupported Seyrix Settings protocol");
				return G_SOURCE_REMOVE;
			}
			client->negotiated = true;
			client_trace(client, "hello-response",
				" request=%u error=%u negotiated=1",
				frame.request_id, error);
			client->retry_delay_ms = 250;
			if (!client_send_request(client,
					SEYRIX_SETTINGS_OP_GET_SNAPSHOT, NULL, 0,
					client->revision)) {
				client->io_source = 0;
				client_disconnect(client, "Could not request compositor state");
				return G_SOURCE_REMOVE;
			}
			continue;
		}
		if (request_opcode == SEYRIX_SETTINGS_OP_GET_SNAPSHOT ||
				request_opcode == SEYRIX_SETTINGS_OP_SUBSCRIBE) {
			if (request_opcode == SEYRIX_SETTINGS_OP_SUBSCRIBE &&
					error == SEYRIX_SETTINGS_ERROR_TOO_MANY_CLIENTS) {
				client->subscribed = false;
				client->snapshot_pending = false;
				client->state = SEYRIX_SETTINGS_CLIENT_CONNECTED;
				client_set_status(client,
					"Connected; live updates unavailable (subscriber limit reached)");
				if (client->refresh_pending) {
					client->refresh_pending = false;
					client_request_snapshot(client);
				}
				continue;
			}
			if ((error != SEYRIX_SETTINGS_ERROR_OK &&
					error != SEYRIX_SETTINGS_ERROR_STALE_REVISION) ||
				!seyrix_settings_snapshot_parse(body, body_size,
					frame.revision, &client->snapshot)) {
				client->io_source = 0;
				client_disconnect(client, "Could not read compositor settings snapshot");
				return G_SOURCE_REMOVE;
			}
			client->revision = frame.revision;
			client->has_snapshot = true;
			client->snapshot_pending = false;
			if (request_opcode == SEYRIX_SETTINGS_OP_SUBSCRIBE &&
					(error == SEYRIX_SETTINGS_ERROR_OK ||
					 error == SEYRIX_SETTINGS_ERROR_STALE_REVISION)) {
				client->subscribed = true;
			}
			client_trace(client, "state-response",
				" request=%u response_to=%u error=%u revision=%llu snapshot=1 subscribed=%d",
				frame.request_id, request_opcode, error,
				(unsigned long long)frame.revision, client->subscribed);
			client->state = SEYRIX_SETTINGS_CLIENT_CONNECTED;
			client_set_status(client, error == SEYRIX_SETTINGS_ERROR_STALE_REVISION ?
				"Connected; refreshed a newer read-only snapshot" :
				"Connected to Seyrix compositor; read-only mode");
			if (request_opcode == SEYRIX_SETTINGS_OP_GET_SNAPSHOT &&
					!client->subscribed) {
				if (!client_send_request(client,
						SEYRIX_SETTINGS_OP_SUBSCRIBE, NULL, 0,
					client->revision)) {
					client->io_source = 0;
					client_disconnect(client, "Could not subscribe to state updates");
					return G_SOURCE_REMOVE;
				}
			}
			if (client->refresh_pending) {
				client->refresh_pending = false;
				client_request_snapshot(client);
			}
			continue;
		}
		if (request_opcode == SEYRIX_SETTINGS_OP_UNSUBSCRIBE &&
				error == SEYRIX_SETTINGS_ERROR_OK) {
			client->subscribed = false;
			continue;
		}
		client->io_source = 0;
		client_disconnect(client, "Compositor rejected an unexpected request");
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean client_retry(gpointer data) {
	struct seyrix_settings_client *client = data;
	client->retry_source = 0;
	if (client == NULL || client->fd >= 0) {
		return G_SOURCE_REMOVE;
	}
	client->connection_attempts++;
	client_trace(client, "connect-attempt", NULL);
	const char *runtime = g_getenv("XDG_RUNTIME_DIR");
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	if (runtime == NULL || runtime[0] != '/' ||
		g_snprintf(address.sun_path, sizeof(address.sun_path),
			"%s/seyrix/settings-v1.sock", runtime) >=
			(gint)sizeof(address.sun_path)) {
		client_trace(client, "connect-rejected", " reason=runtime-path");
		client_set_status(client, "Seyrix runtime directory is unavailable");
		client_schedule_retry_safe(client);
		return G_SOURCE_REMOVE;
	}
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0) {
		int socket_errno = errno;
		client_trace(client, "socket-failed", " errno=%d", socket_errno);
		client_set_status(client, "Could not open the Seyrix settings endpoint");
		client_schedule_retry_safe(client);
		return G_SOURCE_REMOVE;
	}
	int result = connect(fd, (struct sockaddr *)&address, sizeof(address));
	int connect_errno = result == 0 ? 0 : errno;
	client_trace(client, "connect-result",
		" result=%d errno=%d path=%s", result, connect_errno,
		address.sun_path);
	if (result != 0 && connect_errno != EINPROGRESS &&
			connect_errno != EAGAIN) {
		close(fd);
		client_set_status(client, "Seyrix compositor is not available");
		client_schedule_retry_safe(client);
		return G_SOURCE_REMOVE;
	}
	client->fd = fd;
	client->state = result == 0 ? SEYRIX_SETTINGS_CLIENT_NEGOTIATING :
		SEYRIX_SETTINGS_CLIENT_CONNECTING;
	if (result == 0) {
		if (!client_begin_connected(client)) {
			client_disconnect(client, "Could not start compositor communication");
		}
	} else if (!client_add_real_io_source(client,
			G_IO_OUT | G_IO_HUP | G_IO_ERR)) {
		client_disconnect(client, "Could not watch the compositor connection");
	}
	return G_SOURCE_REMOVE;
}

void seyrix_settings_client_init(struct seyrix_settings_client *client,
		void (*changed)(void *data), void *changed_data) {
	memset(client, 0, sizeof(*client));
	client->fd = -1;
	client->retry_delay_ms = 250;
	client->state = SEYRIX_SETTINGS_CLIENT_OFFLINE;
	client->changed = changed;
	client->changed_data = changed_data;
	g_strlcpy(client->status, "Connecting to Seyrix compositor…",
		sizeof(client->status));
}

void seyrix_settings_client_start(struct seyrix_settings_client *client) {
	if (client != NULL && client->retry_source == 0 && client->fd < 0) {
		client_trace(client, "start", NULL);
		client_schedule_retry_safe(client);
	}
}

void seyrix_settings_client_finish(struct seyrix_settings_client *client) {
	if (client == NULL) {
		return;
	}
	client_trace(client, "finish", NULL);
	if (client->retry_source != 0) {
		g_source_remove(client->retry_source);
		client->retry_source = 0;
	}
	client_drop_source(client);
	if (client->fd >= 0) {
		close(client->fd);
		client->fd = -1;
	}
	client->pending_count = 0;
}
