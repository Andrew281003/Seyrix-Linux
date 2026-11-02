# Phase 1 build guide

This guide builds the Seyrix Arch Linux live ISO from the repository-owned
archiso profile. The image is for development and testing; it is not an
installer or a production release.

## Build environment

Use an Arch Linux WSL2 distribution on Windows. The tested host used WSL2;
`mkarchiso` ran its package installation with `systemd-nspawn` inside that
environment. The Windows host does not need QEMU, a Linux VM, or a second
package manager installed.

If Arch Linux is not already registered with WSL, install it with:

```powershell
wsl --install -d archlinux
```

Initialize and populate the Arch package keyring before installing packages.
Keep pacman's signature checks enabled:

```powershell
wsl -d archlinux -u root -- pacman-key --init
wsl -d archlinux -u root -- pacman-key --populate archlinux
wsl -d archlinux -u root -- pacman -Syu --noconfirm
wsl -d archlinux -u root -- pacman -S --needed archiso grub qemu-system-x86 edk2-ovmf expect
```

The build profile uses the official `core` and `extra` repositories and
requires package signatures. `config/archiso/pacman.conf` starts from the
Archiso baseline configuration. The build records the builder package list,
package-manager configuration, mirror list, repository names, and profile
file hashes for each run.

## Build the ISO

From PowerShell, convert the repository path to its WSL path and run the build
script as root inside Arch Linux. For example:

```powershell
$repo = 'C:\path\to\Seyrix-Linux'
$repoWsl = (wsl -d archlinux -- wslpath -a $repo).Trim()
wsl -d archlinux -u root -- bash "$repoWsl/build/build-iso.sh"
```

The script copies the tracked profile into a fresh WSL-native build directory,
then calls `mkarchiso`. It stores the work tree, build log, ISO, exact package
manifest, package license metadata, configuration snapshots, size, and
SHA-256 under:

```text
/var/tmp/seyrix-linux/phase1-<UTC timestamp>/
```

Build outputs stay outside the Git checkout. The script prints the exact ISO
path when it finishes. In the active `archlinux` WSL distribution, `/var/tmp`
is stored in `E:\WSL\archlinux\ext4.vhdx`, so large artifacts use E: without
building on the Windows filesystem. You can browse them at
`\\wsl.localhost\archlinux\var\tmp\seyrix-linux\`.

To set a fixed timestamp for the ISO's version fields, pass
`SOURCE_DATE_EPOCH` (seconds since the Unix epoch) to the build process. The
script records its value. Arch's repositories are rolling; this does not pin
package versions. Use each run's `metadata/image-packages.txt`,
`metadata/image-package-info.txt`, `metadata/image-package-licenses.txt`, and
`metadata/builder-packages.txt` to identify the package and license state that
was used.
The build procedure is repeatable, but byte-for-byte reproducibility has not
been established.

## Build the Phase 2D.2 native shell image

The Phase 2D.2 profile builds the compositor against wlroots 0.20, then builds
the Wayland shell client and installs its read-only `Manual.txt` resource. The
Arch build host needs `mkarchiso`, `meson`, `ninja`, `pkgconf`, `wayland-scanner`,
`git`, and the repository's `wlroots0.20`, `wlr-protocols`, and
`wayland-protocols` packages. The profile adds the shell's GLib, GDK-Pixbuf,
Cairo, and Pango runtime packages to the image.

Run the Phase 2D.2 build script from the repository as root in that Arch
environment:

```sh
bash build/build-phase2d2-iso.sh
```

Its default work, metadata, and ISO directories are under
`/var/tmp/seyrix-linux/phase2d2-<UTC timestamp>/`. Set
`SEYRIX_ARTIFACT_ROOT` to relocate the managed tree; `SEYRIX_BUILD_ROOT` must
be a new child of that root. The script checks the wlroots API version,
builds and runs the compositor and shell Meson suites, stages both native
binaries and the manual, and retains package manifests, build logs, and an ISO
checksum. A successful native build does not replace the graphical runtime
acceptance run described below.

If a build fails, inspect `build.log` and the files under `metadata/`. Each
default build directory is unique and is deliberately not reused. Start a new
run rather than mixing a failed work tree with a later package state.

## Boot and verify with QEMU

Pass the generated ISO to the test workflow:

```powershell
wsl -d archlinux -u root -- bash "$repoWsl/tests/qemu-uefi.sh" `
  /var/tmp/seyrix-linux/phase1-<UTC timestamp>/out/seyrix-<date>-x86_64.iso
```

