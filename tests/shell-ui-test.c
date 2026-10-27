#include "shell-ui.h"

#include <glib.h>
#include <stdio.h>

static void test_desktop_manual_item_and_wallpaper_fallback(void) {
	struct seyrix_shell_config config;
	seyrix_shell_config_defaults(&config);
	g_strlcpy(config.wallpaper, "/tmp/seyrix-wallpaper-that-does-not-exist.png",
		sizeof(config.wallpaper));
	struct seyrix_shell_state state;
	seyrix_shell_state_init(&state);

	struct seyrix_shell_view view = {
		.kind = SEYRIX_SHELL_VIEW_DESKTOP,
		.width = 800,
		.height = 600,
		.scale = 1,
		.config = &config,
		.state = &state,
	};
	cairo_surface_t *image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		view.width, view.height);
	cairo_t *cr = cairo_create(image);
	struct seyrix_shell_hit_area hits[SEYRIX_SHELL_HIT_MAX];
	size_t count = 0;
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING,
		"*Wallpaper could not be loaded; using the configured solid color*");
	seyrix_shell_ui_draw(&view, cr, hits, G_N_ELEMENTS(hits), &count);
	g_test_assert_expected_messages();
	g_assert_cmpint(cairo_status(cr), ==, CAIRO_STATUS_SUCCESS);
	g_assert_cmpuint(count, ==, 1);
	g_assert_cmpint(hits[0].kind, ==, SEYRIX_SHELL_HIT_MANUAL);
	g_assert_cmpint(hits[0].x, >=, 0);
	g_assert_cmpint(hits[0].y, >=, 0);
	g_assert_cmpint(hits[0].x + hits[0].width, <=, view.width);
	g_assert_cmpint(hits[0].y + hits[0].height, <=, view.height);
	cairo_destroy(cr);
	cairo_surface_destroy(image);
}

static void test_output_resize_and_switcher_scroll_window(void) {
	struct seyrix_shell_config config;
	seyrix_shell_config_defaults(&config);
	struct seyrix_shell_state state;
	seyrix_shell_state_init(&state);
	for (uint32_t id = 1; id <= 40; id++) {
		char app_id[32];
		(void)snprintf(app_id, sizeof(app_id), "org.example.App%u", id);
		g_assert_true(seyrix_shell_state_upsert(&state, id, 0,
			SEYRIX_SHELL_WINDOW_MAPPED, app_id, "Example window"));
	}
	struct seyrix_shell_view view = {
		.kind = SEYRIX_SHELL_VIEW_SWITCHER,
		.width = 640,
		.height = 480,
		.scale = 1,
		.config = &config,
		.state = &state,
		.selected_index = 39,
	};
	cairo_surface_t *image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		view.width, view.height);
	cairo_t *cr = cairo_create(image);
	struct seyrix_shell_hit_area hits[SEYRIX_SHELL_HIT_MAX];
	size_t count = 0;
	seyrix_shell_ui_draw(&view, cr, hits, G_N_ELEMENTS(hits), &count);
	g_assert_cmpint(cairo_status(cr), ==, CAIRO_STATUS_SUCCESS);
	bool selected_is_visible = false;
	for (size_t index = 0; index < count; index++) {
		if (hits[index].kind == SEYRIX_SHELL_HIT_WINDOW && hits[index].id == 1)
			selected_is_visible = true;
	}
	g_assert_true(selected_is_visible);
	cairo_destroy(cr);
	cairo_surface_destroy(image);

	view.width = 360;
	view.height = 240;
	view.kind = SEYRIX_SHELL_VIEW_PANEL;
	image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		view.width, view.height);
	cr = cairo_create(image);
	seyrix_shell_ui_draw(&view, cr, hits, G_N_ELEMENTS(hits), &count);
	g_assert_cmpint(cairo_status(cr), ==, CAIRO_STATUS_SUCCESS);
	for (size_t index = 0; index < count; index++) {
		g_assert_cmpint(hits[index].x, >=, 0);
		g_assert_cmpint(hits[index].x + hits[index].width, <=, view.width);
	}
	cairo_destroy(cr);
	cairo_surface_destroy(image);

	view.kind = SEYRIX_SHELL_VIEW_PANEL;
	view.width = 1280;
	view.height = 720;
	image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		view.width, view.height);
	cr = cairo_create(image);
	seyrix_shell_ui_draw(&view, cr, hits, G_N_ELEMENTS(hits), &count);
	g_assert_cmpint(cairo_status(cr), ==, CAIRO_STATUS_SUCCESS);
	g_assert_cmpuint(count, >=, 5);
	for (size_t index = 0; index < count; index++) {
		g_assert_cmpint(hits[index].x, >=, 0);
		g_assert_cmpint(hits[index].x + hits[index].width, <=, view.width);
		g_assert_cmpint(hits[index].y + hits[index].height, <=, view.height);
	}
	cairo_destroy(cr);
	cairo_surface_destroy(image);

	struct seyrix_shell_state empty_state;
	seyrix_shell_state_init(&empty_state);
	view.width = 640;
	view.height = 480;
	view.state = &empty_state;
	image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		view.width, view.height);
	cr = cairo_create(image);
	seyrix_shell_ui_draw(&view, cr, hits, G_N_ELEMENTS(hits), &count);
	g_assert_cmpuint(count, ==, 5);
	cairo_destroy(cr);
	cairo_surface_destroy(image);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/shell/ui/desktop-manual-and-wallpaper-fallback",
		test_desktop_manual_item_and_wallpaper_fallback);
	g_test_add_func("/shell/ui/resize-and-switcher-selection-visibility",
		test_output_resize_and_switcher_scroll_window);
	return g_test_run();
}
