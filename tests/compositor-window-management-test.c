#include "../desktop/compositor/src/layout-engine.h"
#include "../desktop/compositor/src/wm-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(condition, description) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: %s\n", description); \
		failures++; \
	} \
} while (0)

struct placement {
	void *owner;
	struct seyrix_layout_rect rect;
};

struct placement_set {
	struct placement items[SEYRIX_LAYOUT_MAX_LEAVES];
	size_t count;
};

static void record_placement(void *owner,
		const struct seyrix_layout_rect *rect, void *data) {
	struct placement_set *set = data;
	if (set->count >= SEYRIX_LAYOUT_MAX_LEAVES) {
		return;
	}
	set->items[set->count].owner = owner;
	set->items[set->count].rect = *rect;
	set->count++;
}

static struct seyrix_layout_limits limits(int min_width, int min_height,
		int max_width, int max_height) {
	return (struct seyrix_layout_limits){
		.min_width = min_width,
		.min_height = min_height,
		.max_width = max_width,
		.max_height = max_height,
	};
}

static struct seyrix_layout_node *add_window(
		struct seyrix_layout_node **root, void *target, void *window,
		enum seyrix_layout_axis axis, uint16_t ratio,
		struct seyrix_layout_limits client_limits) {
	struct seyrix_layout_node *leaf = seyrix_layout_insert(root, target, window,
		axis, ratio, &client_limits, NULL);
	if (leaf != NULL) {
		(void)seyrix_layout_set_active(*root, window, true);
	}
	return leaf;
}

static bool rects_overlap(struct seyrix_layout_rect a,
		struct seyrix_layout_rect b) {
	return a.x < b.x + b.width && b.x < a.x + a.width &&
		a.y < b.y + b.height && b.y < a.y + a.height;
}

static bool placements_are_valid(const struct placement_set *set,
		struct seyrix_layout_rect area) {
	for (size_t i = 0; i < set->count; i++) {
		const struct seyrix_layout_rect *rect = &set->items[i].rect;
		if (rect->width <= 0 || rect->height <= 0 || rect->x < area.x ||
				rect->y < area.y ||
				(int64_t)rect->x + rect->width >
					(int64_t)area.x + area.width ||
				(int64_t)rect->y + rect->height >
					(int64_t)area.y + area.height) {
			return false;
		}
		for (size_t j = i + 1; j < set->count; j++) {
			if (rects_overlap(*rect, set->items[j].rect)) {
				return false;
			}
		}
	}
	return true;
}

static void test_empty_and_single_window(void) {
	struct placement_set placements = {0};
	struct seyrix_layout_rect area = {.x = -100, .y = 40,
		.width = 800, .height = 600};
	CHECK(seyrix_layout_reflow(NULL, area, 10, 8,
		record_placement, &placements), "empty layout reflows successfully");
	CHECK(placements.count == 0, "empty layout emits no placements");

	struct seyrix_layout_node *root = NULL;
	int first;
	CHECK(add_window(&root, NULL, &first, SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(1, 1, 0, 0)) != NULL, "create first layout leaf");
	CHECK(seyrix_layout_reflow(root, area, 10, 8,
		record_placement, &placements), "single window reflows");
	CHECK(placements.count == 1 && placements.items[0].rect.x == -90 &&
		placements.items[0].rect.y == 50 &&
		placements.items[0].rect.width == 780 &&
		placements.items[0].rect.height == 580,
		"outer gaps inset first window without negative geometry");
	CHECK(seyrix_layout_validate(root), "single leaf has valid ownership links");
	seyrix_layout_destroy(&root, NULL);
}

