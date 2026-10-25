# Phase 2A engineering report

**Status: COMPLETE.** On 2026-10-25 the Phase 2A ISO passed graphical QEMU
verification, keyboard and mouse input checks, Wayland application input,
session exit, clean shutdown, and the Phase 1 terminal regression.

## Baseline and branch

- Verified Phase 1 commit: `e6155f9973aca449a90d23750deeb35afc89c521`
  (`feat(archiso): add Seyrix Phase 1 image`).
- Work branch: `phase2a-wayland-foundation`.
- The Phase 1 image and logs remain in the original cache paths. The original
  ISO is 907,358,208 bytes with SHA-256
  `610a4e7b4880b44be989e584a1a7e4cf4232b9a989a42f7b48f9df9c9a297d53`.
- Original Phase 1 QEMU evidence remains at
  `/root/.cache/seyrix-linux/qemu-20261025T155647Z/`; its recorded boot to
  login was 51,217 ms with clean shutdown.
- No changes were made to the original Seyrix or Seyrix-NT repositories. No
  push or merge was performed.

## Build and image

The graphics implementation is a separate overlay on the verified Archiso
profile. The existing terminal boot and serial root console remain available;
Wayland starts only when the unprivileged `seyrix` account runs
`start-wayland` on tty1.

Build command from the repository root in the Arch WSL distribution:

```bash
bash build/build-phase2a-iso.sh
```

The build writes into a new, timestamped cache directory and does not reuse or
overwrite the Phase 1 ISO. The verified build output is:

```text
Build root: /root/.cache/seyrix-linux/phase2a-20261025T174234Z/
ISO:        /root/.cache/seyrix-linux/phase2a-20261025T174234Z/out/seyrix-phase2a-2026.10.25-x86_64.iso
Bytes:      1,038,123,008
SHA-256:    644a79dec31a402f941f393fd4f1f00c778b8466872465960a96b2239bb5376
Packages:   233
```

The build used Archiso 91-1 and GRUB 2:2.16-1. The image package inventory,
installed versions, direct dependencies, and package license expressions are
preserved in [phase2a-package-metadata](phase2a-package-metadata/). The
graphics stack summary is in [graphics-architecture.md](graphics-architecture.md).

## QEMU verification

The graphical test ran on Windows 10 / Arch Linux WSL2 with WSLg. The host had
no `/dev/kvm`, so QEMU used TCG. The test configured q35, OVMF UEFI, 4 GiB
RAM, 2 virtual CPUs, `virtio-vga`, a QEMU xHCI controller with USB keyboard
and tablet, a read-only virtual CD, user-mode networking, QMP, and a GTK
display with `gl=off`. OVMF writable variables were a disposable copy. No
physical disk or host device was passed through.

The final run command and full boot/graphics transcript are preserved in
[the graphical QEMU evidence directory](evidence/phase2a/graphics-verified-exit/).
The separate Phase 1 terminal regression evidence is in
[terminal-regression](evidence/phase2a/terminal-regression/).

| Acceptance requirement | Result | Evidence |
|---|---|---|
| ISO builds | PASS | [build transcript](evidence/phase2a/build-transcript.txt), ISO digest above |
| UEFI boot | PASS | OVMF/QEMU run metadata and QEMU command |
| Linux and systemd initialize | PASS | Kernel `7.2.11-arch1-1`, PID 1 systemd, systemd running; logind, networkd, and resolved active |
| Wayland session starts | PASS | Ready marker and Weston plus terminal process checks pass |
| Visible graphical background | PASS | [QMP screenshot with Weston Terminal](images/phase2a-wayland-terminal-keyboard-final.png) |
| Mouse pointer moves | PASS | USB tablet ABS_X/ABS_Y events and [two pointer screenshots](wayland-testing.md#mouse-input) |
| Keyboard input works | PASS | Typed command created the guest marker; visible in the QMP screenshot |
| Wayland application opens and responds | PASS | `weston-terminal` remained running and executed the typed command |
| Session exits to tty1 | PASS | Weston was active before `exit`; the session marker cleared and the tty1 shell/getty returned |
| System shuts down | PASS | `systemctl poweroff`, QEMU exited successfully |
| Phase 1 terminal regression | PASS | Final Phase 2A ISO booted to serial login in 58,418 ms and shut down cleanly |

The input and lifecycle checks used QMP events and guest-side markers. The
screenshots were captured by QEMU QMP and visually inspected after the run;
there was no manual live interaction with the QEMU window. See
[wayland-testing.md](wayland-testing.md) for the detailed evidence and commands.

## Performance and graphics measurements

| Measurement | Result |
|---|---:|
| QEMU start to serial login, graphical run | 62,795 ms |
| QEMU start to Wayland ready marker | 84,854 ms |
| Guest memory total | 3,994,664 KiB |
| Guest memory available at login | 3,617,288 KiB |
| Guest memory available with Wayland active | 3,557,344 KiB |
| Approximate memory used with Wayland active (total minus available) | 437,320 KiB |
| Running systemd services with Wayland active | 15 |
| Installed package count | 233 |

The measured Mesa renderer was `llvmpipe (LLVM 23.1.3, 128 bits)`, using GL
ES 3.2. The launcher sets `LIBGL_ALWAYS_SOFTWARE=1`; QEMU used TCG and
`gl=off`, with VirGL disabled. The kernel loaded `virtio_gpu` and exposed the
Virtio DRM/KMS device at `/dev/dri/card1`. This verifies a virtual 2D display
path and software rendering. It does not establish hardware 3D acceleration.
The timing and memory figures apply only to this WSL2/TCG configuration and
are not compared with native hardware.

## Warnings and remaining limits

- The build emitted `.pacnew` notices for `/etc/issue` and `/etc/shadow`.
  The final account and branded tty1 login were verified in QEMU.
- GRUB reported SBAT level 4 in the installed CSV versus build-time level 6.
  UEFI boot passed with Secure Boot disabled; Secure Boot was not tested.
- The live tty1 convenience login uses a password-disabled, unprivileged
  development account. Root graphical login is not enabled. The serial root
  console remains available for recovery. Do not ship this login policy as a
  production default.
- Arch is rolling release. Versions are recorded, but the repositories were
  not pinned to an immutable snapshot, so byte-for-byte rebuilds are not
  guaranteed.
- Only QEMU TCG, the Virtio virtual GPU, USB keyboard/tablet, and the software
  renderer were tested. Physical GPUs, hardware acceleration, Secure Boot,
  other input devices, installation, and persistent storage remain unverified.

Phase 2A stops at this verified Wayland foundation. Aero, Chroma, a custom
compositor, kernel or driver changes, and Phase 2B work are out of scope.
