#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ "${SEYRIX_QEMU_LOCK_HELD:-0}" != 1 ]]; then
  exec bash "$script_dir/qemu-lock.sh" -- "$script_dir/qemu-cold-boot-campaign.sh" "$@"
fi
if (( $# < 2 || $# > 3 )); then
  echo "Usage: $0 /path/to/seyrix-phase2b.iso /new/evidence/directory [run-count]" >&2
  exit 2
fi

iso_path="$(realpath -- "$1")"
if [[ ! -f "$iso_path" ]]; then
  echo "ISO does not exist: $iso_path" >&2
  exit 2
fi

run_count="${3:-20}"
if [[ ! "$run_count" =~ ^[0-9]+$ ]] || (( run_count < 20 )); then
  echo "Run count must be an integer of at least 20." >&2
  exit 2
fi

evidence_root="$(realpath -m -- "$2")"
if [[ -e "$evidence_root" ]]; then
  echo "Evidence directory already exists: $evidence_root" >&2
  echo "Choose a new path. Existing run evidence is never overwritten." >&2
  exit 2
fi
mkdir -p "$evidence_root"
if [[ -n "${SEYRIX_QEMU_LOCK_OWNER_FILE:-}" && -r "$SEYRIX_QEMU_LOCK_OWNER_FILE" ]]; then
  cp -- "$SEYRIX_QEMU_LOCK_OWNER_FILE" "$evidence_root/qemu-lock-owner.txt"
fi
campaign_child_pid=""
run_dir=""
campaign_child_is_owned() {
  local pid="$campaign_child_pid" parent
  local -a process_args=()
  [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/cmdline" ]] || return 1
  parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
  mapfile -d '' -t process_args < "/proc/$pid/cmdline" 2>/dev/null || return 1
  [[ "$parent" == "$$" && "${process_args[1]:-}" == "$script_dir/qemu-graphics.sh" &&
      "${process_args[2]:-}" == "$iso_path" ]] &&
    tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -Fxq "SEYRIX_QEMU_GRAPHICS_DIR=$run_dir"
}
handle_campaign_signal() {
  local signal="$1"
  local status="$2"
  trap - INT TERM HUP
  if campaign_child_is_owned && kill -0 "$campaign_child_pid" 2>/dev/null; then
    kill -s "$signal" "$campaign_child_pid" 2>/dev/null || true
    wait "$campaign_child_pid" 2>/dev/null || true
  elif [[ -n "$campaign_child_pid" ]] && kill -0 "$campaign_child_pid" 2>/dev/null; then
    echo "Refusing to signal campaign child PID $campaign_child_pid: run identity changed." >&2
  fi
  exit "$status"
}
trap 'handle_campaign_signal INT 130' INT
trap 'handle_campaign_signal TERM 143' TERM
trap 'handle_campaign_signal HUP 129' HUP

summary="$evidence_root/campaign.tsv"
printf 'run\tresult\texit_code\tstarted_utc\tfinished_utc\tboot_to_login_ms\tcompositor_ready_ms\tcompositor_startup_ms\trenderer_identity\tapplication_launch\tcompositor_exit\tguest_shutdown\n' > "$summary"
failures=0

for ((run = 1; run <= run_count; run++)); do
  run_id="$(printf 'boot-%02d' "$run")"
  run_dir="$evidence_root/$run_id"
  wrapper_log="$evidence_root/$run_id-launcher.log"
  if [[ -e "$run_dir" || -e "$wrapper_log" ]]; then
    echo "Run evidence already exists for $run_id. Refusing to overwrite." >&2
    exit 2
  fi

  started_utc="$(date -u --iso-8601=seconds)"
  echo "[$run/$run_count] Starting independent cold boot: $started_utc"
  SEYRIX_QEMU_GRAPHICS_DIR="$run_dir" SEYRIX_QEMU_WORKFLOW=phase2b \
    "$script_dir/qemu-graphics.sh" "$iso_path" > "$wrapper_log" 2>&1 &
  campaign_child_pid=$!
  if wait "$campaign_child_pid"; then
    result="passed"
    exit_code=0
  else
    exit_code=$?
    result="failed"
    failures=$((failures + 1))
  fi
  campaign_child_pid=""
  finished_utc="$(date -u --iso-8601=seconds)"

  mkdir -p "$run_dir"
  mv -- "$wrapper_log" "$run_dir/launcher.log"
  test_log="$run_dir/test.log"
  if [[ -f "$test_log" ]]; then
    boot_to_login_ms="$(sed -n 's/.*BOOT_TO_LOGIN_MS=\([0-9][0-9]*\).*/\1/p' "$test_log" | tail -n 1)"
    compositor_ready_ms="$(sed -n 's/.*GRAPHICAL_BOOT_MS=\([0-9][0-9]*\).*/\1/p' "$test_log" | tail -n 1)"
    compositor_startup_ms="$(sed -n 's/.*COMPOSITOR_STARTUP_MS=\([0-9][0-9]*\).*/\1/p' "$test_log" | tail -n 1)"
    renderer_identity="$(sed -n 's/.*GL renderer: //p' "$test_log" | head -n 1 | tr -d '\r')"
    application_launch="$(sed -n 's/.*APPLICATION_LAUNCH=\(passed\|failed\).*/\1/p' "$test_log" | tail -n 1)"
    compositor_exit="$(sed -n 's/.*COMPOSITOR_EXIT=\(passed\|failed\).*/\1/p' "$test_log" | tail -n 1)"
    guest_shutdown="$(sed -n 's/.*CLEAN_SHUTDOWN=\(passed\|failed\).*/\1/p' "$test_log" | tail -n 1)"
    warning_inputs=("$test_log")
    if [[ -f "$run_dir/serial.log" ]]; then
      warning_inputs+=("$run_dir/serial.log")
    fi
    grep -Ein '(^\[[[:space:]]*[0-9]+(\.[0-9]+)?\].*(warning|warn|error|oops|panic|segfault)|^[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+ \[(warning|warn|error)\]|AddressSanitizer|runtime error:|^qemu-system-x86_64: (warning|error))' "${warning_inputs[@]}" > "$run_dir/warnings-errors.log" || true
    if [[ ! "$renderer_identity" =~ ^llvmpipe([[:space:]]|$) ]]; then
      printf 'Required llvmpipe renderer was not observed; recorded identity: %s\n' \
        "${renderer_identity:-not-reached}" >> "$run_dir/warnings-errors.log"
      if [[ "$result" == passed ]]; then
        result="failed"
        exit_code=1
        failures=$((failures + 1))
      fi
    fi
  else
    boot_to_login_ms=""
    compositor_ready_ms=""
    compositor_startup_ms=""
    renderer_identity="not-reached"
    application_launch=""
    compositor_exit=""
    guest_shutdown=""
    : > "$run_dir/warnings-errors.log"
  fi

  for variable in boot_to_login_ms compositor_ready_ms compositor_startup_ms renderer_identity application_launch compositor_exit guest_shutdown; do
    if [[ -z "${!variable}" ]]; then
      printf -v "$variable" 'not-reached'
    fi
  done
  {
    printf 'Run: %s\nResult: %s\nExit code: %s\nStarted UTC: %s\nFinished UTC: %s\n' \
      "$run_id" "$result" "$exit_code" "$started_utc" "$finished_utc"
    printf 'Boot to serial login ms: %s\nCompositor ready from QEMU start ms: %s\n' \
      "$boot_to_login_ms" "$compositor_ready_ms"
    printf 'Compositor startup ms: %s\nRenderer identity: %s\nApplication launch: %s\nCompositor exit: %s\nGuest shutdown: %s\n' \
      "$compositor_startup_ms" "$renderer_identity" "$application_launch" "$compositor_exit" "$guest_shutdown"
  } > "$run_dir/run-result.txt"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$run_id" "$result" "$exit_code" "$started_utc" "$finished_utc" \
    "$boot_to_login_ms" "$compositor_ready_ms" "$compositor_startup_ms" "$renderer_identity" \
    "$application_launch" "$compositor_exit" "$guest_shutdown" >> "$summary"
  echo "[$run/$run_count] $result; evidence: $run_dir"
done

printf 'Completed %s independent cold boots; failures: %s\n' "$run_count" "$failures"
printf 'Campaign summary: %s\n' "$summary"
if (( failures > 0 )); then
  exit 1
fi
