#include "settings-window.h"

#include "system-info.h"

#include <errno.h>
#include <gdk/gdkkeysyms.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum category_id {
	CATEGORY_APPEARANCE,
	CATEGORY_DESKTOP,
	CATEGORY_WINDOWS,
	CATEGORY_DISPLAYS,
	CATEGORY_INPUT,
	CATEGORY_ABOUT,
	CATEGORY_COUNT,
};

enum value_id {
	VALUE_WALLPAPER,
	VALUE_BACKGROUND_COLOR,
	VALUE_SHELL_MODE,
	VALUE_SHELL_POSITION,
	VALUE_SHELL_ALIGNMENT,
	VALUE_ICON_SIZE,
	VALUE_PINNED_APPS,
	VALUE_AUTO_HIDE,
	VALUE_ACTIVE_WORKSPACE,
	VALUE_WORKSPACE_MODE,
	VALUE_DEFAULT_MODE,
	VALUE_WINDOW_GAPS,
	VALUE_SPLIT,
	VALUE_APP_RULES,
	VALUE_SWITCHER_SCOPE,
	VALUE_OUTPUTS,
	VALUE_DISPLAY_MODES,
	VALUE_DISPLAY_LAYOUT,
	VALUE_KEYBOARDS,
	VALUE_SHORTCUTS,
	VALUE_DEVICE_PREFS,
	VALUE_WORKSPACE_COUNT,
	VALUE_COUNT,
};

struct setting_descriptor {
	enum category_id category;
	const char *title;
	const char *description;
	const char *keywords;
};

struct seyrix_settings_window {
	GtkWidget *window;
	GtkWidget *sidebar;
	GtkWidget *sidebar_scroll;
	GtkWidget *category_selector;
	GtkWidget *paned;
	GtkWidget *stack;
	GtkWidget *pages[CATEGORY_COUNT + 1];
	GtkWidget *search;
	GtkWidget *status;
	GtkWidget *values[VALUE_COUNT];
	GtkWidget *controls[VALUE_COUNT];
	GtkWidget *about_values[8];
	enum category_id selected_category;
	struct seyrix_settings_client *client;
	GtkApplication *application;
	struct seyrix_settings_window_state state;
	seyrix_settings_window_state_changed state_changed;
	gpointer state_data;
	struct seyrix_system_info system_info;
	bool sidebar_detached;
	bool compact_window_maximized;
	bool compact_layout;
	bool category_sync_active;
	bool restoring_state;
#if defined(SEYRIX_SETTINGS_RENDER_DIAGNOSTICS)
	bool layout_state_traced;
#endif
	guint layout_source;
};

static const char *settings_category_name(guint category);

static gboolean on_window_close_request(GtkWindow *window, gpointer data) {
	struct seyrix_settings_window *settings = data;
	GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(window));
	seyrix_settings_window_get_state(settings, &settings->state);
	if (settings->state_changed != NULL) {
		settings->state_changed(settings->state_data);
	}
	if (display == NULL || !gdk_display_is_closed(display)) {
		g_object_set_data(G_OBJECT(settings->application), "seyrix-user-close",
			GINT_TO_POINTER(1));
		g_message("SEYRIX_SETTINGS event=user-close pid=%ld exit_status=0 category=%s width=%d height=%d paned_position=%d maximized=%d",
			(long)getpid(), settings_category_name(settings->state.category),
			settings->state.width, settings->state.height,
			settings->state.paned_position, settings->state.maximized);
		g_application_quit(G_APPLICATION(settings->application));
	} else {
		g_message("SEYRIX_SETTINGS event=display-lost pid=%ld exit_status=75",
			(long)getpid());
		g_object_set_data(G_OBJECT(settings->application),
			"seyrix-exit-status", GINT_TO_POINTER(75));
		g_application_quit(G_APPLICATION(settings->application));
	}
	return FALSE;
}

static const char *const category_names[CATEGORY_COUNT] = {
	"Appearance",
	"Desktop & Dock",
	"Window Management",
	"Displays",
	"Input",
	"About Seyrix",
};

static const char *const category_ids[CATEGORY_COUNT] = {
	"appearance", "desktop", "windows", "displays", "input", "about",
};

static const char *settings_category_name(guint category) {
	return category < CATEGORY_COUNT ? category_ids[category] : "appearance";
}


static const char *const category_selector_names[] = {
	"Appearance", "Desktop & Dock", "Window Management", "Displays",
	"Input", "About Seyrix", NULL,
};

#if defined(SEYRIX_SETTINGS_RENDER_DIAGNOSTICS)
static bool settings_render_trace_enabled(void) {
	const char *enabled = g_getenv("SEYRIX_SETTINGS_RENDER_TRACE");
	return enabled != NULL && strcmp(enabled, "1") == 0;
}

static void settings_trace_event(struct seyrix_settings_window *window,
		const char *event, const char *page, GtkWidget *widget) {
	if (!settings_render_trace_enabled()) {
		return;
	}
	const char *selected = NULL;
	GtkWidget *selected_child = NULL;
	if (GTK_IS_STACK(window->stack)) {
		selected = gtk_stack_get_visible_child_name(GTK_STACK(window->stack));
		selected_child = gtk_stack_get_visible_child(GTK_STACK(window->stack));
	}
	GtkWidget *selected_content = selected_child != NULL &&
		GTK_IS_SCROLLED_WINDOW(selected_child) ?
		gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(selected_child)) : NULL;
	GtkWidget *selected_title = selected_content;
	while (selected_title != NULL && !GTK_IS_LABEL(selected_title)) {
		selected_title = gtk_widget_get_first_child(selected_title);
	}
	const char *selected_title_text = GTK_IS_LABEL(selected_title) ?
		gtk_label_get_text(GTK_LABEL(selected_title)) : "none";
	GtkWidget *selected_description = selected_title != NULL ?
		gtk_widget_get_next_sibling(selected_title) : NULL;
	GtkWidget *selected_first_row = selected_description != NULL ?
		gtk_widget_get_next_sibling(selected_description) : NULL;
	GtkWidget *focus = window->window != NULL ?
		gtk_root_get_focus(GTK_ROOT(window->window)) : NULL;
	int focus_category = -1;
	for (GtkWidget *ancestor = focus; ancestor != NULL &&
			ancestor != window->sidebar; ancestor = gtk_widget_get_parent(ancestor)) {
		if (GTK_IS_LIST_BOX_ROW(ancestor)) {
			focus_category = GPOINTER_TO_INT(g_object_get_data(
				G_OBJECT(ancestor), "seyrix-category"));
			break;
		}
	}
	GtkListBoxRow *selected_row = window->sidebar != NULL ?
		gtk_list_box_get_selected_row(GTK_LIST_BOX(window->sidebar)) : NULL;
	int selected_category = selected_row != NULL ? GPOINTER_TO_INT(
		g_object_get_data(G_OBJECT(selected_row), "seyrix-category")) : -1;
	int target_category = -1;
	for (GtkWidget *ancestor = widget; ancestor != NULL &&
			ancestor != window->sidebar; ancestor = gtk_widget_get_parent(ancestor)) {
		if (GTK_IS_LIST_BOX_ROW(ancestor)) {
			target_category = GPOINTER_TO_INT(g_object_get_data(
				G_OBJECT(ancestor), "seyrix-category"));
			break;
		}
	}
	graphene_rect_t bounds;
	bool have_bounds = widget != NULL && window->window != NULL &&
		gtk_widget_compute_bounds(widget, window->window, &bounds);
	GskRenderer *renderer = window->window != NULL ?
		gtk_native_get_renderer(GTK_NATIVE(window->window)) : NULL;
	gint64 monotonic_us = g_get_monotonic_time();
	uint64_t monotonic_ns = monotonic_us > 0 ?
		(uint64_t)monotonic_us * 1000ULL : 0;
	char trace[2048];
	int length = snprintf(trace, sizeof(trace),
		"SEYRIX_SETTINGS_TRACE monotonic_ns=%llu pid=%ld event=%s page=%s selected=%s visible_child_title=%s requested_gsk_renderer=%s gsk_renderer=%s page_size=%dx%d content_size=%dx%d title_size=%dx%d description_size=%dx%d first_row_size=%dx%d visible_child_child_visible=%d visible=%d mapped=%d category_sync_active=%d focus_widget=%s focus_category=%s selected_category=%s target_widget=%s target_category=%s bounds_space=GtkWindow bounds_valid=%d bounds=%.1f,%.1f,%.1f,%.1f\n",
		(unsigned long long)monotonic_ns, (long)getpid(), event,
		page != NULL ? page : "none",
		selected != NULL ? selected : "none",
		selected_title_text,
		g_getenv("GSK_RENDERER") != NULL ? g_getenv("GSK_RENDERER") : "default",
		renderer != NULL ? G_OBJECT_TYPE_NAME(renderer) : "none",
		selected_child != NULL ? gtk_widget_get_width(selected_child) : 0,
		selected_child != NULL ? gtk_widget_get_height(selected_child) : 0,
		selected_content != NULL ? gtk_widget_get_width(selected_content) : 0,
		selected_content != NULL ? gtk_widget_get_height(selected_content) : 0,
		selected_title != NULL ? gtk_widget_get_width(selected_title) : 0,
		selected_title != NULL ? gtk_widget_get_height(selected_title) : 0,
		selected_description != NULL ? gtk_widget_get_width(selected_description) : 0,
		selected_description != NULL ? gtk_widget_get_height(selected_description) : 0,
		selected_first_row != NULL ? gtk_widget_get_width(selected_first_row) : 0,
		selected_first_row != NULL ? gtk_widget_get_height(selected_first_row) : 0,
		selected_child != NULL && gtk_widget_get_child_visible(selected_child),
		widget != NULL && gtk_widget_get_visible(widget),
		widget != NULL && gtk_widget_get_mapped(widget),
		window->category_sync_active,
		focus != NULL ? G_OBJECT_TYPE_NAME(focus) : "none",
		focus_category >= 0 && focus_category < CATEGORY_COUNT ?
			category_ids[focus_category] : "none",
		selected_category >= 0 && selected_category < CATEGORY_COUNT ?
			category_ids[selected_category] : "none",
		widget != NULL ? G_OBJECT_TYPE_NAME(widget) : "none",
		target_category >= 0 && target_category < CATEGORY_COUNT ?
			category_ids[target_category] : "none",
		have_bounds,
		have_bounds ? bounds.origin.x : 0.0f,
		have_bounds ? bounds.origin.y : 0.0f,
		have_bounds ? bounds.size.width : 0.0f,
		have_bounds ? bounds.size.height : 0.0f);
	if (length > 0 && (size_t)length < sizeof(trace)) {
		ssize_t written;
		do {
			written = write(STDERR_FILENO, trace, (size_t)length);
		} while (written < 0 && errno == EINTR);
	}
}

