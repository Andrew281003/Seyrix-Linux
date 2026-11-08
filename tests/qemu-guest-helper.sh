#!/usr/bin/env bash
set -Eeuo pipefail

field() {
  local line="$1" name="$2" token
  for token in $line; do
    case "$token" in
      "$name"=*) printf '%s\n' "${token#*=}"; return 0 ;;
    esac
  done
  return 1
}

fail() {
  printf '%s\n' "$*" >&2
  printf 'QEMU_GUEST_HELPER=failed reason=%s\n' "$*"
  exit 1
}

runtime_dir=/run/user/1000
compositor_log=/tmp/seyrix-compositor.log
geometry_log=/tmp/seyrix-compositor.log
wayland_display="$(sed -n 's/.*WAYLAND_DISPLAY=\([^ ]*\).*/\1/p' "$compositor_log" | head -n 1)"
[[ -n "$wayland_display" ]] || fail "wayland-display-unavailable"

test_run_id() {
  local identity_path identity=""
  shopt -s nullglob
  local identity_files=(/tmp/seyrix-qemu-test-run-id-seyrix-*-graphics-test-*)
  shopt -u nullglob
  [[ "${#identity_files[@]}" -eq 1 ]] || fail "test-run-identity-file-ambiguous"
  identity_path="${identity_files[0]}"
  identity="${identity_path##*/seyrix-qemu-test-run-id-}"
  [[ "$identity" =~ ^seyrix-[a-z0-9-]+-graphics-test-[0-9TZ]+$ ]] ||
    fail "test-run-identity-unavailable"
  printf '%s' "$identity"
}

process_uid() {
  awk '/^Uid:/ { print $2; exit }' "/proc/$1/status" 2>/dev/null || true
}

process_group_id() {
  awk '{ sub(/^.*\) /, ""); print $3; exit }' "/proc/$1/stat" 2>/dev/null || true
}

process_has_arg_pair() {
  local pid="$1" expected_name="$2" expected_value="$3"
  local argument previous=""
  while IFS= read -r -d '' argument; do
    if [[ "$previous" == "$expected_name" && "$argument" == "$expected_value" ]]; then
      return 0
    fi
    previous="$argument"
  done < "/proc/$pid/cmdline" 2>/dev/null
  return 1
}

process_has_arg() {
  local pid="$1" expected="$2" argument
  while IFS= read -r -d '' argument; do
    [[ "$argument" == "$expected" ]] && return 0
  done < "/proc/$pid/cmdline" 2>/dev/null
  return 1
}

probe_identity_matches() {
  local pid="$1" app_id="$2" executable=""
  [[ "$(process_uid "$pid")" == 1000 ]] || return 1
  executable="$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)"
  [[ "$executable" == /usr/bin/seyrix-protocol-probe ]] || return 1
  process_has_arg_pair "$pid" --app-id "$app_id"
}

find_probe_pid() {
  local app_id="$1" proc_dir pid matches=0 selected=""
  for proc_dir in /proc/[0-9]*; do
    [[ -r "$proc_dir/cmdline" ]] || continue
    pid="${proc_dir##*/}"
    if probe_identity_matches "$pid" "$app_id"; then
      matches=$((matches + 1))
      selected="$pid"
    fi
  done
  [[ "$matches" -eq 1 ]] || return 1
  printf '%s' "$selected"
}

kill_probe_client() {
  local app_id="$1" run_id pid
  run_id="$(test_run_id)"
  [[ "$app_id" == "seyrix.qemu.$run_id."* ]] || fail "probe-app-id-is-not-run-scoped"
  pid="$(find_probe_pid "$app_id")" || fail "probe-process-identity-ambiguous-app-id-$app_id"
  grep -Fq "GEOMETRY_EVIDENCE event=map pid=$pid " "$geometry_log" ||
    fail "probe-process-has-no-current-compositor-map-pid-$pid"
  probe_identity_matches "$pid" "$app_id" || fail "probe-process-identity-changed-pid-$pid"
  printf 'TEST_CLIENT_TERMINATION_BEGIN pid=%s app_id=%s run=%s\n' "$pid" "$app_id" "$run_id"
  kill -KILL "$pid"
  for _ in $(seq 1 20); do
    [[ -e "/proc/$pid" ]] || break
    sleep 0.25
  done
  [[ ! -e "/proc/$pid" ]] || fail "probe-process-did-not-exit-pid-$pid"
  printf 'TEST_CLIENT_TERMINATION=passed pid=%s app_id=%s\n' "$pid" "$app_id"
}

current_compositor_pid() {
  local proc_dir pid uid executable state anchor_pid anchor_uid anchor_executable anchor_state
  local anchor_parent compositor_uid compositor_executable compositor_state compositor_parent
  local -a pids=()
  anchor_pid="$(sed -n 's/.*Started Seyrix session process-group anchor pid=\([0-9][0-9]*\).*/\1/p' \
    "$compositor_log" | tail -n 1)"
  [[ "$anchor_pid" =~ ^[0-9]+$ && -r "/proc/$anchor_pid/status" ]] || {
    printf 'COMPOSITOR_IDENTITY_ANCHOR unavailable pid=%s\n' "${anchor_pid:-none}" >&2
    return 1
  }
  anchor_uid="$(process_uid "$anchor_pid")"
  anchor_executable="$(readlink -f "/proc/$anchor_pid/exe" 2>/dev/null || true)"
  anchor_state="$(awk '/^State:/ { print $2; exit }' "/proc/$anchor_pid/status" 2>/dev/null || true)"
  anchor_parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$anchor_pid/status" 2>/dev/null || true)"
  [[ "$anchor_uid" == 1000 && "$anchor_executable" == /usr/bin/seyrix-compositor &&
     "$anchor_state" != Z && "$anchor_state" != X && "$anchor_parent" =~ ^[0-9]+$ ]] || {
    printf 'COMPOSITOR_IDENTITY_ANCHOR invalid pid=%s uid=%s exe=%s state=%s ppid=%s\n' \
      "$anchor_pid" "${anchor_uid:-none}" "${anchor_executable:-none}" \
      "${anchor_state:-none}" "${anchor_parent:-none}" >&2
    return 1
  }
  compositor_uid="$(process_uid "$anchor_parent")"
  compositor_executable="$(readlink -f "/proc/$anchor_parent/exe" 2>/dev/null || true)"
  compositor_state="$(awk '/^State:/ { print $2; exit }' "/proc/$anchor_parent/status" 2>/dev/null || true)"
  compositor_parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$anchor_parent/status" 2>/dev/null || true)"
  [[ "$compositor_uid" == 1000 && "$compositor_executable" == /usr/bin/seyrix-compositor &&
     "$compositor_state" != Z && "$compositor_state" != X && "$compositor_parent" =~ ^[0-9]+$ ]] || {
    printf 'COMPOSITOR_IDENTITY_PARENT invalid pid=%s uid=%s exe=%s state=%s ppid=%s\n' \
      "$anchor_parent" "${compositor_uid:-none}" "${compositor_executable:-none}" \
      "${compositor_state:-none}" "${compositor_parent:-none}" >&2
    return 1
  }
  for proc_dir in /proc/[0-9]*; do
    [[ -r "$proc_dir/status" ]] || continue
    pid="${proc_dir##*/}"
    [[ "$pid" != "$anchor_pid" ]] || continue
    uid="$(process_uid "$pid")"
    [[ "$uid" == 1000 ]] || continue
    executable="$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)"
    [[ "$executable" == /usr/bin/seyrix-compositor ]] || continue
    state="$(awk '/^State:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
    [[ "$state" != Z && "$state" != X ]] || continue
    pids+=("$pid")
  done
  if [[ "${#pids[@]}" -ne 1 || "${pids[0]:-}" != "$anchor_parent" ]]; then
    printf 'COMPOSITOR_IDENTITY_CANDIDATES count=%s pids=%s anchor_pid=%s anchor_parent=%s\n' \
      "${#pids[@]}" "${pids[*]:-none}" "$anchor_pid" "$anchor_parent" >&2
    return 1
  fi
  printf '%s' "$anchor_parent"
}

crash_panel_owner_shell() {
  local run_id compositor shell_pid uid executable state parent
  local before_started before_panels before_exits started panels exits current
  local new_shell_pid new_uid new_executable new_state new_parent proc_dir pid
  local -a pids=()
  run_id="$(test_run_id)"
  compositor="$(current_compositor_pid)" || fail "current-compositor-identity-ambiguous"
  grep -q 'Layer shell surface mapped namespace=seyrix-panel' "$compositor_log" ||
    fail "panel-surface-not-mapped-by-current-shell"
  before_started="$(grep -c 'Started isolated Seyrix desktop shell pid=' "$compositor_log" || true)"
  before_panels="$(grep -c 'Layer shell surface mapped namespace=seyrix-panel' "$compositor_log" || true)"
  before_exits="$(grep -c 'Seyrix shell exited; compositor remains available' "$compositor_log" || true)"
  shell_pid="$(sed -n 's/.*Started isolated Seyrix desktop shell pid=\([0-9][0-9]*\).*/\1/p' \
    "$compositor_log" | tail -n 1)"
  [[ "$shell_pid" =~ ^[0-9]+$ && -r "/proc/$shell_pid/status" ]] ||
    fail "panel-owner-shell-pid-not-recorded-by-current-compositor"
  uid="$(process_uid "$shell_pid")"
  executable="$(readlink -f "/proc/$shell_pid/exe" 2>/dev/null || true)"
  state="$(awk '/^State:/ { print $2; exit }' "/proc/$shell_pid/status" 2>/dev/null || true)"
  parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$shell_pid/status" 2>/dev/null || true)"
  [[ "$uid" == 1000 && "$executable" == /usr/bin/seyrix-shell &&
     "$state" != Z && "$state" != X && "$parent" == "$compositor" ]] ||
    fail "panel-owner-shell-not-current-compositor-child-pid-$shell_pid"
  for proc_dir in /proc/[0-9]*; do
    [[ -r "$proc_dir/status" ]] || continue
    pid="${proc_dir##*/}"
    [[ "$(process_uid "$pid")" == 1000 ]] || continue
    [[ "$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)" == /usr/bin/seyrix-shell ]] || continue
    [[ "$(awk '/^State:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)" != Z ]] || continue
    [[ "$(awk '/^State:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)" != X ]] || continue
    [[ "$(awk '/^PPid:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)" == "$compositor" ]] || continue
    pids+=("$pid")
  done
  [[ "${#pids[@]}" -eq 1 && "${pids[0]}" == "$shell_pid" ]] ||
    fail "panel-owner-shell-child-identity-ambiguous-pids-${pids[*]:-none}"
  printf 'PANEL_OWNER_TERMINATION_BEGIN shell_pid=%s compositor_pid=%s run=%s uid=%s exe=%s\n' \
    "$shell_pid" "$compositor" "$run_id" "$uid" "$executable"
  kill -KILL "$shell_pid"
  for _ in $(seq 1 80); do
    started="$(grep -c 'Started isolated Seyrix desktop shell pid=' "$compositor_log" || true)"
    panels="$(grep -c 'Layer shell surface mapped namespace=seyrix-panel' "$compositor_log" || true)"
    exits="$(grep -c 'Seyrix shell exited; compositor remains available' "$compositor_log" || true)"
    if [[ "$started" -gt "$before_started" && "$panels" -gt "$before_panels" &&
          "$exits" -gt "$before_exits" ]]; then
      new_shell_pid="$(sed -n 's/.*Started isolated Seyrix desktop shell pid=\([0-9][0-9]*\).*/\1/p' \
        "$compositor_log" | tail -n 1)"
      current="$(current_compositor_pid || true)"
      new_uid="$(process_uid "$new_shell_pid")"
      new_executable="$(readlink -f "/proc/$new_shell_pid/exe" 2>/dev/null || true)"
      new_state="$(awk '/^State:/ { print $2; exit }' "/proc/$new_shell_pid/status" 2>/dev/null || true)"
      new_parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$new_shell_pid/status" 2>/dev/null || true)"
      pids=()
      for proc_dir in /proc/[0-9]*; do
        [[ -r "$proc_dir/status" ]] || continue
        pid="${proc_dir##*/}"
        [[ "$(process_uid "$pid")" == 1000 ]] || continue
        [[ "$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)" == /usr/bin/seyrix-shell ]] || continue
        state="$(awk '/^State:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
        [[ "$state" != Z && "$state" != X ]] || continue
        parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
        [[ "$parent" == "$compositor" ]] || continue
        pids+=("$pid")
      done
      if [[ "$current" == "$compositor" && "$new_uid" == 1000 &&
            "$new_executable" == /usr/bin/seyrix-shell && "$new_state" != Z &&
            "$new_state" != X && "$new_parent" == "$compositor" &&
            "${#pids[@]}" -eq 1 && "${pids[0]}" == "$new_shell_pid" ]]; then
        printf 'PHASE2D2_PANEL_OWNER_CRASH_ISOLATION=passed old_shell_pid=%s new_shell_pid=%s compositor_pid=%s panel_maps=%s\n' \
          "$shell_pid" "$new_shell_pid" "$compositor" "$((panels - before_panels))"
        return 0
      fi
    fi
    sleep 0.25
  done
  printf 'PANEL_OWNER_RECOVERY_FAILED compositor_pid=%s started_before=%s started_after=%s panel_maps_before=%s panel_maps_after=%s exits_before=%s exits_after=%s\n' \
    "$compositor" "$before_started" "${started:-unknown}" "$before_panels" \
    "${panels:-unknown}" "$before_exits" "${exits:-unknown}"
  fail "panel-owner-shell-crash-did-not-restore-panel-and-compositor"
}

