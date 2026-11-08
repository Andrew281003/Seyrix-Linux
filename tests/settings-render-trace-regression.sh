#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "$script_dir/.." && pwd -P)"
fixture_dir="$script_dir/fixtures/settings-render-trace"
temporary_dir="$(mktemp -d)"
trap 'rm -rf -- "$temporary_dir"' EXIT
source "$script_dir/settings-render-trace-functions.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

assert_eq() {
  local expected=$1 actual=$2 description=$3
  [[ "$actual" == "$expected" ]] || fail "$description (expected '$expected', got '$actual')"
}

assert_true() {
  local description=$1
  shift
  "$@" || fail "$description"
}

assert_false() {
  local description=$1
  shift
  if "$@"; then
    fail "$description"
  fi
}

cat >"$temporary_dir/synthetic-valid.trace" <<'EOF'
SEYRIX_SETTINGS_TRACE monotonic_ns=1 pid=776 event=gtk_page_map page=appearance selected=appearance mapped=1
monotonic_ns=10 source=client-stderr pid=776 -> wl_surface#16.attach(wl_buffer#24, 0, 0)
monotonic_ns=20 source=client-stderr pid=776 -> wl_surface#16.damage_buffer(2, 3, 20, 30)
monotonic_ns=30 source=client-stderr pid=776 -> wl_surface#16.commit()
EOF
cat >"$temporary_dir/synthetic-valid.log" <<'EOF'
SETTINGS_TRACE_APP_PID=776 UID=1000 NATIVE_EXE=/usr/bin/seyrix-settings TRACE_OWNER=seyrix:seyrix CHECKED_PROC_PID=776
SEYRIX_SETTINGS_RENDER_TRACE=1
WAYLAND_DEBUG=client
XDG_RUNTIME_DIR=/run/user/1000
WAYLAND_DISPLAY=wayland-0
EOF
synthetic_trace_pid="$(settings_trace_initial_appearance_pid "$temporary_dir/synthetic-valid.trace")"
synthetic_metadata="$(grep -a -E 'SETTINGS_TRACE_APP_PID=[0-9]+ UID=' "$temporary_dir/synthetic-valid.log" | head -n 1)"
synthetic_log_pid="$(printf '%s\n' "$synthetic_metadata" | sed -n -E 's/.*SETTINGS_TRACE_APP_PID=([0-9]+).*/\1/p')"
synthetic_uid="$(printf '%s\n' "$synthetic_metadata" | sed -n -E 's/.* UID=([0-9]+) .*/\1/p')"
synthetic_exe="$(printf '%s\n' "$synthetic_metadata" | sed -n -E 's/.* NATIVE_EXE=([^ ]+) .*/\1/p')"
synthetic_owner="$(printf '%s\n' "$synthetic_metadata" | sed -n -E 's/.* TRACE_OWNER=([^ ]+).*/\1/p')"
assert_eq 776 "$synthetic_trace_pid" "synthetic trace GTK process parser"
assert_eq "$synthetic_trace_pid" "$synthetic_log_pid" "synthetic trace PID matches the captured Settings process"
assert_eq 1000 "$synthetic_uid" "synthetic Settings UID"
assert_eq /usr/bin/seyrix-settings "$synthetic_exe" "synthetic Settings executable"
assert_eq seyrix:seyrix "$synthetic_owner" "synthetic trace owner"
assert_true "synthetic log confirms traced environment values" grep -a -Fq 'SEYRIX_SETTINGS_RENDER_TRACE=1' "$temporary_dir/synthetic-valid.log"
assert_true "synthetic log confirms WAYLAND_DEBUG=client" grep -a -Fq 'WAYLAND_DEBUG=client' "$temporary_dir/synthetic-valid.log"
synthetic_ops="$(settings_trace_client_surface_event_count "$temporary_dir/synthetic-valid.trace" "$synthetic_trace_pid")"
synthetic_ids="$(settings_trace_client_surface_ids_from_file "$temporary_dir/synthetic-valid.trace" "$synthetic_trace_pid")"
assert_eq 3 "$synthetic_ops" "synthetic wl_surface operation count"
assert_eq 16 "$synthetic_ids" "synthetic wl_surface object ID"
assert_true "synthetic GTK and proc evidence passes the production launch gate" \
  settings_trace_launch_gate "$synthetic_trace_pid" "$synthetic_log_pid" "$synthetic_uid" "$synthetic_exe" "$synthetic_owner" \
  appearance yes yes yes 1 client /run/user/1000 wayland-0 wayland-0
echo "PASS: synthetic trace PID, environment, and launch gate"


empty_count="$(settings_trace_client_surface_event_count /dev/null 776)"
empty_pid="$(settings_trace_initial_appearance_pid /dev/null || true)"
assert_eq 0 "$empty_count" "empty trace has no surface activity"
assert_eq '' "$empty_pid" "empty trace has no Settings GTK PID"
assert_false "empty trace fails the production launch gate" \
  settings_trace_launch_gate '' '' 1000 /usr/bin/seyrix-settings seyrix:seyrix appearance yes yes yes 1 client /run/user/1000 wayland-0 wayland-0

malformed_count="$(settings_trace_client_surface_event_count "$fixture_dir/malformed.trace" 500)"
truncated_count="$(settings_trace_client_surface_event_count "$fixture_dir/truncated.trace" 500)"
assert_eq 0 "$malformed_count" "malformed trace has no surface activity"
assert_eq 0 "$truncated_count" "truncated request lines are not counted as complete operations"
assert_eq '' "$(settings_trace_client_surface_ids_from_file "$fixture_dir/malformed.trace" 500)" "malformed IDs are rejected"
echo "PASS: empty, malformed, and truncated trace fixtures fail closed"

at_count="$(settings_trace_client_surface_event_count "$fixture_dir/valid-at.trace" 500)"
at_ids="$(settings_trace_client_surface_ids_from_file "$fixture_dir/valid-at.trace" 500)"
assert_eq 3 "$at_count" "wl_surface@N operation count"
assert_eq 12 "$at_ids" "wl_surface@N object ID"

cat >"$temporary_dir/hash-format.trace" <<'EOF'
monotonic_ns=1 source=client-stderr pid=501 -> wl_surface#9.attach(wl_buffer#24, 0, 0)
monotonic_ns=2 source=client-stderr pid=501 -> wl_surface#9.damage_buffer(2, 3, 20, 30)
monotonic_ns=3 source=client-stderr pid=501 -> wl_surface#9.commit()
EOF
assert_eq 3 "$(settings_trace_client_surface_event_count "$temporary_dir/hash-format.trace" 501)" "wl_surface#N operation count"
assert_eq 9 "$(settings_trace_client_surface_ids_from_file "$temporary_dir/hash-format.trace" 501)" "wl_surface#N object ID"
echo "PASS: both wl_surface@N and wl_surface#N request formats"

other_count="$(settings_trace_client_surface_event_count "$fixture_dir/unrelated-process.trace" 776)"
assert_eq 0 "$other_count" "unrelated process protocol records are ignored"
assert_eq 2 "$(settings_trace_client_surface_event_count "$fixture_dir/unrelated-process.trace" 740)" "unrelated process can be parsed only under its own PID"
cat >"$temporary_dir/gtk-no-client.trace" <<'EOF'
SEYRIX_SETTINGS_TRACE monotonic_ns=1 pid=776 event=gtk_page_map page=appearance selected=appearance mapped=1 visible=1
monotonic_ns=2 source=client-stderr pid=740 -> wl_surface#16.commit()
EOF
no_client_pid="$(settings_trace_initial_appearance_pid "$temporary_dir/gtk-no-client.trace")"
no_client_ops="$(settings_trace_client_surface_event_count "$temporary_dir/gtk-no-client.trace" "$no_client_pid")"
assert_eq 776 "$no_client_pid" "Settings GTK event chooses the traced Settings PID"
assert_eq 0 "$no_client_ops" "unrelated process activity does not become Settings protocol activity"
assert_true "valid Settings GTK and environment evidence permits input without optional client records" \
  settings_trace_launch_gate "$no_client_pid" 776 1000 /usr/bin/seyrix-settings seyrix:seyrix \
  appearance yes yes yes 1 client /run/user/1000 wayland-0 wayland-0
assert_false "environment checks cannot use a different process PID" \
  settings_trace_launch_gate "$no_client_pid" 740 1000 /usr/bin/seyrix-settings seyrix:seyrix \
  appearance yes yes yes 1 client /run/user/1000 wayland-0 wayland-0
echo "PASS: optional client format does not block input; unrelated PID cannot satisfy Settings checks"

assert_true "Settings and compositor process session IDs must match" \
  settings_trace_same_session_gate 644 644
assert_false "a serial-session Settings process cannot pass the compositor-session gate" \
  settings_trace_same_session_gate 644 611
ss_connected='u_seq ESTAB 0 0 /run/user/1000/seyrix/settings-v1.sock 1284 * 0 users:(("seyrix-settings",pid=776,fd=12))'
assert_true "Settings must hold an established sequenced-packet endpoint connection" \
  settings_trace_endpoint_connection_gate "$ss_connected" 776
