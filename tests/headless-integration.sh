#!/usr/bin/env bash
set -Eeuo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
run_id="$(date -u +%Y%m%dT%H%M%SZ)"
run_root="${SEYRIX_HEADLESS_ROOT:-${HOME}/.cache/seyrix-linux/headless-${run_id}}"
build_root="$run_root/build"
stage_root="$run_root/stage"
runtime_dir="$run_root/runtime"
result_file="$run_root/results.tsv"
compositor_bin="$build_root/compositor/seyrix-compositor"
probe_bin="$build_root/protocol-probe/seyrix-protocol-probe"
shell_bin="$build_root/shell/seyrix-shell"
panel_link=/usr/bin/seyrix-shell
compositor_pid=
created_panel_link=0
failures=0

if (( EUID != 0 )); then
	echo "Run this controlled headless integration test as root in Arch WSL." >&2
	exit 2
fi
for command in meson ninja timeout wlr-randr; do
	if ! command -v "$command" >/dev/null 2>&1; then
		echo "Required command is missing: $command" >&2
		exit 2
	fi
done
if [[ -e "$run_root" ]]; then
	echo "Headless test directory already exists: $run_root" >&2
	exit 2
fi
if [[ -e "$panel_link" || -L "$panel_link" ]]; then
	echo "$panel_link already exists; refusing to replace a system file." >&2
	exit 2
fi
if [[ -n "${SEYRIX_HEADLESS_SANITIZERS:-}" &&
		"${SEYRIX_HEADLESS_SANITIZERS}" != address &&
		"${SEYRIX_HEADLESS_SANITIZERS}" != undefined &&
		"${SEYRIX_HEADLESS_SANITIZERS}" != address,undefined ]]; then
	echo "SEYRIX_HEADLESS_SANITIZERS must be empty, address, undefined, or address,undefined." >&2
	exit 2
fi
mkdir -p "$build_root" "$stage_root" "$runtime_dir"
chmod 700 "$runtime_dir"
printf 'check\tstatus\tdetail\n' > "$result_file"

log="$run_root/compositor.log"
test_log="$run_root/test.log"
: > "$test_log"

record() {
	local name="$1" status="$2" detail="$3"
	printf '%s\t%s\t%s\n' "$name" "$status" "$detail" | tee -a "$result_file" "$test_log"
	if [[ "$status" == FAIL || "$status" == INCOMPLETE ]]; then
		failures=$((failures + 1))
	fi
}

same_process() {
	local pid="$1" expected="$2"
	[[ -r "/proc/$pid/exe" ]] &&
		[[ "$(readlink -f "/proc/$pid/exe")" == "$(readlink -f "$expected")" ]]
}

cleanup() {
	local exit_code=$?
	trap - EXIT INT TERM
	set +e
	if [[ -n "$compositor_pid" ]] && same_process "$compositor_pid" "$compositor_bin"; then
		kill -TERM "$compositor_pid" 2>/dev/null
		for _ in $(seq 1 100); do
			if ! same_process "$compositor_pid" "$compositor_bin"; then break; fi
			sleep 0.1
		done
		if same_process "$compositor_pid" "$compositor_bin"; then
			printf 'Cleanup: compositor PID %s remained after SIGTERM; process state retained.\n' "$compositor_pid" >> "$test_log"
			if same_process "$compositor_pid" "$compositor_bin"; then
				kill -KILL "$compositor_pid" 2>/dev/null
			fi
		fi
		wait "$compositor_pid" 2>/dev/null
	fi
	if (( created_panel_link )) && [[ -L "$panel_link" ]] &&
		[[ "$(readlink -f "$panel_link")" == "$(readlink -f "$shell_bin")" ]]; then
		rm -- "$panel_link"
	fi
	if (( failures > 0 )); then exit_code=1; fi
	printf 'RUN_FINISHED_UTC=%s\nEXIT_STATUS=%s\n' "$(date -u --iso-8601=seconds)" "$exit_code" >> "$test_log"
	exit "$exit_code"
}
trap cleanup EXIT INT TERM

wait_for() {
	local name="$1" seconds="$2" command="$3" started=$SECONDS
	while (( SECONDS - started < seconds )); do
		if eval "$command"; then
			record "$name" PASS "observed after $((SECONDS - started))s"
			return 0
		fi
		sleep 0.1
	done
	record "$name" FAIL "timed out after ${seconds}s; see $log"
	return 1
}

