#!/usr/bin/env bash
set -Eeuo pipefail

if (( $# < 2 )) || [[ "$1" != "--" ]]; then
  echo "Usage: $0 -- command [argument ...]" >&2
  exit 2
fi
shift
target_command=("$@")

lock_file="${SEYRIX_QEMU_LOCK_FILE:-${TMPDIR:-/tmp}/seyrix-qemu-acceptance.lock}"
lock_timeout="${SEYRIX_QEMU_LOCK_TIMEOUT_SEC:-30}"
if [[ ! "$lock_timeout" =~ ^[0-9]+$ ]] || (( lock_timeout > 3600 )); then
  echo "SEYRIX_QEMU_LOCK_TIMEOUT_SEC must be an integer from 0 through 3600." >&2
  exit 2
fi
if ! command -v flock >/dev/null 2>&1; then
  echo "Required command is missing: flock (util-linux)." >&2
  exit 2
fi

lock_dir="$(dirname -- "$lock_file")"
mkdir -p -- "$lock_dir"
exec {lock_fd}>"$lock_file"
if ! flock -w "$lock_timeout" "$lock_fd"; then
  echo "Timed out after ${lock_timeout}s waiting for the QEMU test lock: $lock_file" >&2
  owner_file="${lock_file}.owner"
  if [[ -r "$owner_file" ]]; then
    cat -- "$owner_file" >&2
  fi
  exit 75
fi

# Do not start a harness guest while any QEMU system process is already alive.
# This check runs after taking the lock, and deliberately refuses rather than
# signalling a guest whose owner is outside this invocation.
existing_qemu=0
for proc_dir in /proc/[0-9]*; do
  [[ -d "$proc_dir" ]] || continue
  pid="${proc_dir##*/}"
  [[ "$pid" == "$$" || "$pid" == "$PPID" ]] && continue
  cmdline="$proc_dir/cmdline"
  [[ -r "$cmdline" ]] || continue
  executable=""
  IFS= read -r -d '' executable < "$cmdline" || true
  case "${executable##*/}" in
    qemu-system-*|qemu-kvm)
      printf 'Refusing to start: existing QEMU process PID %s (%s) is active.\n' \
        "$pid" "${executable##*/}" >&2
      existing_qemu=1
      ;;
  esac
done
if (( existing_qemu )); then
  exit 73
fi

lock_token="$(date -u +%Y%m%dT%H%M%SZ)-$$-${RANDOM}"
owner_file="${lock_file}.owner"
{
  printf 'Lock token: %s\n' "$lock_token"
  printf 'Lock owner PID: %s\n' "$$"
  printf 'Lock started UTC: %s\n' "$(date -u --iso-8601=seconds)"
  printf 'Lock file: %s\n' "$lock_file"
  printf 'Command:'
  printf ' %q' "$@"
  printf '\n'
} > "$owner_file"

child_pid=""
child_is_owned() {
  local pid="$child_pid" expected parent index=0 argument_offset=0
  local -a process_args=()
  [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/cmdline" ]] || return 1
  parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
  [[ "$parent" == "$$" ]] || return 1
  mapfile -d '' -t process_args < "/proc/$pid/cmdline" 2>/dev/null || return 1
  if [[ "${process_args[0]:-}" != "${1:-}" ]]; then
    [[ "${process_args[1]:-}" == "${1:-}" ]] || return 1
    argument_offset=1
  fi
  (( ${#process_args[@]} >= $# + argument_offset )) || return 1
  for expected in "$@"; do
    [[ "${process_args[index + argument_offset]}" == "$expected" ]] || return 1
    index=$((index + 1))
  done
}
remove_owner_record() {
  if [[ -r "$owner_file" ]] && grep -Fqx "Lock token: $lock_token" "$owner_file"; then
    rm -f -- "$owner_file"
  fi
}
forward_signal() {
  local signal="$1"
  local exit_status="$2"
  trap - INT TERM HUP
  if child_is_owned "${target_command[@]}" && kill -0 "$child_pid" 2>/dev/null; then
    kill -s "$signal" "$child_pid" 2>/dev/null || true
    wait "$child_pid" 2>/dev/null || true
  elif [[ -n "$child_pid" ]] && kill -0 "$child_pid" 2>/dev/null; then
    echo "Refusing to signal child PID $child_pid: command or parent identity changed." >&2
  fi
  remove_owner_record
  exit "$exit_status"
}
trap 'forward_signal INT 130' INT
trap 'forward_signal TERM 143' TERM
trap 'forward_signal HUP 129' HUP
trap remove_owner_record EXIT

run_locked_command() (
  exec {lock_fd}>&-
  export SEYRIX_QEMU_LOCK_HELD=1
  export SEYRIX_QEMU_LOCK_FILE="$lock_file"
  export SEYRIX_QEMU_LOCK_OWNER_FILE="$owner_file"
  export SEYRIX_QEMU_LOCK_TOKEN="$lock_token"
  exec "$@"
)
run_locked_command "$@" &
child_pid=$!
set +e
wait "$child_pid"
status=$?
set -e
child_pid=""
remove_owner_record
trap - EXIT INT TERM HUP
exit "$status"
