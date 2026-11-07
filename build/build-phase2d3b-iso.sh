#!/usr/bin/env bash
set -Eeuo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
git_command=(git -C "$repo_root")
if [[ -f "$repo_root/.git" ]]; then
	git_directory="$(sed -n 's/^gitdir: //p' "$repo_root/.git" | tr -d '\r')"
	if [[ "$git_directory" =~ ^([A-Za-z]):/(.*)$ ]]; then
		drive="$(printf '%s' "${BASH_REMATCH[1]}" | tr '[:upper:]' '[:lower:]')"
		git_directory="/mnt/${drive}/${BASH_REMATCH[2]}"
	fi
	if [[ -d "$git_directory" ]]; then
		git_command=(git --git-dir="$git_directory" --work-tree="$repo_root")
	fi
fi
source "$repo_root/build/retention.sh"
source "$repo_root/build/settings-desktop-entry.sh"
seyrix_retention_init
seyrix_retention_acquire_lock
build_id="$(date -u +%Y%m%dT%H%M%SZ)"
build_root="${SEYRIX_BUILD_ROOT:-${SEYRIX_ARTIFACT_ROOT}/phase2d3b-${build_id}}"
build_root="$(realpath -m -- "$build_root")"
output_dir="${SEYRIX_OUTPUT_DIR:-${build_root}/out}"
output_dir="$(realpath -m -- "$output_dir")"
work_dir="${build_root}/work"
profile_dir="${build_root}/profile"
metadata_dir="${build_root}/metadata"
compositor_build_dir="${build_root}/compositor-build"
protocol_probe_build_dir="${build_root}/protocol-probe-build"
shell_build_dir="${build_root}/shell-build"
settings_build_dir="${build_root}/settings-build"
session_supervisor_build_dir="${build_root}/session-supervisor-build"
staging_dir="${build_root}/staging"

if (( EUID != 0 )); then
  echo "Run this script as root inside the Arch Linux build environment." >&2
  exit 2
fi

for command in mkarchiso pacman sha256sum meson ninja pkg-config wayland-scanner git; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required command is missing: $command" >&2
    exit 2
  fi
done

if ! pkg-config --exists wlroots-0.20 ||
    [[ "$(pkg-config --modversion wlroots-0.20)" != 0.20.* ]]; then
  echo "Install wlroots0.20 development files from the active Arch repositories." >&2
  echo "Expected pkg-config dependency wlroots-0.20 in the 0.20 API series." >&2
  exit 2
fi

sanitizers="${SEYRIX_BUILD_SANITIZERS:-}"
if [[ -n "$sanitizers" && "$sanitizers" != address &&
		"$sanitizers" != undefined && "$sanitizers" != address,undefined ]]; then
	echo "SEYRIX_BUILD_SANITIZERS must be empty, address, undefined, or address,undefined." >&2
	exit 2
fi

case "${SEYRIX_QEMU_ACCEPTANCE_HOOKS:-0}" in
	1|true) qemu_acceptance_hooks=true ;;
	0|false|'') qemu_acceptance_hooks=false ;;
	*) echo "SEYRIX_QEMU_ACCEPTANCE_HOOKS must be 0/false or 1/true." >&2; exit 2 ;;
esac

case "${SEYRIX_QEMU_SETTINGS_RENDER_TRACE:-0}" in
	1|true)
		if [[ "$qemu_acceptance_hooks" != true ]]; then
			echo "SEYRIX_QEMU_SETTINGS_RENDER_TRACE requires QEMU acceptance hooks." >&2
			exit 2
		fi
		qemu_settings_render_trace=true ;;
	0|false|'') qemu_settings_render_trace=false ;;
	*) echo "SEYRIX_QEMU_SETTINGS_RENDER_TRACE must be 0/false or 1/true." >&2; exit 2 ;;
esac

