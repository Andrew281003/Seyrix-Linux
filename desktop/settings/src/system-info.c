#define _POSIX_C_SOURCE 200809L

#include "system-info.h"

#include <stdbool.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <glib.h>

static void copy_sanitized(char *destination, size_t capacity,
		const char *source) {
	if (source == NULL || source[0] == '\0') {
		g_strlcpy(destination, "Unavailable", capacity);
		return;
	}
	gchar *valid = g_utf8_make_valid(source, -1);
	g_strlcpy(destination, valid, capacity);
	g_free(valid);
}

static void collect_cpu(struct seyrix_system_info *info) {
	FILE *file = fopen("/proc/cpuinfo", "r");
	if (file == NULL) {
		return;
	}
	char *line = NULL;
	size_t capacity = 0;
	while (getline(&line, &capacity, file) >= 0) {
		char *separator = strchr(line, ':');
		if (separator == NULL) {
			continue;
		}
		*separator++ = '\0';
		g_strstrip(line);
		if (strcmp(line, "model name") != 0 && strcmp(line, "Hardware") != 0 &&
				strcmp(line, "Processor") != 0) {
			continue;
		}
		g_strstrip(separator);
		if (separator[0] != '\0') {
			copy_sanitized(info->cpu, sizeof(info->cpu), separator);
			break;
		}
	}
	free(line);
	fclose(file);
}

static void collect_memory(struct seyrix_system_info *info) {
	FILE *file = fopen("/proc/meminfo", "r");
	if (file == NULL) {
		return;
	}
	char line[256];
	unsigned long long kib = 0;
	while (fgets(line, sizeof(line), file) != NULL) {
		if (sscanf(line, "MemTotal: %llu kB", &kib) == 1) {
			break;
		}
	}
	fclose(file);
	if (kib > 0) {
		double gib = (double)kib / (1024.0 * 1024.0);
		g_snprintf(info->memory, sizeof(info->memory), "%.1f GiB", gib);
	}
}

static bool card_name(const char *name) {
	if (strncmp(name, "card", 4) != 0 || name[4] == '\0') {
		return false;
	}
	for (const char *cursor = name + 4; *cursor != '\0'; cursor++) {
		if (*cursor < '0' || *cursor > '9') {
			return false;
		}
	}
	return true;
}

static void collect_graphics(struct seyrix_system_info *info) {
	DIR *directory = opendir("/sys/class/drm");
	if (directory == NULL) {
		return;
	}
	GString *summary = g_string_new(NULL);
	struct dirent *entry;
	while ((entry = readdir(directory)) != NULL && summary->len < 180) {
		if (!card_name(entry->d_name)) {
			continue;
		}
		char link_path[PATH_MAX];
		if (g_snprintf(link_path, sizeof(link_path),
				"/sys/class/drm/%s/device/driver", entry->d_name) >=
				(gint)sizeof(link_path)) {
			continue;
		}
		char resolved[PATH_MAX];
		ssize_t length = readlink(link_path, resolved, sizeof(resolved) - 1);
		const char *driver = "unknown driver";
		if (length >= 0) {
			resolved[length] = '\0';
			const char *slash = strrchr(resolved, '/');
			if (slash != NULL && slash[1] != '\0') {
				driver = slash + 1;
			}
		}
		if (summary->len > 0) {
			g_string_append(summary, ", ");
		}
		g_string_append_printf(summary, "%s (%s)", entry->d_name, driver);
	}
	closedir(directory);
	if (summary->len > 0) {
		copy_sanitized(info->graphics, sizeof(info->graphics), summary->str);
	}
	g_string_free(summary, TRUE);
}

static void collect_build(struct seyrix_system_info *info) {
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;
	if (!g_key_file_load_from_file(key_file,
			"/usr/share/seyrix/build-info", G_KEY_FILE_NONE, &error)) {
		g_clear_error(&error);
		g_key_file_unref(key_file);
		return;
	}
	char *revision = g_key_file_get_string(key_file, "build", "revision", NULL);
	char *branch = g_key_file_get_string(key_file, "build", "branch", NULL);
	char *timestamp = g_key_file_get_string(key_file, "build", "timestamp_utc", NULL);
	char *build = NULL;
	if (revision != NULL && branch != NULL && timestamp != NULL) {
		build = g_strdup_printf("%s · %s · %s", revision, branch, timestamp);
	} else if (revision != NULL) {
		build = g_strdup(revision);
	}
	copy_sanitized(info->build, sizeof(info->build), build);
	g_free(build);
	g_free(revision);
	g_free(branch);
	g_free(timestamp);
	g_key_file_unref(key_file);
}

void seyrix_system_info_collect(struct seyrix_system_info *info) {
	if (info == NULL) {
		return;
	}
	memset(info, 0, sizeof(*info));
	g_strlcpy(info->os_name, "Unavailable", sizeof(info->os_name));
	g_strlcpy(info->os_version, "Unavailable", sizeof(info->os_version));
	g_strlcpy(info->kernel, "Unavailable", sizeof(info->kernel));
	g_strlcpy(info->architecture, "Unavailable", sizeof(info->architecture));
	g_strlcpy(info->cpu, "Unavailable", sizeof(info->cpu));
	g_strlcpy(info->memory, "Unavailable", sizeof(info->memory));
	g_strlcpy(info->graphics, "Unavailable", sizeof(info->graphics));
	g_strlcpy(info->build, "Unavailable", sizeof(info->build));

	copy_sanitized(info->os_name, sizeof(info->os_name),
		g_get_os_info("PRETTY_NAME"));
	copy_sanitized(info->os_version, sizeof(info->os_version),
		g_get_os_info("VERSION_ID"));
	struct utsname system;
	if (uname(&system) == 0) {
		copy_sanitized(info->kernel, sizeof(info->kernel), system.release);
		copy_sanitized(info->architecture, sizeof(info->architecture),
			system.machine);
	}
	collect_cpu(info);
	collect_memory(info);
	collect_graphics(info);
	collect_build(info);
}
