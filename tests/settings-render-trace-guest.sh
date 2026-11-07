#!/usr/bin/env bash
set -uo pipefail

helper_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "$helper_dir/settings-render-trace-functions.sh"

trace=${SEYRIX_SETTINGS_TRACE_FILE:-/tmp/seyrix-compositor.log}
runtime=${SEYRIX_SETTINGS_RUNTIME_DIR:-/run/user/1000}
compositor=${SEYRIX_SETTINGS_COMPOSITOR_LOG:-/tmp/seyrix-compositor.log}
launcher=${SEYRIX_SETTINGS_TRACE_LAUNCHER:-/usr/bin/seyrix-session-supervisor}
desktop=${SEYRIX_SETTINGS_DESKTOP_ENTRY:-/usr/share/applications/seyrix-settings.desktop}
keyboard_log=${SEYRIX_SETTINGS_KEYBOARD_LOG:-/tmp/seyrix-settings-evtest-keyboard.log}
mouse_log=${SEYRIX_SETTINGS_MOUSE_LOG:-/tmp/seyrix-evtest-mouse.log}
launcher_log=${SEYRIX_SETTINGS_LAUNCHER_LOG:-/tmp/seyrix-settings-trace-launcher.log}
page_before_file=${SEYRIX_SETTINGS_PAGE_BEFORE_FILE:-/tmp/seyrix-settings-page-before}
trace_lines_before_file=${SEYRIX_SETTINGS_TRACE_LINES_BEFORE_FILE:-/tmp/seyrix-settings-trace-lines-before}
surface_before_file=${SEYRIX_SETTINGS_SURFACE_BEFORE_FILE:-/tmp/seyrix-settings-surface-commits-before}
scene_before_file=${SEYRIX_SETTINGS_SCENE_BEFORE_FILE:-/tmp/seyrix-settings-scene-commits-before}
present_before_file=${SEYRIX_SETTINGS_PRESENT_BEFORE_FILE:-/tmp/seyrix-settings-output-presents-before}
trace_pid_file=${SEYRIX_SETTINGS_TRACE_PID_FILE:-/tmp/seyrix-settings-trace-pid}

