#define _POSIX_C_SOURCE 200809L

#include "../desktop/shell/src/shell-config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;

#define CHECK(expression) do { \
	checks++; \
	if (!(expression)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return false; \
	} \
} while (0)

static bool parse_rejects(const char *text) {
	struct seyrix_shell_config config;
	seyrix_shell_config_defaults(&config);
	char error[256] = {0};
	return !seyrix_shell_config_parse(text, strlen(text), &config,
		error, sizeof(error)) && error[0] != '\0';
}

static bool test_defaults_and_parser(void) {
	struct seyrix_shell_config config;
	seyrix_shell_config_defaults(&config);
	CHECK(config.mode == SEYRIX_SHELL_MODE_DOCK);
	CHECK(config.position == SEYRIX_SHELL_POSITION_BOTTOM);
	CHECK(config.dock_alignment == SEYRIX_SHELL_ALIGNMENT_CENTER);
	CHECK(config.icon_size == 40);
	CHECK(!config.auto_hide);
	CHECK(config.wallpaper[0] == '\0');
	CHECK(strcmp(config.background_color, "#1C2229") == 0);
	CHECK(config.pinned_count == 0);
	CHECK(strcmp(config.launcher_shortcut, "Super+Space") == 0);
	CHECK(strcmp(config.switcher_shortcut, "Super+Tab") == 0);
	CHECK(config.switcher_active_workspace_only);

	const char valid[] =
		"# Seyrix shell preferences\n"
		"version=1\n"
		"mode=taskbar\n"
		"position=top\n"
		"dock_alignment=end\n"
		"icon_size=64\n"
		"auto_hide=true\n"
		"wallpaper=/home/test/Pictures/wall paper.png\n"
		"background_color=#aBcD09\n"
		"pinned_apps=firefox.desktop, org.videolan.vlc.desktop\n"
		"launcher_shortcut=Super+D\n"
		"switcher_shortcut=Alt+Tab\n"
		"switcher_active_workspace_only=false\n";
	char error[256] = {0};
	CHECK(seyrix_shell_config_parse(valid, sizeof(valid) - 1,
		&config, error, sizeof(error)));
	CHECK(error[0] == '\0');
	CHECK(config.mode == SEYRIX_SHELL_MODE_TASKBAR);
	CHECK(config.position == SEYRIX_SHELL_POSITION_TOP);
	CHECK(config.dock_alignment == SEYRIX_SHELL_ALIGNMENT_END);
	CHECK(config.icon_size == 64);
	CHECK(config.auto_hide);
	CHECK(strcmp(config.wallpaper, "/home/test/Pictures/wall paper.png") == 0);
	CHECK(strcmp(config.background_color, "#aBcD09") == 0);
	CHECK(config.pinned_count == 2);
	CHECK(strcmp(config.pinned_apps[0], "firefox.desktop") == 0);
	CHECK(strcmp(config.pinned_apps[1], "org.videolan.vlc.desktop") == 0);
	CHECK(strcmp(config.launcher_shortcut, "Super+D") == 0);
	CHECK(strcmp(config.switcher_shortcut, "Alt+Tab") == 0);
	CHECK(!config.switcher_active_workspace_only);
	return true;
}

static bool test_parser_rejects_bad_input(void) {
	CHECK(parse_rejects("mode=launch;id;\n"));
	CHECK(parse_rejects("version=2\n"));
	CHECK(parse_rejects("version=1\nversion=1\n"));
	CHECK(parse_rejects("mode=taskbar\nmode=dock\n"));
	CHECK(parse_rejects("icon_size=23\n"));
	CHECK(parse_rejects("icon_size=65\n"));
	CHECK(parse_rejects("icon_size=48;touch /tmp/pwned\n"));
	CHECK(parse_rejects("auto_hide=yes\n"));
	CHECK(parse_rejects("background_color=blue\n"));
	CHECK(parse_rejects("background_color=#12345678\n"));
	CHECK(parse_rejects("wallpaper=relative.png\n"));
	CHECK(parse_rejects("wallpaper=/tmp/../tmp/wall.png\n"));
	CHECK(parse_rejects("wallpaper=/tmp/a\nbad\n"));
	CHECK(parse_rejects("pinned_apps=../../evil.desktop\n"));
	CHECK(parse_rejects("pinned_apps=firefox\n"));
	CHECK(parse_rejects("pinned_apps=firefox.desktop,\n"));
	CHECK(parse_rejects("pinned_apps=firefox.desktop,,vlc.desktop\n"));
	CHECK(parse_rejects("pinned_apps=firefox.desktop,firefox.desktop\n"));
	CHECK(parse_rejects("launcher_shortcut=Super+Q\n"));
	CHECK(parse_rejects("switcher_shortcut=Super+Q\n"));
	CHECK(parse_rejects("switcher_active_workspace_only=1\n"));
	CHECK(parse_rejects("unexpected=ignored\n"));
	const char embedded_nul[] = {'m', 'o', 'd', 'e', '=', 'd', 'o', 'c', 'k', '\0', '\n'};
	struct seyrix_shell_config config;
	char error[256] = {0};
	CHECK(!seyrix_shell_config_parse(embedded_nul, sizeof(embedded_nul),
		&config, error, sizeof(error)));
	CHECK(error[0] != '\0');
	return true;
}

