#!/usr/bin/env bash
set -Eeuo pipefail

fail() { echo "FAIL: $*" >&2; exit 1; }

validate_evidence_root() {
  local evidence_root=$1 designated_run_dir=$2 expected_uid=$3 expected_gid=$4
  local current_identity evidence_path designated_path preflight_path log_path

  current_identity="$(id -u):$(id -g)"
  [[ "$current_identity" == "$expected_uid:$expected_gid" ]] ||
    fail "launcher identity is $current_identity; expected $expected_uid:$expected_gid"

  evidence_path="$(realpath -e -- "$evidence_root" 2>/dev/null)" ||
    fail "evidence root is missing or unresolved: $evidence_root"
  designated_path="$(realpath -e -- "$designated_run_dir" 2>/dev/null)" ||
    fail "designated run directory is missing or unresolved: $designated_run_dir"
  [[ ! -L "$evidence_root" && "$evidence_path" == "$designated_path" && "$evidence_path" == "$evidence_root" ]] ||
    fail "evidence root resolves outside its designated run directory"

  preflight_path="$evidence_root/preflight"
  [[ -d "$preflight_path" && ! -L "$preflight_path" ]] ||
    fail "preflight directory is missing or is a symlink"
  [[ "$(realpath -e -- "$preflight_path")" == "$evidence_root/preflight" ]] ||
    fail "preflight path resolves outside the designated run directory"

  for path in "$evidence_root" "$preflight_path"; do
    [[ "$(stat -c '%u:%g' -- "$path")" == "$expected_uid:$expected_gid" ]] ||
      fail "directory ownership does not match launcher $expected_uid:$expected_gid: $path"
    [[ -w "$path" && -x "$path" ]] || fail "launcher cannot write and search directory: $path"
  done

  log_path="$preflight_path/launch-console.log"
  [[ ! -e "$log_path" && ! -L "$log_path" ]] ||
    fail "refusing to overwrite an existing launch log: $log_path"
  [[ "$(realpath -m -- "$log_path")" == "$evidence_root/preflight/launch-console.log" ]] ||
    fail "launch log resolves outside the designated run directory"
}

initialize_launch_log() {
  local evidence_root=$1 designated_run_dir=$2 expected_uid=$3 expected_gid=$4
  validate_evidence_root "$evidence_root" "$designated_run_dir" "$expected_uid" "$expected_gid"
  ( umask 077; set -o noclobber; : > "$evidence_root/preflight/launch-console.log" ) ||
    fail "could not create a fresh launch log"
}

if [[ "${1:-}" == --initialize ]]; then
  (( $# == 5 )) || fail "usage: $0 --initialize evidence-root designated-run-dir uid gid"
  shift
  initialize_launch_log "$@"
  exit 0
fi

(( EUID == 0 )) || fail "run under podman unshare so root-owned fixtures can be tested"
command -v setpriv >/dev/null || fail "setpriv is required for identity regression fixtures"

expected_uid=1000
expected_gid=1000
fixture_root="$(mktemp -d)"
trap 'rm -rf -- "$fixture_root"' EXIT
chown "$expected_uid:$expected_gid" "$fixture_root"
chmod 0700 "$fixture_root"
script_path="$fixture_root/evidence-permissions-regression.sh"
cp -- "$(realpath -e -- "$0")" "$script_path"
chown "$expected_uid:$expected_gid" "$script_path"
chmod 0600 "$script_path"

make_builder_fixture() {
  local path=$1
  mkdir -p -- "$path/preflight"
  chown -R "$expected_uid:$expected_gid" "$path"
  chmod 0700 "$path" "$path/preflight"
}

expect_rejected() {
  local label=$1
  shift
  if "$@" >/dev/null 2>&1; then fail "accepted invalid evidence setup: $label"; fi
  printf 'PASS: rejected %s\n' "$label"
}

good="$fixture_root/good"
make_builder_fixture "$good"
setpriv --reuid="$expected_uid" --regid="$expected_gid" --clear-groups \
  bash "$script_path" --initialize "$good" "$good" "$expected_uid" "$expected_gid"
[[ "$(stat -c '%u:%g %a' "$good/preflight/launch-console.log")" == "$expected_uid:$expected_gid 600" ]] ||
  fail "launch log was not created with builder ownership and mode 0600"
setpriv --reuid="$expected_uid" --regid="$expected_gid" --clear-groups \
  bash -c 'printf "builder-log-probe\\n" >> "$1" && [[ "$(<"$1")" == builder-log-probe ]]' _ \
  "$good/preflight/launch-console.log"
printf 'PASS: launch log initialized, written, and read as UID:GID %s:%s\n' "$expected_uid" "$expected_gid"

root_owned="$fixture_root/root-owned"
mkdir -p -- "$root_owned/preflight"
chmod 0555 "$root_owned" "$root_owned/preflight"
expect_rejected 'root-owned non-writable evidence root' \
  setpriv --reuid="$expected_uid" --regid="$expected_gid" --clear-groups \
    bash "$script_path" --initialize "$root_owned" "$root_owned" "$expected_uid" "$expected_gid"

no_preflight_write="$fixture_root/no-preflight-write"
make_builder_fixture "$no_preflight_write"
chmod 0500 "$no_preflight_write/preflight"
expect_rejected 'missing write access on preflight/' \
  setpriv --reuid="$expected_uid" --regid="$expected_gid" --clear-groups \
    bash "$script_path" --initialize "$no_preflight_write" "$no_preflight_write" "$expected_uid" "$expected_gid"

expect_rejected 'incorrect launcher UID/GID' \
  setpriv --reuid=1001 --regid=1001 --clear-groups \
    bash "$script_path" --initialize "$good" "$good" "$expected_uid" "$expected_gid"

outside="$fixture_root/outside"
make_builder_fixture "$outside"
expect_rejected 'evidence root outside designated run directory' \
  setpriv --reuid="$expected_uid" --regid="$expected_gid" --clear-groups \
    bash "$script_path" --initialize "$outside" "$good" "$expected_uid" "$expected_gid"

symlinked_preflight="$fixture_root/symlinked-preflight"
mkdir -p -- "$symlinked_preflight"
chown "$expected_uid:$expected_gid" "$symlinked_preflight"
chmod 0700 "$symlinked_preflight"
ln -s "$outside/preflight" "$symlinked_preflight/preflight"
expect_rejected 'preflight path resolving outside designated run directory' \
  setpriv --reuid="$expected_uid" --regid="$expected_gid" --clear-groups \
    bash "$script_path" --initialize "$symlinked_preflight" "$symlinked_preflight" "$expected_uid" "$expected_gid"

printf 'PASS: evidence permission regression suite\n'
