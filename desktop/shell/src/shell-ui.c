#include "shell-ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pango/pangocairo.h>

#define SEYRIX_PI 3.14159265358979323846

static GdkPixbuf *wallpaper;
static char wallpaper_path[SEYRIX_SHELL_PATH_MAX];
static cairo_user_data_key_t pixbuf_data_key;

static void rounded_rect(cairo_t *cr, double x, double y, double width,
		double height, double radius) {
	if (width <= 0 || height <= 0) return;
	if (radius > width / 2.0) radius = width / 2.0;
	if (radius > height / 2.0) radius = height / 2.0;
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + width - radius, y + radius, radius, -SEYRIX_PI / 2, 0);
	cairo_arc(cr, x + width - radius, y + height - radius, radius,
		0, SEYRIX_PI / 2);
	cairo_arc(cr, x + radius, y + height - radius, radius,
		SEYRIX_PI / 2, SEYRIX_PI);
	cairo_arc(cr, x + radius, y + radius, radius,
		SEYRIX_PI, 3 * SEYRIX_PI / 2);
	cairo_close_path(cr);
}

static void text(cairo_t *cr, const char *value, double x, double y,
		double size, double red, double green, double blue, double alpha,
		double max_width) {
	if (value == NULL) return;
	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *font = pango_font_description_new();
	pango_font_description_set_family(font, "Sans");
	pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
	pango_layout_set_font_description(layout, font);
	pango_layout_set_text(layout, value, -1);
	if (max_width > 0) {
		pango_layout_set_width(layout, (int)(max_width * PANGO_SCALE));
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
	}
	cairo_set_source_rgba(cr, red, green, blue, alpha);
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, layout);
	pango_font_description_free(font);
	g_object_unref(layout);
}

static void centered_text(cairo_t *cr, const char *value, double center_x,
		double y, double size, double red, double green, double blue,
		double alpha) {
	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *font = pango_font_description_new();
	pango_font_description_set_family(font, "Sans");
	pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
	pango_layout_set_font_description(layout, font);
	pango_layout_set_text(layout, value != NULL ? value : "", -1);
	int width = 0;
	pango_layout_get_pixel_size(layout, &width, NULL);
	cairo_set_source_rgba(cr, red, green, blue, alpha);
	cairo_move_to(cr, center_x - width / 2.0, y);
	pango_cairo_show_layout(cr, layout);
	pango_font_description_free(font);
	g_object_unref(layout);
}

static void color_from_hex(const char *hex, double *red, double *green,
		double *blue) {
	unsigned int r = 28, g = 34, b = 41;
	if (hex != NULL && strlen(hex) == 7 && hex[0] == '#') {
		(void)sscanf(hex + 1, "%2x%2x%2x", &r, &g, &b);
	}
	*red = r / 255.0;
	*green = g / 255.0;
	*blue = b / 255.0;
}

static void hit_add(struct seyrix_shell_hit_area *hits, size_t capacity,
		size_t *count, int x, int y, int width, int height,
		enum seyrix_shell_hit_kind kind, uint32_t id, const char *desktop_id) {
	if (hits == NULL || count == NULL || *count >= capacity || width <= 0 ||
			height <= 0) return;
	struct seyrix_shell_hit_area *hit = &hits[(*count)++];
	memset(hit, 0, sizeof(*hit));
	hit->x = x;
	hit->y = y;
	hit->width = width;
	hit->height = height;
	hit->kind = kind;
	hit->id = id;
	if (desktop_id != NULL) {
		(void)snprintf(hit->desktop_id, sizeof(hit->desktop_id), "%s", desktop_id);
	}
}

