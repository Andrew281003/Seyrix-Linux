#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "settings-control.h"

#include "seyrix.h"
#include "../../common/settings-ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <wlr/util/log.h>

enum {
	SETTINGS_MAX_CLIENTS = 16,
	SETTINGS_MAX_SUBSCRIBERS = 8,
	SETTINGS_MAX_MESSAGES_PER_DISPATCH = 32,
	SETTINGS_MAX_OUTPUTS = SEYRIX_SETTINGS_MAX_OUTPUTS,
};

struct seyrix_settings_control;

struct seyrix_settings_client {
	struct wl_list link;
	struct seyrix_settings_control *control;
	struct wl_event_source *source;
	int fd;
	uid_t uid;
	pid_t pid;
	uint64_t connection_id;
	bool negotiated;
	bool subscribed;
};

struct seyrix_settings_control {
	struct seyrix_server *server;
	struct wl_event_source *listener_source;
	struct wl_list clients;
	int listener_fd;
	int runtime_fd;
	int directory_fd;
	uid_t owner_uid;
	pid_t owner_sid;
	dev_t socket_device;
	ino_t socket_inode;
	uint64_t revision;
	uint64_t next_connection_id;
	char runtime_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	bool socket_identity_valid;
};

#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
static uint64_t settings_trace_monotonic_ns(void) {
	struct timespec now = {0};
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000000000U + (uint64_t)now.tv_nsec;
}
#endif

static bool settings_send(struct seyrix_settings_client *client,
		const uint8_t *packet, size_t packet_size) {
	ssize_t sent = send(client->fd, packet, packet_size,
		MSG_DONTWAIT | MSG_NOSIGNAL);
	if (sent >= 0 && (size_t)sent == packet_size) {
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
		struct seyrix_settings_frame frame;
		if (seyrix_settings_decode_frame(packet, packet_size, &frame)) {
			if (frame.opcode == SEYRIX_SETTINGS_OP_RESPONSE &&
					frame.payload_size >= 4) {
				const uint8_t *payload = packet + SEYRIX_SETTINGS_HEADER_SIZE;
				wlr_log(WLR_INFO,
					"Settings endpoint sent peer_pid=%ld opcode=%u request=%u bytes=%zu revision=%llu response_status=%u response_to=%u connection_id=%llu fd=%d mono_ns=%llu",
					(long)client->pid, frame.opcode, frame.request_id, packet_size,
					(unsigned long long)frame.revision,
					seyrix_settings_read_u16(payload),
					seyrix_settings_read_u16(payload + 2),
					(unsigned long long)client->connection_id, client->fd,
					(unsigned long long)settings_trace_monotonic_ns());
			} else {
				wlr_log(WLR_INFO,
					"Settings endpoint sent peer_pid=%ld opcode=%u request=%u bytes=%zu revision=%llu connection_id=%llu fd=%d mono_ns=%llu",
					(long)client->pid, frame.opcode, frame.request_id, packet_size,
					(unsigned long long)frame.revision,
					(unsigned long long)client->connection_id, client->fd,
					(unsigned long long)settings_trace_monotonic_ns());
			}
		}
#endif
		return true;
	}
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
	int send_errno = errno;
	wlr_log(WLR_INFO,
		"Settings endpoint send failed peer_pid=%ld sent=%ld expected=%zu errno=%d connection_id=%llu fd=%d mono_ns=%llu",
		(long)client->pid, (long)sent, packet_size, send_errno,
		(unsigned long long)client->connection_id, client->fd,
		(unsigned long long)settings_trace_monotonic_ns());
#endif
	return false;
}

static bool settings_add_tlv(uint8_t *buffer, size_t capacity, size_t *used,
		uint16_t type, const void *value, size_t value_size) {
	if (value_size > UINT16_MAX || *used > capacity ||
			capacity - *used < 4 || value_size > capacity - *used - 4) {
		return false;
	}
	seyrix_settings_write_u16(buffer + *used, type);
	seyrix_settings_write_u16(buffer + *used + 2, (uint16_t)value_size);
	if (value_size > 0) {
		memcpy(buffer + *used + 4, value, value_size);
	}
	*used += 4 + value_size;
	return true;
}

static bool settings_add_u8(uint8_t *buffer, size_t capacity, size_t *used,
		uint16_t type, uint8_t value) {
	return settings_add_tlv(buffer, capacity, used, type, &value, sizeof(value));
}

static bool settings_add_u16(uint8_t *buffer, size_t capacity, size_t *used,
		uint16_t type, uint16_t value) {
	uint8_t encoded[2];
	seyrix_settings_write_u16(encoded, value);
	return settings_add_tlv(buffer, capacity, used, type, encoded, sizeof(encoded));
}

static bool settings_add_text(uint8_t *buffer, size_t capacity, size_t *used,
		uint16_t type, const char *value, size_t maximum) {
	if (value == NULL) {
		return false;
	}
	size_t length = strnlen(value, maximum + 1);
	return length <= maximum && settings_add_tlv(buffer, capacity, used,
		type, value, length);
}