static gboolean on_trace_input_event(GtkEventControllerLegacy *controller,
		GdkEvent *event, gpointer data) {
	(void)controller;
	struct seyrix_settings_window *window = data;
	GdkEventType type = gdk_event_get_event_type(event);
	if (type != GDK_BUTTON_PRESS && type != GDK_BUTTON_RELEASE &&
			type != GDK_KEY_PRESS) {
		return FALSE;
	}
	GtkWidget *target = NULL;
	if (type == GDK_BUTTON_PRESS || type == GDK_BUTTON_RELEASE) {
		double x = 0.0, y = 0.0;
		gboolean positioned = gdk_event_get_position(event, &x, &y);
		if (positioned) {
			target = gtk_widget_pick(window->window, x, y, GTK_PICK_DEFAULT);
		}
		guint button = gdk_button_event_get_button(event);
		const char *input_type = type == GDK_BUTTON_PRESS ?
			"button-press" : "button-release";
		const char *trace_event = type == GDK_BUTTON_PRESS ?
			"gtk_input_button_press" : "gtk_input_button_release";
		fprintf(stderr,
			"SEYRIX_SETTINGS_RAW_INPUT pid=%ld type=%s space=GtkWindow button=%u positioned=%d x=%.1f y=%.1f target_widget=%s\n",
			(long)getpid(), input_type, button, positioned, positioned ? x : 0.0,
			positioned ? y : 0.0,
			target != NULL ? G_OBJECT_TYPE_NAME(target) : "none");
		fflush(stderr);
		settings_trace_event(window, trace_event,
			button == GDK_BUTTON_PRIMARY ? "button-1" : "button-other", target);
	} else {
		target = gtk_root_get_focus(GTK_ROOT(window->window));
		guint keyval = gdk_key_event_get_keyval(event);
		guint keycode = gdk_key_event_get_keycode(event);
		const char *key_name = gdk_keyval_name(keyval);
		fprintf(stderr,
			"SEYRIX_SETTINGS_RAW_INPUT pid=%ld type=key-press keyval=%u keycode=%u name=%s target_widget=%s\n",
			(long)getpid(), keyval, keycode,
			key_name != NULL ? key_name : "unknown",
			target != NULL ? G_OBJECT_TYPE_NAME(target) : "none");
		fflush(stderr);
		settings_trace_event(window, "gtk_input_key_press",
			gdk_keyval_name(keyval), target);
	}
	return FALSE;
}

static const char *settings_trace_page_name(
		struct seyrix_settings_window *window, GtkWidget *widget) {
	for (int i = 0; i <= CATEGORY_COUNT; i++) {
		if (window->pages[i] == widget) {
			return i == CATEGORY_COUNT ? "search" : category_ids[i];
		}
	}
	return "unknown";
}

static gboolean on_trace_page_bounds(gpointer data) {
	GtkWidget *widget = data;
	struct seyrix_settings_window *window = g_object_get_data(
		G_OBJECT(widget), "seyrix-settings-window");
	if (window == NULL || !gtk_widget_get_mapped(widget)) {
		return G_SOURCE_REMOVE;
	}
	if (gtk_widget_get_width(widget) <= 0 || gtk_widget_get_height(widget) <= 0) {
		return G_SOURCE_CONTINUE;
	}
	settings_trace_event(window, "gtk_page_bounds",
		settings_trace_page_name(window, widget), widget);
	return G_SOURCE_REMOVE;
}

static void on_trace_page_map(GtkWidget *widget, gpointer data) {
	struct seyrix_settings_window *window = data;
	const char *page = settings_trace_page_name(window, widget);
	settings_trace_event(window, "gtk_page_map", page, widget);
	g_object_set_data(G_OBJECT(widget), "seyrix-settings-window", window);
	g_timeout_add_full(G_PRIORITY_DEFAULT, 100, on_trace_page_bounds,
		g_object_ref(widget), g_object_unref);
}

static void on_trace_page_unmap(GtkWidget *widget, gpointer data) {
	struct seyrix_settings_window *window = data;
	const char *page = settings_trace_page_name(window, widget);
	settings_trace_event(window, "gtk_page_unmap", page, widget);
}

static gboolean on_trace_sidebar_row_bounds(gpointer data) {
	GtkWidget *row = data;
	if (!gtk_widget_get_mapped(row)) {
		return G_SOURCE_REMOVE;
	}
	if (gtk_widget_get_width(row) <= 20 || gtk_widget_get_height(row) <= 16) {
		return G_SOURCE_CONTINUE;
	}
	struct seyrix_settings_window *window = g_object_get_data(
		G_OBJECT(row), "seyrix-settings-window");
	int category = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row),
		"seyrix-category"));
	graphene_rect_t bounds;
	if (window != NULL && window->window != NULL && category >= 0 &&
			category < CATEGORY_COUNT &&
			gtk_widget_compute_bounds(row, window->window, &bounds)) {
		char *bounds_text = g_strdup_printf("%.1f,%.1f,%.1f,%.1f",
			bounds.origin.x, bounds.origin.y, bounds.size.width,
			bounds.size.height);
		const char *previous = g_object_get_data(G_OBJECT(row),
			"seyrix-last-traced-bounds");
		if (g_strcmp0(previous, bounds_text) != 0) {
			settings_trace_event(window, "gtk_sidebar_row_bounds",
				category_ids[category], row);
			g_object_set_data_full(G_OBJECT(row), "seyrix-last-traced-bounds",
				g_strdup(bounds_text), g_free);
		}
		g_free(bounds_text);
		return G_SOURCE_CONTINUE;
	}
	return G_SOURCE_CONTINUE;
}

static void on_trace_sidebar_row_map(GtkWidget *widget, gpointer data) {
	struct seyrix_settings_window *window = data;
	int category = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
		"seyrix-category"));
	if (category >= 0 && category < CATEGORY_COUNT) {
		settings_trace_event(window, "gtk_sidebar_row_map",
			category_ids[category], widget);
		g_object_set_data(G_OBJECT(widget), "seyrix-last-traced-bounds", NULL);
		g_object_set_data(G_OBJECT(widget), "seyrix-settings-window", window);
		g_timeout_add_full(G_PRIORITY_DEFAULT, 100, on_trace_sidebar_row_bounds,
			g_object_ref(widget), g_object_unref);
	}
}

static gboolean on_trace_category_selector_bounds(gpointer data) {
	struct seyrix_settings_window *window = data;
	GtkWidget *selector = window->category_selector;
	if (!window->compact_layout) {
		return G_SOURCE_REMOVE;
	}
	if (!gtk_widget_get_mapped(selector) || gtk_widget_get_width(selector) <= 0 ||
			gtk_widget_get_height(selector) <= 0) {
		return G_SOURCE_CONTINUE;
	}
	settings_trace_event(window, "gtk_category_selector_bounds",
		"category-selector", selector);
	return G_SOURCE_REMOVE;
}

