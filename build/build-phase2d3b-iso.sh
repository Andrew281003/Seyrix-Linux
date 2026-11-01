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
build_id="$(date -u +%Y%m%dT%H%M%SZ)"
build_root="${SEYRIX_BUILD_ROOT:-${TMPDIR:-/var/tmp}/seyrix-linux/phase2d3b-${build_id}}"
output_dir="${SEYRIX_OUTPUT_DIR:-${build_root}/out}"
work_dir="${build_root}/work"
profile_dir="${build_root}/profile"
metadata_dir="${build_root}/metadata"
resume_build="${SEYRIX_BUILD_RESUME:-0}"
compositor_build_dir="${build_root}/compositor-build"
protocol_probe_build_dir="${build_root}/protocol-probe-build"
shell_build_dir="${build_root}/shell-build"
settings_build_dir="${build_root}/settings-build"
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

if ! pkg-config --exists wlr-protocols ||
    ! pkg-config --exists wayland-protocols; then
  echo "Install wlr-protocols and wayland-protocols development files." >&2
  exit 2
fi

if [[ "$resume_build" != 0 && "$resume_build" != 1 ]]; then
  echo "SEYRIX_BUILD_RESUME must be 0 or 1." >&2
  exit 2
fi
if [[ -e "$build_root" && "$resume_build" != 1 ]]; then
  echo "Build directory already exists: $build_root" >&2
  echo "Set SEYRIX_BUILD_ROOT to a new, empty path to start another build." >&2
  exit 2
fi

