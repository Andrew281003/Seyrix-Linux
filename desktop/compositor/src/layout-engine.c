#include "layout-engine.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum node_type {
	NODE_LEAF,
	NODE_SPLIT,
};

struct seyrix_layout_node {
	struct seyrix_layout_node *parent;
	enum node_type type;
	union {
		struct {
			void *owner;
			struct seyrix_layout_limits limits;
			struct seyrix_layout_rect last_rect;
			bool active;
		} leaf;
		struct {
			struct seyrix_layout_node *first;
			struct seyrix_layout_node *second;
			enum seyrix_layout_axis axis;
			uint16_t ratio;
		} split;
	} data;
};

struct layout_requirement {
	int64_t width;
	int64_t height;
	size_t leaves;
};

static void *default_allocate(size_t size, void *data) {
	(void)data;
	return calloc(1, size);
}

static void default_release(void *pointer, void *data) {
	(void)data;
	free(pointer);
}

static void *node_allocate(const struct seyrix_layout_allocator *allocator) {
	if (allocator != NULL && allocator->allocate != NULL) {
		return allocator->allocate(sizeof(struct seyrix_layout_node),
			allocator->data);
	}
	return default_allocate(sizeof(struct seyrix_layout_node), NULL);
}

static void node_release(struct seyrix_layout_node *node,
		const struct seyrix_layout_allocator *allocator) {
	if (node == NULL) {
		return;
	}
	if (allocator != NULL && allocator->release != NULL) {
		allocator->release(node, allocator->data);
	} else {
		default_release(node, NULL);
	}
}

static uint16_t clamp_ratio(int ratio) {
	if (ratio < (int)SEYRIX_LAYOUT_MIN_RATIO) {
		return SEYRIX_LAYOUT_MIN_RATIO;
	}
	if (ratio > (int)SEYRIX_LAYOUT_MAX_RATIO) {
		return SEYRIX_LAYOUT_MAX_RATIO;
	}
	return (uint16_t)ratio;
}

static int32_t min_dimension(int32_t value) {
	return value > 0 ? value : 1;
}

static struct seyrix_layout_node *find_leaf(struct seyrix_layout_node *node,
		const void *owner) {
	if (node == NULL || owner == NULL) {
		return NULL;
	}
	if (node->type == NODE_LEAF) {
		return node->data.leaf.owner == owner ? node : NULL;
	}
	struct seyrix_layout_node *found = find_leaf(node->data.split.first, owner);
	return found != NULL ? found : find_leaf(node->data.split.second, owner);
}

static bool owner_exists(const struct seyrix_layout_node *node,
		const void *owner) {
	if (node == NULL || owner == NULL) {
		return false;
	}
	if (node->type == NODE_LEAF) {
		return node->data.leaf.owner == owner;
	}
	return owner_exists(node->data.split.first, owner) ||
		owner_exists(node->data.split.second, owner);
}

bool seyrix_layout_contains(const struct seyrix_layout_node *root,
		const void *owner) {
	return owner_exists(root, owner);
}

static size_t leaf_count(const struct seyrix_layout_node *node) {
	if (node == NULL) {
		return 0;
	}
	if (node->type == NODE_LEAF) {
		return 1;
	}
	return leaf_count(node->data.split.first) +
		leaf_count(node->data.split.second);
}

static size_t node_depth(const struct seyrix_layout_node *node) {
	if (node == NULL) {
		return 0;
	}
	if (node->type == NODE_LEAF) {
		return 1;
	}
	size_t first = node_depth(node->data.split.first);
	size_t second = node_depth(node->data.split.second);
	return 1 + (first > second ? first : second);
}

static struct seyrix_layout_node *new_leaf(void *owner,
		const struct seyrix_layout_limits *limits,
		const struct seyrix_layout_allocator *allocator) {
	struct seyrix_layout_node *node = node_allocate(allocator);
	if (node == NULL) {
		return NULL;
	}
	memset(node, 0, sizeof(*node));
	node->type = NODE_LEAF;
	node->data.leaf.owner = owner;
	if (limits != NULL) {
		node->data.leaf.limits = *limits;
	}
	return node;
}

