#include "settings-model.h"

#include <string.h>

#include <glib.h>

enum singleton_field {
	FIELD_ACTIVE_WORKSPACE = 1U << 0,
	FIELD_WORKSPACE_COUNT = 1U << 1,
	FIELD_SHELL_MODE = 1U << 2,
	FIELD_SHELL_POSITION = 1U << 3,
	FIELD_SHELL_ALIGNMENT = 1U << 4,
	FIELD_SHELL_ICON_SIZE = 1U << 5,
	FIELD_SHELL_AUTO_HIDE = 1U << 6,
	FIELD_WALLPAPER = 1U << 7,
	FIELD_BACKGROUND_COLOR = 1U << 8,
	FIELD_LAUNCHER_SHORTCUT = 1U << 9,
	FIELD_SWITCHER_SHORTCUT = 1U << 10,
	FIELD_SWITCHER_SCOPE = 1U << 11,
	FIELD_APP_RULE_COUNT = 1U << 12,
	FIELD_KEYBOARD_COUNT = 1U << 13,
};

static bool copy_text(char *destination, size_t capacity,
		const uint8_t *source, size_t length, bool require_utf8) {
	if (length >= capacity || (length > 0 && memchr(source, '\0', length) != NULL)) {
		return false;
	}
	if (require_utf8 && !g_utf8_validate((const char *)source,
			(gssize)length, NULL)) {
		return false;
	}
	if (length > 0) {
		memcpy(destination, source, length);
	}
	destination[length] = '\0';
	return true;
}

static bool mark_singleton(uint32_t *seen, enum singleton_field field) {
	if ((*seen & (uint32_t)field) != 0) {
		return false;
	}
	*seen |= (uint32_t)field;
	return true;
}

static bool parse_workspace(const uint8_t *value, uint16_t length,
		struct seyrix_settings_snapshot *snapshot) {
	if (length != 16 || value[0] > 4 || value[1] > 2 || value[2] > 2 ||
			value[3] != 0) {
		return false;
	}
	uint8_t index = value[0];
	if (snapshot->workspace_seen[index]) {
		return false;
	}
	uint32_t inner_gap = seyrix_settings_read_u32(value + 4);
	uint32_t outer_gap = seyrix_settings_read_u32(value + 8);
	uint32_t split_ratio = seyrix_settings_read_u32(value + 12);
	if (inner_gap > 128 || outer_gap > 128 || split_ratio < 50 ||
			split_ratio > 950) {
		return false;
	}
	snapshot->workspaces[index] = (struct seyrix_settings_workspace){
		.index = index,
		.mode = value[1],
		.orientation = value[2],
		.inner_gap = (int32_t)inner_gap,
		.outer_gap = (int32_t)outer_gap,
		.split_ratio = split_ratio,
	};
	snapshot->workspace_seen[index] = true;
	return true;
}

static bool parse_output(const uint8_t *value, uint16_t length,
		struct seyrix_settings_snapshot *snapshot) {
	if (length < 24 || snapshot->output_count >= SEYRIX_SETTINGS_MAX_OUTPUTS ||
			value[1] > 1 || value[2] != 0 || value[3] != 0 ||
			value[0] > SEYRIX_SETTINGS_MAX_OUTPUT_NAME_BYTES ||
			length != (uint16_t)(24 + value[0]) || value[0] == 0) {
		return false;
	}
	struct seyrix_settings_output output = {0};
	output.enabled = value[1] != 0;
	output.width = seyrix_settings_read_u32(value + 4);
	output.height = seyrix_settings_read_u32(value + 8);
	output.scale_milli = seyrix_settings_read_u32(value + 12);
	output.x = (int32_t)seyrix_settings_read_u32(value + 16);
	output.y = (int32_t)seyrix_settings_read_u32(value + 20);
	if (output.scale_milli > 10000 ||
			!copy_text(output.name, sizeof(output.name), value + 24,
				value[0], true)) {
		return false;
	}
	for (size_t i = 0; i < snapshot->output_count; i++) {
		if (strcmp(snapshot->outputs[i].name, output.name) == 0) {
			return false;
		}
	}
	snapshot->outputs[snapshot->output_count++] = output;
	return true;
}

