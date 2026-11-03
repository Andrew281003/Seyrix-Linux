#define _POSIX_C_SOURCE 200809L

#include "shell-config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum {
	CONFIG_MAX_BYTES = 65536,
	ICON_SIZE_MIN = 24,
	ICON_SIZE_MAX = 64,
};

static void set_error(char *error, size_t size, const char *format, ...) {
	if (error == NULL || size == 0) {
		return;
	}
	va_list arguments;
	va_start(arguments, format);
	(void)vsnprintf(error, size, format, arguments);
	va_end(arguments);
}

static bool copy_string(char *destination, size_t capacity, const char *source) {
	size_t length = strlen(source);
	if (capacity == 0 || length >= capacity) {
		return false;
	}
	memcpy(destination, source, length + 1);
	return true;
}

void seyrix_shell_config_defaults(struct seyrix_shell_config *config) {
	if (config == NULL) {
		return;
	}
	memset(config, 0, sizeof(*config));
	config->mode = SEYRIX_SHELL_MODE_DOCK;
	config->position = SEYRIX_SHELL_POSITION_BOTTOM;
	config->dock_alignment = SEYRIX_SHELL_ALIGNMENT_CENTER;
	config->icon_size = 40;
	config->auto_hide = false;
	(void)copy_string(config->background_color,
		sizeof(config->background_color), "#1C2229");
	(void)copy_string(config->launcher_shortcut,
		sizeof(config->launcher_shortcut), "Super+Space");
	(void)copy_string(config->switcher_shortcut,
		sizeof(config->switcher_shortcut), "Super+Tab");
	config->switcher_active_workspace_only = true;
}

static char *trim(char *text) {
	while (*text == ' ' || *text == '\t' || *text == '\r') {
		text++;
	}
	size_t length = strlen(text);
	while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
			text[length - 1] == '\r')) {
		text[--length] = '\0';
	}
	return text;
}

static bool valid_color(const char *value) {
	if (strnlen(value, 8) != 7 || value[0] != '#') {
		return false;
	}
	for (size_t index = 1; index < 7; index++) {
		char c = value[index];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
				(c >= 'A' && c <= 'F'))) {
			return false;
		}
	}
	return true;
}

static bool valid_wallpaper_path(const char *path) {
	size_t path_length = strnlen(path, SEYRIX_SHELL_PATH_MAX);
	if (path_length == SEYRIX_SHELL_PATH_MAX) {
		return false;
	}
	if (path_length == 0) {
		return true;
	}
	if (path[0] != '/') {
		return false;
	}
	const unsigned char *cursor = (const unsigned char *)path;
	for (; *cursor != '\0'; cursor++) {
		if (*cursor < 0x20 || *cursor == 0x7f) {
			return false;
		}
	}
	const char *component = path;
	while (*component != '\0') {
		while (*component == '/') {
			component++;
		}
		const char *end = strchr(component, '/');
		size_t length = end != NULL ? (size_t)(end - component) : strlen(component);
		if ((length == 1 && component[0] == '.') ||
				(length == 2 && component[0] == '.' && component[1] == '.')) {
			return false;
		}
		component = end != NULL ? end : component + length;
	}
	return true;
}

static bool valid_desktop_id(const char *identifier) {
	size_t length = strnlen(identifier, SEYRIX_SHELL_DESKTOP_ID_MAX + 1);
	if (length < 9 || length > SEYRIX_SHELL_DESKTOP_ID_MAX ||
			identifier[0] == '.' || strcmp(identifier, "..") == 0 ||
			strcmp(identifier + length - 8, ".desktop") != 0) {
		return false;
	}
	for (size_t index = 0; index < length; index++) {
		char c = identifier[index];
		bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
		if (!allowed) {
			return false;
		}
		if (c == '.' && index > 0 && identifier[index - 1] == '.') {
			return false;
		}
	}
	return true;
}

static bool valid_launcher_shortcut(const char *shortcut) {
	return strnlen(shortcut, SEYRIX_SHELL_SHORTCUT_MAX + 1) <=
			SEYRIX_SHELL_SHORTCUT_MAX && (strcmp(shortcut, "Super+Space") == 0 ||
		strcmp(shortcut, "Super+D") == 0 ||
		strcmp(shortcut, "Super+P") == 0);
}

