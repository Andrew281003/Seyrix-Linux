# Archiso profile provenance

This project-owned profile began as the `baseline` profile shipped by Arch
Linux `archiso` 91-1. The upstream package is licensed GPL-3.0-or-later; its
license text is retained in [COPYING.archiso](COPYING.archiso).

The profile keeps Arch's kernel, initramfs, systemd, pacman, official package
repositories, and archiso boot infrastructure. Seyrix changes the image name
and boot menu, adds a text identity and serial console, and trims cloud-init,
SSH, and unrelated virtual-machine guest agents. It adds the official
`linux-firmware`, `iw`, `iwd`, `iptables`, `pv`, and `usbutils` packages for
wider hardware coverage, command-line networking, and the archiso copy-to-RAM
path.

No upstream Archiso installation profile is edited during a build. The build
script copies this tracked directory to a temporary profile before invoking
`mkarchiso`.
