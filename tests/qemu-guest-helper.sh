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
  fi
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
  rm -f -- "$log" "$output" "$identity"
  local run_id
  run_id="$(test_run_id)"
  local -a args=(--resize "$edge" --title "Seyrix QEMU Resize $edge" \
    --app-id "seyrix.qemu.$run_id.resize.$edge.$direction" --initial-size 440x320 --log "$log")
  [[ -z "$min_size" ]] || args+=(--min-size "$min_size")
  [[ -z "$max_size" ]] || args+=(--max-size "$max_size")
  nohup runuser -u seyrix -- env XDG_RUNTIME_DIR="$runtime_dir" \
    WAYLAND_DISPLAY="$wayland_display" seyrix-protocol-probe "${args[@]}" \
    >"$output" 2>&1 </dev/null &

  local ready="" map_line="" pid="" surface="" x="" y="" width="" height=""
  for _ in $(seq 1 45); do
    ready="$(grep 'RESIZE_READY=1 ' "$log" 2>/dev/null | tail -n 1 || true)"
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
    printf 'RESIZE_TARGET_FAILED edge=%s ready=%s map=%s\n' "$edge" \
      "${ready:-missing}" "${map_line:-missing}"
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
  local map_line grab_line commit_line actual_width actual_height actual_x actual_y
  map_line="$(map_for_identity "$pid" "$surface" || true)"
  grab_line="$(grep -F "GEOMETRY_EVIDENCE event=grab-end pid=$pid surface=$surface " \
    "$geometry_log" | tail -n 1 || true)"
  commit_line="$(grep -F "GEOMETRY_EVIDENCE event=commit pid=$pid surface=$surface " \
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
  if [[ ! "$before_width" =~ ^[0-9]+$ || ! "$before_height" =~ ^[0-9]+$ ||
        ! "$after_width" =~ ^[0-9]+$ || ! "$after_height" =~ ^[0-9]+$ ]]; then
    printf 'RESIZE_ASSERTION_FAILED edge=%s reason=malformed-probe-geometry before=%s after=%s\n' \
      "$edge" "$before" "$after"
    resize_cleanup "$edge" "$direction"
    return 1
  fi
  actual_width="$(field "$commit_line" width || true)"
  actual_height="$(field "$commit_line" height || true)"
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
  initial_x="$(field "$map_line" x || true)"
  initial_y="$(field "$map_line" y || true)"
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
        -n "$map_line" && -n "$grab_line" && -n "$commit_line" ]]; then
    printf 'RESIZE_ASSERTION=passed edge=%s before=%s after=%s scene=%s,%s\n' \
      "$edge" "$before" "$after" "$actual_x" "$actual_y"
    return 0
  fi
  printf 'RESIZE_ASSERTION_FAILED edge=%s probe=%s actual=%sx%s changed=%s map=%s grab-end=%s\n' \
    "$edge" "$completion" "${actual_width:-missing}" "${actual_height:-missing}" \
    "$changed" "${map_line:-missing}" "${grab_line:-missing}"
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

case "${1:-}" in
  resize-start) shift; resize_start "$@" ;;
  resize-check) shift; resize_check "$@" ;;
  watch-start) watch_start ;;
  watch-baseline) watch_baseline ;;
  watch-hidden-baseline) watch_baseline WATCH_POINTER_HIDDEN_BASELINE ;;
  watch-check-hidden) watch_check_hidden ;;
  watch-stop) watch_stop ;;
  terminate-client) shift; kill_probe_client "$@" ;;
  crash-panel) kill_panel_child ;;
  kill-mapped-terminal) kill_current_mapped_terminal ;;
  terminate-popup) kill_probe_client "seyrix.qemu.$(test_run_id).popup" ;;
  *) fail "unknown-mode-${1:-missing}" ;;
esac
