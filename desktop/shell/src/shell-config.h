#ifndef SEYRIX_SHELL_CONFIG_H
#define SEYRIX_SHELL_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#define SEYRIX_SHELL_PINNED_MAX 32
#define SEYRIX_SHELL_DESKTOP_ID_MAX 127
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define SEYRIX_SHELL_PATH_MAX PATH_MAX
#define SEYRIX_SHELL_SHORTCUT_MAX 31

enum seyrix_shell_mode {
	SEYRIX_SHELL_MODE_DOCK = 0,
	SEYRIX_SHELL_MODE_TASKBAR,
};

enum seyrix_shell_position {
	SEYRIX_SHELL_POSITION_TOP = 0,
	SEYRIX_SHELL_POSITION_BOTTOM,
};

enum seyrix_shell_alignment {
	SEYRIX_SHELL_ALIGNMENT_START = 0,
	SEYRIX_SHELL_ALIGNMENT_CENTER,
	SEYRIX_SHELL_ALIGNMENT_END,
};

struct seyrix_shell_config {
	enum seyrix_shell_mode mode;
	enum seyrix_shell_position position;
	enum seyrix_shell_alignment dock_alignment;
	int icon_size;
	bool auto_hide;
	char wallpaper[SEYRIX_SHELL_PATH_MAX];
	char background_color[8];
	char pinned_apps[SEYRIX_SHELL_PINNED_MAX][SEYRIX_SHELL_DESKTOP_ID_MAX + 1];
	size_t pinned_count;
	char launcher_shortcut[SEYRIX_SHELL_SHORTCUT_MAX + 1];
	char switcher_shortcut[SEYRIX_SHELL_SHORTCUT_MAX + 1];
	bool switcher_active_workspace_only;
};

void seyrix_shell_config_defaults(struct seyrix_shell_config *config);

/* Parse plain key=value data. Configuration text is never interpreted as code. */
bool seyrix_shell_config_parse(const char *text, size_t length,
	struct seyrix_shell_config *config, char *error, size_t error_size);

/* A missing file is a successful load of safe defaults. */
bool seyrix_shell_config_load(const char *path,
	struct seyrix_shell_config *config, char *error, size_t error_size);
bool seyrix_shell_config_save(const char *path,
	const struct seyrix_shell_config *config, char *error, size_t error_size);

/* Resolve $XDG_CONFIG_HOME/seyrix/shell.conf (or $HOME/.config/seyrix/shell.conf). */
bool seyrix_shell_config_user_path(char *path, size_t path_size,
	char *error, size_t error_size);
bool seyrix_shell_config_load_user(struct seyrix_shell_config *config,
	char *error, size_t error_size);
bool seyrix_shell_config_save_user(const struct seyrix_shell_config *config,
	char *error, size_t error_size);

#endif