static void test_two_axes_and_zero_gaps(void) {
	struct seyrix_layout_node *root = NULL;
	int first, second;
	struct placement_set placements = {0};
	struct seyrix_layout_rect area = {.x = 0, .y = 0,
		.width = 800, .height = 600};
	add_window(&root, NULL, &first, SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(1, 1, 0, 0));
	add_window(&root, &first, &second, SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(1, 1, 0, 0));
	CHECK(seyrix_layout_reflow(root, area, 10, 8,
		record_placement, &placements), "horizontal pair reflows");
	CHECK(placements.count == 2 &&
		placements.items[0].rect.x + placements.items[0].rect.width + 8 ==
		placements.items[1].rect.x,
		"horizontal split applies inner gap exactly");
	CHECK(placements_are_valid(&placements, area),
		"horizontal pair stays inside output without overlap");
	CHECK(seyrix_layout_set_parent_axis(root, &second,
		SEYRIX_LAYOUT_VERTICAL), "focused leaf parent split orientation changes");
	placements.count = 0;
	CHECK(seyrix_layout_reflow(root, area, 0, 0,
		record_placement, &placements) && placements.count == 2 &&
		placements.items[0].rect.y < placements.items[1].rect.y,
		"changing parent split orientation applies vertical geometry");

	placements.count = 0;
	seyrix_layout_destroy(&root, NULL);
	add_window(&root, NULL, &first, SEYRIX_LAYOUT_VERTICAL, 500,
		limits(1, 1, 0, 0));
	add_window(&root, &first, &second, SEYRIX_LAYOUT_VERTICAL, 500,
		limits(1, 1, 0, 0));
	CHECK(seyrix_layout_reflow(root, area, 0, 0,
		record_placement, &placements), "zero-gap vertical pair reflows");
	CHECK(placements.count == 2 &&
		placements.items[0].rect.y + placements.items[0].rect.height ==
		placements.items[1].rect.y,
		"zero-gap mode removes inner and outer gaps");
	CHECK(placements_are_valid(&placements, area),
		"vertical pair stays inside output without overlap");
	seyrix_layout_destroy(&root, NULL);
}

static void test_nested_and_many_windows(void) {
	struct seyrix_layout_node *root = NULL;
	int windows[12];
	struct placement_set placements = {0};
	struct seyrix_layout_rect area = {.x = 20, .y = -30,
		.width = 1600, .height = 1000};
	add_window(&root, NULL, &windows[0], SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(1, 1, 0, 0));
	for (size_t i = 1; i < 12; i++) {
		enum seyrix_layout_axis axis = i % 2 == 0 ?
			SEYRIX_LAYOUT_HORIZONTAL : SEYRIX_LAYOUT_VERTICAL;
		add_window(&root, &windows[i - 1], &windows[i], axis, 500,
			limits(1, 1, 0, 0));
	}
	CHECK(seyrix_layout_leaf_count(root) == 12,
		"tree contains more than two windows");
	CHECK(seyrix_layout_depth(root) == 12,
		"nested subdivisions retain deterministic depth before rebalance");
	CHECK(seyrix_layout_reflow(root, area, 12, 10,
		record_placement, &placements), "nested multi-window layout reflows");
	CHECK(placements.count == 12 && placements_are_valid(&placements, area),
		"twelve nested windows receive positive non-overlapping geometry");
	CHECK(seyrix_layout_rebalance(&root, SEYRIX_LAYOUT_HORIZONTAL, 500, NULL),
		"tree supports explicit rebalancing");
	CHECK(seyrix_layout_depth(root) < 12 && seyrix_layout_validate(root),
		"rebalanced tree is shallower and valid");
	placements.count = 0;
	CHECK(seyrix_layout_reflow(root, area, 12, 10,
		record_placement, &placements) && placements.count == 12 &&
		placements_are_valid(&placements, area),
		"rebalanced windows retain non-overlapping geometry");
	seyrix_layout_destroy(&root, NULL);
}

