#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
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

summary="$evidence_root/campaign.tsv"
printf 'run\tresult\texit_code\tstarted_utc\tfinished_utc\tboot_to_login_ms\tcompositor_ready_ms\tcompositor_startup_ms\tapplication_launch\tcompositor_exit\tguest_shutdown\n' > "$summary"
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
  if SEYRIX_QEMU_GRAPHICS_DIR="$run_dir" SEYRIX_QEMU_WORKFLOW=phase2b \
      "$script_dir/qemu-graphics.sh" "$iso_path" > "$wrapper_log" 2>&1; then
    result="passed"
    exit_code=0
  else
    exit_code=$?
    result="failed"
    failures=$((failures + 1))
  fi
  finished_utc="$(date -u --iso-8601=seconds)"

  mkdir -p "$run_dir"
  mv -- "$wrapper_log" "$run_dir/launcher.log"
  test_log="$run_dir/test.log"
  if [[ -f "$test_log" ]]; then
    boot_to_login_ms="$(sed -n 's/.*BOOT_TO_LOGIN_MS=\([0-9][0-9]*\).*/\1/p' "$test_log" | tail -n 1)"
    compositor_ready_ms="$(sed -n 's/.*GRAPHICAL_BOOT_MS=\([0-9][0-9]*\).*/\1/p' "$test_log" | tail -n 1)"
    compositor_startup_ms="$(sed -n 's/.*COMPOSITOR_STARTUP_MS=\([0-9][0-9]*\).*/\1/p' "$test_log" | tail -n 1)"
    application_launch="$(sed -n 's/.*APPLICATION_LAUNCH=\(passed\|failed\).*/\1/p' "$test_log" | tail -n 1)"
    compositor_exit="$(sed -n 's/.*COMPOSITOR_EXIT=\(passed\|failed\).*/\1/p' "$test_log" | tail -n 1)"
    guest_shutdown="$(sed -n 's/.*CLEAN_SHUTDOWN=\(passed\|failed\).*/\1/p' "$test_log" | tail -n 1)"
    warning_inputs=("$test_log")
    if [[ -f "$run_dir/serial.log" ]]; then
      warning_inputs+=("$run_dir/serial.log")
    fi
    grep -Ein '(^\[[[:space:]]*[0-9]+(\.[0-9]+)?\].*(warning|warn|error|oops|panic|segfault)|^[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+ \[(warning|warn|error)\]|AddressSanitizer|runtime error:|^qemu-system-x86_64: (warning|error))' "${warning_inputs[@]}" > "$run_dir/warnings-errors.log" || true
  else
    boot_to_login_ms=""
    compositor_ready_ms=""
    compositor_startup_ms=""
    application_launch=""
    compositor_exit=""
    guest_shutdown=""
    : > "$run_dir/warnings-errors.log"
  fi

  for variable in boot_to_login_ms compositor_ready_ms compositor_startup_ms application_launch compositor_exit guest_shutdown; do
    if [[ -z "${!variable}" ]]; then
      printf -v "$variable" 'not-reached'
    fi
  done
  {
    printf 'Run: %s\nResult: %s\nExit code: %s\nStarted UTC: %s\nFinished UTC: %s\n' \
      "$run_id" "$result" "$exit_code" "$started_utc" "$finished_utc"
    printf 'Boot to serial login ms: %s\nCompositor ready from QEMU start ms: %s\n' \
      "$boot_to_login_ms" "$compositor_ready_ms"
    printf 'Compositor startup ms: %s\nApplication launch: %s\nCompositor exit: %s\nGuest shutdown: %s\n' \
      "$compositor_startup_ms" "$application_launch" "$compositor_exit" "$guest_shutdown"
  } > "$run_dir/run-result.txt"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$run_id" "$result" "$exit_code" "$started_utc" "$finished_utc" \
    "$boot_to_login_ms" "$compositor_ready_ms" "$compositor_startup_ms" \
    "$application_launch" "$compositor_exit" "$guest_shutdown" >> "$summary"
  echo "[$run/$run_count] $result; evidence: $run_dir"
done

printf 'Completed %s independent cold boots; failures: %s\n' "$run_count" "$failures"
printf 'Campaign summary: %s\n' "$summary"
if (( failures > 0 )); then
  exit 1
fi