recover_shell_after_startup_failure() {
  local run_id compositor shell_pid uid executable state parent fixture
  local before_started before_panels before_exits before_schedules started panels exits schedules
  local new_shell_pid new_uid new_executable new_state new_parent current proc_dir pid
  local trace_path=/tmp/seyrix-shell-startup-failure.trace
  local -a pids=()
  shell_startup_trace() {
    local message="SHELL_RECOVERY_TRACE $*"
    printf '%s\n' "$message" >>"$trace_path" || return 1
    printf '%s\n' "$message"
  }
  : >"$trace_path" || fail "cannot-create-shell-startup-trace"
  chmod 600 "$trace_path" || fail "cannot-restrict-shell-startup-trace"
  run_id="$(test_run_id)"
  compositor="$(current_compositor_pid)" || fail "current-compositor-identity-ambiguous"
  [[ "$(id -u)" == 1000 ]] || fail "shell-startup-failure-test-not-running-as-seyrix"
  [[ -d "$runtime_dir" && -O "$runtime_dir" &&
     "$(stat -c '%a' "$runtime_dir")" == 700 ]] ||
    fail "private-user-runtime-directory-not-available"
  fixture="$runtime_dir/seyrix-qemu-fail-shell-exec-once"
  [[ ! -e "$fixture" && ! -L "$fixture" ]] || fail "shell-startup-failure-fixture-not-clean"
  before_started="$(grep -c 'Started isolated Seyrix desktop shell pid=' "$compositor_log" || true)"
  before_panels="$(grep -c 'Layer shell surface mapped namespace=seyrix-panel' "$compositor_log" || true)"
  before_exits="$(grep -c 'Seyrix shell exited; compositor remains available' "$compositor_log" || true)"
  before_schedules="$(grep -c 'Scheduling isolated shell restart attempt' "$compositor_log" || true)"
  shell_pid="$(sed -n 's/.*Started isolated Seyrix desktop shell pid=\([0-9][0-9]*\).*/\1/p' \
    "$compositor_log" | tail -n 1)"
  [[ "$shell_pid" =~ ^[0-9]+$ && -r "/proc/$shell_pid/status" ]] ||
    fail "current-shell-pid-not-recorded"
  uid="$(process_uid "$shell_pid")"
  executable="$(readlink -f "/proc/$shell_pid/exe" 2>/dev/null || true)"
  state="$(awk '/^State:/ { print $2; exit }' "/proc/$shell_pid/status" 2>/dev/null || true)"
  parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$shell_pid/status" 2>/dev/null || true)"
  [[ "$uid" == 1000 && "$executable" == /usr/bin/seyrix-shell &&
     "$state" != Z && "$state" != X && "$parent" == "$compositor" ]] ||
    fail "current-shell-identity-invalid-pid-$shell_pid"
  printf 'SHELL_STARTUP_FAILURE_BEGIN shell_pid=%s compositor_pid=%s run=%s fixture=%s uid=%s\n' \
    "$shell_pid" "$compositor" "$run_id" "$fixture" "$(id -u)"
  shell_startup_trace "phase=validated shell_pid=$shell_pid compositor_pid=$compositor before_started=$before_started before_panels=$before_panels before_exits=$before_exits before_schedules=$before_schedules"
  : >"$fixture" || fail "cannot-create-private-shell-startup-fixture"
  chmod 600 "$fixture" || fail "cannot-restrict-shell-startup-fixture"
  trap 'rm -f -- "$fixture"' EXIT
  kill -KILL "$shell_pid"
  shell_startup_trace "phase=kill-returned shell_pid=$shell_pid"
  for attempt in $(seq 1 100); do
    exits="$(grep -c 'Seyrix shell exited; compositor remains available' "$compositor_log" || true)"
    schedules="$(grep -c 'Scheduling isolated shell restart attempt' "$compositor_log" || true)"
    if [[ "$exits" -ge $((before_exits + 2)) &&
          "$schedules" -ge $((before_schedules + 2)) ]] &&
       grep -Fq 'SEYRIX_QEMU_ACCEPTANCE_SHELL_EXEC_FAILURE=consumed' "$compositor_log" &&
       grep -Fq 'Scheduling isolated shell restart attempt 1 in 1000 ms' "$compositor_log" &&
       grep -Fq 'Scheduling isolated shell restart attempt 2 in 2000 ms' "$compositor_log"; then
      shell_startup_trace "phase=retry-sequence-observed iteration=$attempt exits=$exits schedules=$schedules"
      break
    fi
    if (( attempt % 10 == 0 )); then
      shell_startup_trace "phase=waiting-for-retries iteration=$attempt exits=$exits schedules=$schedules fixture_present=$([[ -e "$fixture" ]] && echo yes || echo no)"
    fi
    sleep 0.1
  done
  if [[ -e "$fixture" ||
        "${exits:-0}" -lt $((before_exits + 2)) ||
        "${schedules:-0}" -lt $((before_schedules + 2)) ]] ||
     ! grep -Fq 'SEYRIX_QEMU_ACCEPTANCE_SHELL_EXEC_FAILURE=consumed' "$compositor_log" ||
     ! grep -Fq 'Scheduling isolated shell restart attempt 2 in 2000 ms' "$compositor_log"; then
    fail "shell-startup-failure-was-not-observed-after-first-retry"
  fi
  for attempt in $(seq 1 120); do
    started="$(grep -c 'Started isolated Seyrix desktop shell pid=' "$compositor_log" || true)"
    panels="$(grep -c 'Layer shell surface mapped namespace=seyrix-panel' "$compositor_log" || true)"
    current="$(current_compositor_pid || true)"
    if [[ "$started" -ge $((before_started + 1)) &&
          "$panels" -gt "$before_panels" && "$current" == "$compositor" ]]; then
      new_shell_pid="$(sed -n 's/.*Started isolated Seyrix desktop shell pid=\([0-9][0-9]*\).*/\1/p' \
        "$compositor_log" | tail -n 1)"
      new_uid="$(process_uid "$new_shell_pid")"
      new_executable="$(readlink -f "/proc/$new_shell_pid/exe" 2>/dev/null || true)"
      new_state="$(awk '/^State:/ { print $2; exit }' "/proc/$new_shell_pid/status" 2>/dev/null || true)"
      new_parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$new_shell_pid/status" 2>/dev/null || true)"
      pids=()
      for proc_dir in /proc/[0-9]*; do
        [[ -r "$proc_dir/status" ]] || continue
        pid="${proc_dir##*/}"
        [[ "$(process_uid "$pid")" == 1000 ]] || continue
        [[ "$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)" == /usr/bin/seyrix-shell ]] || continue
        state="$(awk '/^State:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
        [[ "$state" != Z && "$state" != X ]] || continue
        parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
        [[ "$parent" == "$compositor" ]] || continue
        pids+=("$pid")
      done
      if [[ "$new_uid" == 1000 && "$new_executable" == /usr/bin/seyrix-shell &&
            "$new_state" != Z && "$new_state" != X && "$new_parent" == "$compositor" &&
            "${#pids[@]}" -eq 1 && "${pids[0]}" == "$new_shell_pid" ]]; then
        shell_startup_trace "phase=recovered-session-observed iteration=$attempt shell_pid=$new_shell_pid compositor=$compositor"
        printf 'PHASE2D2_SHELL_STARTUP_FAILURE_RECOVERY=passed failed_launches=1 restart_attempts=2 recovered_shell_pid=%s compositor_pid=%s panel_maps=%s\n' \
          "$new_shell_pid" "$compositor" "$((panels - before_panels))"
        rm -f -- "$fixture"
        trap - EXIT
        return 0
      fi
    fi
    if (( attempt % 20 == 0 )); then
      shell_startup_trace "phase=waiting-for-shell iteration=$attempt started=$started panels=$panels compositor=$current"
    fi
    sleep 0.25
  done
  fail "shell-did-not-recover-after-startup-exec-failure"
}

stop_compositor_and_verify_cleanup() {
  local compositor anchor shell_pid terminal_pid run_id uid executable state parent group
  local signal_name="${1:-SIGKILL}" signal_option before_clean_stops clean_stops
  local candidate candidate_state candidate_group current tty_shell
  local -a active_group_pids=()
  case "$signal_name" in
    SIGTERM) signal_option=TERM ;;
    SIGKILL) signal_option=KILL ;;
    *) fail "unsupported-compositor-signal-$signal_name" ;;
  esac
  run_id="$(test_run_id)"
  compositor="$(current_compositor_pid)" || fail "current-compositor-identity-ambiguous"
  anchor="$(sed -n 's/.*Started Seyrix session process-group anchor pid=\([0-9][0-9]*\).*/\1/p' \
    "$compositor_log" | tail -n 1)"
  shell_pid="$(sed -n 's/.*Started isolated Seyrix desktop shell pid=\([0-9][0-9]*\).*/\1/p' \
    "$compositor_log" | tail -n 1)"
  terminal_pid="$(sed -n 's/.*Started weston-terminal with pid \([0-9][0-9]*\).*/\1/p' \
    "$compositor_log" | tail -n 1)"
  [[ "$anchor" =~ ^[0-9]+$ && "$shell_pid" =~ ^[0-9]+$ &&
     "$terminal_pid" =~ ^[0-9]+$ ]] || fail "session-process-identities-not-recorded"
  [[ "$(process_uid "$anchor")" == 1000 &&
     "$(readlink -f "/proc/$anchor/exe" 2>/dev/null || true)" == /usr/bin/seyrix-compositor &&
     "$(awk '/^PPid:/ { print $2; exit }' "/proc/$anchor/status" 2>/dev/null || true)" == "$compositor" &&
     "$(process_group_id "$anchor")" == "$anchor" ]] || fail "session-anchor-identity-invalid-pid-$anchor"
  for candidate in "$shell_pid" "$terminal_pid"; do
    [[ -r "/proc/$candidate/status" ]] || fail "session-child-missing-pid-$candidate"
    uid="$(process_uid "$candidate")"
    executable="$(readlink -f "/proc/$candidate/exe" 2>/dev/null || true)"
    state="$(awk '/^State:/ { print $2; exit }' "/proc/$candidate/status" 2>/dev/null || true)"
    parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$candidate/status" 2>/dev/null || true)"
    group="$(process_group_id "$candidate")"
    if [[ "$candidate" == "$shell_pid" ]]; then
      [[ "$executable" == /usr/bin/seyrix-shell ]] || fail "session-shell-executable-invalid-pid-$candidate"
    else
      [[ "$executable" == /usr/bin/weston-terminal ]] || fail "session-terminal-executable-invalid-pid-$candidate"
    fi
    [[ "$uid" == 1000 && "$state" != Z && "$state" != X &&
       "$parent" == "$compositor" && "$group" == "$anchor" ]] ||
      fail "session-child-ownership-invalid-pid-$candidate"
  done
  before_clean_stops="$(grep -c 'Seyrix compositor stopped cleanly' "$compositor_log" || true)"
  printf 'COMPOSITOR_TERMINATION_BEGIN compositor_pid=%s anchor_pid=%s shell_pid=%s terminal_pid=%s run=%s signal=%s\n' \
    "$compositor" "$anchor" "$shell_pid" "$terminal_pid" "$run_id" "$signal_name"
  kill -"$signal_option" "$compositor"
  for _ in $(seq 1 100); do
    active_group_pids=()
    for proc_dir in /proc/[0-9]*; do
      [[ -r "$proc_dir/status" ]] || continue
      candidate="${proc_dir##*/}"
      candidate_state="$(awk '/^State:/ { print $2; exit }' "$proc_dir/status" 2>/dev/null || true)"
      [[ "$candidate_state" != Z && "$candidate_state" != X ]] || continue
      candidate_group="$(process_group_id "$candidate")"
      [[ "$candidate_group" == "$anchor" ]] && active_group_pids+=("$candidate")
    done
    current="$(readlink -f "/proc/$compositor/exe" 2>/dev/null || true)"
    tty_shell=0
    if systemctl is-active --quiet getty@tty1 && pgrep -a -u 1000 | grep -Eq ' [-]?bash$'; then
      tty_shell=1
    fi
    if [[ -z "$current" && "${#active_group_pids[@]}" -eq 0 && "$tty_shell" -eq 1 ]]; then
      break
    fi
    sleep 0.2
  done
  if [[ -n "${current:-}" || "${#active_group_pids[@]}" -ne 0 || "$tty_shell" -ne 1 ]]; then
    printf 'COMPOSITOR_TERMINATION_CLEANUP_FAILED signal=%s compositor_exe=%s group_pids=%s tty_shell=%s\n' \
      "$signal_name" "${current:-none}" "${active_group_pids[*]:-none}" "${tty_shell:-0}"
    pgrep -a -u 1000 -f 'seyrix-compositor|seyrix-shell|weston-terminal' || true
    fail "compositor-$signal_name-did-not-clean-session-process-group"
  fi
  if [[ "$signal_name" == SIGTERM ]]; then
    clean_stops="$(grep -c 'Seyrix compositor stopped cleanly' "$compositor_log" || true)"
    [[ "$clean_stops" -gt "$before_clean_stops" ]] ||
      fail "compositor-sigterm-did-not-log-clean-shutdown"
    printf 'PHASE2D2_COMPOSITOR_SIGTERM_CLEANUP=passed compositor_pid=%s anchor_pid=%s shell_pid=%s terminal_pid=%s active_group_members=0 tty_shell=present clean_stop_log=present\n' \
      "$compositor" "$anchor" "$shell_pid" "$terminal_pid"
  else
    printf 'PHASE2D2_COMPOSITOR_CRASH_CLEANUP=passed compositor_pid=%s anchor_pid=%s shell_pid=%s terminal_pid=%s active_group_members=0 tty_shell=present\n' \
      "$compositor" "$anchor" "$shell_pid" "$terminal_pid"
  fi
}

kill_current_mapped_terminal() {
  local compositor run_id pid uid executable parent
  local -a candidates=()
  run_id="$(test_run_id)"
  compositor="$(current_compositor_pid)" || fail "current-compositor-identity-ambiguous"
  mapfile -t candidates < <(sed -n 's/.*Started weston-terminal with pid \([0-9][0-9]*\).*/\1/p' \
    "$compositor_log")
  for ((index = ${#candidates[@]} - 1; index >= 0; index--)); do
    pid="${candidates[index]}"
    uid="$(process_uid "$pid")"
    executable="$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)"
    parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
    if [[ "$uid" == 1000 && "$executable" == /usr/bin/weston-terminal &&
          "$parent" == "$compositor" ]] &&
        grep -Fq "GEOMETRY_EVIDENCE event=map pid=$pid " "$geometry_log"; then
      printf 'TEST_TERMINAL_TERMINATION_BEGIN pid=%s compositor_pid=%s run=%s uid=%s exe=%s\n' \
        "$pid" "$compositor" "$run_id" "$uid" "$executable"
      kill -KILL "$pid"
      for _ in $(seq 1 20); do
        [[ -e "/proc/$pid" ]] || break
        sleep 0.25
      done
      [[ ! -e "/proc/$pid" ]] || fail "terminal-process-did-not-exit-pid-$pid"
      echo "TEST_TERMINAL_TERMINATION=passed pid=$pid"
      return 0
    fi
  done
  fail "no-current-compositor-mapped-terminal-process-to-terminate"
}

map_for_identity() {
  local pid="$1" surface="$2"
  grep -F "GEOMETRY_EVIDENCE event=map pid=$pid surface=$surface " "$geometry_log" | tail -n 1
}

resize_cleanup() {
  local edge="$1" direction="$2" run_id app_id="" pid="" surface=""
  run_id="$(test_run_id)"
  app_id="seyrix.qemu.$run_id.resize.$edge.$direction"
  if [[ -r "/tmp/seyrix-resize-$edge-$direction.identity" ]]; then
    read -r pid surface < "/tmp/seyrix-resize-$edge-$direction.identity"
  else
    pid="$(find_probe_pid "$app_id" || true)"
  fi
  if [[ "$pid" =~ ^[0-9]+$ ]] && probe_identity_matches "$pid" "$app_id" &&
      process_has_arg_pair "$pid" --resize "$edge"; then
    printf 'RESIZE_CLIENT_CLEANUP pid=%s surface=%s app_id=%s run=%s\n' \
      "$pid" "${surface:-unmapped}" "$app_id" "$run_id"
    kill -TERM "$pid" 2>/dev/null || true
    for _ in $(seq 1 12); do
      probe_identity_matches "$pid" "$app_id" || return 0
      sleep 0.25
    done
    if probe_identity_matches "$pid" "$app_id" &&
        process_has_arg_pair "$pid" --resize "$edge"; then
      kill -KILL "$pid" 2>/dev/null || true
    fi
  fi
}

resize_cleanup_command() {
  resize_cleanup "$1" "$2"
  echo RESIZE_CLEANUP=passed
}

qmp_coordinate() {
  local point="$1" extent="$2"
  (( point < 0 )) && point=0
  (( point >= extent )) && point=$((extent - 1))
  printf '%s' "$((point * 32767 / (extent - 1)))"
}

resize_start() {
  local edge="$1" min_size="${2:-}" max_size="${3:-}" direction="${4:-normal}"
  case "$edge" in
    top|bottom|left|right|top-left|top-right|bottom-left|bottom-right) ;;
    *) fail "invalid-resize-edge-$edge" ;;
  esac
  local log="/tmp/seyrix-resize-$edge-$direction.log"
  local output="/tmp/seyrix-resize-$edge-$direction.stdout"
  local identity="/tmp/seyrix-resize-$edge-$direction.identity"
  local app_id="seyrix.qemu.$(test_run_id).resize.$edge.$direction"
  rm -f -- "$log" "$output" "$identity"
  local -a args=(--resize "$edge" --title "Seyrix QEMU Resize $edge" \
    --app-id "$app_id" --initial-size 440x320 --log "$log")
  [[ -z "$min_size" ]] || args+=(--min-size "$min_size")
  [[ -z "$max_size" ]] || args+=(--max-size "$max_size")
  nohup env XDG_RUNTIME_DIR="$runtime_dir" \
    WAYLAND_DISPLAY="$wayland_display" seyrix-protocol-probe "${args[@]}" \
    >"$output" 2>&1 </dev/null &

  local ready="" decoration_mode="" map_line="" pid="" surface="" x="" y="" width="" height=""
  for _ in $(seq 1 45); do
    ready="$(grep 'RESIZE_READY=1 ' "$log" 2>/dev/null | tail -n 1 || true)"
    decoration_mode="$(grep -E 'DECORATION_MODE=(server_side|client_side)|DECORATION_REQUEST=unavailable' \
      "$log" 2>/dev/null | tail -n 1 || true)"
    if [[ -n "$ready" && -n "$decoration_mode" ]]; then
      pid="$(field "$ready" pid || true)"
      surface="$(field "$ready" surface || true)"
      map_line="$(map_for_identity "$pid" "$surface" || true)"
      if [[ -n "$map_line" ]]; then
        x="$(field "$map_line" x || true)"
        y="$(field "$map_line" y || true)"
        width="$(field "$map_line" width || true)"
        height="$(field "$map_line" height || true)"
        break
      fi
    fi
    sleep 1
  done
  if [[ -z "$decoration_mode" || ! "$pid" =~ ^[0-9]+$ || ! "$surface" =~ ^[0-9]+$ ||
        ! "$x" =~ ^-?[0-9]+$ || ! "$y" =~ ^-?[0-9]+$ ||
        ! "$width" =~ ^[0-9]+$ || ! "$height" =~ ^[0-9]+$ ]]; then
    printf 'RESIZE_TARGET_FAILED edge=%s ready=%s decoration=%s map=%s\n' "$edge" \
      "${ready:-missing}" "${decoration_mode:-missing}" "${map_line:-missing}"
    printf 'RESIZE_PROBE_LOG_BEGIN edge=%s path=%s\n' "$edge" "$log"
    if [[ -r "$log" ]]; then
      tail -n 40 "$log"
    else
      printf 'RESIZE_PROBE_LOG=missing\n'
    fi
    printf 'RESIZE_PROBE_STDOUT_BEGIN edge=%s path=%s\n' "$edge" "$output"
    if [[ -r "$output" ]]; then
      tail -n 40 "$output"
    else
      printf 'RESIZE_PROBE_STDOUT=missing\n'
    fi
    local app_id="seyrix.qemu.$run_id.resize.$edge.$direction"
    printf 'RESIZE_COMPOSITOR_LOG_BEGIN edge=%s app_id=%s\n' "$edge" "$app_id"
    grep -F "$app_id" "$geometry_log" | tail -n 20 || true
    printf 'RESIZE_COMPOSITOR_LOG_END edge=%s\n' "$edge"
    resize_cleanup "$edge" "$direction"
    return 1
  fi
  printf '%s %s\n' "$pid" "$surface" > "$identity"

  local client_x=$((x + 60))
  local client_y=$((y + 26 + 60))
  local drag_x="$client_x" drag_y="$client_y"
  if [[ "$direction" == normal ]]; then
    case "$edge" in
      left|top-left|bottom-left) drag_x=$((drag_x - 60)) ;;
      right|top-right|bottom-right) drag_x=$((drag_x + 60)) ;;
    esac
    case "$edge" in
      top|top-left|top-right) drag_y=$((drag_y - 60)) ;;
      bottom|bottom-left|bottom-right) drag_y=$((drag_y + 60)) ;;
    esac
  elif [[ "$direction" == in && "$edge" == bottom-right ]]; then
    drag_x=$((drag_x - 100))
    drag_y=$((drag_y - 100))
  elif [[ "$direction" == out && "$edge" == bottom-right ]]; then
    drag_x=$((drag_x + 100))
    drag_y=$((drag_y + 100))
  else
    fail "invalid-resize-direction-$edge-$direction"
  fi
  printf 'RESIZE_TARGET_READY edge=%s pid=%s surface=%s scene=%s,%s size=%sx%s qmp_start=%s,%s qmp_end=%s,%s\n' \
    "$edge" "$pid" "$surface" "$x" "$y" "$width" "$height" \
    "$(qmp_coordinate "$client_x" 640)" "$(qmp_coordinate "$client_y" 480)" \
    "$(qmp_coordinate "$drag_x" 640)" "$(qmp_coordinate "$drag_y" 480)"
}