static void test_ratio_clamping_and_neighbor_reorder(void) {
	struct seyrix_layout_node *root = NULL;
	int windows[3];
	add_window(&root, NULL, &windows[0], SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(1, 1, 0, 0));
	add_window(&root, &windows[0], &windows[1],
		SEYRIX_LAYOUT_HORIZONTAL, 500, limits(1, 1, 0, 0));
	add_window(&root, &windows[1], &windows[2],
		SEYRIX_LAYOUT_VERTICAL, 500, limits(1, 1, 0, 0));
	CHECK(seyrix_layout_adjust_ratio(root, &windows[1],
		SEYRIX_LAYOUT_HORIZONTAL, -10000), "ratio adjusts downward");
	uint16_t ratio = 0;
	CHECK(seyrix_layout_get_ratio(root, &windows[1],
		SEYRIX_LAYOUT_HORIZONTAL, &ratio) && ratio == SEYRIX_LAYOUT_MIN_RATIO,
		"split ratio clamps at safe minimum");
	CHECK(seyrix_layout_adjust_ratio(root, &windows[1],
		SEYRIX_LAYOUT_HORIZONTAL, 10000) &&
		seyrix_layout_get_ratio(root, &windows[1],
			SEYRIX_LAYOUT_HORIZONTAL, &ratio) &&
		ratio == SEYRIX_LAYOUT_MAX_RATIO,
		"split ratio clamps at safe maximum");
	CHECK(seyrix_layout_find_neighbor(root, &windows[0], 1) == &windows[1],
		"focus traversal finds next active window");
	CHECK(seyrix_layout_swap_adjacent(root, &windows[0], 1),
		"layout supports deterministic adjacent-window movement");
	CHECK(seyrix_layout_find_neighbor(root, &windows[0], -1) == &windows[1],
		"reordered leaves preserve stable neighbor traversal");
	CHECK(seyrix_layout_validate(root), "ratio and reorder preserve tree links");
	seyrix_layout_destroy(&root, NULL);
}

static void test_client_constraints_and_resize(void) {
	struct seyrix_layout_node *root = NULL;
	int first, second;
	struct placement_set placements = {0};
	struct seyrix_layout_rect area = {.x = 0, .y = 0,
		.width = 500, .height = 180};
	add_window(&root, NULL, &first, SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(240, 160, 280, 170));
	add_window(&root, &first, &second, SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(240, 160, 260, 170));
	CHECK(seyrix_layout_reflow(root, area, 0, 20,
		record_placement, &placements),
		"minimum client widths and heights fit exact usable area");
	CHECK(placements.count == 2 && placements_are_valid(&placements, area),
		"constrained tiles remain positive and non-overlapping");
	struct seyrix_layout_rect client;
	struct seyrix_layout_limits client_limits = limits(240, 100, 180, 140);
	CHECK(seyrix_layout_fit_client(placements.items[0].rect,
		&client_limits, 20, &client),
		"fit helper respects minimum over contradictory maximum");
	CHECK(client.width == 240 && client.height == 120,
		"minimum dimensions win when client maximum is contradictory");
	client_limits = limits(100, 80, 180, 110);
	CHECK(seyrix_layout_fit_client(area, &client_limits, 20, &client) &&
		client.width == 180 && client.height == 90,
		"maximum client dimensions center inside the assigned cell");
	CHECK(!seyrix_layout_fit_client((struct seyrix_layout_rect){0, 0, 90, 50},
		&client_limits, 20, &client),
		"fit helper rejects cells below minimum client dimensions");

	placements.count = 0;
	CHECK(seyrix_layout_reflow(root,
		(struct seyrix_layout_rect){.x = 0, .y = 0, .width = 900, .height = 700},
		10, 10, record_placement, &placements),
		"output resize recomputes layout");
	CHECK(placements_are_valid(&placements,
		(struct seyrix_layout_rect){.x = 0, .y = 0, .width = 900, .height = 700}),
		"resized layout remains bounded");
	seyrix_layout_destroy(&root, NULL);
}

static void test_gap_and_minimum_fallback(void) {
	struct seyrix_layout_node *root = NULL;
	int first, second;
	struct placement_set placements = {0};
	add_window(&root, NULL, &first, SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(200, 1, 0, 0));
	add_window(&root, &first, &second, SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(200, 1, 0, 0));
	struct seyrix_layout_rect area = {.x = 0, .y = 0,
		.width = 500, .height = 300};
	CHECK(seyrix_layout_reflow(root, area, 30, 80,
		record_placement, &placements),
		"engine drops configured gaps before violating client minimums");
	CHECK(placements.items[0].rect.x == area.x &&
		placements.items[1].rect.x + placements.items[1].rect.width ==
		area.x + area.width,
		"minimum fallback deterministically uses zero outer gap");

	placements.count = 0;
	struct seyrix_layout_rect too_small = {.x = 0, .y = 0,
		.width = 350, .height = 300};
	CHECK(!seyrix_layout_reflow(root, too_small, 10, 10,
		record_placement, &placements),
		"unsatisfiable client minima preserve prior valid geometry");
	CHECK(placements.count == 0 && seyrix_layout_validate(root),
		"impossible area emits no invalid geometry or partial placement");
	seyrix_layout_destroy(&root, NULL);
}

