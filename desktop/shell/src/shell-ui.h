#ifndef SEYRIX_SHELL_UI_H
#define SEYRIX_SHELL_UI_H

#include <cairo.h>
#include <glib.h>

#include "app-catalog.h"
#include "shell-config.h"
#include "shell-state.h"

enum seyrix_shell_view_kind {
	SEYRIX_SHELL_VIEW_DESKTOP,
	SEYRIX_SHELL_VIEW_PANEL,
	SEYRIX_SHELL_VIEW_LAUNCHER,
	SEYRIX_SHELL_VIEW_SWITCHER,
	SEYRIX_SHELL_VIEW_MANUAL,
};

enum seyrix_shell_hit_kind {
	SEYRIX_SHELL_HIT_NONE,
	SEYRIX_SHELL_HIT_MANUAL,
	SEYRIX_SHELL_HIT_WORKSPACE,
	SEYRIX_SHELL_HIT_LAUNCHER,
	SEYRIX_SHELL_HIT_APPLICATION,
	SEYRIX_SHELL_HIT_OVERFLOW,
	SEYRIX_SHELL_HIT_WINDOW,
};

struct seyrix_shell_hit_area {
	int x;
	int y;
	int width;
	int height;
	enum seyrix_shell_hit_kind kind;
	uint32_t id;
	char desktop_id[SEYRIX_SHELL_DESKTOP_ID_MAX + 1];
};

struct seyrix_shell_view {
	enum seyrix_shell_view_kind kind;
	int width;
	int height;
	int scale;
	const struct seyrix_shell_config *config;
	const struct seyrix_shell_state *state;
	const seyrix_app_catalog *catalog;
	const char *query;
	GPtrArray *search_results;
	size_t selected_index;
	const char *manual_text;
	int manual_scroll;
	size_t application_offset;
};

#define SEYRIX_SHELL_HIT_MAX 256

void seyrix_shell_ui_draw(const struct seyrix_shell_view *view,
	cairo_t *cairo, struct seyrix_shell_hit_area *hits,
	size_t hit_capacity, size_t *hit_count);

#endif