case "${SEYRIX_QEMU_SETTINGS_GSK_CAIRO:-0}" in
	1|true)
		if [[ "$qemu_acceptance_hooks" != true ||
				"$qemu_settings_render_trace" != true ]]; then
			echo "SEYRIX_QEMU_SETTINGS_GSK_CAIRO requires QEMU acceptance hooks and Settings render tracing." >&2
			exit 2
		fi
		qemu_settings_gsk_cairo=true ;;
	0|false|'') qemu_settings_gsk_cairo=false ;;
	*) echo "SEYRIX_QEMU_SETTINGS_GSK_CAIRO must be 0/false or 1/true." >&2; exit 2 ;;
esac

case "${SEYRIX_BUILD_NATIVE_SANITIZERS:-0}" in
	1|true) native_sanitizers=true ;;
	0|false|'') native_sanitizers=false ;;
	*) echo "SEYRIX_BUILD_NATIVE_SANITIZERS must be 0/false or 1/true." >&2; exit 2 ;;
esac

if ! pkg-config --exists wlr-protocols ||
    ! pkg-config --exists wayland-protocols; then
  echo "Install wlr-protocols and wayland-protocols development files." >&2
  exit 2
fi

if [[ -e "$build_root" ]]; then
  echo "Build directory already exists: $build_root" >&2
  echo "Set SEYRIX_BUILD_ROOT to a new, empty path to retry." >&2
  exit 2
fi

if ! seyrix_retention_is_managed_path "$build_root" ||
    ! seyrix_retention_is_child "$SEYRIX_ARTIFACT_ROOT" "$build_root" ||
    ! seyrix_retention_is_child "$build_root" "$output_dir"; then
  echo "Build and ISO output paths must be under the external artifact root and outside every Git worktree." >&2
  exit 2
fi
seyrix_retention_preflight "$SEYRIX_RETENTION_BUILD_RESERVE_BYTES" "$build_root"

