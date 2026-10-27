#ifndef SEYRIX_SHELL_STATE_H
#define SEYRIX_SHELL_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/shell-ipc.h"

/* Keep these limits in sync with the bounded compositor-to-shell packet. */
enum {
	SEYRIX_SHELL_WORKSPACE_COUNT = 4,
	SEYRIX_SHELL_MAX_WINDOWS = 128,
	SEYRIX_SHELL_MAX_PINS = 64,
	SEYRIX_SHELL_MAX_APPLICATIONS =
		SEYRIX_SHELL_MAX_WINDOWS + SEYRIX_SHELL_MAX_PINS,
	SEYRIX_SHELL_APP_ID_SIZE = 128,
	SEYRIX_SHELL_TITLE_SIZE = 256,
	SEYRIX_SHELL_KEY_SIZE = 144,
};

struct seyrix_shell_window {
	uint32_t id;
	uint8_t workspace;
	uint8_t flags;
	bool has_valid_app_id;
	char app_id[SEYRIX_SHELL_APP_ID_SIZE];
	char title[SEYRIX_SHELL_TITLE_SIZE];
	uint64_t mru_order;
};

struct seyrix_shell_state {
	struct seyrix_shell_window windows[SEYRIX_SHELL_MAX_WINDOWS];
	size_t window_count;
	uint8_t active_workspace;
	uint32_t focused_window_id;
	uint64_t next_mru_order;
};

/* Shared app records are used by dock and taskbar presentation code. */
struct seyrix_shell_application {
	char key[SEYRIX_SHELL_KEY_SIZE];
	char app_id[SEYRIX_SHELL_APP_ID_SIZE];
	char title[SEYRIX_SHELL_TITLE_SIZE];
	uint32_t window_id;
	uint8_t representative_workspace;
	uint8_t workspace_mask;
	uint8_t representative_flags;
	uint16_t window_count;
	bool has_valid_app_id;
	bool pinned;
	bool running;
	bool active;
};

void seyrix_shell_state_init(struct seyrix_shell_state *state);

/* Snapshot updates reject zero IDs, invalid workspaces, unknown flags, or
 * an over-capacity model. Long titles are safely truncated; invalid/missing
 * application IDs remain distinct per-window identities. */
bool seyrix_shell_state_upsert(struct seyrix_shell_state *state,
	uint32_t window_id, uint8_t workspace, uint8_t flags,
	const char *app_id, const char *title);
bool seyrix_shell_state_remove(struct seyrix_shell_state *state,
	uint32_t window_id);
bool seyrix_shell_state_set_active_workspace(struct seyrix_shell_state *state,
	uint8_t workspace);
bool seyrix_shell_state_set_focus(struct seyrix_shell_state *state,
	uint32_t window_id);

const struct seyrix_shell_window *seyrix_shell_state_find_window(
	const struct seyrix_shell_state *state, uint32_t window_id);

/* Returns mapped, non-minimized windows on the active workspace in MRU order.
 * If ids is NULL or capacity is zero, zero is returned. */
size_t seyrix_shell_state_switcher_windows(
	const struct seyrix_shell_state *state, uint32_t *ids, size_t capacity);

/* Returns the first switcher target except current_window_id, or zero. */
uint32_t seyrix_shell_state_next_window(
	const struct seyrix_shell_state *state, uint32_t current_window_id);

/* Build one shared application list for either dock or taskbar. Pins appear
 * first in configured order, including non-running pins. Remaining running
 * applications follow their most-recent window order. `pin_ids` are desktop
 * IDs or app IDs; a trailing ".desktop" is ignored when matching an app ID.
 * Returns the number of records written, limited by capacity. */
size_t seyrix_shell_state_applications(
	const struct seyrix_shell_state *state,
	const char *const *pin_ids, size_t pin_count,
	struct seyrix_shell_application *applications, size_t capacity);

#endif