static cairo_surface_t *pixbuf_surface(GdkPixbuf *pixbuf) {
	if (pixbuf == NULL) return NULL;
	int width = gdk_pixbuf_get_width(pixbuf);
	int height = gdk_pixbuf_get_height(pixbuf);
	if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return NULL;
	size_t stride = (size_t)width * sizeof(uint32_t);
	uint32_t *pixels = g_malloc0(stride * (size_t)height);
	const guchar *source = gdk_pixbuf_get_pixels(pixbuf);
	int source_stride = gdk_pixbuf_get_rowstride(pixbuf);
	int channels = gdk_pixbuf_get_n_channels(pixbuf);
	bool has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
	for (int y = 0; y < height; y++) {
		const guchar *row = source + (size_t)y * (size_t)source_stride;
		for (int x = 0; x < width; x++) {
			const guchar *pixel = row + (size_t)x * (size_t)channels;
			uint32_t alpha = has_alpha ? pixel[3] : 255U;
			uint32_t red = ((uint32_t)pixel[0] * alpha + 127U) / 255U;
			uint32_t green = ((uint32_t)pixel[1] * alpha + 127U) / 255U;
			uint32_t blue = ((uint32_t)pixel[2] * alpha + 127U) / 255U;
			pixels[(size_t)y * (size_t)width + (size_t)x] =
				(alpha << 24) | (red << 16) | (green << 8) | blue;
		}
	}
	cairo_surface_t *surface = cairo_image_surface_create_for_data(
		(unsigned char *)pixels, CAIRO_FORMAT_ARGB32, width, height, (int)stride);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		g_free(pixels);
		return NULL;
	}
	/* Keep the converted pixel buffer alive for the lifetime of this surface. */
	if (cairo_surface_set_user_data(surface, &pixbuf_data_key,
			pixels, g_free) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		g_free(pixels);
		return NULL;
	}
	return surface;
}

static void load_wallpaper(const struct seyrix_shell_config *config) {
	const char *path = config != NULL ? config->wallpaper : "";
	if (strcmp(path, wallpaper_path) == 0) return;
	if (wallpaper != NULL) g_object_unref(wallpaper);
	wallpaper = NULL;
	(void)snprintf(wallpaper_path, sizeof(wallpaper_path), "%s", path);
	if (path[0] == '\0') return;
	GError *error = NULL;
	wallpaper = gdk_pixbuf_new_from_file_at_scale(path, 4096, 4096,
		TRUE, &error);
	if (wallpaper == NULL) {
		g_warning("Wallpaper could not be loaded; using the configured solid color: %s",
			error != NULL ? error->message : "invalid image");
		g_clear_error(&error);
	}
}

static void draw_background(const struct seyrix_shell_view *view,
		cairo_t *cr) {
	double red, green, blue;
	color_from_hex(view->config != NULL ? view->config->background_color : NULL,
		&red, &green, &blue);
	cairo_set_source_rgb(cr, red, green, blue);
	cairo_paint(cr);
	load_wallpaper(view->config);
	if (wallpaper == NULL) return;
	cairo_surface_t *image = pixbuf_surface(wallpaper);
	if (image == NULL) return;
	double source_width = cairo_image_surface_get_width(image);
	double source_height = cairo_image_surface_get_height(image);
	double scale_x = view->width / source_width;
	double scale_y = view->height / source_height;
	double image_scale = fmax(scale_x, scale_y);
	double image_width = source_width * image_scale;
	double image_height = source_height * image_scale;
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, view->width, view->height);
	cairo_clip(cr);
	cairo_translate(cr, (view->width - image_width) / 2.0,
		(view->height - image_height) / 2.0);
	cairo_scale(cr, image_scale, image_scale);
	cairo_set_source_surface(cr, image, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_surface_destroy(image);
}

static char *resolve_icon(const char *icon) {
	if (icon == NULL || icon[0] == '\0') return NULL;
	if (g_path_is_absolute(icon) && g_file_test(icon, G_FILE_TEST_IS_REGULAR)) {
		return g_strdup(icon);
	}
	char *base = g_strdup(icon);
	char *dot = strrchr(base, '.');
	if (dot != NULL) *dot = '\0';
	const char *roots[] = {
		"/usr/share/icons/hicolor/64x64/apps",
		"/usr/share/icons/hicolor/48x48/apps",
		"/usr/share/icons/hicolor/32x32/apps",
		"/usr/share/icons/hicolor/scalable/apps",
		"/usr/share/pixmaps",
	};
	const char *extensions[] = {"png", "svg", "xpm"};
	char *result = NULL;
	for (size_t root = 0; root < G_N_ELEMENTS(roots) && result == NULL; root++) {
		for (size_t extension = 0; extension < G_N_ELEMENTS(extensions); extension++) {
			char *candidate = g_strdup_printf("%s/%s.%s", roots[root], base,
				extensions[extension]);
			if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR)) {
				result = candidate;
				break;
			}
			g_free(candidate);
		}
	}
	g_free(base);
	return result;
}