static bool settings_add_wm_preferences(uint8_t *buffer, size_t capacity,
		size_t *used, uint8_t workspace,
		const struct seyrix_layout_preferences *preferences) {
	uint8_t encoded[16] = {0};
	encoded[0] = workspace;
	encoded[1] = (uint8_t)preferences->mode;
	encoded[2] = (uint8_t)preferences->split_axis;
	seyrix_settings_write_u32(encoded + 4,
		(uint32_t)preferences->inner_gap);
	seyrix_settings_write_u32(encoded + 8,
		(uint32_t)preferences->outer_gap);
	seyrix_settings_write_u32(encoded + 12,
		(uint32_t)preferences->split_ratio);
	return settings_add_tlv(buffer, capacity, used,
		SEYRIX_SETTINGS_TLV_WM_WORKSPACE, encoded, sizeof(encoded));
}

static bool settings_build_snapshot(struct seyrix_server *server,
		uint8_t *buffer, size_t capacity, size_t *snapshot_size) {
	size_t used = 0;
	if (server == NULL || buffer == NULL || snapshot_size == NULL ||
			capacity > SEYRIX_SETTINGS_MAX_PACKET_SIZE -
				SEYRIX_SETTINGS_HEADER_SIZE - 4) {
		return false;
	}
	uint8_t active_workspace = server->active_workspace_index <
			SEYRIX_WORKSPACE_COUNT ?
			(uint8_t)(server->active_workspace_index + 1) : 1;
	if (!settings_add_u8(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_ACTIVE_WORKSPACE, active_workspace) ||
		!settings_add_u8(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_WORKSPACE_COUNT, SEYRIX_WORKSPACE_COUNT) ||
		!settings_add_u8(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_SHELL_MODE,
			(uint8_t)server->shell_config.mode) ||
		!settings_add_u8(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_SHELL_POSITION,
			(uint8_t)server->shell_config.position) ||
		!settings_add_u8(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_SHELL_ALIGNMENT,
			(uint8_t)server->shell_config.dock_alignment) ||
		!settings_add_u8(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_SHELL_ICON_SIZE,
			(uint8_t)server->shell_config.icon_size) ||
		!settings_add_u8(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_SHELL_AUTO_HIDE,
			server->shell_config.auto_hide ? 1U : 0U) ||
		!settings_add_text(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_SHELL_WALLPAPER,
			server->shell_config.wallpaper,
			SEYRIX_SETTINGS_MAX_WALLPAPER_BYTES) ||
		!settings_add_text(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_SHELL_BACKGROUND_COLOR,
			server->shell_config.background_color, 7) ||
		!settings_add_text(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_LAUNCHER_SHORTCUT,
			server->shell_config.launcher_shortcut,
			SEYRIX_SHELL_SHORTCUT_MAX) ||
		!settings_add_text(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_SWITCHER_SHORTCUT,
			server->shell_config.switcher_shortcut,
			SEYRIX_SHELL_SHORTCUT_MAX) ||
		!settings_add_u8(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_SWITCHER_ACTIVE_WORKSPACE_ONLY,
			server->shell_config.switcher_active_workspace_only ? 1U : 0U)) {
		return false;
	}
	for (size_t i = 0; i < server->shell_config.pinned_count &&
			i < SEYRIX_SETTINGS_MAX_PINNED_APPS; i++) {
		if (!settings_add_text(buffer, capacity, &used,
				SEYRIX_SETTINGS_TLV_PINNED_APP,
				server->shell_config.pinned_apps[i],
				SEYRIX_SETTINGS_MAX_DESKTOP_ID_BYTES)) {
			return false;
		}
	}

	if (!settings_add_wm_preferences(buffer, capacity, &used, 0,
			&server->wm_config.defaults)) {
		return false;
	}
	for (size_t i = 0; i < SEYRIX_WORKSPACE_COUNT; i++) {
		if (!settings_add_wm_preferences(buffer, capacity, &used,
				(uint8_t)(i + 1), &server->workspaces[i].preferences)) {
			return false;
		}
	}
	uint16_t app_rule_count = 0;
	for (const struct seyrix_app_rule *rule = server->wm_config.app_rules;
			rule != NULL; rule = rule->next) {
		if (app_rule_count < UINT16_MAX) {
			app_rule_count++;
		}
	}
	if (!settings_add_u16(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_WM_APP_RULE_COUNT, app_rule_count)) {
		return false;
	}

	struct seyrix_output *output;
	size_t output_count = 0;
	wl_list_for_each(output, &server->outputs, link) {
		if (output_count++ >= SETTINGS_MAX_OUTPUTS) {
			return false;
		}
		const char *name = output->wlr_output->name != NULL ?
			output->wlr_output->name : "unknown";
		size_t name_length = strnlen(name,
			SEYRIX_SETTINGS_MAX_OUTPUT_NAME_BYTES + 1U);
		if (name_length > SEYRIX_SETTINGS_MAX_OUTPUT_NAME_BYTES) {
			return false;
		}
		uint8_t encoded[24 + SEYRIX_SETTINGS_MAX_OUTPUT_NAME_BYTES] = {0};
		encoded[0] = (uint8_t)name_length;
		encoded[1] = output->wlr_output->enabled ? 1U : 0U;
		seyrix_settings_write_u32(encoded + 4,
			(uint32_t)(output->width > 0 ? output->width : 0));
		seyrix_settings_write_u32(encoded + 8,
			(uint32_t)(output->height > 0 ? output->height : 0));
		float scale = output->scale > 0.0f ? output->scale : 1.0f;
		seyrix_settings_write_u32(encoded + 12,
			(uint32_t)(scale * 1000.0f + 0.5f));
		double x = 0.0;
		double y = 0.0;
		if (server->output_layout != NULL) {
			wlr_output_layout_output_coords(server->output_layout,
				output->wlr_output, &x, &y);
		}
		seyrix_settings_write_u32(encoded + 16, (uint32_t)(int32_t)x);
		seyrix_settings_write_u32(encoded + 20, (uint32_t)(int32_t)y);
		memcpy(encoded + 24, name, name_length);
		if (!settings_add_tlv(buffer, capacity, &used,
				SEYRIX_SETTINGS_TLV_OUTPUT,
				encoded, 24 + name_length)) {
			return false;
		}
	}

	uint16_t keyboard_count = 0;
	struct seyrix_keyboard *keyboard;
	wl_list_for_each(keyboard, &server->keyboards, link) {
		if (keyboard_count < UINT16_MAX) {
			keyboard_count++;
		}
	}
	if (!settings_add_u16(buffer, capacity, &used,
			SEYRIX_SETTINGS_TLV_KEYBOARD_COUNT, keyboard_count)) {
		return false;
	}
	*snapshot_size = used;
	return true;
}

