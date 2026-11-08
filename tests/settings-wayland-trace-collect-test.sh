#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
temporary_dir="$(mktemp -d)"
trap 'rm -rf -- "$temporary_dir"' EXIT
collector="$script_dir/settings-wayland-trace-collect.py"
trace="$temporary_dir/trace.log"
capture="$temporary_dir/test.log"
output="$temporary_dir/output"
mkdir "$output"
cat >"$trace" <<'EOF'
SEYRIX_SETTINGS_TRACE monotonic_ns=10 source=client-stderr pid=2158 [ 10] -> wl_display.get_registry(new id wl_registry#2)
SEYRIX_SETTINGS_TRACE monotonic_ns=20 source=client-stderr pid=2731 [ 20] -> wl_display.sync(new id wl_callback#8)
SEYRIX_SETTINGS_TRACE monotonic_ns=30 source=client-stderr pid=2731 [ 30] -> wl_surface#16.commit()
EOF
bytes="$(stat -c %s "$trace")"
digest="$(sha256sum "$trace" | awk '{print $1}')"
{
  printf 'guest output before trace export\nSETTINGS_WAYLAND_TRACE_EXPORT_BEGIN pid=2731 bytes=%s sha256=%s\n' "$bytes" "$digest"
  base64 -w 76 "$trace"
  printf 'SETTINGS_WAYLAND_TRACE_EXPORT_END pid=2731\nguest output after trace export\n'
} >"$capture"
report="$(python3 "$collector" "$capture" "$output")"
grep -Fq 'SETTINGS_WAYLAND_TRACE_COLLECTION=passed pid=2731' <<<"$report"
cmp -s "$trace" "$output/settings-wayland-trace-pid-2731.log"
[[ "$(stat -c %a "$output/settings-wayland-trace-pid-2731.log")" == 600 ]]

sed 's/SETTINGS_WAYLAND_TRACE_EXPORT_END pid=2731/SETTINGS_WAYLAND_TRACE_EXPORT_END pid=2158/' "$capture" >"$temporary_dir/wrong-end.log"
if python3 "$collector" "$temporary_dir/wrong-end.log" "$temporary_dir/wrong-end" >/dev/null 2>&1; then
  echo 'FAIL: mismatched trace end marker was accepted' >&2
  exit 1
fi

sed '/SETTINGS_WAYLAND_TRACE_EXPORT_END/d' "$capture" >"$temporary_dir/truncated.log"
if python3 "$collector" "$temporary_dir/truncated.log" "$temporary_dir/truncated" >/dev/null 2>&1; then
  echo 'FAIL: truncated trace export was accepted' >&2
  exit 1
fi

sed 's/pid=2731/pid=2158/g' "$trace" >"$temporary_dir/wrong-pid.trace"
wrong_bytes="$(stat -c %s "$temporary_dir/wrong-pid.trace")"
wrong_digest="$(sha256sum "$temporary_dir/wrong-pid.trace" | awk '{print $1}')"
{
  printf 'SETTINGS_WAYLAND_TRACE_EXPORT_BEGIN pid=2731 bytes=%s sha256=%s\n' "$wrong_bytes" "$wrong_digest"
  base64 -w 76 "$temporary_dir/wrong-pid.trace"
  printf 'SETTINGS_WAYLAND_TRACE_EXPORT_END pid=2731\n'
} >"$temporary_dir/wrong-pid.log"
if python3 "$collector" "$temporary_dir/wrong-pid.log" "$temporary_dir/wrong-pid" >/dev/null 2>&1; then
  echo 'FAIL: trace for another PID satisfied attribution' >&2
  exit 1
fi

printf 'SETTINGS_WAYLAND_TRACE_COLLECT_REGRESSION=PASS\n'
