#include "shell-state.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static bool add_window(struct seyrix_shell_state *state, uint32_t id,
		uint8_t workspace, uint8_t flags, const char *app_id,
		const char *title) {
	return seyrix_shell_state_upsert(state, id, workspace, flags, app_id, title);
}

static const struct seyrix_shell_application *find_app(
		const struct seyrix_shell_application *apps, size_t count,
		const char *app_id) {
	for (size_t index = 0; index < count; index++) {
		if (strcmp(apps[index].app_id, app_id) == 0) {
			return &apps[index];
		}
	}
	return NULL;
}

static void test_create_group_and_destroy(void) {
	struct seyrix_shell_state state;
	seyrix_shell_state_init(&state);
	CHECK(add_window(&state, 11, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.Editor", "Editor one"));
	CHECK(add_window(&state, 12, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.Editor", "Editor two"));
	CHECK(add_window(&state, 13, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.Terminal", "Terminal"));
	struct seyrix_shell_application apps[SEYRIX_SHELL_MAX_APPLICATIONS];
	size_t count = seyrix_shell_state_applications(&state, NULL, 0,
		apps, SEYRIX_SHELL_MAX_APPLICATIONS);
	CHECK(count == 2);
	const struct seyrix_shell_application *editor = find_app(apps, count,
		"org.example.Editor");
	CHECK(editor != NULL);
	CHECK(editor != NULL && editor->window_count == 2);
	CHECK(editor != NULL && editor->running);
	CHECK(editor != NULL && editor->window_id == 12);
	CHECK(seyrix_shell_state_remove(&state, 12));
	count = seyrix_shell_state_applications(&state, NULL, 0,
		apps, SEYRIX_SHELL_MAX_APPLICATIONS);
	CHECK(count == 2);
	editor = find_app(apps, count, "org.example.Editor");
	CHECK(editor != NULL && editor->window_count == 1);
	CHECK(editor != NULL && editor->window_id == 11);
	CHECK(!seyrix_shell_state_remove(&state, 99));
	CHECK(seyrix_shell_state_remove(&state, 11));
	count = seyrix_shell_state_applications(&state, NULL, 0,
		apps, SEYRIX_SHELL_MAX_APPLICATIONS);
	CHECK(count == 1);
	CHECK(strcmp(apps[0].app_id, "org.example.Terminal") == 0);
}

static void test_missing_and_invalid_app_ids_are_per_window(void) {
	struct seyrix_shell_state state;
	seyrix_shell_state_init(&state);
	CHECK(add_window(&state, 1, 0, SEYRIX_SHELL_WINDOW_MAPPED, NULL, "No ID A"));
	CHECK(add_window(&state, 2, 0, SEYRIX_SHELL_WINDOW_MAPPED, "", "No ID B"));
	CHECK(add_window(&state, 3, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"not an app id", "Invalid ID"));
	struct seyrix_shell_application apps[SEYRIX_SHELL_MAX_APPLICATIONS];
	size_t count = seyrix_shell_state_applications(&state, NULL, 0,
		apps, SEYRIX_SHELL_MAX_APPLICATIONS);
	CHECK(count == 3);
	CHECK(!apps[0].has_valid_app_id);
	CHECK(strcmp(apps[0].key, "window:3") == 0);
	CHECK(strcmp(apps[1].key, "window:2") == 0);
	CHECK(strcmp(apps[2].key, "window:1") == 0);
	CHECK(strcmp(apps[2].title, "No ID A") == 0);
}

static void test_workspace_focus_and_switcher_filtering(void) {
	struct seyrix_shell_state state;
	seyrix_shell_state_init(&state);
	CHECK(add_window(&state, 20, 0, SEYRIX_SHELL_WINDOW_MAPPED |
		SEYRIX_SHELL_WINDOW_FOCUSED, "org.example.A", "A"));
	CHECK(add_window(&state, 21, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.B", "B"));
	CHECK(add_window(&state, 22, 1, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.C", "C hidden"));
	CHECK(state.focused_window_id == 20);
	CHECK(seyrix_shell_state_set_focus(&state, 21));
	CHECK(state.focused_window_id == 21);
	uint32_t ids[8] = {0};
	CHECK(seyrix_shell_state_switcher_windows(&state, ids, 8) == 2);
	CHECK(ids[0] == 21 && ids[1] == 20);
	CHECK(seyrix_shell_state_next_window(&state, 21) == 20);
	CHECK(seyrix_shell_state_set_active_workspace(&state, 1));
	CHECK(state.focused_window_id == 0);
	CHECK(seyrix_shell_state_switcher_windows(&state, ids, 8) == 1);
	CHECK(ids[0] == 22);
	CHECK(!seyrix_shell_state_set_focus(&state, 20));
	CHECK(seyrix_shell_state_set_focus(&state, 22));
	CHECK(state.focused_window_id == 22);
	CHECK(!seyrix_shell_state_set_active_workspace(&state,
		SEYRIX_SHELL_WORKSPACE_COUNT));
}

static void test_minimize_unmap_restore_and_focus(void) {
	struct seyrix_shell_state state;
	seyrix_shell_state_init(&state);
	CHECK(add_window(&state, 30, 0, SEYRIX_SHELL_WINDOW_MAPPED |
		SEYRIX_SHELL_WINDOW_FOCUSED, "org.example.App", "main"));
	CHECK(add_window(&state, 31, 0, SEYRIX_SHELL_WINDOW_MAPPED |
		SEYRIX_SHELL_WINDOW_MINIMIZED, "org.example.App", "minimized"));
	uint32_t ids[4] = {0};
	CHECK(seyrix_shell_state_switcher_windows(&state, ids, 4) == 1);
	CHECK(ids[0] == 30);
	CHECK(!seyrix_shell_state_set_focus(&state, 31));
	CHECK(add_window(&state, 30, 0, 0, "org.example.App", "unmapped"));
	CHECK(state.focused_window_id == 0);
	CHECK(seyrix_shell_state_switcher_windows(&state, ids, 4) == 0);
	CHECK(add_window(&state, 30, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.App", "restored"));
	CHECK(seyrix_shell_state_set_focus(&state, 30));
	CHECK(seyrix_shell_state_switcher_windows(&state, ids, 4) == 1);
	CHECK(ids[0] == 30);
	CHECK(add_window(&state, 31, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.App", "restored sibling"));
	struct seyrix_shell_application apps[SEYRIX_SHELL_MAX_APPLICATIONS];
	size_t count = seyrix_shell_state_applications(&state, NULL, 0,
		apps, SEYRIX_SHELL_MAX_APPLICATIONS);
	CHECK(count == 1);
	CHECK(apps[0].window_count == 2);
	CHECK(apps[0].active);
	CHECK(apps[0].window_id == 30);
}

static void test_mru_order_and_destroyed_windows(void) {
	struct seyrix_shell_state state;
	seyrix_shell_state_init(&state);
	CHECK(add_window(&state, 40, 0, SEYRIX_SHELL_WINDOW_MAPPED |
		SEYRIX_SHELL_WINDOW_FOCUSED, "org.example.A", "A"));
	CHECK(add_window(&state, 41, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.B", "B"));
	CHECK(add_window(&state, 42, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.C", "C"));
	CHECK(seyrix_shell_state_set_focus(&state, 41));
	CHECK(seyrix_shell_state_set_focus(&state, 40));
	uint32_t ids[4] = {0};
	CHECK(seyrix_shell_state_switcher_windows(&state, ids, 4) == 3);
	CHECK(ids[0] == 40 && ids[1] == 41 && ids[2] == 42);
	CHECK(seyrix_shell_state_next_window(&state, 40) == 41);
	CHECK(seyrix_shell_state_remove(&state, 40));
	CHECK(state.focused_window_id == 0);
	CHECK(seyrix_shell_state_next_window(&state, 40) == 41);
	CHECK(seyrix_shell_state_remove(&state, 41));
	CHECK(seyrix_shell_state_remove(&state, 42));
	CHECK(state.window_count == 0);
}

static void test_pinned_application_entries_and_shared_mode_data(void) {
	struct seyrix_shell_state state;
	seyrix_shell_state_init(&state);
	CHECK(add_window(&state, 50, 0, SEYRIX_SHELL_WINDOW_MAPPED |
		SEYRIX_SHELL_WINDOW_FOCUSED, "org.example.Editor", "Editor"));
	CHECK(add_window(&state, 51, 2, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.Editor", "Editor other workspace"));
	CHECK(add_window(&state, 52, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.Terminal", "Terminal"));
	const char *pins[] = {
		"org.example.Editor.desktop",
		"org.example.Launcher.desktop",
		"org.example.Editor",
		"bad app id",
	};
	struct seyrix_shell_application dock_entries[SEYRIX_SHELL_MAX_APPLICATIONS];
	struct seyrix_shell_application taskbar_entries[SEYRIX_SHELL_MAX_APPLICATIONS];
	size_t dock_count = seyrix_shell_state_applications(&state, pins,
		4, dock_entries, SEYRIX_SHELL_MAX_APPLICATIONS);
	size_t taskbar_count = seyrix_shell_state_applications(&state, pins,
		4, taskbar_entries, SEYRIX_SHELL_MAX_APPLICATIONS);
	CHECK(dock_count == 3);
	CHECK(taskbar_count == dock_count);
	CHECK(dock_entries[0].pinned && dock_entries[0].running);
	CHECK(strcmp(dock_entries[0].app_id, "org.example.Editor") == 0);
	CHECK(dock_entries[0].window_count == 2);
	CHECK(dock_entries[0].workspace_mask == ((1U << 0) | (1U << 2)));
	CHECK(dock_entries[0].active && dock_entries[0].window_id == 50);
	CHECK(dock_entries[1].pinned && !dock_entries[1].running);
	CHECK(strcmp(dock_entries[1].app_id, "org.example.Launcher") == 0);
	CHECK(!dock_entries[2].pinned &&
		strcmp(dock_entries[2].app_id, "org.example.Terminal") == 0);
	CHECK(strcmp(dock_entries[2].key, taskbar_entries[2].key) == 0);
	CHECK(dock_entries[2].window_id == taskbar_entries[2].window_id);
}

static void test_snapshot_validation_and_safe_title_truncation(void) {
	struct seyrix_shell_state state;
	seyrix_shell_state_init(&state);
	CHECK(!add_window(&state, 0, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.App", "zero ID"));
	CHECK(!add_window(&state, 1, 4, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.App", "bad workspace"));
	CHECK(!add_window(&state, 1, 0, 0x80U, "org.example.App", "bad flags"));
	CHECK(!add_window(&state, 1, 0, SEYRIX_SHELL_WINDOW_FOCUSED,
		"org.example.App", "not mapped"));
	char long_title[SEYRIX_SHELL_TITLE_SIZE + 30];
	memset(long_title, 'X', sizeof(long_title));
	long_title[sizeof(long_title) - 1] = '\0';
	CHECK(add_window(&state, 1, 0, SEYRIX_SHELL_WINDOW_MAPPED,
		"org.example.App", long_title));
	const struct seyrix_shell_window *window =
		seyrix_shell_state_find_window(&state, 1);
	CHECK(window != NULL);
	CHECK(strlen(window->title) == SEYRIX_SHELL_TITLE_SIZE - 1);
	CHECK(!seyrix_shell_state_find_window(&state, 0));
}

int main(void) {
	test_create_group_and_destroy();
	test_missing_and_invalid_app_ids_are_per_window();
	test_workspace_focus_and_switcher_filtering();
	test_minimize_unmap_restore_and_focus();
	test_mru_order_and_destroyed_windows();
	test_pinned_application_entries_and_shared_mode_data();
	test_snapshot_validation_and_safe_title_truncation();
	if (failures != 0) {
		fprintf(stderr, "shell state tests: %u failure(s)\n", failures);
		return 1;
	}
	puts("shell state tests: PASS");
	return 0;
}