resize_check() {
  local edge="$1" min_size="${2:-}" max_size="${3:-}" direction="${4:-normal}"
  local log="/tmp/seyrix-resize-$edge-$direction.log"
  local ready="" completion="" geometry="" pid="" surface=""
  for _ in $(seq 1 45); do
    ready="$(grep 'RESIZE_READY=1 ' "$log" 2>/dev/null | tail -n 1 || true)"
    completion="$(grep 'RESIZE_COMPLETE=' "$log" 2>/dev/null | tail -n 1 || true)"
    if [[ "$completion" == *'RESIZE_COMPLETE=failed'* ]]; then
      printf 'RESIZE_ASSERTION_FAILED edge=%s probe=%s\n' "$edge" "$completion"
      tail -n 20 "$log" || true
      pid="$(field "$ready" pid || true)"
      surface="$(field "$ready" surface || true)"
      if [[ "$pid" =~ ^[0-9]+$ && "$surface" =~ ^[0-9]+$ ]]; then
        printf 'RESIZE_COMPOSITOR_DIAGNOSTICS_BEGIN edge=%s identity=%s,%s\n' \
          "$edge" "$pid" "$surface"
        grep -F "GEOMETRY_EVIDENCE event=grab-start pid=$pid surface=$surface " \
          "$geometry_log" | tail -n 3 || true
        grep -F "GEOMETRY_EVIDENCE event=grab-end pid=$pid surface=$surface " \
          "$geometry_log" | tail -n 3 || true
        grep -F "RESIZE_FRAME_START pid=$pid surface=$surface " \
          "$geometry_log" | tail -n 3 || true
        grep -E 'Window resize gesture started|Resize gesture ended|Resize frame update' \
          "$geometry_log" | tail -n 12 || true
        printf 'RESIZE_COMPOSITOR_DIAGNOSTICS_END edge=%s\n' "$edge"
      fi
      resize_cleanup "$edge" "$direction"
      return 1
    fi
    [[ "$completion" == *'RESIZE_COMPLETE=passed'* ]] && break
    sleep 1
  done
  if [[ "$completion" != *'RESIZE_COMPLETE=passed'* ]]; then
    printf 'RESIZE_ASSERTION_FAILED edge=%s reason=probe-completion-timeout\n' "$edge"
    tail -n 20 "$log" || true
    resize_cleanup "$edge" "$direction"
    return 1
  fi

  pid="$(field "$ready" pid || true)"
  surface="$(field "$ready" surface || true)"
  geometry="$(grep "RESIZE_GEOMETRY edge=$edge " "$log" | tail -n 1 || true)"
  local before="" after="" changed="" before_width="" before_height="" after_width="" after_height=""
  before="$(field "$geometry" before || true)"
  after="$(field "$geometry" after || true)"
  changed="$(field "$geometry" changed || true)"
  local map_line grab_start_line grab_end_line commit_line actual_width actual_height actual_x actual_y
  map_line="$(map_for_identity "$pid" "$surface" || true)"
  grab_start_line="$(grep -F "GEOMETRY_EVIDENCE event=grab-start pid=$pid surface=$surface " \
    "$geometry_log" | tail -n 1 || true)"
  grab_end_line="$(grep -F "GEOMETRY_EVIDENCE event=grab-end pid=$pid surface=$surface " \
    "$geometry_log" | tail -n 1 || true)"
  [[ "$before" =~ ^([0-9]+)x([0-9]+)$ ]] || before=""
  if [[ -n "$before" ]]; then
    before_width="${BASH_REMATCH[1]}"
    before_height="${BASH_REMATCH[2]}"
  fi
  [[ "$after" =~ ^([0-9]+)x([0-9]+)$ ]] || after=""
  if [[ -n "$after" ]]; then
    after_width="${BASH_REMATCH[1]}"
    after_height="${BASH_REMATCH[2]}"
  fi
  for _ in $(seq 1 50); do
    commit_line="$(grep -F "GEOMETRY_EVIDENCE event=commit pid=$pid surface=$surface " \
      "$geometry_log" | tail -n 1 || true)"
    actual_width="$(field "$commit_line" width || true)"
    actual_height="$(field "$commit_line" height || true)"
    if [[ "$actual_width" == "$after_width" &&
          "$actual_height" == "$after_height" ]]; then
      break
    fi
    sleep 0.1
  done
  if [[ ! "$before_width" =~ ^[0-9]+$ || ! "$before_height" =~ ^[0-9]+$ ||
        ! "$after_width" =~ ^[0-9]+$ || ! "$after_height" =~ ^[0-9]+$ ]]; then
    printf 'RESIZE_ASSERTION_FAILED edge=%s reason=malformed-probe-geometry before=%s after=%s\n' \
      "$edge" "$before" "$after"
    resize_cleanup "$edge" "$direction"
    return 1
  fi
  actual_x="$(field "$commit_line" x || true)"
  actual_y="$(field "$commit_line" y || true)"
  local expected_width_change=0 expected_height_change=0
  case "$edge" in
    left|right) expected_width_change=1 ;;
    top|bottom) expected_height_change=1 ;;
    top-left|top-right|bottom-left|bottom-right)
      expected_width_change=1
      expected_height_change=1
      ;;
  esac
  local width_changed=0 height_changed=0
  [[ "$before_width" != "$after_width" ]] && width_changed=1
  [[ "$before_height" != "$after_height" ]] && height_changed=1
  local initial_x initial_y expected_x expected_y anchor_match=1
  initial_x="$(field "$grab_start_line" x || true)"
  initial_y="$(field "$grab_start_line" y || true)"
  if [[ ! "$initial_x" =~ ^-?[0-9]+$ || ! "$initial_y" =~ ^-?[0-9]+$ ||
        ! "$actual_x" =~ ^-?[0-9]+$ || ! "$actual_y" =~ ^-?[0-9]+$ ]]; then
    anchor_match=0
  else
    expected_x="$initial_x"
    expected_y="$initial_y"
    case "$edge" in
      left|top-left|bottom-left)
        expected_x=$((initial_x - after_width + before_width))
        ;;
    esac
    case "$edge" in
      top|top-left|top-right)
        expected_y=$((initial_y - after_height + before_height))
        ;;
    esac
    [[ "$actual_x" == "$expected_x" && "$actual_y" == "$expected_y" ]] || anchor_match=0
  fi
  local constraint_match=1
  if [[ -n "$min_size" && "$edge" == bottom-right ]]; then
    local minimum_width="${min_size%x*}" minimum_height="${min_size#*x}"
    if [[ "$before_width" -gt "$minimum_width" && "$before_height" -gt "$minimum_height" ]]; then
      [[ "$after_width" == "$minimum_width" && "$after_height" == "$minimum_height" ]] || constraint_match=0
    fi
  fi
  if [[ -n "$max_size" && "$edge" == bottom-right ]]; then
    local maximum_width="${max_size%x*}" maximum_height="${max_size#*x}"
    if [[ "$before_width" -lt "$maximum_width" && "$before_height" -lt "$maximum_height" ]]; then
      [[ "$after_width" == "$maximum_width" && "$after_height" == "$maximum_height" ]] || constraint_match=0
    fi
  fi
  printf 'RESIZE_GEOMETRY_EVIDENCE edge=%s identity=%s,%s before=%s after=%s scene=%s,%s committed=%sx%s\n' \
    "$edge" "$pid" "$surface" "$before" "$after" "${actual_x:-missing}" \
    "${actual_y:-missing}" "${actual_width:-missing}" "${actual_height:-missing}"
  if [[ "$changed" == 1 && "$width_changed" -eq "$expected_width_change" &&
        "$height_changed" -eq "$expected_height_change" &&
        "$actual_width" == "$after_width" && "$actual_height" == "$after_height" &&
        "$constraint_match" -eq 1 && "$anchor_match" -eq 1 &&
        -n "$map_line" && -n "$grab_start_line" && -n "$grab_end_line" &&
        -n "$commit_line" ]]; then
    printf 'RESIZE_ASSERTION=passed edge=%s before=%s after=%s scene=%s,%s\n' \
      "$edge" "$before" "$after" "$actual_x" "$actual_y"
    return 0
  fi
  printf 'RESIZE_ASSERTION_FAILED edge=%s probe=%s actual=%sx%s changed=%s map=%s grab-start=%s grab-end=%s\n' \
    "$edge" "$completion" "${actual_width:-missing}" "${actual_height:-missing}" \
    "$changed" "${map_line:-missing}" "${grab_start_line:-missing}" "${grab_end_line:-missing}"
  printf 'RESIZE_ANCHOR_ASSERTION_FAILED edge=%s initial=%s,%s expected=%s,%s committed=%s,%s commit=%s\n' \
    "$edge" "${initial_x:-missing}" "${initial_y:-missing}" \
    "${expected_x:-missing}" "${expected_y:-missing}" \
    "${actual_x:-missing}" "${actual_y:-missing}" "${commit_line:-missing}"
  tail -n 24 "$log" || true
  resize_cleanup "$edge" "$direction"
  return 1
}

watch_start() {
  local run_id
  run_id="$(test_run_id)"
  local log=/tmp/seyrix-watch-pointer.log
  local output=/tmp/seyrix-watch-pointer.stdout
  rm -f -- "$log" "$output" /tmp/seyrix-watch-pointer.identity
  nohup env XDG_RUNTIME_DIR="$runtime_dir" \
    WAYLAND_DISPLAY="$wayland_display" seyrix-protocol-probe --watch-pointer \
    --initial-size 280x180 --title "Seyrix QEMU Pointer Watch" \
    --app-id "seyrix.qemu.$run_id.pointer-watch" --log "$log" \
    >"$output" 2>&1 </dev/null &
  local ready="" map_line="" pid="" surface="" x="" y="" width="" height=""
  for _ in $(seq 1 45); do
    ready="$(grep 'WATCH_POINTER_READY ' "$log" 2>/dev/null | tail -n 1 || true)"
    if [[ -n "$ready" ]]; then
      pid="$(field "$ready" pid || true)"
      surface="$(field "$ready" surface || true)"
      map_line="$(map_for_identity "$pid" "$surface" || true)"
      if [[ -n "$map_line" ]]; then
        x="$(field "$map_line" x || true)"
        y="$(field "$map_line" y || true)"
        width="$(field "$map_line" width || true)"
        height="$(field "$map_line" height || true)"
        break
      fi
    fi
    sleep 1
  done
  if [[ ! "$pid" =~ ^[0-9]+$ || ! "$surface" =~ ^[0-9]+$ ||
        ! "$x" =~ ^-?[0-9]+$ || ! "$y" =~ ^-?[0-9]+$ ||
        ! "$width" =~ ^[0-9]+$ || ! "$height" =~ ^[0-9]+$ ]]; then
    printf 'WATCH_POINTER_TARGET_FAILED ready=%s map=%s\n' \
      "${ready:-missing}" "${map_line:-missing}"
    local app_id="seyrix.qemu.$run_id.pointer-watch"
    local cleanup_pid
    cleanup_pid="$(find_probe_pid "$app_id" || true)"
    if [[ "$cleanup_pid" =~ ^[0-9]+$ ]] &&
        probe_identity_matches "$cleanup_pid" "$app_id" &&
        process_has_arg "$cleanup_pid" --watch-pointer; then
      printf 'WATCH_POINTER_FAILED_START_CLEANUP pid=%s app_id=%s\n' \
        "$cleanup_pid" "$app_id"
      kill -TERM "$cleanup_pid" 2>/dev/null || true
      for _ in $(seq 1 12); do
        probe_identity_matches "$cleanup_pid" "$app_id" || break
        sleep 0.25
      done
      if probe_identity_matches "$cleanup_pid" "$app_id" &&
          process_has_arg "$cleanup_pid" --watch-pointer; then
        printf 'WATCH_POINTER_FAILED_START_ESCALATION pid=%s app_id=%s\n' \
          "$cleanup_pid" "$app_id"
        kill -KILL "$cleanup_pid" 2>/dev/null || true
      fi
    fi
    return 1
  fi
  printf '%s %s\n' "$pid" "$surface" > /tmp/seyrix-watch-pointer.identity
  local client_x=$((x + 60)) client_y=$((y + 26 + 60))
  printf 'WATCH_POINTER_TARGET_READY pid=%s surface=%s scene=%s,%s size=%sx%s qmp=%s,%s\n' \
    "$pid" "$surface" "$x" "$y" "$width" "$height" \
    "$(qmp_coordinate "$client_x" 640)" "$(qmp_coordinate "$client_y" 480)"
}

watch_baseline() {
  local log=/tmp/seyrix-watch-pointer.log
  local enters leaves motions buttons marker="${1:-WATCH_POINTER_ACTIVE_BASELINE}"
  enters="$(grep -c 'WATCH_POINTER_ENTER count=' "$log" || true)"
  leaves="$(grep -c 'WATCH_POINTER_LEAVE ' "$log" || true)"
  motions="$(grep -c 'WATCH_POINTER_MOTION count=' "$log" || true)"
  buttons="$(grep -c 'WATCH_POINTER_BUTTON count=' "$log" || true)"
  if (( buttons < 1 )); then
    printf 'WATCH_POINTER_ASSERTION_FAILED reason=no-active-workspace-button-callback\n'
    tail -n 20 "$log" || true
    return 1
  fi
  printf '%s %s %s %s\n' "$enters" "$leaves" "$motions" "$buttons" \
    > /tmp/seyrix-watch-pointer.baseline
  printf '%s=passed enter=%s leave=%s motion=%s button=%s\n' \
    "$marker" "$enters" "$leaves" "$motions" "$buttons"
}

watch_check_hidden() {
  local log=/tmp/seyrix-watch-pointer.log
  local before_enter before_leave before_motion before_button
  local after_enter after_leave after_motion after_button
  read -r before_enter before_leave before_motion before_button < /tmp/seyrix-watch-pointer.baseline
  after_enter="$(grep -c 'WATCH_POINTER_ENTER count=' "$log" || true)"
  after_leave="$(grep -c 'WATCH_POINTER_LEAVE ' "$log" || true)"
  after_motion="$(grep -c 'WATCH_POINTER_MOTION count=' "$log" || true)"
  after_button="$(grep -c 'WATCH_POINTER_BUTTON count=' "$log" || true)"
  printf 'WATCH_POINTER_COUNTS_BEFORE=%s,%s,%s,%s AFTER=%s,%s,%s,%s\n' \
    "$before_enter" "$before_leave" "$before_motion" "$before_button" \
    "$after_enter" "$after_leave" "$after_motion" "$after_button"
  if [[ "$before_enter" == "$after_enter" && "$before_leave" == "$after_leave" &&
        "$before_motion" == "$after_motion" &&
        "$before_button" == "$after_button" ]]; then
    echo WATCH_POINTER_HIDDEN_ASSERTION=passed
    return 0
  fi
  echo WATCH_POINTER_HIDDEN_ASSERTION=failed
  tail -n 24 "$log" || true
  return 1
}

