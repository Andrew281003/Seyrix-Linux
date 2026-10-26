#define _GNU_SOURCE

#include "wm-config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CONFIG_LINE_MAX 512U
#define CONFIG_LINE_COUNT_MAX 512U

enum preference_field {
	PREF_MODE = 1U << 0,
	PREF_INNER_GAP = 1U << 1,
	PREF_OUTER_GAP = 1U << 2,
	PREF_RATIO = 1U << 3,
	PREF_AXIS = 1U << 4,
};

static void set_error(char *error, size_t error_size, const char *message) {
	if (error != NULL && error_size > 0) {
		snprintf(error, error_size, "%s", message);
	}
}

static char *trim(char *text) {
	while (isspace((unsigned char)*text)) {
		text++;
	}
	size_t length = strlen(text);
	while (length > 0 && isspace((unsigned char)text[length - 1])) {
		text[--length] = '\0';
	}
	return text;
}

static void free_rules(struct seyrix_app_rule *rule) {
	while (rule != NULL) {
		struct seyrix_app_rule *next = rule->next;
		free(rule->app_id);
		free(rule);
		rule = next;
	}
}

void seyrix_wm_config_defaults(struct seyrix_wm_config *config) {
	memset(config, 0, sizeof(*config));
	config->defaults.mode = SEYRIX_LAYOUT_HYBRID;
	config->defaults.inner_gap = 12;
	config->defaults.outer_gap = 12;
	config->defaults.split_ratio = 500;
	config->defaults.split_axis = SEYRIX_SPLIT_AUTO;
	for (size_t i = 0; i < SEYRIX_WM_WORKSPACE_COUNT; i++) {
		config->workspaces[i] = config->defaults;
	}
}

void seyrix_wm_config_finish(struct seyrix_wm_config *config) {
	if (config == NULL) {
		return;
	}
	free_rules(config->app_rules);
	free(config->path);
	memset(config, 0, sizeof(*config));
}

static bool parse_mode(const char *text, enum seyrix_layout_mode *mode) {
	if (strcmp(text, "floating") == 0) {
		*mode = SEYRIX_LAYOUT_FLOATING;
	} else if (strcmp(text, "tiling") == 0) {
		*mode = SEYRIX_LAYOUT_TILING;
	} else if (strcmp(text, "hybrid") == 0) {
		*mode = SEYRIX_LAYOUT_HYBRID;
	} else {
		return false;
	}
	return true;
}

static const char *mode_name(enum seyrix_layout_mode mode) {
	switch (mode) {
	case SEYRIX_LAYOUT_FLOATING:
		return "floating";
	case SEYRIX_LAYOUT_TILING:
		return "tiling";
	case SEYRIX_LAYOUT_HYBRID:
		return "hybrid";
	}
	return "hybrid";
}

static bool parse_axis(const char *text,
		enum seyrix_split_axis_policy *axis) {
	if (strcmp(text, "auto") == 0) {
		*axis = SEYRIX_SPLIT_AUTO;
	} else if (strcmp(text, "horizontal") == 0) {
		*axis = SEYRIX_SPLIT_HORIZONTAL;
	} else if (strcmp(text, "vertical") == 0) {
		*axis = SEYRIX_SPLIT_VERTICAL;
	} else {
		return false;
	}
	return true;
}

static const char *axis_name(enum seyrix_split_axis_policy axis) {
	switch (axis) {
	case SEYRIX_SPLIT_AUTO:
		return "auto";
	case SEYRIX_SPLIT_HORIZONTAL:
		return "horizontal";
	case SEYRIX_SPLIT_VERTICAL:
		return "vertical";
	}
	return "auto";
}

static bool parse_integer(const char *text, long minimum, long maximum,
		long *value) {
	char *end = NULL;
	errno = 0;
	long parsed = strtol(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' ||
			parsed < minimum || parsed > maximum) {
		return false;
	}
	*value = parsed;
	return true;
}