static void on_trace_category_selector_map(GtkWidget *widget, gpointer data) {
	struct seyrix_settings_window *window = data;
	settings_trace_event(window, "gtk_category_selector_map",
		"category-selector", widget);
	g_timeout_add(100, on_trace_category_selector_bounds, window);
}

static void on_trace_category_selector_unmap(GtkWidget *widget, gpointer data) {
	settings_trace_event(data, "gtk_category_selector_unmap",
		"category-selector", widget);
}

static void on_trace_stack_selection(GObject *object, GParamSpec *property,
		gpointer data) {
	(void)property;
	GtkWidget *page = gtk_stack_get_visible_child(GTK_STACK(object));
	struct seyrix_settings_window *window = data;
	settings_trace_event(window, "gtk_page_selection",
		settings_trace_page_name(window, page), page);
}

static void on_trace_window_map(GtkWidget *widget, gpointer data) {
	settings_trace_event(data, "gtk_window_map", "window", widget);
}

static void on_trace_window_unmap(GtkWidget *widget, gpointer data) {
	settings_trace_event(data, "gtk_window_unmap", "window", widget);
}
#else
#define settings_trace_event(window, event, page, widget) ((void)0)
#endif

static const struct setting_descriptor setting_descriptors[] = {
	{CATEGORY_APPEARANCE, "Wallpaper", "Choose the image shown behind desktop windows.", "background image photo picture"},
	{CATEGORY_APPEARANCE, "Background presentation", "Choose how a wallpaper fits the screen.", "fill fit center stretch scale crop"},
	{CATEGORY_APPEARANCE, "Background color", "Choose the solid desktop background color.", "color appearance"},
	{CATEGORY_APPEARANCE, "Themes", "Theme selection is planned for a later phase.", "theme dark light style"},
	{CATEGORY_DESKTOP, "Dock or taskbar", "Choose the current desktop panel presentation.", "panel mode dock taskbar"},
	{CATEGORY_DESKTOP, "Dock position", "The current panel supports top or bottom placement.", "panel top bottom position"},
	{CATEGORY_DESKTOP, "Dock alignment", "Place dock icons at the start, center, or end.", "alignment"},
	{CATEGORY_DESKTOP, "Icon size", "Choose a supported panel icon size.", "size icons"},
	{CATEGORY_DESKTOP, "Pinned applications", "Choose applications from installed desktop entries.", "pin favorites launcher applications"},
	{CATEGORY_DESKTOP, "Workspace indicators", "Four fixed workspaces are shown by the shell.", "workspace desktop indicator"},
	{CATEGORY_DESKTOP, "Auto-hide", "Dock auto-hide is not implemented; the panel stays visible.", "hide autohide"},
	{CATEGORY_WINDOWS, "Window layout mode", "Floating, Automatic Tiling, or Hybrid mode.", "window floating tiling hybrid"},
	{CATEGORY_WINDOWS, "Window gaps", "Inner and outer gaps apply to Automatic Tiling.", "spacing inner outer"},
	{CATEGORY_WINDOWS, "Workspace layout", "Each workspace keeps its own layout preferences.", "workspace behavior mode"},
	{CATEGORY_WINDOWS, "Split orientation and ratio", "Show the configured orientation and split ratio.", "horizontal vertical automatic split"},
	{CATEGORY_WINDOWS, "Application rules", "Exact application IDs can select tiled or floating behavior.", "app id classification rules"},
	{CATEGORY_WINDOWS, "Switcher focus scope", "The current switcher can be limited to this workspace.", "focus switcher keyboard"},
	{CATEGORY_DISPLAYS, "Connected outputs", "Read active output name, resolution, scale, and arrangement.", "monitor screen display output"},
	{CATEGORY_DISPLAYS, "Resolution and refresh rate", "Mode changes are reserved for the display-safety milestone.", "mode refresh frequency"},
	{CATEGORY_DISPLAYS, "Scale and arrangement", "Output scale and position are read-only in this milestone.", "scale position layout"},
	{CATEGORY_INPUT, "Keyboard information", "Show the number of keyboards known to the compositor.", "keyboard device layout"},
	{CATEGORY_INPUT, "Pointer information", "Physical pointer inventory is not exposed yet.", "mouse touchpad pointer"},
	{CATEGORY_INPUT, "Supported shortcuts", "Show current launcher and window-switcher shortcuts.", "keybinding shortcut launcher switcher"},
	{CATEGORY_INPUT, "Device preferences", "Device-specific input configuration is not implemented.", "acceleration repeat remap"},
	{CATEGORY_ABOUT, "System information", "Read-only operating system, kernel, processor, memory, and graphics details.", "about version build hardware system"},
};

static const char *const shell_modes[] = {"Dock", "Taskbar", NULL};
static const char *const shell_positions[] = {"Top", "Bottom", NULL};
static const char *const shell_alignments[] = {"Start", "Center", "End", NULL};
static const char *const wm_modes[] = {"Floating", "Automatic Tiling", "Hybrid", NULL};
static const char *const split_orientations[] = {"Automatic", "Horizontal", "Vertical", NULL};

static const char css[] =
	"window { background: #121a22; color: #e8edf2; }"
	"headerbar { background: #1c2630; border-bottom: 1px solid #344452; }"
	".sidebar-list { background: #18222c; padding: 10px; }"
	".sidebar-list row { padding: 8px 10px; border-radius: 8px; color: #e8edf2; }"
	".sidebar-list row label { color: #e8edf2; }"
	".sidebar-list row:selected { background: #31546c; color: #ffffff; }"
	".sidebar-list row:selected label { color: #ffffff; }"
	".settings-page { padding: 16px; }"
	".settings-page-title { font-size: 22px; font-weight: 700; }"
	".settings-subtitle { color: #b7c3ce; margin-bottom: 8px; }"
	".settings-row { padding: 10px 6px; border-bottom: 1px solid #2b3945; }"
	".settings-description { color: #b7c3ce; }"
	".settings-current { color: #d3e2ee; font-weight: 600; }"
	".settings-status { padding: 8px 12px; border-radius: 8px; background: #263744; }"
	".settings-status-warning { background: #493e2b; color: #ffe0a0; }"
	".settings-result { padding: 10px; }"
	"button.settings-search-result { text-align: left; padding: 10px; }"
	"*:focus { outline: 2px solid #75c5f2; outline-offset: 2px; }";

static GtkWidget *make_dropdown(const char *const *items) {
	GtkWidget *dropdown = gtk_drop_down_new_from_strings(items);
	gtk_widget_set_size_request(dropdown, 130, -1);
	gtk_widget_set_sensitive(dropdown, FALSE);
	gtk_widget_add_css_class(dropdown, "settings-control");
	gtk_widget_set_tooltip_text(dropdown,
		"Read-only in Phase 2D.3B; no change request is sent.");
	return dropdown;
}

static GtkWidget *make_disabled_button(const char *label, const char *reason) {
	GtkWidget *button = gtk_button_new_with_label(label);
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_set_tooltip_text(button, reason);
	return button;
}

static GtkWidget *make_disabled_spin(double minimum, double maximum,
		double step, const char *reason) {
	GtkWidget *control = gtk_spin_button_new_with_range(minimum, maximum, step);
	gtk_widget_set_sensitive(control, FALSE);
	gtk_widget_set_tooltip_text(control, reason);
	return control;
}

static GtkWidget *make_page(struct seyrix_settings_window *window,
		enum category_id category, const char *subtitle) {
	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_add_css_class(content, "settings-page");
	GtkWidget *title = gtk_label_new(category_names[category]);
	gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
	gtk_widget_add_css_class(title, "settings-page-title");
	gtk_box_append(GTK_BOX(content), title);
	GtkWidget *description = gtk_label_new(subtitle);
	gtk_label_set_wrap(GTK_LABEL(description), TRUE);
	gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
	gtk_widget_add_css_class(description, "settings-subtitle");
	gtk_box_append(GTK_BOX(content), description);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);
	window->pages[category] = scroll;
	gtk_stack_add_named(GTK_STACK(window->stack), scroll,
		category_ids[category]);
	return content;
}

