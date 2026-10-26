#ifndef SEYRIX_LAYOUT_ENGINE_H
#define SEYRIX_LAYOUT_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SEYRIX_LAYOUT_RATIO_SCALE 1000U
#define SEYRIX_LAYOUT_MIN_RATIO 50U
#define SEYRIX_LAYOUT_MAX_RATIO 950U
#define SEYRIX_LAYOUT_MAX_LEAVES 256U

enum seyrix_layout_axis {
	SEYRIX_LAYOUT_HORIZONTAL,
	SEYRIX_LAYOUT_VERTICAL,
};

struct seyrix_layout_node;

struct seyrix_layout_rect {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

struct seyrix_layout_limits {
	int32_t min_width;
	int32_t min_height;
	int32_t max_width;
	int32_t max_height;
};

struct seyrix_layout_allocator {
	void *(*allocate)(size_t size, void *data);
	void (*release)(void *pointer, void *data);
	void *data;
};

typedef void (*seyrix_layout_apply_fn)(void *owner,
	const struct seyrix_layout_rect *rect, void *data);

struct seyrix_layout_node *seyrix_layout_insert(
	struct seyrix_layout_node **root, void *target_owner, void *owner,
	enum seyrix_layout_axis axis, uint16_t ratio,
	const struct seyrix_layout_limits *limits,
	const struct seyrix_layout_allocator *allocator);
bool seyrix_layout_remove(struct seyrix_layout_node **root, const void *owner,
	const struct seyrix_layout_allocator *allocator);
void seyrix_layout_destroy(struct seyrix_layout_node **root,
	const struct seyrix_layout_allocator *allocator);

bool seyrix_layout_set_active(struct seyrix_layout_node *root,
	const void *owner, bool active);
bool seyrix_layout_set_limits(struct seyrix_layout_node *root,
	const void *owner, const struct seyrix_layout_limits *limits);
bool seyrix_layout_contains(const struct seyrix_layout_node *root,
	const void *owner);
bool seyrix_layout_get_rect(struct seyrix_layout_node *root, const void *owner,
	struct seyrix_layout_rect *rect);
bool seyrix_layout_set_all_ratios(struct seyrix_layout_node *root,
	uint16_t ratio);
bool seyrix_layout_adjust_ratio(struct seyrix_layout_node *root,
	const void *owner, enum seyrix_layout_axis axis, int delta);
bool seyrix_layout_get_ratio(struct seyrix_layout_node *root,
	const void *owner, enum seyrix_layout_axis axis, uint16_t *ratio);
bool seyrix_layout_set_parent_axis(struct seyrix_layout_node *root,
	const void *owner, enum seyrix_layout_axis axis);
bool seyrix_layout_swap_adjacent(struct seyrix_layout_node *root,
	const void *owner, int direction);

void *seyrix_layout_find_neighbor(struct seyrix_layout_node *root,
	const void *owner, int direction);
void *seyrix_layout_largest_leaf(struct seyrix_layout_node *root,
	bool active_only);
bool seyrix_layout_reflow(struct seyrix_layout_node *root,
	struct seyrix_layout_rect area, int32_t outer_gap, int32_t inner_gap,
	seyrix_layout_apply_fn apply, void *data);
bool seyrix_layout_fit_client(struct seyrix_layout_rect cell,
	const struct seyrix_layout_limits *limits, int32_t titlebar_height,
	struct seyrix_layout_rect *client_box);
bool seyrix_layout_rebalance(struct seyrix_layout_node **root,
	enum seyrix_layout_axis first_axis, uint16_t ratio,
	const struct seyrix_layout_allocator *allocator);

size_t seyrix_layout_leaf_count(const struct seyrix_layout_node *root);
size_t seyrix_layout_depth(const struct seyrix_layout_node *root);
bool seyrix_layout_validate(const struct seyrix_layout_node *root);

#endif