launch_trace() {
  printf 'SETTINGS_DESKTOP_ENTRY_BEGIN\n'
  grep -E '^(Exec|TryExec)=' "$desktop" || true
  printf 'SETTINGS_DESKTOP_ENTRY_END\n'
  printf 'SETTINGS_TRACE_PATH=%s\n' "$trace"
  printf 'SETTINGS_TRACE_RUNTIME='
  stat -c 'owner=%U:%G mode=%a' "$runtime"
  if runuser -u seyrix -- test -w "$runtime"; then
    echo SETTINGS_TRACE_RUNTIME_WRITABLE=yes
  else
    echo SETTINGS_TRACE_RUNTIME_WRITABLE=no
  fi
  printf 'SETTINGS_TRACE_LAUNCHER='
  test -x "$launcher" && echo executable || echo missing
  printf 'SETTINGS_TRACE_BINARY='
  test -x /usr/bin/seyrix-settings && echo executable || echo missing

  terminal_pid=$(pgrep -u 1000 -x weston-terminal | head -n 1 || true)
  session_environment=$(test -n "$terminal_pid" && tr '\0' '\n' < "/proc/$terminal_pid/environ" 2>/dev/null || true)
  wayland_display=$(printf '%s\n' "$session_environment" | sed -n 's/^WAYLAND_DISPLAY=//p' | head -n 1)
  dbus_address=$(printf '%s\n' "$session_environment" | sed -n 's/^DBUS_SESSION_BUS_ADDRESS=//p' | head -n 1)
  test -n "$wayland_display" || wayland_display=wayland-0
  test -n "$dbus_address" || dbus_address=unix:path=/run/user/1000/bus
  printf 'SETTINGS_LAUNCH_SESSION_PID=%s WAYLAND_DISPLAY=%s DBUS_SESSION_BUS_ADDRESS=%s\n' "$terminal_pid" "$wayland_display" "$dbus_address"

  keyboard_event=$(awk -v RS='' '/QEMU.*Keyboard/ { if (match($0, /event[0-9]+/)) { print substr($0, RSTART, RLENGTH); exit } }' /proc/bus/input/devices)
  printf 'SETTINGS_KEYBOARD_EVENT=%s\n' "$keyboard_event"
  if test -n "$keyboard_event"; then
    nohup timeout 900s evtest "/dev/input/$keyboard_event" >"$keyboard_log" 2>&1 </dev/null &
    echo SETTINGS_KEYBOARD_MONITOR=started
  else
    echo SETTINGS_KEYBOARD_MONITOR=missing
  fi

  if [[ -n "$terminal_pid" && -S "$runtime/$wayland_display" && -x "$launcher" &&
      -x /usr/bin/seyrix-settings && -r "$compositor" ]] &&
      grep -Fxq 'Exec=seyrix-settings-launch' "$desktop" &&
      grep -Fxq 'TryExec=seyrix-settings-launch' "$desktop" &&
      pgrep -u 1000 -x seyrix-settings >/dev/null; then
    echo SETTINGS_WRAPPER_LAUNCH_REQUESTED=passed source=production-session-supervisor-existing-app
  else
    echo SETTINGS_WRAPPER_LAUNCH_REQUESTED=failed reason=production-managed-settings-not-active
  fi

  trace_pid=
  for n in $(seq 1 90); do
    active_pid=$(pgrep -u 1000 -x seyrix-settings | head -n 1 || true)
    trace_pid=$(settings_trace_initial_appearance_pid "$trace" "$active_pid" 2>/dev/null || true)
    mapped=$(grep -F "Window mapped: pid=$active_pid " "$compositor" | grep -c 'app_id=org.seyrix.settings' || true)
    if [[ -n "$trace_pid" && "$mapped" -gt 0 && -r "$trace" && -r "/proc/$trace_pid/environ" ]]; then
      break
    fi
    sleep 0.5
  done

  proc_pid=$(test -n "$trace_pid" && sed -n 's/^Pid:[[:space:]]*\([0-9]*\).*/\1/p' "/proc/$trace_pid/status" 2>/dev/null || true)
  uid=$(test -n "$trace_pid" && sed -n 's/^Uid:[[:space:]]*\([0-9]*\).*/\1/p' "/proc/$trace_pid/status" 2>/dev/null || true)
  native_exe=$(test -n "$trace_pid" && readlink -f "/proc/$trace_pid/exe" 2>/dev/null || true)
  manager_pid=$(test -r /run/user/1000/seyrix-settings-supervisor.pid && cat /run/user/1000/seyrix-settings-supervisor.pid || true)
  app_parent=$(test -n "$trace_pid" && sed -n 's/^PPid:[[:space:]]*\([0-9]*\).*/\1/p' "/proc/$trace_pid/status" 2>/dev/null || true)
  manager_exe=$(test -n "$manager_pid" && readlink -f "/proc/$manager_pid/exe" 2>/dev/null || true)
  compositor_pid=$(pgrep -u 1000 -f '^/usr/bin/seyrix-compositor$' | head -n 1 || true)
  settings_sid=$(test -n "$trace_pid" && ps -o sid= -p "$trace_pid" 2>/dev/null | tr -d ' ' || true)
  compositor_sid=$(test -n "$compositor_pid" && ps -o sid= -p "$compositor_pid" 2>/dev/null | tr -d ' ' || true)
  trace_environment=$(test -n "$trace_pid" && tr '\0' '\n' < "/proc/$trace_pid/environ" 2>/dev/null || true)
  trace_owner=$(stat -c '%U:%G' "$trace" 2>/dev/null || true)
  page_before=empty
  if [[ -n "$trace_pid" ]] && settings_trace_gtk_lines_for_pid "$trace" "$trace_pid" | grep -Eq 'event=gtk_page_map page=appearance selected=appearance([[:space:]]|$)'; then
    page_before=appearance
  fi
  trace_readable=no
  launcher_executable=no
  binary_executable=no
  [[ -r "$trace" ]] && trace_readable=yes
  [[ -x "$launcher" && "$manager_exe" == /usr/bin/seyrix-session-supervisor && "$app_parent" == "$manager_pid" ]] && launcher_executable=yes
  [[ -x /usr/bin/seyrix-settings ]] && binary_executable=yes
  trace_env_settings=$(printf '%s\n' "$trace_environment" | sed -n 's/^SEYRIX_SETTINGS_RENDER_TRACE=//p' | head -n 1)
  trace_env_wayland=$(printf '%s\n' "$trace_environment" | sed -n 's/^WAYLAND_DEBUG=//p' | head -n 1)
  trace_env_runtime=$(printf '%s\n' "$trace_environment" | sed -n 's/^XDG_RUNTIME_DIR=//p' | head -n 1)
  trace_env_display=$(printf '%s\n' "$trace_environment" | sed -n 's/^WAYLAND_DISPLAY=//p' | head -n 1)
  wayland_socket=no
  if [[ -n "$wayland_display" && -S "$runtime/$wayland_display" ]]; then
    wayland_socket="$wayland_display"
  fi
  client_events=$(settings_trace_client_surface_event_count "$trace" "$trace_pid")

  printf 'SETTINGS_TRACE_APP_PID=%s CHECKED_PROC_PID=%s UID=%s NATIVE_EXE=%s TRACE_OWNER=%s MANAGER_PID=%s APP_PARENT=%s MANAGER_EXE=%s\n' "$trace_pid" "$proc_pid" "$uid" "$native_exe" "$trace_owner" "$manager_pid" "$app_parent" "$manager_exe"
  printf 'SETTINGS_TRACE_SESSION_PID=%s SETTINGS_TRACE_SESSION_ID=%s COMPOSITOR_PID=%s COMPOSITOR_SESSION_ID=%s\n' "$trace_pid" "$settings_sid" "$compositor_pid" "$compositor_sid"
  printf 'SETTINGS_ACTIVE_PAGE_BEFORE=%s\n' "$page_before"
  printf 'SETTINGS_TRACE_ENV SEYRIX_SETTINGS_RENDER_TRACE=%s WAYLAND_DEBUG=%s XDG_RUNTIME_DIR=%s WAYLAND_DISPLAY=%s\n' "$trace_env_settings" "$trace_env_wayland" "$trace_env_runtime" "$trace_env_display"
  printf '%s\n' "$trace_environment" | grep -E '^DBUS_SESSION_BUS_ADDRESS=' || true
  printf 'SETTINGS_CLIENT_WAYLAND_PROTOCOL_EVENT_COUNT=%s\n' "$client_events"

  trace_lines_before=0
  surface_commits_before=0
  scene_commits_before=0
  output_presents_before=0
  if test -r "$trace"; then
    trace_lines_before=$(wc -l < "$trace")
  fi
  if test -r "$compositor"; then
    surface_commits_before=$(grep "SETTINGS_RENDER_TRACE event=surface_commit .*pid=$trace_pid " "$compositor" | wc -l)
    scene_commits_before=$(grep "SETTINGS_RENDER_TRACE event=output_scene_commit .*pid=$trace_pid " "$compositor" | wc -l)
    output_presents_before=$(grep "SETTINGS_RENDER_TRACE event=output_present .*pid=$trace_pid " "$compositor" | wc -l)
  fi
  printf '%s\n' "$page_before" >"$page_before_file"
  printf '%s\n' "$trace_lines_before" >"$trace_lines_before_file"
  printf '%s\n' "$surface_commits_before" >"$surface_before_file"
  printf '%s\n' "$scene_commits_before" >"$scene_before_file"
  printf '%s\n' "$output_presents_before" >"$present_before_file"
  printf 'SETTINGS_TRACE_BASELINES trace_lines=%s surface_commits=%s scene_commits=%s output_presents=%s\n' "$trace_lines_before" "$surface_commits_before" "$scene_commits_before" "$output_presents_before"

  if settings_trace_same_session_gate "$settings_sid" "$compositor_sid"; then
    echo SETTINGS_TRACE_SESSION=passed
  else
    echo SETTINGS_TRACE_SESSION=failed
  fi

  endpoint_ss=
  for n in $(seq 1 60); do
    endpoint_ss=$(ss -xapn 2>/dev/null | grep -E '^[[:space:]]*u_seq[[:space:]]+ESTAB[[:space:]]' | grep -F "pid=$trace_pid," || true)
    [[ -n "$endpoint_ss" ]] && break
    sleep 0.25
  done
  printf 'SETTINGS_ENDPOINT_SOCKET_EVIDENCE_BEGIN\n%s\nSETTINGS_ENDPOINT_SOCKET_EVIDENCE_END\n' "$endpoint_ss"
  if settings_trace_endpoint_connection_gate "$endpoint_ss" "$trace_pid"; then
    echo SETTINGS_ENDPOINT_CONNECTION=passed
    echo 'STAGE_RESULT name={Settings read-only endpoint connection} result=PASS'
  elif [[ -n "$trace_pid" ]]; then
    echo SETTINGS_ENDPOINT_CONNECTION=failed reason=no-established-settings-seqpacket-socket
    echo 'STAGE_RESULT name={Settings read-only endpoint connection} result=FAIL'
  else
    echo SETTINGS_ENDPOINT_CONNECTION=INCOMPLETE reason=settings-pid-missing
    echo 'STAGE_RESULT name={Settings read-only endpoint connection} result=INCOMPLETE'
  fi

  if settings_trace_launch_gate "$trace_pid" "$proc_pid" "$uid" "$native_exe" "$trace_owner" "$page_before" \
      "$trace_readable" "$launcher_executable" "$binary_executable" "$trace_env_settings" \
      "$trace_env_wayland" "$trace_env_runtime" "$trace_env_display" "$wayland_socket" && \
      settings_trace_same_session_gate "$settings_sid" "$compositor_sid"; then
    printf '%s\n' "$trace_pid" >"$trace_pid_file"
    echo SETTINGS_RENDER_TRACE_LAUNCH=passed
    return 0
  fi
  echo SETTINGS_RENDER_TRACE_LAUNCH=failed
  return 1
}