static bool settings_send_response(struct seyrix_settings_client *client,
		uint32_t request_id, uint16_t request_opcode,
		enum seyrix_settings_error error, const uint8_t *data,
		size_t data_size) {
	uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	if (data_size > sizeof(packet) - SEYRIX_SETTINGS_HEADER_SIZE - 4 ||
			request_id == 0) {
		return false;
	}
	uint32_t payload_size = (uint32_t)(data_size + 4);
	if (!seyrix_settings_encode_header(packet, sizeof(packet),
			SEYRIX_SETTINGS_OP_RESPONSE, request_id, payload_size,
			client->control->revision)) {
		return false;
	}
	seyrix_settings_write_u16(packet + SEYRIX_SETTINGS_HEADER_SIZE,
		(uint16_t)error);
	seyrix_settings_write_u16(packet + SEYRIX_SETTINGS_HEADER_SIZE + 2,
		request_opcode);
	if (data_size > 0) {
		memcpy(packet + SEYRIX_SETTINGS_HEADER_SIZE + 4, data, data_size);
	}
	return settings_send(client, packet,
		SEYRIX_SETTINGS_HEADER_SIZE + payload_size);
}

static bool settings_send_snapshot(struct seyrix_settings_client *client,
		const struct seyrix_settings_frame *request) {
	uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	size_t snapshot_size = 0;
	if (!settings_build_snapshot(client->control->server,
			packet + SEYRIX_SETTINGS_HEADER_SIZE + 4,
		sizeof(packet) - SEYRIX_SETTINGS_HEADER_SIZE - 4, &snapshot_size)) {
		return settings_send_response(client, request->request_id,
			request->opcode, SEYRIX_SETTINGS_ERROR_INTERNAL, NULL, 0);
	}
	uint32_t payload_size = (uint32_t)(snapshot_size + 4);
	if (!seyrix_settings_encode_header(packet, sizeof(packet),
			SEYRIX_SETTINGS_OP_RESPONSE, request->request_id,
			payload_size, client->control->revision)) {
		return false;
	}
	enum seyrix_settings_error status = SEYRIX_SETTINGS_ERROR_OK;
	if (request->revision != 0 &&
			request->revision != client->control->revision) {
		status = SEYRIX_SETTINGS_ERROR_STALE_REVISION;
	}
	seyrix_settings_write_u16(packet + SEYRIX_SETTINGS_HEADER_SIZE,
		(uint16_t)status);
	seyrix_settings_write_u16(packet + SEYRIX_SETTINGS_HEADER_SIZE + 2,
		request->opcode);
	return settings_send(client, packet,
		SEYRIX_SETTINGS_HEADER_SIZE + payload_size);
}

static bool settings_send_changed(struct seyrix_settings_client *client) {
	uint8_t packet[SEYRIX_SETTINGS_HEADER_SIZE];
	if (!seyrix_settings_encode_header(packet, sizeof(packet),
			SEYRIX_SETTINGS_OP_CHANGED, 0, 0,
			client->control->revision)) {
		return false;
	}
	return settings_send(client, packet, sizeof(packet));
}

static void settings_remove_client(struct seyrix_settings_client *client) {
	if (client == NULL) {
		return;
	}
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
	wlr_log(WLR_INFO,
		"Settings endpoint removing peer_pid=%ld connection_id=%llu fd=%d negotiated=%d subscribed=%d mono_ns=%llu",
		(long)client->pid, (unsigned long long)client->connection_id,
		client->fd, client->negotiated, client->subscribed,
		(unsigned long long)settings_trace_monotonic_ns());
#endif
	if (client->source != NULL) {
		wl_event_source_remove(client->source);
		client->source = NULL;
	}
	if (client->fd >= 0) {
		close(client->fd);
		client->fd = -1;
	}
	if (client->link.next != NULL && client->link.prev != NULL) {
		wl_list_remove(&client->link);
		wl_list_init(&client->link);
	}
	free(client);
}

