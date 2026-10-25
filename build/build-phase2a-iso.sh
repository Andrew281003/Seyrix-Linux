#!/usr/bin/env bash
set -Eeuo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
build_id="$(date -u +%Y%m%dT%H%M%SZ)"
build_root="${SEYRIX_BUILD_ROOT:-${HOME}/.cache/seyrix-linux/phase2a-${build_id}}"
output_dir="${SEYRIX_OUTPUT_DIR:-${build_root}/out}"
work_dir="${build_root}/work"
profile_dir="${build_root}/profile"
metadata_dir="${build_root}/metadata"

if (( EUID != 0 )); then
  echo "Run this script as root inside the Arch Linux build environment." >&2
  exit 2
fi

for command in mkarchiso pacman sha256sum; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required command is missing: $command" >&2
    exit 2
  fi
done

if [[ -e "$build_root" ]]; then
  echo "Build directory already exists: $build_root" >&2
  echo "Set SEYRIX_BUILD_ROOT to a new, empty path to retry." >&2
  exit 2
fi

export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"
if [[ ! "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]]; then
  echo "SOURCE_DATE_EPOCH must be a non-negative integer." >&2
  exit 2
fi

mkdir -p "$work_dir" "$output_dir" "$profile_dir" "$metadata_dir"
if [[ -n "$(find "$output_dir" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
  echo "ISO output directory must be empty: $output_dir" >&2
  exit 2
fi

cp -a "$repo_root/config/archiso/." "$profile_dir/"
cp -a "$repo_root/config/graphics-overlay/airootfs/." "$profile_dir/airootfs/"
cat "$repo_root/config/graphics-overlay/packages.x86_64" >> "$profile_dir/packages.x86_64"
sed -i \
  -e 's/^iso_name=.*/iso_name="seyrix-phase2a"/' \
  -e 's|^iso_application=.*|iso_application="Seyrix Phase 2A Wayland development image"|' \
  -e '/^file_permissions=(/a\  ["/root"]="0:0:750"' \
  -e '/^file_permissions=(/a\  ["/home/seyrix"]="1000:1000:700"' \
  -e '/^file_permissions=(/a\  ["/home/seyrix/.bash_profile"]="1000:1000:600"' \
  -e '/^file_permissions=(/a\  ["/usr/local/bin/start-wayland"]="0:0:755"' \
  "$profile_dir/profiledef.sh"

cp /etc/pacman.conf "$metadata_dir/builder-pacman.conf"
cp /etc/pacman.d/mirrorlist "$metadata_dir/builder-mirrorlist"
cp "$profile_dir/pacman.conf" "$metadata_dir/profile-pacman.conf"
LC_ALL=C pacman -Q > "$metadata_dir/builder-packages.txt"
pacman -Q archiso grub qemu-system-x86 qemu-ui-gtk qemu-hw-display-virtio-gpu \
  qemu-hw-display-virtio-vga \
  edk2-ovmf expect > "$metadata_dir/tool-versions.txt"
pacman-conf --repo-list > "$metadata_dir/repositories.txt"
{
  printf 'Build started UTC: %s\n' "$(date -u --iso-8601=seconds)"
  printf 'SOURCE_DATE_EPOCH: %s\n' "$SOURCE_DATE_EPOCH"
  printf 'Build host kernel: %s\n' "$(uname -r)"
  printf 'Build host virtualization: %s\n' "$(systemd-detect-virt 2>/dev/null || true)"
  printf 'Base profile source: %s\n' "$repo_root/config/archiso"
  printf 'Graphics overlay source: %s\n' "$repo_root/config/graphics-overlay"
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
  weston mesa libinput evtest seatd wayland libdrm systemd ttf-dejavu xkeyboard-config \
  > "$metadata_dir/graphics-stack-info.txt"
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