static bool equal_config(const struct seyrix_shell_config *left,
		const struct seyrix_shell_config *right) {
	if (left->mode != right->mode || left->position != right->position ||
			left->dock_alignment != right->dock_alignment ||
			left->icon_size != right->icon_size ||
			left->auto_hide != right->auto_hide ||
			strcmp(left->wallpaper, right->wallpaper) != 0 ||
			strcmp(left->background_color, right->background_color) != 0 ||
			left->pinned_count != right->pinned_count ||
			strcmp(left->launcher_shortcut, right->launcher_shortcut) != 0 ||
			strcmp(left->switcher_shortcut, right->switcher_shortcut) != 0 ||
			left->switcher_active_workspace_only != right->switcher_active_workspace_only) {
		return false;
	}
	for (size_t index = 0; index < left->pinned_count; index++) {
		if (strcmp(left->pinned_apps[index], right->pinned_apps[index]) != 0) {
			return false;
		}
	}
	return true;
}

static bool directory_has_no_temp_files(const char *path) {
	DIR *directory = opendir(path);
	if (directory == NULL) return false;
	bool clear = true;
	struct dirent *entry;
	while ((entry = readdir(directory)) != NULL) {
		if (strstr(entry->d_name, ".tmp.") != NULL) clear = false;
	}
	closedir(directory);
	return clear;
}

