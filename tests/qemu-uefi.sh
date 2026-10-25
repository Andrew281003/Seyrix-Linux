#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
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

ovmf_vars="$run_dir/OVMF_VARS.fd"
serial_log="$run_dir/serial.log"
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
} > "$run_dir/run-metadata.txt"

if ! expect "$script_dir/qemu-boot.exp" "$serial_log" "$(command -v qemu-system-x86_64)" \
  "${qemu_args[@]}" > "$run_dir/test.log" 2>&1; then
  tail -n 50 "$run_dir/test.log" >&2
  exit 1
fi
grep -E 'BOOT_TO_LOGIN_MS=|CLEAN_SHUTDOWN=' "$run_dir/test.log"

printf 'QEMU test passed. Evidence: %s\n' "$run_dir"
