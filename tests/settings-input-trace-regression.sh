#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source_file="$script_dir/../desktop/settings/src/settings-window.c"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

callback="$(sed -n '/^static gboolean on_trace_input_event(/,/^}/p' "$source_file")"
[[ -n "$callback" ]] || fail "cannot locate the opt-in GTK input trace callback"

assert_callback_contains() {
  local expected=$1 description=$2
  grep -Fq -- "$expected" <<<"$callback" || fail "$description"
}

assert_callback_contains 'type != GDK_BUTTON_RELEASE' \
  "callback does not accept GDK button-release events"
assert_callback_contains 'type == GDK_BUTTON_PRESS || type == GDK_BUTTON_RELEASE' \
  "button press and release do not share the raw-input trace path"
assert_callback_contains '"button-press" : "button-release"' \
  "raw-input trace does not distinguish press from release"
assert_callback_contains '"gtk_input_button_press" : "gtk_input_button_release"' \
  "GTK trace does not distinguish press from release"
assert_callback_contains 'button=%u positioned=%d' \
  "raw-input trace omits the button number or pointer position"
assert_callback_contains 'settings_trace_event(window, trace_event,' \
  "GTK trace event is not emitted for button input"
assert_callback_contains 'button == GDK_BUTTON_PRIMARY ? "button-1"' \
  "primary button release cannot be identified in the trace"
assert_callback_contains 'return FALSE;' \
  "trace callback must propagate input without changing behavior"
if grep -Fq 'return TRUE;' <<<"$callback"; then
  fail "trace callback consumes input"
fi
grep -Fq 'return enabled != NULL && strcmp(enabled, "1") == 0;' "$source_file" ||
  fail "raw input tracing is not gated by SEYRIX_SETTINGS_RENDER_TRACE=1"
grep -Fq 'g_signal_connect(trace_events, "event",' "$source_file" ||
  fail "opt-in legacy event controller is not connected"
grep -Fq 'G_CALLBACK(on_trace_input_event), window);' "$source_file" ||
  fail "opt-in event controller is not connected to the raw-input trace callback"

click_callback="$(sed -n '/^static void on_sidebar_row_click_released(/,/^}/p' "$source_file")"
[[ -n "$click_callback" ]] || fail "cannot locate the sidebar pointer-release handler"
grep -Fq 'n_press != 1' <<<"$click_callback" ||
  fail "sidebar pointer handler does not reject multi-press activations"
grep -Fq 'x >= gtk_widget_get_width(row)' <<<"$click_callback" ||
  fail "sidebar pointer handler does not require release inside the row"
grep -Fq 'select_category(window, (enum category_id)category);' <<<"$click_callback" ||
  fail "sidebar pointer release does not select its category"
grep -Fq 'gtk_editable_set_text(GTK_EDITABLE(window->search), "");' <<<"$click_callback" ||
  fail "sidebar pointer release does not clear search"
grep -Fq 'GtkGesture *click = gtk_gesture_click_new();' "$source_file" ||
  fail "sidebar rows lack the GTK click fallback controller"
grep -Fq 'G_CALLBACK(on_sidebar_row_click_released), window);' "$source_file" ||
  fail "sidebar click fallback is not connected"
grep -Fq 'gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),' "$source_file" ||
  fail "sidebar click fallback does not restrict activation to the primary button"

echo "PASS: raw GDK button trace accepts and labels press and release events"
echo "PASS: primary button identity and pointer position remain in trace evidence"
echo "PASS: trace callback propagates events without changing input behavior"
echo "PASS: raw-input tracing remains behind the existing opt-in environment gate"
echo "PASS: sidebar pointer release selects the hit category and clears search"
echo "PASS: sidebar click fallback accepts only a primary single release inside the row"
