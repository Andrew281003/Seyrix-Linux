#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "$script_dir/qemu-graphics-preflight.sh"
if [[ "${SEYRIX_QEMU_LOCK_HELD:-0}" != 1 ]]; then
  exec bash "$script_dir/qemu-lock.sh" -- "$script_dir/qemu-graphics.sh" "$@"
fi
repo_root="$(cd -- "$script_dir/.." && pwd -P)"
source "$repo_root/build/retention.sh"
seyrix_retention_init
seyrix_retention_acquire_lock
if (( $# != 1 )); then
  echo "Usage: $0 /path/to/seyrix-development.iso" >&2
  exit 2
fi

iso_path="$(realpath -- "$1")"
if [[ ! -f "$iso_path" ]]; then
  echo "ISO does not exist: $iso_path" >&2
  exit 2
fi

for command in qemu-system-x86_64 expect sha256sum realpath stat pacman python3; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required command is missing: $command" >&2
    exit 2
  fi
done

qemu_bin="$(command -v qemu-system-x86_64)"
workflow="${SEYRIX_QEMU_WORKFLOW:-phase2a}"
resolution_profile="${SEYRIX_QEMU_RESOLUTION_PROFILE:-640x480}"
case "$resolution_profile" in
  640x480)
    vga_device="virtio-vga"
    expected_output_size="640x480"
    ;;
  800x600)
    vga_device="VGA,edid=on,xres=800,yres=600"
    expected_output_size="800x600"
    ;;
  1280x720)
    vga_device="VGA,edid=on,xres=1280,yres=720"
    expected_output_size="1280x720"
    ;;
  1920x1080)
    vga_device="VGA,edid=on,xres=1920,yres=1080"
    expected_output_size="1920x1080"
    ;;
  *)
    echo "SEYRIX_QEMU_RESOLUTION_PROFILE must be 640x480, 800x600, 1280x720, or 1920x1080." >&2
    exit 2
    ;;
esac
export SEYRIX_QEMU_EXPECTED_OUTPUT_SIZE="$expected_output_size"
export SEYRIX_QEMU_WORKFLOW="$workflow"
smp_count="${SEYRIX_QEMU_SMP:-2}"
tcg_thread="${SEYRIX_QEMU_TCG_THREAD:-}"
renderer_request="${SEYRIX_QEMU_RENDERER:-}"
readiness_only="${SEYRIX_QEMU_READINESS_ONLY:-0}"
layout_only="${SEYRIX_QEMU_SETTINGS_LAYOUT_ONLY:-0}"
settings_ipc_only="${SEYRIX_QEMU_SETTINGS_IPC_ONLY:-0}"
sanitizer_only="${SEYRIX_QEMU_SANITIZER_ONLY:-0}"
if [[ "$readiness_only" != 0 && "$readiness_only" != 1 ]] ||
    [[ "$layout_only" != 0 && "$layout_only" != 1 ]] ||
    [[ "$settings_ipc_only" != 0 && "$settings_ipc_only" != 1 ]] ||
    [[ "$sanitizer_only" != 0 && "$sanitizer_only" != 1 ]] ||
    [[ "$readiness_only" == 1 && ( "$layout_only" == 1 || "$settings_ipc_only" == 1 ) ]] ||
    [[ "$layout_only" == 1 && "$settings_ipc_only" == 1 ]] ||
    [[ ( "$readiness_only" == 1 || "$layout_only" == 1 || "$settings_ipc_only" == 1 || "$sanitizer_only" == 1 ) && "$workflow" != phase2d3b ]] ||
    [[ "$sanitizer_only" == 1 && ( "$readiness_only" == 1 || "$layout_only" == 1 || "$settings_ipc_only" == 1 || "${SEYRIX_QEMU_SANITIZERS:-0}" != 1 ) ]]; then
  echo "Scoped QEMU modes require phase2d3b; sanitizer-only also requires sanitizers and cannot be combined." >&2
  exit 2
fi
export SEYRIX_QEMU_READINESS_ONLY="$readiness_only"
export SEYRIX_QEMU_SETTINGS_LAYOUT_ONLY="$layout_only"
export SEYRIX_QEMU_SETTINGS_IPC_ONLY="$settings_ipc_only"
export SEYRIX_QEMU_SANITIZER_ONLY="$sanitizer_only"
if [[ "$workflow" != phase2a && "$workflow" != phase2b && "$workflow" != phase2c && "$workflow" != phase2d2 && "$workflow" != phase2d3b ]]; then
  echo "Unsupported QEMU workflow: $workflow" >&2
  exit 2