static bool valid_switcher_shortcut(const char *shortcut) {
	return strnlen(shortcut, SEYRIX_SHELL_SHORTCUT_MAX + 1) <=
			SEYRIX_SHELL_SHORTCUT_MAX && (strcmp(shortcut, "Super+Tab") == 0 ||
		strcmp(shortcut, "Alt+Tab") == 0);
}

static bool validate_config(const struct seyrix_shell_config *config,
		char *error, size_t error_size) {
	if (config == NULL) {
		set_error(error, error_size, "configuration is null");
		return false;
	}
	if (config->mode != SEYRIX_SHELL_MODE_DOCK &&
			config->mode != SEYRIX_SHELL_MODE_TASKBAR) {
		set_error(error, error_size, "invalid shell mode");
		return false;
	}
	if (config->position != SEYRIX_SHELL_POSITION_TOP &&
			config->position != SEYRIX_SHELL_POSITION_BOTTOM) {
		set_error(error, error_size, "invalid shell position");
		return false;
	}
	if (config->dock_alignment < SEYRIX_SHELL_ALIGNMENT_START ||
			config->dock_alignment > SEYRIX_SHELL_ALIGNMENT_END) {
		set_error(error, error_size, "invalid dock alignment");
		return false;
	}
	if (config->icon_size < ICON_SIZE_MIN || config->icon_size > ICON_SIZE_MAX) {
		set_error(error, error_size, "icon_size must be between 24 and 64");
		return false;
	}
	if (!valid_wallpaper_path(config->wallpaper)) {
		set_error(error, error_size, "wallpaper must be an absolute local path without control characters or dot traversal");
		return false;
	}
	if (!valid_color(config->background_color)) {
		set_error(error, error_size, "background_color must use #RRGGBB format");
		return false;
	}
	if (config->pinned_count > SEYRIX_SHELL_PINNED_MAX) {
		set_error(error, error_size, "too many pinned applications");
		return false;
	}
	for (size_t index = 0; index < config->pinned_count; index++) {
		if (!valid_desktop_id(config->pinned_apps[index])) {
			set_error(error, error_size, "invalid desktop ID in pinned_apps");
			return false;
		}
		for (size_t other = 0; other < index; other++) {
			if (strcmp(config->pinned_apps[index], config->pinned_apps[other]) == 0) {
				set_error(error, error_size, "duplicate desktop ID in pinned_apps");
				return false;
			}
		}
	}
	if (!valid_launcher_shortcut(config->launcher_shortcut)) {
		set_error(error, error_size, "unsupported launcher_shortcut");
		return false;
	}
	if (!valid_switcher_shortcut(config->switcher_shortcut)) {
		set_error(error, error_size, "unsupported switcher_shortcut");
		return false;
	}
	return true;
}

static bool parse_boolean(const char *value, bool *result) {
	if (strcmp(value, "true") == 0) {
		*result = true;
		return true;
	}
	if (strcmp(value, "false") == 0) {
		*result = false;
		return true;
	}
	return false;
}

static bool parse_integer(const char *value, int *result) {
	if (value[0] == '\0') {
		return false;
	}
	int number = 0;
	for (const char *cursor = value; *cursor != '\0'; cursor++) {
		if (*cursor < '0' || *cursor > '9') {
			return false;
		}
		int digit = *cursor - '0';
		if (number > (ICON_SIZE_MAX * 10 - digit) / 10) {
			return false;
		}
		number = number * 10 + digit;
	}
	if (number < ICON_SIZE_MIN || number > ICON_SIZE_MAX) {
		return false;
	}
	*result = number;
	return true;
}