static bool set_preference(struct seyrix_layout_preferences *preferences,
		const char *field, const char *value, unsigned *seen) {
	long number;
	if (strcmp(field, "mode") == 0 || strcmp(field, "default_mode") == 0) {
		if ((*seen & PREF_MODE) != 0 || !parse_mode(value, &preferences->mode)) {
			return false;
		}
		*seen |= PREF_MODE;
		return true;
	}
	if (strcmp(field, "inner_gap") == 0) {
		if ((*seen & PREF_INNER_GAP) != 0 ||
				!parse_integer(value, 0, SEYRIX_WM_GAP_MAX, &number)) {
			return false;
		}
		preferences->inner_gap = (int32_t)number;
		*seen |= PREF_INNER_GAP;
		return true;
	}
	if (strcmp(field, "outer_gap") == 0) {
		if ((*seen & PREF_OUTER_GAP) != 0 ||
				!parse_integer(value, 0, SEYRIX_WM_GAP_MAX, &number)) {
			return false;
		}
		preferences->outer_gap = (int32_t)number;
		*seen |= PREF_OUTER_GAP;
		return true;
	}
	if (strcmp(field, "split_ratio") == 0) {
		if ((*seen & PREF_RATIO) != 0 || !parse_integer(value, 50, 950, &number)) {
			return false;
		}
		preferences->split_ratio = (uint16_t)number;
		*seen |= PREF_RATIO;
		return true;
	}
	if (strcmp(field, "split_orientation") == 0) {
		if ((*seen & PREF_AXIS) != 0 ||
				!parse_axis(value, &preferences->split_axis)) {
			return false;
		}
		*seen |= PREF_AXIS;
		return true;
	}
	return false;
}

static struct seyrix_app_rule *find_rule(struct seyrix_app_rule *rules,
		const char *app_id) {
	for (struct seyrix_app_rule *rule = rules; rule != NULL;
			rule = rule->next) {
		if (strcmp(rule->app_id, app_id) == 0) {
			return rule;
		}
	}
	return NULL;
}

bool seyrix_wm_config_get_app_class(const struct seyrix_wm_config *config,
		const char *app_id, enum seyrix_window_classification *classification) {
	if (config == NULL || app_id == NULL || classification == NULL) {
		return false;
	}
	for (const struct seyrix_app_rule *rule = config->app_rules;
			rule != NULL; rule = rule->next) {
		if (strcmp(rule->app_id, app_id) == 0) {
			*classification = rule->classification;
			return true;
		}
	}
	return false;
}

bool seyrix_wm_config_set_app_class(struct seyrix_wm_config *config,
		const char *app_id, enum seyrix_window_classification classification) {
	if (config == NULL || app_id == NULL || app_id[0] == '\0' ||
			strlen(app_id) > SEYRIX_WM_APP_ID_MAX ||
			(classification != SEYRIX_WINDOW_TILED &&
			classification != SEYRIX_WINDOW_FLOATING)) {
		return false;
	}
	struct seyrix_app_rule *rule = find_rule(config->app_rules, app_id);
	if (rule != NULL) {
		rule->classification = classification;
		return true;
	}
	size_t rule_count = 0;
	for (rule = config->app_rules; rule != NULL; rule = rule->next) {
		if (++rule_count >= SEYRIX_WM_APP_RULE_MAX) {
			return false;
		}
	}
	rule = calloc(1, sizeof(*rule));
	if (rule == NULL) {
		return false;
	}
	rule->app_id = strdup(app_id);
	if (rule->app_id == NULL) {
		free(rule);
		return false;
	}
	rule->classification = classification;
	rule->next = config->app_rules;
	config->app_rules = rule;
	return true;
}

static bool decode_app_id(const char *hex, char **app_id) {
	size_t length = strlen(hex);
	if (length == 0 || (length & 1U) != 0 || length > SEYRIX_WM_APP_ID_MAX * 2U) {
		return false;
	}
	char *decoded = calloc(length / 2 + 1, 1);
	if (decoded == NULL) {
		return false;
	}
	for (size_t i = 0; i < length; i += 2) {
		char pair[3] = {hex[i], hex[i + 1], '\0'};
		char *end = NULL;
		long value = strtol(pair, &end, 16);
		if (end == pair || *end != '\0' || value == 0 || value < 0 ||
				value == 0x7f || value < 0x20) {
			free(decoded);
			return false;
		}
		decoded[i / 2] = (char)value;
	}
	*app_id = decoded;
	return true;
}

