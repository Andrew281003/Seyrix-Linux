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
  local -a pids=()
  mapfile -t pids < <(pgrep -u 1000 -f '^/usr/bin/seyrix-compositor$' || true)
  [[ "${#pids[@]}" -eq 1 ]] || return 1
  printf '%s' "${pids[0]}"
}

kill_panel_child() {
  local run_id compositor panel uid executable parent
  run_id="$(test_run_id)"
  compositor="$(current_compositor_pid)" || fail "current-compositor-identity-ambiguous"
  panel="$(sed -n 's/.*Started isolated Seyrix layer-shell panel pid=\([0-9][0-9]*\).*/\1/p' \
    "$compositor_log" | tail -n 1)"
  [[ "$panel" =~ ^[0-9]+$ ]] || fail "panel-pid-not-recorded-by-current-compositor"
  uid="$(process_uid "$panel")"
  executable="$(readlink -f "/proc/$panel/exe" 2>/dev/null || true)"
  parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$panel/status" 2>/dev/null || true)"
  [[ "$uid" == 1000 && "$executable" == /usr/bin/seyrix-shell && "$parent" == "$compositor" ]] ||
    fail "panel-process-not-current-compositor-child-pid-$panel"
  printf 'PANEL_TERMINATION_BEGIN pid=%s compositor_pid=%s run=%s uid=%s exe=%s\n' \
    "$panel" "$compositor" "$run_id" "$uid" "$executable"
  kill -KILL "$panel"
  for _ in $(seq 1 20); do
    [[ -e "/proc/$panel" ]] || break
    sleep 0.25
  done
  [[ ! -e "/proc/$panel" ]] || fail "panel-process-did-not-exit-pid-$panel"
  current_compositor_pid >/dev/null || fail "compositor-did-not-survive-panel-termination"
  echo PHASE2C_PANEL_CRASH_ISOLATION=passed
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
  crash-panel) kill_panel_child ;;
  kill-mapped-terminal) kill_current_mapped_terminal ;;
  terminate-popup) kill_probe_client "seyrix.qemu.$(test_run_id).popup" ;;
  *) fail "unknown-mode-${1:-missing}" ;;
esac