static bool test_load_save_atomicity(void) {
	char template[] = "/tmp/seyrix-shell-config-test-XXXXXX";
	char *directory = mkdtemp(template);
	CHECK(directory != NULL);
	char path[PATH_MAX];
	int count = snprintf(path, sizeof(path), "%s/shell.conf", directory);
	CHECK(count > 0 && (size_t)count < sizeof(path));
	char main_file[PATH_MAX];
	CHECK(snprintf(main_file, sizeof(main_file), "%s", path) > 0);

	struct seyrix_shell_config original;
	seyrix_shell_config_defaults(&original);
	original.mode = SEYRIX_SHELL_MODE_TASKBAR;
	original.position = SEYRIX_SHELL_POSITION_TOP;
	original.dock_alignment = SEYRIX_SHELL_ALIGNMENT_START;
	original.icon_size = 56;
	original.auto_hide = true;
	(void)snprintf(original.wallpaper, sizeof(original.wallpaper),
		"%s/Pictures/wall paper.png", directory);
	(void)snprintf(original.background_color, sizeof(original.background_color), "#123aBc");
	original.pinned_count = 2;
	(void)snprintf(original.pinned_apps[0], sizeof(original.pinned_apps[0]), "firefox.desktop");
	(void)snprintf(original.pinned_apps[1], sizeof(original.pinned_apps[1]), "org.videolan.vlc.desktop");
	(void)snprintf(original.launcher_shortcut, sizeof(original.launcher_shortcut), "Super+P");
	(void)snprintf(original.switcher_shortcut, sizeof(original.switcher_shortcut), "Alt+Tab");
	original.switcher_active_workspace_only = false;
	char error[256] = {0};
	CHECK(seyrix_shell_config_save(path, &original, error, sizeof(error)));
	CHECK(error[0] == '\0');
	struct stat status;
	CHECK(stat(path, &status) == 0 && S_ISREG(status.st_mode));
	CHECK((status.st_mode & 0777) == 0600);
	CHECK(directory_has_no_temp_files(directory));
	struct seyrix_shell_config invalid_config = original;
	memset(invalid_config.wallpaper, 'x', sizeof(invalid_config.wallpaper));
	CHECK(!seyrix_shell_config_save(path, &invalid_config, error, sizeof(error)));
	invalid_config = original;
	memset(invalid_config.background_color, 'x', sizeof(invalid_config.background_color));
	CHECK(!seyrix_shell_config_save(path, &invalid_config, error, sizeof(error)));
	invalid_config = original;
	memset(invalid_config.launcher_shortcut, 'x', sizeof(invalid_config.launcher_shortcut));
	CHECK(!seyrix_shell_config_save(path, &invalid_config, error, sizeof(error)));

	struct seyrix_shell_config loaded;
	CHECK(seyrix_shell_config_load(path, &loaded, error, sizeof(error)));
	CHECK(equal_config(&original, &loaded));

	struct seyrix_shell_config missing;
	seyrix_shell_config_defaults(&missing);
	(void)snprintf(path, sizeof(path), "%s/missing.conf", directory);
	CHECK(seyrix_shell_config_load(path, &loaded, error, sizeof(error)));
	CHECK(equal_config(&missing, &loaded));

	/* Parsing a bad file fails and leaves the caller's object unchanged. */
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	CHECK(fd >= 0);
	const char invalid[] = "mode=not-a-mode\n";
	CHECK(write(fd, invalid, sizeof(invalid) - 1) == (ssize_t)(sizeof(invalid) - 1));
	CHECK(close(fd) == 0);
	struct seyrix_shell_config sentinel;
	seyrix_shell_config_defaults(&sentinel);
	sentinel.mode = SEYRIX_SHELL_MODE_TASKBAR;
	loaded = sentinel;
	CHECK(!seyrix_shell_config_load(path, &loaded, error, sizeof(error)));
	CHECK(equal_config(&sentinel, &loaded));

	/* Atomic rename replaces a symlink itself, never its target. */
	char target[PATH_MAX];
	char link_path[PATH_MAX];
	CHECK(snprintf(target, sizeof(target), "%s/target", directory) > 0);
	CHECK(snprintf(link_path, sizeof(link_path), "%s/link.conf", directory) > 0);
	fd = open(target, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	CHECK(fd >= 0);
	CHECK(write(fd, "sentinel", 8) == 8);
	CHECK(close(fd) == 0);
	CHECK(symlink(target, link_path) == 0);
	seyrix_shell_config_defaults(&original);
	CHECK(seyrix_shell_config_save(link_path, &original, error, sizeof(error)));
	CHECK(lstat(link_path, &status) == 0 && S_ISREG(status.st_mode));
	char content[16] = {0};
	fd = open(target, O_RDONLY | O_CLOEXEC);
	CHECK(fd >= 0);
	CHECK(read(fd, content, sizeof(content)) == 8);
	CHECK(close(fd) == 0);
	CHECK(strcmp(content, "sentinel") == 0);
	CHECK(directory_has_no_temp_files(directory));

	/* Remove all test artifacts. */
	CHECK(unlink(main_file) == 0);
	CHECK(unlink(path) == 0);
	CHECK(unlink(link_path) == 0);
	CHECK(unlink(target) == 0);
	CHECK(rmdir(directory) == 0);
	return true;
}

static bool test_xdg_path_and_user_persistence(void) {
	char template[] = "/tmp/seyrix-shell-xdg-test-XXXXXX";
	char *directory = mkdtemp(template);
	CHECK(directory != NULL);
	char path[PATH_MAX];
	char error[256] = {0};
	CHECK(setenv("XDG_CONFIG_HOME", directory, 1) == 0);
	CHECK(seyrix_shell_config_user_path(path, sizeof(path), error, sizeof(error)));
	char expected[PATH_MAX];
	CHECK(snprintf(expected, sizeof(expected), "%s/seyrix/shell.conf", directory) > 0);
	CHECK(strcmp(path, expected) == 0);

	struct seyrix_shell_config config;
	seyrix_shell_config_defaults(&config);
	config.mode = SEYRIX_SHELL_MODE_TASKBAR;
	config.pinned_count = 1;
	(void)snprintf(config.pinned_apps[0], sizeof(config.pinned_apps[0]), "firefox.desktop");
	CHECK(seyrix_shell_config_save_user(&config, error, sizeof(error)));
	struct stat status;
	CHECK(stat(path, &status) == 0 && (status.st_mode & 0777) == 0600);
	CHECK(seyrix_shell_config_load_user(&config, error, sizeof(error)));
	CHECK(config.mode == SEYRIX_SHELL_MODE_TASKBAR && config.pinned_count == 1);
	CHECK(strcmp(config.pinned_apps[0], "firefox.desktop") == 0);

	CHECK(setenv("XDG_CONFIG_HOME", "relative/path", 1) == 0);
	CHECK(!seyrix_shell_config_user_path(path, sizeof(path), error, sizeof(error)));
	CHECK(unsetenv("XDG_CONFIG_HOME") == 0);
	CHECK(setenv("HOME", directory, 1) == 0);
	CHECK(seyrix_shell_config_user_path(path, sizeof(path), error, sizeof(error)));
	CHECK(snprintf(expected, sizeof(expected), "%s/.config/seyrix/shell.conf", directory) > 0);
	CHECK(strcmp(path, expected) == 0);

	/* Cleanup the user config tree created through save_user. */
	CHECK(setenv("XDG_CONFIG_HOME", directory, 1) == 0);
	char app_dir[PATH_MAX];
	CHECK(snprintf(app_dir, sizeof(app_dir), "%s/seyrix", directory) > 0);
	CHECK(unlink(path) != 0); /* HOME path does not exist. */
	CHECK(snprintf(path, sizeof(path), "%s/shell.conf", app_dir) > 0);
	CHECK(unlink(path) == 0);
	CHECK(rmdir(app_dir) == 0);
	CHECK(rmdir(directory) == 0);
	return true;
}

int main(void) {
	if (!test_defaults_and_parser() || !test_parser_rejects_bad_input() ||
			!test_load_save_atomicity() || !test_xdg_path_and_user_persistence()) {
		return 1;
	}
	printf("PASS shell config tests: %d checks\n", checks);
	return 0;
}