static void test_remove_and_workspace_migration(void) {
	struct seyrix_layout_node *source = NULL;
	struct seyrix_layout_node *destination = NULL;
	int windows[4];
	add_window(&source, NULL, &windows[0], SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(1, 1, 0, 0));
	add_window(&source, &windows[0], &windows[1],
		SEYRIX_LAYOUT_VERTICAL, 500, limits(1, 1, 0, 0));
	add_window(&source, &windows[1], &windows[2],
		SEYRIX_LAYOUT_HORIZONTAL, 500, limits(1, 1, 0, 0));
	CHECK(seyrix_layout_remove(&source, &windows[1], NULL),
		"window removal compacts split parent");
	CHECK(seyrix_layout_leaf_count(source) == 2 &&
		seyrix_layout_validate(source),
		"compacted source tree has no stale nodes");
	add_window(&destination, NULL, &windows[3], SEYRIX_LAYOUT_HORIZONTAL, 500,
		limits(1, 1, 0, 0));
	CHECK(seyrix_layout_remove(&source, &windows[2], NULL),
		"workspace migration detaches source leaf");
	CHECK(add_window(&destination, &windows[3], &windows[2],
		SEYRIX_LAYOUT_VERTICAL, 500, limits(1, 1, 0, 0)) != NULL,
		"workspace migration inserts same owner into destination");
	CHECK(seyrix_layout_leaf_count(source) == 1 &&
		seyrix_layout_leaf_count(destination) == 2 &&
		seyrix_layout_validate(source) && seyrix_layout_validate(destination),
		"workspace migration preserves tree ownership and source compaction");
	seyrix_layout_destroy(&source, NULL);
	seyrix_layout_destroy(&destination, NULL);
}

struct allocation_state {
	size_t attempts;
	size_t frees;
	size_t fail_at;
};

static void *test_allocate(size_t size, void *data) {
	struct allocation_state *state = data;
	state->attempts++;
	if (state->attempts == state->fail_at) {
		return NULL;
	}
	return malloc(size);
}

static void test_release(void *pointer, void *data) {
	struct allocation_state *state = data;
	state->frees++;
	free(pointer);
}

static void test_allocation_failure_is_atomic(void) {
	struct allocation_state state = {.fail_at = SIZE_MAX};
	struct seyrix_layout_allocator allocator = {
		.allocate = test_allocate,
		.release = test_release,
		.data = &state,
	};
	struct seyrix_layout_node *root = NULL;
	int first, second;
	CHECK(seyrix_layout_insert(&root, NULL, &first,
		SEYRIX_LAYOUT_HORIZONTAL, 500, NULL, &allocator) != NULL,
		"create allocator-injected root leaf");
	state.fail_at = state.attempts + 2;
	CHECK(seyrix_layout_insert(&root, &first, &second,
		SEYRIX_LAYOUT_HORIZONTAL, 500, NULL, &allocator) == NULL,
		"partial split allocation failure is reported");
	CHECK(seyrix_layout_leaf_count(root) == 1 &&
		seyrix_layout_validate(root) && state.frees == 1,
		"failed insertion releases temporary split and preserves old tree");
	state.fail_at = SIZE_MAX;
	CHECK(seyrix_layout_insert(&root, &first, &second,
		SEYRIX_LAYOUT_HORIZONTAL, 500, NULL, &allocator) != NULL,
		"tree remains usable after allocation failure");
	state.fail_at = state.attempts + 1;
	CHECK(!seyrix_layout_rebalance(&root, SEYRIX_LAYOUT_HORIZONTAL, 500,
		&allocator) && seyrix_layout_leaf_count(root) == 2 &&
		seyrix_layout_validate(root),
		"rebalance allocation failure preserves original tree");
	state.fail_at = SIZE_MAX;
	seyrix_layout_destroy(&root, &allocator);
	CHECK(state.frees == 4,
		"allocator releases every successful node allocation");
}

