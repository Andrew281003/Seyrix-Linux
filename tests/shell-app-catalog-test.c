#define _POSIX_C_SOURCE 200809L

#include "../desktop/shell/src/app-catalog.h"

#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

static gchar *test_root;
static gchar *data_home;
static gchar *applications_dir;
static gchar *system_data_dir;
static gchar *helper_path;

static void write_file(const gchar *path, const gchar *contents, mode_t mode) {
	GError *error = NULL;
	g_assert_true(g_file_set_contents(path, contents, -1, &error));
	g_assert_no_error(error);
	g_assert_cmpint(g_chmod(path, mode), ==, 0);
}

static gchar *desktop_path(const gchar *id) {
	return g_build_filename(applications_dir, id, NULL);
}

static void create_desktop_entries(void) {
	GError *error = NULL;
	test_root = g_dir_make_tmp("seyrix-app-catalog-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(test_root);
	data_home = g_build_filename(test_root, "data", NULL);
	applications_dir = g_build_filename(data_home, "applications", NULL);
	system_data_dir = g_build_filename(test_root, "system-data", NULL);
	g_assert_cmpint(g_mkdir_with_parents(applications_dir, 0700), ==, 0);
	gchar *system_apps = g_build_filename(
		system_data_dir, "applications", NULL);
	g_assert_cmpint(g_mkdir_with_parents(system_apps, 0700), ==, 0);
	g_free(system_apps);
	g_assert_true(g_setenv("XDG_DATA_HOME", data_home, TRUE));
	g_assert_true(g_setenv("XDG_DATA_DIRS", system_data_dir, TRUE));
	g_assert_true(g_setenv("XDG_CURRENT_DESKTOP", "Seyrix", TRUE));

	helper_path = g_build_filename(test_root, "record arguments", NULL);
	write_file(helper_path,
		"#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$SEYRIX_TEST_CAPTURE_FILE\"\n",
		0700);

	gchar *browser_path = desktop_path("seyrix-test-browser.desktop");
	gchar *browser_entry = g_strdup_printf(
		"[Desktop Entry]\n"
		"Type=Application\n"
		"Name=Web Browser Test\n"
		"Exec=\"%s\" marker %%F\n"
		"TryExec=%s\n"
		"Path=%s\n"
		"Icon=web-browser\n"
		"Terminal=false\n",
		helper_path, helper_path, test_root);
	write_file(browser_path, browser_entry, 0600);
	g_free(browser_entry);
	g_free(browser_path);

	gchar *hidden_path = desktop_path("hidden-test-app.desktop");
	write_file(hidden_path,
		"[Desktop Entry]\nType=Application\nName=Hidden Test App\n"
		"Exec=/usr/bin/true\nNoDisplay=true\nTerminal=false\n", 0600);
	g_free(hidden_path);

	gchar *missing_try_path = desktop_path("missing-tryexec.desktop");
	gchar *missing_try_entry = g_strdup_printf(
		"[Desktop Entry]\nType=Application\nName=Missing TryExec Test\n"
		"Exec=/usr/bin/true\nTryExec=%s/not-installed\nTerminal=false\n",
		test_root);
	write_file(missing_try_path, missing_try_entry, 0600);
	g_free(missing_try_entry);
	g_free(missing_try_path);

	/* Application entries require an Exec key. */
	gchar *invalid_path = desktop_path("invalid-missing-exec.desktop");
	write_file(invalid_path,
		"[Desktop Entry]\nType=Application\nName=Invalid Missing Exec\n",
		0600);
	g_free(invalid_path);

	/* A valid Link entry is not a launchable Application entry. */
	gchar *link_path = desktop_path("link-not-an-app.desktop");
	write_file(link_path,
		"[Desktop Entry]\nType=Link\nName=Documentation Link\n"
		"URL=https://example.invalid/\n", 0600);
	g_free(link_path);
}

static const seyrix_app_entry *find_entry(
		const seyrix_app_catalog *catalog, const gchar *id) {
	for (gsize i = 0; i < seyrix_app_catalog_count(catalog); i++) {
		const seyrix_app_entry *entry = seyrix_app_catalog_get(catalog, i);
		if (g_strcmp0(seyrix_app_entry_id(entry), id) == 0) {
			return entry;
		}
	}
	return NULL;
}

static void test_discovery_and_validation(void) {
	seyrix_app_catalog *catalog = seyrix_app_catalog_new();
	g_assert_nonnull(catalog);
	g_assert_cmpuint(seyrix_app_catalog_count(catalog), >, 0);

	const seyrix_app_entry *browser = find_entry(
		catalog, "seyrix-test-browser.desktop");
	g_assert_nonnull(browser);
	g_assert_cmpstr(seyrix_app_entry_name(browser), ==, "Web Browser Test");
	g_assert_cmpstr(seyrix_app_entry_icon(browser), ==, "web-browser");
	g_assert_null(find_entry(catalog, "hidden-test-app.desktop"));
	g_assert_null(find_entry(catalog, "missing-tryexec.desktop"));
	g_assert_null(find_entry(catalog, "invalid-missing-exec.desktop"));
	g_assert_null(find_entry(catalog, "link-not-an-app.desktop"));
	g_assert_null(seyrix_app_catalog_get(catalog,
		seyrix_app_catalog_count(catalog)));

	seyrix_app_catalog_free(catalog);
}

static void test_case_insensitive_search(void) {
	seyrix_app_catalog *catalog = seyrix_app_catalog_new();
	GPtrArray *matches = seyrix_app_catalog_search(catalog, "bRoWsEr");
	g_assert_cmpuint(matches->len, ==, 1);
	const seyrix_app_entry *entry = g_ptr_array_index(matches, 0);
	g_assert_cmpstr(seyrix_app_entry_id(entry), ==,
		"seyrix-test-browser.desktop");
	g_ptr_array_unref(matches);

	matches = seyrix_app_catalog_search(catalog,
		"SEYRIX-TEST-BROWSER.DESKTOP");
	g_assert_cmpuint(matches->len, ==, 1);
	g_ptr_array_unref(matches);

	matches = seyrix_app_catalog_search(catalog, "no such application");
	g_assert_cmpuint(matches->len, ==, 0);
	g_ptr_array_unref(matches);

	matches = seyrix_app_catalog_search(catalog, NULL);
	g_assert_cmpuint(matches->len, ==, seyrix_app_catalog_count(catalog));
	g_ptr_array_unref(matches);
	seyrix_app_catalog_free(catalog);
}

static void test_desktop_exec_argument_safety(void) {
	seyrix_app_catalog *catalog = seyrix_app_catalog_new();
	const seyrix_app_entry *browser = find_entry(
		catalog, "seyrix-test-browser.desktop");
	g_assert_nonnull(browser);

	if (geteuid() == 0) {
		GError *error = NULL;
		g_assert_false(seyrix_app_entry_launch(browser, NULL, NULL, &error));
		g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
		g_clear_error(&error);
		seyrix_app_catalog_free(catalog);
		return;
	}

	gchar *capture_path = g_build_filename(test_root, "captured-arguments", NULL);
	gchar *input_path = g_build_filename(test_root,
		"file with spaces; touch INJECTION-MUST-NOT-RUN", NULL);
	GError *error = NULL;
	g_assert_true(g_file_set_contents(input_path, "fixture", -1, &error));
	g_assert_no_error(error);
	GFile *file = g_file_new_for_path(input_path);
	GList *files = g_list_append(NULL, file);
	GAppLaunchContext *context = g_app_launch_context_new();
	g_app_launch_context_setenv(context, "SEYRIX_TEST_CAPTURE_FILE", capture_path);

	gchar *expected = g_strdup_printf("marker\n%s\n", input_path);
	g_assert_true(seyrix_app_entry_launch(browser, files, context, &error));
	g_assert_no_error(error);
	gboolean captured = FALSE;
	gchar *contents = NULL;
	for (guint attempt = 0; attempt < 500; attempt++) {
		if (g_file_get_contents(capture_path, &contents, NULL, &error)) {
			if (g_strcmp0(contents, expected) == 0) {
				captured = TRUE;
				break;
			}
			g_clear_pointer(&contents, g_free);
		}
		g_clear_error(&error);
		g_usleep(10000);
	}
	g_assert_true(captured);
	g_assert_cmpstr(contents, ==, expected);
	gchar *injection_path = g_build_filename(test_root,
		"INJECTION-MUST-NOT-RUN", NULL);
	g_assert_false(g_file_test(injection_path, G_FILE_TEST_EXISTS));

	/* Removing the declared TryExec makes a later launch fail closed. */
	g_assert_cmpint(g_unlink(helper_path), ==, 0);
	g_assert_false(seyrix_app_entry_launch(browser, NULL, NULL, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error(&error);

	g_free(expected);
	g_free(injection_path);
	g_free(contents);
	g_free(capture_path);
	g_free(input_path);
	g_list_free_full(files, g_object_unref);
	g_object_unref(context);
	seyrix_app_catalog_free(catalog);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);
	create_desktop_entries();
	g_test_add_func("/shell/app-catalog/discovery-validation",
		test_discovery_and_validation);
	g_test_add_func("/shell/app-catalog/case-insensitive-search",
		test_case_insensitive_search);
	g_test_add_func("/shell/app-catalog/exec-argument-safety",
		test_desktop_exec_argument_safety);
	return g_test_run();
}