static const seyrix_app_entry *app_for_id(const struct seyrix_shell_view *view,
		const char *desktop_id, const char *app_id) {
	if (view->catalog == NULL) return NULL;
	for (gsize index = 0; index < seyrix_app_catalog_count(view->catalog); index++) {
		const seyrix_app_entry *entry = seyrix_app_catalog_get(view->catalog, index);
		const char *id = seyrix_app_entry_id(entry);
		if (id != NULL && ((desktop_id != NULL && strcmp(id, desktop_id) == 0) ||
				(app_id != NULL && (strcmp(id, app_id) == 0 ||
				(g_str_has_suffix(id, ".desktop") &&
				strlen(id) == strlen(app_id) + strlen(".desktop") &&
				strncmp(id, app_id, strlen(app_id)) == 0))))) {
			return entry;
		}
	}
	return NULL;
}

static const char *application_name(const struct seyrix_shell_view *view,
		const struct seyrix_shell_application *application) {
	const seyrix_app_entry *entry = app_for_id(view, NULL,
		application->has_valid_app_id ? application->app_id : NULL);
	if (entry != NULL) return seyrix_app_entry_name(entry);
	if (application->title[0] != '\0') return application->title;
	return application->has_valid_app_id ? application->app_id : "Application";
}

static void draw_icon(cairo_t *cr, const struct seyrix_shell_view *view,
		const seyrix_app_entry *entry, const char *fallback, double x, double y,
		double size) {
	const char *icon = seyrix_app_entry_icon(entry);
	char *path = resolve_icon(icon);
	if (path != NULL) {
		GError *error = NULL;
		GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(path,
			(int)size, (int)size, TRUE, &error);
		if (pixbuf != NULL) {
			cairo_surface_t *surface = pixbuf_surface(pixbuf);
			if (surface != NULL) {
				cairo_save(cr);
				cairo_translate(cr, x, y);
				cairo_scale(cr, size / cairo_image_surface_get_width(surface),
					size / cairo_image_surface_get_height(surface));
				cairo_set_source_surface(cr, surface, 0, 0);
				cairo_paint(cr);
				cairo_restore(cr);
				cairo_surface_destroy(surface);
				g_object_unref(pixbuf);
				g_free(path);
				return;
			}
			g_object_unref(pixbuf);
		}
		g_clear_error(&error);
		g_free(path);
	}
	char *initial = NULL;
	if (fallback != NULL && fallback[0] != '\0') {
		const char *end = g_utf8_next_char(fallback);
		initial = g_strndup(fallback, (gsize)(end - fallback));
	}
	rounded_rect(cr, x, y, size, size, size * 0.22);
	cairo_set_source_rgba(cr, 0.23, 0.42, 0.58, 0.96);
	cairo_fill(cr);
	centered_text(cr, initial != NULL ? initial : "?", x + size / 2.0,
		y + size * 0.23, size * 0.42, 0.96, 0.98, 1.0, 1.0);
	g_free(initial);
	(void)view;
}

static void draw_manual_icon(cairo_t *cr, double x, double y, double size) {
	rounded_rect(cr, x, y, size * 0.78, size, size * 0.08);
	cairo_set_source_rgba(cr, 0.91, 0.94, 0.97, 0.96);
	cairo_fill(cr);
	for (int line = 0; line < 4; line++) {
		rounded_rect(cr, x + size * 0.13, y + size * (0.23 + 0.13 * line),
			size * 0.52, size * 0.035, size * 0.015);
		cairo_set_source_rgba(cr, 0.26, 0.43, 0.56, 0.9);
		cairo_fill(cr);
	}
}

