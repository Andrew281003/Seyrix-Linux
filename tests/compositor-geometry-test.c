#define WLR_USE_UNSTABLE

#include <math.h>
#include <stdio.h>

#include <wayland-server-core.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

#include "../desktop/compositor/src/geometry.h"

static int failures;

#define CHECK(condition, description) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: %s\n", description); \
		failures++; \
	} \
} while (0)

static void check_resize(uint32_t edges, double start_x, double start_y,
		double cursor_x, double cursor_y, int expected_x, int expected_y,
		int expected_width, int expected_height, const char *name) {
	struct wlr_box initial = { .x = 100, .y = 100, .width = 200, .height = 120 };
	double grab_x;
	double grab_y;
	struct seyrix_resize_geometry result;
	if (!seyrix_resize_grab_offsets(&initial, edges, start_x, start_y,
			&grab_x, &grab_y)) {
		CHECK(false, "valid resize edge should create grab offsets");
		return;
	}
	if (!seyrix_calculate_resize(&initial, cursor_x, cursor_y,
			grab_x, grab_y, edges, 1, 1, 0, 0, 5, 7, 24, &result)) {
		CHECK(false, "valid resize edge should calculate geometry");
		return;
	}
	if (result.client_x != expected_x || result.client_y != expected_y ||
			result.width != expected_width || result.height != expected_height ||
			result.scene_x != expected_x - 5 ||
			result.scene_y != expected_y - 7 - 24) {
		fprintf(stderr,
			"FAIL: %s got client=%d,%d %dx%d scene=%d,%d\n",
			name, result.client_x, result.client_y, result.width,
			result.height, result.scene_x, result.scene_y);
		failures++;
	}
}

static void test_resize_edges_and_corners(void) {
	check_resize(WLR_EDGE_LEFT, 100, 150, 50, 150, 50, 100, 250, 120,
		"left edge keeps right anchor");
	check_resize(WLR_EDGE_RIGHT, 300, 150, 350, 150, 100, 100, 250, 120,
		"right edge keeps left anchor");
	check_resize(WLR_EDGE_TOP, 200, 100, 200, 80, 100, 80, 200, 140,
		"top edge keeps bottom anchor");
	check_resize(WLR_EDGE_BOTTOM, 200, 220, 200, 250, 100, 100, 200, 150,
		"bottom edge keeps top anchor");
	check_resize(WLR_EDGE_TOP | WLR_EDGE_LEFT, 100, 100, 50, 80,
		50, 80, 250, 140, "top-left corner");
	check_resize(WLR_EDGE_TOP | WLR_EDGE_RIGHT, 300, 100, 350, 80,
		100, 80, 250, 140, "top-right corner");
	check_resize(WLR_EDGE_BOTTOM | WLR_EDGE_LEFT, 100, 220, 50, 250,
		50, 100, 250, 150, "bottom-left corner");
	check_resize(WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT, 300, 220, 350, 250,
		100, 100, 250, 150, "bottom-right corner");
}

static void test_resize_constraints_preserve_anchors(void) {
	struct wlr_box initial = { .x = 100, .y = 100, .width = 200, .height = 120 };
	struct seyrix_resize_geometry result;
	double grab_x;
	double grab_y;

	CHECK(seyrix_resize_grab_offsets(&initial, WLR_EDGE_LEFT, 100, 150,
		&grab_x, &grab_y), "left edge offsets for minimum test");
	CHECK(seyrix_calculate_resize(&initial, 250, 150, grab_x, grab_y,
		WLR_EDGE_LEFT, 80, 1, 0, 0, 0, 0, 0, &result),
		"minimum width resize calculation");
	CHECK(result.client_x == 220 && result.width == 80 &&
		result.client_x + result.width == 300,
		"minimum width keeps the opposite right edge anchored");

	CHECK(seyrix_resize_grab_offsets(&initial, WLR_EDGE_RIGHT, 300, 150,
		&grab_x, &grab_y), "right edge offsets for maximum test");
	CHECK(seyrix_calculate_resize(&initial, 500, 150, grab_x, grab_y,
		WLR_EDGE_RIGHT, 1, 1, 250, 0, 0, 0, 0, &result),
		"maximum width resize calculation");
	CHECK(result.client_x == 100 && result.width == 250 &&
		result.client_x + result.width == 350,
		"maximum width keeps the opposite left edge anchored");

	CHECK(seyrix_resize_grab_offsets(&initial, WLR_EDGE_TOP, 200, 100,
		&grab_x, &grab_y), "top edge offsets for minimum test");
	CHECK(seyrix_calculate_resize(&initial, 200, 200, grab_x, grab_y,
		WLR_EDGE_TOP, 1, 50, 0, 0, 0, 0, 0, &result),
		"minimum height resize calculation");
	CHECK(result.client_y == 170 && result.height == 50 &&
		result.client_y + result.height == 220,
		"minimum height keeps the opposite bottom edge anchored");

	CHECK(seyrix_resize_grab_offsets(&initial, WLR_EDGE_BOTTOM, 200, 220,
		&grab_x, &grab_y), "bottom edge offsets for maximum test");
	CHECK(seyrix_calculate_resize(&initial, 200, 300, grab_x, grab_y,
		WLR_EDGE_BOTTOM, 1, 1, 0, 160, 0, 0, 0, &result),
		"maximum height resize calculation");
	CHECK(result.client_y == 100 && result.height == 160 &&
		result.client_y + result.height == 260,
		"maximum height keeps the opposite top edge anchored");

	CHECK(seyrix_resize_constrain_dimension(20, 80, 50) == 80,
		"minimum takes precedence for contradictory client constraints");

	CHECK(!seyrix_resize_grab_offsets(&initial, 0, 100, 100,
		&grab_x, &grab_y), "no resize edge is rejected");
}