struct seyrix_layout_node *seyrix_layout_insert(
		struct seyrix_layout_node **root, void *target_owner, void *owner,
		enum seyrix_layout_axis axis, uint16_t ratio,
		const struct seyrix_layout_limits *limits,
		const struct seyrix_layout_allocator *allocator) {
	if (root == NULL || owner == NULL ||
			(axis != SEYRIX_LAYOUT_HORIZONTAL && axis != SEYRIX_LAYOUT_VERTICAL) ||
			owner_exists(*root, owner) || leaf_count(*root) >= SEYRIX_LAYOUT_MAX_LEAVES) {
		return NULL;
	}
	if (*root == NULL) {
		if (target_owner != NULL) {
			return NULL;
		}
		*root = new_leaf(owner, limits, allocator);
		return *root;
	}

	struct seyrix_layout_node *target = target_owner != NULL ?
		find_leaf(*root, target_owner) : NULL;
	if (target_owner != NULL && target == NULL) {
		return NULL;
	}
	if (target == NULL) {
		void *target_fallback = seyrix_layout_largest_leaf(*root, true);
		if (target_fallback == NULL) {
			target_fallback = seyrix_layout_largest_leaf(*root, false);
		}
		target = find_leaf(*root, target_fallback);
	}
	if (target == NULL) {
		return NULL;
	}

	struct seyrix_layout_node *split = node_allocate(allocator);
	if (split == NULL) {
		return NULL;
	}
	memset(split, 0, sizeof(*split));
	struct seyrix_layout_node *new_node = new_leaf(owner, limits, allocator);
	if (new_node == NULL) {
		node_release(split, allocator);
		return NULL;
	}

	split->type = NODE_SPLIT;
	split->data.split.first = target;
	split->data.split.second = new_node;
	split->data.split.axis = axis;
	split->data.split.ratio = clamp_ratio(ratio);
	split->parent = target->parent;
	target->parent = split;
	new_node->parent = split;
	if (split->parent == NULL) {
		*root = split;
	} else if (split->parent->data.split.first == target) {
		split->parent->data.split.first = split;
	} else {
		split->parent->data.split.second = split;
	}
	return new_node;
}

bool seyrix_layout_remove(struct seyrix_layout_node **root, const void *owner,
		const struct seyrix_layout_allocator *allocator) {
	if (root == NULL || *root == NULL || owner == NULL) {
		return false;
	}
	struct seyrix_layout_node *leaf = find_leaf(*root, owner);
	if (leaf == NULL) {
		return false;
	}
	struct seyrix_layout_node *parent = leaf->parent;
	if (parent == NULL) {
		*root = NULL;
		node_release(leaf, allocator);
		return true;
	}
	struct seyrix_layout_node *sibling = parent->data.split.first == leaf ?
		parent->data.split.second : parent->data.split.first;
	struct seyrix_layout_node *grandparent = parent->parent;
	sibling->parent = grandparent;
	if (grandparent == NULL) {
		*root = sibling;
	} else if (grandparent->data.split.first == parent) {
		grandparent->data.split.first = sibling;
	} else {
		grandparent->data.split.second = sibling;
	}
	node_release(leaf, allocator);
	node_release(parent, allocator);
	return true;
}

static void destroy_node(struct seyrix_layout_node *node,
		const struct seyrix_layout_allocator *allocator) {
	if (node == NULL) {
		return;
	}
	if (node->type == NODE_SPLIT) {
		destroy_node(node->data.split.first, allocator);
		destroy_node(node->data.split.second, allocator);
	}
	node_release(node, allocator);
}

void seyrix_layout_destroy(struct seyrix_layout_node **root,
		const struct seyrix_layout_allocator *allocator) {
	if (root == NULL) {
		return;
	}
	destroy_node(*root, allocator);
	*root = NULL;
}

bool seyrix_layout_set_active(struct seyrix_layout_node *root,
		const void *owner, bool active) {
	struct seyrix_layout_node *leaf = find_leaf(root, owner);
	if (leaf == NULL) {
		return false;
	}
	leaf->data.leaf.active = active;
	return true;
}

bool seyrix_layout_set_limits(struct seyrix_layout_node *root,
		const void *owner, const struct seyrix_layout_limits *limits) {
	struct seyrix_layout_node *leaf = find_leaf(root, owner);
	if (leaf == NULL || limits == NULL) {
		return false;
	}
	leaf->data.leaf.limits = *limits;
	return true;
}

bool seyrix_layout_get_rect(struct seyrix_layout_node *root, const void *owner,
		struct seyrix_layout_rect *rect) {
	struct seyrix_layout_node *leaf = find_leaf(root, owner);
	if (leaf == NULL || rect == NULL) {
		return false;
	}
	*rect = leaf->data.leaf.last_rect;
	return rect->width > 0 && rect->height > 0;
}

