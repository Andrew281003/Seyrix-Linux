#!/usr/bin/env bash

qemu_graphics_required_support_files() {
  local workflow=$1 settings_trace_only=$2
  printf '%s\n' qemu-graphics.exp qemu-lock.sh
  case "$workflow" in
    phase2c|phase2d2|phase2d3b)
      printf '%s\n' qemu-guest-helper.sh
      ;;
  esac
  if [[ "$workflow" == phase2d3b ]]; then
    printf '%s\n' settings-wayland-trace-collect.py
  fi
  if [[ "$settings_trace_only" == 1 ]]; then
    printf '%s\n' \
      qemu-graphics-preflight.sh \
      qemu-graphics-capture.tcl \
      settings-render-trace-functions.sh \
      settings-render-trace-guest.sh \
      settings-render-pixel-compare.py \
      settings-render-pixel-regression.py \
      settings-render-trace-launcher.py \
      settings-render-trace-regression.sh \
      settings-render-trace-serial-regression.exp \
      settings-render-trace-tcl-completeness.tcl \
      fixtures/settings-render-trace/malformed.trace \
      fixtures/settings-render-trace/pointer-namespace-collision.trace \
      fixtures/settings-render-trace/truncated.trace \
      fixtures/settings-render-trace/unrelated-process.trace \
      fixtures/settings-render-trace/valid-at.trace
  fi
}

qemu_graphics_required_executable_files() {
  local workflow=$1 settings_trace_only=$2
  printf '%s\n' qemu-graphics.sh qemu-graphics.exp qemu-lock.sh
  case "$workflow" in
    phase2c|phase2d2|phase2d3b)
      printf '%s\n' qemu-guest-helper.sh
      ;;
  esac
  if [[ "$settings_trace_only" == 1 ]]; then
    printf '%s\n' \
      qemu-graphics-preflight.sh \
      qemu-graphics-capture.tcl \
      settings-render-trace-functions.sh \
      settings-render-trace-guest.sh \
      settings-render-pixel-compare.py \
      settings-render-pixel-regression.py \
      settings-render-trace-launcher.py \
      settings-render-trace-regression.sh \
      settings-render-trace-serial-regression.exp \
      settings-render-trace-tcl-completeness.tcl
  fi
}

qemu_graphics_validate_support_files() {
  local script_dir=$1 workflow=$2 settings_trace_only=$3 helper
  while IFS= read -r helper; do
    [[ -n "$helper" ]] || continue
    if [[ ! -f "$script_dir/$helper" || ! -r "$script_dir/$helper" ]]; then
      printf 'Required QEMU harness support file is missing or unreadable: %s\n' \
        "$script_dir/$helper" >&2
      return 1
    fi
  done < <(qemu_graphics_required_support_files "$workflow" "$settings_trace_only")

  while IFS= read -r helper; do
    [[ -n "$helper" ]] || continue
    if [[ ! -x "$script_dir/$helper" ]]; then
      printf 'Required QEMU harness script is not executable: %s\n' \
        "$script_dir/$helper" >&2
      return 1
    fi
  done < <(qemu_graphics_required_executable_files "$workflow" "$settings_trace_only")
}