capture_trace() {
  before=$(cat "$page_before_file" 2>/dev/null || true)
  settings_pid=$(cat "$trace_pid_file" 2>/dev/null || true)
  trace_before=$(cat "$trace_lines_before_file" 2>/dev/null || echo 0)
  surface_before=$(cat "$surface_before_file" 2>/dev/null || echo 0)
  scene_before=$(cat "$scene_before_file" 2>/dev/null || echo 0)
  present_before=$(cat "$present_before_file" 2>/dev/null || echo 0)
  gtk_events=$(settings_trace_gtk_lines_for_pid "$trace" "$settings_pid" 2>/dev/null || true)
  selected_event=$(printf '%s\n' "$gtk_events" | grep -E 'event=gtk_page_selection page=desktop selected=desktop([[:space:]]|$)' | tail -n 1 || true)
  selected_pid=$(printf '%s\n' "$selected_event" | sed -n -E 's/.*pid=([0-9]+) event=gtk_page_selection.*/\1/p')
  after=$(printf '%s\n' "$selected_event" | sed -n -E 's/.*event=gtk_page_selection page=([^ ]+) selected=.*/\1/p')
  page_map=$(printf '%s\n' "$gtk_events" | grep -E 'event=gtk_page_map page=desktop selected=desktop([[:space:]]|$)' | tail -n 1 || true)
  page_unmap=$(printf '%s\n' "$gtk_events" | grep -E 'event=gtk_page_unmap page=appearance selected=desktop([[:space:]]|$)' | tail -n 1 || true)

  printf 'SETTINGS_RENDER_TRACE_DUMP_BEGIN\n'
  printf 'SETTINGS_TRACE_PATH=%s\nSETTINGS_TRACE_APP_PID=%s\nSETTINGS_GTK_SELECTION_PID=%s\nSETTINGS_ACTIVE_PAGE_BEFORE=%s\nSETTINGS_ACTIVE_PAGE_AFTER=%s\n' "$trace" "$settings_pid" "$selected_pid" "$before" "$after"
  printf 'SETTINGS_INPUT_MOUSE_EVENTS_BEGIN\n'
  grep -E 'ABS_X|ABS_Y|BTN_LEFT' "$mouse_log" | tail -n 16 || true
  printf 'SETTINGS_INPUT_KEYBOARD_EVENTS_BEGIN\n'
  grep -E 'KEY_HOME|KEY_DOWN|KEY_ENTER' "$keyboard_log" | tail -n 16 || true
  printf 'SETTINGS_INPUT_EVENTS_END\n'

  mouse_x_events=$(grep -Ec 'Event: time .*code 0 \(ABS_X\), value ' "$mouse_log" 2>/dev/null || true)
  mouse_y_events=$(grep -Ec 'Event: time .*code 1 \(ABS_Y\), value ' "$mouse_log" 2>/dev/null || true)
  mouse_left_press=$(grep -Ec 'BTN_LEFT.*value 1' "$mouse_log" 2>/dev/null || true)
  mouse_left_release=$(grep -Ec 'BTN_LEFT.*value 0' "$mouse_log" 2>/dev/null || true)
  keyboard_sequence=passed
  for key in KEY_HOME KEY_DOWN KEY_ENTER; do
    if ! grep -q "$key.*value 1" "$keyboard_log" 2>/dev/null || ! grep -q "$key.*value 0" "$keyboard_log" 2>/dev/null; then
      keyboard_sequence=failed
    fi
  done
  printf 'SETTINGS_INPUT_VERIFICATION mouse_x=%s mouse_y=%s left_press=%s left_release=%s keyboard_HOME_DOWN_ENTER=%s\n' "$mouse_x_events" "$mouse_y_events" "$mouse_left_press" "$mouse_left_release" "$keyboard_sequence"

  if test -r "$trace"; then
    printf 'SETTINGS_CLIENT_TRACE_COMPLETE_BEGIN\n'
    cat "$trace"
    printf 'SETTINGS_CLIENT_TRACE_COMPLETE_END\n'
    printf 'SETTINGS_TRACE_STAT='
    stat -c 'owner=%U:%G mode=%a size=%s mtime=%y ctime=%z' "$trace"
  else
    echo SETTINGS_TRACE_FILE=missing
    echo 'STAGE_RESULT name={Complete GTK and Wayland client trace} result=INCOMPLETE'
  fi

  printf 'SETTINGS_GTK_PAGE_MAP_DESKTOP=%s\nSETTINGS_GTK_PAGE_UNMAP_APPEARANCE=%s\n' "$page_map" "$page_unmap"
  client_after=$(tail -n +$((trace_before + 1)) "$trace" 2>/dev/null | settings_trace_client_surface_ops "$settings_pid")
  client_surface_ids=$(printf '%s\n' "$client_after" | settings_trace_client_surface_ids_from_stream "$settings_pid")
  surface_after=$(grep "SETTINGS_RENDER_TRACE event=surface_commit .*pid=$settings_pid " "$compositor" | tail -n +$((surface_before + 1)) || true)
  scene_after=$(grep "SETTINGS_RENDER_TRACE event=output_scene_commit .*pid=$settings_pid " "$compositor" | tail -n +$((scene_before + 1)) || true)
  present_after=$(grep "SETTINGS_RENDER_TRACE event=output_present .*pid=$settings_pid " "$compositor" | tail -n +$((present_before + 1)) || true)

  surface_id=$(settings_trace_correlate_surface_id "$settings_pid" "$client_surface_ids" "$surface_after" || true)
  surface_after_for_id=
  if [[ -n "$surface_id" ]]; then
    surface_after_for_id=$(printf '%s\n' "$surface_after" | grep -F "wl_surface=$surface_id " || true)
  fi
  matching_surface_commit=
  surface_seq=
  scene_for_id=
  present_for_id=
  client_for_id=$(printf '%s\n' "$client_after" | grep -E "wl_surface[@#]$surface_id\\." | grep -E '\.(attach|damage|damage_buffer|commit)\(' || true)
  selection_ns=$(settings_trace_page_selection_timestamp_ns "$trace" "$settings_pid" || true)
  scene_after_for_id=$(printf '%s\n' "$scene_after" | grep -F "wl_surface=$surface_id " || true)
  present_after_for_id=$(printf '%s\n' "$present_after" | grep -F "wl_surface=$surface_id " || true)

  client_attach_count=$(printf '%s\n' "$client_for_id" | grep -c '.attach(' || true)
  client_damage_count=$(printf '%s\n' "$client_for_id" | grep -Ec '\.damage(_buffer)?\(' || true)
  client_commit_count=$(printf '%s\n' "$client_for_id" | grep -c '.commit(' || true)
  surface_attach_count=0
  surface_damage_count=0
  time_correlation_pass=failed
  time_correlation_evidence=
  while IFS= read -r candidate_surface_commit; do
    [[ "$candidate_surface_commit" == *"buffer_attached=1 "* ]] || continue
    [[ "$candidate_surface_commit" == *"surface_damage=1:"* ||
      "$candidate_surface_commit" == *"buffer_damage=1:"* ]] || continue
    candidate_surface_seq=$(printf '%s\n' "$candidate_surface_commit" |
      sed -n 's/.* seq=\([0-9][0-9]*\) committed=.*/\1/p')
    [[ "$candidate_surface_seq" =~ ^[0-9]+$ && -n "$selection_ns" ]] || continue
    if candidate_time_evidence=$(settings_trace_render_sequence_gate \
        "$client_for_id" "$candidate_surface_commit" "$scene_after_for_id" "$present_after_for_id" \
        "$settings_pid" "$surface_id" "$candidate_surface_seq" "$selection_ns"); then
      matching_surface_commit=$candidate_surface_commit
      surface_seq=$candidate_surface_seq
      scene_for_id=$(printf '%s\n' "$scene_after_for_id" |
        grep -E "surface_seq=$surface_seq([[:space:]]|$)" || true)
      present_for_id=$(printf '%s\n' "$present_after_for_id" |
        grep -E "surface_seq=$surface_seq([[:space:]]|$)" || true)
      time_correlation_evidence=$candidate_time_evidence
      time_correlation_pass=passed
      break
    fi
  done <<< "$surface_after_for_id"
  if [[ -n "$matching_surface_commit" ]]; then
    surface_attach_count=1
    surface_damage_count=1
  fi
  scene_commit_count=$(printf '%s\n' "$scene_for_id" | grep -c 'result=success' || true)
  output_present_count=$(printf '%s\n' "$present_for_id" | grep -c 'presented=1' || true)
  printf '%s\n' "$time_correlation_evidence"
  if [[ "$time_correlation_pass" != passed ]]; then
    echo SETTINGS_TRACE_TIME_CORRELATION=failed
  fi

  printf 'SETTINGS_CLIENT_WL_SURFACE_OBJECT_IDS=%s\nSETTINGS_CORRELATED_WL_SURFACE_ID=%s SURFACE_SEQ=%s SETTINGS_TRACE_PID=%s\n' "$client_surface_ids" "$surface_id" "$surface_seq" "$settings_pid"
  printf 'SETTINGS_CLIENT_SURFACE_COUNTS attach=%s damage=%s commit=%s\n' "$client_attach_count" "$client_damage_count" "$client_commit_count"
  printf 'SETTINGS_WLROOTS_SURFACE_COUNTS attached=%s damage=%s scene_commit=%s presented=%s\n' "$surface_attach_count" "$surface_damage_count" "$scene_commit_count" "$output_present_count"
  printf 'SETTINGS_WLROOTS_ALL_SURFACE_EVENTS_BEGIN\n%s\nSETTINGS_WLROOTS_ALL_SURFACE_EVENTS_END\n' "$surface_after"
  printf 'SETTINGS_WLROOTS_ALL_SCENE_COMMIT_EVENTS_BEGIN\n%s\nSETTINGS_WLROOTS_ALL_SCENE_COMMIT_EVENTS_END\n' "$scene_after"
  printf 'SETTINGS_WLROOTS_ALL_PRESENT_EVENTS_BEGIN\n%s\nSETTINGS_WLROOTS_ALL_PRESENT_EVENTS_END\n' "$present_after"
  printf 'SETTINGS_WLROOTS_SCENE_COMMIT_EVENTS_BEGIN\n%s\n' "$scene_for_id"
  printf 'SETTINGS_WLROOTS_PRESENT_EVENTS_BEGIN\n%s\n' "$present_for_id"

  if test -r "$compositor"; then
    printf 'SETTINGS_COMPOSITOR_LOG_COMPLETE_BEGIN\n'
    cat "$compositor"
    printf 'SETTINGS_COMPOSITOR_LOG_COMPLETE_END\n'
  else
    echo SETTINGS_COMPOSITOR_LOG_COMPLETE=missing
  fi

  input_pass=failed
  if settings_trace_input_gate "$mouse_x_events" "$mouse_y_events" "$mouse_left_press" "$mouse_left_release" "$keyboard_sequence"; then
    input_pass=passed
  fi
  gtk_pass=failed
  if [[ "$before" == appearance && "$after" == desktop && -n "$page_map" && -n "$page_unmap" && "$selected_pid" == "$settings_pid" ]] && settings_trace_gtk_transition_gate "$trace" "$settings_pid"; then
    gtk_pass=passed
  fi
  if [[ "$input_pass" == passed ]]; then
    echo SETTINGS_PAGE_INPUT_SEQUENCE=verified
    echo 'STAGE_RESULT name={Settings input verification} result=PASS'
  else
    echo SETTINGS_PAGE_INPUT_SEQUENCE=NOT_VERIFIED
    echo 'STAGE_RESULT name={Settings input verification} result=INCOMPLETE'
  fi
  printf 'SETTINGS_INPUT_VERIFICATION_RESULT=%s\nSETTINGS_GTK_PAGE_TRANSITION=%s\nSETTINGS_GTK_PAGE_BEFORE=%s\nSETTINGS_GTK_PAGE_AFTER=%s\n' "$input_pass" "$gtk_pass" "$before" "$after"
  client_pass=failed
  if [[ "$client_attach_count" -gt 0 && "$client_damage_count" -gt 0 && "$client_commit_count" -gt 0 && -n "$surface_id" ]]; then
    client_pass=passed
  fi
  wlroots_pass=failed
  if [[ "$surface_attach_count" -gt 0 && "$surface_damage_count" -gt 0 ]]; then
    wlroots_pass=passed
  fi
  scene_pass=failed
  if [[ "$scene_commit_count" -gt 0 ]]; then
    scene_pass=passed
  fi
  present_pass=failed
  if [[ "$output_present_count" -gt 0 ]]; then
    present_pass=passed
  fi
  printf 'SETTINGS_INPUT_VERIFICATION_RESULT=%s\nSETTINGS_GTK_PAGE_TRANSITION=%s\nSETTINGS_CLIENT_WAYLAND_TRACE=%s\nSETTINGS_WLROOTS_SURFACE_UPDATE=%s\nSETTINGS_SCENE_COMMIT=%s\nSETTINGS_OUTPUT_PRESENT=%s\nSETTINGS_TRACE_TIME_CORRELATION=%s\n' "$input_pass" "$gtk_pass" "$client_pass" "$wlroots_pass" "$scene_pass" "$present_pass" "$time_correlation_pass"

  if [[ "$gtk_pass" == passed ]]; then
    echo 'STAGE_RESULT name={GTK page selection and map transition} result=PASS'
  elif [[ "$input_pass" == passed ]]; then
    echo 'STAGE_RESULT name={GTK page selection and map transition} result=FAIL'
  else
    echo 'STAGE_RESULT name={GTK page selection and map transition} result=INCOMPLETE'
  fi
  if [[ "$client_pass" != passed || "$wlroots_pass" != passed || "$scene_pass" != passed || "$present_pass" != passed || "$time_correlation_pass" != passed ]]; then
    if [[ "$input_pass" == passed && "$gtk_pass" == passed ]]; then
      echo 'STAGE_RESULT name={GTK-to-Wayland-to-wlroots-to-output trace} result=FAIL reason=incomplete-or-uncorrelated-render-events'
    else
      echo 'STAGE_RESULT name={GTK-to-Wayland-to-wlroots-to-output trace} result=INCOMPLETE'
    fi
  else
    echo 'STAGE_RESULT name={GTK-to-Wayland-to-wlroots-to-output trace} result=PASS'
  fi

  if [[ "$input_pass" == passed && "$gtk_pass" == passed && "$client_pass" == passed &&
      "$wlroots_pass" == passed && "$scene_pass" == passed && "$present_pass" == passed &&
      "$time_correlation_pass" == passed ]]; then
    echo SETTINGS_PAGE_SELECTION_VERIFIED=passed
    echo 'SETTINGS_RENDER_TRACE_DUMP_END'
    return 0
  fi
  echo SETTINGS_PAGE_SELECTION_VERIFIED=failed
  echo 'SETTINGS_RENDER_TRACE_DUMP_END'
  return 1
}

if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
  return 0
fi

mode=
if [[ $# -gt 0 ]]; then
  mode=$1
fi
case "$mode" in
  launch) launch_trace ;;
  capture) capture_trace ;;
  *) echo "Usage: $0 launch|capture" >&2; exit 2 ;;
esac