static bool parse_app_rule(struct seyrix_wm_config *config, const char *key,
		const char *value) {
	static const char prefix[] = "app_id.";
	if (strncmp(key, prefix, sizeof(prefix) - 1) != 0) {
		return false;
	}
	char *app_id = NULL;
	if (!decode_app_id(key + sizeof(prefix) - 1, &app_id)) {
		return false;
	}
	enum seyrix_window_classification classification;
	bool valid = false;
	if (strcmp(value, "tiled") == 0) {
		classification = SEYRIX_WINDOW_TILED;
		valid = true;
	} else if (strcmp(value, "floating") == 0) {
		classification = SEYRIX_WINDOW_FLOATING;
		valid = true;
	}
	if (!valid || find_rule(config->app_rules, app_id) != NULL ||
			!seyrix_wm_config_set_app_class(config, app_id, classification)) {
		free(app_id);
		return false;
	}
	free(app_id);
	return true;
}

static bool parse_workspace_key(char *key, char **field,
		size_t *workspace_index) {
	static const char prefix[] = "workspace.";
	if (strncmp(key, prefix, sizeof(prefix) - 1) != 0) {
		return false;
	}
	char *number_text = key + sizeof(prefix) - 1;
	char *dot = strchr(number_text, '.');
	if (dot == NULL) {
		return false;
	}
	*dot = '\0';
	long number;
	if (!parse_integer(number_text, 1, SEYRIX_WM_WORKSPACE_COUNT, &number)) {
		return false;
	}
	*workspace_index = (size_t)number - 1;
	*field = dot + 1;
	return **field != '\0';
}

bool seyrix_wm_config_parse(FILE *stream, struct seyrix_wm_config *config,
		char *error, size_t error_size) {
	if (stream == NULL || config == NULL) {
		set_error(error, error_size, "invalid configuration input");
		return false;
	}
	if (error != NULL && error_size > 0) {
		error[0] = '\0';
	}
	struct seyrix_wm_config parsed = {0};
	seyrix_wm_config_defaults(&parsed);
	unsigned default_seen = 0;
	unsigned workspace_seen[SEYRIX_WM_WORKSPACE_COUNT] = {0};
	bool version_seen = false;
	char line[CONFIG_LINE_MAX];
	size_t line_number = 0;
	bool valid = true;
	while (fgets(line, sizeof(line), stream) != NULL) {
		line_number++;
		if (line_number > CONFIG_LINE_COUNT_MAX ||
				(strchr(line, '\n') == NULL && !feof(stream))) {
			valid = false;
			break;
		}
		char *contents = trim(line);
		if (*contents == '\0' || *contents == '#') {
			continue;
		}
		char *equals = strchr(contents, '=');
		if (equals == NULL) {
			valid = false;
			break;
		}
		*equals = '\0';
		char *key = trim(contents);
		char *value = trim(equals + 1);
		if (*key == '\0' || *value == '\0') {
			valid = false;
			break;
		}
		if (strcmp(key, "version") == 0) {
			if (version_seen || strcmp(value, "1") != 0) {
				valid = false;
				break;
			}
			version_seen = true;
			continue;
		}
		if (strncmp(key, "app_id.", sizeof("app_id.") - 1) == 0) {
			if (!parse_app_rule(&parsed, key, value)) {
				valid = false;
				break;
			}
			continue;
		}
		if (strncmp(key, "workspace.", sizeof("workspace.") - 1) == 0) {
			char *field = NULL;
			size_t index = 0;
			if (!parse_workspace_key(key, &field, &index) ||
					!set_preference(&parsed.workspaces[index], field, value,
						&workspace_seen[index])) {
				valid = false;
				break;
			}
			continue;
		}
		char field[32];
		if (strcmp(key, "default_mode") == 0) {
			snprintf(field, sizeof(field), "default_mode");
		} else {
			snprintf(field, sizeof(field), "%s", key);
		}
		if (!set_preference(&parsed.defaults, field, value, &default_seen)) {
			valid = false;
			break;
		}
	}
	if (ferror(stream)) {
		valid = false;
	}
	if (!valid) {
		char message[128];
		snprintf(message, sizeof(message),
			"invalid window-management configuration near line %zu", line_number);
		set_error(error, error_size, message);
		seyrix_wm_config_finish(&parsed);
		return false;
	}
	for (size_t i = 0; i < SEYRIX_WM_WORKSPACE_COUNT; i++) {
		if ((workspace_seen[i] & PREF_MODE) == 0) {
			parsed.workspaces[i].mode = parsed.defaults.mode;
		}
		if ((workspace_seen[i] & PREF_INNER_GAP) == 0) {
			parsed.workspaces[i].inner_gap = parsed.defaults.inner_gap;
		}
		if ((workspace_seen[i] & PREF_OUTER_GAP) == 0) {
			parsed.workspaces[i].outer_gap = parsed.defaults.outer_gap;
		}
		if ((workspace_seen[i] & PREF_RATIO) == 0) {
			parsed.workspaces[i].split_ratio = parsed.defaults.split_ratio;
		}
		if ((workspace_seen[i] & PREF_AXIS) == 0) {
			parsed.workspaces[i].split_axis = parsed.defaults.split_axis;
		}
	}
	seyrix_wm_config_finish(config);
	*config = parsed;
	return true;
}