static bool settings_handle_hello(struct seyrix_settings_client *client,
		const struct seyrix_settings_frame *frame) {
	if (frame->payload_size != 4 || frame->revision != 0) {
		return settings_send_response(client, frame->request_id,
			frame->opcode, SEYRIX_SETTINGS_ERROR_MALFORMED, NULL, 0);
	}
	uint8_t min_major = frame->payload[0];
	uint8_t max_major = frame->payload[1];
	uint8_t min_minor = frame->payload[2];
	uint8_t max_minor = frame->payload[3];
	if (min_major > max_major || min_minor > max_minor ||
			min_major > 1 || max_major < 1 ||
			(min_major == 1 && min_minor > 0)) {
		return settings_send_response(client, frame->request_id,
			frame->opcode, SEYRIX_SETTINGS_ERROR_UNSUPPORTED_VERSION, NULL, 0);
	}
	const uint8_t selected_version[2] = {1, 0};
	client->negotiated = true;
	return settings_send_response(client, frame->request_id, frame->opcode,
		SEYRIX_SETTINGS_ERROR_OK, selected_version,
		sizeof(selected_version));
}

static size_t settings_subscriber_count(
		const struct seyrix_settings_control *control) {
	size_t count = 0;
	const struct seyrix_settings_client *client;
	wl_list_for_each(client, &control->clients, link) {
		if (client->subscribed) {
			count++;
		}
	}
	return count;
}

static bool settings_handle_request(struct seyrix_settings_client *client,
		const struct seyrix_settings_frame *frame) {
	if (frame->request_id == 0) {
		return false;
	}
	if (frame->frame_version != SEYRIX_SETTINGS_FRAME_VERSION) {
		return settings_send_response(client, frame->request_id,
			frame->opcode, SEYRIX_SETTINGS_ERROR_UNSUPPORTED_FRAME, NULL, 0);
	}
	if (frame->flags != 0) {
		return settings_send_response(client, frame->request_id,
			frame->opcode, SEYRIX_SETTINGS_ERROR_MALFORMED, NULL, 0);
	}
	if (frame->opcode == SEYRIX_SETTINGS_OP_HELLO) {
		return settings_handle_hello(client, frame);
	}
	if (!client->negotiated) {
		return settings_send_response(client, frame->request_id,
			frame->opcode, SEYRIX_SETTINGS_ERROR_NOT_NEGOTIATED, NULL, 0);
	}
	switch (frame->opcode) {
	case SEYRIX_SETTINGS_OP_GET_SNAPSHOT:
		if (frame->payload_size != 0) {
			return settings_send_response(client, frame->request_id,
				frame->opcode, SEYRIX_SETTINGS_ERROR_MALFORMED, NULL, 0);
		}
		return settings_send_snapshot(client, frame);
	case SEYRIX_SETTINGS_OP_SUBSCRIBE:
		if (frame->payload_size != 0) {
			return settings_send_response(client, frame->request_id,
				frame->opcode, SEYRIX_SETTINGS_ERROR_MALFORMED, NULL, 0);
		}
		if (!client->subscribed && settings_subscriber_count(
				client->control) >= SETTINGS_MAX_SUBSCRIBERS) {
			return settings_send_response(client, frame->request_id,
				frame->opcode, SEYRIX_SETTINGS_ERROR_TOO_MANY_CLIENTS, NULL, 0);
		}
		client->subscribed = true;
		return settings_send_snapshot(client, frame);
	case SEYRIX_SETTINGS_OP_UNSUBSCRIBE:
		if (frame->payload_size != 0) {
			return settings_send_response(client, frame->request_id,
				frame->opcode, SEYRIX_SETTINGS_ERROR_MALFORMED, NULL, 0);
		}
		client->subscribed = false;
		return settings_send_response(client, frame->request_id,
			frame->opcode, SEYRIX_SETTINGS_ERROR_OK, NULL, 0);
	default:
		return settings_send_response(client, frame->request_id,
			frame->opcode, SEYRIX_SETTINGS_ERROR_UNKNOWN_OPCODE, NULL, 0);
	}
}

