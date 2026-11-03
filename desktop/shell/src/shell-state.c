#include "shell-state.h"

#include <stdio.h>
#include <string.h>

static size_t bounded_length(const char *value, size_t maximum) {
	if (value == NULL) {
		return 0;
	}
	size_t length = 0;
	while (length < maximum && value[length] != '\0') {
		length++;
	}
	return length;
}

static void copy_truncated(char *destination, size_t capacity,
		const char *source) {
	if (capacity == 0) {
		return;
	}
	size_t length = bounded_length(source, capacity - 1);
	if (length != 0) {
		memcpy(destination, source, length);
	}
	destination[length] = '\0';
}

static bool valid_app_id(const char *app_id) {
	size_t length = bounded_length(app_id, SEYRIX_SHELL_APP_ID_SIZE);
	if (length == 0 || length >= SEYRIX_SHELL_APP_ID_SIZE) {
		return false;
	}
	for (size_t index = 0; index < length; index++) {
		unsigned char character = (unsigned char)app_id[index];
		bool valid = (character >= 'a' && character <= 'z') ||
			(character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') ||
			character == '.' || character == '_' || character == '-';
		if (!valid) {
			return false;
		}
	}
	return true;
}

static bool has_active_focus(const struct seyrix_shell_state *state,
		const struct seyrix_shell_window *window) {
	return window->id == state->focused_window_id &&
		window->workspace == state->active_workspace &&
		(window->flags & SEYRIX_SHELL_WINDOW_MAPPED) != 0 &&
		(window->flags & SEYRIX_SHELL_WINDOW_MINIMIZED) == 0;
}

void seyrix_shell_state_init(struct seyrix_shell_state *state) {
	if (state == NULL) {
		return;
	}
	memset(state, 0, sizeof(*state));
	state->next_mru_order = 1;
}

static size_t find_window_index(const struct seyrix_shell_state *state,
		uint32_t window_id) {
	for (size_t index = 0; index < state->window_count; index++) {
		if (state->windows[index].id == window_id) {
			return index;
		}
	}
	return SIZE_MAX;
}

const struct seyrix_shell_window *seyrix_shell_state_find_window(
		const struct seyrix_shell_state *state, uint32_t window_id) {
	if (state == NULL || window_id == 0) {
		return NULL;
	}
	size_t index = find_window_index(state, window_id);
	return index == SIZE_MAX ? NULL : &state->windows[index];
}

static void clear_focus(struct seyrix_shell_state *state) {
	for (size_t index = 0; index < state->window_count; index++) {
		state->windows[index].flags &=
			(uint8_t)~SEYRIX_SHELL_WINDOW_FOCUSED;
	}
	state->focused_window_id = 0;
}

bool seyrix_shell_state_set_focus(struct seyrix_shell_state *state,
		uint32_t window_id) {
	if (state == NULL) {
		return false;
	}
	if (window_id == 0) {
		clear_focus(state);
		return true;
	}
	size_t index = find_window_index(state, window_id);
	if (index == SIZE_MAX) {
		return false;
	}
	struct seyrix_shell_window *window = &state->windows[index];
	if (window->workspace != state->active_workspace ||
			(window->flags & SEYRIX_SHELL_WINDOW_MAPPED) == 0 ||
			(window->flags & SEYRIX_SHELL_WINDOW_MINIMIZED) != 0) {
		return false;
	}
	clear_focus(state);
	window->flags |= SEYRIX_SHELL_WINDOW_FOCUSED;
	window->mru_order = state->next_mru_order++;
	if (state->next_mru_order == 0) {
		/* Keep ordering monotonic after wrap by renormalizing the tiny model. */
		for (size_t current = 0; current < state->window_count; current++) {
			state->windows[current].mru_order = current + 1;
		}
		state->next_mru_order = state->window_count + 1;
	}
	state->focused_window_id = window_id;
	return true;
}

bool seyrix_shell_state_upsert(struct seyrix_shell_state *state,
		uint32_t window_id, uint8_t workspace, uint8_t flags,
		const char *app_id, const char *title) {
	if (state == NULL || window_id == 0 ||
			workspace >= SEYRIX_SHELL_WORKSPACE_COUNT ||
			(flags & (uint8_t)~SEYRIX_SHELL_WINDOW_KNOWN_FLAGS) != 0) {
		return false;
	}
	bool focused = (flags & SEYRIX_SHELL_WINDOW_FOCUSED) != 0;
	if (focused && (workspace != state->active_workspace ||
			(flags & SEYRIX_SHELL_WINDOW_MAPPED) == 0 ||
			(flags & SEYRIX_SHELL_WINDOW_MINIMIZED) != 0)) {
		return false;
	}
	size_t index = find_window_index(state, window_id);
	if (index == SIZE_MAX) {
		if (state->window_count >= SEYRIX_SHELL_MAX_WINDOWS) {
			return false;
		}
		index = state->window_count++;
		memset(&state->windows[index], 0, sizeof(state->windows[index]));
		state->windows[index].id = window_id;
		state->windows[index].mru_order = state->next_mru_order++;
	}
	struct seyrix_shell_window *window = &state->windows[index];
	if (state->focused_window_id == window_id && !focused) {
		clear_focus(state);
	} else if (focused) {
		clear_focus(state);
	}
	window->workspace = workspace;
	window->flags = flags & (uint8_t)~SEYRIX_SHELL_WINDOW_FOCUSED;
	window->has_valid_app_id = valid_app_id(app_id);
	if (window->has_valid_app_id) {
		copy_truncated(window->app_id, sizeof(window->app_id), app_id);
	} else {
		window->app_id[0] = '\0';
	}
	copy_truncated(window->title, sizeof(window->title), title);
	if (focused) {
		window->flags |= SEYRIX_SHELL_WINDOW_FOCUSED;
		window->mru_order = state->next_mru_order++;
		state->focused_window_id = window_id;
	}
	return true;
}

bool seyrix_shell_state_remove(struct seyrix_shell_state *state,
		uint32_t window_id) {
	if (state == NULL || window_id == 0) {
		return false;
	}
	size_t index = find_window_index(state, window_id);
	if (index == SIZE_MAX) {
		return false;
	}
	if (state->focused_window_id == window_id) {
		state->focused_window_id = 0;
	}	
	if (index + 1 < state->window_count) {
		memmove(&state->windows[index], &state->windows[index + 1],
			(state->window_count - index - 1) * sizeof(state->windows[0]));
	}
	state->window_count--;
	memset(&state->windows[state->window_count], 0, sizeof(state->windows[0]));
	return true;
}

bool seyrix_shell_state_set_active_workspace(struct seyrix_shell_state *state,
		uint8_t workspace) {
	if (state == NULL || workspace >= SEYRIX_SHELL_WORKSPACE_COUNT) {
		return false;
	}
	state->active_workspace = workspace;
	const struct seyrix_shell_window *focused =
		seyrix_shell_state_find_window(state, state->focused_window_id);
	if (focused == NULL || focused->workspace != workspace) {
		clear_focus(state);
	}
	return true;
}

size_t seyrix_shell_state_switcher_windows(
		const struct seyrix_shell_state *state, uint32_t *ids, size_t capacity) {
	if (state == NULL || ids == NULL || capacity == 0) {
		return 0;
	}
	bool used[SEYRIX_SHELL_MAX_WINDOWS] = {false};
	size_t written = 0;
	while (written < capacity && written < state->window_count) {
		size_t best = SIZE_MAX;
		for (size_t index = 0; index < state->window_count; index++) {
			const struct seyrix_shell_window *window = &state->windows[index];
			if (used[index] || window->workspace != state->active_workspace ||
					(window->flags & SEYRIX_SHELL_WINDOW_MAPPED) == 0 ||
					(window->flags & SEYRIX_SHELL_WINDOW_MINIMIZED) != 0) {
				continue;
			}
			if (best == SIZE_MAX || window->mru_order >
					state->windows[best].mru_order) {
				best = index;
			}
		}
		if (best == SIZE_MAX) {
			break;
		}
		used[best] = true;
		ids[written++] = state->windows[best].id;
	}
	return written;
}

uint32_t seyrix_shell_state_next_window(
		const struct seyrix_shell_state *state, uint32_t current_window_id) {
	uint32_t ids[SEYRIX_SHELL_MAX_WINDOWS];
	size_t count = seyrix_shell_state_switcher_windows(state, ids,
		SEYRIX_SHELL_MAX_WINDOWS);
	for (size_t index = 0; index < count; index++) {
		if (ids[index] != current_window_id) {
			return ids[index];
		}
	}
	return 0;
}

static size_t pin_id_length(const char *pin_id) {
	size_t length = bounded_length(pin_id, SEYRIX_SHELL_APP_ID_SIZE);
	if (length == 0 || length >= SEYRIX_SHELL_APP_ID_SIZE ||
			!valid_app_id(pin_id)) {
		return 0;
	}
	if (length > sizeof(".desktop") - 1 &&
			strcmp(pin_id + length - (sizeof(".desktop") - 1), ".desktop") == 0) {
		length -= sizeof(".desktop") - 1;
	}
	return length;
}

static bool same_application(const struct seyrix_shell_window *left,
		const struct seyrix_shell_window *right) {
	if (left->has_valid_app_id && right->has_valid_app_id) {
		return strcmp(left->app_id, right->app_id) == 0;
	}
	return left->id == right->id;
}

static void set_application_key(struct seyrix_shell_application *application,
		const struct seyrix_shell_window *window) {
	if (window->has_valid_app_id) {
		copy_truncated(application->key, sizeof(application->key), window->app_id);
		copy_truncated(application->app_id, sizeof(application->app_id),
			window->app_id);
		application->has_valid_app_id = true;
		return;
	}
	(void)snprintf(application->key, sizeof(application->key),
		"window:%u", window->id);
	application->app_id[0] = '\0';
}

static void fill_running_application(const struct seyrix_shell_state *state,
		struct seyrix_shell_application *application, size_t seed_index) {
	const struct seyrix_shell_window *seed = &state->windows[seed_index];
	set_application_key(application, seed);
	const struct seyrix_shell_window *representative = NULL;
	for (size_t index = 0; index < state->window_count; index++) {
		const struct seyrix_shell_window *window = &state->windows[index];
		if (!same_application(seed, window) ||
				(window->flags & SEYRIX_SHELL_WINDOW_MAPPED) == 0) {
			continue;
		}
		application->workspace_mask |= (uint8_t)(1U << window->workspace);
		if (application->window_count < UINT16_MAX) {
			application->window_count++;
		}
		application->running = true;
		if (has_active_focus(state, window)) {
			representative = window;
			application->active = true;
		} else if (!application->active && (representative == NULL ||
				window->mru_order > representative->mru_order)) {
			representative = window;
		}
	}
	if (representative != NULL) {
		application->window_id = representative->id;
		application->representative_workspace = representative->workspace;
		application->representative_flags = representative->flags;
		copy_truncated(application->title, sizeof(application->title),
			representative->title);
	}
}

static size_t find_group_seed(const struct seyrix_shell_state *state,
		const char *app_id) {
	size_t best = SIZE_MAX;
	for (size_t index = 0; index < state->window_count; index++) {
		const struct seyrix_shell_window *window = &state->windows[index];
		if (!window->has_valid_app_id || strcmp(window->app_id, app_id) != 0) {
			continue;
		}
		if (best == SIZE_MAX || window->mru_order >
				state->windows[best].mru_order) {
			best = index;
		}
	}
	return best;
}

static bool same_pin(const char *left, const char *right) {
	size_t left_length = pin_id_length(left);
	size_t right_length = pin_id_length(right);
	return left_length != 0 && left_length == right_length &&
		memcmp(left, right, left_length) == 0;
}

static bool pin_already_emitted(const char *const *pin_ids,
		size_t pin_index) {
	for (size_t previous = 0; previous < pin_index; previous++) {
		if (same_pin(pin_ids[previous], pin_ids[pin_index])) {
			return true;
		}
	}
	return false;
}

size_t seyrix_shell_state_applications(
		const struct seyrix_shell_state *state,
		const char *const *pin_ids, size_t pin_count,
		struct seyrix_shell_application *applications, size_t capacity) {
	if (state == NULL || applications == NULL || capacity == 0) {
		return 0;
	}
	if (pin_count > SEYRIX_SHELL_MAX_PINS) {
		pin_count = SEYRIX_SHELL_MAX_PINS;
	}
	if (pin_ids == NULL) {
		pin_count = 0;
	}
	if (capacity > SEYRIX_SHELL_MAX_APPLICATIONS) {
		capacity = SEYRIX_SHELL_MAX_APPLICATIONS;
	}
	bool handled[SEYRIX_SHELL_MAX_WINDOWS] = {false};
	size_t written = 0;
	for (size_t pin = 0; pin < pin_count && written < capacity; pin++) {
		const char *pin_id = pin_ids[pin];
		size_t pin_length = pin_id_length(pin_id);
		if (pin_length == 0 || pin_already_emitted(pin_ids, pin)) {
			continue;
		}
		char normalized_pin[SEYRIX_SHELL_APP_ID_SIZE] = {0};
		memcpy(normalized_pin, pin_id, pin_length);
		size_t seed = find_group_seed(state, normalized_pin);
		struct seyrix_shell_application *application =
			&applications[written++];
		memset(application, 0, sizeof(*application));
		application->pinned = true;
		if (seed == SIZE_MAX) {
			copy_truncated(application->key, sizeof(application->key),
				normalized_pin);
			copy_truncated(application->app_id, sizeof(application->app_id),
				normalized_pin);
			application->has_valid_app_id = true;
			continue;
		}
		fill_running_application(state, application, seed);
		application->pinned = true;
		for (size_t index = 0; index < state->window_count; index++) {
			if (same_application(&state->windows[seed], &state->windows[index])) {
				handled[index] = true;
			}
		}
	}

	while (written < capacity) {
		size_t best = SIZE_MAX;
		for (size_t index = 0; index < state->window_count; index++) {
			const struct seyrix_shell_window *window = &state->windows[index];
			if (handled[index] ||
					(window->flags & SEYRIX_SHELL_WINDOW_MAPPED) == 0) {
				continue;
			}
			bool group_handled = false;
			for (size_t group = 0; group < state->window_count; group++) {
				if (handled[group] && same_application(window,
						&state->windows[group])) {
					group_handled = true;
					break;
				}
			}
			if (group_handled) {
				continue;
			}
			if (best == SIZE_MAX || window->mru_order >
					state->windows[best].mru_order) {
				best = index;
			}
		}
		if (best == SIZE_MAX) {
			break;
		}
		struct seyrix_shell_application *application =
			&applications[written++];
		memset(application, 0, sizeof(*application));
		fill_running_application(state, application, best);
		for (size_t index = 0; index < state->window_count; index++) {
			if (same_application(&state->windows[best], &state->windows[index])) {
				handled[index] = true;
			}
		}
	}
	return written;
}