static bool parse_pinned_apps(const char *value,
		struct seyrix_shell_config *config) {
	config->pinned_count = 0;
	if (value[0] == '\0') {
		return true;
	}
	char *copy = strdup(value);
	if (copy == NULL) {
		return false;
	}
	char *save = NULL;
	char *item = strtok_r(copy, ",", &save);
	while (item != NULL) {
		item = trim(item);
		if (config->pinned_count == SEYRIX_SHELL_PINNED_MAX ||
				!valid_desktop_id(item)) {
			free(copy);
			return false;
		}
		for (size_t index = 0; index < config->pinned_count; index++) {
			if (strcmp(config->pinned_apps[index], item) == 0) {
				free(copy);
				return false;
			}
		}
		(void)copy_string(config->pinned_apps[config->pinned_count],
			sizeof(config->pinned_apps[0]), item);
		config->pinned_count++;
		item = strtok_r(NULL, ",", &save);
	}
	free(copy);
	/* strtok_r collapses delimiters, so explicitly reject empty list members. */
	return value[0] != ',' && value[strlen(value) - 1] != ',' &&
		strstr(value, ",,") == NULL;
}

bool seyrix_shell_config_parse(const char *text, size_t length,
		struct seyrix_shell_config *config, char *error, size_t error_size) {
	if (config == NULL || (text == NULL && length != 0) ||
			length > CONFIG_MAX_BYTES ||
			(text != NULL && memchr(text, '\0', length) != NULL)) {
		set_error(error, error_size, "invalid configuration text");
		return false;
	}
	char *copy = malloc(length + 1);
	if (copy == NULL) {
		set_error(error, error_size, "out of memory parsing configuration");
		return false;
	}
	if (length > 0) {
		memcpy(copy, text, length);
	}
	copy[length] = '\0';

	struct seyrix_shell_config parsed;
	seyrix_shell_config_defaults(&parsed);
	unsigned int seen = 0;
	char *line_cursor = copy;
	unsigned int line_number = 0;
	while (line_cursor != NULL) {
		char *line = line_cursor;
		char *newline = strchr(line_cursor, '\n');
		if (newline != NULL) {
			*newline = '\0';
			line_cursor = newline + 1;
		} else {
			line_cursor = NULL;
		}
		line_number++;
		line = trim(line);
		if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
			continue;
		}
		char *equals = strchr(line, '=');
		if (equals == NULL) {
			set_error(error, error_size, "line %u: expected key=value", line_number);
			free(copy);
			return false;
		}
		*equals = '\0';
		char *key = trim(line);
		char *value = trim(equals + 1);
		if (key[0] == '\0' || (value[0] == '\0' &&
				strcmp(key, "wallpaper") != 0 && strcmp(key, "pinned_apps") != 0)) {
			set_error(error, error_size, "line %u: empty key or value", line_number);
			free(copy);
			return false;
		}
		unsigned int bit = 0;
		if (strcmp(key, "version") == 0) bit = 1U << 0;
		else if (strcmp(key, "mode") == 0) bit = 1U << 1;
		else if (strcmp(key, "position") == 0) bit = 1U << 2;
		else if (strcmp(key, "dock_alignment") == 0) bit = 1U << 3;
		else if (strcmp(key, "icon_size") == 0) bit = 1U << 4;
		else if (strcmp(key, "auto_hide") == 0) bit = 1U << 5;
		else if (strcmp(key, "wallpaper") == 0) bit = 1U << 6;
		else if (strcmp(key, "background_color") == 0) bit = 1U << 7;
		else if (strcmp(key, "pinned_apps") == 0) bit = 1U << 8;
		else if (strcmp(key, "launcher_shortcut") == 0) bit = 1U << 9;
		else if (strcmp(key, "switcher_shortcut") == 0) bit = 1U << 10;
		else if (strcmp(key, "switcher_active_workspace_only") == 0) bit = 1U << 11;
		else {
			set_error(error, error_size, "line %u: unknown key '%s'", line_number, key);
			free(copy);
			return false;
		}
		if ((seen & bit) != 0) {
			set_error(error, error_size, "line %u: duplicate key '%s'", line_number, key);
			free(copy);
			return false;
		}
		seen |= bit;

		bool valid = true;
		if (strcmp(key, "version") == 0) {
			valid = strcmp(value, "1") == 0;
		} else if (strcmp(key, "mode") == 0) {
			if (strcmp(value, "dock") == 0) parsed.mode = SEYRIX_SHELL_MODE_DOCK;
			else if (strcmp(value, "taskbar") == 0) parsed.mode = SEYRIX_SHELL_MODE_TASKBAR;
			else valid = false;
		} else if (strcmp(key, "position") == 0) {
			if (strcmp(value, "top") == 0) parsed.position = SEYRIX_SHELL_POSITION_TOP;
			else if (strcmp(value, "bottom") == 0) parsed.position = SEYRIX_SHELL_POSITION_BOTTOM;
			else valid = false;
		} else if (strcmp(key, "dock_alignment") == 0) {
			if (strcmp(value, "start") == 0) parsed.dock_alignment = SEYRIX_SHELL_ALIGNMENT_START;
			else if (strcmp(value, "center") == 0) parsed.dock_alignment = SEYRIX_SHELL_ALIGNMENT_CENTER;
			else if (strcmp(value, "end") == 0) parsed.dock_alignment = SEYRIX_SHELL_ALIGNMENT_END;
			else valid = false;
		} else if (strcmp(key, "icon_size") == 0) {
			valid = parse_integer(value, &parsed.icon_size);
		} else if (strcmp(key, "auto_hide") == 0) {
			valid = parse_boolean(value, &parsed.auto_hide);
		} else if (strcmp(key, "wallpaper") == 0) {
			valid = valid_wallpaper_path(value) &&
				copy_string(parsed.wallpaper, sizeof(parsed.wallpaper), value);
		} else if (strcmp(key, "background_color") == 0) {
			valid = valid_color(value) &&
				copy_string(parsed.background_color, sizeof(parsed.background_color), value);
		} else if (strcmp(key, "pinned_apps") == 0) {
			valid = parse_pinned_apps(value, &parsed);
		} else if (strcmp(key, "launcher_shortcut") == 0) {
			valid = valid_launcher_shortcut(value) &&
				copy_string(parsed.launcher_shortcut,
					sizeof(parsed.launcher_shortcut), value);
		} else if (strcmp(key, "switcher_shortcut") == 0) {
			valid = valid_switcher_shortcut(value) &&
				copy_string(parsed.switcher_shortcut,
					sizeof(parsed.switcher_shortcut), value);
		} else if (strcmp(key, "switcher_active_workspace_only") == 0) {
			valid = parse_boolean(value, &parsed.switcher_active_workspace_only);
		}
		if (!valid) {
			set_error(error, error_size, "line %u: invalid value for '%s'", line_number, key);
			free(copy);
			return false;
		}
	}
	free(copy);
	if (!validate_config(&parsed, error, error_size)) {
		return false;
	}
	*config = parsed;
	if (error != NULL && error_size > 0) {
		error[0] = '\0';
	}
	return true;
}

