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
evdev_log=/tmp/seyrix-evtest-mouse.log
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
  local run_id compositor shell_pid uid executable state parent
  local binary=/usr/bin/seyrix-shell backup=/usr/bin/seyrix-shell.phase2d2-test-backup
  local before_started before_panels before_exits before_schedules started panels exits schedules
  local new_shell_pid new_uid new_executable new_state new_parent current proc_dir pid
  local -a pids=()
  run_id="$(test_run_id)"
  compositor="$(current_compositor_pid)" || fail "current-compositor-identity-ambiguous"
  [[ -x "$binary" && ! -e "$backup" ]] || fail "shell-startup-failure-fixture-not-clean"
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
  [[ "$uid" == 1000 && "$executable" == "$binary" &&
     "$state" != Z && "$state" != X && "$parent" == "$compositor" ]] ||
    fail "current-shell-identity-invalid-pid-$shell_pid"
  printf 'SHELL_STARTUP_FAILURE_BEGIN shell_pid=%s compositor_pid=%s run=%s backup=%s\n' \
    "$shell_pid" "$compositor" "$run_id" "$backup"
  mv -- "$binary" "$backup" || fail "cannot-hide-shell-executable-for-test"
  trap 'if [[ -e /usr/bin/seyrix-shell.phase2d2-test-backup && ! -e /usr/bin/seyrix-shell ]]; then mv -- /usr/bin/seyrix-shell.phase2d2-test-backup /usr/bin/seyrix-shell; fi' EXIT
  kill -KILL "$shell_pid"
  for _ in $(seq 1 100); do
    exits="$(grep -c 'Seyrix shell exited; compositor remains available' "$compositor_log" || true)"
    schedules="$(grep -c 'Scheduling isolated shell restart attempt' "$compositor_log" || true)"
    if [[ "$exits" -ge $((before_exits + 2)) &&
          "$schedules" -ge $((before_schedules + 2)) ]] &&
       grep -Fq 'Scheduling isolated shell restart attempt 1 in 1000 ms' "$compositor_log" &&
       grep -Fq 'Scheduling isolated shell restart attempt 2 in 2000 ms' "$compositor_log"; then
      break
    fi
    sleep 0.1
  done
  if [[ ! -x "$backup" || -e "$binary" ||
        "${exits:-0}" -lt $((before_exits + 2)) ||
        "${schedules:-0}" -lt $((before_schedules + 2)) ]] ||
     ! grep -Fq 'Scheduling isolated shell restart attempt 2 in 2000 ms' "$compositor_log"; then
    if [[ -e "$backup" && ! -e "$binary" ]]; then mv -- "$backup" "$binary"; fi
    fail "shell-startup-failure-was-not-observed-after-first-retry"
  fi
  mv -- "$backup" "$binary" || fail "cannot-restore-shell-executable-after-test"
  for _ in $(seq 1 120); do
    started="$(grep -c 'Started isolated Seyrix desktop shell pid=' "$compositor_log" || true)"
    panels="$(grep -c 'Layer shell surface mapped namespace=seyrix-panel' "$compositor_log" || true)"
    current="$(current_compositor_pid || true)"
    if [[ "$started" -ge $((before_started + 2)) &&
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
        [[ "$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)" == "$binary" ]] || continue
        state="$(awk '/^State:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
        [[ "$state" != Z && "$state" != X ]] || continue
        parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
        [[ "$parent" == "$compositor" ]] || continue
        pids+=("$pid")
      done
      if [[ "$new_uid" == 1000 && "$new_executable" == "$binary" &&
            "$new_state" != Z && "$new_state" != X && "$new_parent" == "$compositor" &&
            "${#pids[@]}" -eq 1 && "${pids[0]}" == "$new_shell_pid" ]]; then
        printf 'PHASE2D2_SHELL_STARTUP_FAILURE_RECOVERY=passed failed_launches=1 restart_attempts=2 recovered_shell_pid=%s compositor_pid=%s panel_maps=%s\n' \
          "$new_shell_pid" "$compositor" "$((panels - before_panels))"
        return 0
      fi
    fi
    sleep 0.25
  done
  if [[ -e "$backup" && ! -e "$binary" ]]; then mv -- "$backup" "$binary"; fi
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
  nohup runuser -u seyrix -- env XDG_RUNTIME_DIR="$runtime_dir" \
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
  nohup runuser -u seyrix -- env XDG_RUNTIME_DIR="$runtime_dir" \
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

move_baseline() {
  local map_line commit_line output_line pid surface x y width height
  local output_width output_height start_x start_y end_x end_y
  local qmp_start_x qmp_start_y qmp_end_x qmp_end_y
  local end_before button_before alt_before gesture_before classification_before
  local evdev_button_before evdev_x_before evdev_y_before
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
  evdev_button_before="$(grep -Ec 'Event: time .*code 272 \(BTN_LEFT\)' \
    "$evdev_log" || true)"
  evdev_x_before="$(grep -Ec 'Event: time .*code 0 \(ABS_X\)' \
    "$evdev_log" || true)"
  evdev_y_before="$(grep -Ec 'Event: time .*code 1 \(ABS_Y\)' \
    "$evdev_log" || true)"
  printf '%s %s %s %s %s %s %s %s %s %s %s\n' \
    "$pid" "$surface" "$x" "$y" "$end_before" "$button_before" \
    "$alt_before" "$gesture_before" "$classification_before" "$evdev_button_before" \
    "$evdev_x_before $evdev_y_before" > /tmp/seyrix-window-move-before
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
  wm-same-identity) shift; wm_same_identity "$@" ;;
  wm-same-geometry) shift; wm_same_geometry "$@" ;;
  wm-geometry-changed) shift; wm_geometry_changed "$@" ;;
  popup-alive) popup_process_alive ;;
  terminate-client) shift; kill_probe_client "$@" ;;
  crash-panel-owner) crash_panel_owner_shell ;;
  startup-failure) recover_shell_after_startup_failure ;;
  terminate-compositor) stop_compositor_and_verify_cleanup SIGTERM ;;
  crash-compositor) stop_compositor_and_verify_cleanup SIGKILL ;;
  kill-mapped-terminal) kill_current_mapped_terminal ;;
  terminate-popup) kill_probe_client "seyrix.qemu.$(test_run_id).popup" ;;
  *) fail "unknown-mode-${1:-missing}" ;;
esac