assert_false "an endpoint listener is not a Settings client connection" \
  settings_trace_endpoint_connection_gate 'u_seq LISTEN 0 16 /run/user/1000/seyrix/settings-v1.sock 1283 * 0 users:(("seyrix-compositor",pid=644,fd=9))' 776
assert_false "another process endpoint connection cannot prove Settings connectivity" \
  settings_trace_endpoint_connection_gate 'u_seq ESTAB 0 0 /run/user/1000/seyrix/settings-v1.sock 1284 * 0 users:(("seyrix-settings",pid=740,fd=12))' 776
echo "PASS: endpoint evidence requires an established socket owned by the Settings PID in the compositor session"

collision_ids="$(settings_trace_client_surface_ids_from_file "$fixture_dir/pointer-namespace-collision.trace" 776)"
assert_eq 16 "$collision_ids" "client wl_surface ID is scoped to the Settings PID"
pointer_only_server='SETTINGS_RENDER_TRACE event=surface_commit monotonic_ns=3 pid=776 pointer_id=16 wl_surface=22 seq=4 buffer_attached=1'
assert_false "compositor pointer ID cannot be mistaken for a wl_surface resource ID" \
  settings_trace_correlate_surface_id 776 "$collision_ids" "$pointer_only_server"
matching_server='SETTINGS_RENDER_TRACE event=surface_commit monotonic_ns=4 pid=776 pointer_id=7 wl_surface=16 seq=5 buffer_attached=1'
assert_eq 16 "$(settings_trace_correlate_surface_id 776 "$collision_ids" "$matching_server")" \
  "same-process compositor wl_surface resource correlation"
echo "PASS: client object IDs correlate only with same-PID compositor wl_surface fields"

cat >"$temporary_dir/gtk-transition.trace" <<'EOF'
monotonic_ns=100 source=client-stderr pid=776 SEYRIX_SETTINGS_TRACE monotonic_ns=1 pid=776 event=gtk_page_map page=appearance selected=appearance mapped=1
monotonic_ns=200 source=client-stderr pid=740 SEYRIX_SETTINGS_TRACE monotonic_ns=2 pid=740 event=gtk_page_map page=desktop selected=desktop mapped=1
monotonic_ns=300 source=client-stderr pid=776 SEYRIX_SETTINGS_TRACE monotonic_ns=3 pid=776 event=gtk_page_map page=desktop selected=desktop mapped=1
monotonic_ns=400 source=client-stderr pid=776 SEYRIX_SETTINGS_TRACE monotonic_ns=4 pid=776 event=gtk_page_selection page=desktop selected=desktop mapped=1
monotonic_ns=500 source=client-stderr pid=776 SEYRIX_SETTINGS_TRACE monotonic_ns=5 pid=776 event=gtk_page_unmap page=appearance selected=desktop mapped=0
EOF
assert_true "selected page transition is verified for the Settings PID" \
  settings_trace_gtk_transition_gate "$temporary_dir/gtk-transition.trace" 776
assert_false "another process's page events cannot prove Settings selection" \
  settings_trace_gtk_transition_gate "$temporary_dir/gtk-transition.trace" 740
assert_eq 4 "$(settings_trace_page_selection_timestamp_ns "$temporary_dir/gtk-transition.trace" 776)" \
  "GTK Desktop selection timestamp belongs to the Settings PID"
cat >"$temporary_dir/gtk-transition-selection-first.trace" <<'EOF'
SEYRIX_SETTINGS_TRACE monotonic_ns=1 pid=776 event=gtk_page_map page=appearance selected=appearance mapped=1
SEYRIX_SETTINGS_TRACE monotonic_ns=2 pid=776 event=gtk_page_selection page=desktop selected=desktop mapped=1
SEYRIX_SETTINGS_TRACE monotonic_ns=3 pid=776 event=gtk_page_map page=desktop selected=desktop mapped=1
SEYRIX_SETTINGS_TRACE monotonic_ns=4 pid=776 event=gtk_page_unmap page=appearance selected=desktop mapped=0
EOF
assert_true "GTK also accepts a selection signal before destination-page mapping" \
  settings_trace_gtk_transition_gate "$temporary_dir/gtk-transition-selection-first.trace" 776
cat >"$temporary_dir/gtk-transition-out-of-order.trace" <<'EOF'
SEYRIX_SETTINGS_TRACE monotonic_ns=4 pid=776 event=gtk_page_map page=appearance selected=appearance mapped=1
SEYRIX_SETTINGS_TRACE monotonic_ns=3 pid=776 event=gtk_page_selection page=desktop selected=desktop mapped=1
SEYRIX_SETTINGS_TRACE monotonic_ns=2 pid=776 event=gtk_page_map page=desktop selected=desktop mapped=1
SEYRIX_SETTINGS_TRACE monotonic_ns=1 pid=776 event=gtk_page_unmap page=appearance selected=desktop mapped=0
EOF
assert_false "GTK selection and destination mapping must both precede Appearance unmap" \
  settings_trace_gtk_transition_gate "$temporary_dir/gtk-transition-out-of-order.trace" 776
cat >"$temporary_dir/gtk-sidebar-bounds.trace" <<'EOF'
SEYRIX_SETTINGS_TRACE monotonic_ns=10 pid=776 event=gtk_sidebar_row_bounds page=desktop selected=appearance mapped=1 bounds_space=GtkWindow bounds_valid=1 bounds=20.0,225.0,172.0,34.0
SEYRIX_SETTINGS_TRACE monotonic_ns=20 pid=776 event=gtk_sidebar_row_bounds page=desktop selected=appearance mapped=1 bounds_space=GtkWindow bounds_valid=1 bounds=20.0,183.0,172.0,34.0
SEYRIX_SETTINGS_TRACE monotonic_ns=30 pid=776 event=gtk_input_button_press page=button-1 selected=appearance mapped=1 target_widget=GtkLabel target_category=desktop bounds_space=GtkWindow bounds_valid=1 bounds=30.0,191.0,152.0,18.0
SEYRIX_SETTINGS_RAW_INPUT pid=776 type=button-press space=GtkWindow button=1 positioned=1 x=106.0 y=200.0 target_widget=GtkLabel
EOF
sidebar_bounds_line="$(grep -F 'pid=776 event=gtk_sidebar_row_bounds page=desktop ' "$temporary_dir/gtk-sidebar-bounds.trace" | tail -n 1)"
sidebar_bounds="$(sed -nE 's/.*bounds_space=GtkWindow bounds_valid=1 bounds=([0-9.]+),([0-9.]+),([0-9.]+),([0-9.]+).*/\1,\2,\3,\4/p' <<< "$sidebar_bounds_line")"
assert_eq '20.0,183.0,172.0,34.0' "$sidebar_bounds" "latest mapped Desktop row bounds replace stale map-time coordinates"
mouse_target_line="$(grep -F 'pid=776 event=gtk_input_button_press ' "$temporary_dir/gtk-sidebar-bounds.trace")"
mouse_raw_line="$(grep -F 'SEYRIX_SETTINGS_RAW_INPUT pid=776 type=button-press ' "$temporary_dir/gtk-sidebar-bounds.trace")"
mouse_target_bounds="$(sed -nE 's/.*bounds_space=GtkWindow bounds_valid=1 bounds=([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+).*/\1 \2 \3 \4/p' <<< "$mouse_target_line")"
mouse_raw_position="$(sed -nE 's/.*positioned=1 x=([-0-9.]+) y=([-0-9.]+).*/\1 \2/p' <<< "$mouse_raw_line")"
read -r target_x target_y target_width target_height <<< "$mouse_target_bounds"
read -r event_x event_y <<< "$mouse_raw_position"
assert_true "QMP click translation lands within the GTK picked widget bounds" \
  awk -v x="$event_x" -v y="$event_y" -v left="$target_x" -v top="$target_y" \
    -v width="$target_width" -v height="$target_height" \
    'BEGIN { exit !(x >= left && x <= left + width && y >= top && y <= top + height) }'
assert_eq '106.0 200.0' "$mouse_raw_position" "raw GTK event coordinates parse despite intervening button metadata"
assert_true "picked widget identity matches the intended GTK category" \
  grep -Fq 'target_category=desktop' <<< "$mouse_target_line"
echo "PASS: latest mapped sidebar bounds, GTK widget identity, and click coordinate space"
assert_true "mouse diagnostic captures the QMP tablet event log it starts" grep -Fq 'SETTINGS_QMP_EVTEST_BEGIN' "$repo_root/tests/qemu-graphics.exp"
assert_true "complete pointer and keyboard input passes the production input gate" \
  settings_trace_input_gate 1 1 1 1 passed
assert_false "missing pointer release fails the production input gate" \
  settings_trace_input_gate 1 1 1 0 passed
assert_false "skipped keys fail the production input gate" \
  settings_trace_input_gate 1 1 1 1 failed
