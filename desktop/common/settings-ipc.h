#ifndef SEYRIX_SETTINGS_IPC_H
#define SEYRIX_SETTINGS_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wire format v1 is byte encoded; no C structure layout crosses the socket. */
#define SEYRIX_SETTINGS_MAGIC_0 ((uint8_t)'S')
#define SEYRIX_SETTINGS_MAGIC_1 ((uint8_t)'Y')
#define SEYRIX_SETTINGS_MAGIC_2 ((uint8_t)'R')
#define SEYRIX_SETTINGS_MAGIC_3 ((uint8_t)'X')
#define SEYRIX_SETTINGS_FRAME_VERSION 1U
#define SEYRIX_SETTINGS_HEADER_SIZE 24U
#define SEYRIX_SETTINGS_MAX_PACKET_SIZE 16384U
#define SEYRIX_SETTINGS_MAX_OUTPUTS 16U
#define SEYRIX_SETTINGS_WORKSPACE_COUNT 4U
#define SEYRIX_SETTINGS_MAX_WALLPAPER_BYTES 4095U
#define SEYRIX_SETTINGS_MAX_PINNED_APPS 32U
#define SEYRIX_SETTINGS_MAX_DESKTOP_ID_BYTES 127U
#define SEYRIX_SETTINGS_MAX_OUTPUT_NAME_BYTES 64U

enum seyrix_settings_opcode {
	SEYRIX_SETTINGS_OP_HELLO = 1,
	SEYRIX_SETTINGS_OP_GET_SNAPSHOT = 2,
	SEYRIX_SETTINGS_OP_SUBSCRIBE = 3,
	SEYRIX_SETTINGS_OP_UNSUBSCRIBE = 4,
	SEYRIX_SETTINGS_OP_RESPONSE = 0x8000,
	SEYRIX_SETTINGS_OP_CHANGED = 0x8001,
};

enum seyrix_settings_error {
	SEYRIX_SETTINGS_ERROR_OK = 0,
	SEYRIX_SETTINGS_ERROR_MALFORMED = 1,
	SEYRIX_SETTINGS_ERROR_UNSUPPORTED_FRAME = 2,
	SEYRIX_SETTINGS_ERROR_UNSUPPORTED_VERSION = 3,
	SEYRIX_SETTINGS_ERROR_NOT_NEGOTIATED = 4,
	SEYRIX_SETTINGS_ERROR_UNKNOWN_OPCODE = 5,
	SEYRIX_SETTINGS_ERROR_NOT_AUTHORIZED = 6,
	SEYRIX_SETTINGS_ERROR_WRONG_SESSION = 7,
	SEYRIX_SETTINGS_ERROR_TOO_MANY_CLIENTS = 8,
	SEYRIX_SETTINGS_ERROR_STALE_REVISION = 9,
	SEYRIX_SETTINGS_ERROR_INTERNAL = 10,
};

enum seyrix_settings_tlv {
	SEYRIX_SETTINGS_TLV_ACTIVE_WORKSPACE = 1,
	SEYRIX_SETTINGS_TLV_WORKSPACE_COUNT = 2,
	SEYRIX_SETTINGS_TLV_SHELL_MODE = 16,
	SEYRIX_SETTINGS_TLV_SHELL_POSITION = 17,
	SEYRIX_SETTINGS_TLV_SHELL_ALIGNMENT = 18,
	SEYRIX_SETTINGS_TLV_SHELL_ICON_SIZE = 19,
	SEYRIX_SETTINGS_TLV_SHELL_AUTO_HIDE = 20,
	SEYRIX_SETTINGS_TLV_SHELL_WALLPAPER = 21,
	SEYRIX_SETTINGS_TLV_SHELL_BACKGROUND_COLOR = 22,
	SEYRIX_SETTINGS_TLV_PINNED_APP = 23,
	SEYRIX_SETTINGS_TLV_LAUNCHER_SHORTCUT = 24,
	SEYRIX_SETTINGS_TLV_SWITCHER_SHORTCUT = 25,
	SEYRIX_SETTINGS_TLV_SWITCHER_ACTIVE_WORKSPACE_ONLY = 26,
	SEYRIX_SETTINGS_TLV_WM_WORKSPACE = 32,
	SEYRIX_SETTINGS_TLV_WM_APP_RULE_COUNT = 33,
	SEYRIX_SETTINGS_TLV_OUTPUT = 48,
	SEYRIX_SETTINGS_TLV_KEYBOARD_COUNT = 64,
	SEYRIX_SETTINGS_TLV_SHELL_CONNECTED = 65,
};

