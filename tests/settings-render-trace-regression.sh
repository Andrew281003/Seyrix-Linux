#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "$script_dir/.." && pwd -P)"
fixture_dir="$script_dir/fixtures/settings-render-trace"
saved_trace="$repo_root/docs/phase2d3b4c-final-trace-evidence/settings-render.trace"
saved_log="$repo_root/docs/phase2d3b4c-final-trace-evidence/test.log"
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

test -r "$saved_trace" || fail "saved Phase 2D.3B.4c client trace is missing"
test -r "$saved_log" || fail "saved Phase 2D.3B.4c QEMU log is missing"

saved_trace_pid="$(settings_trace_initial_appearance_pid "$saved_trace")"
saved_metadata="$(grep -a -E 'SETTINGS_TRACE_APP_PID=[0-9]+ UID=' "$saved_log" | head -n 1 | tr -d '\r')"
saved_log_pid="$(printf '%s\n' "$saved_metadata" | sed -n -E 's/.*SETTINGS_TRACE_APP_PID=([0-9]+).*/\1/p')"
saved_uid="$(printf '%s\n' "$saved_metadata" | sed -n -E 's/.* UID=([0-9]+) .*/\1/p')"
saved_exe="$(printf '%s\n' "$saved_metadata" | sed -n -E 's/.* NATIVE_EXE=([^ ]+) .*/\1/p')"
saved_owner="$(printf '%s\n' "$saved_metadata" | sed -n -E 's/.* TRACE_OWNER=([^ ]+).*/\1/p')"
assert_eq 776 "$saved_trace_pid" "saved 4c trace GTK process parser"
assert_eq "$saved_trace_pid" "$saved_log_pid" "saved trace PID matches the captured Settings process"
assert_eq 1000 "$saved_uid" "saved Settings UID"
assert_eq /usr/bin/seyrix-settings "$saved_exe" "saved Settings executable"
assert_eq seyrix:seyrix "$saved_owner" "saved trace owner"
assert_true "saved log confirms traced environment values" grep -a -Fq 'SEYRIX_SETTINGS_RENDER_TRACE=1' "$saved_log"
assert_true "saved log confirms WAYLAND_DEBUG=client" grep -a -Fq 'WAYLAND_DEBUG=client' "$saved_log"
assert_true "saved log confirms XDG_RUNTIME_DIR" grep -a -Fq 'XDG_RUNTIME_DIR=/run/user/1000' "$saved_log"
assert_true "saved log confirms the same Wayland display" grep -a -Fq 'WAYLAND_DISPLAY=wayland-0' "$saved_log"
saved_ops="$(settings_trace_client_surface_event_count "$saved_trace" "$saved_trace_pid")"
saved_ids="$(settings_trace_client_surface_ids_from_file "$saved_trace" "$saved_trace_pid")"
assert_eq 26 "$saved_ops" "saved 4c surface operation count using production parser"
assert_eq 16 "$saved_ids" "saved 4c wl_surface object ID using production parser"
assert_true "saved GTK and proc evidence passes the production launch gate without protocol gating" \
  settings_trace_launch_gate "$saved_trace_pid" "$saved_log_pid" "$saved_uid" "$saved_exe" "$saved_owner" \
  appearance yes yes yes 1 client /run/user/1000 wayland-0 wayland-0
echo "PASS: saved 4c trace replay, PID scoping, environment, and launch gate"

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
tclsh "$script_dir/settings-render-trace-tcl-completeness.tcl" \
  "$script_dir/qemu-graphics.exp" \
  "$script_dir/qemu-graphics-capture.tcl" \
  "$script_dir/settings-render-trace-serial-regression.exp"
expect "$script_dir/settings-render-trace-serial-regression.exp"

echo "SETTINGS_RENDER_TRACE_OFFLINE_REGRESSION=PASS"
