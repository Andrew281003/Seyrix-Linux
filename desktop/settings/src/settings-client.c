#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "settings-client.h"

#include "../../common/settings-ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <glib-unix.h>

enum { CLIENT_MAX_MESSAGES_PER_DISPATCH = 32 };

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
		return false;
	}
	uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	uint32_t request_id = client_allocate_request_id(client);
	if (request_id == 0 || !seyrix_settings_encode_header(packet,
			sizeof(packet), opcode, request_id, (uint32_t)payload_size,
			revision) || !client_pending_add(client, request_id, opcode)) {
		return false;
	}
	if (payload_size > 0) {
		memcpy(packet + SEYRIX_SETTINGS_HEADER_SIZE, payload, payload_size);
	}
	ssize_t sent = send(client->fd, packet,
		SEYRIX_SETTINGS_HEADER_SIZE + payload_size,
		MSG_DONTWAIT | MSG_NOSIGNAL);
	if (sent < 0 || (size_t)sent != SEYRIX_SETTINGS_HEADER_SIZE + payload_size) {
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
	if (client->state == SEYRIX_SETTINGS_CLIENT_CONNECTING) {
		if ((condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) != 0) {
			int socket_error = 0;
		socklen_t size = sizeof(socket_error);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &size) != 0 ||
				socket_error != 0) {
				client->io_source = 0;
				client_disconnect(client, "Seyrix compositor is unavailable");
				return G_SOURCE_REMOVE;
			}
		}
		if ((condition & G_IO_OUT) != 0) {
			int socket_error = 0;
			socklen_t size = sizeof(socket_error);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &size) != 0 ||
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
		if (received == 0) {
			client->io_source = 0;
			client_disconnect(client,
				"Compositor disconnected; showing last-known state");
			return G_SOURCE_REMOVE;
		}
		if (received < 0) {
			if (errno == EINTR) {
				handled--;
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
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
		if (has_control || (message.msg_flags & MSG_TRUNC) != 0 ||
				(size_t)received > sizeof(packet) ||
				!seyrix_settings_decode_frame(packet, (size_t)received, &frame) ||
			frame.frame_version != SEYRIX_SETTINGS_FRAME_VERSION ||
			frame.flags != 0) {
			client->io_source = 0;
			client_disconnect(client, "Compositor sent an invalid protocol message");
			return G_SOURCE_REMOVE;
		}
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
	const char *runtime = g_getenv("XDG_RUNTIME_DIR");
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	if (runtime == NULL || runtime[0] != '/' ||
		g_snprintf(address.sun_path, sizeof(address.sun_path),
			"%s/seyrix/settings-v1.sock", runtime) >=
			(gint)sizeof(address.sun_path)) {
		client_set_status(client, "Seyrix runtime directory is unavailable");
		client_schedule_retry_safe(client);
		return G_SOURCE_REMOVE;
	}
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0) {
		client_set_status(client, "Could not open the Seyrix settings endpoint");
		client_schedule_retry_safe(client);
		return G_SOURCE_REMOVE;
	}
	int result = connect(fd, (struct sockaddr *)&address, sizeof(address));
	if (result != 0 && errno != EINPROGRESS && errno != EAGAIN) {
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
		client_schedule_retry_safe(client);
	}
}

void seyrix_settings_client_finish(struct seyrix_settings_client *client) {
	if (client == NULL) {
		return;
	}
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