static void set_ratios(struct seyrix_layout_node *node, uint16_t ratio) {
	if (node == NULL || node->type == NODE_LEAF) {
		return;
	}
	node->data.split.ratio = clamp_ratio(ratio);
	set_ratios(node->data.split.first, ratio);
	set_ratios(node->data.split.second, ratio);
}

bool seyrix_layout_set_all_ratios(struct seyrix_layout_node *root,
		uint16_t ratio) {
	if (root == NULL || root->type == NODE_LEAF) {
		return false;
	}
	set_ratios(root, ratio);
	return true;
}

bool seyrix_layout_adjust_ratio(struct seyrix_layout_node *root,
		const void *owner, enum seyrix_layout_axis axis, int delta) {
	struct seyrix_layout_node *leaf = find_leaf(root, owner);
	if (leaf == NULL || delta == 0 ||
			(axis != SEYRIX_LAYOUT_HORIZONTAL && axis != SEYRIX_LAYOUT_VERTICAL)) {
		return false;
	}
	for (struct seyrix_layout_node *parent = leaf->parent; parent != NULL;
			parent = parent->parent) {
			if (parent->data.split.axis == axis) {
				parent->data.split.ratio = clamp_ratio(
					(int)parent->data.split.ratio + delta);
				return true;
			}
	}
	return false;
}

bool seyrix_layout_get_ratio(struct seyrix_layout_node *root,
		const void *owner, enum seyrix_layout_axis axis, uint16_t *ratio) {
	struct seyrix_layout_node *leaf = find_leaf(root, owner);
	if (leaf == NULL || ratio == NULL) {
		return false;
	}
	for (struct seyrix_layout_node *parent = leaf->parent; parent != NULL;
			parent = parent->parent) {
		if (parent->data.split.axis == axis) {
			*ratio = parent->data.split.ratio;
			return true;
		}
	}
	return false;
}

bool seyrix_layout_set_parent_axis(struct seyrix_layout_node *root,
		const void *owner, enum seyrix_layout_axis axis) {
	struct seyrix_layout_node *leaf = find_leaf(root, owner);
	if (leaf == NULL || leaf->parent == NULL ||
			(axis != SEYRIX_LAYOUT_HORIZONTAL &&
			axis != SEYRIX_LAYOUT_VERTICAL)) {
		return false;
	}
	leaf->parent->data.split.axis = axis;
	return true;
}

struct node_array {
	struct seyrix_layout_node *items[SEYRIX_LAYOUT_MAX_LEAVES];
	size_t count;
};

static void collect_leaves(struct seyrix_layout_node *node,
		struct node_array *array) {
	if (node == NULL || array->count >= SEYRIX_LAYOUT_MAX_LEAVES) {
		return;
	}
	if (node->type == NODE_LEAF) {
		array->items[array->count++] = node;
		return;
	}
	collect_leaves(node->data.split.first, array);
	collect_leaves(node->data.split.second, array);
}

void *seyrix_layout_find_neighbor(struct seyrix_layout_node *root,
		const void *owner, int direction) {
	if (owner == NULL || direction == 0) {
		return NULL;
	}
	struct node_array array = {0};
	collect_leaves(root, &array);
	bool found = false;
	if (direction > 0) {
		for (size_t i = 0; i < array.count; i++) {
			struct seyrix_layout_node *leaf = array.items[i];
			if (found && leaf->data.leaf.active) {
				return leaf->data.leaf.owner;
			}
			if (leaf->data.leaf.owner == owner) {
				found = true;
			}
		}
	} else {
		for (size_t i = array.count; i > 0; i--) {
			struct seyrix_layout_node *leaf = array.items[i - 1];
			if (found && leaf->data.leaf.active) {
				return leaf->data.leaf.owner;
			}
			if (leaf->data.leaf.owner == owner) {
				found = true;
			}
		}
	}
	return NULL;
}

bool seyrix_layout_swap_adjacent(struct seyrix_layout_node *root,
		const void *owner, int direction) {
	struct seyrix_layout_node *leaf = find_leaf(root, owner);
	if (leaf == NULL || direction == 0 || !leaf->data.leaf.active) {
		return false;
	}
	void *neighbor_owner = seyrix_layout_find_neighbor(root, owner, direction);
	struct seyrix_layout_node *neighbor = find_leaf(root, neighbor_owner);
	if (neighbor == NULL) {
		return false;
	}
	void *old_owner = leaf->data.leaf.owner;
	struct seyrix_layout_limits old_limits = leaf->data.leaf.limits;
	struct seyrix_layout_rect old_rect = leaf->data.leaf.last_rect;
	leaf->data.leaf.owner = neighbor->data.leaf.owner;
	leaf->data.leaf.limits = neighbor->data.leaf.limits;
	leaf->data.leaf.last_rect = neighbor->data.leaf.last_rect;
	neighbor->data.leaf.owner = old_owner;
	neighbor->data.leaf.limits = old_limits;
	neighbor->data.leaf.last_rect = old_rect;
	return true;
}

