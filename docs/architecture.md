# Seyrix Linux architecture

This document records the selected foundation and the boundaries of the first
development image. It does not claim production readiness or physical hardware
compatibility.

## Phase 1 foundation

- **Distribution:** Arch Linux, x86-64, using the official `core` and `extra`
  repositories and signed packages.
- **Kernel:** Arch's current stable `linux` package. The version used by each
  image is recorded in its package manifest.
- **Init and package management:** systemd and pacman.
- **Image tooling:** a project-owned copy of the Archiso `baseline` profile.
- **Boot:** upstream Archiso GRUB for UEFI and Syslinux for BIOS. UEFI is the
  tested path.
- **Live root:** read-only EROFS with a temporary writable overlay in RAM. No
  partitioning, ext4 installation, or persistent storage is included.
- **Networking:** systemd-networkd and systemd-resolved for wired networking;
  `iw` and `iwd` are available for manual wireless use.
- **Graphics:** no desktop is installed. The image uses the Arch kernel and
  upstream firmware; no custom GPU drivers are provided.
- **Development languages:** C and C++ remain the primary implementation
  languages for future system components.

## Security and licensing

Pacman keeps package signature verification enabled. The live profile has a
local root console login with an empty password, inherited from Archiso's
development baseline. It does not automatically log in and does not include
OpenSSH. Revisit this policy before building any production image. Arch
package license files remain in the image; the copied Archiso profile retains
its GPL-3.0-or-later notice. The repository has not selected a project-wide
license.

## Explicit phase boundaries

Phase 1 does not include Aero, Chroma, a desktop environment, a custom GPU
driver, a kernel fork, an installer, a custom package manager, or a persistent
root filesystem. QEMU results are not evidence of physical hardware support.

## Open decisions for later phases

The supported hardware matrix, production authentication policy, persistent
installation and recovery design, update and release model, Secure Boot
support, desktop graphics stack, and project-wide licensing need separate
decisions and evidence.

## Current status

The Arch foundation and bootable ISO have passed the Phase 1 UEFI QEMU
acceptance checks, including interactive login and clean shutdown. Results and
limits are recorded in the [Phase 1 report](phase1-report.md).
