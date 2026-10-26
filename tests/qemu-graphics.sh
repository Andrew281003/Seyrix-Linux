#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if (( $# != 1 )); then
  echo "Usage: $0 /path/to/seyrix-development.iso" >&2
  exit 2
fi

iso_path="$(realpath -- "$1")"
if [[ ! -f "$iso_path" ]]; then
  echo "ISO does not exist: $iso_path" >&2
  exit 2
fi

for command in qemu-system-x86_64 expect sha256sum realpath stat pacman; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required command is missing: $command" >&2
    exit 2
  fi
done

qemu_bin="$(command -v qemu-system-x86_64)"
workflow="${SEYRIX_QEMU_WORKFLOW:-phase2a}"
smp_count="${SEYRIX_QEMU_SMP:-2}"
tcg_thread="${SEYRIX_QEMU_TCG_THREAD:-}"
renderer_request="${SEYRIX_QEMU_RENDERER:-}"
if [[ "$workflow" != phase2a && "$workflow" != phase2b && "$workflow" != phase2c ]]; then
  echo "Unsupported QEMU workflow: $workflow" >&2
  exit 2
fi
if [[ ! "$smp_count" =~ ^[1-9][0-9]*$ ]] || (( smp_count > 64 )); then
  echo "SEYRIX_QEMU_SMP must be an integer from 1 through 64." >&2
  exit 2
fi
if [[ -n "$tcg_thread" && "$tcg_thread" != single && "$tcg_thread" != multi ]]; then
  echo "SEYRIX_QEMU_TCG_THREAD must be single or multi when set." >&2
  exit 2
fi
if [[ -n "$renderer_request" && "$renderer_request" != pixman && "$renderer_request" != gles2 ]]; then
  echo "SEYRIX_QEMU_RENDERER must be pixman or gles2 when set." >&2
  exit 2
fi
if ! "$qemu_bin" -display help 2>&1 | grep -q '^gtk$'; then
  echo "QEMU GTK display backend is missing. Install qemu-ui-gtk." >&2
  exit 2
fi
if ! "$qemu_bin" -device help 2>&1 | grep -q 'virtio-vga'; then
  echo "QEMU virtio-vga device is missing. Install qemu-hw-display-virtio-vga." >&2
  exit 2
fi
if ! pacman -Q qemu-hw-display-virtio-gpu >/dev/null 2>&1; then
  echo "QEMU virtio GPU module is missing. Install qemu-hw-display-virtio-gpu." >&2
  exit 2
fi

firmware_dir="/usr/share/edk2/x64"
ovmf_code="${SEYRIX_OVMF_CODE:-${firmware_dir}/OVMF_CODE.4m.fd}"
ovmf_vars_template="${SEYRIX_OVMF_VARS:-${firmware_dir}/OVMF_VARS.4m.fd}"
if [[ ! -r "$ovmf_code" || ! -r "$ovmf_vars_template" ]]; then
  echo "OVMF firmware not found. Install edk2-ovmf or set SEYRIX_OVMF_CODE and SEYRIX_OVMF_VARS." >&2
  exit 2
fi

run_id="$(date -u +%Y%m%dT%H%M%SZ)"
run_dir="${SEYRIX_QEMU_GRAPHICS_DIR:-${HOME}/.cache/seyrix-linux/qemu-graphics-${run_id}}"
if [[ -e "$run_dir" ]]; then
  echo "QEMU graphics test directory already exists: $run_dir" >&2
  echo "Set SEYRIX_QEMU_GRAPHICS_DIR to a new, empty path to retry." >&2
  exit 2
fi
mkdir -p "$run_dir"

monitor_port="${SEYRIX_QEMU_MONITOR_PORT:-$((40000 + RANDOM % 20000))}"
ovmf_vars="$run_dir/OVMF_VARS.fd"
cp "$ovmf_vars_template" "$ovmf_vars"

qemu_args=(
  -name "seyrix-${workflow}-graphics-test"
)
if [[ -n "$tcg_thread" ]]; then
  qemu_args+=(-machine q35 -accel "tcg,thread=${tcg_thread}")
else
  qemu_args+=(-machine q35,accel=tcg)
fi
qemu_args+=(
  -cpu qemu64
  -smp "$smp_count"
  -m 4G
  -drive "if=pflash,format=raw,readonly=on,file=${ovmf_code}"
  -drive "if=pflash,format=raw,file=${ovmf_vars}"
  -cdrom "$iso_path"
  -boot order=d,menu=off
  -device virtio-vga
  -device qemu-xhci
  -device usb-kbd
  -device usb-tablet
  -netdev user,id=net0
  -device virtio-net-pci,netdev=net0
  -display gtk,gl=off,show-cursor=on
  -serial stdio
  -qmp "tcp:127.0.0.1:${monitor_port},server=on,wait=off"
  -no-reboot
)

{
  printf '%q' "$qemu_bin"
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
  printf 'QEMU GTK package: %s\n' "$(pacman -Q qemu-ui-gtk)"
  printf 'QEMU virtio GPU package: %s\n' "$(pacman -Q qemu-hw-display-virtio-gpu)"
  printf 'QEMU virtio VGA package: %s\n' "$(pacman -Q qemu-hw-display-virtio-vga)"
  printf 'OVMF package: %s\n' "$(pacman -Q edk2-ovmf)"
  printf 'Display: GTK on WSLg; guest virtio-vga 2D; QEMU TCG\n'
  printf 'RAM: 4 GiB; vCPUs: %s; machine: q35; firmware: OVMF UEFI\n' "$smp_count"
  printf 'TCG thread mode: %s\n' "${tcg_thread:-default}"
  printf 'Input: USB keyboard and USB tablet\n'
  printf 'Storage: read-only ISO only; no physical disk passthrough\n'
  printf 'QMP monitor: localhost TCP port %s\n' "$monitor_port"
  printf 'Compositor stress: %s\n' "${SEYRIX_QEMU_STRESS:-0}"
  printf 'AddressSanitizer/UBSan: %s\n' "${SEYRIX_QEMU_SANITIZERS:-0}"
  printf 'Compositor renderer request: %s\n' "${renderer_request:-automatic}"
} > "$run_dir/run-metadata.txt"

if ! expect "$script_dir/qemu-graphics.exp" "$run_dir" "$monitor_port" "$qemu_bin" \
  "${qemu_args[@]}" > "$run_dir/test.log" 2>&1; then
  tail -n 60 "$run_dir/test.log" >&2
  exit 1
fi

grep -E 'BOOT_TO_LOGIN_MS=|GRAPHICAL_BOOT_MS=|KEYBOARD_INPUT=|POINTER_INPUT=|OUTPUT_CONFIGURATION=|PHASE2C_|WORKSPACE_|WINDOW_|COMPOSITOR_EXIT=|SESSION_EXIT=|CLEAN_SHUTDOWN=' \
  "$run_dir/test.log"
sha256sum "$run_dir"/*.ppm > "$run_dir/screenshots.sha256"
printf 'QEMU %s graphics test passed. Evidence: %s\n' "$workflow" "$run_dir"