static void draw_desktop(const struct seyrix_shell_view *view, cairo_t *cr,
		struct seyrix_shell_hit_area *hits, size_t capacity, size_t *count) {
	draw_background(view, cr);
	double icon_size = 54;
	double x = 34;
	double y = fmax(28, view->height - icon_size - 100);
	draw_manual_icon(cr, x, y, icon_size);
	centered_text(cr, "Manual.txt", x + icon_size * 0.39, y + icon_size + 7,
		13, 0.96, 0.97, 0.99, 1.0);
	hit_add(hits, capacity, count, (int)x - 10, (int)y - 10,
		(int)icon_size + 20, (int)icon_size + 43, SEYRIX_SHELL_HIT_MANUAL, 0, NULL);
}

static void draw_workspace_button(cairo_t *cr, int x, int y, int size,
		uint8_t workspace, uint8_t active, struct seyrix_shell_hit_area *hits,
		size_t capacity, size_t *count) {
	bool selected = workspace == active;
	rounded_rect(cr, x, y, size, size, 12);
	cairo_set_source_rgba(cr, selected ? 0.24 : 0.20,
		selected ? 0.43 : 0.27, selected ? 0.60 : 0.33, selected ? 1.0 : 0.95);
	cairo_fill(cr);
	char label[2] = {(char)('1' + workspace), '\0'};
	centered_text(cr, label, x + size / 2.0, y + size * 0.22,
		15, 0.96, 0.98, 1.0, 1.0);
	hit_add(hits, capacity, count, x, y, size, size,
		SEYRIX_SHELL_HIT_WORKSPACE, workspace, NULL);
}