watch_stop() {
  local log=/tmp/seyrix-watch-pointer.log pid surface run_id app_id
  read -r pid surface < /tmp/seyrix-watch-pointer.identity
  run_id="$(test_run_id)"
  app_id="seyrix.qemu.$run_id.pointer-watch"
  if ! probe_identity_matches "$pid" "$app_id" ||
      ! process_has_arg "$pid" --watch-pointer; then
    fail "watch-client-identity-mismatch-pid-$pid-surface-$surface"
  fi
  kill -TERM "$pid"
  for _ in $(seq 1 15); do
    grep -q 'WATCH_POINTER_STOPPED=1' "$log" && break
    sleep 1
  done
  if ! grep -q 'WATCH_POINTER_STOPPED=1' "$log"; then
    if probe_identity_matches "$pid" "$app_id" &&
        process_has_arg "$pid" --watch-pointer; then
      printf 'WATCH_POINTER_STOP_TIMEOUT_CLEANUP pid=%s app_id=%s\n' "$pid" "$app_id"
      kill -KILL "$pid" 2>/dev/null || true
      for _ in $(seq 1 12); do
        probe_identity_matches "$pid" "$app_id" || break
        sleep 0.25
      done
    fi
    fail "watch-client-stop-timeout-pid-$pid-surface-$surface"
  fi
  echo WATCH_POINTER_STOPPED=passed
}

wm_snapshot() {
  local label="$1" expected_count="$2" require_disjoint="${3:-0}"
  local visibility="${4:-contained}"
  local -a pids=() rows=()
  local -a geometry_lines=()
  local log_line output_line output_width output_height path ready
  local pid surface x y width height
  if [[ "$visibility" != contained && "$visibility" != visible ]]; then
    fail "invalid-window-visibility-policy-$visibility"
  fi
  for _ in $(seq 1 30); do
    mapfile -t pids < <(pgrep -u 1000 -x weston-terminal | sort -n || true)
    geometry_lines=()
    if [[ "${#pids[@]}" -eq "$expected_count" ]]; then
      for pid in "${pids[@]}"; do
        log_line="$(grep -F "GEOMETRY_EVIDENCE event=commit pid=$pid " \
          "$geometry_log" | tail -n 1 || true)"
        geometry_lines+=("$log_line")
      done
      if (( ${#geometry_lines[@]} == expected_count )); then
        ready=1
        for log_line in "${geometry_lines[@]}"; do
          [[ -n "$log_line" ]] || ready=0
        done
        if (( ready )); then break; fi
      fi
    fi
    sleep 0.5
  done
  if [[ "${#pids[@]}" -ne "$expected_count" ]]; then
    printf 'WM_GEOMETRY_FAILED expected_processes=%s actual_processes=%s\n' \
      "$expected_count" "${#pids[@]}"
    return 1
  fi
  output_line="$(grep -E 'Output .* active at [0-9]+x[0-9]+' "$geometry_log" | tail -n 1)"
  output_width="$(sed -nE 's/.* active at ([0-9]+)x([0-9]+).*/\1/p' <<< "$output_line")"
  output_height="$(sed -nE 's/.* active at ([0-9]+)x([0-9]+).*/\2/p' <<< "$output_line")"
  if [[ ! "$output_width" =~ ^[0-9]+$ || ! "$output_height" =~ ^[0-9]+$ ]]; then
    printf 'WM_GEOMETRY_FAILED reason=output-size-unavailable line=%s\n' \
      "${output_line:-missing}"
    return 1
  fi
  path="/tmp/seyrix-wm-$label.geometry"
  : > "$path"
  for index in "${!pids[@]}"; do
    pid="${pids[$index]}"
    log_line="${geometry_lines[$index]}"
    surface="$(field "$log_line" surface || true)"
    x="$(field "$log_line" x || true)"
    y="$(field "$log_line" y || true)"
    width="$(field "$log_line" width || true)"
    height="$(field "$log_line" height || true)"
    if [[ ! "$surface" =~ ^[0-9]+$ || ! "$x" =~ ^-?[0-9]+$ ||
          ! "$y" =~ ^-?[0-9]+$ || ! "$width" =~ ^[1-9][0-9]*$ ||
          ! "$height" =~ ^[1-9][0-9]*$ ]]; then
      printf 'WM_GEOMETRY_FAILED pid=%s surface=%s rect=%s,%s %sx%s output=%sx%s\n' \
        "$pid" "${surface:-missing}" "${x:-missing}" "${y:-missing}" \
        "${width:-missing}" "${height:-missing}" "$output_width" "$output_height"
      return 1
    fi
    if [[ "$visibility" == contained ]]; then
      if (( x < 0 || y < 0 || x + width > output_width ||
          y + height > output_height )); then
        printf 'WM_GEOMETRY_FAILED pid=%s surface=%s rect=%s,%s %sx%s output=%sx%s policy=contained\n' \
          "$pid" "$surface" "$x" "$y" "$width" "$height" \
          "$output_width" "$output_height"
        return 1
      fi
    else
      local visible_width visible_height
      visible_width=$(( (x + width < output_width ? x + width : output_width) -
        (x > 0 ? x : 0) ))
      visible_height=$(( (y + height < output_height ? y + height : output_height) -
        (y > 0 ? y : 0) ))
      if (( visible_width < (width < 64 ? width : 64) ||
          visible_height < (height < 64 ? height : 64) )); then
        printf 'WM_GEOMETRY_FAILED pid=%s surface=%s rect=%s,%s %sx%s output=%sx%s visible=%sx%s\n' \
          "$pid" "$surface" "$x" "$y" "$width" "$height" \
          "$output_width" "$output_height" "$visible_width" "$visible_height"
        return 1
      fi
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$pid" "$surface" "$x" "$y" "$width" "$height" >> "$path"
    printf 'WM_GEOMETRY_SAMPLE label=%s pid=%s surface=%s rect=%s,%s %sx%s\n' \
      "$label" "$pid" "$surface" "$x" "$y" "$width" "$height"
  done
  if [[ "$require_disjoint" == 1 ]]; then
    mapfile -t rows < "$path"
    local i j pid_a surface_a x_a y_a w_a h_a pid_b surface_b x_b y_b w_b h_b
    for ((i = 0; i < ${#rows[@]}; i++)); do
      IFS=$'\t' read -r pid_a surface_a x_a y_a w_a h_a <<< "${rows[$i]}"
      for ((j = i + 1; j < ${#rows[@]}; j++)); do
        IFS=$'\t' read -r pid_b surface_b x_b y_b w_b h_b <<< "${rows[$j]}"
        if ! (( x_a + w_a <= x_b || x_b + w_b <= x_a ||
                y_a + h_a <= y_b || y_b + h_b <= y_a )); then
          printf 'WM_GEOMETRY_FAILED reason=overlap pid_a=%s pid_b=%s\n' \
            "$pid_a" "$pid_b"
          return 1
        fi
      done
    done
    printf 'WM_TILED_NONOVERLAP=passed count=%s\n' "${#rows[@]}"
  fi
  printf 'WM_GEOMETRY_SNAPSHOT=passed label=%s count=%s\n' "$label" "$expected_count"
}

focus_target() {
  local expected_count="${1:-2}" focused_line focused_pid target_pid=""
  local output_line output_width output_height geometry_line
  local x y width height point_x point_y qmp_x qmp_y focus_before button_before
  local -a pids=()

  mapfile -t pids < <(pgrep -u 1000 -x weston-terminal | sort -n || true)
  if [[ "${#pids[@]}" -ne "$expected_count" ]]; then
    printf 'WINDOW_FOCUS_TARGET_FAILED reason=process-count expected=%s actual=%s\n' \
      "$expected_count" "${#pids[@]}"
    return 1
  fi

  focused_line="$(grep -E 'Window focused: pid=[0-9]+' "$geometry_log" | tail -n 1 || true)"
  focused_pid="$(sed -nE 's/.*Window focused: pid=([0-9]+).*/\1/p' <<< "$focused_line")"
  if [[ ! "$focused_pid" =~ ^[0-9]+$ ]]; then
    printf 'WINDOW_FOCUS_TARGET_FAILED reason=current-focus-unavailable line=%s\n' \
      "${focused_line:-missing}"
    return 1
  fi
  for pid in "${pids[@]}"; do
    if [[ "$pid" != "$focused_pid" ]]; then
      target_pid="$pid"
      break
    fi
  done
  if [[ -z "$target_pid" ]]; then
    printf 'WINDOW_FOCUS_TARGET_FAILED reason=unfocused-window-unavailable focused_pid=%s\n' \
      "$focused_pid"
    return 1
  fi

  if ! wm_snapshot focus-pointer "$expected_count" 1 visible; then
    printf 'WINDOW_FOCUS_TARGET_FAILED reason=visible-geometry-unavailable\n'
    return 1
  fi
  output_line="$(grep -E 'Output .* active at [0-9]+x[0-9]+' "$geometry_log" | tail -n 1)"
  output_width="$(sed -nE 's/.* active at ([0-9]+)x([0-9]+).*/\1/p' <<< "$output_line")"
  output_height="$(sed -nE 's/.* active at ([0-9]+)x([0-9]+).*/\2/p' <<< "$output_line")"
  geometry_line="$(grep -F "GEOMETRY_EVIDENCE event=commit pid=$target_pid " \
    "$geometry_log" | tail -n 1 || true)"
  x="$(field "$geometry_line" x || true)"
  y="$(field "$geometry_line" y || true)"
  width="$(field "$geometry_line" width || true)"
  height="$(field "$geometry_line" height || true)"
  if [[ ! "$output_width" =~ ^[1-9][0-9]*$ ||
        ! "$output_height" =~ ^[1-9][0-9]*$ ||
        ! "$x" =~ ^-?[0-9]+$ || ! "$y" =~ ^-?[0-9]+$ ||
        ! "$width" =~ ^[1-9][0-9]*$ || ! "$height" =~ ^[1-9][0-9]*$ ]]; then
    printf 'WINDOW_FOCUS_TARGET_FAILED reason=malformed-geometry pid=%s line=%s output=%s\n' \
      "$target_pid" "${geometry_line:-missing}" "${output_line:-missing}"
    return 1
  fi
  point_x=$((x + width / 2))
  point_y=$((y + height / 2))
  if (( point_x < 0 || point_x >= output_width ||
        point_y < 0 || point_y >= output_height )); then
    printf 'WINDOW_FOCUS_TARGET_FAILED reason=target-center-outside-output pid=%s point=%s,%s output=%sx%s\n' \
      "$target_pid" "$point_x" "$point_y" "$output_width" "$output_height"
    return 1
  fi

  qmp_x="$(qmp_coordinate "$point_x" "$output_width")"
  qmp_y="$(qmp_coordinate "$point_y" "$output_height")"
  focus_before="$(grep -Fc "Window focused: pid=$target_pid " "$geometry_log" || true)"
  button_before="$(grep -Fc 'Pointer button press button=272' "$geometry_log" || true)"
  printf 'WINDOW_FOCUS_TARGET_READY pid=%s qmp=%s,%s focus_before=%s button_before=%s\n' \
    "$target_pid" "$qmp_x" "$qmp_y" "$focus_before" "$button_before"
}

move_baseline() {
  local map_line commit_line output_line pid surface x y width height
  local output_width output_height start_x start_y end_x end_y
  local qmp_start_x qmp_start_y qmp_end_x qmp_end_y
  local end_before button_before alt_before gesture_before classification_before
  map_line="$(grep -E 'GEOMETRY_EVIDENCE event=map pid=[0-9]+ surface=[0-9]+' \
    "$geometry_log" | tail -n 1 || true)"
  pid="$(field "$map_line" pid || true)"
  surface="$(field "$map_line" surface || true)"
  if [[ ! "$pid" =~ ^[0-9]+$ || ! "$surface" =~ ^[0-9]+$ ]]; then
    printf 'WINDOW_MOVE_BASELINE=failed reason=latest-mapped-window-unavailable\n'
    return 1
  fi
  commit_line="$(grep -F "GEOMETRY_EVIDENCE event=commit pid=$pid surface=$surface " \
    "$geometry_log" | tail -n 1 || true)"
  x="$(field "$commit_line" x || true)"
  y="$(field "$commit_line" y || true)"
  width="$(field "$commit_line" width || true)"
  height="$(field "$commit_line" height || true)"
  output_line="$(grep -E 'Output .* active at [0-9]+x[0-9]+' \
    "$geometry_log" | tail -n 1 || true)"
  output_width="$(sed -nE 's/.* active at ([0-9]+)x([0-9]+).*/\1/p' \
    <<< "$output_line")"
  output_height="$(sed -nE 's/.* active at ([0-9]+)x([0-9]+).*/\2/p' \
    <<< "$output_line")"
  if [[ ! "$x" =~ ^-?[0-9]+$ || ! "$y" =~ ^-?[0-9]+$ ||
        ! "$width" =~ ^[1-9][0-9]*$ || ! "$height" =~ ^[1-9][0-9]*$ ||
        ! "$output_width" =~ ^[1-9][0-9]*$ ||
        ! "$output_height" =~ ^[1-9][0-9]*$ ]]; then
    printf 'WINDOW_MOVE_BASELINE=failed reason=committed-geometry-unavailable pid=%s surface=%s\n' \
      "$pid" "$surface"
    return 1
  fi
  start_x=$((x + width / 2))
  start_y=$((y + height / 2))
  end_x=$((start_x + 40))
  end_y=$((start_y + 35))
  if (( end_x >= output_width )); then end_x=$((start_x - 40)); fi
  if (( end_y >= output_height )); then end_y=$((start_y - 35)); fi
  if (( start_x < 0 || start_x >= output_width || start_y < 0 ||
        start_y >= output_height || end_x < 0 || end_y < 0 )); then
    printf 'WINDOW_MOVE_BASELINE=failed reason=window-center-outside-output pid=%s rect=%s,%s %sx%s output=%sx%s\n' \
      "$pid" "$x" "$y" "$width" "$height" "$output_width" "$output_height"
    return 1
  fi
  qmp_start_x=$((start_x * 32767 / output_width))
  qmp_start_y=$((start_y * 32767 / output_height))
  qmp_end_x=$((end_x * 32767 / output_width))
  qmp_end_y=$((end_y * 32767 / output_height))
  end_before="$(grep -Fc "GEOMETRY_EVIDENCE event=grab-end pid=$pid surface=$surface " \
    "$geometry_log" || true)"
  button_before="$(grep -Fc 'Pointer button press button=272' \
    "$geometry_log" || true)"
  alt_before="$(grep -Fc 'Alt pointer gesture button=272' \
    "$geometry_log" || true)"
  gesture_before="$(grep -Fc 'Window move gesture started' \
    "$geometry_log" || true)"
  classification_before="$(grep -Fc 'Focused window classification changed to floating' \
    "$geometry_log" || true)"
  printf '%s %s %s %s %s %s %s %s %s\n' \
    "$pid" "$surface" "$x" "$y" "$end_before" "$button_before" \
    "$alt_before" "$gesture_before" "$classification_before" \
    > /tmp/seyrix-window-move-before
  printf 'WINDOW_MOVE_TARGET_PID=%s\nWINDOW_MOVE_TARGET_SURFACE=%s\n' \
    "$pid" "$surface"
  printf 'WINDOW_MOVE_INITIAL_SCENE=%s,%s SIZE=%sx%s OUTPUT=%sx%s\n' \
    "$x" "$y" "$width" "$height" "$output_width" "$output_height"
  printf 'WINDOW_MOVE_BASELINE=ready qmp_start=%s,%s qmp_end=%s,%s\n' \
    "$qmp_start_x" "$qmp_start_y" "$qmp_end_x" "$qmp_end_y"
}

wm_same_identity() {
  local first="/tmp/seyrix-wm-$1.geometry" second="/tmp/seyrix-wm-$2.geometry"
  if cmp -s <(cut -f1,2 "$first") <(cut -f1,2 "$second"); then
    printf 'WM_SURFACE_IDENTITY=preserved before=%s after=%s\n' "$1" "$2"
  else
    printf 'WM_SURFACE_IDENTITY=failed before=%s after=%s\n' "$1" "$2"
    return 1
  fi
}

wm_same_geometry() {
  local first="/tmp/seyrix-wm-$1.geometry" second="/tmp/seyrix-wm-$2.geometry"
  if cmp -s <(cut -f3-6 "$first") <(cut -f3-6 "$second"); then
    printf 'WM_GEOMETRY_RESTORED=passed before=%s after=%s\n' "$1" "$2"
  else
    printf 'WM_GEOMETRY_RESTORED=failed before=%s after=%s\n' "$1" "$2"
    return 1
  fi
}

wm_geometry_changed() {
  local first="/tmp/seyrix-wm-$1.geometry" second="/tmp/seyrix-wm-$2.geometry"
  if ! cmp -s <(cut -f5-6 "$first") <(cut -f5-6 "$second"); then
    printf 'WM_GEOMETRY_CHANGED=passed before=%s after=%s\n' "$1" "$2"
  else
    printf 'WM_GEOMETRY_CHANGED=failed before=%s after=%s\n' "$1" "$2"
    return 1
  fi
}

popup_process_alive() {
  local run_id app_id pid
  run_id="$(test_run_id)"
  app_id="seyrix.qemu.$run_id.popup"
  pid="$(find_probe_pid "$app_id" || true)"
  if [[ "$pid" =~ ^[0-9]+$ ]] && probe_identity_matches "$pid" "$app_id"; then
    printf 'POPUP_PROCESS_ALIVE=passed pid=%s app_id=%s\n' "$pid" "$app_id"
    return 0
  fi
  printf 'POPUP_PROCESS_ALIVE=failed app_id=%s\n' "$app_id"
  return 1
}

settings_manager_pid() {
  local pid_file="$runtime_dir/seyrix-settings-supervisor.pid"
  [[ -r "$pid_file" ]] || return 1
  cat "$pid_file"
}

settings_category_id() {
  case "$1" in
    0|appearance) echo appearance ;;
    1|desktop) echo desktop ;;
    2|windows) echo windows ;;
    3|displays) echo displays ;;
    4|input) echo input ;;
    5|about) echo about ;;
    *) return 1 ;;
  esac
}

settings_boolean() {
  case "${1,,}" in
    1|true|yes) echo 1 ;;
    0|false|no) echo 0 ;;
    *) return 1 ;;
  esac
}

