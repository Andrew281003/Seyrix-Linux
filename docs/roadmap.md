# Seyrix Linux roadmap

Milestones are gated by working software, documented package state, licensing,
security review, and recoverable testing. Planned work is not a claim that the
feature already exists.

## Project foundation — complete

- Establish the repository and initial architecture and roadmap documents.
- Select the initial distribution foundation.

## Phase 1 — Arch Linux foundation and first boot — complete

- Build a project-owned Archiso profile from the official `baseline` profile.
- Generate a minimal x86-64 development ISO with the Arch stable kernel,
  systemd, pacman, text login, networking, firmware, and Seyrix identity.
- Verify UEFI boot, the live root, systemd, shell commands, hardware
  enumeration, and clean shutdown in QEMU.
- Record tool and package versions, the image checksum, test evidence,
  measurements, limitations, and reproduction instructions.

Acceptance results and remaining limitations are in [the Phase 1 report](phase1-report.md).

## Phase 2 — Hardware and installation design — planned

- Define a supported hardware matrix and test devices using disposable media.
- Design a persistent ext4 installation and recovery path before implementing
  an installer.
- Review live-image authentication, Secure Boot, update, and release policies.

## Phase 3 — Desktop prototypes — planned

- Define the shared graphics, accessibility, and application interfaces.
- Prototype Aero and Chroma as separate presentation styles.
- Keep desktop components compatible with the selected upstream system base.

## Phase 4 — Integration and preview releases — planned

- Integrate reviewed desktop and system components in a controlled image.
- Establish signed update, rollback, licensing, and recovery workflows.
- Publish preview images only after security and licensing review.