export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"
if [[ ! "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]]; then
  echo "SOURCE_DATE_EPOCH must be a non-negative integer." >&2
  exit 2
fi

mkdir -p "$build_root"
seyrix_retention_begin "$build_root" build
trap 'seyrix_retention_finish_build "$build_root" "$?"' EXIT
mkdir -p "$work_dir" "$output_dir" "$profile_dir" "$metadata_dir" "$staging_dir"
if [[ -n "$(find "$output_dir" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
  echo "ISO output directory must be empty: $output_dir" >&2
  exit 2
fi

compositor_build_args=(--buildtype=release --prefix=/usr --wrap-mode=nofallback
	"-Dqemu_acceptance_hooks=$qemu_acceptance_hooks")
if [[ -n "$sanitizers" ]]; then
	compositor_build_args=(--buildtype=debugoptimized --prefix=/usr --wrap-mode=nofallback
		"-Db_sanitize=$sanitizers" "-Dqemu_acceptance_hooks=$qemu_acceptance_hooks")
fi
meson setup "$compositor_build_dir" "$repo_root/desktop/compositor" \
	"${compositor_build_args[@]}"
ninja -C "$compositor_build_dir"
meson test -C "$compositor_build_dir" --print-errorlogs
DESTDIR="$staging_dir" meson install -C "$compositor_build_dir"

meson setup "$protocol_probe_build_dir" "$repo_root/desktop/protocol-probe" \
  --buildtype=release --prefix=/usr --wrap-mode=nofallback
ninja -C "$protocol_probe_build_dir"
DESTDIR="$staging_dir" meson install -C "$protocol_probe_build_dir"

meson setup "$shell_build_dir" "$repo_root/desktop/shell" \
  --buildtype=release --prefix=/usr --wrap-mode=nofallback
ninja -C "$shell_build_dir"
meson test -C "$shell_build_dir" --print-errorlogs
DESTDIR="$staging_dir" meson install -C "$shell_build_dir"

settings_build_args=(--buildtype=release --prefix=/usr --wrap-mode=nofallback)
if [[ "$qemu_acceptance_hooks" == true ]]; then
	settings_build_args+=("-Dc_args=-DSEYRIX_SETTINGS_RENDER_DIAGNOSTICS=1")
fi
meson setup "$settings_build_dir" "$repo_root/desktop/settings" \
	"${settings_build_args[@]}"
ninja -C "$settings_build_dir"
meson test -C "$settings_build_dir" --print-errorlogs
DESTDIR="$staging_dir" meson install -C "$settings_build_dir"

meson setup "$session_supervisor_build_dir" "$repo_root/desktop/session-supervisor" \
	--buildtype=release --prefix=/usr --wrap-mode=nofallback
ninja -C "$session_supervisor_build_dir"
meson test -C "$session_supervisor_build_dir" --print-errorlogs
DESTDIR="$staging_dir" meson install -C "$session_supervisor_build_dir"

bash "$repo_root/tests/settings-desktop-entry-test.sh"
python3 "$repo_root/tests/qemu-settings-page-check-test.py"
bash "$repo_root/tests/settings-render-trace-regression.sh"
bash "$repo_root/tests/settings-input-trace-regression.sh"
python3 "$repo_root/tests/settings-render-pixel-regression.py"
tclsh "$repo_root/tests/settings-render-trace-tcl-completeness.tcl" \
	"$repo_root/tests/qemu-graphics.exp" \
	"$repo_root/tests/qemu-graphics-capture.tcl" \
	"$repo_root/tests/settings-render-trace-serial-regression.exp"

if [[ "$native_sanitizers" == true ]]; then
	: > "$metadata_dir/sanitizer-results.txt"
	for component in compositor settings shell session-supervisor; do
		sanitizer_build_dir="$build_root/${component}-sanitizer-build"
		meson setup "$sanitizer_build_dir" "$repo_root/desktop/$component" \
			--buildtype=debugoptimized --prefix=/usr --wrap-mode=nofallback \
			-Db_sanitize=address,undefined
		ninja -C "$sanitizer_build_dir"
		if ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
			meson test -C "$sanitizer_build_dir" --print-errorlogs \
			2>&1 | tee "$metadata_dir/sanitizer-${component}.log"; then
			printf '%s=PASS\n' "$component" >> "$metadata_dir/sanitizer-results.txt"
		else
			printf '%s=FAIL\n' "$component" >> "$metadata_dir/sanitizer-results.txt"
		fi
	done
	printf 'Sanitizer findings summary:\n'
	cat "$metadata_dir/sanitizer-results.txt"
fi

cp -a "$repo_root/config/archiso/." "$profile_dir/"
# Windows Git checkouts can represent index-mode symlinks as regular files
# containing their targets. Recreate tracked Archiso root symlinks in the
# isolated profile so systemd enablement and device aliases work in the image.
while IFS= read -r -d '' index_entry; do
	[[ "${index_entry%% *}" == 120000 ]] || continue
	repo_path="${index_entry#*$'\t'}"
	case "$repo_path" in
		config/archiso/airootfs/*) ;;
		*) continue ;;
	esac
	source_path="$repo_root/$repo_path"
	profile_path="$profile_dir/${repo_path#config/archiso/}"
	if [[ -L "$source_path" ]]; then
		link_target="$(readlink "$source_path")"
	else
		link_target="$(sed -n '1{s/\r$//;p;}' "$source_path")"
	fi
	if [[ -z "$link_target" ]]; then
		echo "Could not read tracked Archiso symlink target: $repo_path" >&2
		exit 1
	fi
	rm -f -- "$profile_path"
	ln -s -- "$link_target" "$profile_path"
done < <("${git_command[@]}" ls-files --stage -z)
cp -a "$repo_root/config/graphics-overlay/airootfs/." "$profile_dir/airootfs/"
cp -a "$repo_root/config/compositor-overlay/airootfs/." "$profile_dir/airootfs/"
cp -a "$repo_root/config/settings-overlay/airootfs/." "$profile_dir/airootfs/"
sed -i 's/\r$//' \
	"$profile_dir/airootfs/usr/local/bin/seyrix-settings-launch"
install -d "$profile_dir/airootfs/etc/systemd/system/multi-user.target.wants"
ln -sfn ../seyrix-qemu-control.service \
	"$profile_dir/airootfs/etc/systemd/system/multi-user.target.wants/seyrix-qemu-control.service"
# Normalize the sourced login profile from Windows checkouts before it enters the ISO.
sed -i 's/\r$//' "$profile_dir/airootfs/home/seyrix/.bash_profile"
install -D -m 0755 "$staging_dir/usr/bin/seyrix-compositor" \
  "$profile_dir/airootfs/usr/bin/seyrix-compositor"
acceptance_hook_marker='SEYRIX_QEMU_ACCEPTANCE_SHELL_EXEC_FAILURE=consumed'
packaged_compositor="$profile_dir/airootfs/usr/bin/seyrix-compositor"
packaged_start_seyrix="$profile_dir/airootfs/usr/local/bin/start-seyrix"
if [[ "$qemu_settings_render_trace" == true ]]; then
	sed -i '/^export LIBGL_ALWAYS_SOFTWARE=1$/a export SEYRIX_COMPOSITOR_RENDER_TRACE=1' \
		"$packaged_start_seyrix"
	if ! grep -Fxq 'export SEYRIX_COMPOSITOR_RENDER_TRACE=1' "$packaged_start_seyrix"; then
		echo "Could not enable compositor render tracing in the diagnostic QEMU image." >&2
		exit 1
	fi
fi
if [[ "$qemu_acceptance_hooks" == true ]]; then
	if ! grep -aFq -- "$acceptance_hook_marker" "$packaged_compositor"; then
		echo "QEMU acceptance hooks were requested but are missing from the packaged compositor." >&2
		exit 1
	fi
	acceptance_hook_check='enabled-marker-present'
else
	if grep -aFq -- "$acceptance_hook_marker" "$packaged_compositor"; then
		echo "QEMU acceptance hooks are disabled, but the packaged compositor contains the test hook." >&2
		exit 1
	fi
	acceptance_hook_check='disabled-marker-absent'
fi
printf 'Requested hooks: %s\nPackaged compositor check: %s\nMarker: %s\n' \
	"$qemu_acceptance_hooks" "$acceptance_hook_check" "$acceptance_hook_marker" \
	> "$metadata_dir/qemu-acceptance-hooks-check.txt"
install -D -m 0755 "$staging_dir/usr/bin/seyrix-protocol-probe" \
  "$profile_dir/airootfs/usr/bin/seyrix-protocol-probe"
install -D -m 0755 "$staging_dir/usr/bin/seyrix-shell" \
  "$profile_dir/airootfs/usr/bin/seyrix-shell"
install -D -m 0755 "$staging_dir/usr/bin/seyrix-settings" \
  "$profile_dir/airootfs/usr/bin/seyrix-settings"
install -D -m 0755 "$staging_dir/usr/bin/seyrix-session-supervisor" \
	"$profile_dir/airootfs/usr/bin/seyrix-session-supervisor"
seyrix_install_settings_desktop_entry \
	"$staging_dir/usr/share/applications/seyrix-settings.desktop" \
	"$profile_dir/airootfs/usr/share/applications/seyrix-settings.desktop"
if [[ "$qemu_settings_gsk_cairo" == true ]]; then
	settings_desktop_entry="$profile_dir/airootfs/usr/share/applications/seyrix-settings.desktop"
	sed -i 's/^Exec=seyrix-settings-launch$/Exec=env GSK_RENDERER=cairo seyrix-settings-launch/' \
		"$settings_desktop_entry"
	if ! grep -Fxq 'Exec=env GSK_RENDERER=cairo seyrix-settings-launch' \
			"$settings_desktop_entry"; then
		echo "Could not enable the diagnostic GTK Cairo renderer for Settings." >&2
		exit 1
	fi
fi
install -D -m 0755 "$settings_build_dir/settings-endpoint-probe" \
  "$profile_dir/airootfs/usr/lib/seyrix-tests/settings-endpoint-probe"
install -D -m 0644 "$staging_dir/usr/share/seyrix/Manual.txt" \
  "$profile_dir/airootfs/usr/share/seyrix/Manual.txt"
install -D -m 0755 "$repo_root/tests/phase2c-probe-check.sh" \
  "$profile_dir/airootfs/usr/bin/seyrix-phase2c-probe-check"
source_revision="$("${git_command[@]}" rev-parse --short HEAD)"
source_branch="$("${git_command[@]}" branch --show-current)"
build_timestamp="$(date -u --date="@$SOURCE_DATE_EPOCH" --iso-8601=seconds)"
install -D -m 0644 /dev/stdin \
  "$profile_dir/airootfs/usr/share/seyrix/build-info" <<EOF
[build]
revision=$source_revision
branch=$source_branch
timestamp_utc=$build_timestamp
EOF
cat "$repo_root/config/graphics-overlay/packages.x86_64" \
  "$repo_root/config/compositor-overlay/packages.x86_64" \
  "$repo_root/config/settings-overlay/packages.x86_64" >> "$profile_dir/packages.x86_64"
# Pacman treats CRLF package names as distinct names, so normalize the combined list.
sed -i 's/\r$//' "$profile_dir/packages.x86_64"

sed -i \
  -e 's/^iso_name=.*/iso_name="seyrix-phase2d3b"/' \
  -e 's|^iso_application=.*|iso_application="Seyrix Phase 2D.3B native System Settings development image"|' \
  -e '/^file_permissions=(/a\  ["/usr/lib/seyrix-tests/settings-endpoint-probe"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/share/applications/seyrix-settings.desktop"]="0:0:644"' \
  -e '/^file_permissions=(/a\  ["/usr/share/seyrix/build-info"]="0:0:644"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-settings"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-session-supervisor"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/local/bin/seyrix-settings-launch"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-protocol-probe"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-shell"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/share/seyrix/Manual.txt"]="0:0:644"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-phase2c-probe-check"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-compositor"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/local/bin/start-seyrix"]="0:0:755"' \
	-e '/^file_permissions=(/a\  ["/etc/udev/rules.d/95-seyrix-qemu-control.rules"]="0:0:644"' \
	-e '/^file_permissions=(/a\  ["/etc/systemd/system/seyrix-qemu-control.service"]="0:0:644"' \
  -e '/^file_permissions=(/a\  ["/root"]="0:0:750"' \
  -e '/^file_permissions=(/a\  ["/home/seyrix"]="1000:1000:700"' \
  -e '/^file_permissions=(/a\  ["/home/seyrix/.bash_profile"]="1000:1000:600"' \
  -e '/^file_permissions=(/a\  ["/usr/local/bin/start-wayland"]="0:0:755"' \
  "$profile_dir/profiledef.sh"

cp /etc/pacman.conf "$metadata_dir/builder-pacman.conf"
cp /etc/pacman.d/mirrorlist "$metadata_dir/builder-mirrorlist"
cp "$profile_dir/pacman.conf" "$metadata_dir/profile-pacman.conf"
LC_ALL=C pacman -Q > "$metadata_dir/builder-packages.txt"
pacman -Q archiso gcc meson ninja pkgconf wlroots0.20 wlr-protocols wayland-protocols gtk4 wlr-randr weston qemu-system-x86 \
  qemu-ui-gtk qemu-hw-display-virtio-gpu qemu-hw-display-virtio-vga \
  edk2-ovmf expect > "$metadata_dir/tool-versions.txt"
pacman-conf --repo-list > "$metadata_dir/repositories.txt"
{
  printf 'Build started UTC: %s\n' "$(date -u --iso-8601=seconds)"
  printf 'SOURCE_DATE_EPOCH: %s\n' "$SOURCE_DATE_EPOCH"
  printf 'Build host kernel: %s\n' "$(uname -r)"
  printf 'Build host virtualization: %s\n' "$(systemd-detect-virt 2>/dev/null || true)"
  printf 'Source branch: %s\n' "$("${git_command[@]}" branch --show-current)"
  printf 'Source commit: %s\n' "$("${git_command[@]}" rev-parse HEAD)"
  if [[ -z "$("${git_command[@]}" status --porcelain)" ]]; then
    printf 'Source worktree: clean\n'
  else
    printf 'Source worktree: modified\n'
    "${git_command[@]}" status --short >> "$metadata_dir/build-environment.txt"
  fi
  printf 'Compositor sanitizers: %s\n' "${sanitizers:-none}"
  printf 'QEMU acceptance hooks: %s\n' "$qemu_acceptance_hooks"
  printf 'QEMU Settings/compositor render tracing: %s\n' "$qemu_settings_render_trace"
  printf 'QEMU Settings Cairo renderer probe: %s\n' "$qemu_settings_gsk_cairo"
  printf 'Base profile source: %s\n' "$repo_root/config/archiso"
  printf 'Graphics overlay source: %s\n' "$repo_root/config/graphics-overlay"
  printf 'Compositor overlay source: %s\n' "$repo_root/config/compositor-overlay"
  printf 'wlroots pkg-config version: %s\n' "$(pkg-config --modversion wlroots-0.20)"
  printf 'wlr-protocols pkg-config version: %s\n' "$(pkg-config --modversion wlr-protocols)"
  printf 'Compositor build directory: %s\n' "$compositor_build_dir"
  printf 'Protocol probe build directory: %s\n' "$protocol_probe_build_dir"
  printf 'Settings GTK4 pkg-config version: %s\n' "$(pkg-config --modversion gtk4)"
  printf 'Settings render diagnostics: %s\n' "$qemu_acceptance_hooks"
  printf 'Settings build directory: %s\n' "$settings_build_dir"
  printf 'Settings build-info revision: %s\n' "$source_revision"
  printf 'Build root: %s\n' "$build_root"
  printf 'ISO output directory: %s\n' "$output_dir"
  printf 'Package signature policy: %s\n' "$(pacman-conf SigLevel)"
} > "$metadata_dir/build-environment.txt"
(cd "$profile_dir" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) \
  > "$metadata_dir/profile-files.sha256"

mkarchiso -w "$work_dir" -o "$output_dir" "$profile_dir" 2>&1 \
  | tee "$build_root/build.log"

mapfile -d '' iso_files < <(find "$output_dir" -maxdepth 1 -type f -name '*.iso' -print0)
if (( ${#iso_files[@]} != 1 )); then
  echo "Expected one ISO in $output_dir; found ${#iso_files[@]}." >&2
  exit 1
fi

iso_path="${iso_files[0]}"
rootfs="$work_dir/x86_64/airootfs"
if [[ ! -d "$rootfs/var/lib/pacman/local" ]]; then
  echo "Could not find the built live root at $rootfs." >&2
  exit 1
fi

pacman --config /dev/null --dbpath "$rootfs/var/lib/pacman" -Q \
  > "$metadata_dir/image-packages.txt"
pacman --config /dev/null --dbpath "$rootfs/var/lib/pacman" -Qi \
  > "$metadata_dir/image-package-info.txt"
pacman --config /dev/null --dbpath "$rootfs/var/lib/pacman" -Qi \
  weston weston mesa libinput evtest wlroots0.20 wayland libdrm systemd \
  gtk4 ttf-dejavu xkeyboard-config wlr-randr > "$metadata_dir/graphics-stack-info.txt"
awk -F ' : ' '/^Licenses[[:space:]]*:/ { print $2 }' "$metadata_dir/image-package-info.txt" \
  | LC_ALL=C sort -u > "$metadata_dir/image-package-licenses.txt"
sha256sum "$iso_path" > "$metadata_dir/iso.sha256"
stat --printf='ISO bytes: %s\n' "$iso_path" > "$metadata_dir/iso-size.txt"
printf 'Image package count: %s\n' "$(wc -l < "$metadata_dir/image-packages.txt")" \
  >> "$metadata_dir/build-environment.txt"
printf 'ISO: %s\n' "$iso_path"
cat "$metadata_dir/iso-size.txt"
cat "$metadata_dir/iso.sha256"
printf 'Build metadata: %s\n' "$metadata_dir"
