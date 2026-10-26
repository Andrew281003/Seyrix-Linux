#ifndef SEYRIX_WM_CONFIG_H
#define SEYRIX_WM_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SEYRIX_WM_WORKSPACE_COUNT 4U
#define SEYRIX_WM_GAP_MAX 128
#define SEYRIX_WM_APP_ID_MAX 128U
#define SEYRIX_WM_APP_RULE_MAX 128U

enum seyrix_layout_mode {
	SEYRIX_LAYOUT_FLOATING,
	SEYRIX_LAYOUT_TILING,
	SEYRIX_LAYOUT_HYBRID,
};

enum seyrix_window_classification {
	SEYRIX_WINDOW_TILED,
	SEYRIX_WINDOW_FLOATING,
};

enum seyrix_split_axis_policy {
	SEYRIX_SPLIT_AUTO,
	SEYRIX_SPLIT_HORIZONTAL,
	SEYRIX_SPLIT_VERTICAL,
};

struct seyrix_layout_preferences {
	enum seyrix_layout_mode mode;
	int32_t inner_gap;
	int32_t outer_gap;
	uint16_t split_ratio;
	enum seyrix_split_axis_policy split_axis;
};

struct seyrix_app_rule {
	struct seyrix_app_rule *next;
	char *app_id;
	enum seyrix_window_classification classification;
};

struct seyrix_wm_config {
	struct seyrix_layout_preferences defaults;
	struct seyrix_layout_preferences workspaces[SEYRIX_WM_WORKSPACE_COUNT];
	struct seyrix_app_rule *app_rules;
	char *path;
};

void seyrix_wm_config_defaults(struct seyrix_wm_config *config);
void seyrix_wm_config_finish(struct seyrix_wm_config *config);
bool seyrix_wm_config_parse(FILE *stream, struct seyrix_wm_config *config,
	char *error, size_t error_size);
bool seyrix_wm_config_load(struct seyrix_wm_config *config,
	char *error, size_t error_size);
bool seyrix_wm_config_save(const struct seyrix_wm_config *config,
	char *error, size_t error_size);
bool seyrix_wm_config_get_app_class(const struct seyrix_wm_config *config,
	const char *app_id, enum seyrix_window_classification *classification);
bool seyrix_wm_config_set_app_class(struct seyrix_wm_config *config,
	const char *app_id, enum seyrix_window_classification classification);

#endif