echo "PASS: GTK transition and actual-input launch gates are process scoped"

cat >"$temporary_dir/render-client.trace" <<'EOF'
monotonic_ns=110 source=client-stderr pid=776 -> wl_surface#16.attach(wl_buffer#24, 0, 0)
monotonic_ns=120 source=client-stderr pid=776 -> wl_surface#16.damage_buffer(10, 97, 620, 34)
monotonic_ns=130 source=client-stderr pid=776 -> wl_surface#16.commit()
EOF
render_surface='SETTINGS_RENDER_TRACE event=surface_commit monotonic_ns=140 pid=776 xdg_surface=22 pointer_id=16 wl_surface=16 seq=2 buffer_attached=1 surface_damage=0:0,0-0,0 buffer_damage=1:10,97-630,131'
render_scene='SETTINGS_RENDER_TRACE event=output_scene_commit monotonic_ns=150 result=success pid=776 xdg_surface=22 pointer_id=16 wl_surface=16 surface_seq=2'
render_present='SETTINGS_RENDER_TRACE event=output_present monotonic_ns=160 presented=1 pid=776 xdg_surface=22 pointer_id=16 wl_surface=16 surface_seq=2'
render_sequence="$(settings_trace_render_sequence_gate \
  "$(cat "$temporary_dir/render-client.trace")" "$render_surface" "$render_scene" "$render_present" \
  776 16 2 100)" || fail "same-PID object and timestamp correlated render sequence was rejected"
assert_true "render correlation returns its ordered timestamp evidence" \
  grep -Fq 'gtk_selection_ns=100 client_attach_ns=110 client_damage_ns=120 client_commit_ns=130 compositor_surface_commit_ns=140 scene_commit_ns=150 output_present_ns=160' <<<"$render_sequence"
pointer_collision_surface='SETTINGS_RENDER_TRACE event=surface_commit monotonic_ns=140 pid=776 xdg_surface=22 pointer_id=16 wl_surface=22 seq=2 buffer_attached=1 surface_damage=1:10,97-630,131'
assert_false "matching compositor pointer ID cannot replace the client wl_surface resource ID" \
  settings_trace_render_sequence_gate "$(cat "$temporary_dir/render-client.trace")" \
    "$pointer_collision_surface" "$render_scene" "$render_present" 776 16 2 100
late_present='SETTINGS_RENDER_TRACE event=output_present monotonic_ns=149 presented=1 pid=776 xdg_surface=22 pointer_id=16 wl_surface=16 surface_seq=2'
assert_false "output presentation timestamp must follow the matching scene commit" \
  settings_trace_render_sequence_gate "$(cat "$temporary_dir/render-client.trace")" \
    "$render_surface" "$render_scene" "$late_present" 776 16 2 100
echo "PASS: render correlation requires post-selection client requests and later same-PID/surface/sequence compositor events"