static int settings_handle_client(int fd, uint32_t mask, void *data) {
	struct seyrix_settings_client *client = data;
	if (client == NULL || client->fd != fd) {
		return 0;
	}
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
	wlr_log(WLR_INFO,
		"Settings endpoint dispatch peer_pid=%ld connection_id=%llu fd=%d mask=0x%x mono_ns=%llu",
		(long)client->pid, (unsigned long long)client->connection_id,
		fd, mask, (unsigned long long)settings_trace_monotonic_ns());
#endif
	if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) != 0) {
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
		wlr_log(WLR_INFO,
			"Settings endpoint peer hangup pid=%ld mask=0x%x negotiated=%d subscribed=%d connection_id=%llu fd=%d mono_ns=%llu",
			(long)client->pid, mask, client->negotiated, client->subscribed,
			(unsigned long long)client->connection_id, fd,
			(unsigned long long)settings_trace_monotonic_ns());
#endif
		settings_remove_client(client);
		return 0;
	}
	for (unsigned int handled = 0;
			handled < SETTINGS_MAX_MESSAGES_PER_DISPATCH; handled++) {
		uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
		char control[CMSG_SPACE(sizeof(int) * 4)];
		struct iovec iov = {.iov_base = packet, .iov_len = sizeof(packet)};
		struct msghdr message = {
			.msg_iov = &iov,
			.msg_iovlen = 1,
			.msg_control = control,
			.msg_controllen = sizeof(control),
		};
		ssize_t received = recvmsg(fd, &message,
			MSG_DONTWAIT | MSG_TRUNC | MSG_CMSG_CLOEXEC);
		int receive_errno = received < 0 ? errno : 0;
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
		wlr_log(WLR_INFO,
			"Settings endpoint read peer_pid=%ld connection_id=%llu fd=%d received=%ld errno=%d msg_flags=0x%x control_len=%zu mono_ns=%llu",
			(long)client->pid, (unsigned long long)client->connection_id, fd,
			(long)received, receive_errno, message.msg_flags,
			(size_t)message.msg_controllen,
			(unsigned long long)settings_trace_monotonic_ns());
#endif
		if (received == 0) {
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
			wlr_log(WLR_INFO,
				"Settings endpoint peer closed pid=%ld connection_id=%llu fd=%d mono_ns=%llu",
				(long)client->pid,
				(unsigned long long)client->connection_id, fd,
				(unsigned long long)settings_trace_monotonic_ns());
#endif
			settings_remove_client(client);
			return 0;
		}
		if (received < 0) {
			if (receive_errno == EINTR) {
				handled--;
				continue;
			}
			if (receive_errno == EAGAIN || receive_errno == EWOULDBLOCK) {
				/* The read callback ran and the accepted socket had no queued frame. */
				return 0;
			}
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
			wlr_log(WLR_INFO,
				"Settings endpoint receive failed peer_pid=%ld errno=%d connection_id=%llu fd=%d mono_ns=%llu",
				(long)client->pid, receive_errno,
				(unsigned long long)client->connection_id, fd,
				(unsigned long long)settings_trace_monotonic_ns());
#endif
			settings_remove_client(client);
			return 0;
		}
		bool received_fds = false;
		for (struct cmsghdr *header = CMSG_FIRSTHDR(&message);
				header != NULL; header = CMSG_NXTHDR(&message, header)) {
			if (header->cmsg_level == SOL_SOCKET &&
					header->cmsg_type == SCM_RIGHTS) {
				size_t bytes = header->cmsg_len >= CMSG_LEN(0) ?
					header->cmsg_len - CMSG_LEN(0) : 0;
				size_t count = bytes / sizeof(int);
				int *descriptors = (int *)CMSG_DATA(header);
				for (size_t i = 0; i < count; i++) {
					close(descriptors[i]);
				}
			}
			received_fds = true;
		}
		struct seyrix_settings_frame frame;
		bool truncated = (message.msg_flags & MSG_TRUNC) != 0 ||
			(size_t)received > sizeof(packet);
		bool decoded = !truncated &&
			seyrix_settings_decode_frame(packet, (size_t)received, &frame);
		if (received_fds || truncated || !decoded) {
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
			const char *reason = received_fds ? "ancillary-data" :
				truncated ? "truncated" : "invalid-wire-frame";
			wlr_log(WLR_INFO,
				"Settings endpoint rejected frame peer_pid=%ld reason=%s size=%ld msg_flags=0x%x control_len=%zu magic=%02x%02x%02x%02x connection_id=%llu fd=%d mono_ns=%llu",
				(long)client->pid, reason, (long)received, message.msg_flags,
				(size_t)message.msg_controllen,
				(unsigned int)(received > 0 ? packet[0] : 0),
				(unsigned int)(received > 1 ? packet[1] : 0),
				(unsigned int)(received > 2 ? packet[2] : 0),
				(unsigned int)(received > 3 ? packet[3] : 0),
				(unsigned long long)client->connection_id, fd,
				(unsigned long long)settings_trace_monotonic_ns());
#endif
			settings_remove_client(client);
			return 0;
		}
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
		wlr_log(WLR_INFO,
			"Settings endpoint frame peer_pid=%ld version=%u flags=%u opcode=%u request=%u payload=%u revision=%llu negotiated=%d connection_id=%llu fd=%d mono_ns=%llu",
			(long)client->pid, frame.frame_version, frame.flags, frame.opcode,
			frame.request_id, frame.payload_size,
			(unsigned long long)frame.revision, client->negotiated,
			(unsigned long long)client->connection_id, fd,
			(unsigned long long)settings_trace_monotonic_ns());
#endif
		bool request_handled = settings_handle_request(client, &frame);
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
		wlr_log(WLR_INFO,
			"Settings endpoint request-dispatch peer_pid=%ld connection_id=%llu fd=%d opcode=%u request=%u result=%s negotiated=%d subscribed=%d mono_ns=%llu",
			(long)client->pid, (unsigned long long)client->connection_id, fd,
			frame.opcode, frame.request_id,
			request_handled ? "handled" : "failed", client->negotiated,
			client->subscribed,
			(unsigned long long)settings_trace_monotonic_ns());
#endif
		if (!request_handled) {
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
			wlr_log(WLR_INFO,
				"Settings endpoint rejected request peer_pid=%ld version=%u flags=%u opcode=%u request=%u payload=%u negotiated=%d connection_id=%llu fd=%d mono_ns=%llu",
				(long)client->pid, frame.frame_version, frame.flags,
				frame.opcode, frame.request_id, frame.payload_size,
				client->negotiated,
				(unsigned long long)client->connection_id, fd,
				(unsigned long long)settings_trace_monotonic_ns());
#endif
			settings_remove_client(client);
			return 0;
		}
	}
	return 0;
}

