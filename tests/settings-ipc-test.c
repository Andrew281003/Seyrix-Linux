#include "../desktop/common/settings-ipc.h"

#include <glib.h>
#include <string.h>

static void test_header_roundtrip(void) {
	uint8_t packet[SEYRIX_SETTINGS_HEADER_SIZE + 3] = {0};
	const uint8_t body[] = {0x00, 0x7f, 0xff};
	g_assert_true(seyrix_settings_encode_header(packet, sizeof(packet),
		SEYRIX_SETTINGS_OP_GET_SNAPSHOT, 0x10203040, sizeof(body),
		UINT64_C(0x0102030405060708)));
	memcpy(packet + SEYRIX_SETTINGS_HEADER_SIZE, body, sizeof(body));
	struct seyrix_settings_frame frame;
	g_assert_true(seyrix_settings_decode_frame(packet, sizeof(packet), &frame));
	g_assert_cmpuint(frame.frame_version, ==, SEYRIX_SETTINGS_FRAME_VERSION);
	g_assert_cmpuint(frame.opcode, ==, SEYRIX_SETTINGS_OP_GET_SNAPSHOT);
	g_assert_cmpuint(frame.request_id, ==, 0x10203040);
	g_assert_cmpuint(frame.payload_size, ==, sizeof(body));
	g_assert_cmpuint(frame.revision, ==, UINT64_C(0x0102030405060708));
	g_assert_cmpmem(frame.payload, frame.payload_size, body, sizeof(body));
}

static void test_header_rejects_invalid_frames(void) {
	uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE + 1] = {0};
	struct seyrix_settings_frame frame;
	g_assert_false(seyrix_settings_decode_frame(NULL, 0, &frame));
	g_assert_false(seyrix_settings_decode_frame(packet,
		SEYRIX_SETTINGS_HEADER_SIZE - 1, &frame));
	g_assert_false(seyrix_settings_decode_frame(packet,
		SEYRIX_SETTINGS_MAX_PACKET_SIZE + 1, &frame));
	packet[0] = 'X';
	g_assert_false(seyrix_settings_decode_frame(packet,
		SEYRIX_SETTINGS_HEADER_SIZE, &frame));
	packet[0] = SEYRIX_SETTINGS_MAGIC_0;
	packet[1] = SEYRIX_SETTINGS_MAGIC_1;
	packet[2] = SEYRIX_SETTINGS_MAGIC_2;
	packet[3] = SEYRIX_SETTINGS_MAGIC_3;
	seyrix_settings_write_u32(packet + 12, 1);
	g_assert_false(seyrix_settings_decode_frame(packet,
		SEYRIX_SETTINGS_HEADER_SIZE, &frame));
	g_assert_false(seyrix_settings_encode_header(packet,
		SEYRIX_SETTINGS_HEADER_SIZE, SEYRIX_SETTINGS_OP_GET_SNAPSHOT,
		1, SEYRIX_SETTINGS_MAX_PACKET_SIZE, 0));
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/settings-ipc/header-roundtrip", test_header_roundtrip);
	g_test_add_func("/settings-ipc/reject-invalid-frames",
		test_header_rejects_invalid_frames);
	return g_test_run();
}