static void test_config_parser_and_persistence(void) {
	struct seyrix_wm_config config = {0};
	seyrix_wm_config_defaults(&config);
	CHECK(config.defaults.mode == SEYRIX_LAYOUT_HYBRID &&
		config.defaults.inner_gap == 12 && config.defaults.outer_gap == 12,
		"safe defaults select hybrid with documented gaps");
	FILE *stream = tmpfile();
	CHECK(stream != NULL, "create temporary config stream");
	if (stream == NULL) {
		seyrix_wm_config_finish(&config);
		return;
	}
	fputs("version=1\ndefault_mode=tiling\ninner_gap=0\nouter_gap=8\n"
		"workspace.2.mode=floating\nworkspace.2.split_ratio=700\n"
		"app_id.6170702e6469616c6f67=floating\n", stream);
	rewind(stream);
	char error[160];
	CHECK(seyrix_wm_config_parse(stream, &config, error, sizeof(error)),
		"valid allowlisted config parses");
	fclose(stream);
	CHECK(config.defaults.mode == SEYRIX_LAYOUT_TILING &&
		config.workspaces[0].mode == SEYRIX_LAYOUT_TILING &&
		config.workspaces[1].mode == SEYRIX_LAYOUT_FLOATING &&
		config.workspaces[1].split_ratio == 700,
		"global defaults and per-workspace overrides apply");
	enum seyrix_window_classification classification;
	CHECK(seyrix_wm_config_get_app_class(&config, "app.dialog",
		&classification) && classification == SEYRIX_WINDOW_FLOATING,
		"hex-encoded app identifier rule parses safely");

	char temporary_directory[] = "/tmp/seyrix-wm-config-XXXXXX";
	char *directory = mkdtemp(temporary_directory);
	CHECK(directory != NULL, "create isolated config persistence directory");
	if (directory != NULL) {
		size_t path_size = strlen(directory) + sizeof("/settings.conf");
		config.path = malloc(path_size);
		CHECK(config.path != NULL, "allocate isolated config path");
		if (config.path != NULL) {
			snprintf(config.path, path_size, "%s/settings.conf", directory);
			CHECK(seyrix_wm_config_save(&config, error, sizeof(error)),
				"configuration writes through atomic persistence path");
			FILE *saved = fopen(config.path, "r");
			struct seyrix_wm_config roundtrip = {0};
			seyrix_wm_config_defaults(&roundtrip);
			CHECK(saved != NULL, "persisted configuration can be reopened");
			if (saved != NULL) {
				CHECK(seyrix_wm_config_parse(saved, &roundtrip, error,
					sizeof(error)), "persisted configuration parses again");
				fclose(saved);
				CHECK(roundtrip.defaults.mode == SEYRIX_LAYOUT_TILING &&
					seyrix_wm_config_get_app_class(&roundtrip, "app.dialog",
						&classification) &&
					classification == SEYRIX_WINDOW_FLOATING,
					"configuration roundtrip preserves mode and app rule");
			}
			seyrix_wm_config_finish(&roundtrip);
			unlink(config.path);
			free(config.path);
			config.path = NULL;
		}
		rmdir(directory);
	}

	stream = tmpfile();
	CHECK(stream != NULL, "create malformed config stream");
	if (stream != NULL) {
		fputs("default_mode=hybrid\ninner_gap=999\n", stream);
		rewind(stream);
		CHECK(!seyrix_wm_config_parse(stream, &config, error, sizeof(error)),
			"out-of-range config is rejected");
		fclose(stream);
		CHECK(config.defaults.mode == SEYRIX_LAYOUT_TILING &&
			config.defaults.inner_gap == 0,
			"rejected config leaves previous validated config unchanged");
	}
	seyrix_wm_config_finish(&config);
}

int main(void) {
	test_empty_and_single_window();
	test_two_axes_and_zero_gaps();
	test_nested_and_many_windows();
	test_ratio_clamping_and_neighbor_reorder();
	test_client_constraints_and_resize();
	test_gap_and_minimum_fallback();
	test_remove_and_workspace_migration();
	test_allocation_failure_is_atomic();
	test_config_parser_and_persistence();
	if (failures != 0) {
		fprintf(stderr, "%d window-management check(s) failed\n", failures);
		return 1;
	}
	puts("COMPOSITOR_WINDOW_MANAGEMENT=passed");
	return 0;
}