static GtkWidget *add_setting_row(struct seyrix_settings_window *window,
		GtkWidget *page, const char *title_text,
		const char *description_text, GtkWidget *control,
		enum value_id value) {
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_add_css_class(row, "settings-row");
	GtkWidget *copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	gtk_widget_set_hexpand(copy, TRUE);
	GtkWidget *title = gtk_label_new(title_text);
	gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
	gtk_widget_add_css_class(title, "heading");
	gtk_box_append(GTK_BOX(copy), title);
	GtkWidget *description = gtk_label_new(description_text);
	gtk_label_set_wrap(GTK_LABEL(description), TRUE);
	gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
	gtk_widget_add_css_class(description, "settings-description");
	gtk_box_append(GTK_BOX(copy), description);
	GtkWidget *current = gtk_label_new("Unavailable until connected");
	gtk_label_set_wrap(GTK_LABEL(current), TRUE);
	gtk_label_set_xalign(GTK_LABEL(current), 0.0f);
	gtk_widget_add_css_class(current, "settings-current");
	gtk_box_append(GTK_BOX(copy), current);
	gtk_box_append(GTK_BOX(row), copy);
	if (control != NULL) {
		gtk_accessible_update_property(GTK_ACCESSIBLE(control),
			GTK_ACCESSIBLE_PROPERTY_LABEL, title_text,
			GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, description_text, -1);
		gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
		gtk_widget_set_tooltip_text(control,
			"Changes are disabled in Phase 2D.3B.");
		gtk_widget_set_sensitive(control, FALSE);
		gtk_box_append(GTK_BOX(row), control);
	}
	gtk_box_append(GTK_BOX(page), row);
	window->values[value] = current;
	if (control != NULL) {
		window->controls[value] = control;
	}
	return current;
}

static GtkWidget *add_value_row(GtkWidget *page, const char *title,
		const char *description, const char *initial, GtkWidget **slot) {
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_add_css_class(row, "settings-row");
	GtkWidget *copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	gtk_widget_set_hexpand(copy, TRUE);
	GtkWidget *label = gtk_label_new(title);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
	gtk_box_append(GTK_BOX(copy), label);
	GtkWidget *detail = gtk_label_new(description);
	gtk_label_set_wrap(GTK_LABEL(detail), TRUE);
	gtk_label_set_xalign(GTK_LABEL(detail), 0.0f);
	gtk_widget_add_css_class(detail, "settings-description");
	gtk_box_append(GTK_BOX(copy), detail);
	gtk_box_append(GTK_BOX(row), copy);
	GtkWidget *value = gtk_label_new(initial);
	gtk_label_set_xalign(GTK_LABEL(value), 0.0f);
	gtk_label_set_wrap(GTK_LABEL(value), TRUE);
	gtk_widget_set_hexpand(value, TRUE);
	gtk_widget_add_css_class(value, "settings-current");
	gtk_accessible_update_property(GTK_ACCESSIBLE(value),
		GTK_ACCESSIBLE_PROPERTY_LABEL, title,
		GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, description, -1);
	gtk_box_append(GTK_BOX(row), value);
	gtk_box_append(GTK_BOX(page), row);
	if (slot != NULL) {
		*slot = value;
	}
	return value;
}

static void add_status_line(GtkWidget *page, const char *text) {
	GtkWidget *label = gtk_label_new(text);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
	gtk_widget_add_css_class(label, "settings-description");
	gtk_box_append(GTK_BOX(page), label);
}

static void build_appearance(struct seyrix_settings_window *window) {
	GtkWidget *page = make_page(window, CATEGORY_APPEARANCE,
		"Wallpaper and color are shown from the active shell configuration. Editing is not enabled in this foundation milestone.");
	add_setting_row(window, page, "Wallpaper",
		"The current image path is read-only. Image decoding and selection arrive in Phase 2D.3C.",
		make_disabled_button("Select image", "Wallpaper changes are not implemented yet."),
		VALUE_WALLPAPER);
	add_setting_row(window, page, "Background color",
		"Used when no wallpaper image is selected.",
		make_disabled_button("Choose color", "Color changes are not implemented yet."),
		VALUE_BACKGROUND_COLOR);
	add_value_row(page, "Background presentation",
		"Selectable image-fit modes are not exposed by the current shell.",
		"Not exposed in this milestone", NULL);
	add_status_line(page,
		"Theme selection is an extension point. No theme engine or extra compositor rendering effect is enabled here.");
}

static void build_desktop(struct seyrix_settings_window *window) {
	GtkWidget *page = make_page(window, CATEGORY_DESKTOP,
		"Current shell values are visible below. All controls are disabled and send no mutation requests.");
	add_setting_row(window, page, "Dock or taskbar",
		"Choose the shell panel presentation in a later phase.",
		make_dropdown(shell_modes), VALUE_SHELL_MODE);
	add_setting_row(window, page, "Panel position",
		"Current supported positions are top and bottom.",
		make_dropdown(shell_positions), VALUE_SHELL_POSITION);
	add_setting_row(window, page, "Dock alignment",
		"Current supported alignments are start, center, and end.",
		make_dropdown(shell_alignments), VALUE_SHELL_ALIGNMENT);
	GtkWidget *icon_size = make_disabled_spin(24, 64, 1,
		"Icon size changes are reserved for Phase 2D.3C.");
	add_setting_row(window, page, "Icon size",
		"The shell accepts icon sizes from 24 to 64 logical pixels.",
		icon_size, VALUE_ICON_SIZE);
	add_setting_row(window, page,
		"Pinned applications", "Pinned desktop entries are read-only in this milestone.",
		make_disabled_button("Manage pins", "Pin management is reserved for Phase 2D.3C."),
		VALUE_PINNED_APPS);
	window->values[VALUE_WORKSPACE_COUNT] = add_value_row(page,
		"Workspace indicators", "The compositor's fixed workspace count.",
		"Unavailable", NULL);
	add_setting_row(window, page, "Auto-hide",
		"Auto-hide is not implemented; the panel stays visible even if a legacy flag is present.",
		make_disabled_button("Not implemented", "The dock remains visible."),
		VALUE_AUTO_HIDE);
}

static void build_window_management(struct seyrix_settings_window *window) {
	GtkWidget *page = make_page(window, CATEGORY_WINDOWS,
		"The snapshot reports current compositor preferences. This application does not apply them.");
	add_setting_row(window, page,
		"Default window mode", "Default mode used when a workspace has no override.",
		make_dropdown(wm_modes), VALUE_DEFAULT_MODE);
	window->values[VALUE_ACTIVE_WORKSPACE] = add_value_row(page,
		"Active workspace", "The current workspace number.", "Unavailable", NULL);
	add_setting_row(window, page,
		"Active workspace mode", "The current workspace may override the default mode.",
		make_dropdown(wm_modes), VALUE_WORKSPACE_MODE);
	add_setting_row(window, page, "Window gaps",
		"Inner and outer gaps are bounded by the compositor configuration schema.",
		make_disabled_button("Edit gaps", "Window configuration changes are disabled."),
		VALUE_WINDOW_GAPS);
	add_setting_row(window, page,
		"Split orientation and ratio", "Current orientation and split ratio.",
		make_dropdown(split_orientations), VALUE_SPLIT);
	window->values[VALUE_APP_RULES] = add_value_row(page,
		"Application rules", "Exact app-id rules currently loaded by the compositor.",
		"Unavailable", NULL);
	window->values[VALUE_SWITCHER_SCOPE] = add_value_row(page,
		"Switcher scope", "Whether the switcher is limited to the active workspace.",
		"Unavailable", NULL);
}

static void build_displays(struct seyrix_settings_window *window) {
	GtkWidget *page = make_page(window, CATEGORY_DISPLAYS,
		"Output discovery is read-only in Phase 2D.3B. Display changes will use a compositor-owned confirmation and rollback transaction in Phase 2D.3E.");
	window->values[VALUE_OUTPUTS] = add_value_row(page, "Connected outputs",
		"Name, active resolution, scale, and position from the compositor snapshot.",
		"Unavailable", NULL);
	add_setting_row(window, page, "Resolution and refresh rate",
		"A mode may be offered only if the output advertises it and backend testing succeeds.",
		make_disabled_button("Mode changes unavailable", "Display mutation is not enabled."),
		VALUE_DISPLAY_MODES);
	add_setting_row(window, page, "Scale and arrangement",
		"Scale and output placement remain read-only until the rollback stage.",
		make_disabled_button("Arrangement unavailable", "Display mutation is not enabled."),
		VALUE_DISPLAY_LAYOUT);
}

static void build_input(struct seyrix_settings_window *window) {
	GtkWidget *page = make_page(window, CATEGORY_INPUT,
		"Input device configuration is not exposed in this milestone. Values shown here come from the compositor session.");
	window->values[VALUE_KEYBOARDS] = add_value_row(page, "Keyboards",
		"Number of keyboard devices known to the compositor.",
		"Unavailable", NULL);
	add_value_row(page, "Pointer information",
		"Physical pointer device inventory is not currently exposed. Pointer input remains active through the compositor.",
		"Inventory unavailable", NULL);
	window->values[VALUE_SHORTCUTS] = add_value_row(page,
		"Launcher and switcher shortcuts",
		"Only the current supported shortcut values are shown.",
		"Unavailable", NULL);
	add_setting_row(window, page, "Device preferences",
		"Acceleration, repeat rate, remapping, and arbitrary keybindings are not implemented.",
		make_disabled_button("Unavailable", "No device configuration request exists."),
		VALUE_DEVICE_PREFS);
}