static size_t settings_client_count(
		const struct seyrix_settings_control *control) {
	size_t count = 0;
	const struct seyrix_settings_client *client;
	wl_list_for_each(client, &control->clients, link) {
		count++;
	}
	return count;
}

bool seyrix_settings_peer_authorized(uid_t peer_uid, pid_t peer_pid,
		uid_t owner_uid, pid_t owner_pid) {
	if (peer_uid != owner_uid || peer_pid <= 0 || owner_pid <= 0) {
		return false;
	}
	pid_t peer_sid = getsid(peer_pid);
	pid_t owner_sid = getsid(owner_pid);
	return peer_sid >= 0 && owner_sid >= 0 && peer_sid == owner_sid;
}

static bool settings_accept_peer(struct seyrix_settings_control *control,
		int fd, uint64_t connection_id, uid_t *uid, pid_t *pid) {
	(void)connection_id;
	struct ucred credentials;
	socklen_t size = sizeof(credentials);
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 ||
			size != sizeof(credentials)) {
		return false;
	}
	if (!seyrix_settings_peer_authorized(credentials.uid, credentials.pid,
			control->owner_uid, getpid())) {
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
		errno = 0;
		pid_t peer_sid = credentials.pid > 0 ? getsid(credentials.pid) : -1;
		int peer_sid_errno = errno;
		errno = 0;
		pid_t owner_sid = getsid(0);
		int owner_sid_errno = errno;
		wlr_log(WLR_INFO,
			"Settings endpoint rejected peer uid=%ld pid=%ld sid=%ld sid_errno=%d; owner uid=%ld pid=%ld sid=%ld sid_errno=%d connection_id=%llu fd=%d mono_ns=%llu",
			(long)credentials.uid, (long)credentials.pid, (long)peer_sid,
			peer_sid_errno, (long)control->owner_uid, (long)getpid(),
			(long)owner_sid, owner_sid_errno,
			(unsigned long long)connection_id, fd,
			(unsigned long long)settings_trace_monotonic_ns());
#endif
		return false;
	}
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
	struct stat socket_status = {0};
	int socket_stat_result = fstat(fd, &socket_status);
	int socket_stat_errno = socket_stat_result == 0 ? 0 : errno;
	wlr_log(WLR_INFO,
		"Settings endpoint accepted peer uid=%ld pid=%ld sid=%ld owner_pid=%ld owner_sid=%ld connection_id=%llu fd=%d inode=%llu stat_errno=%d mono_ns=%llu",
		(long)credentials.uid, (long)credentials.pid,
		(long)getsid(credentials.pid), (long)getpid(), (long)getsid(0),
		(unsigned long long)connection_id, fd,
		(unsigned long long)(socket_stat_result == 0 ? socket_status.st_ino : 0),
		socket_stat_errno,
		(unsigned long long)settings_trace_monotonic_ns());
#endif
	*uid = credentials.uid;
	*pid = credentials.pid;
	return true;
}

static void settings_accept_clients(struct seyrix_settings_control *control) {
	for (unsigned int accepted = 0; accepted < SETTINGS_MAX_CLIENTS; accepted++) {
		int fd = accept4(control->listener_fd, NULL, NULL,
			SOCK_CLOEXEC | SOCK_NONBLOCK);
		if (fd < 0) {
			if (errno == EINTR) {
				accepted--;
				continue;
			}
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				wlr_log(WLR_ERROR, "Settings endpoint accept failed: %s",
					strerror(errno));
			}
			return;
		}
		if (settings_client_count(control) >= SETTINGS_MAX_CLIENTS) {
			close(fd);
			continue;
		}
		uint64_t connection_id = ++control->next_connection_id;
		if (connection_id == 0) {
			connection_id = ++control->next_connection_id;
		}
		uid_t uid = (uid_t)-1;
		pid_t pid = -1;
		if (!settings_accept_peer(control, fd, connection_id, &uid, &pid)) {
			close(fd);
			continue;
		}
		struct seyrix_settings_client *client = calloc(1, sizeof(*client));
		if (client == NULL) {
			close(fd);
			continue;
		}
		client->control = control;
		client->fd = fd;
		client->uid = uid;
		client->pid = pid;
		client->connection_id = connection_id;
		wl_list_init(&client->link);
		client->source = wl_event_loop_add_fd(
			wl_display_get_event_loop(control->server->display), fd,
			WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
			settings_handle_client, client);
		if (client->source == NULL) {
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
			wlr_log(WLR_INFO,
				"Settings endpoint watch failed peer_pid=%ld connection_id=%llu fd=%d mono_ns=%llu",
				(long)client->pid,
				(unsigned long long)client->connection_id, fd,
				(unsigned long long)settings_trace_monotonic_ns());
#endif
			close(fd);
			free(client);
			continue;
		}
#ifdef SEYRIX_QEMU_ACCEPTANCE_HOOKS
		wlr_log(WLR_INFO,
			"Settings endpoint watch ready peer_pid=%ld connection_id=%llu fd=%d mono_ns=%llu",
			(long)client->pid,
			(unsigned long long)client->connection_id, fd,
			(unsigned long long)settings_trace_monotonic_ns());
#endif
		wl_list_insert(&control->clients, &client->link);
	}
}

