#!/usr/bin/env bash
set -Eeuo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
fixture_root="$(mktemp -d /var/tmp/seyrix-retention-validation.XXXXXX)"
validation_failed=0
checks=0
cleanup_fixture=1

source "$repo_root/build/retention.sh"
SEYRIX_ARTIFACT_ROOT="$fixture_root"
seyrix_retention_init
seyrix_retention_acquire_lock

record_check() {
	local description="$1" passed="$2"
	checks=$((checks + 1))
	if [[ "$passed" == 1 ]]; then
		printf 'RETENTION_CHECK=PASS name=%s\n' "$description"
	else
		printf 'RETENTION_CHECK=FAIL name=%s\n' "$description"
		validation_failed=1
	fi
}

make_build() {
	local name="$1" path="$fixture_root/$1"
	mkdir -p "$path/out" "$path/staging"
	seyrix_retention_begin "$path" build
	printf 'fixture ISO: %s\n' "$name" > "$path/out/seyrix-${name}.iso"
	seyrix_retention_finish_build "$path" 0
	printf '%s\n' "$path/out/seyrix-${name}.iso"
}

make_pass_run() {
	local name="$1" iso_path="$2" workflow="${3:-phase2d2}" acceptance="${4:-0}" path="$fixture_root/$1"
	mkdir -p "$path"
	seyrix_retention_begin "$path" qemu-run
	printf 'Run result: PASS\n' > "$path/run-result.txt"
	printf 'fixture graphical acceptance evidence\n' > "$path/test.log"
	if [[ "$acceptance" == 1 ]]; then
		printf 'FULL_GRAPHICAL_ACCEPTANCE=PASS\n' >> "$path/test.log"
	fi
	printf 'Workflow: %s\n' "$workflow" > "$path/run-metadata.txt"
	printf 'STAGE_RESULT fixture result=PASS\n' > "$path/stage-results.log"
	seyrix_retention_finish_qemu_run "$path" "$iso_path" 0
}

if [[ "$SEYRIX_RETENTION_BUILD_RESERVE_BYTES" != 17179869184 ||
		"$SEYRIX_RETENTION_QEMU_RESERVE_BYTES" != 4294967296 ||
		"$SEYRIX_RETENTION_MAX_BYTES" != 161061273600 ||
		"$SEYRIX_RETENTION_MIN_FREE_BYTES" != 32212254720 ]]; then
	record_check configured-default-limits 0
else
	record_check configured-default-limits 1
fi

if flock -n "$fixture_root/.retention.lock" -c true 2>/dev/null; then
	record_check build-qemu-lock-serialization 0
else
	record_check build-qemu-lock-serialization 1
fi

saved_max_bytes="$SEYRIX_RETENTION_MAX_BYTES"
SEYRIX_RETENTION_MAX_BYTES=1
if seyrix_retention_preflight 1 "$fixture_root/budget-reject" >/dev/null 2>&1; then
	record_check artifact-budget-rejects-overflow 0
else
	record_check artifact-budget-rejects-overflow 1
fi
SEYRIX_RETENTION_MAX_BYTES="$saved_max_bytes"

saved_min_free_bytes="$SEYRIX_RETENTION_MIN_FREE_BYTES"
available_bytes="$(df -B1 --output=avail /var/tmp | awk 'NR == 2 {print $1}')"
SEYRIX_RETENTION_MIN_FREE_BYTES=$((available_bytes + 1))
if seyrix_retention_preflight 0 "$fixture_root/free-space-reject" >/dev/null 2>&1; then
	record_check free-space-preflight-rejects-unsafe-run 0
else
	record_check free-space-preflight-rejects-unsafe-run 1
fi
SEYRIX_RETENTION_MIN_FREE_BYTES="$saved_min_free_bytes"

if seyrix_retention_preflight "$SEYRIX_RETENTION_BUILD_RESERVE_BYTES" \
		"$fixture_root/build-preflight" >/dev/null &&
	seyrix_retention_preflight "$SEYRIX_RETENTION_QEMU_RESERVE_BYTES" \
		"$fixture_root/qemu-preflight" >/dev/null; then
	record_check build-and-qemu-reserves-enforced 1