bool seyrix_settings_snapshot_parse(const uint8_t *payload,
		size_t payload_size, uint64_t revision,
		struct seyrix_settings_snapshot *snapshot) {
	if (snapshot == NULL || (payload == NULL && payload_size != 0) ||
			payload_size > SEYRIX_SETTINGS_MAX_PACKET_SIZE) {
		return false;
	}
	struct seyrix_settings_snapshot parsed = {0};
	parsed.revision = revision;
	uint32_t seen = 0;
	size_t offset = 0;
	while (offset < payload_size) {
		if (payload_size - offset < 4) {
			return false;
		}
		uint16_t type = seyrix_settings_read_u16(payload + offset);
		uint16_t length = seyrix_settings_read_u16(payload + offset + 2);
		offset += 4;
		if (length > payload_size - offset) {
			return false;
		}
		const uint8_t *value = payload + offset;
		bool valid = true;
		switch (type) {
		case SEYRIX_SETTINGS_TLV_ACTIVE_WORKSPACE:
			valid = length == 1 && value[0] > 0 &&
				value[0] <= SEYRIX_SETTINGS_WORKSPACE_COUNT &&
				mark_singleton(&seen, FIELD_ACTIVE_WORKSPACE);
			if (valid) parsed.active_workspace = value[0];
			break;
		case SEYRIX_SETTINGS_TLV_WORKSPACE_COUNT:
			valid = length == 1 && value[0] == SEYRIX_SETTINGS_WORKSPACE_COUNT &&
				mark_singleton(&seen, FIELD_WORKSPACE_COUNT);
			if (valid) parsed.workspace_count = value[0];
			break;
		case SEYRIX_SETTINGS_TLV_SHELL_MODE:
			valid = length == 1 && value[0] <= 1 &&
				mark_singleton(&seen, FIELD_SHELL_MODE);
			if (valid) parsed.shell_mode = value[0];
			break;
		case SEYRIX_SETTINGS_TLV_SHELL_POSITION:
			valid = length == 1 && value[0] <= 1 &&
				mark_singleton(&seen, FIELD_SHELL_POSITION);
			if (valid) parsed.shell_position = value[0];
			break;
		case SEYRIX_SETTINGS_TLV_SHELL_ALIGNMENT:
			valid = length == 1 && value[0] <= 2 &&
				mark_singleton(&seen, FIELD_SHELL_ALIGNMENT);
			if (valid) parsed.shell_alignment = value[0];
			break;
		case SEYRIX_SETTINGS_TLV_SHELL_ICON_SIZE:
			valid = length == 1 && value[0] >= 24 && value[0] <= 64 &&
				mark_singleton(&seen, FIELD_SHELL_ICON_SIZE);
			if (valid) parsed.shell_icon_size = value[0];
			break;
		case SEYRIX_SETTINGS_TLV_SHELL_AUTO_HIDE:
			valid = length == 1 && value[0] <= 1 &&
				mark_singleton(&seen, FIELD_SHELL_AUTO_HIDE);
			if (valid) parsed.shell_auto_hide = value[0] != 0;
			break;
		case SEYRIX_SETTINGS_TLV_SHELL_WALLPAPER:
			valid = mark_singleton(&seen, FIELD_WALLPAPER) &&
				copy_text(parsed.wallpaper, sizeof(parsed.wallpaper),
					value, length, false);
			break;
		case SEYRIX_SETTINGS_TLV_SHELL_BACKGROUND_COLOR:
			valid = length == 7 && value[0] == '#' &&
				mark_singleton(&seen, FIELD_BACKGROUND_COLOR) &&
				copy_text(parsed.background_color,
					sizeof(parsed.background_color), value, length, true);
			break;
		case SEYRIX_SETTINGS_TLV_PINNED_APP:
			valid = parsed.pinned_count < SEYRIX_SETTINGS_MAX_PINNED_APPS &&
				length > 0 && length <= SEYRIX_SETTINGS_MAX_DESKTOP_ID_BYTES &&
				copy_text(parsed.pinned_apps[parsed.pinned_count],
					sizeof(parsed.pinned_apps[0]), value, length, true);
			if (valid) parsed.pinned_count++;
			break;
		case SEYRIX_SETTINGS_TLV_LAUNCHER_SHORTCUT:
			valid = length <= 31 &&
				mark_singleton(&seen, FIELD_LAUNCHER_SHORTCUT) &&
				copy_text(parsed.launcher_shortcut,
					sizeof(parsed.launcher_shortcut), value, length, true);
			break;
		case SEYRIX_SETTINGS_TLV_SWITCHER_SHORTCUT:
			valid = length <= 31 &&
				mark_singleton(&seen, FIELD_SWITCHER_SHORTCUT) &&
				copy_text(parsed.switcher_shortcut,
					sizeof(parsed.switcher_shortcut), value, length, true);
			break;
		case SEYRIX_SETTINGS_TLV_SWITCHER_ACTIVE_WORKSPACE_ONLY:
			valid = length == 1 && value[0] <= 1 &&
				mark_singleton(&seen, FIELD_SWITCHER_SCOPE);
			if (valid) parsed.switcher_active_workspace_only = value[0] != 0;
			break;
		case SEYRIX_SETTINGS_TLV_WM_WORKSPACE:
			valid = parse_workspace(value, length, &parsed);
			break;
		case SEYRIX_SETTINGS_TLV_WM_APP_RULE_COUNT:
			valid = length == 2 &&
				mark_singleton(&seen, FIELD_APP_RULE_COUNT);
			if (valid) parsed.app_rule_count = seyrix_settings_read_u16(value);
			break;
		case SEYRIX_SETTINGS_TLV_OUTPUT:
			valid = parse_output(value, length, &parsed);
			break;
		case SEYRIX_SETTINGS_TLV_KEYBOARD_COUNT:
			valid = length == 2 &&
				mark_singleton(&seen, FIELD_KEYBOARD_COUNT);
			if (valid) {
				parsed.keyboard_count = seyrix_settings_read_u16(value);
				parsed.has_keyboard_count = true;
			}
			break;
		default:
			/* Unknown TLVs are skipped for compatible minor revisions. */
			break;
		}
		if (!valid) {
			return false;
		}
		offset += length;
	}
	uint32_t required = FIELD_ACTIVE_WORKSPACE | FIELD_WORKSPACE_COUNT |
		FIELD_SHELL_MODE | FIELD_SHELL_POSITION | FIELD_SHELL_ALIGNMENT |
		FIELD_SHELL_ICON_SIZE | FIELD_SHELL_AUTO_HIDE | FIELD_WALLPAPER |
		FIELD_BACKGROUND_COLOR | FIELD_LAUNCHER_SHORTCUT |
		FIELD_SWITCHER_SHORTCUT | FIELD_SWITCHER_SCOPE |
		FIELD_APP_RULE_COUNT | FIELD_KEYBOARD_COUNT;
	if ((seen & required) != required) {
		return false;
	}
	for (size_t i = 0; i < sizeof(parsed.workspace_seen) /
			sizeof(parsed.workspace_seen[0]); i++) {
		if (!parsed.workspace_seen[i]) {
			return false;
		}
	}
	*snapshot = parsed;
	return true;
}
