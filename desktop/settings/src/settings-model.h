#ifndef SEYRIX_SETTINGS_MODEL_H
#define SEYRIX_SETTINGS_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/settings-ipc.h"

struct seyrix_settings_workspace {
	uint8_t index; /* 0 is the default; 1..4 are concrete workspaces. */
	uint8_t mode;
	uint8_t orientation;
	int32_t inner_gap;
	int32_t outer_gap;
	uint32_t split_ratio;
};

struct seyrix_settings_output {
	char name[SEYRIX_SETTINGS_MAX_OUTPUT_NAME_BYTES + 1];
	bool enabled;
	uint32_t width;
	uint32_t height;
	uint32_t scale_milli;
	int32_t x;
	int32_t y;
};

struct seyrix_settings_snapshot {
	uint64_t revision;
	uint8_t active_workspace;
	uint8_t workspace_count;
	uint8_t shell_mode;
	uint8_t shell_position;
	uint8_t shell_alignment;
	uint8_t shell_icon_size;
	bool shell_auto_hide;
	char wallpaper[SEYRIX_SETTINGS_MAX_WALLPAPER_BYTES + 1];
	char background_color[8];
	char launcher_shortcut[32];
	char switcher_shortcut[32];
	bool switcher_active_workspace_only;
	char pinned_apps[SEYRIX_SETTINGS_MAX_PINNED_APPS]
		[SEYRIX_SETTINGS_MAX_DESKTOP_ID_BYTES + 1];
	size_t pinned_count;
	struct seyrix_settings_workspace workspaces[5];
	bool workspace_seen[5];
	uint16_t app_rule_count;
	struct seyrix_settings_output outputs[SEYRIX_SETTINGS_MAX_OUTPUTS];
	size_t output_count;
	uint16_t keyboard_count;
	bool has_keyboard_count;
};

bool seyrix_settings_snapshot_parse(const uint8_t *payload,
	size_t payload_size, uint64_t revision,
	struct seyrix_settings_snapshot *snapshot);

#endif