static int settings_handle_listener(int fd, uint32_t mask, void *data) {
	struct seyrix_settings_control *control = data;
	if (control == NULL || control->listener_fd != fd) {
		return 0;
	}
	if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) != 0) {
		wlr_log(WLR_ERROR, "Settings endpoint listener reported an error");
		return 0;
	}
	settings_accept_clients(control);
	return 0;
}

static bool settings_verify_runtime_dir(const char *path, int *fd_out,
		uid_t owner) {
	struct stat status;
	if (path == NULL || path[0] != '/' || lstat(path, &status) != 0 ||
			!S_ISDIR(status.st_mode) || status.st_uid != owner ||
			(status.st_mode & 0077) != 0) {
		return false;
	}
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0 || fstat(fd, &status) != 0 || !S_ISDIR(status.st_mode) ||
			status.st_uid != owner || (status.st_mode & 0077) != 0) {
		if (fd >= 0) {
			close(fd);
		}
		return false;
	}
	*fd_out = fd;
	return true;
}

static bool settings_open_private_dir(struct seyrix_settings_control *control) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime == NULL || strlen(runtime) >= sizeof(control->runtime_path)) {
		return false;
	}
	if (!settings_verify_runtime_dir(runtime, &control->runtime_fd,
			control->owner_uid)) {
		return false;
	}
	memcpy(control->runtime_path, runtime, strlen(runtime) + 1);
	if (mkdirat(control->runtime_fd, "seyrix", 0700) != 0 && errno != EEXIST) {
		return false;
	}
	control->directory_fd = openat(control->runtime_fd, "seyrix",
		O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (control->directory_fd < 0) {
		return false;
	}
	struct stat status;
	if (fstat(control->directory_fd, &status) != 0 ||
			!S_ISDIR(status.st_mode) || status.st_uid != control->owner_uid ||
		fchmod(control->directory_fd, 0700) != 0 ||
		fstat(control->directory_fd, &status) != 0 ||
		(status.st_mode & 0077) != 0) {
		return false;
	}
	int path_size = snprintf(control->socket_path,
		sizeof(control->socket_path), "%s/seyrix/settings-v1.sock", runtime);
	return path_size > 0 && (size_t)path_size < sizeof(control->socket_path);
}

