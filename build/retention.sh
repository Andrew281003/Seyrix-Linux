#!/usr/bin/env bash
# Shared storage guards for Seyrix build and QEMU scripts. Source this file.

SEYRIX_RETENTION_REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
: "${SEYRIX_ARTIFACT_ROOT:=/var/tmp/seyrix-linux}"
: "${SEYRIX_RETENTION_MAX_BYTES:=42949672960}"          # 40 GiB
: "${SEYRIX_RETENTION_MIN_FREE_BYTES:=32212254720}"      # 30 GiB
: "${SEYRIX_RETENTION_BUILD_RESERVE_BYTES:=17179869184}" # 16 GiB
: "${SEYRIX_RETENTION_QEMU_RESERVE_BYTES:=4294967296}"   # 4 GiB
: "${SEYRIX_RETENTION_KEEP_ARTIFACTS:=0}"
: "${SEYRIX_RETENTION_KEEP_EVIDENCE:=0}"

seyrix_retention_error() {
	printf 'Seyrix retention: %s\n' "$*" >&2
}

seyrix_retention_init() {
	local setting storage_source
	for setting in \
		"$SEYRIX_RETENTION_MAX_BYTES" \
		"$SEYRIX_RETENTION_MIN_FREE_BYTES" \
		"$SEYRIX_RETENTION_BUILD_RESERVE_BYTES" \
		"$SEYRIX_RETENTION_QEMU_RESERVE_BYTES"; do
		if [[ ! "$setting" =~ ^[0-9]+$ ]]; then
			seyrix_retention_error "storage limits must be non-negative integer byte counts."
			return 2
		fi
	done
	case "$SEYRIX_RETENTION_KEEP_ARTIFACTS" in
		0|false|'') ;;
		1|true) SEYRIX_RETENTION_KEEP_ARTIFACTS=1 ;;
		*) seyrix_retention_error "SEYRIX_RETENTION_KEEP_ARTIFACTS must be 0/false or 1/true."; return 2 ;;
	esac
	case "$SEYRIX_RETENTION_KEEP_EVIDENCE" in
		0|false|'') ;;
		1|true) SEYRIX_RETENTION_KEEP_EVIDENCE=1 ;;
		*) seyrix_retention_error "SEYRIX_RETENTION_KEEP_EVIDENCE must be 0/false or 1/true."; return 2 ;;
	esac
	storage_source="$(findmnt -n -o SOURCE --target /var/tmp)" || {
		seyrix_retention_error "could not identify the active Arch filesystem for /var/tmp."
		return 2
	}
	[[ -n "$storage_source" ]] || {
		seyrix_retention_error "could not identify the active Arch filesystem for /var/tmp."
		return 2
	}
	SEYRIX_RETENTION_STORAGE_SOURCE="$storage_source"
	SEYRIX_ARTIFACT_ROOT="$(realpath -m -- "$SEYRIX_ARTIFACT_ROOT")"
	if [[ "$SEYRIX_ARTIFACT_ROOT" == / ]] || \
		! seyrix_retention_safe_path "$SEYRIX_ARTIFACT_ROOT"; then
		seyrix_retention_error "artifact root is unsafe: $SEYRIX_ARTIFACT_ROOT"
		return 2
	fi
	mkdir -p -- "$SEYRIX_ARTIFACT_ROOT"
	SEYRIX_ARTIFACT_ROOT="$(realpath -e -- "$SEYRIX_ARTIFACT_ROOT")"
	seyrix_retention_safe_path "$SEYRIX_ARTIFACT_ROOT" || {
		seyrix_retention_error "artifact root resolved into an unsafe path: $SEYRIX_ARTIFACT_ROOT"
		return 2
	}
	export SEYRIX_ARTIFACT_ROOT SEYRIX_RETENTION_KEEP_ARTIFACTS SEYRIX_RETENTION_KEEP_EVIDENCE
}