static bool read_config_fd(int fd, struct seyrix_shell_config *config,
		char *error, size_t error_size) {
	struct stat status;
	if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
			status.st_size < 0 || status.st_size > CONFIG_MAX_BYTES) {
		set_error(error, error_size, "configuration is not a regular file or exceeds 64 KiB");
		return false;
	}
	size_t capacity = (size_t)status.st_size;
	char *contents = malloc(capacity + 1);
	if (contents == NULL) {
		set_error(error, error_size, "out of memory reading configuration");
		return false;
	}
	size_t length = 0;
	while (length < capacity) {
		ssize_t count = read(fd, contents + length, capacity - length);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			set_error(error, error_size, "failed to read configuration: %s",
				count == 0 ? "unexpected end of file" : strerror(errno));
			free(contents);
			return false;
		}
		length += (size_t)count;
	}
	contents[length] = '\0';
	bool result = seyrix_shell_config_parse(contents, length, config,
		error, error_size);
	free(contents);
	return result;
}

bool seyrix_shell_config_load(const char *path,
		struct seyrix_shell_config *config, char *error, size_t error_size) {
	if (config == NULL || path == NULL || path[0] != '/') {
		set_error(error, error_size, "configuration path must be absolute");
		return false;
	}
	struct seyrix_shell_config parsed;
	seyrix_shell_config_defaults(&parsed);
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		if (errno == ENOENT) {
			*config = parsed;
			if (error != NULL && error_size > 0) error[0] = '\0';
			return true;
		}
		set_error(error, error_size, "cannot open configuration: %s", strerror(errno));
		return false;
	}
	bool result = read_config_fd(fd, &parsed, error, error_size);
	int saved_errno = errno;
	close(fd);
	errno = saved_errno;
	if (result) {
		*config = parsed;
	}
	return result;
}