fi
settings_trace_only="${SEYRIX_QEMU_SETTINGS_RENDER_DIAGNOSTIC:-0}"
if [[ "$settings_trace_only" != 0 && "$settings_trace_only" != 1 ]]; then
  echo "SEYRIX_QEMU_SETTINGS_RENDER_DIAGNOSTIC must be 0 or 1." >&2
  exit 2
fi
if [[ "$settings_trace_only" == 1 && "$workflow" != phase2d3b ]]; then
  echo "Settings render tracing requires the phase2d3b workflow." >&2
  exit 2
fi
qemu_graphics_validate_support_files "$script_dir" "$workflow" "$settings_trace_only"
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
qemu_name="seyrix-${workflow}-graphics-test-${run_id}"
run_dir="${SEYRIX_QEMU_GRAPHICS_DIR:-${SEYRIX_ARTIFACT_ROOT}/qemu-graphics-${run_id}}"
run_dir="$(realpath -m -- "$run_dir")"
if ! seyrix_retention_is_managed_path "$run_dir" ||
	! seyrix_retention_is_child "$SEYRIX_ARTIFACT_ROOT" "$run_dir"; then
	seyrix_retention_error "managed QEMU output must be a direct child of the artifact root: $run_dir"
	exit 2
fi
seyrix_retention_preflight "$SEYRIX_RETENTION_QEMU_RESERVE_BYTES" "$run_dir"
if [[ -e "$run_dir" ]]; then
  echo "QEMU graphics test directory already exists: $run_dir" >&2
  echo "Set SEYRIX_QEMU_GRAPHICS_DIR to a new, empty path to retry." >&2
  exit 2
fi
mkdir -p "$run_dir"
chmod 700 "$run_dir"
seyrix_retention_begin "$run_dir" qemu-run