seyrix_retention_safe_path() {
	local resolved="$1" existing git_root storage_source
	resolved="$(realpath -m -- "$resolved")" || return 1
	[[ "$resolved" != / && "$resolved" != "$SEYRIX_RETENTION_REPO_ROOT" &&
		"$resolved" != "$SEYRIX_RETENTION_REPO_ROOT/"* ]] || return 1
	existing="$resolved"
	while [[ ! -e "$existing" && "$existing" != / ]]; do
		existing="$(dirname -- "$existing")"
	done
	storage_source="$(findmnt -n -o SOURCE --target "$existing" 2>/dev/null)" || return 1
	[[ -n "$SEYRIX_RETENTION_STORAGE_SOURCE" &&
		"$storage_source" == "$SEYRIX_RETENTION_STORAGE_SOURCE" ]] || return 1
	if command -v git >/dev/null 2>&1 &&
		git_root="$(git -C "$existing" rev-parse --show-toplevel 2>/dev/null)"; then
		git_root="$(realpath -m -- "$git_root")"
		[[ "$resolved" != "$git_root" && "$resolved" != "$git_root/"* ]] || return 1
	fi
	return 0
}

seyrix_retention_is_managed_path() {
	local resolved
	seyrix_retention_safe_path "$1" || return 1
	resolved="$(realpath -m -- "$1")" || return 1
	[[ "$resolved" != "$SEYRIX_ARTIFACT_ROOT" &&
		"$resolved" == "$SEYRIX_ARTIFACT_ROOT/"* ]]
}

seyrix_retention_is_child() {
	local parent candidate
	parent="$(realpath -m -- "$1")" || return 1
	candidate="$(realpath -m -- "$2")" || return 1
	[[ "$candidate" != "$parent" && "$candidate" == "$parent/"* ]]
}

seyrix_retention_acquire_lock() {
	local lock_path="$SEYRIX_ARTIFACT_ROOT/.retention.lock"
	command -v flock >/dev/null 2>&1 || {
		seyrix_retention_error "flock is required (util-linux)."
		return 2
	}
	if [[ -L "$lock_path" ]] ||
		{ [[ -e "$lock_path" ]] && [[ "$(stat -c '%h' -- "$lock_path")" != 1 ]]; }; then
		seyrix_retention_error "refusing a linked retention lock file: $lock_path"
		return 2
	fi
	exec {SEYRIX_RETENTION_LOCK_FD}>"$lock_path"
	if ! flock -n "$SEYRIX_RETENTION_LOCK_FD"; then
		seyrix_retention_error "another Seyrix build or QEMU run is active."
		exec {SEYRIX_RETENTION_LOCK_FD}>&-
		return 75
	fi
}

seyrix_retention_ensure_internal_dir() {
	local name="$1" target="$SEYRIX_ARTIFACT_ROOT/$1"
	[[ "$name" =~ ^[A-Za-z0-9._-]+$ && "$name" != . && "$name" != .. ]] || return 2
	if [[ -L "$target" ]]; then
		seyrix_retention_error "refusing an artifact-root symlink: $target"
		return 2
	fi
	mkdir -p -- "$target"
	seyrix_retention_is_managed_path "$target" || {
		seyrix_retention_error "internal directory resolved outside the artifact root: $target"
		return 2
	}
}