static void build_about(struct seyrix_settings_window *window) {
	GtkWidget *page = make_page(window, CATEGORY_ABOUT,
		"System facts are read from ordinary user-readable OS and kernel interfaces. Missing information is shown as unavailable.");
	const char *titles[] = {
		"Operating system", "OS version", "Linux kernel", "Architecture",
		"CPU", "Memory", "Graphics", "Image build",
	};
	const char *descriptions[] = {
		"Distribution identity from os-release.",
		"Distribution version from os-release.",
		"Kernel release from uname.",
		"Machine architecture from uname.",
		"Processor model from /proc/cpuinfo.",
		"Installed memory from /proc/meminfo.",
		"Readable DRM device and driver identifiers.",
		"Optional image metadata from /usr/share/seyrix/build-info.",
	};
	const char *values[] = {
		window->system_info.os_name, window->system_info.os_version,
		window->system_info.kernel, window->system_info.architecture,
		window->system_info.cpu, window->system_info.memory,
		window->system_info.graphics, window->system_info.build,
	};
	for (size_t i = 0; i < G_N_ELEMENTS(titles); i++) {
		add_value_row(page, titles[i], descriptions[i], values[i],
			&window->about_values[i]);
	}
}

/* GTK caches each widget's render node separately from its page container. */
static void queue_page_widget_tree_draw(GtkWidget *widget) {
	gtk_widget_queue_draw(widget);
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
			child = gtk_widget_get_next_sibling(child)) {
		queue_page_widget_tree_draw(child);
	}
}

static gboolean queue_page_redraw_if_allocated(GtkWidget *page,
		GdkFrameClock *frame_clock, gpointer data) {
	(void)frame_clock;
	struct seyrix_settings_window *window = data;
	if (!gtk_widget_get_mapped(page) || gtk_widget_get_width(page) <= 0 ||
			gtk_widget_get_height(page) <= 0) {
		return G_SOURCE_CONTINUE;
	}
	if (window == NULL || !GTK_IS_STACK(window->stack) ||
			gtk_stack_get_visible_child(GTK_STACK(window->stack)) != page) {
		return G_SOURCE_REMOVE;
	}
	GtkWidget *content = GTK_IS_SCROLLED_WINDOW(page) ?
		gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(page)) : NULL;
	if (content != NULL && (gtk_widget_get_width(content) <= 0 ||
			gtk_widget_get_height(content) <= 0)) {
		return G_SOURCE_CONTINUE;
	}
	queue_page_widget_tree_draw(page);
	gtk_widget_queue_draw(window->stack);
	gtk_widget_queue_draw(window->window);
	settings_trace_event(window, "gtk_page_redraw_after_allocation",
		"visible", page);
	return G_SOURCE_REMOVE;
}

static void set_visible_page(struct seyrix_settings_window *window,
		size_t page_index) {
	if (window == NULL || page_index >= G_N_ELEMENTS(window->pages) ||
			window->pages[page_index] == NULL ||
			!GTK_IS_STACK(window->stack)) {
		return;
	}
	GtkWidget *page = window->pages[page_index];
	gtk_stack_set_visible_child(GTK_STACK(window->stack), page);
	gtk_widget_queue_resize(window->stack);
	if (queue_page_redraw_if_allocated(page, NULL, window) ==
			G_SOURCE_CONTINUE) {
		gtk_widget_add_tick_callback(page, queue_page_redraw_if_allocated,
			window, NULL);
	}
}

static void select_category(struct seyrix_settings_window *window,
		enum category_id category) {
	if (window == NULL || category >= CATEGORY_COUNT) {
		return;
	}
	window->selected_category = category;
	window->category_sync_active = true;
	if (gtk_drop_down_get_selected(GTK_DROP_DOWN(window->category_selector)) !=
			(guint)category) {
		gtk_drop_down_set_selected(GTK_DROP_DOWN(window->category_selector),
			(guint)category);
	}
	set_visible_page(window, category);
	GtkListBoxRow *row = gtk_list_box_get_row_at_index(
		GTK_LIST_BOX(window->sidebar), category);
	if (row != NULL && gtk_list_box_get_selected_row(
			GTK_LIST_BOX(window->sidebar)) != row) {
		gtk_list_box_select_row(GTK_LIST_BOX(window->sidebar), row);
	}
	window->category_sync_active = false;
	window->state.category = (guint)category;
	if (window->state_changed != NULL && !window->restoring_state) {
		window->state_changed(window->state_data);
	}
}

static void on_category_selector_changed(GObject *object, GParamSpec *property,
		gpointer data) {
	(void)property;
	struct seyrix_settings_window *window = data;
	if (window == NULL) {
		return;
	}
	guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
	if (window->category_sync_active) {
		if (selected < CATEGORY_COUNT) {
			settings_trace_event(window, "gtk_category_selector_suppressed",
				category_ids[selected], GTK_WIDGET(object));
		}
		return;
	}
	if (selected < CATEGORY_COUNT) {
		settings_trace_event(window, "gtk_category_selector_changed",
			category_ids[selected], GTK_WIDGET(object));
		select_category(window, (enum category_id)selected);
	}
}

static void update_compact_rows(GtkWidget *widget, bool compact) {
	if (GTK_IS_BOX(widget) &&
			gtk_widget_has_css_class(widget, "settings-row")) {
		gtk_orientable_set_orientation(GTK_ORIENTABLE(widget), compact ?
			GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
		GtkWidget *copy = gtk_widget_get_first_child(widget);
		GtkWidget *control = copy != NULL ? gtk_widget_get_next_sibling(copy) : NULL;
		if (control != NULL && !GTK_IS_BOX(control)) {
			gtk_widget_set_valign(control, compact ? GTK_ALIGN_START :
				GTK_ALIGN_CENTER);
		}
	}
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
			child = gtk_widget_get_next_sibling(child)) {
		update_compact_rows(child, compact);
	}
}

static void update_responsive_layout(struct seyrix_settings_window *window) {
	int width = gtk_widget_get_width(window->window);
	GdkDisplay *display = gtk_widget_get_display(window->window);
	GListModel *monitors = gdk_display_get_monitors(display);
	if (monitors != NULL && g_list_model_get_n_items(monitors) > 0) {
		GdkMonitor *monitor = g_list_model_get_item(monitors, 0);
		if (monitor != NULL) {
			GdkRectangle geometry;
			gdk_monitor_get_geometry(monitor, &geometry);
			if (geometry.width > 0 && (width <= 0 || geometry.width < width)) {
				width = geometry.width;
			}
			g_object_unref(monitor);
		}
	}
	if (width <= 0) {
		return;
	}
	bool compact = width > 0 && width < 760;
	bool layout_changed = window->compact_layout != compact;
	if (layout_changed) {
		window->compact_layout = compact;
		update_compact_rows(window->stack, compact);
	}
	if (compact && !window->compact_window_maximized) {
		window->compact_window_maximized = true;
		gtk_window_maximize(GTK_WINDOW(window->window));
	}
	if (gtk_widget_get_visible(window->category_selector) != compact) {
		gtk_widget_set_visible(window->category_selector, compact);
	}
	GtkWidget *sidebar_child = gtk_paned_get_start_child(
		GTK_PANED(window->paned));
	if (compact && sidebar_child != NULL) {
		g_object_ref(window->sidebar_scroll);
		gtk_paned_set_start_child(GTK_PANED(window->paned), NULL);
		window->sidebar_detached = true;
	} else if (!compact && sidebar_child == NULL && window->sidebar_detached) {
		gtk_paned_set_start_child(GTK_PANED(window->paned),
			window->sidebar_scroll);
		g_object_unref(window->sidebar_scroll);
		window->sidebar_detached = false;
	}
	gtk_widget_set_visible(window->sidebar_scroll, !compact);
#if defined(SEYRIX_SETTINGS_RENDER_DIAGNOSTICS)
	if (settings_render_trace_enabled() &&
			(layout_changed || !window->layout_state_traced)) {
		window->layout_state_traced = true;
		settings_trace_event(window, "gtk_layout_state",
			compact ? "compact" : "wide", window->window);
		settings_trace_event(window, "gtk_category_selector_state",
			compact ? "compact" : "wide", window->category_selector);
	}
#endif
}

static gboolean on_window_layout_check(gpointer data) {
	update_responsive_layout(data);
	return G_SOURCE_CONTINUE;
}