settings_recovery_baseline() {
  local pid start manager manager_start manager_exe manager_args manager_sid manager_pgid
  local compositor compositor_start compositor_sid compositor_pgid parent process_count marker state_saved_marker state_evidence log_lines
  local state_file state_section category width height maximized paned_position saved_category state_saved_original_marker
  local diagnostic_saved_category diagnostic_saved_width diagnostic_saved_height diagnostic_saved_maximized diagnostic_saved_paned
  local diagnostic_marker_category diagnostic_marker_width diagnostic_marker_height diagnostic_marker_maximized diagnostic_marker_paned
  manager="$(settings_manager_pid)"
  compositor="$(pgrep -u 1000 -f '^/usr/bin/seyrix-compositor$' | head -n 1 || true)"
  compositor_start="$(awk '{print $22}' "/proc/$compositor/stat" 2>/dev/null || true)"
  pid="$(pgrep -u 1000 -x seyrix-settings | head -n 1 || true)"
  process_count="$(pgrep -u 1000 -x seyrix-settings | wc -l)"
  start="$(awk '{print $22}' "/proc/$pid/stat" 2>/dev/null || true)"
  manager_start="$(awk '{print $22}' "/proc/$manager/stat" 2>/dev/null || true)"
  manager_exe="$(readlink -f "/proc/$manager/exe" 2>/dev/null || true)"
  manager_args="$(tr '\0' ' ' <"/proc/$manager/cmdline" 2>/dev/null || true)"
  manager_sid="$(ps -o sid= -p "$manager" 2>/dev/null | tr -d ' ')"
  manager_pgid="$(ps -o pgid= -p "$manager" 2>/dev/null | tr -d ' ')"
  compositor_sid="$(ps -o sid= -p "$compositor" 2>/dev/null | tr -d ' ')"
  compositor_pgid="$(ps -o pgid= -p "$compositor" 2>/dev/null | tr -d ' ')"
  parent="$(awk '/^PPid:/ {print $2}' "/proc/$pid/status" 2>/dev/null || true)"
  marker="$(grep -F "SEYRIX_SETTINGS event=start pid=$pid " "$compositor_log" 2>/dev/null | tail -n 1 || true)"
  [[ "$pid" =~ ^[1-9][0-9]*$ && "$start" =~ ^[0-9]+$ &&
     "$manager" =~ ^[1-9][0-9]*$ && "$manager_start" =~ ^[0-9]+$ &&
     "$manager_exe" == /usr/bin/seyrix-session-supervisor &&
     "$manager_args" == *--serve* && "$manager_sid" == "$compositor_sid" &&
     "$manager_pgid" == "$manager" && "$compositor_pgid" =~ ^[0-9]+$ && "$manager_pgid" != "$compositor_pgid" &&
     "$parent" == "$manager" && "$process_count" == 1 && -n "$marker" ]] ||
    fail "settings-supervisor-baseline-invalid-settings-$pid-manager-$manager-count-$process_count"
  state_file="${XDG_STATE_HOME:-/home/seyrix/.local/state}/seyrix/settings.ini"
  [[ -s "$state_file" ]] || fail "settings-state-file-missing-or-empty"
  for _ in $(seq 1 40); do
    state_section="$(sed -n '/^\[window\][[:space:]]*$/,/^\[/ { /^\[/d; p; }' "$state_file")"
    category="$(sed -n 's/^[[:space:]]*category[[:space:]]*=[[:space:]]*//p' <<<"$state_section" | tail -n 1 | tr -d '\r')"
    width="$(sed -n 's/^[[:space:]]*width[[:space:]]*=[[:space:]]*//p' <<<"$state_section" | tail -n 1 | tr -d '\r')"
    height="$(sed -n 's/^[[:space:]]*height[[:space:]]*=[[:space:]]*//p' <<<"$state_section" | tail -n 1 | tr -d '\r')"
    maximized="$(sed -n 's/^[[:space:]]*maximized[[:space:]]*=[[:space:]]*//p' <<<"$state_section" | tail -n 1 | tr -d '\r' | tr '[:upper:]' '[:lower:]')"
    paned_position="$(sed -n 's/^[[:space:]]*paned-position[[:space:]]*=[[:space:]]*//p' <<<"$state_section" | tail -n 1 | tr -d '\r')"
    category="$(settings_category_id "$category" 2>/dev/null || echo unknown)"
    maximized="$(settings_boolean "$maximized" 2>/dev/null || echo unknown)"
    state_saved_marker="$(grep -F "SEYRIX_SETTINGS event=state-saved pid=$pid " "$compositor_log" 2>/dev/null | tail -n 1 || true)"
    state_saved_original_marker="$state_saved_marker"
    saved_category="$(settings_category_id "$(field "$state_saved_marker" category || true)" 2>/dev/null || echo unknown)"
    state_evidence=state-saved
    if [[ "$saved_category" != "$category" ||
          "$(field "$state_saved_marker" width || true)" != "$width" ||
          "$(field "$state_saved_marker" height || true)" != "$height" ||
          "$(settings_boolean "$(field "$state_saved_marker" maximized || true)" 2>/dev/null || echo unknown)" != "$maximized" ||
          "$(field "$state_saved_marker" paned_position || true)" != "$paned_position" ]]; then
      state_saved_marker="$marker"
      state_evidence=start
      saved_category="$(settings_category_id "$(field "$state_saved_marker" category || true)" 2>/dev/null || echo unknown)"
    fi
    [[ "$category" != unknown && "$saved_category" == "$category" &&
       "$(field "$state_saved_marker" width || true)" == "$width" &&
       "$(field "$state_saved_marker" height || true)" == "$height" &&
       "$(settings_boolean "$(field "$state_saved_marker" maximized || true)" 2>/dev/null || echo unknown)" == "$maximized" &&
       "$(field "$state_saved_marker" paned_position || true)" == "$paned_position" ]] && break
    sleep 0.25
  done
  if [[ ! "$width" =~ ^[0-9]+$ || ! "$height" =~ ^[0-9]+$ ||
        ! "$paned_position" =~ ^[0-9]+$ || "$saved_category" != "$category" ]]; then
    diagnostic_saved_category="$(field "$state_saved_original_marker" category || true)"
    diagnostic_saved_width="$(field "$state_saved_original_marker" width || true)"
    diagnostic_saved_height="$(field "$state_saved_original_marker" height || true)"
    diagnostic_saved_maximized="$(field "$state_saved_original_marker" maximized || true)"
    diagnostic_saved_paned="$(field "$state_saved_original_marker" paned_position || true)"
    diagnostic_marker_category="$(field "$state_saved_marker" category || true)"
    diagnostic_marker_width="$(field "$state_saved_marker" width || true)"
    diagnostic_marker_height="$(field "$state_saved_marker" height || true)"
    diagnostic_marker_maximized="$(field "$state_saved_marker" maximized || true)"
    diagnostic_marker_paned="$(field "$state_saved_marker" paned_position || true)"
    printf 'SETTINGS_SUPERVISOR_BASELINE_STATE_DIAGNOSTIC pid=%s category=%s width=%s height=%s maximized=%s paned_position=%s saved_category=%s state_evidence=%s state_saved_marker_category=%s state_saved_marker_width=%s state_saved_marker_height=%s state_saved_marker_maximized=%s state_saved_marker_paned_position=%s selected_marker_category=%s selected_marker_width=%s selected_marker_height=%s selected_marker_maximized=%s selected_marker_paned_position=%s\n' \
      "$pid" "$category" "$width" "$height" "$maximized" "$paned_position" "$saved_category" "$state_evidence" \
      "${diagnostic_saved_category:-missing}" "${diagnostic_saved_width:-missing}" "${diagnostic_saved_height:-missing}" \
      "${diagnostic_saved_maximized:-missing}" "${diagnostic_saved_paned:-missing}" \
      "${diagnostic_marker_category:-missing}" "${diagnostic_marker_width:-missing}" "${diagnostic_marker_height:-missing}" \
      "${diagnostic_marker_maximized:-missing}" "${diagnostic_marker_paned:-missing}"
    fail "settings-state-window-fields-invalid"
  fi
  log_lines="$(wc -l <"$compositor_log")"
  printf '%s %s %s %s %s %s %s %s %s %s %s %s %s %s\n' "$pid" "$start" "$manager" \
    "$manager_start" "$category" "$width" "$height" "$maximized" \
    "$paned_position" "$log_lines" "$compositor" "$compositor_start" \
    "$manager_pgid" "$compositor_pgid" \
    >/tmp/seyrix-settings-supervisor-before
  printf 'SETTINGS_SUPERVISOR_BASELINE manager_pid=%s manager_start=%s manager_pgid=%s compositor_pid=%s compositor_start=%s compositor_pgid=%s settings_pid=%s settings_start=%s category=%s width=%s height=%s maximized=%s paned_position=%s state_evidence=%s log_lines=%s state_file=%s\n' \
    "$manager" "$manager_start" "$manager_pgid" "$compositor" "$compositor_start" "$compositor_pgid" "$pid" "$start" "$category" "$width" \
    "$height" "$maximized" "$paned_position" "$state_evidence" \
    "$log_lines" "$state_file"
  echo SETTINGS_SUPERVISOR_BASELINE=passed
}

settings_wayland_trace_export() {
  local pid="$1" path="$runtime_dir/seyrix-settings-wayland-trace.log"
  local owner mode bytes digest last_byte
  [[ "$pid" =~ ^[1-9][0-9]*$ && -f "$path" && ! -L "$path" ]] || {
    echo 'SETTINGS_WAYLAND_TRACE_EXPORT=failed reason=missing-or-invalid-path'
    return 1
  }
  read -r owner mode bytes < <(stat -c '%u %a %s' "$path")
  last_byte="$(tail -c 1 "$path" | od -An -t x1 | tr -d ' \n')"
  digest="$(sha256sum "$path" | awk '{print $1}')"
  if [[ "$owner" != 1000 || "$mode" != 600 || ! "$bytes" =~ ^[1-9][0-9]*$ ||
        "$last_byte" != 0a || ! "$digest" =~ ^[0-9a-f]{64}$ ]] ||
      ! grep -Fq "source=client-stderr pid=$pid " "$path" ||
      ! grep -Eq "source=client-stderr pid=$pid .*wl_display" "$path" ||
      ! grep -Eq "source=client-stderr pid=$pid .*wl_surface" "$path"; then
    echo 'SETTINGS_WAYLAND_TRACE_EXPORT=failed reason=integrity-or-attribution'
    return 1
  fi
  printf '\nSETTINGS_WAYLAND_TRACE_EXPORT_BEGIN pid=%s bytes=%s sha256=%s\n' \
    "$pid" "$bytes" "$digest"
  base64 -w 76 "$path"
  printf 'SETTINGS_WAYLAND_TRACE_EXPORT_END pid=%s\n' "$pid"
}

