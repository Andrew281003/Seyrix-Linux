# Compositor development

## Build environment

Build the native compositor and ISO inside the Arch Linux WSL distribution.
Use the active Arch stable repositories and keep pacman package-signature
checks enabled. Required packages are `archiso`, `base-devel`, `meson`,
`ninja`, `pkgconf`, `wlroots0.20`, `wayland-protocols`, `weston`, QEMU, OVMF,
and Expect. The build script checks that the installed `wlroots-0.20`
pkg-config dependency belongs to the 0.20 API series.

From PowerShell:

```powershell
$repo = 'C:\path\to\Seyrix-Linux'
$repoWsl = (wsl -d archlinux -- wslpath -a $repo).Trim()
wsl -d archlinux -u root -- pacman -S --needed archiso base-devel meson ninja pkgconf wlroots0.20 wayland-protocols weston qemu-system-x86 edk2-ovmf expect
wsl -d archlinux -u root -- bash "$repoWsl/build/build-phase2b-iso.sh"
```

The script compiles `desktop/compositor/` with Meson and Ninja, stages the
binary into a fresh Archiso profile, adds the compositor overlay, and builds a
new Phase 2B ISO under `/root/.cache/seyrix-linux/phase2b-<UTC timestamp>/`.
It records the tool versions, wlroots API version, package manifests, licenses,
profile hashes, build log, ISO size, and checksum. Existing Phase 1 and Phase
2A build roots are not reused.

## Run and verify

Boot the new ISO with the graphical Phase 2B test:

```powershell
wsl -d archlinux -u root -- bash "$repoWsl/tests/qemu-phase2b.sh" `
  /root/.cache/seyrix-linux/phase2b-<UTC timestamp>/out/<phase2b ISO>.iso
```

Run the existing graphical workflow against the same ISO to verify the Weston
reference session, then run the serial test for the Phase 1 terminal
regression:

```powershell
wsl -d archlinux -u root -- bash "$repoWsl/tests/qemu-graphics.sh" /path/to/phase2b.iso
wsl -d archlinux -u root -- bash "$repoWsl/tests/qemu-uefi.sh" /path/to/phase2b.iso
```

QEMU uses q35, OVMF UEFI, 4 GiB RAM, virtio-vga, a USB keyboard, and a USB
tablet. It uses TCG and GTK with GL disabled. The tests capture serial logs,
QMP input events, screenshots, compositor logs, package counts, memory data,
and shutdown results under a new `/root/.cache/seyrix-linux/` run directory.

## Start in the guest

Log in to the unprivileged `seyrix` account on local tty1. Start either
session manually:

```sh
start-seyrix
```

`start-seyrix` checks the account, tty, runtime directory, and active
systemd-logind session. It uses libseat's logind backend and sets
`LIBGL_ALWAYS_SOFTWARE=1` plus `WLR_RENDERER_ALLOW_SOFTWARE=1` for reliable
QEMU rendering. The compositor chooses the Wayland socket name and exports
`WAYLAND_DISPLAY` to `weston-terminal`. The launcher writes compositor output
to `/tmp/seyrix-compositor.log` and keeps it visible on tty1.

The Weston reference remains available with `start-wayland`. Neither session
is a mandatory boot path. The serial root recovery terminal remains enabled.

## Troubleshooting

- If the build cannot find wlroots, install `wlroots0.20` from the active Arch
  repositories and check `pkg-config --modversion wlroots-0.20`.
- If startup reports that `XDG_RUNTIME_DIR` is missing, start from the local
  tty1 logind session. Do not run the compositor as root.
- If no output appears, inspect `/tmp/seyrix-compositor.log` for backend,
  renderer, seat, and output errors. The compositor exits if no usable output
  is discovered.
- If the renderer cannot initialize, check the guest's Mesa and DRM packages.
  Keep QEMU `gl=off`; the Phase 2B test does not require physical GPU access.
- Use **Super+Shift+Q** to return to the development shell. Use **Super+Q** to
  ask the focused application to close itself.