static int settings_probe_existing_socket(
		struct seyrix_settings_control *control) {
	struct stat status;
	if (fstatat(control->directory_fd, "settings-v1.sock", &status,
			AT_SYMLINK_NOFOLLOW) != 0) {
		return errno == ENOENT ? 0 : -1;
	}
	if (!S_ISSOCK(status.st_mode) || status.st_uid != control->owner_uid) {
		errno = EEXIST;
		return -1;
	}
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	if (strlen(control->socket_path) >= sizeof(address.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(address.sun_path, control->socket_path,
		strlen(control->socket_path) + 1);
	int probe = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (probe < 0) {
		return -1;
	}
	int result = connect(probe, (struct sockaddr *)&address, sizeof(address));
	int connect_error = result == 0 ? 0 : errno;
	if (result != 0 && (connect_error == EINPROGRESS ||
			connect_error == EAGAIN || connect_error == EALREADY)) {
		struct pollfd poll_fd = {.fd = probe, .events = POLLOUT};
		if (poll(&poll_fd, 1, 100) > 0) {
			socklen_t error_size = sizeof(connect_error);
			if (getsockopt(probe, SOL_SOCKET, SO_ERROR, &connect_error,
					&error_size) != 0) {
				connect_error = errno;
			}
		}
	}
	close(probe);
	if (result == 0 || connect_error == 0 || connect_error == EINPROGRESS ||
			connect_error == EAGAIN || connect_error == EALREADY) {
		errno = EADDRINUSE;
		return -1;
	}
	if (connect_error != ECONNREFUSED && connect_error != ENOENT) {
		errno = connect_error;
		return -1;
	}
	struct stat current;
	if (fstatat(control->directory_fd, "settings-v1.sock", &current,
			AT_SYMLINK_NOFOLLOW) != 0 || current.st_dev != status.st_dev ||
			current.st_ino != status.st_ino || !S_ISSOCK(current.st_mode)) {
		errno = EAGAIN;
		return -1;
	}
	return unlinkat(control->directory_fd, "settings-v1.sock", 0);
}

bool seyrix_settings_control_init(struct seyrix_server *server) {
	if (server == NULL || server->display == NULL ||
			server->settings_control != NULL) {
		return false;
	}
	struct seyrix_settings_control *control = calloc(1, sizeof(*control));
	if (control == NULL) {
		return false;
	}
	control->server = server;
	control->listener_fd = -1;
	control->runtime_fd = -1;
	control->directory_fd = -1;
	control->owner_uid = geteuid();
	control->owner_sid = getsid(0);
	control->revision = 1;
	wl_list_init(&control->clients);
	server->settings_control = control;
	if (control->owner_sid < 0 || !settings_open_private_dir(control) ||
			settings_probe_existing_socket(control) != 0) {
		wlr_log(WLR_ERROR, "Cannot prepare private Settings runtime endpoint: %s",
			strerror(errno));
		seyrix_settings_control_finish(server);
		return false;
	}
	control->listener_fd = socket(AF_UNIX,
		SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (control->listener_fd < 0) {
		seyrix_settings_control_finish(server);
		return false;
	}
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	memcpy(address.sun_path, control->socket_path,
		strlen(control->socket_path) + 1);
	if (bind(control->listener_fd, (struct sockaddr *)&address,
			sizeof(address)) != 0) {
		wlr_log(WLR_ERROR, "Cannot create Settings endpoint socket: %s",
			strerror(errno));
		seyrix_settings_control_finish(server);
		return false;
	}
	struct stat status;
	if (fstatat(control->directory_fd, "settings-v1.sock", &status,
			AT_SYMLINK_NOFOLLOW) != 0 || !S_ISSOCK(status.st_mode) ||
			status.st_uid != control->owner_uid) {
		wlr_log(WLR_ERROR, "Settings endpoint socket has unsafe ownership or mode");
		seyrix_settings_control_finish(server);
		return false;
	}
	control->socket_device = status.st_dev;
	control->socket_inode = status.st_ino;
	control->socket_identity_valid = true;
	if (fchmodat(control->directory_fd, "settings-v1.sock", 0600, 0) != 0 ||
			listen(control->listener_fd, SETTINGS_MAX_CLIENTS) != 0) {
		wlr_log(WLR_ERROR, "Cannot secure or listen on Settings endpoint socket: %s",
			strerror(errno));
		seyrix_settings_control_finish(server);
		return false;
	}
	if (fstatat(control->directory_fd, "settings-v1.sock", &status,
			AT_SYMLINK_NOFOLLOW) != 0) {
		wlr_log(WLR_ERROR, "Cannot verify Settings endpoint socket: %s",
			strerror(errno));
		seyrix_settings_control_finish(server);
		return false;
	}
	if (!S_ISSOCK(status.st_mode) || status.st_uid != control->owner_uid ||
			(status.st_mode & 0777) != 0600 ||
			status.st_dev != control->socket_device ||
			status.st_ino != control->socket_inode) {
		wlr_log(WLR_ERROR, "Settings endpoint socket ownership or mode changed");
		seyrix_settings_control_finish(server);
		return false;
	}
	control->listener_source = wl_event_loop_add_fd(
		wl_display_get_event_loop(server->display), control->listener_fd,
		WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
		settings_handle_listener, control);
	if (control->listener_source == NULL) {
		seyrix_settings_control_finish(server);
		return false;
	}
	wlr_log(WLR_INFO,
		"Settings read-only endpoint ready at %s protocol=1.0 revision=%llu",
		control->socket_path, (unsigned long long)control->revision);
	return true;
}

void seyrix_settings_control_finish(struct seyrix_server *server) {
	if (server == NULL || server->settings_control == NULL) {
		return;
	}
	struct seyrix_settings_control *control = server->settings_control;
	server->settings_control = NULL;
	if (control->listener_source != NULL) {
		wl_event_source_remove(control->listener_source);
		control->listener_source = NULL;
	}
	struct seyrix_settings_client *client, *temporary;
	wl_list_for_each_safe(client, temporary, &control->clients, link) {
		settings_remove_client(client);
	}
	if (control->listener_fd >= 0) {
		close(control->listener_fd);
		control->listener_fd = -1;
	}
	if (control->socket_identity_valid && control->directory_fd >= 0) {
		struct stat status;
		if (fstatat(control->directory_fd, "settings-v1.sock", &status,
				AT_SYMLINK_NOFOLLOW) == 0 && S_ISSOCK(status.st_mode) &&
			status.st_dev == control->socket_device &&
			status.st_ino == control->socket_inode) {
			(void)unlinkat(control->directory_fd, "settings-v1.sock", 0);
		}
	}
	if (control->directory_fd >= 0) {
		close(control->directory_fd);
	}
	if (control->runtime_fd >= 0) {
		close(control->runtime_fd);
	}
	free(control);
}

void seyrix_settings_control_changed(struct seyrix_server *server) {
	if (server == NULL || server->settings_control == NULL) {
		return;
	}
	struct seyrix_settings_control *control = server->settings_control;
	control->revision++;
	if (control->revision == 0) {
		control->revision = 1;
	}
	struct seyrix_settings_client *client, *temporary;
	wl_list_for_each_safe(client, temporary, &control->clients, link) {
		if (client->subscribed && client->negotiated &&
				!settings_send_changed(client)) {
			settings_remove_client(client);
		}
	}
}