void *seyrix_layout_largest_leaf(struct seyrix_layout_node *root,
		bool active_only) {
	struct node_array array = {0};
	collect_leaves(root, &array);
	struct seyrix_layout_node *best = NULL;
	uint64_t best_area = 0;
	for (size_t i = 0; i < array.count; i++) {
		struct seyrix_layout_node *leaf = array.items[i];
		if (active_only && !leaf->data.leaf.active) {
			continue;
		}
		int32_t width = leaf->data.leaf.last_rect.width;
		int32_t height = leaf->data.leaf.last_rect.height;
		uint64_t area = width > 0 && height > 0 ?
			(uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height : 0;
		if (best == NULL || area > best_area) {
			best = leaf;
			best_area = area;
		}
	}
	return best != NULL ? best->data.leaf.owner : NULL;
}

static struct layout_requirement requirement(
		const struct seyrix_layout_node *node, int32_t inner_gap) {
	struct layout_requirement result = {0};
	if (node == NULL) {
		return result;
	}
	if (node->type == NODE_LEAF) {
		if (node->data.leaf.active) {
			result.width = min_dimension(node->data.leaf.limits.min_width);
			result.height = min_dimension(node->data.leaf.limits.min_height);
			result.leaves = 1;
		}
		return result;
	}
	struct layout_requirement first = requirement(node->data.split.first, inner_gap);
	struct layout_requirement second = requirement(node->data.split.second, inner_gap);
	result.leaves = first.leaves + second.leaves;
	if (first.leaves == 0) {
		return second;
	}
	if (second.leaves == 0) {
		return first;
	}
	if (node->data.split.axis == SEYRIX_LAYOUT_HORIZONTAL) {
		result.width = first.width + second.width + inner_gap;
		result.height = first.height > second.height ? first.height : second.height;
	} else {
		result.width = first.width > second.width ? first.width : second.width;
		result.height = first.height + second.height + inner_gap;
	}
	return result;
}

static int32_t clamp_gap(int32_t requested, int32_t extent) {
	if (requested <= 0 || extent <= 1) {
		return 0;
	}
	int32_t maximum = (extent - 1) / 2;
	return requested < maximum ? requested : maximum;
}

static int64_t ceil_divide(int64_t numerator, int64_t denominator) {
	return (numerator + denominator - 1) / denominator;
}

static void apply_node(struct seyrix_layout_node *node,
		struct seyrix_layout_rect rect, int32_t inner_gap,
		seyrix_layout_apply_fn apply, void *data) {
	if (node == NULL) {
		return;
	}
	if (node->type == NODE_LEAF) {
		if (node->data.leaf.active) {
			node->data.leaf.last_rect = rect;
			apply(node->data.leaf.owner, &rect, data);
		}
		return;
	}
	struct layout_requirement first = requirement(node->data.split.first, inner_gap);
	struct layout_requirement second = requirement(node->data.split.second, inner_gap);
	if (first.leaves == 0) {
		apply_node(node->data.split.second, rect, inner_gap, apply, data);
		return;
	}
	if (second.leaves == 0) {
		apply_node(node->data.split.first, rect, inner_gap, apply, data);
		return;
	}

	bool horizontal = node->data.split.axis == SEYRIX_LAYOUT_HORIZONTAL;
	int32_t extent = horizontal ? rect.width : rect.height;
	int32_t gap = inner_gap;
	if (gap > extent - 2) {
		gap = extent - 2;
	}
	int32_t available = extent - gap;
	int64_t first_min = horizontal ? first.width : first.height;
	int64_t second_min = horizontal ? second.width : second.height;
	int64_t low = ceil_divide(first_min * SEYRIX_LAYOUT_RATIO_SCALE, available);
	int64_t high = SEYRIX_LAYOUT_RATIO_SCALE - ceil_divide(
		second_min * SEYRIX_LAYOUT_RATIO_SCALE, available);
	int64_t ratio = node->data.split.ratio;
	if (ratio < SEYRIX_LAYOUT_MIN_RATIO) {
		ratio = SEYRIX_LAYOUT_MIN_RATIO;
	} else if (ratio > SEYRIX_LAYOUT_MAX_RATIO) {
		ratio = SEYRIX_LAYOUT_MAX_RATIO;
	}
	if (ratio < low) {
		ratio = low;
	} else if (ratio > high) {
		ratio = high;
	}
	int64_t first_size = (int64_t)available * ratio /
		SEYRIX_LAYOUT_RATIO_SCALE;
	if (first_size < first_min) {
		first_size = first_min;
	} else if (first_size > (int64_t)available - second_min) {
		first_size = (int64_t)available - second_min;
	}
	int32_t first_extent = (int32_t)first_size;
	int32_t second_extent = available - first_extent;
	struct seyrix_layout_rect first_rect = rect;
	struct seyrix_layout_rect second_rect = rect;
	if (horizontal) {
		first_rect.width = first_extent;
		second_rect.x = (int32_t)((int64_t)rect.x + first_extent + gap);
		second_rect.width = second_extent;
	} else {
		first_rect.height = first_extent;
		second_rect.y = (int32_t)((int64_t)rect.y + first_extent + gap);
		second_rect.height = second_extent;
	}
	apply_node(node->data.split.first, first_rect, inner_gap, apply, data);
	apply_node(node->data.split.second, second_rect, inner_gap, apply, data);
}

bool seyrix_layout_reflow(struct seyrix_layout_node *root,
		struct seyrix_layout_rect area, int32_t outer_gap, int32_t inner_gap,
		seyrix_layout_apply_fn apply, void *data) {
	if (root == NULL) {
		return true;
	}
	if (apply == NULL || area.width <= 0 || area.height <= 0 ||
		(int64_t)area.x + area.width > INT32_MAX ||
		(int64_t)area.y + area.height > INT32_MAX) {
		return false;
	}
	if (outer_gap < 0) {
		outer_gap = 0;
	}
	if (inner_gap < 0) {
		inner_gap = 0;
	}
	int32_t horizontal_outer = clamp_gap(outer_gap, area.width);
	int32_t vertical_outer = clamp_gap(outer_gap, area.height);
	struct seyrix_layout_rect content = {
		.x = area.x + horizontal_outer,
		.y = area.y + vertical_outer,
		.width = area.width - 2 * horizontal_outer,
		.height = area.height - 2 * vertical_outer,
	};
	struct layout_requirement needed = requirement(root, inner_gap);
	if (needed.leaves == 0) {
		return true;
	}
	if (needed.width > content.width || needed.height > content.height) {
		/* Drop all gaps before declaring client minimums unsatisfiable. */
		content = area;
		needed = requirement(root, 0);
		if (needed.width > content.width || needed.height > content.height) {
			return false;
		}
		inner_gap = 0;
	}
	apply_node(root, content, inner_gap, apply, data);
	return true;
}

bool seyrix_layout_fit_client(struct seyrix_layout_rect cell,
		const struct seyrix_layout_limits *limits, int32_t titlebar_height,
		struct seyrix_layout_rect *client_box) {
	if (limits == NULL || client_box == NULL || cell.width <= 0 ||
			cell.height <= 0 || titlebar_height < 0 ||
			(int64_t)cell.x + cell.width > INT32_MAX ||
			(int64_t)cell.y + cell.height > INT32_MAX) {
		return false;
	}
	int32_t min_width = min_dimension(limits->min_width);
	int32_t min_outer_height = min_dimension(limits->min_height);
	int32_t min_height = min_outer_height - titlebar_height;
	if (min_height < 1) {
		min_height = 1;
	}
	int32_t width = cell.width;
	int32_t height = cell.height - titlebar_height;
	if (width < min_width || height < min_height) {
		return false;
	}
	int32_t max_width = limits->max_width;
	int32_t max_height = limits->max_height > 0 ?
		limits->max_height - titlebar_height : 0;
	if (max_width > 0 && max_width < min_width) {
		max_width = min_width;
	}
	if (max_height > 0 && max_height < min_height) {
		max_height = min_height;
	}
	if (max_width > 0 && width > max_width) {
		width = max_width;
	}
	if (max_height > 0 && height > max_height) {
		height = max_height;
	}
	int32_t outer_height = height + titlebar_height;
	client_box->x = cell.x + (cell.width - width) / 2;
	client_box->y = cell.y + (cell.height - outer_height) / 2;
	client_box->width = width;
	client_box->height = height;
	return true;
}

static void collect_split_nodes(struct seyrix_layout_node *node,
		struct node_array *array) {
	if (node == NULL || node->type == NODE_LEAF) {
		return;
	}
	if (array->count < SEYRIX_LAYOUT_MAX_LEAVES) {
		array->items[array->count++] = node;
	}
	collect_split_nodes(node->data.split.first, array);
	collect_split_nodes(node->data.split.second, array);
}

static struct seyrix_layout_node *build_balanced(
		struct seyrix_layout_node **leaves, size_t begin, size_t end,
		struct seyrix_layout_node **splits, size_t *split_index,
		enum seyrix_layout_axis axis, uint16_t ratio,
		struct seyrix_layout_node *parent) {
	if (end - begin == 1) {
		leaves[begin]->parent = parent;
		return leaves[begin];
	}
	size_t middle = begin + (end - begin) / 2;
	struct seyrix_layout_node *split = splits[(*split_index)++];
	memset(split, 0, sizeof(*split));
	split->type = NODE_SPLIT;
	split->parent = parent;
	split->data.split.axis = axis;
	split->data.split.ratio = clamp_ratio(ratio);
	enum seyrix_layout_axis next_axis = axis == SEYRIX_LAYOUT_HORIZONTAL ?
		SEYRIX_LAYOUT_VERTICAL : SEYRIX_LAYOUT_HORIZONTAL;
	split->data.split.first = build_balanced(leaves, begin, middle, splits,
		split_index, next_axis, ratio, split);
	split->data.split.second = build_balanced(leaves, middle, end, splits,
		split_index, next_axis, ratio, split);
	return split;
}

bool seyrix_layout_rebalance(struct seyrix_layout_node **root,
		enum seyrix_layout_axis first_axis, uint16_t ratio,
		const struct seyrix_layout_allocator *allocator) {
	if (root == NULL || *root == NULL ||
			(first_axis != SEYRIX_LAYOUT_HORIZONTAL &&
			first_axis != SEYRIX_LAYOUT_VERTICAL)) {
		return false;
	}
	struct node_array leaves = {0};
	collect_leaves(*root, &leaves);
	if (leaves.count < 2) {
		return true;
	}
	struct node_array old_splits = {0};
	collect_split_nodes(*root, &old_splits);
	struct seyrix_layout_node *new_splits[SEYRIX_LAYOUT_MAX_LEAVES - 1];
	memset(new_splits, 0, sizeof(new_splits));
	size_t required = leaves.count - 1;
	for (size_t i = 0; i < required; i++) {
		new_splits[i] = node_allocate(allocator);
		if (new_splits[i] == NULL) {
			for (size_t j = 0; j < i; j++) {
				node_release(new_splits[j], allocator);
			}
			return false;
		}
	}
	size_t split_index = 0;
	struct seyrix_layout_node *new_root = build_balanced(leaves.items, 0,
		leaves.count, new_splits, &split_index, first_axis, ratio, NULL);
	for (size_t i = 0; i < old_splits.count; i++) {
		node_release(old_splits.items[i], allocator);
	}
	*root = new_root;
	return true;
}

size_t seyrix_layout_leaf_count(const struct seyrix_layout_node *root) {
	return leaf_count(root);
}

size_t seyrix_layout_depth(const struct seyrix_layout_node *root) {
	return node_depth(root);
}

static bool validate_node(const struct seyrix_layout_node *node,
		const struct seyrix_layout_node *parent, size_t *count) {
	if (node == NULL || node->parent != parent) {
		return false;
	}
	if (node->type == NODE_LEAF) {
		if (node->data.leaf.owner == NULL || ++*count > SEYRIX_LAYOUT_MAX_LEAVES) {
			return false;
		}
		return true;
	}
	if (node->type != NODE_SPLIT || node->data.split.first == NULL ||
			node->data.split.second == NULL ||
			(node->data.split.axis != SEYRIX_LAYOUT_HORIZONTAL &&
			node->data.split.axis != SEYRIX_LAYOUT_VERTICAL) ||
			node->data.split.ratio < SEYRIX_LAYOUT_MIN_RATIO ||
			node->data.split.ratio > SEYRIX_LAYOUT_MAX_RATIO) {
		return false;
	}
	return validate_node(node->data.split.first, node, count) &&
		validate_node(node->data.split.second, node, count);
}

bool seyrix_layout_validate(const struct seyrix_layout_node *root) {
	if (root == NULL) {
		return true;
	}
	size_t count = 0;
	return validate_node(root, NULL, &count);
}