seyrix_retention_preflight() {
	local planned_bytes="$1" target="$2" target_parent budget_root used available
	[[ "$planned_bytes" =~ ^[0-9]+$ ]] || {
		seyrix_retention_error "planned storage must be an integer byte count."
		return 2
	}
	target="$(realpath -m -- "$target")" || return 2
	seyrix_retention_safe_path "$target" || {
		seyrix_retention_error "preflight target is outside the active Arch filesystem or inside a Git worktree: $target"
		return 1
	}
	if [[ "$target" == "$SEYRIX_ARTIFACT_ROOT" ||
		"$target" == "$SEYRIX_ARTIFACT_ROOT/"* ]]; then
		budget_root="$SEYRIX_ARTIFACT_ROOT"
	elif [[ "$target" == "$SEYRIX_RETENTION_REPO_ROOT" ||
		"$target" == "$SEYRIX_RETENTION_REPO_ROOT/"* ]]; then
		budget_root="$SEYRIX_RETENTION_REPO_ROOT"
	else
		target_parent="$(dirname -- "$target")"
		while [[ ! -d "$target_parent" && "$target_parent" != / ]]; do
			target_parent="$(dirname -- "$target_parent")"
		done
		budget_root="$target_parent"
	fi
	used="$(du -sx -B1 -- "$budget_root" | awk '{print $1}')"
	available="$(df -B1 --output=avail "$budget_root" | awk 'NR == 2 {print $1}')"
	if (( available < SEYRIX_RETENTION_MIN_FREE_BYTES + planned_bytes )); then
		seyrix_retention_error "preflight needs $((SEYRIX_RETENTION_MIN_FREE_BYTES + planned_bytes)) free bytes; $available available on $budget_root."
		return 1
	fi
	if (( used + planned_bytes > SEYRIX_RETENTION_MAX_BYTES )); then
		seyrix_retention_error "preflight would exceed the $SEYRIX_RETENTION_MAX_BYTES-byte budget; $used bytes already stored under $budget_root."
		return 1
	fi
	printf 'Seyrix storage preflight: %s used, %s planned, %s free at %s\n' \
		"$used" "$planned_bytes" "$available" "$budget_root"
}

seyrix_retention_begin() {
	local target="$1" kind="$2" resolved
	seyrix_retention_is_managed_path "$target" || {
		seyrix_retention_error "refusing to mark an unmanaged or repository path: $target"
		return 2
	}
	resolved="$(realpath -m -- "$target")"
	seyrix_retention_is_child "$SEYRIX_ARTIFACT_ROOT" "$resolved" || {
		seyrix_retention_error "managed build and test outputs must be direct children of the artifact root: $resolved"
		return 2
	}
	[[ -d "$resolved" ]] || return 2
	[[ ! -e "$resolved/.seyrix-managed" && ! -L "$resolved/.seyrix-managed" &&
		! -e "$resolved/.seyrix-active" && ! -L "$resolved/.seyrix-active" ]] || {
		seyrix_retention_error "managed output already has a retention marker: $resolved"
		return 2
	}
	printf 'type=%s\npath=%s\n' "$kind" "$resolved" > "$resolved/.seyrix-managed"
	printf '%s\n' "$$" > "$resolved/.seyrix-active"
}

seyrix_retention_finish_build() {
	local target="$1" status="${2:-1}"
	if [[ -f "$target/.seyrix-managed" && ! -L "$target/.seyrix-managed" ]]; then
		if [[ -L "$target/.seyrix-active" || -L "$target/.seyrix-build-complete" ||
			-L "$target/.seyrix-build-failed" ]]; then
			seyrix_retention_error "refusing to update linked build markers: $target"
			return 0
		fi
		rm -f -- "$target/.seyrix-active"
		if (( status == 0 )); then
			: > "$target/.seyrix-build-complete"
		else
			: > "$target/.seyrix-build-failed"
		fi
	fi
	return 0
}

seyrix_retention_path_in_use() {
	local target
	target="$(realpath -m -- "$1")" || return 1
	find /proc/[0-9]*/cwd /proc/[0-9]*/fd -maxdepth 2 -type l \
		\( -lname "$target" -o -lname "$target/*" \) -print -quit 2>/dev/null | grep -q .
}