static void draw_panel(const struct seyrix_shell_view *view, cairo_t *cr,
		struct seyrix_shell_hit_area *hits, size_t capacity, size_t *count) {
	int icon_size = view->config != NULL ? view->config->icon_size : 40;
	if (icon_size < 24 || icon_size > 64) icon_size = 40;
	int height = icon_size + 18;
	int y = view->config != NULL &&
		view->config->position == SEYRIX_SHELL_POSITION_TOP ? 0 : view->height - height;
	/* The background layer owns the full-output wallpaper; the panel is a
	 * narrow translucent surface with no full-width pointer input region. */
	int workspace_width = 4 * 30 + 3 * 6 + 18;
	int launcher_width = height;
	const char *pins[SEYRIX_SHELL_MAX_PINS];
	size_t pin_count = view->config != NULL ? view->config->pinned_count : 0;
	for (size_t index = 0; index < pin_count; index++) {
		pins[index] = view->config->pinned_apps[index];
	}
	struct seyrix_shell_application applications[SEYRIX_SHELL_MAX_APPLICATIONS];
	size_t app_count = view->state != NULL ? seyrix_shell_state_applications(
		view->state, pins, pin_count, applications,
		SEYRIX_SHELL_MAX_APPLICATIONS) : 0;
	bool dock = view->config == NULL || view->config->mode == SEYRIX_SHELL_MODE_DOCK;
	int slot_width = dock ? icon_size + 18 : 152;
	int available = view->width - 44 - workspace_width - launcher_width;
	int max_slots = slot_width > 0 && available > 0 ? available / slot_width : 0;
	size_t visible = (size_t)max_slots;
	if (visible > app_count) visible = app_count;
	int overflow_width = 0;
	if (app_count > visible && visible > 0) {
		int remainder = available - (int)visible * slot_width;
		if (remainder >= 51) {
			overflow_width = 51;
		} else if (visible > 1) {
			visible--;
			overflow_width = 51;
		}
	}
	int app_area = (int)visible * slot_width + overflow_width;
	int content_width = workspace_width + launcher_width + app_area;
	int alignment = view->config != NULL ? view->config->dock_alignment :
		SEYRIX_SHELL_ALIGNMENT_CENTER;
	int left;
	if (!dock || alignment == SEYRIX_SHELL_ALIGNMENT_CENTER) {
		left = (view->width - content_width) / 2;
	} else if (alignment == SEYRIX_SHELL_ALIGNMENT_END) {
		left = view->width - content_width - 20;
	} else {
		left = 20;
	}
	if (left < 8) left = 8;
	if (content_width > view->width - 16) content_width = view->width - 16;
	rounded_rect(cr, left, y + 3, content_width, height - 6, 15);
	cairo_set_source_rgba(cr, 0.10, 0.14, 0.19, 0.93);
	cairo_fill(cr);
	rounded_rect(cr, left + 0.5, y + 3.5, content_width - 1,
		height - 7, 14.5);
	cairo_set_source_rgba(cr, 0.75, 0.82, 0.90, 0.10);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	int control_y = y + (height - 30) / 2;
	for (uint8_t workspace = 0; workspace < SEYRIX_SHELL_WORKSPACE_COUNT;
			workspace++) {
		draw_workspace_button(cr, left + 9 + (int)workspace * 36,
			control_y, 30, workspace,
			view->state != NULL ? view->state->active_workspace : 0,
			hits, capacity, count);
	}
	int launcher_x = left + workspace_width;
	rounded_rect(cr, launcher_x + 5, y + 5, height - 10, height - 10, 12);
	cairo_set_source_rgba(cr, 0.23, 0.43, 0.60, 0.98);
	cairo_fill(cr);
	centered_text(cr, "S", launcher_x + height / 2.0,
		y + height * 0.22, height * 0.44, 0.98, 0.99, 1.0, 1.0);
	hit_add(hits, capacity, count, launcher_x, y, height, height,
		SEYRIX_SHELL_HIT_LAUNCHER, 0, NULL);

	size_t offset = app_count > 0 ? view->application_offset % app_count : 0;
	size_t shown = 0;
	int app_x = launcher_x + launcher_width;
	for (; shown < visible && offset + shown < app_count; shown++) {
		const struct seyrix_shell_application *application = &applications[offset + shown];
		int x = app_x + (int)shown * slot_width;
		bool active = application->active;
		if (active) {
			rounded_rect(cr, x + 5, y + 5, slot_width - 10, height - 10, 11);
			cairo_set_source_rgba(cr, 0.26, 0.43, 0.58, 0.55);
			cairo_fill(cr);
		}
		const seyrix_app_entry *entry = app_for_id(view,
			application->pinned ? application->app_id : NULL,
			application->has_valid_app_id ? application->app_id : NULL);
		const char *name = entry != NULL ? seyrix_app_entry_name(entry) :
			application_name(view, application);
		if (dock) {
			double icon_x = x + (slot_width - icon_size) / 2.0;
			double icon_y = y + (height - icon_size) / 2.0;
			draw_icon(cr, view, entry, name, icon_x, icon_y, icon_size);
			if (application->running) {
				cairo_arc(cr, x + slot_width / 2.0, y + height - 2.2,
					active ? 2.5 : 1.8, 0, 2 * G_PI);
				cairo_set_source_rgba(cr, active ? 0.72 : 0.65,
					active ? 0.89 : 0.73, active ? 1.0 : 0.84, 0.98);
				cairo_fill(cr);
			}
		} else {
			draw_icon(cr, view, entry, name, x + 8,
				y + (height - 26) / 2.0, 26);
			text(cr, name, x + 43, y + height * 0.22,
				14, 0.93, 0.96, 0.99, 1.0, slot_width - 58);
			if (application->running) {
				char workspace[2] = {(char)('1' + application->representative_workspace), '\0'};
				text(cr, workspace, x + 43, y + height * 0.57,
					10, 0.66, 0.76, 0.85, 0.98, 20);
			}
		}
		if (application->running) {
			hit_add(hits, capacity, count, x, y, slot_width, height,
				SEYRIX_SHELL_HIT_WINDOW, application->window_id, NULL);
		} else if (application->pinned && entry != NULL) {
			hit_add(hits, capacity, count, x, y, slot_width, height,
				SEYRIX_SHELL_HIT_APPLICATION, 0,
				seyrix_app_entry_id(entry));
		}
	}
	if (overflow_width > 0 && app_count > shown) {
		int count_x = app_x + (int)shown * slot_width + 3;
		size_t remaining = app_count - shown;
		char label[16];
		(void)snprintf(label, sizeof(label), "+%u", (unsigned int)remaining);
		rounded_rect(cr, count_x, y + 9, 42, height - 18, 10);
		cairo_set_source_rgba(cr, 0.24, 0.31, 0.39, 0.98);
		cairo_fill(cr);
		centered_text(cr, label, count_x + 21, y + height * 0.30,
			13, 0.93, 0.96, 0.99, 1.0);
		hit_add(hits, capacity, count, count_x, y, 48, height,
			SEYRIX_SHELL_HIT_OVERFLOW, 0, NULL);
	}
}

