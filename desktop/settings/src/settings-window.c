#include "settings-window.h"

#include "system-info.h"

#include <gdk/gdkkeysyms.h>
#include <string.h>

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
	struct seyrix_system_info system_info;
	bool sidebar_detached;
	bool compact_window_maximized;
	bool compact_layout;
	guint layout_source;
};

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


static const char *const category_selector_names[] = {
	"Appearance", "Desktop & Dock", "Window Management", "Displays",
	"Input", "About Seyrix", NULL,
};

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

static void set_visible_page(struct seyrix_settings_window *window,
		size_t page_index) {
	if (window == NULL || page_index >= G_N_ELEMENTS(window->pages) ||
			window->pages[page_index] == NULL ||
			!GTK_IS_STACK(window->stack)) {
		return;
	}
	GtkWidget *page = window->pages[page_index];
	gtk_stack_set_visible_child(GTK_STACK(window->stack), page);
}

static void select_category(struct seyrix_settings_window *window,
		enum category_id category) {
	if (category >= CATEGORY_COUNT) {
		return;
	}
	window->selected_category = category;
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
}

static void on_category_selector_changed(GObject *object, GParamSpec *property,
		gpointer data) {
	(void)property;
	struct seyrix_settings_window *window = data;
	guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
	if (selected < CATEGORY_COUNT) {
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
	if (window->compact_layout != compact) {
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
}

static gboolean on_window_layout_check(gpointer data) {
	update_responsive_layout(data);
	return G_SOURCE_CONTINUE;
}

static void on_window_width_changed(GObject *object, GParamSpec *property,
		gpointer data) {
	(void)object;
	(void)property;
	update_responsive_layout(data);
}

static void on_sidebar_row_selected(GtkListBox *list, GtkListBoxRow *row,
		gpointer data) {
	(void)list;
	if (row == NULL) {
		return;
	}
	struct seyrix_settings_window *window = data;
	int category = gtk_list_box_row_get_index(row);
	if (category >= 0 && category < CATEGORY_COUNT) {
		select_category(window, (enum category_id)category);
		gtk_editable_set_text(GTK_EDITABLE(window->search), "");
	}
}

static void on_sidebar_row_activated(GtkListBox *list, GtkListBoxRow *row,
		gpointer data) {
	on_sidebar_row_selected(list, row, data);
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
		GtkApplication *application, struct seyrix_settings_client *client) {
	struct seyrix_settings_window *window = g_new0(
		struct seyrix_settings_window, 1);
	window->client = client;
	window->selected_category = CATEGORY_APPEARANCE;
	seyrix_system_info_collect(&window->system_info);
	window->window = gtk_application_window_new(application);
	gtk_window_set_title(GTK_WINDOW(window->window), "System Settings");
	gtk_window_set_default_size(GTK_WINDOW(window->window), 720, 560);
	gtk_window_set_resizable(GTK_WINDOW(window->window), TRUE);

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

	add_sidebar_rows(window);
	build_pages(window);
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
	GtkEventController *keys = gtk_event_controller_key_new();
	g_signal_connect(keys, "key-pressed", G_CALLBACK(on_window_key), window);
	gtk_widget_add_controller(window->window, keys);
	select_category(window, CATEGORY_APPEARANCE);
	seyrix_settings_window_refresh(window);
	window->layout_source = g_timeout_add(120, on_window_layout_check, window);
	return window;
}

void seyrix_settings_window_present(struct seyrix_settings_window *window) {
	if (window != NULL && window->window != NULL) {
		gtk_window_present(GTK_WINDOW(window->window));
	}
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