else
	record_check build-and-qemu-reserves-enforced 0
fi

if ! seyrix_retention_safe_path "$repo_root/build/retention.sh" &&
		! seyrix_retention_is_managed_path "$repo_root/build/retention.sh" &&
		! seyrix_retention_safe_path /mnt/c/Windows/System32 &&
		! seyrix_retention_remove_managed /mnt/c/Windows/System32 build \
			>/dev/null 2>&1; then
	record_check worktree-and-windows-paths-rejected 1
else
	record_check worktree-and-windows-paths-rejected 0
fi

if seyrix_retention_remove_managed "$repo_root" build >/dev/null 2>&1 ||
		[[ ! -f "$repo_root/build/retention.sh" ]]; then
	record_check cleanup-cannot-enter-git-worktree 0
else
	record_check cleanup-cannot-enter-git-worktree 1
fi

active_build="$fixture_root/active-build"
mkdir -p "$active_build"
seyrix_retention_begin "$active_build" build
if seyrix_retention_remove_managed "$active_build" build >/dev/null 2>&1 ||
		[[ ! -f "$active_build/.seyrix-active" ]]; then
	record_check active-build-protected 0
else
	record_check active-build-protected 1
fi

failed_build="$fixture_root/failed-build"
mkdir -p "$failed_build"
seyrix_retention_begin "$failed_build" build
seyrix_retention_finish_build "$failed_build" 1
if [[ -f "$failed_build/.seyrix-build-failed" &&
		! -e "$failed_build/.seyrix-active" ]]; then
	record_check failed-build-evidence-retained 1
else
	record_check failed-build-evidence-retained 0
fi

interrupted_run="$fixture_root/interrupted-power-loss-run"
mkdir -p "$interrupted_run"
seyrix_retention_begin "$interrupted_run" qemu-run
printf 'serial and QEMU evidence from an interrupted fixture\n' > "$interrupted_run/serial.log"
printf '99999999\n' > "$interrupted_run/.seyrix-active"
if seyrix_retention_remove_managed "$interrupted_run" qemu-run >/dev/null 2>&1 ||
		[[ ! -f "$interrupted_run/serial.log" ||
			! -f "$interrupted_run/.seyrix-interrupted" ]]; then
	record_check interrupted-run-evidence-retained 0
else
	record_check interrupted-run-evidence-retained 1
fi

verified_dir="$fixture_root/verified"
mkdir -p "$verified_dir"
baseline_iso="$verified_dir/seyrix-phase2d2-2026.10.27-x86_64.iso"
printf 'fixture copy of the known-good Phase 2D.2 release\n' > "$baseline_iso"
sha256sum "$baseline_iso" | awk '{print $1}' > "$baseline_iso.sha256"
printf 'ISO: %s\nSHA-256: %s\nVerified UTC: fixture\n' \
	"$baseline_iso" "$(sha256sum "$baseline_iso" | awk '{print $1}')" \
	> "$verified_dir/latest.txt"
cp "$verified_dir/latest.txt" "$fixture_root/latest-before-failed-run.txt"

failed_iso="$(make_build failed-verification)"
failed_qemu="$fixture_root/failed-verification-run"
mkdir -p "$failed_qemu"
seyrix_retention_begin "$failed_qemu" qemu-run
printf 'Run result: FAIL\n' > "$failed_qemu/run-result.txt"
printf 'genuine fixture diagnostic\n' > "$failed_qemu/test.log"
seyrix_retention_finish_qemu_run "$failed_qemu" "$failed_iso" 1
if [[ -f "$failed_qemu/.seyrix-failed" &&
		-f "$failed_qemu/test.log" &&
		-f "$(dirname -- "$failed_iso")/../.seyrix-build-complete" &&
		"$(cat "$verified_dir/latest.txt")" == "$(cat "$fixture_root/latest-before-failed-run.txt")" ]]; then
	record_check failed-run-evidence-and-latest-pointer-retained 1
else
	record_check failed-run-evidence-and-latest-pointer-retained 0
fi