static char *configuration_path(void) {
	const char *base = getenv("XDG_CONFIG_HOME");
	char fallback[PATH_MAX];
	if (base == NULL || base[0] != '/') {
		const char *home = getenv("HOME");
		if (home == NULL || home[0] != '/' ||
				snprintf(fallback, sizeof(fallback), "%s/.config", home) >=
				(int)sizeof(fallback)) {
			return NULL;
		}
		base = fallback;
	}
	char *path = NULL;
	if (asprintf(&path, "%s/seyrix/window-management.conf", base) < 0) {
		return NULL;
	}
	if (strlen(path) >= PATH_MAX) {
		free(path);
		return NULL;
	}
	return path;
}

bool seyrix_wm_config_load(struct seyrix_wm_config *config,
		char *error, size_t error_size) {
	if (config == NULL) {
		set_error(error, error_size, "invalid configuration destination");
		return false;
	}
	if (error != NULL && error_size > 0) {
		error[0] = '\0';
	}
	struct seyrix_wm_config loaded = {0};
	seyrix_wm_config_defaults(&loaded);
	loaded.path = configuration_path();
	if (loaded.path == NULL) {
		set_error(error, error_size, "cannot determine XDG configuration path");
		seyrix_wm_config_finish(config);
		seyrix_wm_config_defaults(config);
		return false;
	}
	int fd = open(loaded.path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0 && errno == ENOENT) {
		seyrix_wm_config_finish(config);
		*config = loaded;
		return true;
	}
	if (fd < 0) {
		set_error(error, error_size, "cannot read window-management configuration");
		seyrix_wm_config_finish(config);
		seyrix_wm_config_defaults(config);
		config->path = loaded.path;
		loaded.path = NULL;
		seyrix_wm_config_finish(&loaded);
		return false;
	}
	struct stat status;
	FILE *stream = NULL;
	if (fstat(fd, &status) == 0 && S_ISREG(status.st_mode)) {
		stream = fdopen(fd, "r");
	}
	if (stream == NULL) {
		close(fd);
		set_error(error, error_size, "window-management configuration is not a regular file");
		seyrix_wm_config_finish(config);
		seyrix_wm_config_defaults(config);
		config->path = loaded.path;
		loaded.path = NULL;
		seyrix_wm_config_finish(&loaded);
		return false;
	}
	struct seyrix_wm_config parsed = {0};
	seyrix_wm_config_defaults(&parsed);
	bool parsed_ok = seyrix_wm_config_parse(stream, &parsed, error, error_size);
	if (fclose(stream) != 0) {
		parsed_ok = false;
		set_error(error, error_size, "cannot close window-management configuration");
	}
	if (!parsed_ok) {
		seyrix_wm_config_finish(&parsed);
		seyrix_wm_config_finish(config);
		seyrix_wm_config_defaults(config);
		config->path = loaded.path;
		loaded.path = NULL;
		seyrix_wm_config_finish(&loaded);
		return false;
	}
	parsed.path = loaded.path;
	loaded.path = NULL;
	seyrix_wm_config_finish(config);
	*config = parsed;
	seyrix_wm_config_finish(&loaded);
	return true;
}

static bool make_parent_directories(const char *file_path) {
	char directory[PATH_MAX];
	snprintf(directory, sizeof(directory), "%s", file_path);
	char *last_slash = strrchr(directory, '/');
	if (last_slash == NULL || last_slash == directory) {
		return false;
	}
	*last_slash = '\0';
	for (char *cursor = directory + 1; ; cursor++) {
		if (*cursor != '/' && *cursor != '\0') {
			continue;
		}
		char saved = *cursor;
		*cursor = '\0';
		if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
			return false;
		}
		struct stat status;
		if (stat(directory, &status) != 0 || !S_ISDIR(status.st_mode)) {
			return false;
		}
		*cursor = saved;
		if (saved == '\0') {
			break;
		}
	}
	return true;
}