static size_t switcher_ids(const struct seyrix_shell_view *view,
		uint32_t *ids, size_t capacity) {
	if (view->state == NULL || capacity == 0) return 0;
	if (view->config == NULL || view->config->switcher_active_workspace_only) {
		return seyrix_shell_state_switcher_windows(view->state, ids, capacity);
	}
	bool used[SEYRIX_SHELL_MAX_WINDOWS] = {false};
	size_t count = 0;
	while (count < capacity && count < view->state->window_count) {
		size_t best = SIZE_MAX;
		for (size_t index = 0; index < view->state->window_count; index++) {
			const struct seyrix_shell_window *window = &view->state->windows[index];
			if (used[index] || (window->flags & SEYRIX_SHELL_WINDOW_MAPPED) == 0 ||
					(window->flags & SEYRIX_SHELL_WINDOW_MINIMIZED) != 0) continue;
			if (best == SIZE_MAX || window->mru_order >
					view->state->windows[best].mru_order) best = index;
		}
		if (best == SIZE_MAX) break;
		used[best] = true;
		ids[count++] = view->state->windows[best].id;
	}
	return count;
}

static void draw_switcher(const struct seyrix_shell_view *view, cairo_t *cr,
		struct seyrix_shell_hit_area *hits, size_t capacity, size_t *hit_count) {
	uint32_t ids[SEYRIX_SHELL_MAX_WINDOWS];
	size_t total_count = switcher_ids(view, ids, SEYRIX_SHELL_MAX_WINDOWS);
	int card_width = view->width - 80;
	if (card_width > 760) card_width = 760;
	if (card_width < 300) card_width = view->width - 24;
	int row_height = 62;
	size_t visible_rows = (size_t)((view->height - 130) / row_height);
	if (visible_rows < 1) visible_rows = 1;
	if (visible_rows > SEYRIX_SHELL_HIT_MAX) visible_rows = SEYRIX_SHELL_HIT_MAX;
	size_t selected_index = view->selected_index < total_count ?
		view->selected_index : 0;
	size_t offset = selected_index >= visible_rows ?
		selected_index - visible_rows + 1 : 0;
	if (offset > total_count) offset = total_count;
	size_t count = total_count - offset;
	if (count > visible_rows) count = visible_rows;
	int card_height = 68 + (int)count * row_height;
	int left = (view->width - card_width) / 2;
	int top = (view->height - card_height) / 2;
	if (top < 12) top = 12;
	rounded_rect(cr, left, top, card_width, card_height, 22);
	cairo_set_source_rgba(cr, 0.08, 0.11, 0.16, 0.96);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.75, 0.82, 0.91, 0.20);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	text(cr, "Window switcher", left + 24, top + 15, 17,
		0.91, 0.95, 0.98, 1.0, card_width - 48);
	for (size_t index = 0; index < count; index++) {
		int y = top + 50 + (int)index * row_height;
		size_t window_index = offset + index;
		uint32_t id = ids[window_index];
		const struct seyrix_shell_window *window =
			seyrix_shell_state_find_window(view->state, id);
		if (window == NULL) continue;
		bool selected = window_index == selected_index;
		if (selected) {
			rounded_rect(cr, left + 12, y, card_width - 24, row_height - 4, 12);
			cairo_set_source_rgba(cr, 0.22, 0.38, 0.52, 0.72);
			cairo_fill(cr);
		}
		const char *app_id = window->has_valid_app_id ? window->app_id : NULL;
		const seyrix_app_entry *entry = app_for_id(view, NULL, app_id);
		const char *name = entry != NULL ? seyrix_app_entry_name(entry) :
			(window->title[0] != '\0' ? window->title : "Application");
		draw_icon(cr, view, entry, name, left + 24, y + 9, 38);
		text(cr, name, left + 76, y + 8, 15,
			0.95, 0.97, 0.99, 1.0, card_width - 110);
		text(cr, window->title, left + 76, y + 31, 12,
			0.65, 0.72, 0.79, 1.0, card_width - 110);
		if (window->workspace != view->state->active_workspace) {
			char workspace[32];
			(void)snprintf(workspace, sizeof(workspace), "Workspace %u",
				(unsigned int)window->workspace + 1U);
			text(cr, workspace, left + card_width - 140, y + 20, 11,
				0.67, 0.77, 0.86, 1.0, 120);
		}
		hit_add(hits, capacity, hit_count, left + 12, y, card_width - 24,
			row_height - 4, SEYRIX_SHELL_HIT_WINDOW, id, NULL);
	}
	if (count == 0) {
		text(cr, "No open windows on this workspace", left + 24, top + 48,
			14, 0.83, 0.87, 0.91, 1.0, card_width - 48);
	}
}