diagnostic_iso="$(make_build targeted-diagnostic)"
diagnostic_run="$fixture_root/targeted-diagnostic-run"
mkdir -p "$diagnostic_run"
seyrix_retention_begin "$diagnostic_run" qemu-run
printf 'Run result: PASS\n' > "$diagnostic_run/run-result.txt"
printf 'FULL_GRAPHICAL_ACCEPTANCE=NOT_RUN reason=targeted-diagnostic-only\n' > "$diagnostic_run/test.log"
printf 'Workflow: phase2d3b\n' > "$diagnostic_run/run-metadata.txt"
printf 'STAGE_RESULT fixture result=PASS\n' > "$diagnostic_run/stage-results.log"
latest_before_diagnostic="$(cat "$verified_dir/latest.txt")"
seyrix_retention_finish_qemu_run "$diagnostic_run" "$diagnostic_iso" 0
if [[ -f "$fixture_root/targeted-diagnostic/.seyrix-build-complete" &&
		"$(cat "$verified_dir/latest.txt")" == "$latest_before_diagnostic" &&
		! -e "$verified_dir/seyrix-targeted-diagnostic.iso" &&
		-f "$fixture_root/history/targeted-diagnostic-run.summary" ]]; then
	record_check phase2d3b-diagnostic-does-not-promote-or-clean-build 1
else
	record_check phase2d3b-diagnostic-does-not-promote-or-clean-build 0
fi

successful_iso="$(make_build successful-verification)"
make_pass_run successful-verification-run "$successful_iso" phase2d3b 1
if [[ ! -e "$fixture_root/successful-verification" &&
		! -e "$fixture_root/successful-verification-run" &&
		-f "$baseline_iso" && -f "$baseline_iso.sha256" &&
		"$(<"$baseline_iso.sha256")" == \
			"$(sha256sum "$baseline_iso" | awk '{print $1}')" &&
		-f "$fixture_root/history/successful-verification-run.summary" ]] &&
		grep -Fq "$verified_dir/seyrix-successful-verification.iso" \
			"$verified_dir/latest.txt"; then
	record_check successful-promotion-cleans-staging-and-preserves-baseline 1
else
	record_check successful-promotion-cleans-staging-and-preserves-baseline 0
fi

SEYRIX_RETENTION_KEEP_EVIDENCE=1
keep_evidence_iso="$(make_build keep-evidence)"
make_pass_run keep-evidence-run "$keep_evidence_iso"
if [[ -f "$fixture_root/keep-evidence-run/test.log" &&
		-e "$fixture_root/keep-evidence-run/.seyrix-managed" &&
		! -e "$fixture_root/keep-evidence" ]]; then
	record_check keep-evidence-retains-qemu-and-cleans-successful-build 1
else
	record_check keep-evidence-retains-qemu-and-cleans-successful-build 0
fi
SEYRIX_RETENTION_KEEP_EVIDENCE=0

SEYRIX_RETENTION_KEEP_ARTIFACTS=1
keep_artifacts_iso="$(make_build keep-artifacts)"
make_pass_run keep-artifacts-run "$keep_artifacts_iso"
if [[ -f "$fixture_root/keep-artifacts/.seyrix-build-complete" &&
		-f "$fixture_root/keep-artifacts-run/run-result.txt" ]]; then
	record_check keep-artifacts-retains-build-and-qemu 1
else
	record_check keep-artifacts-retains-build-and-qemu 0
fi

printf 'RETENTION_VALIDATION_CHECKS=%s\n' "$checks"
if (( validation_failed == 0 )); then
	printf 'RETENTION_VALIDATION=PASS\n'
else
	printf 'RETENTION_VALIDATION=FAIL fixture=%s\n' "$fixture_root"
	cleanup_fixture=0
	exit 1
fi

if (( cleanup_fixture )); then
	resolved_fixture="$(realpath -e -- "$fixture_root")"
	case "$resolved_fixture" in
		/var/tmp/seyrix-retention-validation.*)
			rm -rf -- "$resolved_fixture"
			;;
		*)
			seyrix_retention_error "refusing to clean unexpected fixture path: $resolved_fixture"
			exit 2
			;;
	esac
fi