The workflow uses QEMU's `q35` machine with two virtual CPUs, 4 GiB of RAM,
OVMF UEFI firmware, a disposable copy of OVMF variable storage, a read-only
virtual CD, serial capture, and user-mode networking. It attaches no virtual
hard disk and never passes through a host disk. It waits for the serial login,
logs in as the live root user, checks the kernel, live root, systemd, networking,
memory, services, package count, and PCI enumeration, then requests a clean
shutdown.

This host did not expose `/dev/kvm` or nested hardware virtualization to WSL2,
so the test uses QEMU TCG software emulation. Test evidence is saved under a
fresh directory at:

```text
/var/tmp/seyrix-linux/qemu-<UTC timestamp>/
```

For a failed or interrupted run, the directory retains `qemu-command.txt`,
`run-metadata.txt`, `serial.log`, and `test.log`. The serial log is the boot
and login evidence; reaching GRUB alone does not count as a successful test.
Successful runs keep a compact summary under
`/var/tmp/seyrix-linux/history/` and remove their temporary logs.

## Image contents and access

The live environment has a read-only EROFS image with a temporary writable
overlay in RAM. It does not create or require an ext4 partition. Changes made
in the live session are discarded when the machine shuts down.

The Archiso baseline permits a local root login with an empty password. This
profile does not enable automatic login and removes OpenSSH, so the root
session is available on the local virtual or physical console only. Treat this
as a development image; review the login policy before any production use.
Networked Ethernet uses systemd-networkd and systemd-resolved. The `iw` and
`iwd` command-line tools are present for manual wireless use; wireless hardware
was not exercised by the QEMU test.

## Reproduction limits

Arch package state changes over time. For a detailed audit or later
comparison, set `SEYRIX_RETENTION_KEEP_ARTIFACTS=1` before building to retain
the package manifests, pacman configuration, mirror list, profile hashes,
build log, and ISO checksum. The QEMU results demonstrate a virtual UEFI boot only; they do not establish
physical hardware compatibility, Secure Boot support, installation behavior,
or performance on real hardware.

## Storage retention

All Archiso builders and QEMU harnesses use the external artifact root
`/var/tmp/seyrix-linux` by default. One retention lock serializes builds and
QEMU runs. Before each operation, the scripts check a 40 GiB artifact budget,
reserve 16 GiB for a build or 4 GiB for a QEMU run, and require at least 30 GiB
of additional free space. These limits can be set in bytes with
`SEYRIX_RETENTION_MAX_BYTES`, `SEYRIX_RETENTION_BUILD_RESERVE_BYTES`,
`SEYRIX_RETENTION_QEMU_RESERVE_BYTES`, and
`SEYRIX_RETENTION_MIN_FREE_BYTES`.

Build output is marked as managed only after its path is confirmed outside
every Git worktree. Failed or interrupted builds and tests remain in place.
After a QEMU harness records `Run result: PASS` and confirms QEMU has exited,
it keeps the ISO and a small summary under `verified/` and `history/`, then
removes the marked staging and successful run directories when no process is
using them. It keeps the newest verified ISO and the verified Phase 2D.2
recovery ISO. It refuses artifact roots and test outputs outside the active
Arch filesystem or inside a Git worktree.

For debugging, set `SEYRIX_RETENTION_KEEP_ARTIFACTS=1` to retain successful
build and test outputs. Set `SEYRIX_RETENTION_KEEP_EVIDENCE=1` to retain
successful QEMU evidence, including screenshots and serial logs, while removing
successful build staging. Use `SEYRIX_ARTIFACT_ROOT` to relocate the managed
tree; keep it on the same Linux filesystem as `/var/tmp` and outside all Git
worktrees. Build and QEMU output overrides must be new children of that root.

The latest verified ISO is kept at
`/var/tmp/seyrix-linux/verified/` by default. See
[`storage-cleanup-retention-report.md`](storage-cleanup-retention-report.md)
and the linked artifact manifest for the current cleanup record.
