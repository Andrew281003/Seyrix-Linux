#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="seyrix"
iso_label="SEYRIX_$(date -u --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y%m)"
iso_publisher="Seyrix Project <https://github.com/Andrew281003/Seyrix-Linux>"
iso_application="Seyrix Linux development image"
iso_version="$(date -u --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y.%m.%d)"
install_dir="seyrix"
bootmodes=('bios.syslinux'
           'uefi.grub')
pacman_conf="pacman.conf"
airootfs_image_type="erofs"
airootfs_image_tool_options=('-zlzma,109' -E 'ztailpacking')
bootstrap_tarball_compression=(xz -9e)
file_permissions=(
  ["/etc/shadow"]="0:0:400"
)