static void on_window_width_changed(GObject *object, GParamSpec *property,
		gpointer data) {
	(void)object;
	(void)property;
	struct seyrix_settings_window *window = data;
	update_responsive_layout(window);
	if (window->state_changed != NULL) {
		window->state_changed(window->state_data);
	}
}

static void on_window_size_changed(GObject *object, GParamSpec *property,
		gpointer data) {
	(void)object;
	(void)property;
	struct seyrix_settings_window *window = data;
	if (window->state_changed != NULL) {
		window->state_changed(window->state_data);
	}
}

static void on_paned_position_changed(GObject *object, GParamSpec *property,
		gpointer data) {
	(void)object;
	(void)property;
	struct seyrix_settings_window *window = data;
	if (window->state_changed != NULL) {
		window->state_changed(window->state_data);
	}
}

static void on_sidebar_row_selected(GtkListBox *list, GtkListBoxRow *row,
		gpointer data) {
	(void)list;
	if (row == NULL) {
		return;
	}
	struct seyrix_settings_window *window = data;
	if (window == NULL) {
		return;
	}
	int category = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row),
		"seyrix-category"));
	if (category < 0 || category >= CATEGORY_COUNT) {
		return;
	}
	if (window->category_sync_active) {
		settings_trace_event(window, "gtk_sidebar_row_selected_suppressed",
			category_ids[category], GTK_WIDGET(row));
		return;
	}
	settings_trace_event(window, "gtk_sidebar_row_selected",
		category_ids[category], GTK_WIDGET(row));
	select_category(window, (enum category_id)category);
	gtk_editable_set_text(GTK_EDITABLE(window->search), "");
}

static void on_sidebar_row_activated(GtkListBox *list, GtkListBoxRow *row,
		gpointer data) {
	struct seyrix_settings_window *window = data;
	int category = row != NULL ? GPOINTER_TO_INT(g_object_get_data(
		G_OBJECT(row), "seyrix-category")) : -1;
	if (category >= 0 && category < CATEGORY_COUNT) {
		settings_trace_event(window, "gtk_sidebar_row_activated",
			category_ids[category], GTK_WIDGET(row));
		if (window != NULL && window->selected_category == category) {
			gtk_editable_set_text(GTK_EDITABLE(window->search), "");
			return;
		}
	}
	on_sidebar_row_selected(list, row, data);
}

static void on_sidebar_row_click_released(GtkGestureClick *gesture,
		int n_press, double x, double y, gpointer data) {
	if (n_press != 1) {
		return;
	}
	struct seyrix_settings_window *window = data;
	GtkWidget *row = gtk_event_controller_get_widget(
		GTK_EVENT_CONTROLLER(gesture));
	if (window == NULL || row == NULL || !GTK_IS_LIST_BOX_ROW(row) ||
		x < 0 || y < 0 ||
		x >= gtk_widget_get_width(row) || y >= gtk_widget_get_height(row)) {
		return;
	}
	int category = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row),
		"seyrix-category"));
	if (category < 0 || category >= CATEGORY_COUNT) {
		return;
	}
	settings_trace_event(window, "gtk_sidebar_pointer_release",
		category_ids[category], row);
	if (window->selected_category != category) {
		select_category(window, (enum category_id)category);
	}
	gtk_editable_set_text(GTK_EDITABLE(window->search), "");
}

static void on_search_result_clicked(GtkButton *button, gpointer data) {
	struct seyrix_settings_window *window = data;
	int category = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
		"seyrix-category"));
	if (category >= 0 && category < CATEGORY_COUNT) {
		select_category(window, (enum category_id)category);
		gtk_editable_set_text(GTK_EDITABLE(window->search), "");
	}
}

static void clear_search_results(GtkWidget *list) {
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(list)) != NULL) {
		gtk_list_box_remove(GTK_LIST_BOX(list), child);
	}
}

static void on_search_changed(GtkEditable *editable, gpointer data) {
	struct seyrix_settings_window *window = data;
	const char *query = gtk_editable_get_text(editable);
	gchar *folded_query = g_utf8_casefold(query, -1);
	if (folded_query == NULL || folded_query[0] == '\0') {
		g_free(folded_query);
		set_visible_page(window, window->selected_category);
		return;
	}
	GtkWidget *page = window->pages[CATEGORY_COUNT];
	GtkWidget *list = g_object_get_data(G_OBJECT(page), "results-list");
	clear_search_results(list);
	unsigned int results = 0;
	for (size_t i = 0; i < G_N_ELEMENTS(setting_descriptors); i++) {
		const struct setting_descriptor *descriptor = &setting_descriptors[i];
		gchar *source = g_strdup_printf("%s %s %s %s",
			descriptor->title, descriptor->description, descriptor->keywords,
			category_names[descriptor->category]);
		gchar *haystack = g_utf8_casefold(source, -1);
		g_free(source);
		bool matches = haystack != NULL && g_strstr_len(haystack, -1,
			folded_query) != NULL;
		g_free(haystack);
		if (!matches) {
			continue;
		}
		GtkWidget *button = gtk_button_new();
		gtk_widget_add_css_class(button, "settings-search-result");
		GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
		GtkWidget *label = gtk_label_new(descriptor->title);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
		GtkWidget *detail = gtk_label_new(NULL);
		gchar *detail_text = g_strdup_printf("%s · %s",
			category_names[descriptor->category], descriptor->description);
		gtk_label_set_text(GTK_LABEL(detail), detail_text);
		gtk_label_set_wrap(GTK_LABEL(detail), TRUE);
		gtk_label_set_xalign(GTK_LABEL(detail), 0.0f);
		gtk_widget_add_css_class(detail, "settings-description");
		gtk_box_append(GTK_BOX(content), label);
		gtk_box_append(GTK_BOX(content), detail);
		gtk_button_set_child(GTK_BUTTON(button), content);
		g_object_set_data(G_OBJECT(button), "seyrix-category",
			GINT_TO_POINTER(descriptor->category));
		g_signal_connect(button, "clicked",
			G_CALLBACK(on_search_result_clicked), window);
		gtk_list_box_append(GTK_LIST_BOX(list), button);
		g_free(detail_text);
		results++;
	}
	if (results == 0) {
		GtkWidget *empty = gtk_label_new("No matching settings descriptions.");
		gtk_widget_add_css_class(empty, "settings-description");
		gtk_list_box_append(GTK_LIST_BOX(list), empty);
	}
	set_visible_page(window, CATEGORY_COUNT);
	g_free(folded_query);
}

static gboolean on_window_key(GtkEventControllerKey *controller,
		guint keyval, guint keycode, GdkModifierType state, gpointer data) {
	(void)controller;
	(void)keycode;
	struct seyrix_settings_window *window = data;
	if (keyval == GDK_KEY_f && (state & GDK_CONTROL_MASK) != 0) {
		gtk_widget_grab_focus(window->search);
		return TRUE;
	}
	if (keyval == GDK_KEY_Escape &&
			gtk_editable_get_text(GTK_EDITABLE(window->search))[0] != '\0') {
		gtk_editable_set_text(GTK_EDITABLE(window->search), "");
		return TRUE;
	}
	return FALSE;
}

static void add_sidebar_rows(struct seyrix_settings_window *window) {
	for (int i = 0; i < CATEGORY_COUNT; i++) {
		GtkWidget *label = gtk_label_new(category_names[i]);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
		GtkWidget *row = gtk_list_box_row_new();
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
		g_object_set_data(G_OBJECT(row), "seyrix-category", GINT_TO_POINTER(i));
		GtkGesture *click = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),
			GDK_BUTTON_PRIMARY);
		gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
			GTK_PHASE_CAPTURE);
		g_signal_connect(click, "released",
			G_CALLBACK(on_sidebar_row_click_released), window);
		gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(click));
#if defined(SEYRIX_SETTINGS_RENDER_DIAGNOSTICS)
		if (settings_render_trace_enabled()) {
			g_signal_connect(row, "map",
				G_CALLBACK(on_trace_sidebar_row_map), window);
		}
#endif
		gtk_list_box_append(GTK_LIST_BOX(window->sidebar), row);
	}
}

static void build_search_page(struct seyrix_settings_window *window) {
	GtkWidget *page = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_add_css_class(box, "settings-page");
	GtkWidget *title = gtk_label_new("Search settings");
	gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
	gtk_widget_add_css_class(title, "settings-page-title");
	gtk_box_append(GTK_BOX(box), title);
	GtkWidget *list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
	gtk_widget_set_hexpand(list, TRUE);
	gtk_box_append(GTK_BOX(box), list);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page), box);
	g_object_set_data(G_OBJECT(page), "results-list", list);
	window->pages[CATEGORY_COUNT] = page;
	gtk_stack_add_named(GTK_STACK(window->stack), page, "search");
}