settings_recovery_verify() {
  local expected=${1:-compositor} before_pid before_start before_manager before_manager_start
  local before_category before_width before_height before_maximized before_paned_position log_baseline
  local before_compositor before_compositor_start before_manager_pgid before_compositor_pgid manager manager_start manager_pgid pid count start parent compositor compositor_start compositor_pgid compositor_sid sid
  local fresh app_start launch_marker ready_marker attempt_marker exit_marker app_exit_marker
  local trace_fd trace_pid stream_line stream_lines deadline recovery_wait_complete settings_pids
  local wait_frames wait_hello wait_snapshot_request wait_subscribe_request wait_snapshot_response wait_subscribe_response
  local diagnostic_pids diagnostic_display_lost diagnostic_supervisor_exit diagnostic_attempt diagnostic_ready
  local diagnostic_launch diagnostic_app_start diagnostic_accepted diagnostic_frames diagnostic_hello
  local diagnostic_snapshot_request diagnostic_snapshot_response diagnostic_subscribe_request diagnostic_subscribe_response
  local diagnostic_mapped diagnostic_compositor diagnostic_processes diagnostic_socket diagnostic_socket_status diagnostic_socket
  local diagnostic_wchan diagnostic_syscall diagnostic_fd diagnostic_fds diagnostic_stderr
  local diagnostic_compositor_stderr diagnostic_manager_stderr
  local old_start_now new_category new_width new_height new_maximized new_paned_position state_restored
  local launch_line ready_line attempt_line app_line exit_line exit_status exit_signal expected_status state_file
  local attempt_number ready_attempt launch_attempt ready_display
  local server_lines trace_export
  local accepted frames hello snapshot_request subscribe_request snapshot_response snapshot_status snapshot_to
  local subscribe_response snapshot_revision subscribe_revision subscribe_status subscribe_to mapped
  read -r before_pid before_start before_manager before_manager_start before_category \
    before_width before_height before_maximized before_paned_position log_baseline \
    before_compositor before_compositor_start before_manager_pgid before_compositor_pgid \
    </tmp/seyrix-settings-supervisor-before || fail "settings-supervisor-baseline-unreadable"
  case "$expected" in compositor) expected_status='^(1|75)$' ;; process) expected_status='^137$' ;; *) fail "unknown-settings-recovery-mode-$expected" ;; esac
  manager=""
  pid=""
  count=0
  fresh=""
  stream_lines=0
  recovery_wait_complete=0
  exec {trace_fd}< <(tail -n +$((log_baseline + 1)) -F "$compositor_log")
  trace_pid=$!
  deadline=$((SECONDS + 150))
  while (( SECONDS < deadline )); do
    if IFS= read -r -t 0.5 -u "$trace_fd" stream_line; then
      case "$stream_line" in
        *"SEYRIX_SETTINGS_SUPERVISOR event="*|*"SEYRIX_SETTINGS event="*|*"SEYRIX_SESSION event=compositor-exit "*|*"Settings endpoint "*|*"Window mapped: pid="*)
          fresh+="$stream_line"$'\n'
          stream_lines=$((stream_lines + 1))
          ;;
      esac
    fi
    manager="$(settings_manager_pid 2>/dev/null || true)"
    settings_pids="$(pgrep -u 1000 -x seyrix-settings 2>/dev/null || true)"
    count="$(awk 'NF {n++} END {print n+0}' <<<"$settings_pids")"
    pid="$(head -n 1 <<<"$settings_pids" || true)"
    if [[ "$pid" =~ ^[1-9][0-9]*$ && "$pid" != "$before_pid" && "$count" == 1 ]] && \
        grep -Fq "SEYRIX_SETTINGS event=start pid=$pid " <<<"$fresh" && \
        grep -Fq "SEYRIX_SETTINGS_SUPERVISOR event=launch pid=$pid" <<<"$fresh"; then
      server_lines="$(grep -E 'Settings endpoint (accepted peer|watch ready|dispatch|read|frame|request-dispatch|sent peer_pid)' "$compositor_log" | grep -F "pid=$pid " || true)"
      wait_frames="$(grep -F "Settings endpoint frame peer_pid=$pid " <<<"$server_lines" || true)"
      wait_hello=0
      grep -Eq 'opcode=1 .*negotiated=0' <<<"$wait_frames" && wait_hello=1 || true
      wait_snapshot_request="$(sed -nE 's/.*opcode=2 request=([0-9]+) .*/\1/p' <<<"$wait_frames" | head -n 1)"
      wait_subscribe_request="$(sed -nE 's/.*opcode=3 request=([0-9]+) .*/\1/p' <<<"$wait_frames" | head -n 1)"
      wait_snapshot_response="$(grep -F "Settings endpoint sent peer_pid=$pid opcode=32768 request=$wait_snapshot_request " <<<"$server_lines" | tail -n 1 || true)"
      wait_subscribe_response="$(grep -F "Settings endpoint sent peer_pid=$pid opcode=32768 request=$wait_subscribe_request " <<<"$server_lines" | tail -n 1 || true)"
      accepted="$(grep -F "Settings endpoint accepted peer uid=1000 pid=$pid " <<<"$server_lines" | tail -n 1 || true)"
      mapped="$(grep -F "Window mapped: pid=$pid " <<<"$fresh" | grep -F 'app_id=org.seyrix.settings title=System Settings' | tail -n 1 || true)"
      if [[ "$wait_hello" == 1 && -n "$wait_snapshot_request" &&
            -n "$wait_subscribe_request" && -n "$wait_snapshot_response" &&
            -n "$wait_subscribe_response" && -n "$accepted" && -n "$mapped" ]]; then
        recovery_wait_complete=1
        break
      fi
    fi
  done
  if [[ "$recovery_wait_complete" != 1 ]]; then
    diagnostic_display_lost="$(grep -F "SEYRIX_SETTINGS event=display-lost pid=$before_pid " <<<"$fresh" | tail -n 1 || true)"
    diagnostic_supervisor_exit="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=exit pid=$before_pid " <<<"$fresh" | tail -n 1 || true)"
    diagnostic_attempt="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=attempt ' <<<"$fresh" | tail -n 1 || true)"
    diagnostic_ready="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=wayland-ready ' <<<"$fresh" | tail -n 1 || true)"
    diagnostic_launch="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=launch ' <<<"$fresh" | tail -n 1 || true)"
    diagnostic_app_start="$(grep -F 'SEYRIX_SETTINGS event=start ' <<<"$fresh" | tail -n 1 || true)"
    diagnostic_compositor="$(pgrep -u 1000 -f '^/usr/bin/seyrix-compositor$' | head -n 1 || true)"
    diagnostic_compositor_stderr="$(readlink "/proc/$diagnostic_compositor/fd/2" 2>/dev/null || true)"
    diagnostic_manager_stderr="$(readlink "/proc/$manager/fd/2" 2>/dev/null || true)"
    diagnostic_pids="$manager,$before_pid"
    [[ "$diagnostic_compositor" =~ ^[1-9][0-9]*$ ]] && diagnostic_pids+=",$diagnostic_compositor"
    [[ "$pid" =~ ^[1-9][0-9]*$ ]] && diagnostic_pids+=",$pid"
    diagnostic_processes="$(ps -o pid,ppid,sid,pgid,uid,stat,comm,args -p "$diagnostic_pids" 2>&1 || true)"
    diagnostic_wchan="$(cat "/proc/$pid/wchan" 2>&1 || true)"
    diagnostic_syscall="$(cat "/proc/$pid/syscall" 2>&1 || true)"
    diagnostic_fds=""
    diagnostic_socket_fds=""
    diagnostic_peer_socket_fds=""
    if [[ "$pid" =~ ^[1-9][0-9]*$ && -d "/proc/$pid/fd" ]]; then
      for diagnostic_fd in /proc/"$pid"/fd/*; do
        [[ -e "$diagnostic_fd" || -L "$diagnostic_fd" ]] || continue
        diagnostic_fds+="${diagnostic_fd##*/}=$(readlink "$diagnostic_fd" 2>&1)"$'\n'
      done
      if command -v ss >/dev/null 2>&1; then
        diagnostic_socket_fds="$(ss -xapne 2>&1 | grep -F "pid=$pid," || true)"
        if [[ "$diagnostic_compositor" =~ ^[1-9][0-9]*$ && -d "/proc/$diagnostic_compositor/fd" ]]; then
          while IFS= read -r diagnostic_socket_line; do
            [[ "$diagnostic_socket_line" == u_seq\ ESTAB* ]] || continue
            read -r -a diagnostic_socket_fields <<< "$diagnostic_socket_line"
            (( ${#diagnostic_socket_fields[@]} >= 9 )) || continue
            diagnostic_client_inode="${diagnostic_socket_fields[5]}"
            diagnostic_peer_inode="${diagnostic_socket_fields[7]}"
            diagnostic_client_fd="${diagnostic_socket_fields[8]##*fd=}"
            diagnostic_client_fd="${diagnostic_client_fd%%)*}"
            [[ "$diagnostic_client_fd" =~ ^[0-9]+$ &&
              "$diagnostic_peer_inode" =~ ^[0-9]+$ ]] || continue
            diagnostic_compositor_fds=""
            for diagnostic_peer_fd in /proc/"$diagnostic_compositor"/fd/*; do
              [[ -e "$diagnostic_peer_fd" || -L "$diagnostic_peer_fd" ]] || continue
              if [[ "$(readlink "$diagnostic_peer_fd" 2>/dev/null || true)" == "socket:[$diagnostic_peer_inode]" ]]; then
                diagnostic_compositor_fds+="${diagnostic_peer_fd##*/},"
              fi
            done
            diagnostic_compositor_fds="${diagnostic_compositor_fds%,}"
            [[ -n "$diagnostic_compositor_fds" ]] || diagnostic_compositor_fds=unmatched
            diagnostic_peer_socket_fds+="settings_fd=$diagnostic_client_fd settings_inode=$diagnostic_client_inode compositor_pid=$diagnostic_compositor compositor_fd=$diagnostic_compositor_fds peer_inode=$diagnostic_peer_inode ss_row=$diagnostic_socket_line"$'\n'
          done <<< "$diagnostic_socket_fds"
        fi
      fi
    fi
    diagnostic_stderr="$(grep -F "source=client-stderr pid=$pid " "$compositor_log" 2>/dev/null | tail -n 80 || true)"
    diagnostic_ipc_client_trace="$(grep -F "source=client-stderr pid=$pid " "$compositor_log" 2>/dev/null | grep -F 'SETTINGS_IPC_CLIENT ' | tail -n 100 || true)"
    diagnostic_ipc_server_trace="$(grep -E 'Settings endpoint (accepted peer|watch ready|watch failed|dispatch|read|frame|request-dispatch|rejected frame|rejected request|peer hangup|peer closed|receive failed|sent peer_pid|send failed|removing)' "$compositor_log" | grep -F "pid=$pid " | tail -n 120 || true)"
    diagnostic_socket_status=""
    for diagnostic_socket in "$runtime_dir"/wayland-* "$runtime_dir"/seyrix/settings-v1.sock; do
      if [[ -e "$diagnostic_socket" || -S "$diagnostic_socket" ]]; then
        diagnostic_socket_status+="$(stat -c '%n type=%F mode=%a owner=%u:%g' -- "$diagnostic_socket" 2>&1 || true)"$'\n'
      fi
    done
    diagnostic_socket_status="${diagnostic_socket_status:-none}"
    diagnostic_frames=""
    if [[ "$pid" =~ ^[1-9][0-9]*$ ]]; then
      diagnostic_frames="$(grep -F "Settings endpoint frame peer_pid=$pid " <<<"$diagnostic_ipc_server_trace" || true)"
      diagnostic_accepted="$(grep -F "Settings endpoint accepted peer uid=1000 pid=$pid " <<<"$diagnostic_ipc_server_trace" | tail -n 1 || true)"
      diagnostic_mapped="$(grep -F "Window mapped: pid=$pid " <<<"$fresh" | grep -F 'app_id=org.seyrix.settings title=System Settings' | tail -n 1 || true)"
    else
      diagnostic_accepted=""
      diagnostic_mapped=""
    fi
    diagnostic_hello=0
    grep -Eq 'opcode=1 .*negotiated=0' <<<"$diagnostic_frames" && diagnostic_hello=1 || true
    diagnostic_snapshot_request="$(sed -nE 's/.*opcode=2 request=([0-9]+) .*/\1/p' <<<"$diagnostic_frames" | head -n 1)"
    diagnostic_subscribe_request="$(sed -nE 's/.*opcode=3 request=([0-9]+) .*/\1/p' <<<"$diagnostic_frames" | head -n 1)"
    diagnostic_snapshot_response=""
    diagnostic_subscribe_response=""
    if [[ -n "$diagnostic_snapshot_request" && "$pid" =~ ^[1-9][0-9]*$ ]]; then
      diagnostic_snapshot_response="$(grep -F "Settings endpoint sent peer_pid=$pid opcode=32768 request=$diagnostic_snapshot_request " <<<"$fresh" | tail -n 1 || true)"
    fi
    if [[ -n "$diagnostic_subscribe_request" && "$pid" =~ ^[1-9][0-9]*$ ]]; then
      diagnostic_subscribe_response="$(grep -F "Settings endpoint sent peer_pid=$pid opcode=32768 request=$diagnostic_subscribe_request " <<<"$fresh" | tail -n 1 || true)"
    fi
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_STATE mode=%s old_pid=%s manager=%s compositor=%s settings_pid=%s count=%s category=%s width=%s height=%s maximized=%s paned_position=%s filtered_lines=%s\n' \
      "$expected" "$before_pid" "${manager:-missing}" "${diagnostic_compositor:-missing}" \
      "${pid:-missing}" "$count" "$before_category" "$before_width" "$before_height" \
      "$before_maximized" "$before_paned_position" "$stream_lines"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_MARKERS display_lost=%s supervisor_exit=%s attempt=%s wayland_ready=%s launch=%s app_start=%s accepted=%s hello=%s snapshot_request=%s snapshot_response=%s subscribe_request=%s subscribe_response=%s mapped=%s\n' \
      "${diagnostic_display_lost:+yes}" "${diagnostic_supervisor_exit:+yes}" \
      "${diagnostic_attempt:+yes}" "${diagnostic_ready:+yes}" "${diagnostic_launch:+yes}" \
      "${diagnostic_app_start:+yes}" "${diagnostic_accepted:+yes}" "$diagnostic_hello" \
      "${diagnostic_snapshot_request:-missing}" "${diagnostic_snapshot_response:+yes}" \
      "${diagnostic_subscribe_request:-missing}" "${diagnostic_subscribe_response:+yes}" \
      "${diagnostic_mapped:+yes}"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_PROCESS_TREE_BEGIN\n%s\nSETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_PROCESS_TREE_END\n' \
      "${diagnostic_processes:-unavailable}"
    printf 'SETTINGS_RECOVERY_LOG_FD compositor_stderr=%s supervisor_stderr=%s log=%s\n' \
      "${diagnostic_compositor_stderr:-unavailable}" \
      "${diagnostic_manager_stderr:-unavailable}" "$compositor_log"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_WCHAN pid=%s value=%s\n' "${pid:-missing}" "${diagnostic_wchan:-unavailable}"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_SYSCALL pid=%s value=%s\n' "${pid:-missing}" "${diagnostic_syscall:-unavailable}"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_FDS_BEGIN\n%sSETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_FDS_END\n' "${diagnostic_fds:-unavailable}"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_SOCKET_FDS_BEGIN\n%s\nSETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_SOCKET_FDS_END\n' "${diagnostic_socket_fds:-unavailable}"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_PEER_SOCKET_FDS_BEGIN\n%sSETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_PEER_SOCKET_FDS_END\n' "${diagnostic_peer_socket_fds:-unavailable}"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_RAW_STDERR_BEGIN pid=%s\n%s\nSETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_RAW_STDERR_END\n' \
      "${pid:-missing}" "${diagnostic_stderr:-no-client-stderr-lines-captured}"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_IPC_CLIENT_TRACE_BEGIN pid=%s\n%s\nSETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_IPC_CLIENT_TRACE_END\n' \
      "${pid:-missing}" "${diagnostic_ipc_client_trace:-no-settings-ipc-client-trace-lines-captured}"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_IPC_SERVER_TRACE_BEGIN pid=%s\n%s\nSETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_IPC_SERVER_TRACE_END\n' \
      "${pid:-missing}" "${diagnostic_ipc_server_trace:-no-settings-ipc-server-trace-lines-captured}"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_SOCKETS_BEGIN\n%sSETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_SOCKETS_END\n' \
      "$diagnostic_socket_status"
    printf 'SETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_EVENTS_BEGIN\n%s\nSETTINGS_SUPERVISOR_RECOVERY_TIMEOUT_EVENTS_END\n' \
      "$(tail -n 30 <<<"$fresh")"
  fi
  kill "$trace_pid" 2>/dev/null || true
  wait "$trace_pid" 2>/dev/null || true
  exec {trace_fd}<&-
  if [[ "$recovery_wait_complete" != 1 ]]; then
    printf 'SETTINGS_SUPERVISOR_RECOVERY_WAIT mode=%s result=timeout old_pid=%s manager=%s compositor=%s settings_pid=%s count=%s filtered_lines=%s total_log_lines=%s\n' \
      "$expected" "$before_pid" "${manager:-missing}" "$(pgrep -u 1000 -f '^/usr/bin/seyrix-compositor$' | head -n 1 || true)" \
      "${pid:-missing}" "$count" "$stream_lines" "$(wc -l <"$compositor_log" 2>/dev/null || echo missing)"
  fi
  start="$(awk '{print $22}' "/proc/$pid/stat" 2>/dev/null || true)"
  manager_start="$(awk '{print $22}' "/proc/$manager/stat" 2>/dev/null || true)"
  parent="$(awk '/^PPid:/ {print $2}' "/proc/$pid/status" 2>/dev/null || true)"
  compositor="$(pgrep -u 1000 -f '^/usr/bin/seyrix-compositor$' | head -n 1 || true)"
  compositor_start="$(awk '{print $22}' "/proc/$compositor/stat" 2>/dev/null || true)"
  compositor_sid="$(ps -o sid= -p "$compositor" 2>/dev/null | tr -d ' ')"
  sid="$(ps -o sid= -p "$manager" 2>/dev/null | tr -d ' ')"
  manager_pgid="$(ps -o pgid= -p "$manager" 2>/dev/null | tr -d ' ')"
  compositor_pgid="$(ps -o pgid= -p "$compositor" 2>/dev/null | tr -d ' ')"
  old_start_now="$(awk '{print $22}' "/proc/$before_pid/stat" 2>/dev/null || true)"
  app_start="$(grep -F "SEYRIX_SETTINGS event=start pid=$pid " <<<"$fresh" | tail -n 1 || true)"
  launch_marker="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=launch pid=$pid" <<<"$fresh" | tail -n 1 || true)"
  ready_marker="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=wayland-ready ' <<<"$fresh" | tail -n 1 || true)"
  attempt_marker="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=attempt ' <<<"$fresh" | tail -n 1 || true)"
  exit_marker="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=exit pid=$before_pid " <<<"$fresh" | tail -n 1 || true)"
  app_exit_marker=""
  if [[ "$expected" == compositor ]]; then
    app_exit_marker="$(grep -F "SEYRIX_SETTINGS event=display-lost pid=$before_pid exit_status=75" <<<"$fresh" | tail -n 1 || true)"
  fi
  new_category="$(settings_category_id "$(field "$app_start" category || true)" 2>/dev/null || echo unknown)"
  new_width="$(field "$app_start" width || true)"
  new_height="$(field "$app_start" height || true)"
  new_maximized="$(settings_boolean "$(field "$app_start" maximized || true)" 2>/dev/null || echo unknown)"
  new_paned_position="$(field "$app_start" paned_position || true)"
  state_restored="$(field "$app_start" state_restored || true)"
  ready_line="$(grep -nF 'SEYRIX_SETTINGS_SUPERVISOR event=wayland-ready ' <<<"$fresh" | tail -n 1 | cut -d: -f1)"
  launch_line="$(grep -nF "SEYRIX_SETTINGS_SUPERVISOR event=launch pid=$pid" <<<"$fresh" | tail -n 1 | cut -d: -f1)"
  attempt_line="$(grep -nF 'SEYRIX_SETTINGS_SUPERVISOR event=attempt ' <<<"$fresh" | tail -n 1 | cut -d: -f1)"
  app_line="$(grep -nF "SEYRIX_SETTINGS event=start pid=$pid " <<<"$fresh" | tail -n 1 | cut -d: -f1)"
  exit_status="$(field "$exit_marker" status || true)"
  exit_signal="$(field "$exit_marker" signal || true)"
  exit_line="$(grep -nF "SEYRIX_SETTINGS_SUPERVISOR event=exit pid=$before_pid " <<<"$fresh" | tail -n 1 | cut -d: -f1)"
  attempt_number="$(field "$attempt_marker" attempt || true)"
  ready_attempt="$(field "$ready_marker" attempt || true)"
  launch_attempt="$(field "$launch_marker" attempt || true)"
  ready_display="$(field "$ready_marker" display || true)"
  server_lines="$(grep -E 'Settings endpoint (accepted peer|watch ready|dispatch|read|frame|request-dispatch|sent peer_pid)' "$compositor_log" | grep -F "pid=$pid " || true)"
  accepted="$(grep -F "Settings endpoint accepted peer uid=1000 pid=$pid " <<<"$server_lines" | tail -n 1 || true)"
  frames="$(grep -F "Settings endpoint frame peer_pid=$pid " <<<"$server_lines" || true)"
  hello=0
  grep -Eq 'opcode=1 .*negotiated=0' <<<"$frames" && hello=1 || true
  snapshot_request="$(sed -nE 's/.*opcode=2 request=([0-9]+) .*/\1/p' <<<"$frames" | head -n 1)"
  subscribe_request="$(sed -nE 's/.*opcode=3 request=([0-9]+) .*/\1/p' <<<"$frames" | head -n 1)"
  snapshot_response="$(grep -F "Settings endpoint sent peer_pid=$pid opcode=32768 request=$snapshot_request " <<<"$server_lines" | tail -n 1 || true)"
  subscribe_response="$(grep -F "Settings endpoint sent peer_pid=$pid opcode=32768 request=$subscribe_request " <<<"$server_lines" | tail -n 1 || true)"
  snapshot_revision="$(sed -nE 's/.*revision=([0-9]+).*/\1/p' <<<"$snapshot_response")"
  subscribe_revision="$(sed -nE 's/.*revision=([0-9]+).*/\1/p' <<<"$subscribe_response")"
  snapshot_status="$(field "$snapshot_response" response_status || true)"
  snapshot_to="$(field "$snapshot_response" response_to || true)"
  subscribe_status="$(field "$subscribe_response" response_status || true)"
  subscribe_to="$(field "$subscribe_response" response_to || true)"
  mapped="$(grep -F "Window mapped: pid=$pid " <<<"$fresh" | grep -F 'app_id=org.seyrix.settings title=System Settings' | tail -n 1 || true)"
  if settings_wayland_trace_export "$pid"; then trace_export=passed; else trace_export=failed; fi
  printf 'SETTINGS_IPC_SERVER_LOG_BEGIN pid=%s\n%s\nSETTINGS_IPC_SERVER_LOG_END\n' \
    "${pid:-missing}" "${server_lines:-no-matching-server-records}"
  state_file="${XDG_STATE_HOME:-/home/seyrix/.local/state}/seyrix/settings.ini"
  printf 'SETTINGS_SUPERVISOR_RECOVERY mode=%s old_pid=%s old_alive=%s new_pid=%s new_start=%s count=%s manager_before=%s manager_after=%s manager_same_start=%s compositor_before=%s compositor_after=%s compositor_same_start=%s ppid=%s session=%s compositor_session=%s manager_pgid=%s compositor_pgid=%s attempt=%s attempt_number=%s attempt_line=%s ready_display=%s ready_attempt=%s ready_line=%s launch_attempt=%s launch_line=%s app_line=%s exit_line=%s exit_status=%s exit_signal=%s display_lost=%s category=%s/%s width=%s/%s height=%s/%s maximized=%s/%s paned_position=%s/%s state_restored=%s mapped=%s accepted=%s hello=%s snapshot_request=%s snapshot_revision=%s snapshot_status=%s snapshot_to=%s subscribe_request=%s subscribe_revision=%s subscribe_status=%s subscribe_to=%s wayland_trace=%s state_file=%s\n' \
    "$expected" "$before_pid" "$([[ "$old_start_now" == "$before_start" ]] && echo yes || echo no)" \
    "${pid:-missing}" "${start:-missing}" "${count:-missing}" "$before_manager" \
    "${manager:-missing}" "$([[ "$manager" == "$before_manager" && "$manager_start" == "$before_manager_start" ]] && echo yes || echo no)" \
    "$before_compositor" "${compositor:-missing}" "$([[ "$compositor" == "$before_compositor" && "$compositor_start" == "$before_compositor_start" ]] && echo yes || echo no)" \
    "${parent:-missing}" "${sid:-missing}" "${compositor_sid:-missing}" \
    "${manager_pgid:-missing}" "${compositor_pgid:-missing}" \
    "${attempt_marker:+yes}" "${attempt_number:-missing}" "${attempt_line:-missing}" \
    "${ready_display:-missing}" "${ready_attempt:-missing}" "${ready_line:-missing}" \
    "${launch_attempt:-missing}" "${launch_line:-missing}" "${app_line:-missing}" \
    "${exit_line:-missing}" "${exit_status:-missing}" \
    "${exit_signal:-none}" "${app_exit_marker:+yes}" "${new_category:-missing}" "$before_category" \
    "${new_width:-missing}" "$before_width" "${new_height:-missing}" "$before_height" \
    "${new_maximized:-missing}" "$before_maximized" "${new_paned_position:-missing}" \
    "$before_paned_position" "${state_restored:-missing}" \
    "${mapped:+yes}" "${accepted:+yes}" "$hello" "${snapshot_request:-missing}" \
    "${snapshot_revision:-missing}" "${snapshot_status:-missing}" "${snapshot_to:-missing}" \
    "${subscribe_request:-missing}" "${subscribe_revision:-missing}" \
    "${subscribe_status:-missing}" "${subscribe_to:-missing}" "$trace_export" "$state_file"
  if [[ "$manager" == "$before_manager" && "$manager_start" == "$before_manager_start" &&
        "$pid" =~ ^[1-9][0-9]*$ && "$pid" != "$before_pid" &&
        "$start" =~ ^[0-9]+$ && "$count" == 1 && "$parent" == "$manager" &&
        "$sid" == "$compositor_sid" && "$manager_pgid" == "$manager" && "$manager_pgid" == "$before_manager_pgid" && "$manager_pgid" != "$compositor_pgid" && "$before_manager_pgid" != "$before_compositor_pgid" && "$old_start_now" != "$before_start" &&
        -n "$attempt_marker" && -n "$ready_marker" && -n "$launch_marker" &&
        -n "$exit_marker" && "$attempt_line" =~ ^[0-9]+$ &&
        "$exit_line" =~ ^[0-9]+$ && "$exit_line" -lt "$attempt_line" &&
        "$attempt_number" =~ ^[0-9]+$ && "$attempt_number" == "$ready_attempt" &&
        "$attempt_number" == "$launch_attempt" && -n "$ready_display" &&
        -S "/run/user/1000/$ready_display" &&
        "$ready_line" =~ ^[0-9]+$ && "$launch_line" =~ ^[0-9]+$ &&
        "$app_line" =~ ^[0-9]+$ && "$attempt_line" -lt "$ready_line" &&
        "$ready_line" -lt "$launch_line" &&
        "$launch_line" -lt "$app_line" &&
        ( "$exit_status" =~ $expected_status ||
          ( "$expected" == process && "$exit_signal" == 9 ) ) &&
        "$new_category" == "$before_category" && "$new_width" == "$before_width" &&
        "$new_height" == "$before_height" && "$new_maximized" == "$before_maximized" &&
        "$new_paned_position" == "$before_paned_position" &&
        ( "$expected" != compositor || "$compositor" =~ ^[1-9][0-9]*$ && ( "$compositor" != "$before_compositor" || "$compositor_start" != "$before_compositor_start" ) ) &&
        "$state_restored" =~ ^(1|true|yes)$ &&
        -n "$mapped" && -n "$accepted" && "$hello" == 1 && "$trace_export" == passed &&
        -n "$snapshot_request" && -n "$snapshot_response" &&
        -n "$snapshot_revision" && "$snapshot_status" == 0 && "$snapshot_to" == 2 && -n "$subscribe_request" &&
        -n "$subscribe_response" && -n "$subscribe_revision" && "$subscribe_status" == 0 && "$subscribe_to" == 3 &&
        -s "$state_file" ]]; then
    echo SETTINGS_SUPERVISOR_RECOVERY=passed
    echo SETTINGS_RECOVERY_HARNESS_APP_LAUNCHES=0
    return 0
  fi
  echo SETTINGS_SUPERVISOR_RECOVERY=failed
  printf 'SETTINGS_SUPERVISOR_RECOVERY_LOG_BEGIN\n%s\nSETTINGS_SUPERVISOR_RECOVERY_LOG_END\n' "$fresh" | tail -n 80
  return 1
}

