#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "$script_dir/.." && pwd -P)"
source "$repo_root/build/retention.sh"

fixture_root="$(mktemp -d)"
trap 'rm -rf -- "$fixture_root"' EXIT

failures=0
check() {
	local name="$1" status="$2" stages="$3" marker="$4" expected_result="$5" expect_status="$6" qemu_status="$7"
	local run_dir="$fixture_root/$name"
	mkdir -p "$run_dir"
	printf '%s\n' "$stages" > "$run_dir/test.log"
	printf 'Expect exit status: %s\n' "$expect_status" > "$run_dir/expect-exit-status.txt"
	printf 'QEMU exit status: %s\n' "$qemu_status" > "$run_dir/qemu-exit-status.txt"
if [[ "$marker" == yes ]]; then : > "$run_dir/.seyrix-failed"; fi
	local result=PASS
	if (( status != 0 )); then result=FAIL; fi
	if [[ -e "$run_dir/.seyrix-failed" ]] || \
		[[ ! -f "$run_dir/expect-exit-status.txt" ]] || \
		! grep -Fxq 'Expect exit status: 0' "$run_dir/expect-exit-status.txt" || \
		[[ ! -f "$run_dir/qemu-exit-status.txt" ]] || \
		! grep -Fxq 'QEMU exit status: 0' "$run_dir/qemu-exit-status.txt" || \
		grep -q 'still-running-after-test' "$run_dir/qemu-process-state.txt" 2>/dev/null; then result=INCOMPLETE; fi
	if [[ -e "$run_dir/.seyrix-failed" ]] || \
		[[ ! -f "$run_dir/test.log" ]] || \
		grep -Eq 'STAGE_RESULT .*result=(FAIL|INCOMPLETE)' "$run_dir/test.log" || \
		! awk '/^STAGE_BEGIN / { begun++ } /^STAGE_RESULT .*result=PASS/ { passed++ } END { exit !(begun > 0 && begun == passed) }' "$run_dir/test.log"; then result=INCOMPLETE; fi
	if [[ "$result" != "$expected_result" ]]; then
		printf 'STATUS_FIXTURE=FAIL case=%s expected=%s actual=%s\n' "$name" "$expected_result" "$result"
		failures=$((failures + 1))
	else
		printf 'STATUS_FIXTURE=PASS case=%s result=%s\n' "$name" "$result"
	fi
}

check all-stages-pass 0 $'STAGE_BEGIN a\nSTAGE_RESULT a result=PASS\nSTAGE_BEGIN b\nSTAGE_RESULT b result=PASS' no PASS 0 0
check stage-missing-result 0 $'STAGE_BEGIN a\nSTAGE_RESULT a result=PASS\nSTAGE_BEGIN b' no INCOMPLETE 0 0
check explicit-timeout 0 $'STAGE_BEGIN a\nSTAGE_RESULT a result=INCOMPLETE reason=timeout' no INCOMPLETE 0 0
check failing-stage 0 $'STAGE_BEGIN a\nSTAGE_RESULT a result=FAIL' no INCOMPLETE 0 0
check external-timeout 124 $'STAGE_BEGIN a\nSTAGE_RESULT a result=PASS\nSTAGE_BEGIN b' no INCOMPLETE 0 0
check nonzero-qemu 0 $'STAGE_BEGIN a\nSTAGE_RESULT a result=PASS' no INCOMPLETE 0 9
check failure-marker 0 $'STAGE_BEGIN a\nSTAGE_RESULT a result=PASS' yes INCOMPLETE 0 0

if (( failures > 0 )); then
	printf 'STATUS_FIXTURE_SUITE=FAIL failures=%d\n' "$failures"
	exit 1
fi
printf 'STATUS_FIXTURE_SUITE=PASS cases=7\n'
