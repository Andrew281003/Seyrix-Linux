#!/usr/bin/env bash
set -Eeuo pipefail

if (( $# != 2 )); then
  echo "Usage: $0 execution-snapshot launch-wrapper" >&2
  exit 2
fi

snapshot_root="$(realpath -e -- "$1")"
launch_wrapper="$(realpath -e -- "$2")"
fail() { echo "FAIL: $*" >&2; exit 1; }
tests_dir="$snapshot_root/tests"
driver="$tests_dir/qemu-graphics.sh"
expect_script="$tests_dir/qemu-graphics.exp"

[[ -x "$launch_wrapper" ]] || fail "launch wrapper is not executable: $launch_wrapper"
tag="$(sed -n 's/^tag=//p' "$launch_wrapper")"
artifact_root="$(sed -n 's/^artifact_root=//p' "$launch_wrapper")"
tag_token='$tag'
[[ "$artifact_root" == *"$tag_token" ]] || fail "launch wrapper artifact root is not tied to its run tag"
artifact_root_prefix="${artifact_root%"$tag_token"}"
[[ "$(sed -n 's/^snapshot_root=//p' "$launch_wrapper")" == '$artifact_root/execution-snapshot' ]] ||
  fail "launch wrapper does not derive its snapshot from the isolated artifact root"
[[ "$snapshot_root" == "${artifact_root_prefix}${tag}/execution-snapshot" ]] ||
  fail "launch wrapper does not name the supplied execution snapshot"
grep -Fq 'xvfb-run -a --server-args="-screen 0 2560x1440x24 -ac" bash "$snapshot_root/tests/qemu-graphics.sh" "$iso_path"' "$launch_wrapper" ||
  fail "launch wrapper does not invoke the snapshot shell driver"

for script in qemu-graphics.sh qemu-lock.sh qemu-graphics.exp qemu-guest-helper.sh qemu-retain-compositor-log.sh; do
  path="$tests_dir/$script"
  [[ "$(realpath -e -- "$path")" == "$path" ]] || fail "$script resolves outside the execution snapshot"
  [[ -x "$path" ]] || fail "required executable lost its execute permission: $path"
  printf 'PASS: executable %s\n' "$path"
done

for path in "$tests_dir/qemu-graphics-preflight.sh" "$snapshot_root/build/retention.sh"; do
  [[ "$(realpath -e -- "$path")" == "$path" ]] || fail "sourced file resolves outside the execution snapshot: $path"
done

grep -Fq 'source "$script_dir/qemu-graphics-preflight.sh"' "$driver" || fail "shell driver does not source its sibling preflight"
grep -Fq 'exec bash "$script_dir/qemu-lock.sh" -- "$script_dir/qemu-graphics.sh" "$@"' "$driver" || fail "shell driver does not resolve the snapshot lock script"
grep -Fq 'expect "$script_dir/qemu-graphics.exp"' "$driver" || fail "shell driver does not invoke its sibling Expect script"
grep -Fq '"$script_dir/qemu-retain-compositor-log.sh"' "$driver" || fail "shell driver does not resolve the snapshot log retainer"
grep -Fq '[file join [file dirname [file normalize [info script]]] qemu-guest-helper.sh]' "$expect_script" || fail "Expect script does not resolve its sibling guest helper"

mount_options="$(findmnt -n -o OPTIONS -T "$snapshot_root")"
[[ -n "$mount_options" ]] || fail "cannot determine execution snapshot mount options"
case ",$mount_options," in
  *,noexec,*) fail "snapshot filesystem is mounted noexec: $mount_options" ;;
esac
if find "$snapshot_root" -perm /222 -print -quit | grep -q .; then
  fail "sealed execution snapshot contains a writable path"
fi

printf 'PASS: launch wrapper and runtime support paths resolve to %s\n' "$snapshot_root"
printf 'PASS: snapshot filesystem permits execution (%s) and snapshot is read-only\n' "$mount_options"