static bool append_format(char *buffer, size_t capacity, size_t *used,
		const char *format, ...) {
	if (*used >= capacity) {
		return false;
	}
	va_list arguments;
	va_start(arguments, format);
	int count = vsnprintf(buffer + *used, capacity - *used, format, arguments);
	va_end(arguments);
	if (count < 0 || (size_t)count >= capacity - *used) {
		return false;
	}
	*used += (size_t)count;
	return true;
}

static char *serialize_config(const struct seyrix_shell_config *config,
		size_t *length, char *error, size_t error_size) {
	if (!validate_config(config, error, error_size)) {
		return NULL;
	}
	size_t capacity = SEYRIX_SHELL_PATH_MAX +
		SEYRIX_SHELL_PINNED_MAX * (SEYRIX_SHELL_DESKTOP_ID_MAX + 1) + 1024;
	char *buffer = malloc(capacity);
	if (buffer == NULL) {
		set_error(error, error_size, "out of memory serializing configuration");
		return NULL;
	}
	size_t used = 0;
	const char *mode = config->mode == SEYRIX_SHELL_MODE_DOCK ? "dock" : "taskbar";
	const char *position = config->position == SEYRIX_SHELL_POSITION_TOP ? "top" : "bottom";
	const char *alignment = config->dock_alignment == SEYRIX_SHELL_ALIGNMENT_START ? "start" :
		config->dock_alignment == SEYRIX_SHELL_ALIGNMENT_END ? "end" : "center";
	if (!append_format(buffer, capacity, &used,
			"version=1\nmode=%s\nposition=%s\ndock_alignment=%s\nicon_size=%d\n"
			"auto_hide=%s\nwallpaper=%s\nbackground_color=%s\npinned_apps=",
			mode, position, alignment, config->icon_size,
			config->auto_hide ? "true" : "false", config->wallpaper,
			config->background_color)) {
		free(buffer);
		set_error(error, error_size, "serialized configuration is too large");
		return NULL;
	}
	for (size_t index = 0; index < config->pinned_count; index++) {
		if (!append_format(buffer, capacity, &used, "%s%s",
				index == 0 ? "" : ",", config->pinned_apps[index])) {
			free(buffer);
			set_error(error, error_size, "serialized configuration is too large");
			return NULL;
		}
	}
	if (!append_format(buffer, capacity, &used,
			"\nlauncher_shortcut=%s\nswitcher_shortcut=%s\n"
			"switcher_active_workspace_only=%s\n",
			config->launcher_shortcut, config->switcher_shortcut,
			config->switcher_active_workspace_only ? "true" : "false")) {
		free(buffer);
		set_error(error, error_size, "serialized configuration is too large");
		return NULL;
	}
	*length = used;
	return buffer;
}

static bool write_all(int fd, const char *buffer, size_t length) {
	size_t written = 0;
	while (written < length) {
		ssize_t count = write(fd, buffer + written, length - written);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			return false;
		}
		written += (size_t)count;
	}
	return true;
}