static bool write_configuration(FILE *stream,
		const struct seyrix_wm_config *config) {
	if (fprintf(stream,
			"version=1\n"
			"default_mode=%s\n"
			"inner_gap=%d\n"
			"outer_gap=%d\n"
			"split_ratio=%u\n"
			"split_orientation=%s\n",
			mode_name(config->defaults.mode), config->defaults.inner_gap,
			config->defaults.outer_gap, config->defaults.split_ratio,
			axis_name(config->defaults.split_axis)) < 0) {
		return false;
	}
	for (size_t i = 0; i < SEYRIX_WM_WORKSPACE_COUNT; i++) {
		const struct seyrix_layout_preferences *preferences =
			&config->workspaces[i];
		if (fprintf(stream,
				"workspace.%zu.mode=%s\n"
				"workspace.%zu.inner_gap=%d\n"
				"workspace.%zu.outer_gap=%d\n"
				"workspace.%zu.split_ratio=%u\n"
				"workspace.%zu.split_orientation=%s\n",
				i + 1, mode_name(preferences->mode),
				i + 1, preferences->inner_gap,
				i + 1, preferences->outer_gap,
				i + 1, preferences->split_ratio,
				i + 1, axis_name(preferences->split_axis)) < 0) {
			return false;
		}
	}
	static const char digits[] = "0123456789abcdef";
	for (const struct seyrix_app_rule *rule = config->app_rules;
			rule != NULL; rule = rule->next) {
		if (fputs("app_id.", stream) == EOF) {
			return false;
		}
		for (const unsigned char *byte = (const unsigned char *)rule->app_id;
				*byte != '\0'; byte++) {
			if (fputc(digits[*byte >> 4], stream) == EOF ||
					fputc(digits[*byte & 0x0f], stream) == EOF) {
				return false;
			}
		}
		const char *value = rule->classification == SEYRIX_WINDOW_FLOATING ?
			"floating" : "tiled";
		if (fprintf(stream, "=%s\n", value) < 0) {
			return false;
		}
	}
	return !ferror(stream);
}

bool seyrix_wm_config_save(const struct seyrix_wm_config *config,
		char *error, size_t error_size) {
	if (config == NULL || config->path == NULL ||
			strlen(config->path) >= PATH_MAX) {
		set_error(error, error_size, "window-management configuration path is unavailable");
		return false;
	}
	if (!make_parent_directories(config->path)) {
		set_error(error, error_size, "cannot create window-management configuration directory");
		return false;
	}
	char temporary_path[PATH_MAX];
	if (snprintf(temporary_path, sizeof(temporary_path), "%s.tmp.XXXXXX",
			config->path) >= (int)sizeof(temporary_path)) {
		set_error(error, error_size, "window-management configuration path is too long");
		return false;
	}
	int fd = mkstemp(temporary_path);
	if (fd < 0) {
		set_error(error, error_size, "cannot create temporary configuration file");
		return false;
	}
	(void)fcntl(fd, F_SETFD, FD_CLOEXEC);
	(void)fchmod(fd, 0600);
	FILE *stream = fdopen(fd, "w");
	if (stream == NULL) {
		close(fd);
		unlink(temporary_path);
		set_error(error, error_size, "cannot open temporary configuration stream");
		return false;
	}
	bool written = write_configuration(stream, config);
	if (written && fflush(stream) != 0) {
		written = false;
	}
	if (written && fsync(fileno(stream)) != 0) {
		written = false;
	}
	if (fclose(stream) != 0) {
		written = false;
	}
	if (!written || rename(temporary_path, config->path) != 0) {
		unlink(temporary_path);
		set_error(error, error_size, "cannot persist window-management configuration");
		return false;
	}
	char directory[PATH_MAX];
	snprintf(directory, sizeof(directory), "%s", config->path);
	char *last_slash = strrchr(directory, '/');
	if (last_slash != NULL) {
		*last_slash = '\0';
		int directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (directory_fd >= 0) {
			(void)fsync(directory_fd);
			close(directory_fd);
		}
	}
	if (error != NULL && error_size > 0) {
		error[0] = '\0';
	}
	return true;
}