static void draw_launcher(const struct seyrix_shell_view *view, cairo_t *cr,
		struct seyrix_shell_hit_area *hits, size_t capacity, size_t *hit_count) {
	int card_width = view->width - 72;
	if (card_width > 720) card_width = 720;
	int card_height = view->height - 100;
	if (card_height > 700) card_height = 700;
	if (card_height < 220) card_height = view->height - 20;
	int left = (view->width - card_width) / 2;
	int top = (view->height - card_height) / 2;
	rounded_rect(cr, left, top, card_width, card_height, 24);
	cairo_set_source_rgba(cr, 0.07, 0.10, 0.14, 0.97);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.75, 0.83, 0.91, 0.20);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	text(cr, "Applications", left + 26, top + 20, 20,
		0.96, 0.98, 1.0, 1.0, card_width - 52);
	rounded_rect(cr, left + 22, top + 55, card_width - 44, 42, 10);
	cairo_set_source_rgba(cr, 0.16, 0.20, 0.25, 1.0);
	cairo_fill(cr);
	text(cr, view->query != NULL && view->query[0] != '\0' ? view->query :
		"Type to search applications", left + 35, top + 65, 14,
		view->query != NULL && view->query[0] != '\0' ? 0.96 : 0.57,
		view->query != NULL && view->query[0] != '\0' ? 0.97 : 0.64,
		view->query != NULL && view->query[0] != '\0' ? 0.99 : 0.70,
		1.0, card_width - 70);
	size_t count = view->search_results != NULL ? view->search_results->len : 0;
	size_t visible = (size_t)((card_height - 125) / 58);
	if (visible > 9) visible = 9;
	if (count > visible) count = visible;
	for (size_t index = 0; index < count; index++) {
		const seyrix_app_entry *entry = g_ptr_array_index(view->search_results,
			(guint)index);
		int y = top + 110 + (int)index * 58;
		if (index == view->selected_index) {
			rounded_rect(cr, left + 18, y, card_width - 36, 52, 11);
			cairo_set_source_rgba(cr, 0.22, 0.40, 0.56, 0.76);
			cairo_fill(cr);
		}
		draw_icon(cr, view, entry, seyrix_app_entry_name(entry), left + 30,
			y + 8, 36);
		text(cr, seyrix_app_entry_name(entry), left + 82, y + 14, 15,
			0.95, 0.97, 0.99, 1.0, card_width - 112);
		hit_add(hits, capacity, hit_count, left + 18, y,
			card_width - 36, 52, SEYRIX_SHELL_HIT_APPLICATION,
			(uint32_t)index, seyrix_app_entry_id(entry));
	}
	if (count == 0) {
		text(cr, "No matching applications", left + 30, top + 122, 14,
			0.74, 0.79, 0.84, 1.0, card_width - 60);
	}
}