static void build_pages(struct seyrix_settings_window *window) {
	build_appearance(window);
	build_desktop(window);
	build_window_management(window);
	build_displays(window);
	build_input(window);
	build_about(window);
	build_search_page(window);
}

static const char *mode_name(uint8_t mode) {
	static const char *const names[] = {"Floating", "Automatic Tiling", "Hybrid"};
	return mode < G_N_ELEMENTS(names) ? names[mode] : "Unavailable";
}

static const char *orientation_name(uint8_t orientation) {
	static const char *const names[] = {"Automatic", "Horizontal", "Vertical"};
	return orientation < G_N_ELEMENTS(names) ? names[orientation] : "Unavailable";
}

static void set_value(struct seyrix_settings_window *window,
		enum value_id value, const char *text) {
	if (window->values[value] != NULL) {
		gtk_label_set_text(GTK_LABEL(window->values[value]), text);
	}
}

static void update_dropdown(GtkWidget *widget, uint8_t selected) {
	if (widget != NULL && GTK_IS_DROP_DOWN(widget)) {
		gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), selected);
	}
}

static void update_spin(GtkWidget *widget, double value) {
	if (widget != NULL && GTK_IS_SPIN_BUTTON(widget)) {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), value);
	}
}

void seyrix_settings_window_refresh(struct seyrix_settings_window *window) {
	if (window == NULL || window->client == NULL) {
		return;
	}
	gtk_label_set_text(GTK_LABEL(window->status), window->client->status);
	gtk_widget_remove_css_class(window->status, "settings-status-warning");
	if (window->client->state != SEYRIX_SETTINGS_CLIENT_CONNECTED) {
		gtk_widget_add_css_class(window->status, "settings-status-warning");
	}
	if (!window->client->has_snapshot) {
		for (enum value_id id = 0; id < VALUE_COUNT; id++) {
			set_value(window, id, "Unavailable until the compositor connects");
			if (window->controls[id] != NULL) {
				gtk_widget_set_visible(window->controls[id], FALSE);
			}
		}
		return;
	}
	for (enum value_id id = 0; id < VALUE_COUNT; id++) {
		if (window->controls[id] != NULL) {
			gtk_widget_set_visible(window->controls[id], TRUE);
		}
	}
	const struct seyrix_settings_snapshot *snapshot = &window->client->snapshot;
	gchar *wallpaper = snapshot->wallpaper[0] != '\0' ?
		g_utf8_make_valid(snapshot->wallpaper, -1) : g_strdup("Solid background");
	set_value(window, VALUE_WALLPAPER, wallpaper);
	g_free(wallpaper);
	set_value(window, VALUE_BACKGROUND_COLOR, snapshot->background_color);
	static const char *const mode_names[] = {"Dock", "Taskbar"};
	static const char *const position_names[] = {"Top", "Bottom"};
	static const char *const alignment_names[] = {"Start", "Center", "End"};
	set_value(window, VALUE_SHELL_MODE,
		snapshot->shell_mode < G_N_ELEMENTS(mode_names) ?
		mode_names[snapshot->shell_mode] : "Unavailable");
	set_value(window, VALUE_SHELL_POSITION,
		snapshot->shell_position < G_N_ELEMENTS(position_names) ?
		position_names[snapshot->shell_position] : "Unavailable");
	set_value(window, VALUE_SHELL_ALIGNMENT,
		snapshot->shell_alignment < G_N_ELEMENTS(alignment_names) ?
		alignment_names[snapshot->shell_alignment] : "Unavailable");
	gchar *text = g_strdup_printf("%u logical pixels", snapshot->shell_icon_size);
	set_value(window, VALUE_ICON_SIZE, text);
	g_free(text);
	text = g_strdup_printf("%zu pinned applications", snapshot->pinned_count);
	set_value(window, VALUE_PINNED_APPS, text);
	g_free(text);
	set_value(window, VALUE_AUTO_HIDE, snapshot->shell_auto_hide ?
		"Legacy flag is set; auto-hide is not implemented" :
		"Off; auto-hide is not implemented");
	set_value(window, VALUE_DISPLAY_MODES,
		"Mode changes unavailable in Phase 2D.3B");
	set_value(window, VALUE_DISPLAY_LAYOUT,
		"Read-only in Phase 2D.3B");
	set_value(window, VALUE_DEVICE_PREFS,
		"Device changes are not implemented");
	text = g_strdup_printf("%u fixed workspaces", snapshot->workspace_count);
	set_value(window, VALUE_WORKSPACE_COUNT, text);
	g_free(text);
	text = g_strdup_printf("%u of %u", snapshot->active_workspace,
		snapshot->workspace_count);
	set_value(window, VALUE_ACTIVE_WORKSPACE, text);
	g_free(text);
	const struct seyrix_settings_workspace *defaults = &snapshot->workspaces[0];
	set_value(window, VALUE_DEFAULT_MODE, mode_name(defaults->mode));
	const struct seyrix_settings_workspace *active =
		&snapshot->workspaces[snapshot->active_workspace];
	set_value(window, VALUE_WORKSPACE_MODE, mode_name(active->mode));
	text = g_strdup_printf("Inner %d · Outer %d logical pixels",
		active->inner_gap, active->outer_gap);
	set_value(window, VALUE_WINDOW_GAPS, text);
	g_free(text);
	text = g_strdup_printf("%s · ratio %u/1000",
		orientation_name(active->orientation), active->split_ratio);
	set_value(window, VALUE_SPLIT, text);
	g_free(text);
	text = g_strdup_printf("%u exact app-id rules", snapshot->app_rule_count);
	set_value(window, VALUE_APP_RULES, text);
	g_free(text);
	set_value(window, VALUE_SWITCHER_SCOPE,
		snapshot->switcher_active_workspace_only ?
		"Active workspace only" : "All workspaces");
	GString *outputs = g_string_new(NULL);
	if (snapshot->output_count == 0) {
		g_string_append(outputs, "No output state reported");
	}
	for (size_t i = 0; i < snapshot->output_count; i++) {
		const struct seyrix_settings_output *output = &snapshot->outputs[i];
		if (i > 0) g_string_append(outputs, "\n");
		g_string_append_printf(outputs, "%s · %ux%u · %.2fx · %d,%d%s",
			output->name, output->width, output->height,
			(double)output->scale_milli / 1000.0, output->x, output->y,
			output->enabled ? "" : " · disabled");
	}
	set_value(window, VALUE_OUTPUTS, outputs->str);
	g_string_free(outputs, TRUE);
	text = g_strdup_printf("%u active", snapshot->keyboard_count);
	set_value(window, VALUE_KEYBOARDS, text);
	g_free(text);
	text = g_strdup_printf("Launcher: %s\nSwitcher: %s",
		snapshot->launcher_shortcut, snapshot->switcher_shortcut);
	set_value(window, VALUE_SHORTCUTS, text);
	g_free(text);

	update_dropdown(window->controls[VALUE_SHELL_MODE], snapshot->shell_mode);
	update_dropdown(window->controls[VALUE_SHELL_POSITION],
		snapshot->shell_position);
	update_dropdown(window->controls[VALUE_SHELL_ALIGNMENT],
		snapshot->shell_alignment);
	update_spin(window->controls[VALUE_ICON_SIZE], snapshot->shell_icon_size);
	update_dropdown(window->controls[VALUE_DEFAULT_MODE], defaults->mode);
	update_dropdown(window->controls[VALUE_WORKSPACE_MODE], active->mode);
	update_dropdown(window->controls[VALUE_SPLIT], active->orientation);
}