static bool atomic_write_at(int directory_fd, const char *filename,
		const char *contents, size_t length, char *error, size_t error_size) {
	char temporary[96];
	int fd = -1;
	for (unsigned int attempt = 0; attempt < 32; attempt++) {
		int count = snprintf(temporary, sizeof(temporary),
			".%s.tmp.%ld.%u", filename, (long)getpid(), attempt);
		if (count < 0 || (size_t)count >= sizeof(temporary)) {
			set_error(error, error_size, "configuration filename is too long");
			return false;
		}
		fd = openat(directory_fd, temporary,
			O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (fd >= 0 || errno != EEXIST) {
			break;
		}
	}
	if (fd < 0) {
		set_error(error, error_size, "cannot create temporary configuration: %s",
			strerror(errno));
		return false;
	}
	bool success = fchmod(fd, 0600) == 0 && write_all(fd, contents, length) &&
		fsync(fd) == 0;
	int saved_errno = errno;
	if (close(fd) != 0 && success) {
		success = false;
		saved_errno = errno;
	}
	if (success && renameat(directory_fd, temporary, directory_fd, filename) != 0) {
		success = false;
		saved_errno = errno;
	}
	if (success && fsync(directory_fd) != 0) {
		success = false;
		saved_errno = errno;
	}
	if (!success) {
		(void)unlinkat(directory_fd, temporary, 0);
		errno = saved_errno;
		set_error(error, error_size, "cannot atomically save configuration: %s",
			strerror(errno));
		return false;
	}
	if (error != NULL && error_size > 0) error[0] = '\0';
	return true;
}

bool seyrix_shell_config_save(const char *path,
		const struct seyrix_shell_config *config, char *error, size_t error_size) {
	if (path == NULL || path[0] != '/') {
		set_error(error, error_size, "configuration path must be absolute");
		return false;
	}
	size_t length = 0;
	char *contents = serialize_config(config, &length, error, error_size);
	if (contents == NULL) {
		return false;
	}
	char *copy = strdup(path);
	if (copy == NULL) {
		free(contents);
		set_error(error, error_size, "out of memory preparing configuration path");
		return false;
	}
	char *slash = strrchr(copy, '/');
	char *filename = slash + 1;
	if (filename[0] == '\0' || strcmp(filename, ".") == 0 ||
			strcmp(filename, "..") == 0) {
		free(copy);
		free(contents);
		set_error(error, error_size, "configuration path has no filename");
		return false;
	}
	if (slash == copy) {
		slash[1] = '\0';
	} else {
		*slash = '\0';
	}
	int directory_fd = open(copy, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directory_fd < 0) {
		set_error(error, error_size, "cannot open configuration directory: %s", strerror(errno));
		free(copy);
		free(contents);
		return false;
	}
	bool result = atomic_write_at(directory_fd, filename, contents, length,
		error, error_size);
	int saved_errno = errno;
	close(directory_fd);
	errno = saved_errno;
	free(copy);
	free(contents);
	return result;
}

static bool valid_absolute_directory(const char *path) {
	if (path == NULL || path[0] != '/') {
		return false;
	}
	for (const unsigned char *cursor = (const unsigned char *)path;
			*cursor != '\0'; cursor++) {
		if (*cursor < 0x20 || *cursor == 0x7f) {
			return false;
		}
	}
	const char *component = path;
	while (*component != '\0') {
		while (*component == '/') component++;
		const char *end = strchr(component, '/');
		size_t length = end != NULL ? (size_t)(end - component) : strlen(component);
		if ((length == 1 && component[0] == '.') ||
				(length == 2 && component[0] == '.' && component[1] == '.')) {
			return false;
		}
		component = end != NULL ? end : component + length;
	}
	return strlen(path) + sizeof("/seyrix/shell.conf") <= SEYRIX_SHELL_PATH_MAX;
}

bool seyrix_shell_config_user_path(char *path, size_t path_size,
		char *error, size_t error_size) {
	if (path == NULL || path_size == 0) {
		set_error(error, error_size, "path output buffer is empty");
		return false;
	}
	const char *base = getenv("XDG_CONFIG_HOME");
	if (base == NULL || base[0] == '\0') {
		base = getenv("HOME");
		if (base == NULL || base[0] != '/') {
			set_error(error, error_size, "HOME must be absolute when XDG_CONFIG_HOME is unset");
			return false;
		}
		if (strlen(base) + sizeof("/.config/seyrix/shell.conf") > path_size ||
				!valid_absolute_directory(base)) {
			set_error(error, error_size, "HOME is invalid or too long");
			return false;
		}
		int count = snprintf(path, path_size, "%s/.config/seyrix/shell.conf", base);
		if (count < 0 || (size_t)count >= path_size) {
			set_error(error, error_size, "configuration path is too long");
			return false;
		}
	} else {
		if (!valid_absolute_directory(base)) {
			set_error(error, error_size, "XDG_CONFIG_HOME must be an absolute path without dot traversal");
			return false;
		}
		int count = snprintf(path, path_size, "%s/seyrix/shell.conf", base);
		if (count < 0 || (size_t)count >= path_size) {
			set_error(error, error_size, "configuration path is too long");
			return false;
		}
	}
	if (error != NULL && error_size > 0) error[0] = '\0';
	return true;
}

static int open_directory_chain(const char *absolute_path, bool create,
		char *error, size_t error_size) {
	if (!valid_absolute_directory(absolute_path)) {
		set_error(error, error_size, "directory path is invalid");
		errno = EINVAL;
		return -1;
	}
	int directory = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (directory < 0) {
		set_error(error, error_size, "cannot open root directory: %s", strerror(errno));
		return -1;
	}
	char *copy = strdup(absolute_path + 1);
	if (copy == NULL) {
		close(directory);
		set_error(error, error_size, "out of memory opening configuration directory");
		return -1;
	}
	char *save = NULL;
	for (char *component = strtok_r(copy, "/", &save); component != NULL;
			component = strtok_r(NULL, "/", &save)) {
		if (create && mkdirat(directory, component, 0700) != 0 && errno != EEXIST) {
			set_error(error, error_size, "cannot create configuration directory: %s", strerror(errno));
			close(directory);
			free(copy);
			return -1;
		}
		int next = openat(directory, component,
			O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (next < 0) {
			set_error(error, error_size, "cannot open configuration directory: %s", strerror(errno));
			close(directory);
			free(copy);
			return -1;
		}
		close(directory);
		directory = next;
	}
	free(copy);
	struct stat status;
	if (fstat(directory, &status) != 0 || !S_ISDIR(status.st_mode) ||
			status.st_uid != geteuid() || (status.st_mode & 0022) != 0) {
		set_error(error, error_size, "seyrix configuration directory must be owned by this user and not group/world writable");
		close(directory);
		errno = EPERM;
		return -1;
	}
	return directory;
}

static int open_user_directory(bool create, char *error, size_t error_size) {
	char config_path[SEYRIX_SHELL_PATH_MAX];
	if (!seyrix_shell_config_user_path(config_path, sizeof(config_path), error, error_size)) {
		return -1;
	}
	char *slash = strrchr(config_path, '/');
	if (slash == NULL) {
		set_error(error, error_size, "invalid user configuration path");
		return -1;
	}
	*slash = '\0';
	return open_directory_chain(config_path, create, error, error_size);
}

bool seyrix_shell_config_load_user(struct seyrix_shell_config *config,
		char *error, size_t error_size) {
	if (config == NULL) {
		set_error(error, error_size, "configuration output is null");
		return false;
	}
	struct seyrix_shell_config parsed;
	seyrix_shell_config_defaults(&parsed);
	int directory = open_user_directory(false, error, error_size);
	if (directory < 0) {
		if (errno == ENOENT) {
			*config = parsed;
			if (error != NULL && error_size > 0) error[0] = '\0';
			return true;
		}
		return false;
	}
	int fd = openat(directory, "shell.conf", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	int saved_errno = errno;
	close(directory);
	errno = saved_errno;
	if (fd < 0 && errno == ENOENT) {
		*config = parsed;
		if (error != NULL && error_size > 0) error[0] = '\0';
		return true;
	}
	if (fd < 0) {
		set_error(error, error_size, "cannot open shell.conf: %s", strerror(errno));
		return false;
	}
	bool result = read_config_fd(fd, &parsed, error, error_size);
	saved_errno = errno;
	close(fd);
	errno = saved_errno;
	if (result) *config = parsed;
	return result;
}

bool seyrix_shell_config_save_user(const struct seyrix_shell_config *config,
		char *error, size_t error_size) {
	size_t length = 0;
	char *contents = serialize_config(config, &length, error, error_size);
	if (contents == NULL) {
		return false;
	}
	int directory = open_user_directory(true, error, error_size);
	if (directory < 0) {
		free(contents);
		return false;
	}
	bool result = atomic_write_at(directory, "shell.conf", contents, length,
		error, error_size);
	int saved_errno = errno;
	close(directory);
	errno = saved_errno;
	free(contents);
	return result;
}