static void draw_manual(const struct seyrix_shell_view *view, cairo_t *cr) {
	int card_width = view->width - 96;
	if (card_width > 900) card_width = 900;
	int card_height = view->height - 72;
	if (card_height > 800) card_height = 800;
	int left = (view->width - card_width) / 2;
	int top = (view->height - card_height) / 2;
	rounded_rect(cr, left, top, card_width, card_height, 20);
	cairo_set_source_rgba(cr, 0.08, 0.11, 0.15, 0.98);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.77, 0.85, 0.92, 0.20);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	text(cr, "Manual.txt", left + 30, top + 22, 22,
		0.96, 0.98, 1.0, 1.0, card_width - 60);
	text(cr, "Read only  •  Esc to close  •  Page Up / Page Down to scroll",
		left + 30, top + 55, 12, 0.64, 0.73, 0.81, 1.0, card_width - 60);
	cairo_save(cr);
	cairo_rectangle(cr, left + 28, top + 84, card_width - 56, card_height - 108);
	cairo_clip(cr);
	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *font = pango_font_description_new();
	pango_font_description_set_family(font, "Monospace");
	pango_font_description_set_absolute_size(font, 13 * PANGO_SCALE);
	pango_layout_set_font_description(layout, font);
	pango_layout_set_width(layout, (card_width - 64) * PANGO_SCALE);
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_text(layout, view->manual_text != NULL ? view->manual_text :
		"Manual.txt is unavailable.", -1);
	cairo_set_source_rgb(cr, 0.86, 0.89, 0.92);
	cairo_move_to(cr, left + 32, top + 88 - view->manual_scroll);
	pango_cairo_show_layout(cr, layout);
	pango_font_description_free(font);
	g_object_unref(layout);
	cairo_restore(cr);
}

void seyrix_shell_ui_draw(const struct seyrix_shell_view *view,
		cairo_t *cairo, struct seyrix_shell_hit_area *hits,
		size_t hit_capacity, size_t *hit_count) {
	if (view == NULL || cairo == NULL || view->width <= 0 || view->height <= 0) return;
	if (hit_count != NULL) *hit_count = 0;
	cairo_save(cairo);
	if (view->scale > 1) cairo_scale(cairo, view->scale, view->scale);
	switch (view->kind) {
	case SEYRIX_SHELL_VIEW_DESKTOP:
		draw_desktop(view, cairo, hits, hit_capacity, hit_count);
		break;
	case SEYRIX_SHELL_VIEW_PANEL:
		draw_panel(view, cairo, hits, hit_capacity, hit_count);
		break;
	case SEYRIX_SHELL_VIEW_LAUNCHER:
		draw_background(view, cairo);
		cairo_set_source_rgba(cairo, 0.02, 0.04, 0.07, 0.40);
		cairo_paint(cairo);
		draw_launcher(view, cairo, hits, hit_capacity, hit_count);
		break;
	case SEYRIX_SHELL_VIEW_SWITCHER:
		draw_background(view, cairo);
		cairo_set_source_rgba(cairo, 0.02, 0.04, 0.07, 0.48);
		cairo_paint(cairo);
		draw_switcher(view, cairo, hits, hit_capacity, hit_count);
		break;
	case SEYRIX_SHELL_VIEW_MANUAL:
		draw_background(view, cairo);
		cairo_set_source_rgba(cairo, 0.02, 0.04, 0.07, 0.48);
		cairo_paint(cairo);
		draw_manual(view, cairo);
		break;
	}
	cairo_restore(cairo);
}