export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"
if [[ ! "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]]; then
  echo "SOURCE_DATE_EPOCH must be a non-negative integer." >&2
  exit 2
fi

mkdir -p "$work_dir" "$output_dir" "$profile_dir" "$metadata_dir" "$staging_dir"
if [[ -n "$(find "$output_dir" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
  echo "ISO output directory must be empty: $output_dir" >&2
  exit 2
fi

meson_setup_or_reconfigure() {
  local build_dir="$1" source_dir="$2"
  shift 2
  if [[ -f "$build_dir/build.ninja" ]]; then
    meson setup --reconfigure "$build_dir" "$source_dir" "$@"
  else
    meson setup "$build_dir" "$source_dir" "$@"
  fi
}

compositor_build_args=(--buildtype=release --prefix=/usr --wrap-mode=nofallback)
if [[ -n "$sanitizers" ]]; then
	compositor_build_args=(--buildtype=debugoptimized --prefix=/usr --wrap-mode=nofallback
		"-Db_sanitize=$sanitizers")
fi
meson_setup_or_reconfigure "$compositor_build_dir" \
	"$repo_root/desktop/compositor" "${compositor_build_args[@]}"
ninja -C "$compositor_build_dir"
meson test -C "$compositor_build_dir" --print-errorlogs
DESTDIR="$staging_dir" meson install -C "$compositor_build_dir"

meson_setup_or_reconfigure "$protocol_probe_build_dir" \
  "$repo_root/desktop/protocol-probe" \
  --buildtype=release --prefix=/usr --wrap-mode=nofallback
ninja -C "$protocol_probe_build_dir"
DESTDIR="$staging_dir" meson install -C "$protocol_probe_build_dir"

meson_setup_or_reconfigure "$shell_build_dir" "$repo_root/desktop/shell" \
  --buildtype=release --prefix=/usr --wrap-mode=nofallback
ninja -C "$shell_build_dir"
DESTDIR="$staging_dir" meson install -C "$shell_build_dir"

meson_setup_or_reconfigure "$settings_build_dir" \
  "$repo_root/desktop/settings" \
  --buildtype=release --prefix=/usr --wrap-mode=nofallback
ninja -C "$settings_build_dir"
meson test -C "$settings_build_dir" --print-errorlogs
DESTDIR="$staging_dir" meson install -C "$settings_build_dir"

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
serial_getty_rule="$profile_dir/airootfs/etc/systemd/system/getty.target.wants/serial-getty@ttyS0.service"
if [[ ! -L "$serial_getty_rule" ]] ||
    [[ "$(readlink "$serial_getty_rule")" != /usr/lib/systemd/system/serial-getty@.service ]]; then
  echo "The existing experimental ttyS0 serial-getty enablement rule is missing from the diagnostic profile." >&2
  exit 1
fi
cp -a "$repo_root/config/graphics-overlay/airootfs/." "$profile_dir/airootfs/"
cp -a "$repo_root/config/compositor-overlay/airootfs/." "$profile_dir/airootfs/"
# Normalize the sourced login profile from Windows checkouts before it enters the ISO.
sed -i 's/\r$//' "$profile_dir/airootfs/home/seyrix/.bash_profile"
install -D -m 0755 "$staging_dir/usr/bin/seyrix-compositor" \
  "$profile_dir/airootfs/usr/bin/seyrix-compositor"
install -D -m 0755 "$staging_dir/usr/bin/seyrix-protocol-probe" \
  "$profile_dir/airootfs/usr/bin/seyrix-protocol-probe"
install -D -m 0755 "$staging_dir/usr/bin/seyrix-shell" \
  "$profile_dir/airootfs/usr/bin/seyrix-shell"
install -D -m 0755 "$staging_dir/usr/bin/seyrix-settings" \
  "$profile_dir/airootfs/usr/bin/seyrix-settings"
install -D -m 0644 "$staging_dir/usr/share/applications/seyrix-settings.desktop" \
  "$profile_dir/airootfs/usr/share/applications/seyrix-settings.desktop"
install -D -m 0755 "$repo_root/tests/settings-render-trace-launcher.py" \
  "$profile_dir/airootfs/usr/local/bin/seyrix-settings-render-trace"
desktop_entry_path="$profile_dir/airootfs/usr/share/applications/seyrix-settings.desktop"
sed -i 's/\r$//' "$desktop_entry_path"
sed -i \
  -e 's#^Exec=seyrix-settings$#Exec=/usr/local/bin/seyrix-settings-render-trace#' \
  -e 's#^TryExec=seyrix-settings$#TryExec=/usr/local/bin/seyrix-settings-render-trace#' \
  "$desktop_entry_path"
if ! grep -qx 'Exec=/usr/local/bin/seyrix-settings-render-trace' "$desktop_entry_path" || \
    ! grep -qx 'TryExec=/usr/local/bin/seyrix-settings-render-trace' "$desktop_entry_path"; then
  echo "Could not route the Settings desktop entry through its diagnostic trace launcher." >&2
  exit 1
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
# The diagnostic-only Wayland trace launcher uses Python to timestamp each client protocol line.
printf 'python\n' >> "$profile_dir/packages.x86_64"
# Pacman treats CRLF package names as distinct names, so normalize the combined list.
sed -i 's/\r$//' "$profile_dir/packages.x86_64"

sed -i \
  -e 's/^iso_name=.*/iso_name="seyrix-phase2d3b"/' \
  -e 's|^iso_application=.*|iso_application="Seyrix Phase 2D.3B native System Settings development image"|' \
  -e '/^file_permissions=(/a\  ["/usr/lib/seyrix-tests/settings-endpoint-probe"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/share/applications/seyrix-settings.desktop"]="0:0:644"' \
  -e '/^file_permissions=(/a\  ["/usr/share/seyrix/build-info"]="0:0:644"' \
  -e '/^file_permissions=(/a\  ["/usr/local/bin/seyrix-settings-render-trace"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-settings"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-protocol-probe"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-shell"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/share/seyrix/Manual.txt"]="0:0:644"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-phase2c-probe-check"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/bin/seyrix-compositor"]="0:0:755"' \
  -e '/^file_permissions=(/a\  ["/usr/local/bin/start-seyrix"]="0:0:755"' \
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
  printf 'Base profile source: %s\n' "$repo_root/config/archiso"
  printf 'Graphics overlay source: %s\n' "$repo_root/config/graphics-overlay"
  printf 'Compositor overlay source: %s\n' "$repo_root/config/compositor-overlay"
  printf 'wlroots pkg-config version: %s\n' "$(pkg-config --modversion wlroots-0.20)"
  printf 'wlr-protocols pkg-config version: %s\n' "$(pkg-config --modversion wlr-protocols)"
  printf 'Compositor build directory: %s\n' "$compositor_build_dir"
  printf 'Protocol probe build directory: %s\n' "$protocol_probe_build_dir"
  printf 'Settings GTK4 pkg-config version: %s\n' "$(pkg-config --modversion gtk4)"
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