source "$script_dir/qemu-graphics-preflight.sh"
mkdir -p "$temporary_dir/harness-complete" "$temporary_dir/harness-missing-helper" "$temporary_dir/harness-missing-fixture"
while IFS= read -r helper; do
  helper_dir="$(dirname -- "$helper")"
  mkdir -p "$temporary_dir/harness-complete/$helper_dir" \
    "$temporary_dir/harness-missing-helper/$helper_dir" \
    "$temporary_dir/harness-missing-fixture/$helper_dir"
  : >"$temporary_dir/harness-complete/$helper"
  : >"$temporary_dir/harness-missing-helper/$helper"
  : >"$temporary_dir/harness-missing-fixture/$helper"
  if [[ "$helper" != fixtures/* ]]; then
    chmod 755 "$temporary_dir/harness-complete/$helper" \
      "$temporary_dir/harness-missing-helper/$helper" \
      "$temporary_dir/harness-missing-fixture/$helper"
  fi
  if [[ "$helper" != qemu-guest-helper.sh ]]; then
    :
  else
    rm -f -- "$temporary_dir/harness-missing-helper/$helper"
  fi
  if [[ "$helper" == fixtures/settings-render-trace/valid-at.trace ]]; then
    rm -f -- "$temporary_dir/harness-missing-fixture/$helper"
  fi
done < <(qemu_graphics_required_support_files phase2d3b 1)
while IFS= read -r helper; do
  helper_dir="$(dirname -- "$helper")"
  mkdir -p "$temporary_dir/harness-complete/$helper_dir" \
    "$temporary_dir/harness-missing-helper/$helper_dir" \
    "$temporary_dir/harness-missing-fixture/$helper_dir"
  for harness in harness-complete harness-missing-helper harness-missing-fixture; do
    : >"$temporary_dir/$harness/$helper"
    chmod 755 "$temporary_dir/$harness/$helper"
  done
done < <(qemu_graphics_required_executable_files phase2d3b 1)
rm -f -- "$temporary_dir/harness-missing-helper/qemu-guest-helper.sh" \
  "$temporary_dir/harness-missing-fixture/fixtures/settings-render-trace/valid-at.trace"
assert_true "complete trace-run snapshot passes the production support-file preflight" \
  qemu_graphics_validate_support_files "$temporary_dir/harness-complete" phase2d3b 1
if qemu_graphics_validate_support_files "$temporary_dir/harness-missing-helper" phase2d3b 1 \
    2>"$temporary_dir/missing-helper.error"; then
  fail "missing phase2D guest helper passed the production preflight"
fi
assert_true "missing guest helper is named in preflight error" \
  grep -Fq 'qemu-guest-helper.sh' "$temporary_dir/missing-helper.error"
if qemu_graphics_validate_support_files "$temporary_dir/harness-missing-fixture" phase2d3b 1 \
    2>"$temporary_dir/missing-fixture.error"; then
  fail "missing trace fixture passed the production preflight"
fi
assert_true "missing trace fixture is named in preflight error" \
  grep -Fq 'valid-at.trace' "$temporary_dir/missing-fixture.error"
echo "PASS: QEMU preflight rejects a missing executable helper or trace fixture before boot"

cat >"$temporary_dir/capture.trace" <<'EOF'
SEYRIX_SETTINGS_TRACE monotonic_ns=90 pid=776 event=gtk_page_map page=appearance selected=appearance mapped=1
SEYRIX_SETTINGS_TRACE monotonic_ns=100 pid=776 event=gtk_page_selection page=desktop selected=desktop mapped=1
monotonic_ns=110 source=client-stderr pid=776 -> wl_surface#16.attach(wl_buffer#24, 0, 0)
monotonic_ns=120 source=client-stderr pid=776 -> wl_surface#16.damage_buffer(22, 66, 364, 15)
monotonic_ns=130 source=client-stderr pid=776 -> wl_surface#16.commit()
SEYRIX_SETTINGS_TRACE monotonic_ns=135 pid=776 event=gtk_page_map page=desktop selected=desktop mapped=1
SEYRIX_SETTINGS_TRACE monotonic_ns=136 pid=776 event=gtk_page_unmap page=appearance selected=desktop mapped=0
EOF
cat >"$temporary_dir/capture.compositor" <<'EOF'
SETTINGS_RENDER_TRACE event=surface_commit monotonic_ns=140 pid=776 xdg_surface=22 wl_surface=16 seq=2 committed=0x00000003 buffer_attached=1 mapped=1 width=640 height=480 surface_damage=1:10,97-630,131
SETTINGS_RENDER_TRACE event=output_scene_commit monotonic_ns=150 result=success pid=776 xdg_surface=22 wl_surface=16 surface_seq=2
SETTINGS_RENDER_TRACE event=output_present monotonic_ns=160 presented=1 pid=776 xdg_surface=22 wl_surface=16 surface_seq=2
EOF
cat >"$temporary_dir/capture.keyboard" <<'EOF'
Event: time 1.000000, type 1 (EV_KEY), code 102 (KEY_HOME), value 1
Event: time 1.000100, type 1 (EV_KEY), code 102 (KEY_HOME), value 0
Event: time 1.001000, type 1 (EV_KEY), code 108 (KEY_DOWN), value 1
Event: time 1.001100, type 1 (EV_KEY), code 108 (KEY_DOWN), value 0
Event: time 1.002000, type 1 (EV_KEY), code 28 (KEY_ENTER), value 1
Event: time 1.002100, type 1 (EV_KEY), code 28 (KEY_ENTER), value 0
EOF
cat >"$temporary_dir/capture.mouse" <<'EOF'
Event: time 1.000000, type 3 (EV_ABS), code 0 (ABS_X), value 16409
Event: time 1.000010, type 3 (EV_ABS), code 1 (ABS_Y), value 10671
Event: time 1.000020, type 1 (EV_KEY), code 272 (BTN_LEFT), value 1
Event: time 1.000030, type 1 (EV_KEY), code 272 (BTN_LEFT), value 0
EOF
printf 'appearance\n' >"$temporary_dir/capture.page-before"
printf '1\n' >"$temporary_dir/capture.trace-lines-before"
printf '0\n' >"$temporary_dir/capture.surface-before"
printf '0\n' >"$temporary_dir/capture.scene-before"
printf '0\n' >"$temporary_dir/capture.present-before"
printf '776\n' >"$temporary_dir/capture.pid"
export SEYRIX_SETTINGS_TRACE_FILE="$temporary_dir/capture.trace"
export SEYRIX_SETTINGS_COMPOSITOR_LOG="$temporary_dir/capture.compositor"
export SEYRIX_SETTINGS_KEYBOARD_LOG="$temporary_dir/capture.keyboard"
export SEYRIX_SETTINGS_MOUSE_LOG="$temporary_dir/capture.mouse"
export SEYRIX_SETTINGS_PAGE_BEFORE_FILE="$temporary_dir/capture.page-before"
export SEYRIX_SETTINGS_TRACE_LINES_BEFORE_FILE="$temporary_dir/capture.trace-lines-before"
export SEYRIX_SETTINGS_SURFACE_BEFORE_FILE="$temporary_dir/capture.surface-before"
export SEYRIX_SETTINGS_SCENE_BEFORE_FILE="$temporary_dir/capture.scene-before"
export SEYRIX_SETTINGS_PRESENT_BEFORE_FILE="$temporary_dir/capture.present-before"
export SEYRIX_SETTINGS_TRACE_PID_FILE="$temporary_dir/capture.pid"
source "$script_dir/settings-render-trace-guest.sh"
capture_trace >"$temporary_dir/capture.output"
assert_true "production capture preserves the complete client trace" \
  grep -Fq 'wl_surface#16.commit()' "$temporary_dir/capture.output"
assert_true "production capture preserves compositor presentation records" \
  grep -Fq 'event=output_present monotonic_ns=160 presented=1 pid=776' "$temporary_dir/capture.output"
assert_true "production capture emits the complete compositor log before return" \
  grep -Fq 'SETTINGS_COMPOSITOR_LOG_COMPLETE_END' "$temporary_dir/capture.output"
assert_true "production capture verifies the real page-selection sequence" \
  grep -Fq 'SETTINGS_PAGE_SELECTION_VERIFIED=passed' "$temporary_dir/capture.output"
echo "PASS: production capture preserves client and compositor logs before shutdown"

python3 "$script_dir/settings-render-pixel-regression.py"
python3 "$script_dir/settings-socket-path-replacement-test.py"
assert_true "GTK render diagnostics compile only with QEMU acceptance hooks" \
  grep -Fq 'settings_build_args+=("-Dc_args=-DSEYRIX_SETTINGS_RENDER_DIAGNOSTICS=1")' \
    "$repo_root/build/build-phase2d3b-iso.sh"
assert_true "the packaged Settings desktop entry uses the production launcher" \
  grep -Fxq 'Exec=seyrix-settings-launch' <(tr -d '\r' < "$repo_root/desktop/settings/data/seyrix-settings.desktop") && \
  grep -Fxq 'TryExec=seyrix-settings-launch' <(tr -d '\r' < "$repo_root/desktop/settings/data/seyrix-settings.desktop")
assert_true "render diagnostics set WAYLAND_DEBUG before starting the production session manager" \
  grep -Fq 'if {$settings_render_diagnostic || $settings_ipc_only}' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'WAYLAND_DEBUG=client $compositor_launch_command' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'source=client-stderr pid=$app_pid' "$script_dir/qemu-graphics.exp"
assert_true "IPC-only recovery keeps Wayland protocol evidence separate from compositor IPC logs" \
  grep -Fq 'const char *ipc_trace = getenv("SEYRIX_SETTINGS_IPC_TRACE")' "$repo_root/desktop/session-supervisor/main.c" && \
  grep -Fq 'supervisor->trace_line[0] == '\''['\''' "$repo_root/desktop/session-supervisor/main.c" && \
  grep -Fq 'wayland_trace_stream' "$repo_root/desktop/session-supervisor/main.c" && \
  grep -Fq '/run/user/1000/seyrix-settings-wayland-trace.log' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'export WAYLAND_DEBUG=client' "$script_dir/qemu-graphics.exp"
assert_true "diagnostic launcher requests a normal open from the production supervisor" \
  grep -Fq 'seyrix-session-supervisor", "--open' "$script_dir/settings-render-trace-launcher.py"
if grep -Eq 'subprocess\.Popen|/usr/bin/seyrix-settings' "$script_dir/settings-render-trace-launcher.py"; then
  fail "diagnostic launcher must not spawn Settings outside the production manager"
fi
assert_true "production Settings source keeps render hooks behind the test-only define" \
  grep -Fq '#if defined(SEYRIX_SETTINGS_RENDER_DIAGNOSTICS)' \
    "$repo_root/desktop/settings/src/settings-window.c"
assert_true "GTK trace polls mapped page allocation after stack selection" \
  grep -Fq 'gtk_page_bounds' "$repo_root/desktop/settings/src/settings-window.c"
assert_true "full navigation selects compact mode from Settings window width" \
  grep -Fq 'if {$settings_window_width < 760}' "$script_dir/qemu-graphics.exp"
assert_true "targeted trace uses live Settings window width for compact input" \
  grep -Fq 'if {$trace_window_width < 760}' "$script_dir/qemu-graphics.exp"
assert_true "keyboard page comparison starts only after Appearance pixels are restored" \
  grep -Fq 'SETTINGS_RESTORE_APPEARANCE_PIXELS=passed' "$script_dir/qemu-graphics.exp"
assert_true "full category navigation waits for stable live Settings geometry" \
  grep -Fq 'PHASE2D3B_STABLE_SETTINGS_GEOMETRY' "$script_dir/qemu-graphics.exp"
assert_true "targeted mouse navigation uses allocated visible sidebar row bounds" \
  grep -Fq 'event=gtk_sidebar_row_bounds page=desktop' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_DIAGNOSTIC_SIDEBAR_BOUNDS=' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'round($trace_window_y + [lindex $sidebar_bounds 2]' "$script_dir/qemu-graphics.exp"
assert_true "render trace records the existing manager-owned Settings PID" \
  grep -Fq '"$exe" == /usr/bin/seyrix-session-supervisor' "$script_dir/qemu-graphics.exp" && \
  grep -Fq '"$parent" == "$manager"' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'source=client-stderr pid=$app_pid' "$script_dir/qemu-graphics.exp"
assert_true "render trace selects visible Settings result through the production launcher" \
  grep -Fq 'qmp_mouse_click_pixel $qmp $launcher_button_x $launcher_button_y $output_width $output_height' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'settings-render-launcher-menu.ppm' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'launcher_card_top + 110 + 2 * 58 + 26' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'qmp_mouse_click_pixel $qmp $settings_entry_x $settings_entry_y $output_width $output_height' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'int y = top + 110 + (int)index * 58' "$repo_root/desktop/shell/src/shell-ui.c" && \
  grep -Fq 'select visible Settings result in the production launcher and verify its manager-owned process' "$script_dir/qemu-graphics.exp"
assert_true "QMP click waits for bounded compositor pointer settling before button press" \
  awk '
    /^proc qmp_mouse_click \{channel\} \{/ { in_proc = 1; next }
    in_proc && /^}/ { exit }
    in_proc && /after 100/ && !settle { settle = NR }
    in_proc && /down.*true/ { press = NR }
    END { exit !(settle && press && settle < press) }
  ' "$script_dir/qemu-graphics.exp"
assert_true "launcher result click is gated on a newly mapped launcher surface" \
  python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import sys

text = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
needles = [
    "SETTINGS_RENDER_LAUNCHER_MAP_BASELINE=ready",
    "qmp_mouse_click_pixel $qmp $launcher_button_x $launcher_button_y $output_width $output_height",
    "settings-render-launcher-menu.ppm",
    "SETTINGS_RENDER_LAUNCHER_VISIBLE=passed",
    "qmp_mouse_click_pixel $qmp $settings_entry_x $settings_entry_y $output_width $output_height",
]
positions = [text.find(needle) for needle in needles]
if any(position < 0 for position in positions) or positions != sorted(positions):
    raise SystemExit(1)
PY
prior_pid_command="$(sed -n '/set render_trace_prior_pid_command/{s/^[[:space:]]*set render_trace_prior_pid_command {//; s/}$/;/; p;}' "$script_dir/qemu-graphics.exp")"
assert_true "existing Settings process must be a single mapped production-manager child" \
  grep -Fq 'count=$(pgrep -u 1000 -x seyrix-settings | wc -l)' <<< "$prior_pid_command" && \
  grep -Fq '"$count" == 1' <<< "$prior_pid_command" && \
  grep -Fq 'WAYLAND_TRACE_PRIOR_PID=failed' <<< "$prior_pid_command"
assert_true "render trace requires manager-injected trace environment and captured stderr" \
  grep -Fq '"$trace_env" == 1' "$script_dir/qemu-graphics.exp" && \
  grep -Fq '"$debug_env" == client' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'source=client-stderr pid=$app_pid' "$script_dir/qemu-graphics.exp"
assert_true "targeted QEMU diagnostic runs a real control-shell command after traced app exit" \
  grep -Fq 'GUEST_CONTROL_SHELL_FOLLOWUP=passed' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'verify the persistent guest control shell accepts a command after traced Settings exit' \
    "$script_dir/qemu-graphics.exp"
assert_true "targeted QEMU mouse transition records acknowledged QMP edges and a fresh compositor pointer event" \
  grep -Fq 'click_press_reply' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'click_release_reply' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_QMP_BUTTON_CYCLE=passed press=acknowledged release=acknowledged' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_MOUSE_COMPOSITOR_POINTER=%s' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'gtk_category_selector_changed page=desktop' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'gtk_page_unmap page=appearance' "$script_dir/qemu-graphics.exp"
assert_true "compact keyboard navigation identifies the focused dropdown child and verifies its next stack page" \
  grep -Fq 'focus_widget=GtkToggleButton' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'qmp_key_chord $qmp {} spc' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'qmp_key_chord $qmp {} home' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'qmp_key_chord $qmp {} down' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'qmp_key_chord $qmp {} ret' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'gtk_category_selector_changed page=windows' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'gtk_page_selection page=windows selected=windows' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'gtk_page_bounds page=windows' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'gtk_page_unmap page=desktop' "$script_dir/qemu-graphics.exp"
assert_true "targeted transitions require the outgoing GTK page to unmap" \
  grep -Fq 'gtk_page_unmap page=appearance' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'gtk_page_unmap page=desktop' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_MOUSE_OUTGOING_PAGE=%s' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_KEYBOARD_SETUP_MAPPED_PAGE=%s' "$script_dir/qemu-graphics.exp"
assert_true "traced process must have a distinct PID and mapped window" \
  grep -Fq '"$app_pid" == "$prior_pid"' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'Window mapped: pid=$app_pid ' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'WAYLAND_TRACE_APP=failed' "$script_dir/qemu-graphics.exp"
echo "PASS: Settings render trace uses a single production-manager child and captured client stderr"
assert_true "pointer trace resolves child widget to its category row" \
  grep -Fq 'for (GtkWidget *ancestor = widget' "$repo_root/desktop/settings/src/settings-window.c"
assert_true "single-click navigation requires the picked GTK row and row-selected signal" \
  grep -Fq 'target_category=desktop' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'event=gtk_sidebar_row_selected page=desktop ' "$script_dir/qemu-graphics.exp"
assert_true "keyboard navigation compares stack events against a fresh baseline" \
  grep -Fq '/tmp/seyrix-settings-keyboard-trace-baseline' "$script_dir/qemu-graphics.exp"
assert_true "supervised compositor-loss recovery requires new PID, ready Wayland display, restored state, and fresh IPC" \
  grep -Fq 'run_serial_check $target_recovery_check_command {SETTINGS_SUPERVISOR_RECOVERY=passed}' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SEYRIX_SETTINGS_SUPERVISOR event=wayland-ready ' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'state_restored' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'snapshot_revision' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'subscribe_revision' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'SETTINGS_RECOVERY_HARNESS_APP_LAUNCHES=0' "$script_dir/qemu-guest-helper.sh"
assert_true "compositor loss and process kill dispatch the expected production recovery mode" \
  grep -Fq 'settings-recovery-kill-compositor' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'settings-recovery-verify compositor' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'settings-recovery-verify process' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'settings-recovery-verify) shift; settings_recovery_verify "${1:-compositor}" ;;' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'event=launch pid=' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'old_start_now' "$script_dir/qemu-guest-helper.sh"
assert_true "bounded production retry campaign kills six children, verifies limit, and accepts a fresh OPEN" \
  grep -Fq 'settings-recovery-exhaust-budget' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'settings-recovery-budget-fresh-open-verify' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'for index in $(seq 1 6)' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'event=restart-limit ' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'field "$limit_marker" attempts || true)" == 6' "$script_dir/qemu-guest-helper.sh"
assert_true "a deliberate Settings close remains closed and explicit OPEN reopens through the desktop launcher" \
  grep -Fq 'settings-user-close-verify' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'event=user-close pid=$before_pid exit_status=0' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'process_count="$(pgrep -u 1000 -x seyrix-settings | wc -l || true)"' "$script_dir/qemu-guest-helper.sh" && \
  grep -Fq 'settings-recovery-budget-fresh-open-verify' "$script_dir/qemu-graphics.exp"
assert_true "explicit Settings reopen baseline uses a real record separator" \
  grep -Fq "printf '%s %s\\n' \"\$old_pid\" \"\$manager\"" "$script_dir/qemu-graphics.exp"
assert_true "IPC-only final cleanup sends compositor SIGTERM before checking endpoint removal" \
  grep -Fq 'target_final_compositor_stop_command {bash /tmp/seyrix-qemu-guest-helper.sh terminate-compositor}' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'PHASE2D2_COMPOSITOR_SIGTERM_CLEANUP=passed' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_IPC_FINAL_CLEANUP=passed' "$script_dir/qemu-graphics.exp"
assert_true "Settings state baseline waits long enough for bounded consistency polling and reports its result" python3 - "$script_dir/qemu-graphics.exp" "$script_dir/qemu-guest-helper.sh" <<'PY'
import pathlib
import re
import subprocess
import sys
import tempfile

trace, helper = (pathlib.Path(path).read_text() for path in sys.argv[1:])
for name in ("target_recovery_baseline_command", "target_process_kill_baseline_command", "target_user_close_baseline_command"):
    line = next(line for line in trace.splitlines() if f"run_serial_check ${name} " in line)
    assert re.search(r'\s45\s*$', line), f"{name} lacks the 45s helper budget"
baseline = helper.split("settings_recovery_baseline() {", 1)[1].split("\n}", 1)[0]
assert "for _ in $(seq 1 40); do" in baseline
assert "sleep 0.25" in baseline
assert "echo SETTINGS_SUPERVISOR_BASELINE=passed" in baseline
assert "sed -n '/^\\[window\\][[:space:]]*$/,/^\\[/ { /^\\[/d; p; }' \"$state_file\"" in baseline
assert "sed '$d'" not in baseline, "window parsing must preserve its final key"
extract_window = r'''sed -n '/^\[window\][[:space:]]*$/,/^\[/ { /^\[/d; p; }' "$1"'''
samples = (
    "[window]\ncategory=4\nwidth=640\nheight=480\npaned-position=160\nmaximized=false\n",
    "[window]\ncategory=4\nwidth=640\nheight=480\npaned-position=160\nmaximized=false\n[ui]\ntheme=dark\n",
)
for sample in samples:
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8") as state:
        state.write(sample)
        state.flush()
        parsed = subprocess.run(["bash", "-c", extract_window, "window-section-test", state.name], text=True, capture_output=True, check=True).stdout
    assert parsed.splitlines() == ["category=4", "width=640", "height=480", "paned-position=160", "maximized=false"], parsed
print("SETTINGS_RECOVERY_BASELINE_BUDGET=PASS helper_poll_max_s=10 budget_s=45")
PY
assert_true "Settings recovery baseline failure prints bounded state evidence and aborts before compositor termination" python3 - "$script_dir/qemu-graphics.exp" "$script_dir/qemu-guest-helper.sh" <<'PY'
import pathlib
import sys

trace, helper = (pathlib.Path(path).read_text() for path in sys.argv[1:])
baseline = helper.split("settings_recovery_baseline() {", 1)[1].split("\n}", 1)[0]
diagnostic = baseline.split("SETTINGS_SUPERVISOR_BASELINE_STATE_DIAGNOSTIC", 1)[1].split('fail "settings-state-window-fields-invalid"', 1)[0]
for field in (
    "category=%s", "width=%s", "height=%s", "maximized=%s", "paned_position=%s",
    "saved_category=%s", "state_evidence=%s", "state_saved_marker_category=%s",
    "state_saved_marker_width=%s", "state_saved_marker_height=%s", "state_saved_marker_maximized=%s",
    "state_saved_marker_paned_position=%s", "selected_marker_category=%s", "selected_marker_width=%s",
    "selected_marker_height=%s", "selected_marker_maximized=%s", "selected_marker_paned_position=%s",
):
    assert field in diagnostic, f"missing bounded baseline diagnostic field {field}"
assert "compositor_log" not in diagnostic, "diagnostic must not dump the full compositor log"
baseline_call = trace.index("run_serial_check $target_recovery_baseline_command")
guard = trace.index('if {$last_serial_check_result ne "pass"} { exit 1 }', baseline_call)
crash_call = trace.index("run_serial_check $target_recovery_crash_command", baseline_call)
assert baseline_call < guard < crash_call, "baseline failure must abort before compositor termination"
for name in (
    "target_recovery_baseline_command", "target_recovery_crash_command", "target_recovery_check_command",
    "target_process_kill_baseline_command", "target_process_recovery_command", "target_user_close_baseline_command",
    "target_user_close_command", "settings_retry_budget_command", "retry_budget_fresh_open_command",
):
    line = next(line for line in trace.splitlines() if "run_serial_" in line and f"${name}" in line)
    assert "QEMU_GUEST_HELPER=failed reason=[^\\r\\n]*" in line, f"{name} does not match generic helper failure"
print("SETTINGS_RECOVERY_HELPER_FAILURE_HANDLING=PASS bounded_diagnostics=PASS baseline_abort=PASS helper_failure_patterns=PASS")
PY
assert_true "Settings recovery reads lifecycle evidence incrementally through the production readiness bound" python3 - "$script_dir/qemu-graphics.exp" "$script_dir/qemu-guest-helper.sh" <<'PY'
import pathlib
import sys

trace, helper = (pathlib.Path(path).read_text() for path in sys.argv[1:])
verify = helper.split("settings_recovery_verify() {", 1)[1].split("\nsettings_recovery_exhaust_budget() {", 1)[0]
assert 'exec {trace_fd}< <(tail -n +$((log_baseline + 1)) -F "$compositor_log")' in verify
assert 'read -r -t 0.5 -u "$trace_fd" stream_line' in verify
assert 'deadline=$((SECONDS + 150))' in verify and 'while (( SECONDS < deadline )); do' in verify
assert 'fresh="$(tail -n +$((log_baseline + 1))' not in verify, "do not rescan the full growing compositor log"
assert 'kill "$trace_pid"' in verify and 'wait "$trace_pid"' in verify and 'exec {trace_fd}<&-' in verify
for event in (
    'SEYRIX_SETTINGS_SUPERVISOR event=', 'SEYRIX_SETTINGS event=',
    'Settings endpoint frame peer_pid=', 'Settings endpoint sent peer_pid=', 'Window mapped: pid=',
):
    assert event in verify, f"incremental stream omits required evidence {event}"
for criterion in (
    '"$pid" != "$before_pid"', '"$count" == 1', '"$parent" == "$manager"',
    '"$new_category" == "$before_category"', '"$state_restored" =~ ^(1|true|yes)$',
    '"$snapshot_status" == 0', '"$snapshot_to" == 2',
    '"$subscribe_status" == 0', '"$subscribe_to" == 3',
):
    assert criterion in verify, f"incremental wait removed acceptance predicate {criterion}"
for description in (
    "verify production manager readiness-gated process replacement, saved state, singleton, authenticated IPC and fresh subscriptions",
    "verify recovery from direct Settings process termination and fresh IPC",
):
    line = next(line for line in trace.splitlines() if description in line)
    assert line.rstrip().endswith("200"), f"{description} lacks the 200s stage bound"
assert "SETTINGS_SUPERVISOR_RECOVERY_WAIT mode=%s result=timeout" in verify
print("SETTINGS_RECOVERY_INCREMENTAL_WAIT=PASS bounded_s=150 stage_budget_s=200 predicates=preserved")
for marker in (
    "SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_STATE", "SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_MARKERS",
    "SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_PROCESS_TREE_BEGIN", "SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_SOCKETS_BEGIN",
    "SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_EVENTS_BEGIN",
):
    assert marker in verify, f"timeout diagnostics omit {marker}"
for diagnostic in (
    "category=%s width=%s height=%s maximized=%s paned_position=%s",
    "display_lost=%s supervisor_exit=%s attempt=%s wayland_ready=%s launch=%s app_start=%s",
    "accepted=%s hello=%s snapshot_request=%s snapshot_response=%s subscribe_request=%s subscribe_response=%s mapped=%s",
    'ps -o pid,ppid,sid,pgid,uid,stat,comm,args', '"$runtime_dir"/wayland-*',
    '"$runtime_dir"/seyrix/settings-v1.sock', 'tail -n 30 <<<"$fresh"',
):
    assert diagnostic in verify, f"timeout diagnostic missing {diagnostic}"
print("SETTINGS_RECOVERY_TIMEOUT_DIAGNOSTICS=PASS markers=13 process_tree=sockets=wayland/settings events=max30")
PY
assert_true "same-UID wrong-session launch control is denied with unchanged Settings PID and manager" \
	grep -Fq 'setsid --wait /bin/bash -c' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_SUPERVISOR_WRONG_SID=passed' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'before_pid' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'manager_start_after' "$script_dir/qemu-graphics.exp"
assert_true "server IPC socket descriptors return to their exact compositor-session baseline after ten authenticated cycles" \
  grep -Fq -- "-lname 'socket:*'" "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'Seyrix IPC FD Stability' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_ENDPOINT_FD_CYCLE=%s probe=%s baseline=%s after=%s stable_samples=%s' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_ENDPOINT_FD_STABILITY=passed cycles=10' "$script_dir/qemu-graphics.exp" && \
  grep -Fq '"$probe_sid" != "$compositor_sid"' "$script_dir/qemu-graphics.exp"
assert_true "full GTK matrix covers six categories with mouse or keyboard, mapped page identity, and rendered pixels" \
  grep -Fq 'foreach {category_index page screenshot}' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'appearance Appearance desktop {Desktop & Dock} windows {Window Management} displays Displays input Input about {About Seyrix}' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_GTK_PAGE_EVIDENCE=passed' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'SETTINGS_RENDER_PIXEL_COMPARISON' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'page_redraw_after_allocation' "$script_dir/qemu-graphics.exp"
assert_true "four requested resolution profiles are 640x480, 800x600, 1280x720, and 1920x1080" \
  grep -Fq '640x480 - 800x600 - 1280x720 - 1920x1080' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'set output_mode_command' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'set output_width [lindex [split $requested_resolution x] 0]' "$script_dir/qemu-graphics.exp" && \
  grep -Fq 'set settings_window_width' "$script_dir/qemu-graphics.exp"
assert_true "trace capture reads manager-owned compositor log and requires supervisor parent identity" \
  grep -Fq '/tmp/seyrix-compositor.log' "$script_dir/settings-render-trace-guest.sh" && \
  grep -Fq 'APP_PARENT=%s MANAGER_EXE=%s' "$script_dir/settings-render-trace-guest.sh" && \
  grep -Fq 'source=client-stderr pid=$pid' "$script_dir/settings-render-trace-functions.sh"
if grep -Eq 'SETTINGS_IPC_SAME_PROCESS_RECOVERY|gdk_display_manager_open_display|same-process compositor recovery' \
  "$script_dir/qemu-graphics.exp" "$script_dir/qemu-guest-helper.sh" "$repo_root/desktop/settings/src/main.c"; then
  fail "stock GTK settings recovery must use supervised process relaunch"
fi
assert_true "explicit Settings reopen activates the selected launcher result before process checks" \
  python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import sys
text = pathlib.Path(sys.argv[1]).read_text()
start = text.index("for {set reopen_cycle 1}")
end = text.index("if {$reopen_cycle == 1}", start)
loop = text[start:end]
typed = loop.index('qmp_type_slow $qmp "Settings"')
entered = loop.index("qmp_key_chord $qmp {} ret")
checked = loop.index("run_serial_check $settings_launcher_command")
assert typed < entered < checked
PY

assert_true "Escape test waits for a newly mapped launcher before sending keyboard input" python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import sys
text = pathlib.Path(sys.argv[1]).read_text()
start = text.index("set launcher_escape_map_baseline_command")
end = text.index("phase2d2-launcher-escape-closed.ppm", start)
segment = text[start:end]
assert segment.index("qmp_key_chord $qmp {meta_l} spc") < segment.index("run_serial_check $launcher_escape_map_wait_command")
assert segment.index("run_serial_check $launcher_escape_map_wait_command") < segment.index("qmp_key_chord $qmp {} esc")
PY
assert_true "recovery IPC requires successful snapshot and subscription responses from the expected opcodes" python3 - "$repo_root/desktop/compositor/src/settings-control.c" "$script_dir/qemu-guest-helper.sh" <<'PY'
import pathlib
import sys
server, helper = (pathlib.Path(path).read_text() for path in sys.argv[1:])
assert "seyrix_settings_read_u16(payload)" in server
assert "seyrix_settings_read_u16(payload + 2)" in server
assert '"$snapshot_status" == 0 && "$snapshot_to" == 2' in helper
assert '"$subscribe_status" == 0 && "$subscribe_to" == 3' in helper
PY
assert_true "budget recovery fresh OPEN requires successful snapshot and subscription responses" python3 - "$script_dir/qemu-guest-helper.sh" <<'PY'
import pathlib
import sys
helper = pathlib.Path(sys.argv[1]).read_text()
start = helper.index("settings_recovery_budget_fresh_open_verify() {")
end = helper.index("\nsettings_recovery_kill_compositor() {", start)
fresh_open = helper[start:end]
assert '"$snapshot_status" == 0 &&' in fresh_open
assert '"$snapshot_to" == 2' in fresh_open
assert '"$subscribe_status" == 0 &&' in fresh_open
assert '"$subscribe_to" == 3' in fresh_open
PY
assert_true "production recovery evidence checks scheduled and elapsed retry backoff" python3 - "$repo_root/desktop/session-supervisor/main.c" "$script_dir/qemu-guest-helper.sh" <<'PY'
import pathlib
import sys
supervisor, helper = (pathlib.Path(path).read_text() for path in sys.argv[1:])
assert "event=retry-scheduled" in supervisor and "scheduled_ms=%llu retry_at_ms=%llu" in supervisor
assert "monotonic_ms=%llu" in supervisor
assert "expected_delay_ms=$((1000 << (index - 2)))" in helper
assert '"$elapsed_ms" -ge "$retry_delay_ms"' in helper
PY
assert_true "recovery requires session supervisor to use its own process group" python3 - "$script_dir/qemu-guest-helper.sh" <<'PY'
import pathlib
import sys
helper = pathlib.Path(sys.argv[1]).read_text()
assert '"$manager_pgid" == "$manager"' in helper
assert '"$manager_pgid" != "$compositor_pgid"' in helper
assert '"$before_manager_pgid" != "$before_compositor_pgid"' in helper
PY
assert_true "recovery evidence prints display-loss outcome without shifting state fields" python3 - "$script_dir/qemu-guest-helper.sh" <<'PY'
import pathlib
import sys
helper = pathlib.Path(sys.argv[1]).read_text()
line = next(line for line in helper.splitlines() if "SETTINGS_SUPERVISOR_RECOVERY mode=%s" in line)
assert "exit_signal=%s display_lost=%s category=%s/%s width=%s/%s" in line
PY
assert_true "render trace mapping wait uses bounded stage budget and preserves failure context" python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import subprocess
import sys
trace = pathlib.Path(sys.argv[1]).read_text()
prior = next(line for line in trace.splitlines() if "render_trace_prior_pid_command" in line)
launch = next(line for line in trace.splitlines() if "render_trace_launch_command" in line)
assert "seq 1 120" in prior and "seq 1 120" in launch
assert "Settings endpoint|Window mapped|Window focused" in prior
assert 'run_serial_check $render_trace_prior_pid_command {WAYLAND_TRACE_PRIOR_PID=ready} {WAYLAND_TRACE_PRIOR_PID=failed}' in trace
assert 'verify its manager-owned process" 40' in trace
prior_command = prior.split("set render_trace_prior_pid_command {", 1)[1].rsplit("}", 1)[0]
syntax = subprocess.run(["bash", "-n"], input=prior_command, text=True, capture_output=True)
assert syntax.returncode == 0, f"prior Settings PID command Bash syntax failed: {syntax.stderr}"
loop_start = prior_command.index("for n in $(seq 1 120); do")
loop_end = prior_command.index("done; pid=${pid", loop_start)
wait = prior_command[loop_start:loop_end]
assert 'settings_pids=$(pgrep -u 1000 -x seyrix-settings || true)' in wait
assert 'if [[ "$count" == 1 ]]' in wait
assert 'candidate_pid=$(head -n 1 <<< "$settings_pids")' in wait
assert 'SEYRIX_SETTINGS_SUPERVISOR event=launch pid=$candidate_pid ' in wait
assert 'Window mapped: pid=$candidate_pid ' in wait
assert wait.index('SEYRIX_SETTINGS_SUPERVISOR event=launch') < wait.index('Window mapped:') < wait.index('break')
after_wait = prior_command[loop_end:]
for token in (
    'parent=$(awk', 'exe=$(readlink -f', 'sid=$(ps -o sid=', 'manager_sid=$(ps -o sid=',
    'count=$(pgrep -u 1000 -x seyrix-settings | wc -l)', '"$parent" == "$manager"',
    '"$exe" == /usr/bin/seyrix-session-supervisor', '"$sid" == "$manager_sid"',
    '"$mapped" -eq 1', '-n "$launch_marker"', '-n "$mapped_marker"', '"$count" == 1',
    'WAYLAND_TRACE_PRIOR_PID=failed', 'grep -E',
):
    assert token in after_wait, f"prior Settings PID command lost final ownership/diagnostic check: {token}"
print("RENDER_TRACE_PRIOR_PID_WAIT=PASS bounded=120 ownership=checked-after-map singleton=required")
PY
assert_true "compact GTK category option click waits for QMP pointer motion before press" python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import sys
trace = pathlib.Path(sys.argv[1]).read_text()
move = trace.index("qmp_mouse_move $qmp $click_abs_x $click_abs_y")
press = trace.index("set click_press_reply [qmp_mouse_button $qmp left 1]", move)
assert trace[move:press].count("after 100") == 1
selector = next(line for line in trace.splitlines() if "set selector_x " in line)
assert "+ [lindex $selector_bounds 3] / 2" in selector
PY
assert_true "compact GTK category click requires a configured and committed popup before option input" python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import sys
trace = pathlib.Path(sys.argv[1]).read_text()
click = trace.index("qmp_mouse_click_pixel $qmp $trace_click_x $trace_click_y", trace.index("set selector_x "))
gate = trace.index("set popup_trace_command", click)
screenshot = trace.index("settings-render-category-popup.ppm", gate)
option = trace.index("set trace_click_x", screenshot)
assert click < gate < screenshot < option
for token in (r"get_popup\(new id xdg_popup[@#]", r"\.configure\(", r"\.ack_configure\(", r"\.attach\(wl_buffer", r"\.commit\(\)"):
    assert token in trace[gate:option], token
assert "popup_height / 4.0" in trace[option:]
assert "selector_bounds 1] + 80" not in trace[gate:]
PY
assert_true "Settings recovery pointer conversion initializes active profile dimensions before use" python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import sys
trace = pathlib.Path(sys.argv[1]).read_text()
init = trace.index('set requested_resolution ""')
click = trace.index("qmp_mouse_move $qmp $recovery_click_abs_x $recovery_click_abs_y", trace.index("set target_recovery_bounds_command"))
assert trace.index("set output_width 640", init) < click
assert trace.index("set output_height 480", init) < click
assert trace.index("set output_width [lindex [split $requested_resolution x] 0]", init) < click
assert trace.index("set output_height [lindex [split $requested_resolution x] 1]", init) < click
PY
assert_true "compact recovery verifies QMP click and waits for a configured, committed popup before navigation" python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import sys
trace = pathlib.Path(sys.argv[1]).read_text()
assert "if {$settings_render_diagnostic || $settings_ipc_only}" in trace
start = trace.index("set target_recovery_trace_baseline")
end = trace.index("set target_recovery_baseline_command", start)
recovery = trace[start:end]
move = recovery.index("qmp_mouse_move $qmp $recovery_click_abs_x $recovery_click_abs_y")
press = recovery.index("set recovery_click_press_reply [qmp_mouse_button $qmp left 1]", move)
release = recovery.index("set recovery_click_release_reply [qmp_mouse_button $qmp left 0]", press)
popup_command = recovery.index("set target_recovery_popup_command {", release)
popup = recovery.index("set target_recovery_popup [run_serial_capture", popup_command)
home = recovery.index("qmp_key_chord $qmp {} home", popup)
assert move < press < release < popup_command < popup < home
for token in ("xdg_popup", r"\.configure", r"\.ack_configure", r"\.attach", r"\.commit", "SETTINGS_RECOVERY_POPUP_READY=passed"):
    assert token in recovery[popup_command:popup], token
assert '"return"' in recovery[release:popup_command]
PY
assert_true "recovery category and popup checks stream appended trace lines instead of rescanning whole log" python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import subprocess
import sys
trace = pathlib.Path(sys.argv[1]).read_text()
start = trace.index("set target_recovery_trace_baseline")
end = trace.index("set target_recovery_baseline_command", start)
recovery = trace[start:end]
assert recovery.count('tail -n +$((baseline + 1)) -F "$trace"') == 2
assert recovery.count('read -r -t 0.25 -u "$trace_fd" line') == 2
category_start = recovery.index("set target_recovery_category_command")
category_end = recovery.index("run_serial_check $target_recovery_category_command", category_start)
category = recovery[category_start:category_end]
assert 'fresh=$(tail -n' not in category
assert 'for n in $(seq' not in category
assert 'event=gtk_page_selection page=input selected=input' in category
assert 'category=input' in category
assert 'exit 0' not in category
assert 'ready=1' in category and 'break' in category
assert category.index('ready=1') < category.index('kill "$trace_pid"') < category.index('echo SETTINGS_RECOVERY_SAVED_CATEGORY=passed')

def braced_values(name):
    marker = f"set {name} {{"
    values = []
    cursor = 0
    while True:
        marker_at = trace.find(marker, cursor)
        if marker_at < 0:
            return values
        start = marker_at + len(marker)
        depth = 1
        escaped = False
        value = []
        for offset, char in enumerate(trace[start:], start):
            if escaped:
                value.append(char)
                escaped = False
                continue
            if char == "\\":
                value.append(char)
                escaped = True
                continue
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    values.append("".join(value))
                    cursor = offset + 1
                    break
            value.append(char)
        else:
            raise AssertionError(f"unterminated Tcl braced value: {name}")

def braced_value(name):
    values = braced_values(name)
    assert values, f"missing Tcl braced value: {name}"
    return values[0]

for name in ("target_recovery_popup_command", "target_recovery_category_command"):
    result = subprocess.run(["bash", "-n"], input=braced_value(name), text=True, capture_output=True)
    assert result.returncode == 0, f"{name} Bash syntax failed: {result.stderr}"

mouse_commands = braced_values("mouse_transition_check_command")
assert len(mouse_commands) == 2, f"expected compact and wide mouse checks, got {len(mouse_commands)}"
for index, command in enumerate(mouse_commands):
    label = ("compact popup", "wide sidebar")[index]
    result = subprocess.run(["bash", "-n"], input=command, text=True, capture_output=True)
    assert result.returncode == 0, f"{label} mouse command Bash syntax failed: {result.stderr}"
    assert 'delta=$(tail -n +$((baseline + 1)) "$trace")' in command, f"{label} check lacks a fresh trace baseline"
    assert 'release=$(grep -F "pid=$pid event=gtk_input_button_release page=button-1 " <<< "$delta" | tail -n 1)' in command, f"{label} check lacks the post-baseline GTK release"
    assert 'raw_release=$(grep -F "SEYRIX_SETTINGS_RAW_INPUT pid=$pid type=button-release " <<< "$delta" | tail -n 1)' in command, f"{label} check lacks the positioned raw release"
    assert "SETTINGS_MOUSE_GTK_RELEASE=%s" in command, f"{label} check does not print release evidence"
    gate = command.split("if [[", 1)[1].split("]]; then", 1)[0]
    assert command.index("SETTINGS_MOUSE_GTK_RELEASE=%s") < command.index("if [["), f"{label} failure context must print before the gate"
    for evidence in (
        '"$release" == *page=button-1*',
        '"$release" == *bounds_valid=1*',
        '"$release" == *target_category=desktop*',
        '"$raw_release" == *positioned=1*',
        '"$raw_release" == *target_widget=GtkLabel*',
    ):
        assert evidence in gate, f"{label} transition PASS lacks {evidence}"
    decision = command.split("if [[", 1)[1].split("\nfi", 1)[0]
    assert "then\n  echo SETTINGS_MOUSE_TRANSITION=passed\nelse\n  echo SETTINGS_MOUSE_TRANSITION=failed" in decision, f"{label} PASS must be conditional on release evidence"
print("GTK_MOUSE_RELEASE_GATES=PASS embedded_bash_syntax=PASS")
PY
assert_true "Settings IPC diagnostics require successful snapshot and subscription response opcodes" python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import sys
trace = pathlib.Path(sys.argv[1]).read_text()
ipc = next(line for line in trace.splitlines() if "set settings_ipc_event_command " in line)
for token in ('"$snapshot_status" == 0', '"$snapshot_to" == 2', '"$subscribe_status" == 0', '"$subscribe_to" == 3'):
    assert token in ipc, token
PY
assert_true "popup protocol parser accepts mapped popup and rejects a missing surface commit" python3 - "$script_dir/qemu-graphics.exp" <<'PY'
import pathlib
import subprocess
import sys
import tempfile

trace = pathlib.Path(sys.argv[1]).read_text()
line = next(line for line in trace.splitlines() if "set popup_trace_command " in line)
command = line.split("{", 1)[1].rsplit("}", 1)[0]
command = command.replace("pid=$(pgrep -u 1000 -x seyrix-settings | head -n 1)", "pid=2187", 1)
command = command.replace("trace=/tmp/seyrix-compositor.log", "trace=TRACE_PATH", 1)
command = command.replace("baseline=$(cat /tmp/seyrix-settings-mouse-trace-baseline)", "baseline=0", 1)
command = command.replace("seq 1 60", "seq 1 1", 1)
sample = """SEYRIX_SETTINGS_TRACE source=client-stderr pid=2187 xdg_wm_base#70.get_xdg_surface(new id xdg_surface#90, wl_surface#91)
SEYRIX_SETTINGS_TRACE source=client-stderr pid=2187 xdg_surface#90.get_popup(new id xdg_popup#92, xdg_surface#80, xdg_positioner#93)
SEYRIX_SETTINGS_TRACE source=client-stderr pid=2187 xdg_popup#92.configure(0, 168, 180, 240)
SEYRIX_SETTINGS_TRACE source=client-stderr pid=2187 xdg_surface#90.ack_configure(42)
SEYRIX_SETTINGS_TRACE source=client-stderr pid=2187 wl_surface#91.attach(wl_buffer#94, 0, 0)
SEYRIX_SETTINGS_TRACE source=client-stderr pid=2187 wl_surface#91.commit()
"""
with tempfile.TemporaryDirectory(prefix="seyrix-popup-trace-") as directory:
    path = pathlib.Path(directory) / "trace.log"
    for committed, expected in ((True, "SETTINGS_DIAGNOSTIC_POPUP_READY=passed"), (False, "SETTINGS_DIAGNOSTIC_POPUP_READY=failed")):
        path.write_text(sample if committed else sample.replace("wl_surface#91.commit()\n", ""))
        result = subprocess.run(["bash", "-lc", command.replace("TRACE_PATH", str(path))], text=True, capture_output=True, check=True)
        assert expected in result.stdout, result.stdout
        if committed:
            assert "POPUP_GEOMETRY=0,168,180,240" in result.stdout, result.stdout
PY
assert_true "Settings IPC diagnostics trace both endpoints and map the unprivileged socket peer" python3 - "$repo_root" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
client = (root / "desktop/settings/src/settings-client.c").read_text()
compositor = (root / "desktop/compositor/src/settings-control.c").read_text()
supervisor = (root / "desktop/session-supervisor/main.c").read_text()
launch = (root / "tests/qemu-graphics.exp").read_text()
helper = (root / "tests/qemu-guest-helper.sh").read_text()
for token in ("SEYRIX_SETTINGS_IPC_TRACE", '"hello-begin"', '"serialized"',
              '"write-result"', '"read-result"', '"frame-decoded"',
              '"hello-response"', '"state-response"'):
    assert token in client, token
for token in ("settings_trace_monotonic_ns", "Settings endpoint accepted peer",
              "connection_id=%llu", "Settings endpoint dispatch",
              "Settings endpoint read", "Settings endpoint request-dispatch"):
    assert token in compositor, token
trace_emit = supervisor[supervisor.index("static void trace_emit_line"):supervisor.index("static void trace_finish")]
assert "write(output_fd, output + written, output_size - written)" in trace_emit
assert "fprintf(stream" not in trace_emit and "fwrite(" not in trace_emit
launcher_write = launch.index("set ipc_trace_launcher_command ")
launcher_check = launch.index("run_serial_check $ipc_trace_launcher_command", launcher_write)
short_launch = launch.index("set compositor_launch_command {bash /tmp/seyrix-ipc-launch}", launcher_check)
qmp_launch = launch.index("qmp_type $qmp $compositor_launch_command", short_launch)
assert launcher_write < launcher_check < short_launch < qmp_launch
for token in ("export SEYRIX_SETTINGS_IPC_TRACE=1", "export SEYRIX_SETTINGS_RENDER_TRACE=1",
              "exec start-seyrix"):
    assert token in launch, token
assert "export WAYLAND_DEBUG=client" in launch
for token in ("ss -xapne", "u_seq\\ ESTAB", "peer_inode", "/proc/\"$diagnostic_compositor\"/fd",
              "SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_IPC_CLIENT_TRACE_BEGIN",
              "SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_IPC_SERVER_TRACE_BEGIN"):
    assert token in helper, token
assert '*"Settings endpoint "*' in helper
assert '"$compositor_log" | grep -F "pid=$pid "' in helper
assert 'settings_wayland_trace_export "$pid"' in helper
assert "SETTINGS_IPC_SERVER_LOG_BEGIN" in helper
assert 'source=client-stderr pid=$pid ' in helper
assert 'diagnostic_compositor_stderr' in helper and 'diagnostic_manager_stderr' in helper
collector = (root / "tests/settings-wayland-trace-collect.py").read_text()
for token in ("SETTINGS_WAYLAND_TRACE_EXPORT_BEGIN", "hashlib.sha256(payload)",
              "wl_display", "wl_surface", "os.O_EXCL", "os.fsync"):
    assert token in collector, token
print("SETTINGS_IPC_DIAGNOSTIC_INSTRUMENTATION=PASS")
PY
echo "PASS: Settings recovery checks production manager, new process, readiness, state restore, singleton, IPC, bounded retries, and close behavior"
bash "$script_dir/settings-wayland-trace-collect-test.sh"
tclsh "$script_dir/settings-render-trace-tcl-completeness.tcl" \
  "$script_dir/qemu-graphics.exp" \
  "$script_dir/qemu-graphics-capture.tcl" \
  "$script_dir/settings-render-trace-serial-regression.exp"
expect "$script_dir/settings-render-trace-serial-regression.exp"

echo "SETTINGS_RENDER_TRACE_OFFLINE_REGRESSION=PASS"