static void test_wlroots_absolute_pointer_transform(void) {
	struct wl_display *display = wl_display_create();
	CHECK(display != NULL, "create Wayland display for headless cursor test");
	if (display == NULL) {
		return;
	}
	struct wlr_backend *backend = wlr_headless_backend_create(
		wl_display_get_event_loop(display));
	CHECK(backend != NULL, "create headless backend");
	if (backend == NULL) {
		wl_display_destroy(display);
		return;
	}
	struct wlr_renderer *renderer = wlr_pixman_renderer_create();
	CHECK(renderer != NULL, "create headless renderer");
	if (renderer == NULL) {
		wlr_backend_destroy(backend);
		wl_display_destroy(display);
		return;
	}
	struct wlr_allocator *allocator = wlr_allocator_autocreate(backend, renderer);
	CHECK(allocator != NULL, "create headless allocator");
	if (allocator == NULL) {
		wlr_renderer_destroy(renderer);
		wlr_backend_destroy(backend);
		wl_display_destroy(display);
		return;
	}
	struct wlr_output *output = wlr_headless_add_output(backend, 640, 480);
	CHECK(output != NULL, "create headless 640x480 output");
	if (output != NULL) {
		CHECK(wlr_output_init_render(output, allocator, renderer),
			"initialize headless output rendering");
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, true);
		struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
		if (mode != NULL) {
			wlr_output_state_set_mode(&state, mode);
		}
		CHECK(wlr_output_commit_state(output, &state),
			"enable headless output");
		wlr_output_state_finish(&state);
	}
	struct wlr_output_layout *layout = wlr_output_layout_create(display);
	CHECK(layout != NULL, "create output layout");
	struct wlr_cursor *cursor = wlr_cursor_create();
	CHECK(cursor != NULL, "create cursor");
	if (output == NULL || layout == NULL || cursor == NULL) {
		if (cursor != NULL) {
			wlr_cursor_destroy(cursor);
		}
		if (layout != NULL) {
			wlr_output_layout_destroy(layout);
		}
		wlr_allocator_destroy(allocator);
		wlr_renderer_destroy(renderer);
		wlr_backend_destroy(backend);
		wl_display_destroy(display);
		return;
	}
	wlr_output_layout_add(layout, output, -100, 20);
	wlr_cursor_attach_output_layout(cursor, layout);
	CHECK(seyrix_cursor_warp_absolute(cursor, NULL, 0.25, 0.5),
		"warp normalized absolute coordinates through wlroots");
	CHECK(fabs(cursor->x - 60.0) < 0.01 && fabs(cursor->y - 260.0) < 0.01,
		"absolute coordinates map to global scene coordinates on a positioned output");
	CHECK(!seyrix_cursor_warp_absolute(cursor, NULL, 1.1, 0.5),
		"out-of-range absolute coordinates are rejected");

	wlr_cursor_destroy(cursor);
	wlr_output_layout_destroy(layout);
	wlr_allocator_destroy(allocator);
	wlr_renderer_destroy(renderer);
	wlr_backend_destroy(backend);
	wl_display_destroy(display);
}

int main(void) {
	test_resize_edges_and_corners();
	test_resize_constraints_preserve_anchors();
	test_wlroots_absolute_pointer_transform();
	if (failures != 0) {
		fprintf(stderr, "%d compositor geometry check(s) failed\n", failures);
		return 1;
	}
	puts("COMPOSITOR_GEOMETRY=passed");
	return 0;
}