run_probe() {
	local name="$1" validation_mode="$2" probe_mode="$3"
	local probe_log="$run_root/${name}.log" status=0
	if timeout --signal=TERM --kill-after=2s 60s env \
		SEYRIX_PROBE_LOG="$probe_log" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
		XDG_RUNTIME_DIR="$runtime_dir" "$probe_bin" "$probe_mode" \
		>> "$test_log" 2>&1 &&
		bash "$repo_root/tests/phase2c-probe-check.sh" "$validation_mode" "$probe_log" \
		>> "$test_log" 2>&1; then
		record "$name" PASS "client and protocol assertions passed; log=$probe_log"
	else
		status=$?
		record "$name" FAIL "client exit=$status; protocol log=$probe_log"
	fi
}

printf 'Run started UTC: %s\nSource branch: %s\nSource commit: %s\n' \
	"$(date -u --iso-8601=seconds)" \
	"$(git -C "$repo_root" branch --show-current)" \
	"$(git -C "$repo_root" rev-parse HEAD)" > "$run_root/run-metadata.txt"
printf 'Build root: %s\nBackend: headless with two outputs\nRenderer: pixman\nSanitizers: %s\n' \
	"$build_root" "${SEYRIX_HEADLESS_SANITIZERS:-none}" >> "$run_root/run-metadata.txt"

compositor_args=(--buildtype=debugoptimized --prefix=/usr --wrap-mode=nofallback)
if [[ -n "${SEYRIX_HEADLESS_SANITIZERS:-}" ]]; then
	compositor_args+=("-Db_sanitize=${SEYRIX_HEADLESS_SANITIZERS}")
fi
meson setup "$build_root/compositor" "$repo_root/desktop/compositor" "${compositor_args[@]}" \
	> "$run_root/compositor-build.log" 2>&1
ninja -C "$build_root/compositor" >> "$run_root/compositor-build.log" 2>&1
meson install -C "$build_root/compositor" --destdir "$stage_root" >> "$run_root/compositor-build.log" 2>&1
meson setup "$build_root/protocol-probe" "$repo_root/desktop/protocol-probe" \
	--buildtype=release --prefix=/usr --wrap-mode=nofallback \
	> "$run_root/probe-build.log" 2>&1
ninja -C "$build_root/protocol-probe" >> "$run_root/probe-build.log" 2>&1
meson setup "$build_root/shell" "$repo_root/desktop/shell" \
	--buildtype=release --prefix=/usr --wrap-mode=nofallback \
	> "$run_root/shell-build.log" 2>&1
ninja -C "$build_root/shell" >> "$run_root/shell-build.log" 2>&1
compositor_bin="$build_root/compositor/seyrix-compositor"
probe_bin="$build_root/protocol-probe/seyrix-protocol-probe"
shell_bin="$build_root/shell/seyrix-shell"
if [[ ! -x /usr/bin/weston-terminal ]]; then
	echo "weston-terminal is required to exercise initial client readiness." >&2
	exit 2
fi
ln -s "$shell_bin" "$panel_link"
created_panel_link=1

export XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY=wayland-0
export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
"$compositor_bin" > "$log" 2>&1 &
compositor_pid=$!
printf 'Compositor PID: %s\n' "$compositor_pid" >> "$run_root/run-metadata.txt"

wait_for compositor_ready 30 \
	"grep -q 'SEYRIX_COMPOSITOR_READY=1' '$log' && test -S '$runtime_dir/$WAYLAND_DISPLAY'" || true
wait_for initial_terminal_mapped 30 \
	"grep -q 'Window mapped:' '$log' && pgrep -x weston-terminal >/dev/null" || true
wait_for panel_mapped 30 \
	"grep -q 'Layer shell surface mapped namespace=seyrix-panel' '$log' && pgrep -x seyrix-shell >/dev/null" || true

if grep -q 'SEYRIX_COMPOSITOR_READY=1' "$log"; then
	run_probe state_cycle state --state-cycle
	run_probe rapid_state rapid --rapid-state
	run_probe client_decoration decoration --client-decoration
	run_probe destroy_transition destroy --destroy-during-transition
else
	for name in state_cycle rapid_state client_decoration destroy_transition; do
		record "$name" INCOMPLETE "compositor never reached readiness"
	done
fi

