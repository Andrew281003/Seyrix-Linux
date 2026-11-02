#!/usr/bin/env bash
set -Eeuo pipefail

evidence_dir=/mnt/seyrix-controls/evidence
mkdir -p "$evidence_dir" /tmp/seyrix-control-bin
cp /mnt/seyrix-controls/egl-gbm-lifecycle-control /tmp/seyrix-control-bin/
cp /mnt/seyrix-controls/wlroots-renderer-lifecycle-control /tmp/seyrix-control-bin/
chmod 0755 /tmp/seyrix-control-bin/*

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export LIBSEAT_BACKEND=logind
export LIBGL_ALWAYS_SOFTWARE=1
export WLR_RENDERER_ALLOW_SOFTWARE=1
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=Seyrix

{
	printf 'Guest kernel: '; uname -r
	printf 'Guest user: '; id
	printf 'XDG_RUNTIME_DIR=%s\n' "$XDG_RUNTIME_DIR"
	printf 'LIBSEAT_BACKEND=%s\n' "$LIBSEAT_BACKEND"
	printf 'LIBGL_ALWAYS_SOFTWARE=%s\n' "$LIBGL_ALWAYS_SOFTWARE"
	printf 'WLR_RENDERER_ALLOW_SOFTWARE=%s\n' "$WLR_RENDERER_ALLOW_SOFTWARE"
	printf 'XDG_SESSION_TYPE=%s\n' "$XDG_SESSION_TYPE"
	printf 'DRM_NODES_BEGIN\n'
	ls -l /dev/dri
	printf 'DRM_NODES_END\n'
	printf 'DRM_DEVICE_DRIVER='; readlink -f /sys/class/drm/card1/device/driver
	printf 'Mesa package: '; pacman -Q mesa
	printf 'wlroots package: '; pacman -Q wlroots0.20
	printf 'ASan runtime: '; pacman -Q libasan
	printf 'GCC runtime package: '; pacman -Q gcc-libs
} > "$evidence_dir/environment.txt" 2>&1

printf 'SEYRIX_LIFECYCLE_COMPOSITOR_BEGIN\n'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
	/usr/bin/seyrix-compositor > /tmp/seyrix-lifecycle-compositor.log 2>&1 &
compositor_pid=$!
printf '%s\n' "$compositor_pid" > /tmp/seyrix-lifecycle-compositor.pid
printf 'SEYRIX_LIFECYCLE_COMPOSITOR_PID=%s\n' "$compositor_pid"
set +e
wait "$compositor_pid"
compositor_status=$?
set -e
printf '%s\n' "$compositor_status" > /tmp/seyrix-lifecycle-compositor.exit
printf 'SEYRIX_LIFECYCLE_COMPOSITOR_EXIT=%s\n' "$compositor_status"
cp /tmp/seyrix-lifecycle-compositor.log "$evidence_dir/seyrix-compositor.log"
printf '%s\n' "$compositor_status" > "$evidence_dir/seyrix-compositor.exit"

run_control() {
	local name="$1"
	shift
	local log_path="/tmp/seyrix-${name}.log"
	local exit_path="/tmp/seyrix-${name}.exit"
	local status
	set +e
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:symbolize=1 \
		UBSAN_OPTIONS=halt_on_error=1 "$@" > "$log_path" 2>&1
	status=$?
	set -e
	printf '%s\n' "$status" > "$exit_path"
	cp "$log_path" "$evidence_dir/${name}.log"
	printf '%s\n' "$status" > "$evidence_dir/${name}.exit"
	printf 'LIFECYCLE_CONTROL_EXIT name=%s status=%s\n' "$name" "$status"
}

run_control egl-direct-1 env SEYRIX_DRM_PRIMARY=/dev/dri/card1 \
	/tmp/seyrix-control-bin/egl-gbm-lifecycle-control 1
run_control egl-direct-5 env SEYRIX_DRM_PRIMARY=/dev/dri/card1 \
	/tmp/seyrix-control-bin/egl-gbm-lifecycle-control 5
run_control wlroots-1 /tmp/seyrix-control-bin/wlroots-renderer-lifecycle-control 1
run_control wlroots-5 /tmp/seyrix-control-bin/wlroots-renderer-lifecycle-control 5

if grep -q 'GL renderer: llvmpipe' "$evidence_dir/seyrix-compositor.log"; then
	printf 'SEYRIX_LIFECYCLE_RENDERER=llvmpipe\n'
else
	printf 'SEYRIX_LIFECYCLE_RENDERER=unknown\n'
fi
for control in egl-direct-1 egl-direct-5; do
	if grep -q '^EGL_CONTROL_GL_RENDERER=llvmpipe' "$evidence_dir/${control}.log"; then
		printf 'LIFECYCLE_CONTROL_RENDERER name=%s renderer=llvmpipe\n' "$control"
	else
		printf 'LIFECYCLE_CONTROL_RENDERER name=%s renderer=not-confirmed\n' "$control"
	fi
done
for control in wlroots-1 wlroots-5; do
	if grep -q 'GL renderer: llvmpipe' "$evidence_dir/${control}.log"; then
		printf 'LIFECYCLE_CONTROL_RENDERER name=%s renderer=llvmpipe\n' "$control"
	else
		printf 'LIFECYCLE_CONTROL_RENDERER name=%s renderer=not-confirmed\n' "$control"
	fi
done
touch /tmp/seyrix-lifecycle-controls.done
printf 'LIFECYCLE_GUEST_RUN=complete\n'
