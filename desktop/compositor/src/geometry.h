#ifndef SEYRIX_GEOMETRY_H
#define SEYRIX_GEOMETRY_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>

struct seyrix_resize_geometry {
	int scene_x;
	int scene_y;
	int client_x;
	int client_y;
	int width;
	int height;
};

/* Absolute device coordinates are normalized; wlroots maps them into layout space. */
static inline bool seyrix_cursor_warp_absolute(struct wlr_cursor *cursor,
		struct wlr_input_device *device, double x, double y) {
	if (cursor == NULL || !isfinite(x) || !isfinite(y) ||
			x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0) {
		return false;
	}
	wlr_cursor_warp_absolute(cursor, device, x, y);
	return true;
}

static inline bool seyrix_resize_grab_offsets(const struct wlr_box *box,
		uint32_t edges, double cursor_x, double cursor_y,
		double *grab_x, double *grab_y) {
	if (box == NULL || grab_x == NULL || grab_y == NULL ||
			box->width <= 0 || box->height <= 0 ||
			(edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT |
				WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) == 0) {
		return false;
	}
	*grab_x = 0.0;
	*grab_y = 0.0;
	if ((edges & WLR_EDGE_LEFT) != 0) {
		*grab_x = cursor_x - box->x;
	} else if ((edges & WLR_EDGE_RIGHT) != 0) {
		*grab_x = cursor_x - (box->x + box->width);
	}
	if ((edges & WLR_EDGE_TOP) != 0) {
		*grab_y = cursor_y - box->y;
	} else if ((edges & WLR_EDGE_BOTTOM) != 0) {
		*grab_y = cursor_y - (box->y + box->height);
	}
	return true;
}

static inline int seyrix_resize_constrain_dimension(int size, int min_size,
		int max_size) {
	if (size < 1) {
		size = 1;
	}
	if (min_size > 0 && size < min_size) {
		size = min_size;
	}
	if (min_size > 0 && max_size > 0 && max_size < min_size) {
		max_size = min_size;
	}
	if (max_size > 0 && size > max_size) {
		size = max_size;
	}
	return size;
}

/*
 * Resize an initial client-geometry box in global scene coordinates. The
 * unselected edge remains anchored when client min/max constraints clamp size.
 */
static inline bool seyrix_calculate_resize(const struct wlr_box *initial_box,
		double cursor_x, double cursor_y, double grab_x, double grab_y,
		uint32_t edges, int min_width, int min_height,
		int max_width, int max_height, int geometry_x, int geometry_y,
		int titlebar_height, struct seyrix_resize_geometry *result) {
	if (initial_box == NULL || result == NULL ||
			initial_box->width <= 0 || initial_box->height <= 0 ||
			(edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT |
				WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) == 0) {
		return false;
	}

	int left = initial_box->x;
	int right = initial_box->x + initial_box->width;
	int top = initial_box->y;
	int bottom = initial_box->y + initial_box->height;
	if ((edges & WLR_EDGE_LEFT) != 0) {
		left = (int)(cursor_x - grab_x);
		if (left >= right) {
			left = right - 1;
		}
	} else if ((edges & WLR_EDGE_RIGHT) != 0) {
		right = (int)(cursor_x - grab_x);
		if (right <= left) {
			right = left + 1;
		}
	}
	if ((edges & WLR_EDGE_TOP) != 0) {
		top = (int)(cursor_y - grab_y);
		if (top >= bottom) {
			top = bottom - 1;
		}
	} else if ((edges & WLR_EDGE_BOTTOM) != 0) {
		bottom = (int)(cursor_y - grab_y);
		if (bottom <= top) {
			bottom = top + 1;
		}
	}

	int width = seyrix_resize_constrain_dimension(right - left,
		min_width, max_width);
	int height = seyrix_resize_constrain_dimension(bottom - top,
		min_height, max_height);
	if ((edges & WLR_EDGE_LEFT) != 0) {
		left = right - width;
	} else {
		right = left + width;
	}
	if ((edges & WLR_EDGE_TOP) != 0) {
		top = bottom - height;
	} else {
		bottom = top + height;
	}

	result->client_x = left;
	result->client_y = top;
	result->scene_x = left - geometry_x;
	result->scene_y = top - geometry_y - titlebar_height;
	result->width = width;
	result->height = height;
	return true;
}

#endif
