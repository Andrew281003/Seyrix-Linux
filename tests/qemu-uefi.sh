#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ "${SEYRIX_QEMU_LOCK_HELD:-0}" != 1 ]]; then
  exec bash "$script_dir/qemu-lock.sh" -- "$script_dir/qemu-uefi.sh" "$@"
fi
if (( $# != 1 )); then
  echo "Usage: $0 /path/to/seyrix.iso" >&2
  exit 2
fi

iso_path="$(realpath -- "$1")"
if [[ ! -f "$iso_path" ]]; then
  echo "ISO does not exist: $iso_path" >&2
  exit 2
fi

for command in qemu-system-x86_64 expect sha256sum; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required command is missing: $command" >&2
    exit 2
  fi
done

firmware_dir="/usr/share/edk2/x64"
ovmf_code="${SEYRIX_OVMF_CODE:-${firmware_dir}/OVMF_CODE.4m.fd}"
ovmf_vars_template="${SEYRIX_OVMF_VARS:-${firmware_dir}/OVMF_VARS.4m.fd}"
if [[ ! -r "$ovmf_code" || ! -r "$ovmf_vars_template" ]]; then
  echo "OVMF firmware not found. Install edk2-ovmf or set SEYRIX_OVMF_CODE and SEYRIX_OVMF_VARS." >&2
  exit 2
fi

run_id="$(date -u +%Y%m%dT%H%M%SZ)"
run_dir="${SEYRIX_QEMU_TEST_DIR:-${HOME}/.cache/seyrix-linux/qemu-${run_id}}"
if [[ -e "$run_dir" ]]; then
  echo "QEMU test directory already exists: $run_dir" >&2
  echo "Set SEYRIX_QEMU_TEST_DIR to a new, empty path to retry." >&2
  exit 2
fi
mkdir -p "$run_dir"
run_started_utc="$(date -u --iso-8601=seconds)"
serial_log="$run_dir/serial.log"
expect_pid=""
expect_is_owned() {
  local pid="$1" parent expect_executable actual_executable
  local -a process_args=()
  [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/cmdline" ]] || return 1
  parent="$(awk '/^PPid:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null || true)"
  expect_executable="$(readlink -f "$(command -v expect)")"
  actual_executable="$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)"
  mapfile -d '' -t process_args < "/proc/$pid/cmdline" 2>/dev/null || return 1
  [[ "$parent" == "$$" && "$actual_executable" == "$expect_executable" &&
      "${process_args[1]:-}" == "$script_dir/qemu-boot.exp" &&
      "${process_args[2]:-}" == "$serial_log" ]]
}
finish_uefi_run() {
  local status=$?
  trap - EXIT
  {
    printf 'Run finished UTC: %s\n' "$(date -u --iso-8601=seconds)"
    printf 'Run exit status: %s\n' "$status"
    printf 'Expect PID: %s\n' "${expect_pid:-not-started}"
  } >> "$run_dir/run-metadata.txt"
  if [[ -n "${SEYRIX_QEMU_LOCK_OWNER_FILE:-}" && -r "$SEYRIX_QEMU_LOCK_OWNER_FILE" ]]; then
    cp -- "$SEYRIX_QEMU_LOCK_OWNER_FILE" "$run_dir/qemu-lock-owner.txt"
  fi
  if [[ -f "$run_dir/qemu.pid" ]]; then
    local qemu_pid qemu_cmdline
    qemu_pid="$(<"$run_dir/qemu.pid")"
    qemu_cmdline=""
    if [[ "$qemu_pid" =~ ^[0-9]+$ && -r "/proc/$qemu_pid/cmdline" ]]; then
      qemu_cmdline="$(tr '\0' ' ' < "/proc/$qemu_pid/cmdline" 2>/dev/null || true)"
    fi
    {
      printf 'Observed UTC: %s\nQEMU PID: %s\n' "$(date -u --iso-8601=seconds)" "$qemu_pid"
      if [[ "$qemu_cmdline" == *"seyrix-uefi-test-${run_id}"* ]]; then
        printf 'Owned process state: still-running-after-test\nCommand line: %s\n' "$qemu_cmdline"
        sed -n '1,8p' "/proc/$qemu_pid/status" 2>/dev/null || true
      elif [[ -n "$qemu_cmdline" ]]; then
        printf 'Owned process state: PID-reused-or-identity-mismatch; no signal sent\n'
      else
        printf 'Owned process state: exited\n'
      fi
    } > "$run_dir/qemu-process-state.txt"
  else
    printf 'QEMU PID was not recorded before test exit.\n' > "$run_dir/qemu-process-state.txt"
  fi
  if (( status != 0 )); then
    printf 'Failure exit status: %s\nFinished UTC: %s\n' \
      "$status" "$(date -u --iso-8601=seconds)" > "$run_dir/failure-result.txt"
  fi
  exit "$status"
}
trap finish_uefi_run EXIT
handle_uefi_signal() {
  local signal="$1"
  local status="$2"
  trap - INT TERM HUP
  if expect_is_owned "$expect_pid" && kill -0 "$expect_pid" 2>/dev/null; then
    kill -s "$signal" "$expect_pid" 2>/dev/null || true
    wait "$expect_pid" 2>/dev/null || true
  elif [[ -n "$expect_pid" ]] && kill -0 "$expect_pid" 2>/dev/null; then
    echo "Refusing to signal Expect PID $expect_pid: run identity changed." >&2
  fi
  exit "$status"
}
trap 'handle_uefi_signal INT 130' INT
trap 'handle_uefi_signal TERM 143' TERM
trap 'handle_uefi_signal HUP 129' HUP

ovmf_vars="$run_dir/OVMF_VARS.fd"
cp "$ovmf_vars_template" "$ovmf_vars"

qemu_args=(
  -machine q35,accel=tcg
  -cpu qemu64
  -smp 2
  -m 4G
  -drive "if=pflash,format=raw,readonly=on,file=${ovmf_code}"
  -drive "if=pflash,format=raw,file=${ovmf_vars}"
  -cdrom "$iso_path"
  -boot order=d,menu=off
  -netdev user,id=net0
  -device virtio-net-pci,netdev=net0
  -display none
  -serial stdio
  -monitor none
  -no-reboot
)
qemu_name="seyrix-uefi-test-${run_id}"
qemu_args=(-name "$qemu_name" "${qemu_args[@]}")

{
  printf '%q' "$(command -v qemu-system-x86_64)"
  printf ' %q' "${qemu_args[@]}"
  printf '\n'
} > "$run_dir/qemu-command.txt"
{
  printf 'Run started UTC: %s\n' "$(date -u --iso-8601=seconds)"
  printf 'ISO: %s\n' "$iso_path"
  printf 'ISO SHA-256: %s\n' "$(sha256sum "$iso_path" | awk '{print $1}')"
  printf 'ISO bytes: %s\n' "$(stat --printf='%s' "$iso_path")"
  printf 'Host kernel: %s\n' "$(uname -r)"
  printf 'QEMU package: %s\n' "$(pacman -Q qemu-system-x86)"
  printf 'OVMF package: %s\n' "$(pacman -Q edk2-ovmf)"
  printf 'Expected output: %s\n' "$serial_log"
  printf 'QEMU run ID: %s\n' "$run_id"
  printf 'QEMU lock file: %s\n' "${SEYRIX_QEMU_LOCK_FILE:-unavailable}"
  printf 'QEMU lock timeout seconds: %s\n' "${SEYRIX_QEMU_LOCK_TIMEOUT_SEC:-30}"
  printf 'QEMU lock token: %s\n' "${SEYRIX_QEMU_LOCK_TOKEN:-unavailable}"
  printf 'Run started UTC: %s\n' "$run_started_utc"
} > "$run_dir/run-metadata.txt"

expect "$script_dir/qemu-boot.exp" "$serial_log" "$(command -v qemu-system-x86_64)" \
  "${qemu_args[@]}" > "$run_dir/test.log" 2>&1 &
expect_pid=$!
if ! wait "$expect_pid"; then
  tail -n 50 "$run_dir/test.log" >&2
  exit 1
fi
grep -E 'BOOT_TO_LOGIN_MS=|CLEAN_SHUTDOWN=' "$run_dir/test.log"

printf 'QEMU test passed. Evidence: %s\n' "$run_dir"