settings_recovery_exhaust_budget() {
  local manager manager_start manager_exe manager_sid compositor compositor_sid open_line first_pid first_count first_start
  local first_start_marker category width height maximized paned_position state_file
  local cursor index launch_marker retry_marker pid attempt ready_marker attempt_marker app_start ready_display
  local attempt_line ready_line launch_line app_line exit_marker exit_status exit_signal count parent
  local pids=() seen marker limit_marker limit_line fresh pid_count launch_count exit_count retry_delay_ms scheduled_ms retry_at_ms attempt_monotonic_ms elapsed_ms expected_delay_ms
  manager="$(settings_manager_pid)"
  manager_start="$(awk '{print $22}' "/proc/$manager/stat" 2>/dev/null || true)"
  manager_exe="$(readlink -f "/proc/$manager/exe" 2>/dev/null || true)"
  manager_sid="$(ps -o sid= -p "$manager" 2>/dev/null | tr -d ' ')"
  compositor="$(pgrep -u 1000 -f '^/usr/bin/seyrix-compositor$' | head -n 1 || true)"
  compositor_sid="$(ps -o sid= -p "$compositor" 2>/dev/null | tr -d ' ')"
  open_line="$(grep -nF 'SEYRIX_SETTINGS_SUPERVISOR event=open-request ' "$compositor_log" | tail -n 1 | cut -d: -f1)"
  first_pid="$(pgrep -u 1000 -x seyrix-settings | head -n 1 || true)"
  first_count="$(pgrep -u 1000 -x seyrix-settings | wc -l)"
  first_start="$(awk '{print $22}' "/proc/$first_pid/stat" 2>/dev/null || true)"
  first_start_marker="$(grep -F "SEYRIX_SETTINGS event=start pid=$first_pid " "$compositor_log" | tail -n 1 || true)"
  category="$(settings_category_id "$(field "$first_start_marker" category || true)" 2>/dev/null || echo unknown)"
  width="$(field "$first_start_marker" width || true)"
  height="$(field "$first_start_marker" height || true)"
  maximized="$(settings_boolean "$(field "$first_start_marker" maximized || true)" 2>/dev/null || echo unknown)"
  paned_position="$(field "$first_start_marker" paned_position || true)"
  state_file="${XDG_STATE_HOME:-/home/seyrix/.local/state}/seyrix/settings.ini"
  [[ "$manager_exe" == /usr/bin/seyrix-session-supervisor && "$manager_start" =~ ^[0-9]+$ &&
     "$manager_sid" =~ ^[0-9]+$ && "$manager_sid" == "$compositor_sid" &&
     "$open_line" =~ ^[0-9]+$ && "$first_pid" =~ ^[1-9][0-9]*$ &&
     "$first_start" =~ ^[0-9]+$ && "$first_count" == 1 &&
     "$category" != unknown && "$width" =~ ^[0-9]+$ && "$height" =~ ^[0-9]+$ &&
     "$maximized" != unknown && "$paned_position" =~ ^[0-9]+$ && -s "$state_file" ]] ||
    fail "settings-retry-budget-baseline-invalid"
  printf '%s %s %s %s %s %s %s %s\n' "$manager" "$manager_start" "$open_line" \
    "$category" "$width" "$height" "$maximized" "$paned_position" \
    >/tmp/seyrix-settings-recovery-budget-before
  printf 'SETTINGS_RECOVERY_BUDGET_BASELINE manager=%s manager_start=%s manager_sid=%s open_line=%s initial_pid=%s initial_start=%s category=%s width=%s height=%s maximized=%s paned_position=%s\n' \
    "$manager" "$manager_start" "$manager_sid" "$open_line" "$first_pid" "$first_start" \
    "$category" "$width" "$height" "$maximized" "$paned_position"
  : >/tmp/seyrix-settings-recovery-budget-pids
  cursor="$open_line"
  for index in $(seq 1 6); do
    if [[ "$index" == 1 ]]; then
      pid="$first_pid"
      launch_marker="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=launch pid=$pid " "$compositor_log" | tail -n 1 || true)"
    else
      launch_marker=""
      retry_marker=""
      for _ in $(seq 1 480); do
        fresh="$(tail -n +$((cursor + 1)) "$compositor_log" 2>/dev/null || true)"
        retry_marker="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=retry-scheduled next_attempt=$index " <<<"$fresh" | tail -n 1 || true)"
        launch_marker="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=launch " <<<"$fresh" | tail -n 1 || true)"
        [[ -n "$retry_marker" && -n "$launch_marker" ]] && break
        sleep 0.25
      done
      pid="$(field "$launch_marker" pid || true)"
    fi
    attempt="$(field "$launch_marker" attempt || true)"
    attempt_marker="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=attempt attempt=$attempt " "$compositor_log" | tail -n 1 || true)"
    if [[ "$index" -gt 1 ]]; then
      retry_delay_ms="$(field "$retry_marker" delay_ms || true)"
      scheduled_ms="$(field "$retry_marker" scheduled_ms || true)"
      retry_at_ms="$(field "$retry_marker" retry_at_ms || true)"
      attempt_monotonic_ms="$(field "$attempt_marker" monotonic_ms || true)"
      expected_delay_ms=$((1000 << (index - 2)))
      elapsed_ms=$(( ${attempt_monotonic_ms:-0} - ${scheduled_ms:-0} ))
      [[ "$(field "$retry_marker" completed_attempt || true)" == "$((index - 1))" &&
         "$retry_delay_ms" == "$expected_delay_ms" &&
         "$scheduled_ms" =~ ^[0-9]+$ && "$retry_at_ms" =~ ^[0-9]+$ &&
         "$attempt_monotonic_ms" =~ ^[0-9]+$ &&
         $((retry_at_ms - scheduled_ms)) == "$retry_delay_ms" &&
         "$elapsed_ms" -ge "$retry_delay_ms" ]] ||
        fail "settings-retry-backoff-invalid-attempt-$index-delay-${retry_delay_ms:-missing}-elapsed-${elapsed_ms:-missing}"
      printf 'SETTINGS_RECOVERY_BACKOFF index=%s delay_ms=%s elapsed_ms=%s scheduled_ms=%s attempt_ms=%s\n' \
        "$index" "$retry_delay_ms" "$elapsed_ms" "$scheduled_ms" "$attempt_monotonic_ms"
    fi
    ready_marker="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=wayland-ready " "$compositor_log" | grep -F "attempt=$attempt" | tail -n 1 || true)"
    app_start="$(grep -F "SEYRIX_SETTINGS event=start pid=$pid " "$compositor_log" | tail -n 1 || true)"
    count="$(pgrep -u 1000 -x seyrix-settings | wc -l)"
    parent="$(awk '/^PPid:/ {print $2}' "/proc/$pid/status" 2>/dev/null || true)"
    ready_display="$(field "$ready_marker" display || true)"
    attempt_line="$(grep -nF "SEYRIX_SETTINGS_SUPERVISOR event=attempt attempt=$attempt " "$compositor_log" | tail -n 1 | cut -d: -f1)"
    ready_line="$(grep -nF 'SEYRIX_SETTINGS_SUPERVISOR event=wayland-ready ' "$compositor_log" | grep -F "attempt=$attempt" | tail -n 1 | cut -d: -f1)"
    launch_line="$(grep -nF "SEYRIX_SETTINGS_SUPERVISOR event=launch pid=$pid " "$compositor_log" | tail -n 1 | cut -d: -f1)"
    app_line="$(grep -nF "SEYRIX_SETTINGS event=start pid=$pid " "$compositor_log" | tail -n 1 | cut -d: -f1)"
    [[ "$pid" =~ ^[1-9][0-9]*$ && "$attempt" == "$index" && "$count" == 1 &&
       "$parent" == "$manager" && -n "$attempt_marker" && -n "$ready_marker" &&
       -n "$ready_display" && -S "/run/user/1000/$ready_display" &&
       -n "$app_start" && "$attempt_line" =~ ^[0-9]+$ && "$ready_line" =~ ^[0-9]+$ &&
       "$launch_line" =~ ^[0-9]+$ && "$app_line" =~ ^[0-9]+$ &&
       "$attempt_line" -gt "$cursor" && "$attempt_line" -lt "$ready_line" &&
       "$ready_line" -lt "$launch_line" &&
       "$launch_line" -lt "$app_line" &&
       "$(field "$ready_marker" display || true)" != "" ]] ||
      fail "settings-retry-budget-launch-$index-invalid-pid-$pid"
    if printf '%s\n' "${pids[@]}" | grep -Fxq "$pid"; then
      fail "settings-retry-budget-duplicate-pid-$pid"
    fi
    pids+=("$pid")
    printf '%s\n' "$pid" >>/tmp/seyrix-settings-recovery-budget-pids
    printf 'SETTINGS_RECOVERY_BUDGET_LAUNCH index=%s attempt=%s pid=%s parent=%s count=%s attempt_line=%s ready_line=%s launch_line=%s app_line=%s\n' \
      "$index" "$attempt" "$pid" "$parent" "$count" "$attempt_line" "$ready_line" "$launch_line" "$app_line"
    kill -KILL "$pid" || fail "settings-retry-budget-kill-failed-$pid"
    exit_marker=""
    for _ in $(seq 1 80); do
      exit_marker="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=exit pid=$pid " "$compositor_log" | tail -n 1 || true)"
      [[ -n "$exit_marker" ]] && break
      sleep 0.25
    done
    exit_status="$(field "$exit_marker" status || true)"
    exit_signal="$(field "$exit_marker" signal || true)"
    [[ -n "$exit_marker" && ( "$exit_signal" == 9 || "$exit_status" == 137 ) ]] ||
      fail "settings-retry-budget-exit-not-sigkill-$pid"
    cursor="$(grep -nF "SEYRIX_SETTINGS_SUPERVISOR event=exit pid=$pid " "$compositor_log" | tail -n 1 | cut -d: -f1)"
    printf 'SETTINGS_RECOVERY_BUDGET_EXIT index=%s pid=%s status=%s signal=%s cursor=%s\n' \
      "$index" "$pid" "${exit_status:-none}" "${exit_signal:-none}" "$cursor"
  done
  limit_marker=""
  for _ in $(seq 1 160); do
    limit_marker="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=restart-limit ' "$compositor_log" | tail -n 1 || true)"
    [[ -n "$limit_marker" ]] && break
    sleep 0.25
  done
  sleep 2
  limit_line="$(grep -nF 'SEYRIX_SETTINGS_SUPERVISOR event=restart-limit ' "$compositor_log" | tail -n 1 | cut -d: -f1)"
  fresh="$(tail -n +$((open_line + 1)) "$compositor_log" 2>/dev/null || true)"
  pid_count="$(pgrep -u 1000 -x seyrix-settings | wc -l)"
  launch_count="$(grep -Fc 'SEYRIX_SETTINGS_SUPERVISOR event=launch ' <<<"$fresh" || true)"
  exit_count="$(grep -Fc 'SEYRIX_SETTINGS_SUPERVISOR event=exit ' <<<"$fresh" || true)"
  seen="$(sed -n 's/^\([0-9][0-9]*\)$/\1/p' /tmp/seyrix-settings-recovery-budget-pids | sort -u | wc -l)"
  manager_start="$(awk '{print $22}' "/proc/$manager/stat" 2>/dev/null || true)"
  printf 'SETTINGS_RECOVERY_BUDGET_LIMIT marker=%s attempts=%s line=%s launches=%s exits=%s distinct_pids=%s active=%s manager=%s manager_start=%s pid_file=/tmp/seyrix-settings-recovery-budget-pids\n' \
    "${limit_marker:+yes}" "$(field "$limit_marker" attempts || true)" "${limit_line:-missing}" \
    "$launch_count" "$exit_count" "$seen" "$pid_count" "$manager" "$manager_start"
  cat /tmp/seyrix-settings-recovery-budget-pids
  [[ -n "$limit_marker" && "$(field "$limit_marker" attempts || true)" == 6 &&
     "$launch_count" == 6 && "$exit_count" == 6 && "$seen" == 6 && "$pid_count" == 0 &&
     "$manager_start" == "$(awk '{print $2}' /tmp/seyrix-settings-recovery-budget-before)" ]] ||
    fail "settings-retry-budget-not-bounded-to-six"
  limit_line="${limit_line:-0}"
  printf '%s\n' "$limit_line" >/tmp/seyrix-settings-recovery-budget-limit-line
  echo SETTINGS_RECOVERY_BUDGET_EXHAUSTED=passed
  echo SETTINGS_RECOVERY_HARNESS_APP_LAUNCHES=0
}

