#ifndef SEYRIX_SHELL_IPC_H
#define SEYRIX_SHELL_IPC_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

/* Private SOCK_SEQPACKET channel inherited by the compositor-owned shell.
 * Every packet has an exact size and a version; no strings are commands. */
#define SEYRIX_SHELL_IPC_VERSION 1
#define SEYRIX_SHELL_IPC_MAX_WINDOWS 128
#define SEYRIX_SHELL_APP_ID_MAX 128
#define SEYRIX_SHELL_TITLE_MAX 256

enum seyrix_shell_message_type {
	SEYRIX_SHELL_ACTIVE_WORKSPACE = 1,
	SEYRIX_SHELL_SELECT_WORKSPACE = 2,
	SEYRIX_SHELL_WINDOW_UPSERT = 3,
	SEYRIX_SHELL_WINDOW_REMOVE = 4,
	SEYRIX_SHELL_REQUEST_FOCUS = 5,
	SEYRIX_SHELL_SHOW_LAUNCHER = 6,
	SEYRIX_SHELL_SHOW_SWITCHER = 7,
	SEYRIX_SHELL_SHOW_MANUAL = 8,
	SEYRIX_SHELL_CLOSE_OVERLAY = 9,
	SEYRIX_SHELL_ACTIVATE_WINDOW = 10,
	SEYRIX_SHELL_REQUEST_MANUAL = 11,
	SEYRIX_SHELL_REQUEST_SNAPSHOT = 12,
	SEYRIX_SHELL_DISMISS_OVERLAY = 13,
	SEYRIX_SHELL_SNAPSHOT_BEGIN = 14,
	SEYRIX_SHELL_REQUEST_LAUNCHER = 15,
	SEYRIX_SHELL_COMMIT_SWITCHER = 16,
};

enum seyrix_shell_window_flags {
	SEYRIX_SHELL_WINDOW_MAPPED = 1U << 0,
	SEYRIX_SHELL_WINDOW_MINIMIZED = 1U << 1,
	SEYRIX_SHELL_WINDOW_MAXIMIZED = 1U << 2,
	SEYRIX_SHELL_WINDOW_FULLSCREEN = 1U << 3,
	SEYRIX_SHELL_WINDOW_FOCUSED = 1U << 4,
};

#define SEYRIX_SHELL_WINDOW_KNOWN_FLAGS \
	(SEYRIX_SHELL_WINDOW_MAPPED | SEYRIX_SHELL_WINDOW_MINIMIZED | \
	 SEYRIX_SHELL_WINDOW_MAXIMIZED | SEYRIX_SHELL_WINDOW_FULLSCREEN | \
	 SEYRIX_SHELL_WINDOW_FOCUSED)

struct seyrix_shell_packet_header {
	uint8_t version;
	uint8_t type;
	uint16_t size;
};

struct seyrix_shell_packet_workspace {
	struct seyrix_shell_packet_header header;
	uint8_t workspace;
	uint8_t reserved[3];
};

struct seyrix_shell_packet_window {
	struct seyrix_shell_packet_header header;
	uint32_t id;
	uint8_t workspace;
	uint8_t flags;
	uint8_t reserved[2];
	char app_id[SEYRIX_SHELL_APP_ID_MAX];
	char title[SEYRIX_SHELL_TITLE_MAX];
};

struct seyrix_shell_packet_id {
	struct seyrix_shell_packet_header header;
	uint32_t id;
};

struct seyrix_shell_packet_action {
	struct seyrix_shell_packet_header header;
	uint8_t action;
	uint8_t reserved[3];
};

struct seyrix_shell_request {
	uint8_t type;
	uint8_t workspace;
	uint32_t window_id;
};

enum seyrix_shell_ipc_send_result {
	SEYRIX_SHELL_IPC_SENT,
	SEYRIX_SHELL_IPC_WOULD_BLOCK,
	SEYRIX_SHELL_IPC_SEND_ERROR,
};

static inline bool seyrix_shell_ipc_header_valid(
		const struct seyrix_shell_packet_header *header, size_t received,
		size_t expected) {
	return header != NULL && received == expected &&
		header->version == SEYRIX_SHELL_IPC_VERSION &&
		header->size == expected;
}

static inline bool seyrix_shell_ipc_decode_request(const void *data,
		size_t received, uint8_t workspace_count,
		struct seyrix_shell_request *request) {
	if (data == NULL || request == NULL ||
			received < sizeof(struct seyrix_shell_packet_header)) {
		return false;
	}
	struct seyrix_shell_packet_header header;
	memcpy(&header, data, sizeof(header));
	memset(request, 0, sizeof(*request));
	request->type = header.type;

	size_t expected = 0;
	switch (header.type) {
	case SEYRIX_SHELL_SELECT_WORKSPACE: {
		expected = sizeof(struct seyrix_shell_packet_workspace);
		if (received != expected) return false;
		struct seyrix_shell_packet_workspace packet;
		memcpy(&packet, data, sizeof(packet));
		if (packet.workspace >= workspace_count || packet.reserved[0] != 0 ||
				packet.reserved[1] != 0 || packet.reserved[2] != 0) return false;
		request->workspace = packet.workspace;
		break;
	}
	case SEYRIX_SHELL_REQUEST_FOCUS:
	case SEYRIX_SHELL_ACTIVATE_WINDOW: {
		expected = sizeof(struct seyrix_shell_packet_id);
		if (received != expected) return false;
		struct seyrix_shell_packet_id packet;
		memcpy(&packet, data, sizeof(packet));
		if (packet.id == 0) return false;
		request->window_id = packet.id;
		break;
	}
	case SEYRIX_SHELL_REQUEST_MANUAL:
	case SEYRIX_SHELL_REQUEST_LAUNCHER:
	case SEYRIX_SHELL_REQUEST_SNAPSHOT:
	case SEYRIX_SHELL_CLOSE_OVERLAY:
	case SEYRIX_SHELL_DISMISS_OVERLAY: {
		expected = sizeof(struct seyrix_shell_packet_action);
		if (received != expected) return false;
		struct seyrix_shell_packet_action packet;
		memcpy(&packet, data, sizeof(packet));
		if (packet.action != 0 || packet.reserved[0] != 0 ||
				packet.reserved[1] != 0 || packet.reserved[2] != 0) return false;
		break;
	}
	default:
		return false;
	}

	return seyrix_shell_ipc_header_valid(&header, received, expected);
}

static inline enum seyrix_shell_ipc_send_result seyrix_shell_ipc_send_packet(
		int fd, const void *packet, size_t size, int *error_out) {
	ssize_t sent = send(fd, packet, size, MSG_DONTWAIT | MSG_NOSIGNAL);
	if (sent == (ssize_t)size) {
		if (error_out != NULL) *error_out = 0;
		return SEYRIX_SHELL_IPC_SENT;
	}
	int saved_errno = sent < 0 ? errno : EIO;
	if (error_out != NULL) *error_out = saved_errno;
	if (sent < 0 && (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)) {
		return SEYRIX_SHELL_IPC_WOULD_BLOCK;
	}
	return SEYRIX_SHELL_IPC_SEND_ERROR;
}

#endif