struct seyrix_settings_frame {
	uint8_t frame_version;
	uint8_t flags;
	uint16_t opcode;
	uint32_t request_id;
	uint32_t payload_size;
	uint64_t revision;
	const uint8_t *payload;
};

static inline uint16_t seyrix_settings_read_u16(const uint8_t *bytes) {
	return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static inline uint32_t seyrix_settings_read_u32(const uint8_t *bytes) {
	return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
		((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static inline uint64_t seyrix_settings_read_u64(const uint8_t *bytes) {
	return ((uint64_t)seyrix_settings_read_u32(bytes) << 32) |
		seyrix_settings_read_u32(bytes + 4);
}

static inline void seyrix_settings_write_u16(uint8_t *bytes, uint16_t value) {
	bytes[0] = (uint8_t)(value >> 8);
	bytes[1] = (uint8_t)value;
}

static inline void seyrix_settings_write_u32(uint8_t *bytes, uint32_t value) {
	bytes[0] = (uint8_t)(value >> 24);
	bytes[1] = (uint8_t)(value >> 16);
	bytes[2] = (uint8_t)(value >> 8);
	bytes[3] = (uint8_t)value;
}

static inline void seyrix_settings_write_u64(uint8_t *bytes, uint64_t value) {
	seyrix_settings_write_u32(bytes, (uint32_t)(value >> 32));
	seyrix_settings_write_u32(bytes + 4, (uint32_t)value);
}

static inline bool seyrix_settings_decode_frame(const uint8_t *packet,
		size_t packet_size, struct seyrix_settings_frame *frame) {
	if (packet == NULL || frame == NULL ||
			packet_size < SEYRIX_SETTINGS_HEADER_SIZE ||
			packet_size > SEYRIX_SETTINGS_MAX_PACKET_SIZE ||
			packet[0] != SEYRIX_SETTINGS_MAGIC_0 ||
			packet[1] != SEYRIX_SETTINGS_MAGIC_1 ||
			packet[2] != SEYRIX_SETTINGS_MAGIC_2 ||
			packet[3] != SEYRIX_SETTINGS_MAGIC_3) {
		return false;
	}
	uint32_t payload_size = seyrix_settings_read_u32(packet + 12);
	if (payload_size > SEYRIX_SETTINGS_MAX_PACKET_SIZE -
			SEYRIX_SETTINGS_HEADER_SIZE ||
		packet_size != SEYRIX_SETTINGS_HEADER_SIZE + payload_size) {
		return false;
	}
	*frame = (struct seyrix_settings_frame){
		.frame_version = packet[4],
		.flags = packet[5],
		.opcode = seyrix_settings_read_u16(packet + 6),
		.request_id = seyrix_settings_read_u32(packet + 8),
		.payload_size = payload_size,
		.revision = seyrix_settings_read_u64(packet + 16),
		.payload = packet + SEYRIX_SETTINGS_HEADER_SIZE,
	};
	return true;
}

static inline bool seyrix_settings_encode_header(uint8_t *packet,
		size_t capacity, uint16_t opcode, uint32_t request_id,
		uint32_t payload_size, uint64_t revision) {
	if (packet == NULL || capacity < SEYRIX_SETTINGS_HEADER_SIZE ||
			payload_size > SEYRIX_SETTINGS_MAX_PACKET_SIZE -
				SEYRIX_SETTINGS_HEADER_SIZE ||
			capacity < SEYRIX_SETTINGS_HEADER_SIZE + payload_size) {
		return false;
	}
	packet[0] = SEYRIX_SETTINGS_MAGIC_0;
	packet[1] = SEYRIX_SETTINGS_MAGIC_1;
	packet[2] = SEYRIX_SETTINGS_MAGIC_2;
	packet[3] = SEYRIX_SETTINGS_MAGIC_3;
	packet[4] = SEYRIX_SETTINGS_FRAME_VERSION;
	packet[5] = 0;
	seyrix_settings_write_u16(packet + 6, opcode);
	seyrix_settings_write_u32(packet + 8, request_id);
	seyrix_settings_write_u32(packet + 12, payload_size);
	seyrix_settings_write_u64(packet + 16, revision);
	return true;
}

#endif