settings_recovery_budget_fresh_open_verify() {
  local manager manager_start manager_sid compositor compositor_sid open_line limit_line pid count parent start_marker launch_marker
  local ready_marker attempt_marker ready_display category width height maximized paned_position restored
  local start_line attempt_line ready_line launch_line old_pid fresh accepted frames hello snapshot_request
  local subscribe_request snapshot_response subscribe_response snapshot_revision subscribe_revision mapped
  local snapshot_status snapshot_to subscribe_status subscribe_to
  read -r manager manager_start _ _ _ _ _ _ \
    </tmp/seyrix-settings-recovery-budget-before || fail "settings-retry-budget-state-unreadable"
  limit_line="$(cat /tmp/seyrix-settings-recovery-budget-limit-line 2>/dev/null || true)"
  open_line=""
  launch_marker=""
  pid=""
  for _ in $(seq 1 120); do
    open_line="$(grep -nF 'SEYRIX_SETTINGS_SUPERVISOR event=open-request ' "$compositor_log" | tail -n 1 | cut -d: -f1)"
    launch_marker="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=launch ' "$compositor_log" | tail -n 1 || true)"
    pid="$(field "$launch_marker" pid || true)"
    start_marker="$(grep -F "SEYRIX_SETTINGS event=start pid=$pid " "$compositor_log" | tail -n 1 || true)"
    [[ "$open_line" =~ ^[0-9]+$ && "$open_line" -gt "$limit_line" &&
       "$pid" =~ ^[1-9][0-9]*$ && -n "$start_marker" ]] && break
    sleep 0.25
  done
  manager="$(settings_manager_pid 2>/dev/null || true)"
  manager_start="$(awk '{print $22}' "/proc/$manager/stat" 2>/dev/null || true)"
  manager_sid="$(ps -o sid= -p "$manager" 2>/dev/null | tr -d ' ')"
  compositor="$(pgrep -u 1000 -f '^/usr/bin/seyrix-compositor$' | head -n 1 || true)"
  compositor_sid="$(ps -o sid= -p "$compositor" 2>/dev/null | tr -d ' ')"
  count="$(pgrep -u 1000 -x seyrix-settings | wc -l)"
  parent="$(awk '/^PPid:/ {print $2}' "/proc/$pid/status" 2>/dev/null || true)"
  attempt="$(field "$launch_marker" attempt || true)"
  attempt_marker="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=attempt attempt=$attempt " "$compositor_log" | tail -n 1 || true)"
  ready_marker="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=wayland-ready ' "$compositor_log" | grep -F "attempt=$attempt" | tail -n 1 || true)"
  category="$(settings_category_id "$(field "$start_marker" category || true)" 2>/dev/null || echo unknown)"
  width="$(field "$start_marker" width || true)"
  height="$(field "$start_marker" height || true)"
  maximized="$(settings_boolean "$(field "$start_marker" maximized || true)" 2>/dev/null || echo unknown)"
  paned_position="$(field "$start_marker" paned_position || true)"
  restored="$(field "$start_marker" state_restored || true)"
  ready_display="$(field "$ready_marker" display || true)"
  start_line="$(grep -nF "SEYRIX_SETTINGS event=start pid=$pid " "$compositor_log" | tail -n 1 | cut -d: -f1)"
  attempt_line="$(grep -nF "SEYRIX_SETTINGS_SUPERVISOR event=attempt attempt=$attempt " "$compositor_log" | tail -n 1 | cut -d: -f1)"
  ready_line="$(grep -nF 'SEYRIX_SETTINGS_SUPERVISOR event=wayland-ready ' "$compositor_log" | grep -F "attempt=$attempt" | tail -n 1 | cut -d: -f1)"
  launch_line="$(grep -nF "SEYRIX_SETTINGS_SUPERVISOR event=launch pid=$pid " "$compositor_log" | tail -n 1 | cut -d: -f1)"
  old_pid=none
  grep -Fxq "$pid" /tmp/seyrix-settings-recovery-budget-pids && old_pid=duplicate || true
  fresh="$(tail -n +$((open_line + 1)) "$compositor_log" 2>/dev/null || true)"
  accepted="$(grep -F "Settings endpoint accepted peer uid=1000 pid=$pid " <<<"$fresh" | tail -n 1 || true)"
  frames="$(grep -F "Settings endpoint frame peer_pid=$pid " <<<"$fresh" || true)"
  hello=0
  grep -Eq 'opcode=1 .*negotiated=0' <<<"$frames" && hello=1 || true
  snapshot_request="$(sed -nE 's/.*opcode=2 request=([0-9]+) .*/\1/p' <<<"$frames" | head -n 1)"
  subscribe_request="$(sed -nE 's/.*opcode=3 request=([0-9]+) .*/\1/p' <<<"$frames" | head -n 1)"
  snapshot_response="$(grep -F "Settings endpoint sent peer_pid=$pid opcode=32768 request=$snapshot_request " <<<"$fresh" | tail -n 1 || true)"
  subscribe_response="$(grep -F "Settings endpoint sent peer_pid=$pid opcode=32768 request=$subscribe_request " <<<"$fresh" | tail -n 1 || true)"
  snapshot_revision="$(sed -nE 's/.*revision=([0-9]+).*/\1/p' <<<"$snapshot_response")"
  subscribe_revision="$(sed -nE 's/.*revision=([0-9]+).*/\1/p' <<<"$subscribe_response")"
  snapshot_status="$(field "$snapshot_response" response_status || true)"
  snapshot_to="$(field "$snapshot_response" response_to || true)"
  subscribe_status="$(field "$subscribe_response" response_status || true)"
  subscribe_to="$(field "$subscribe_response" response_to || true)"
  mapped="$(grep -F "Window mapped: pid=$pid " <<<"$fresh" | grep -F 'app_id=org.seyrix.settings title=System Settings' | tail -n 1 || true)"
  printf 'SETTINGS_RECOVERY_BUDGET_FRESH_OPEN pid=%s manager=%s/%s open_line=%s limit_line=%s attempt=%s attempt_line=%s ready_line=%s launch_line=%s start_line=%s old_pid=%s count=%s parent=%s category=%s width=%s height=%s maximized=%s paned_position=%s state_restored=%s mapped=%s accepted=%s hello=%s snapshot_request=%s snapshot_status=%s snapshot_to=%s snapshot_revision=%s subscribe_request=%s subscribe_status=%s subscribe_to=%s subscribe_revision=%s\n' \
    "$pid" "$manager" "$manager_start" "$open_line" "$limit_line" "$attempt" \
    "${attempt_line:-missing}" "${ready_line:-missing}" "${launch_line:-missing}" \
    "${start_line:-missing}" "${old_pid:-missing}" "$count" "${parent:-missing}" \
    "$category" "$width" "$height" "$maximized" "$paned_position" "$restored" \
    "${mapped:+yes}" "${accepted:+yes}" "$hello" "${snapshot_request:-missing}" \
    "${snapshot_status:-missing}" "${snapshot_to:-missing}" "${snapshot_revision:-missing}" \
    "${subscribe_request:-missing}" "${subscribe_status:-missing}" "${subscribe_to:-missing}" \
    "${subscribe_revision:-missing}"
  [[ "$manager" == "$(awk '{print $1}' /tmp/seyrix-settings-recovery-budget-before)" &&
     "$manager_start" == "$(awk '{print $2}' /tmp/seyrix-settings-recovery-budget-before)" &&
     "$manager_sid" =~ ^[0-9]+$ && "$manager_sid" == "$compositor_sid" &&
     "$open_line" =~ ^[0-9]+$ && "$open_line" -gt "$limit_line" &&
     "$pid" =~ ^[1-9][0-9]*$ && "$count" == 1 &&
     "$parent" == "$manager" && "$pid" != "$old_pid" &&
     "$attempt" == 1 && -n "$attempt_marker" && -n "$ready_marker" &&
     -n "$ready_display" && -S "/run/user/1000/$ready_display" &&
     "$attempt_line" =~ ^[0-9]+$ && "$ready_line" =~ ^[0-9]+$ &&
     "$launch_line" =~ ^[0-9]+$ && "$start_line" =~ ^[0-9]+$ &&
     "$attempt_line" -lt "$ready_line" && "$ready_line" -lt "$launch_line" &&
     "$launch_line" -lt "$start_line" &&
     "$category" == "$(awk '{print $4}' /tmp/seyrix-settings-recovery-budget-before)" &&
     "$width" == "$(awk '{print $5}' /tmp/seyrix-settings-recovery-budget-before)" &&
     "$height" == "$(awk '{print $6}' /tmp/seyrix-settings-recovery-budget-before)" &&
     "$maximized" == "$(awk '{print $7}' /tmp/seyrix-settings-recovery-budget-before)" &&
     "$paned_position" == "$(awk '{print $8}' /tmp/seyrix-settings-recovery-budget-before)" &&
     "$restored" =~ ^(1|true|yes)$ && -n "$mapped" && -n "$accepted" && "$hello" == 1 &&
     -n "$snapshot_request" && -n "$snapshot_response" && "$snapshot_status" == 0 &&
     "$snapshot_to" == 2 && -n "$snapshot_revision" &&
     -n "$subscribe_request" && -n "$subscribe_response" && "$subscribe_status" == 0 &&
     "$subscribe_to" == 3 && -n "$subscribe_revision" ]] ||
    fail "settings-retry-budget-fresh-open-invalid"
  echo SETTINGS_RECOVERY_BUDGET_FRESH_OPEN=passed
  echo SETTINGS_RECOVERY_HARNESS_APP_LAUNCHES=0
}

settings_recovery_kill_compositor() {
  local pid
  pid="$(pgrep -u 1000 -f '^/usr/bin/seyrix-compositor$' | head -n 1 || true)"
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || fail "settings-recovery-compositor-not-running"
  kill -KILL "$pid"
  printf 'SETTINGS_RECOVERY_COMPOSITOR_KILLED pid=%s signal=KILL\n' "$pid"
}

settings_user_close_verify() {
  local before_pid before_start manager_before manager_start before_category
  local before_width before_height before_maximized before_paned_position log_baseline before_compositor before_compositor_start manager_after
  local app_close manager_exit fresh process_count launch_after
  read -r before_pid before_start manager_before manager_start before_category \
    before_width before_height before_maximized before_paned_position log_baseline \
    before_compositor before_compositor_start \
    </tmp/seyrix-settings-supervisor-before || fail "settings-user-close-baseline-unreadable"
  for _ in $(seq 1 60); do
    app_close="$(grep -F "SEYRIX_SETTINGS event=user-close pid=$before_pid exit_status=0" "$compositor_log" 2>/dev/null | tail -n 1 || true)"
    manager_exit="$(grep -F "SEYRIX_SETTINGS_SUPERVISOR event=exit pid=$before_pid status=0" "$compositor_log" 2>/dev/null | tail -n 1 || true)"
    [[ -n "$app_close" && -n "$manager_exit" ]] && break
    sleep 0.25
  done
  fresh="$(tail -n +$((log_baseline + 1)) "$compositor_log" 2>/dev/null || true)"
  manager_after="$(settings_manager_pid 2>/dev/null || true)"
  manager_start="$(awk '{print $22}' "/proc/$manager_after/stat" 2>/dev/null || true)"
  process_count="$(pgrep -u 1000 -x seyrix-settings | wc -l || true)"
  launch_after="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=launch ' <<<"$fresh" | grep -Fv "pid=$before_pid" || true)"
  sleep 20
  process_count="$(pgrep -u 1000 -x seyrix-settings | wc -l || true)"
  manager_after="$(settings_manager_pid 2>/dev/null || true)"
  manager_start="$(awk '{print $22}' "/proc/$manager_after/stat" 2>/dev/null || true)"
  fresh="$(tail -n +$((log_baseline + 1)) "$compositor_log" 2>/dev/null || true)"
  launch_after="$(grep -F 'SEYRIX_SETTINGS_SUPERVISOR event=launch ' <<<"$fresh" || true)"
  printf 'SETTINGS_USER_CLOSE old_pid=%s app_marker=%s manager_exit=%s active_apps=%s manager_pid=%s manager_start=%s relaunch_markers=%s wait_s=20\n' \
    "$before_pid" "${app_close:+yes}" "${manager_exit:+yes}" "$process_count" \
    "${manager_after:-missing}" "${manager_start:-missing}" "${launch_after:-none}"
  if [[ -n "$app_close" && -n "$manager_exit" && "$process_count" == 0 &&
        "$manager_after" == "$manager_before" && "$manager_start" == "$(awk '{print $4}' /tmp/seyrix-settings-supervisor-before)" &&
        -z "$launch_after" ]]; then
    echo SETTINGS_USER_CLOSE_NO_REOPEN=passed
    return 0
  fi
  echo SETTINGS_USER_CLOSE_NO_REOPEN=failed
  return 1
}

case "${1:-}" in
  resize-start) shift; resize_start "$@" ;;
  resize-check) shift; resize_check "$@" ;;
  resize-cleanup) shift; resize_cleanup_command "$@" ;;
  watch-start) watch_start ;;
  watch-baseline) watch_baseline ;;
  watch-hidden-baseline) watch_baseline WATCH_POINTER_HIDDEN_BASELINE ;;
  watch-check-hidden) watch_check_hidden ;;
  watch-stop) watch_stop ;;
  move-baseline) move_baseline ;;
  wm-snapshot) shift; wm_snapshot "$@" ;;
  focus-target) shift; focus_target "$@" ;;
  wm-same-identity) shift; wm_same_identity "$@" ;;
  wm-same-geometry) shift; wm_same_geometry "$@" ;;
  wm-geometry-changed) shift; wm_geometry_changed "$@" ;;
  popup-alive) popup_process_alive ;;
  settings-recovery-baseline) settings_recovery_baseline ;;
  settings-recovery-verify) shift; settings_recovery_verify "${1:-compositor}" ;;
  settings-recovery-exhaust-budget) settings_recovery_exhaust_budget ;;
  settings-recovery-budget-fresh-open-verify) settings_recovery_budget_fresh_open_verify ;;
  settings-recovery-kill-compositor) settings_recovery_kill_compositor ;;
  settings-user-close-verify) settings_user_close_verify ;;
  terminate-client) shift; kill_probe_client "$@" ;;
  crash-panel-owner) crash_panel_owner_shell ;;
  startup-failure) recover_shell_after_startup_failure ;;
  terminate-compositor) stop_compositor_and_verify_cleanup SIGTERM ;;
  crash-compositor) stop_compositor_and_verify_cleanup SIGKILL ;;
  kill-mapped-terminal) kill_current_mapped_terminal ;;
  terminate-popup) kill_probe_client "seyrix.qemu.$(test_run_id).popup" ;;
  *) fail "unknown-mode-${1:-missing}" ;;
esac