seyrix_retention_remove_managed() {
	local target="$1" kind="$2" resolved active_pid
	seyrix_retention_is_managed_path "$target" || {
		seyrix_retention_error "refusing cleanup outside the managed artifact root: $target"
		return 1
	}
	resolved="$(realpath -m -- "$target")"
	[[ -f "$resolved/.seyrix-managed" && ! -L "$resolved/.seyrix-managed" ]] &&
		grep -Fxq "type=$kind" "$resolved/.seyrix-managed" &&
		grep -Fxq "path=$resolved" "$resolved/.seyrix-managed" || {
		seyrix_retention_error "refusing cleanup without a matching $kind marker: $resolved"
		return 1
	}
	if [[ -L "$resolved/.seyrix-active" ||
		( -e "$resolved/.seyrix-active" && ! -f "$resolved/.seyrix-active" ) ]]; then
		seyrix_retention_error "refusing an invalid active marker: $resolved"
		return 1
	fi
	if [[ -f "$resolved/.seyrix-active" ]]; then
		read -r active_pid < "$resolved/.seyrix-active" || active_pid=''
		if [[ "$active_pid" =~ ^[0-9]+$ ]] && kill -0 "$active_pid" 2>/dev/null; then
			seyrix_retention_error "keeping active output: $resolved (PID $active_pid)"
			return 1
		fi
		if [[ -L "$resolved/.seyrix-interrupted" ||
			( -e "$resolved/.seyrix-interrupted" && ! -f "$resolved/.seyrix-interrupted" ) ]]; then
			seyrix_retention_error "refusing an invalid interrupted marker: $resolved"
			return 1
		fi
		: > "$resolved/.seyrix-interrupted"
		seyrix_retention_error "keeping output with an unfinished active marker: $resolved"
		return 1
	fi
	if seyrix_retention_path_in_use "$resolved"; then
		seyrix_retention_error "keeping output with an open process reference: $resolved"
		return 1
	fi
	rm -rf -- "$resolved"
}

seyrix_retention_find_build_root() {
	local candidate
	candidate="$(dirname -- "$(realpath -m -- "$1")")"
	while [[ "$candidate" != / && "$candidate" != "$SEYRIX_ARTIFACT_ROOT" ]]; do
		if [[ -f "$candidate/.seyrix-managed" && ! -L "$candidate/.seyrix-managed" ]] &&
			seyrix_retention_is_managed_path "$candidate" &&
			grep -Fxq 'type=build' "$candidate/.seyrix-managed" &&
			grep -Fxq "path=$candidate" "$candidate/.seyrix-managed"; then
			printf '%s\n' "$candidate"
			return 0
		fi
		candidate="$(dirname -- "$candidate")"
	done
	return 1
}

seyrix_retention_write_summary() {
	local run_dir="$1" iso_path="$2" summary_dir="$SEYRIX_ARTIFACT_ROOT/history"
	local summary_tmp summary_path="$summary_dir/$(basename -- "$run_dir").summary"
	[[ -d "$run_dir" && ! -L "$run_dir" && -f "$run_dir/run-result.txt" &&
		! -L "$run_dir/run-result.txt" && -f "$iso_path" && ! -L "$iso_path" ]] || {
		seyrix_retention_error "refusing to summarize missing, linked, or non-regular run evidence."
		return 1
	}
	seyrix_retention_ensure_internal_dir history || return 1
	[[ ! -L "$summary_path" && ( ! -e "$summary_path" || -f "$summary_path" ) ]] || {
		seyrix_retention_error "refusing to replace a non-regular summary path: $summary_path"
		return 1
	}
	summary_tmp="$(mktemp "$summary_dir/.summary-XXXXXX")"
	{
		printf 'Run directory: %s\n' "$run_dir"
		cat -- "$run_dir/run-result.txt"
		printf 'ISO: %s\n' "$iso_path"
		sha256sum -- "$iso_path"
		if [[ -f "$run_dir/run-metadata.txt" ]]; then
			grep -E '^(Workflow|QEMU run ID|Run started UTC):' "$run_dir/run-metadata.txt" || true
		fi
		if [[ -f "$run_dir/stage-results.log" ]]; then
			cat -- "$run_dir/stage-results.log"
		fi
	} > "$summary_tmp"
	mv -f -- "$summary_tmp" "$summary_path"
}