outputs="$(XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" wlr-randr 2>> "$test_log" || true)"
printf 'OUTPUTS_BEFORE_BEGIN\n%s\nOUTPUTS_BEFORE_END\n' "$outputs" >> "$test_log"
mapfile -t output_names < <(awk '/^[^[:space:]]/ { print $1 }' <<< "$outputs")
if (( ${#output_names[@]} >= 2 )); then
	output_a="${output_names[0]}"
	output_b="${output_names[1]}"
	if XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
		wlr-randr --output "$output_a" --scale 1.25 >> "$test_log" 2>&1 &&
		XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
		wlr-randr --output "$output_a" --scale 1.0 >> "$test_log" 2>&1; then
		record output_scale_test_apply PASS "$output_a scale 1.25 then 1.0"
	else
		record output_scale_test_apply FAIL "$output_a scale change rejected"
	fi
	if XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
		wlr-randr --output "$output_a" --off >> "$test_log" 2>&1; then
		if XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
			wlr-randr --output "$output_a" --on >> "$test_log" 2>&1; then
			record output_disable_restore PASS "$output_a disabled and restored with $output_b remaining"
		else
			record output_disable_restore FAIL "$output_a could not be re-enabled"
		fi
	else
		record output_disable_restore FAIL "$output_a could not be disabled while $output_b remained enabled"
	fi
	if grep -q "Layer surface .* moved to output $output_b" "$log"; then
		record panel_output_reassignment PASS "panel moved to the surviving enabled output"
	else
		record panel_output_reassignment FAIL "no panel reassignment diagnostic; see compositor log"
	fi
	rejected_before="$(grep -Fc 'Rejecting incomplete or invalid output configuration' "$log" || true)"
	if XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
		wlr-randr --output "$output_a" --off --output "$output_b" --off \
		>> "$test_log" 2>&1; then
		record output_reject_disable_all FAIL "output manager accepted a configuration with no enabled heads"
	else
		outputs_after_reject="$(XDG_RUNTIME_DIR="$runtime_dir" \
			WAYLAND_DISPLAY="$WAYLAND_DISPLAY" wlr-randr 2>> "$test_log" || true)"
		rejected_after="$(grep -Fc 'Rejecting incomplete or invalid output configuration' "$log" || true)"
		if (( rejected_after > rejected_before )) && awk -v a="$output_a" -v b="$output_b" '
			$1 == a || $1 == b { current = $1 }
			current != "" && $1 == "Enabled:" { enabled[current] = $2; current = "" }
			END { exit !(enabled[a] == "yes" && enabled[b] == "yes") }
		' <<< "$outputs_after_reject"; then
			record output_reject_disable_all PASS "zero-enabled-head request rejected and both outputs remained enabled"
		else
			record output_reject_disable_all FAIL "configuration failure changed current output state"
		fi
	fi
else
	record output_two_head_setup INCOMPLETE "headless backend exposed ${#output_names[@]} outputs"
	record output_scale_test_apply INCOMPLETE "two output setup unavailable"
	record output_disable_restore INCOMPLETE "two output setup unavailable"
	record panel_output_reassignment INCOMPLETE "two output setup unavailable"
	record output_reject_disable_all INCOMPLETE "two output setup unavailable"
fi

if [[ -n "$compositor_pid" ]] && same_process "$compositor_pid" "$compositor_bin"; then
	shutdown_pid="$compositor_pid"
	kill -TERM "$shutdown_pid"
	for _ in $(seq 1 100); do
		if ! same_process "$shutdown_pid" "$compositor_bin"; then break; fi
		sleep 0.1
	done
	if same_process "$shutdown_pid" "$compositor_bin"; then
		record compositor_shutdown FAIL "compositor PID $shutdown_pid did not exit after SIGTERM"
	else
		shutdown_status=0
		wait "$shutdown_pid" 2>/dev/null || shutdown_status=$?
		compositor_pid=
		if grep -q 'Seyrix compositor stopped cleanly' "$log" && (( shutdown_status == 0 )); then
			record compositor_shutdown PASS "compositor exited cleanly after client and output interactions"
		else
			record compositor_shutdown FAIL "exit=$shutdown_status; inspect compositor shutdown log"
		fi
	fi
else
	record compositor_shutdown INCOMPLETE "compositor was not running at shutdown stage"
fi

if [[ -n "${SEYRIX_HEADLESS_SANITIZERS:-}" ]]; then
	if grep -Eq 'AddressSanitizer:|LeakSanitizer:|runtime error:' "$log"; then
		record sanitizer_scan FAIL "sanitizer diagnostics found; see $log"
	else
		record sanitizer_scan PASS "instrumented compositor had no ASan/UBSan/LSan diagnostics"
	fi
else
	record sanitizer_scan NOT_RUN "compositor was built without sanitizer instrumentation"
fi
printf 'EVIDENCE_DIR=%s\n' "$run_root"
printf 'RESULTS=%s\n' "$result_file"