struct seyrix_settings_window *seyrix_settings_window_create(
		GtkApplication *application, struct seyrix_settings_client *client,
		const struct seyrix_settings_window_state *state,
		seyrix_settings_window_state_changed state_changed, gpointer state_data) {
	struct seyrix_settings_window *window = g_new0(
		struct seyrix_settings_window, 1);
	window->client = client;
	window->application = application;
	window->state = state != NULL ? *state :
		(struct seyrix_settings_window_state){
			.category = CATEGORY_APPEARANCE,
			.width = 720,
			.height = 560,
			.paned_position = 160,
		};
	if (window->state.category >= CATEGORY_COUNT) {
		window->state.category = CATEGORY_APPEARANCE;
	}
	if (window->state.width < 400 || window->state.width > 8192) {
		window->state.width = 720;
	}
	if (window->state.height < 300 || window->state.height > 8192) {
		window->state.height = 560;
	}
	if (window->state.paned_position < 100 ||
			window->state.paned_position > 2048) {
		window->state.paned_position = 160;
	}
	window->selected_category = (enum category_id)window->state.category;
	window->state_changed = state_changed;
	window->state_data = state_data;
	window->restoring_state = true;
	seyrix_system_info_collect(&window->system_info);
	window->window = gtk_application_window_new(application);
	g_signal_connect(window->window, "close-request",
		G_CALLBACK(on_window_close_request), window);
	gtk_window_set_title(GTK_WINDOW(window->window), "System Settings");
	gtk_window_set_default_size(GTK_WINDOW(window->window),
		window->state.width, window->state.height);
	gtk_window_set_resizable(GTK_WINDOW(window->window), TRUE);
#if defined(SEYRIX_SETTINGS_RENDER_DIAGNOSTICS)
	if (settings_render_trace_enabled()) {
		g_signal_connect(window->window, "map",
			G_CALLBACK(on_trace_window_map), window);
		g_signal_connect(window->window, "unmap",
			G_CALLBACK(on_trace_window_unmap), window);
	}
#endif

	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(provider, css);
	GdkDisplay *display = gtk_widget_get_display(window->window);
	gtk_style_context_add_provider_for_display(display,
		GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);

	GtkWidget *header = gtk_header_bar_new();
	GtkWidget *title = gtk_label_new("System Settings");
	gtk_widget_add_css_class(title, "title-3");
	gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);
	gtk_window_set_titlebar(GTK_WINDOW(window->window), header);

	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(root, 10);
	gtk_widget_set_margin_end(root, 10);
	gtk_widget_set_margin_top(root, 8);
	gtk_widget_set_margin_bottom(root, 10);
	gtk_window_set_child(GTK_WINDOW(window->window), root);
	window->status = gtk_label_new("Connecting to Seyrix compositor…");
	gtk_label_set_xalign(GTK_LABEL(window->status), 0.0f);
	gtk_label_set_wrap(GTK_LABEL(window->status), TRUE);
	gtk_widget_add_css_class(window->status, "settings-status");
	gtk_box_append(GTK_BOX(root), window->status);

	window->search = gtk_search_entry_new();
	gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(window->search),
		"Search settings descriptions…");
	gtk_widget_set_tooltip_text(window->search,
		"Search all setting names and descriptions. Press Control+F to focus.");
	gtk_widget_set_hexpand(window->search, TRUE);
	gtk_box_append(GTK_BOX(root), window->search);
	window->category_selector = gtk_drop_down_new_from_strings(
		category_selector_names);
	gtk_accessible_update_property(GTK_ACCESSIBLE(window->category_selector),
		GTK_ACCESSIBLE_PROPERTY_LABEL, "Settings category", -1);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(window->category_selector), 0);
	gtk_widget_set_visible(window->category_selector, TRUE);
	gtk_box_append(GTK_BOX(root), window->category_selector);

	window->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_wide_handle(GTK_PANED(window->paned), TRUE);
	gtk_widget_set_vexpand(window->paned, TRUE);
	gtk_box_append(GTK_BOX(root), window->paned);
	window->sidebar_scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window->sidebar_scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(window->sidebar_scroll, 142, -1);
	window->sidebar = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(window->sidebar),
		GTK_SELECTION_SINGLE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(window->sidebar), TRUE);
	gtk_widget_add_css_class(window->sidebar, "sidebar-list");
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(window->sidebar_scroll),
		window->sidebar);
	gtk_paned_set_start_child(GTK_PANED(window->paned), window->sidebar_scroll);
	gtk_paned_set_resize_start_child(GTK_PANED(window->paned), FALSE);
	gtk_paned_set_shrink_start_child(GTK_PANED(window->paned), FALSE);
	gtk_widget_set_visible(window->sidebar_scroll, FALSE);
	window->stack = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(window->stack),
		GTK_STACK_TRANSITION_TYPE_NONE);
	gtk_stack_set_hhomogeneous(GTK_STACK(window->stack), FALSE);
	gtk_stack_set_vhomogeneous(GTK_STACK(window->stack), FALSE);
	gtk_widget_set_hexpand(window->stack, TRUE);
	gtk_widget_set_vexpand(window->stack, TRUE);
	gtk_paned_set_end_child(GTK_PANED(window->paned), window->stack);
	gtk_paned_set_resize_end_child(GTK_PANED(window->paned), TRUE);
	gtk_paned_set_shrink_end_child(GTK_PANED(window->paned), TRUE);
	gtk_paned_set_position(GTK_PANED(window->paned),
		window->state.paned_position);

	add_sidebar_rows(window);
	build_pages(window);
#if defined(SEYRIX_SETTINGS_RENDER_DIAGNOSTICS)
	if (settings_render_trace_enabled()) {
		for (int i = 0; i < CATEGORY_COUNT; i++) {
			if (window->pages[i] != NULL) {
				g_signal_connect(window->pages[i], "map",
					G_CALLBACK(on_trace_page_map), window);
				g_signal_connect(window->pages[i], "unmap",
					G_CALLBACK(on_trace_page_unmap), window);
			}
		}
		g_signal_connect(window->stack, "notify::visible-child-name",
			G_CALLBACK(on_trace_stack_selection), window);
		g_signal_connect(window->category_selector, "map",
			G_CALLBACK(on_trace_category_selector_map), window);
		g_signal_connect(window->category_selector, "unmap",
			G_CALLBACK(on_trace_category_selector_unmap), window);
	}
#endif
	g_signal_connect(window->sidebar, "row-selected",
		G_CALLBACK(on_sidebar_row_selected), window);
	g_signal_connect(window->sidebar, "row-activated",
		G_CALLBACK(on_sidebar_row_activated), window);
	g_signal_connect(window->search, "search-changed",
		G_CALLBACK(on_search_changed), window);
	g_signal_connect(window->category_selector, "notify::selected",
		G_CALLBACK(on_category_selector_changed), window);
	g_signal_connect(window->window, "notify::width",
		G_CALLBACK(on_window_width_changed), window);
	g_signal_connect(window->window, "notify::height",
		G_CALLBACK(on_window_size_changed), window);
	g_signal_connect(window->window, "notify::maximized",
		G_CALLBACK(on_window_size_changed), window);
	g_signal_connect(window->paned, "notify::position",
		G_CALLBACK(on_paned_position_changed), window);
	GtkEventController *keys = gtk_event_controller_key_new();
	g_signal_connect(keys, "key-pressed", G_CALLBACK(on_window_key), window);
	gtk_widget_add_controller(window->window, keys);
#if defined(SEYRIX_SETTINGS_RENDER_DIAGNOSTICS)
	if (settings_render_trace_enabled()) {
		GtkEventController *trace_events = gtk_event_controller_legacy_new();
		gtk_event_controller_set_propagation_phase(trace_events,
			GTK_PHASE_CAPTURE);
		g_signal_connect(trace_events, "event",
			G_CALLBACK(on_trace_input_event), window);
		gtk_widget_add_controller(window->window, trace_events);
	}
#endif
	select_category(window, (enum category_id)window->state.category);
	if (window->state.maximized) {
		gtk_window_maximize(GTK_WINDOW(window->window));
	}
	window->restoring_state = false;
	seyrix_settings_window_refresh(window);
	window->layout_source = g_timeout_add(120, on_window_layout_check, window);
	return window;
}

void seyrix_settings_window_present(struct seyrix_settings_window *window) {
	if (window != NULL && window->window != NULL) {
		gtk_window_present(GTK_WINDOW(window->window));
	}
}

void seyrix_settings_window_get_state(struct seyrix_settings_window *window,
		struct seyrix_settings_window_state *state) {
	if (window == NULL || state == NULL) {
		return;
	}
	*state = window->state;
	state->category = (guint)window->selected_category;
	if (window->window == NULL) {
		return;
	}
	gboolean was_maximized = window->state.maximized;
	state->maximized = gtk_window_is_maximized(GTK_WINDOW(window->window));
	if (!state->maximized && !was_maximized) {
		int width = gtk_widget_get_width(window->window);
		int height = gtk_widget_get_height(window->window);
		if (width >= 400 && height >= 300) {
			state->width = width;
			state->height = height;
		}
	}
	int position = gtk_paned_get_position(GTK_PANED(window->paned));
	if (position >= 100 && position <= 2048) {
		state->paned_position = position;
	}
	window->state = *state;
}

const char *seyrix_settings_window_category_id(
		struct seyrix_settings_window *window) {
	return window != NULL && window->selected_category < CATEGORY_COUNT ?
		category_ids[window->selected_category] : "appearance";
}

void seyrix_settings_window_destroy(struct seyrix_settings_window *window) {
	if (window == NULL) {
		return;
	}
	if (window->layout_source != 0) {
		g_source_remove(window->layout_source);
		window->layout_source = 0;
	}
	if (window->sidebar_detached) {
		g_object_unref(window->sidebar_scroll);
		window->sidebar_detached = false;
	}
	if (window->window != NULL) {
		gtk_window_destroy(GTK_WINDOW(window->window));
		window->window = NULL;
	}
	window->stack = NULL;
	window->sidebar = NULL;
	g_free(window);
}
