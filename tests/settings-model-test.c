#include "../desktop/settings/src/settings-model.h"

#include <glib.h>

static void add_tlv(GByteArray *bytes, uint16_t type,
		const uint8_t *value, uint16_t length) {
	uint8_t header[4];
	seyrix_settings_write_u16(header, type);
	seyrix_settings_write_u16(header + 2, length);
	g_byte_array_append(bytes, header, sizeof(header));
	if (length > 0) {
		g_byte_array_append(bytes, value, length);
	}
}

static void add_u8(GByteArray *bytes, uint16_t type, uint8_t value) {
	add_tlv(bytes, type, &value, 1);
}

static void add_u16(GByteArray *bytes, uint16_t type, uint16_t value) {
	uint8_t encoded[2];
	seyrix_settings_write_u16(encoded, value);
	add_tlv(bytes, type, encoded, sizeof(encoded));
}

static void add_workspace(GByteArray *bytes, uint8_t index, uint8_t mode,
		uint8_t orientation) {
	uint8_t encoded[16] = {0};
	encoded[0] = index;
	encoded[1] = mode;
	encoded[2] = orientation;
	seyrix_settings_write_u32(encoded + 4, 8);
	seyrix_settings_write_u32(encoded + 8, 16);
	seyrix_settings_write_u32(encoded + 12, 500);
	add_tlv(bytes, SEYRIX_SETTINGS_TLV_WM_WORKSPACE, encoded,
		sizeof(encoded));
}

static GByteArray *make_valid_snapshot(void) {
	GByteArray *bytes = g_byte_array_new();
	add_u8(bytes, SEYRIX_SETTINGS_TLV_ACTIVE_WORKSPACE, 2);
	add_u8(bytes, SEYRIX_SETTINGS_TLV_WORKSPACE_COUNT, 4);
	add_u8(bytes, SEYRIX_SETTINGS_TLV_SHELL_MODE, 1);
	add_u8(bytes, SEYRIX_SETTINGS_TLV_SHELL_POSITION, 0);
	add_u8(bytes, SEYRIX_SETTINGS_TLV_SHELL_ALIGNMENT, 2);
	add_u8(bytes, SEYRIX_SETTINGS_TLV_SHELL_ICON_SIZE, 32);
	add_u8(bytes, SEYRIX_SETTINGS_TLV_SHELL_AUTO_HIDE, 0);
	const uint8_t wallpaper[] = "/home/seyrix/wallpaper.png";
	add_tlv(bytes, SEYRIX_SETTINGS_TLV_SHELL_WALLPAPER, wallpaper,
		sizeof(wallpaper) - 1);
	const uint8_t color[] = "#121a22";
	add_tlv(bytes, SEYRIX_SETTINGS_TLV_SHELL_BACKGROUND_COLOR, color,
		sizeof(color) - 1);
	const uint8_t launcher[] = "Super_L";
	add_tlv(bytes, SEYRIX_SETTINGS_TLV_LAUNCHER_SHORTCUT, launcher,
		sizeof(launcher) - 1);
	const uint8_t switcher[] = "Alt+Tab";
	add_tlv(bytes, SEYRIX_SETTINGS_TLV_SWITCHER_SHORTCUT, switcher,
		sizeof(switcher) - 1);
	add_u8(bytes, SEYRIX_SETTINGS_TLV_SWITCHER_ACTIVE_WORKSPACE_ONLY, 1);
	for (uint8_t index = 0; index <= 4; index++) {
		add_workspace(bytes, index, index == 0 ? 2 : 1, 0);
	}
	add_u16(bytes, SEYRIX_SETTINGS_TLV_WM_APP_RULE_COUNT, 7);
	const uint8_t output[] = {
		3, 1, 0, 0, 0, 0, 0x05, 0, 0, 0, 0x02, 0xd0,
		0, 0, 0x03, 0xe8, 0xff, 0xff, 0xff, 0xf6, 0, 0, 0, 0,
		'V', 'D', '1',
	};
	add_tlv(bytes, SEYRIX_SETTINGS_TLV_OUTPUT, output, sizeof(output));
	add_u16(bytes, SEYRIX_SETTINGS_TLV_KEYBOARD_COUNT, 2);
	const uint8_t extension[] = {1, 2, 3};
	add_tlv(bytes, 0x7fff, extension, sizeof(extension));
	return bytes;
}

static void test_snapshot_parse_and_unknown_tlv(void) {
	GByteArray *bytes = make_valid_snapshot();
	struct seyrix_settings_snapshot snapshot;
	g_assert_true(seyrix_settings_snapshot_parse(bytes->data, bytes->len,
		UINT64_C(0x1234), &snapshot));
	g_assert_cmpuint(snapshot.revision, ==, UINT64_C(0x1234));
	g_assert_cmpuint(snapshot.active_workspace, ==, 2);
	g_assert_cmpuint(snapshot.workspace_count, ==, 4);
	g_assert_cmpstr(snapshot.wallpaper, ==, "/home/seyrix/wallpaper.png");
	g_assert_cmpstr(snapshot.background_color, ==, "#121a22");
	g_assert_cmpuint(snapshot.pinned_count, ==, 0);
	g_assert_cmpuint(snapshot.workspaces[0].mode, ==, 2);
	g_assert_cmpuint(snapshot.app_rule_count, ==, 7);
	g_assert_cmpuint(snapshot.output_count, ==, 1);
	g_assert_cmpstr(snapshot.outputs[0].name, ==, "VD1");
	g_assert_cmpuint(snapshot.outputs[0].width, ==, 1280);
	g_assert_cmpint(snapshot.outputs[0].x, ==, -10);
	g_assert_cmpuint(snapshot.keyboard_count, ==, 2);
	g_byte_array_unref(bytes);
}

static void test_snapshot_rejects_duplicate_and_incomplete(void) {
	GByteArray *bytes = make_valid_snapshot();
	const uint8_t duplicate_active = 1;
	add_tlv(bytes, SEYRIX_SETTINGS_TLV_ACTIVE_WORKSPACE,
		&duplicate_active, 1);
	struct seyrix_settings_snapshot snapshot;
	g_assert_false(seyrix_settings_snapshot_parse(bytes->data, bytes->len,
		1, &snapshot));
	g_byte_array_unref(bytes);

	bytes = g_byte_array_new();
	add_u8(bytes, SEYRIX_SETTINGS_TLV_ACTIVE_WORKSPACE, 1);
	g_assert_false(seyrix_settings_snapshot_parse(bytes->data, bytes->len,
		1, &snapshot));
	g_byte_array_unref(bytes);
}

static void test_snapshot_rejects_bad_values_and_truncation(void) {
	GByteArray *bytes = make_valid_snapshot();
	/* Active workspaces are numbered from one. */
	bytes->data[4] = 0;
	struct seyrix_settings_snapshot snapshot;
	g_assert_false(seyrix_settings_snapshot_parse(bytes->data, bytes->len,
		1, &snapshot));
	g_byte_array_unref(bytes);

	bytes = make_valid_snapshot();
	g_byte_array_set_size(bytes, bytes->len - 1);
	g_assert_false(seyrix_settings_snapshot_parse(bytes->data, bytes->len,
		1, &snapshot));
	g_byte_array_unref(bytes);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/settings-model/parse-and-skip-extension",
		test_snapshot_parse_and_unknown_tlv);
	g_test_add_func("/settings-model/reject-duplicate-or-incomplete",
		test_snapshot_rejects_duplicate_and_incomplete);
	g_test_add_func("/settings-model/reject-bad-values-and-truncation",
		test_snapshot_rejects_bad_values_and_truncation);
	return g_test_run();
}
