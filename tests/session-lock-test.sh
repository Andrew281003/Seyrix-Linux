#!/usr/bin/env bash
set -Eeuo pipefail

runtime_dir="$(mktemp -d /tmp/seyrix-session-lock-test.XXXXXX)"
lock_file="$runtime_dir/session.lock"
started_file="$runtime_dir/started"
release_file="$runtime_dir/release"
child_pid=""
cleanup() {
	if [[ -n "$child_pid" ]]; then
		touch "$release_file" 2>/dev/null || true
		wait "$child_pid" 2>/dev/null || true
	fi
	rm -rf -- "$runtime_dir"
}
trap cleanup EXIT

flock --nonblock --conflict-exit-code 75 --close "$lock_file" \
	bash -c '
	for descriptor in /proc/$$/fd/*; do
		if [[ "$(readlink "$descriptor" 2>/dev/null || true)" == "$3" ]]; then
			printf "leaked\n" > "$1"
			exit 1
		fi
	done
	printf "ready\n" > "$1"
	while [[ ! -e "$2" ]]; do sleep 0.01; done
' _ "$started_file" "$release_file" "$lock_file" &
child_pid=$!

locked=false
for attempt in {1..100}; do
	if [[ -f "$started_file" ]]; then
		if [[ "$(<"$started_file")" != ready ]]; then
			echo "flock passed its lock descriptor to the command child" >&2
			exit 1
		fi
	else
		if ! kill -0 "$child_pid" 2>/dev/null; then
			echo "flock command child exited before reporting readiness" >&2
			exit 1
		fi
		sleep 0.01
		continue
	fi
	set +e
	flock --nonblock --conflict-exit-code 75 --close "$lock_file" true
	status=$?
	set -e
	if (( status == 75 )); then
		locked=true
		break
	fi
	sleep 0.01
done
if [[ "$locked" != true ]]; then
	echo "a second session acquired an already-held lock" >&2
	exit 1
fi

touch "$release_file"
wait "$child_pid"
child_pid=""
flock --nonblock --conflict-exit-code 75 --close "$lock_file" true
echo "PASS session lock excludes duplicate sessions and closes the lock fd in the child"