run_started_utc="$(date -u --iso-8601=seconds)"
monitor_port="${SEYRIX_QEMU_MONITOR_PORT:-$((40000 + RANDOM % 20000))}"
run_status=1
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
      "${process_args[1]:-}" == "$script_dir/qemu-graphics.exp" &&
      "${process_args[2]:-}" == "$run_dir" &&
      "${process_args[3]:-}" == "$monitor_port" &&
      "${process_args[4]:-}" == "$qemu_bin" ]]
}
finish_graphics_run() {
  local status=$?
  local trace_collection_ok=1
  trap - EXIT
  if [[ "$settings_ipc_only" == 1 ]]; then
    if python3 "$script_dir/settings-wayland-trace-collect.py" \
        "$run_dir/test.log" "$run_dir" > "$run_dir/wayland-trace-collection.txt" 2>&1; then
      trace_collection_ok=0
    else
      status=1
    fi
  fi
  {
    printf 'Run finished UTC: %s\n' "$(date -u --iso-8601=seconds)"
    printf 'Run exit status: %s\n' "$status"
    printf 'Expect PID: %s\n' "${expect_pid:-not-started}"
  } >> "$run_dir/run-metadata.txt"
  if [[ -n "${SEYRIX_QEMU_LOCK_OWNER_FILE:-}" && -r "$SEYRIX_QEMU_LOCK_OWNER_FILE" ]]; then
    cp -- "$SEYRIX_QEMU_LOCK_OWNER_FILE" "$run_dir/qemu-lock-owner.txt"
  fi
  if [[ -f "$run_dir/test.log" ]]; then
    sed -nE 's/^(SEYRIX-USER# )?(STAGE_BEGIN |STAGE_RESULT )/\2/p' "$run_dir/test.log" \
      > "$run_dir/stage-results.log" || true
  else
    : > "$run_dir/stage-results.log"
  fi
  if [[ -f "$run_dir/qemu.pid" ]]; then
    local qemu_pid
    qemu_pid="$(<"$run_dir/qemu.pid")"
    {
      printf 'Observed UTC: %s\n' "$(date -u --iso-8601=seconds)"
      printf 'QEMU PID: %s\n' "$qemu_pid"
      if [[ "$qemu_pid" =~ ^[0-9]+$ && -r "/proc/$qemu_pid/cmdline" ]]; then
        local qemu_cmdline
        qemu_cmdline="$(tr '\0' ' ' < "/proc/$qemu_pid/cmdline" 2>/dev/null || true)"
        if [[ "$qemu_cmdline" == *"$qemu_name"* ]]; then
          printf 'Owned process state: still-running-after-test\n'
          printf 'Command line: %s\n' "$qemu_cmdline"
          sed -n '1,8p' "/proc/$qemu_pid/status" 2>/dev/null || true
        else
          printf 'Owned process state: PID-reused-or-identity-mismatch; no signal sent\n'
        fi
      else
        printf 'Owned process state: exited\n'
      fi
    } > "$run_dir/qemu-process-state.txt"
  else
    printf 'QEMU PID was not recorded before test exit.\n' > "$run_dir/qemu-process-state.txt"
  fi
  shopt -s nullglob
  local screenshots=("$run_dir"/*.ppm)
  if (( ${#screenshots[@]} > 0 )); then
    sha256sum "${screenshots[@]}" > "$run_dir/screenshots.sha256"
  else
    : > "$run_dir/screenshots.sha256"
  fi
  local run_result="PASS"
  if (( status != 0 )); then
    run_result="FAIL"
  fi
  if [[ -e "$run_dir/.seyrix-failed" ]] || \
      [[ ! -f "$run_dir/expect-exit-status.txt" ]] || \
      ! grep -Fxq 'Expect exit status: 0' "$run_dir/expect-exit-status.txt" || \
      [[ ! -f "$run_dir/qemu-exit-status.txt" ]] || \
      ! grep -Fxq 'QEMU exit status: 0' "$run_dir/qemu-exit-status.txt" || \
      grep -q 'still-running-after-test' "$run_dir/qemu-process-state.txt" 2>/dev/null; then
    run_result="INCOMPLETE"
  fi
  if [[ -e "$run_dir/.seyrix-failed" ]] || \
      [[ ! -f "$run_dir/test.log" ]] || \
      grep -Eq 'STAGE_RESULT .*result=(FAIL|INCOMPLETE)' "$run_dir/stage-results.log" || \
      ! awk '/^STAGE_BEGIN / { begun++ } /^STAGE_RESULT .*result=PASS/ { passed++ } END { exit !(begun > 0 && begun == passed) }' "$run_dir/stage-results.log"; then
    run_result="INCOMPLETE"
  fi
  if [[ -f "$run_dir/run-result.txt" ]] && \
      ! grep -Fxq "Run result: $run_result" "$run_dir/run-result.txt"; then
    run_result="INCOMPLETE"
  fi
  if [[ "$settings_ipc_only" == 1 && "$trace_collection_ok" != 0 ]]; then
    run_result="INCOMPLETE"
  fi
  {
    printf 'Run result: %s\nRun exit status: %s\nFinished UTC: %s\n' \
      "$run_result" "$status" "$(date -u --iso-8601=seconds)"
    printf 'Final status requires zero exit, all stages passed, and no failure marker.\n'
  } > "$run_dir/run-result.txt.tmp"
  mv -f -- "$run_dir/run-result.txt.tmp" "$run_dir/run-result.txt"
  if [[ "$run_result" == PASS ]]; then
    seyrix_retention_finish_qemu_run "$run_dir" "$iso_path" 0 || {
      seyrix_retention_error "could not retain successful QEMU run at $run_dir."
      status=1
    }
  else
    seyrix_retention_finish_qemu_run "$run_dir" "$iso_path" 1 || true
  fi
  if [[ -d "$run_dir" ]]; then
    (
      cd -- "$run_dir"
      find . -maxdepth 1 -type f ! -name 'raw-artifacts.sha256*' -print0 |
        LC_ALL=C sort -z | xargs -0r sha256sum
    ) > "$run_dir/raw-artifacts.sha256.tmp"
    mv -f -- "$run_dir/raw-artifacts.sha256.tmp" "$run_dir/raw-artifacts.sha256"
  fi
  if [[ -d "$run_dir" ]]; then
    printf 'QEMU %s graphics test result: %s. Evidence: %s\n' \
      "$workflow" "$(sed -n 's/^Run result: //p' "$run_dir/run-result.txt" | head -n 1)" "$run_dir"
  else
    printf 'QEMU %s graphics test passed. Summary: %s/history/%s.summary\n' \
      "$workflow" "$SEYRIX_ARTIFACT_ROOT" "$(basename -- "$run_dir")"
  fi
  exit "$status"
}
trap finish_graphics_run EXIT
handle_graphics_signal() {
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
trap 'handle_graphics_signal INT 130' INT
trap 'handle_graphics_signal TERM 143' TERM
trap 'handle_graphics_signal HUP 129' HUP

ovmf_vars="$run_dir/OVMF_VARS.fd"
cp "$ovmf_vars_template" "$ovmf_vars"

qemu_args=(
  -name "$qemu_name"
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
  -device "$vga_device"
  -device qemu-xhci
  -device virtio-serial-pci,id=seyrix_control_bus,max_ports=2
  -chardev pty,id=seyrix_user_console
  -device virtserialport,bus=seyrix_control_bus.0,chardev=seyrix_user_console,name=org.seyrix.qemu-control,nr=1
  -device usb-kbd
  -device usb-tablet
  -netdev user,id=net0
  -device virtio-net-pci,netdev=net0
  -display gtk,gl=off,show-cursor=on
  -chardev file,id=seyrix_console,path=${run_dir}/serial.log
  -device isa-serial,id=seyrix_uart0,chardev=seyrix_console
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
  printf 'Display: GTK on WSLg; guest %s 2D; QEMU TCG\n' "$vga_device"
  printf 'Resolution acceptance profile: %s; expected active output: %s\n' "$resolution_profile" "$expected_output_size"
  printf 'RAM: 4 GiB; vCPUs: %s; machine: q35; firmware: OVMF UEFI\n' "$smp_count"
  printf 'TCG thread mode: %s\n' "${tcg_thread:-default}"
  printf 'Input: USB keyboard and USB tablet\n'
  printf 'Storage: read-only ISO only; no physical disk passthrough\n'
  printf 'QMP monitor: localhost TCP port %s\n' "$monitor_port"
  printf 'Guest serial: explicit ISA 16550A UART on ttyS0, captured to serial.log\n'
  printf 'Fallback control: named QEMU virtio-serial data port; Seyrix uid 1000 shell; 25-minute maximum; only active when the named device exists\n'
  printf 'Compositor stress: %s\n' "${SEYRIX_QEMU_STRESS:-0}"
  printf 'AddressSanitizer/UBSan: %s\n' "${SEYRIX_QEMU_SANITIZERS:-0}"
  printf 'Readiness-only mode: %s\n' "$readiness_only"
  printf 'Settings-layout-only mode: %s\n' "$layout_only"
  printf 'Settings-IPC-only mode: %s\n' "$settings_ipc_only"
  printf 'Settings render diagnostic mode: %s\n' "$settings_trace_only"
  printf 'Sanitizer-only diagnostic mode: %s\n' "$sanitizer_only"
  printf 'Compositor renderer request: %s\n' "${renderer_request:-automatic}"
  printf 'QEMU run ID: %s\n' "$run_id"
  printf 'QEMU lock file: %s\n' "${SEYRIX_QEMU_LOCK_FILE:-unavailable}"
  printf 'QEMU lock timeout seconds: %s\n' "${SEYRIX_QEMU_LOCK_TIMEOUT_SEC:-30}"
  printf 'QEMU lock token: %s\n' "${SEYRIX_QEMU_LOCK_TOKEN:-unavailable}"
  printf 'Run started UTC: %s\n' "$run_started_utc"
} > "$run_dir/run-metadata.txt"

if [[ "${SEYRIX_QEMU_LOCK_HELD:-0}" != 1 ]]; then
  echo "Internal error: QEMU acceptance is not running under its ownership lock." >&2
  exit 2
fi
expect "$script_dir/qemu-graphics.exp" "$run_dir" "$monitor_port" "$qemu_bin" \
  "${qemu_args[@]}" > "$run_dir/test.log" 2>&1 &
expect_pid=$!
if wait "$expect_pid"; then
  run_status=0
  printf 'Expect exit status: 0\n' > "$run_dir/expect-exit-status.txt"
else
  run_status=$?
  printf 'Expect exit status: %s\n' "$run_status" > "$run_dir/expect-exit-status.txt"
  tail -n 60 "$run_dir/test.log" >&2
  exit "$run_status"
fi
qemu_pid="$(<"$run_dir/qemu.pid")"
for _ in $(seq 1 60); do
  [[ ! -r "/proc/$qemu_pid/cmdline" ]] && break
  sleep 0.25
done
if [[ -r "/proc/$qemu_pid/cmdline" ]] && \
    [[ "$(tr '\0' ' ' < "/proc/$qemu_pid/cmdline")" == *"$qemu_name"* ]]; then
  printf 'QEMU exit status: unavailable (owned process still running)\n' > "$run_dir/qemu-exit-status.txt"
  run_status=1
  echo "Owned QEMU process remained after Expect completed." >&2
  exit "$run_status"
fi
printf 'QEMU exit status: 0\n' > "$run_dir/qemu-exit-status.txt"

grep -E 'BOOT_TO_LOGIN_MS=|GRAPHICAL_BOOT_MS=|GRAPHICAL_READY=|STAGE_BEGIN|STAGE_RESULT|SANITIZER_FINDINGS=|SETTINGS_IPC_|KEYBOARD_INPUT=|POINTER_INPUT=|OUTPUT_CONFIGURATION=|REAL_SERIAL_|SETTINGS_CONFIG_|SETTINGS_ENDPOINT_|PHASE2C_|PHASE2D2_|PHASE2D3B_|PANEL_PIXEL_ASSERTION=|WORKSPACE_|WINDOW_|COMPOSITOR_EXIT=|SESSION_EXIT=|CLEAN_SHUTDOWN=' \
  "$run_dir/test.log"