seyrix_retention_promote_iso() {
	local run_dir="$1" iso_path="$2" build_root="$3"
	local verified_dir="$SEYRIX_ARTIFACT_ROOT/verified" iso_hash destination base_name latest_tmp old_iso sidecar incoming
	seyrix_retention_ensure_internal_dir verified || return 1
	iso_hash="$(sha256sum -- "$iso_path" | awk '{print $1}')"
	base_name="$(basename -- "$iso_path")"
	destination="$verified_dir/$base_name"
	[[ ! -L "$destination" && ( ! -e "$destination" || -f "$destination" ) ]] || {
		seyrix_retention_error "refusing to replace a non-regular ISO path: $destination"
		return 1
	}
	if [[ -e "$destination" ]] &&
		[[ "$(sha256sum -- "$destination" | awk '{print $1}')" != "$iso_hash" ]]; then
		destination="$verified_dir/${base_name%.iso}-$(basename -- "$run_dir")-${iso_hash:0:8}.iso"
	fi
	[[ ! -L "$destination" && ( ! -e "$destination" || -f "$destination" ) ]] || {
		seyrix_retention_error "refusing to replace a non-regular ISO path: $destination"
		return 1
	}
	if [[ ! -f "$destination" ]]; then
		incoming="$(mktemp "$verified_dir/.incoming-${iso_hash:0:12}-XXXXXX")"
		cp --reflink=auto -- "$iso_path" "$incoming"
		[[ "$(sha256sum -- "$incoming" | awk '{print $1}')" == "$iso_hash" ]] || {
			rm -f -- "$incoming"
			seyrix_retention_error "copied ISO checksum did not match; preserving build output."
			return 1
		}
		mv -- "$incoming" "$destination"
	fi
	sidecar="$destination.sha256"
	[[ ! -L "$sidecar" && ( ! -e "$sidecar" || -f "$sidecar" ) ]] || {
		seyrix_retention_error "refusing to replace a non-regular checksum path: $sidecar"
		return 1
	}
	printf '%s\n' "$iso_hash" > "$sidecar"
	[[ ! -L "$verified_dir/latest.txt" &&
		( ! -e "$verified_dir/latest.txt" || -f "$verified_dir/latest.txt" ) ]] || {
		seyrix_retention_error "refusing to replace a non-regular latest pointer."
		return 1
	}
	latest_tmp="$(mktemp "$verified_dir/.latest-XXXXXX")"
	printf 'ISO: %s\nSHA-256: %s\nVerified UTC: %s\n' \
		"$destination" "$iso_hash" "$(date -u --iso-8601=seconds)" > "$latest_tmp"
	mv -f -- "$latest_tmp" "$verified_dir/latest.txt"
	seyrix_retention_write_summary "$run_dir" "$destination"

	if [[ "$SEYRIX_RETENTION_KEEP_ARTIFACTS" != 1 ]]; then
		shopt -s nullglob
		for old_iso in "$verified_dir"/*.iso; do
			[[ "$old_iso" == "$destination" ]] && continue
			if [[ "$(basename -- "$old_iso")" == \
				"seyrix-phase2d2-2026.10.27-x86_64.iso" ]]; then
				continue
			fi
			if [[ -L "$old_iso" || ! -f "$old_iso" || -L "$old_iso.sha256" ||
				! -f "$old_iso.sha256" ]] ||
				[[ "$(<"$old_iso.sha256")" != "$(sha256sum -- "$old_iso" | awk '{print $1}')" ]]; then
				seyrix_retention_error "keeping ISO without a matching regular checksum sidecar: $old_iso"
				continue
			fi
			if seyrix_retention_path_in_use "$old_iso"; then
				seyrix_retention_error "keeping superseded ISO still in use: $old_iso"
				continue
			fi
			rm -f -- "$old_iso" "$old_iso.sha256"
		done
		shopt -u nullglob
		if [[ -n "$build_root" && -f "$build_root/.seyrix-build-complete" &&
			! -L "$build_root/.seyrix-build-complete" ]]; then
			seyrix_retention_remove_managed "$build_root" build || true
		fi
	fi
	printf 'Verified ISO retained: %s\n' "$destination"
}

seyrix_retention_finish_qemu_run() {
	local run_dir="$1" iso_path="$2" status="$3" result=''
	local managed_run=0 build_root=''
	if [[ -f "$run_dir/.seyrix-managed" &&
		! -L "$run_dir/.seyrix-managed" ]] &&
		seyrix_retention_is_managed_path "$run_dir" &&
		grep -Fxq 'type=qemu-run' "$run_dir/.seyrix-managed"; then
		managed_run=1
		rm -f -- "$run_dir/.seyrix-active"
	fi
	if [[ -f "$run_dir/run-result.txt" ]]; then
		result="$(sed -n 's/^Run result: //p' "$run_dir/run-result.txt" | head -n 1)"
	fi
	if (( status != 0 )) || [[ "$result" != PASS ]] || \
		grep -q 'still-running-after-test' "$run_dir/qemu-process-state.txt" 2>/dev/null; then
		if (( managed_run )); then
			if [[ -L "$run_dir/.seyrix-failed" ]]; then
				seyrix_retention_error "refusing a linked failure marker: $run_dir"
			else
				: > "$run_dir/.seyrix-failed"
			fi
		fi
		return 0
	fi
	if [[ -n "$iso_path" && -f "$iso_path" ]] &&
		build_root="$(seyrix_retention_find_build_root "$iso_path" || true)" &&
		[[ -n "$build_root" && -f "$build_root/.seyrix-build-complete" &&
			! -L "$build_root/.seyrix-build-complete" ]]; then
		seyrix_retention_promote_iso "$run_dir" "$iso_path" "$build_root" || return 1
	elif (( managed_run )); then
		seyrix_retention_write_summary "$run_dir" "$iso_path"
	fi
	if (( managed_run )) && [[ "$SEYRIX_RETENTION_KEEP_ARTIFACTS" != 1 &&
			"$SEYRIX_RETENTION_KEEP_EVIDENCE" != 1 ]]; then
		seyrix_retention_remove_managed "$run_dir" qemu-run || true
	fi
}

seyrix_retention_finish_headless_run() {
	local run_root="$1" status="$2" history_dir="$SEYRIX_ARTIFACT_ROOT/history"
	if [[ ! -f "$run_root/.seyrix-managed" ||
		-L "$run_root/.seyrix-managed" ]] ||
		! seyrix_retention_is_managed_path "$run_root" ||
		! grep -Fxq 'type=headless-run' "$run_root/.seyrix-managed"; then
		return 0
	fi
	if [[ -L "$run_root/.seyrix-active" ]]; then
		seyrix_retention_error "refusing a linked active marker: $run_root"
		return 1
	fi
	rm -f -- "$run_root/.seyrix-active"
	if (( status != 0 )) || grep -Eq $'\t(FAIL|INCOMPLETE)\t' "$run_root/results.tsv"; then
		if [[ -L "$run_root/.seyrix-failed" ]]; then
			seyrix_retention_error "refusing a linked failure marker: $run_root"
		else
			: > "$run_root/.seyrix-failed"
		fi
		return 0
	fi
	seyrix_retention_ensure_internal_dir history || return 1
	local summary_path="$history_dir/$(basename -- "$run_root").summary"
	local summary_tmp
	[[ ! -L "$summary_path" && ( ! -e "$summary_path" || -f "$summary_path" ) ]] || {
		seyrix_retention_error "refusing to replace a non-regular summary path: $summary_path"
		return 1
	}
	summary_tmp="$(mktemp "$history_dir/.summary-XXXXXX")"
	{
		printf 'Run directory: %s\nFinished UTC: %s\n' "$run_root" "$(date -u --iso-8601=seconds)"
		cat -- "$run_root/results.tsv"
	} > "$summary_tmp"
	mv -f -- "$summary_tmp" "$summary_path"
	if [[ "$SEYRIX_RETENTION_KEEP_ARTIFACTS" != 1 ]]; then
		seyrix_retention_remove_managed "$run_root" headless-run || true
	fi
}
