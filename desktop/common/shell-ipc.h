#ifndef SEYRIX_SHELL_IPC_H
#define SEYRIX_SHELL_IPC_H

#include <stdint.h>

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

#endif
