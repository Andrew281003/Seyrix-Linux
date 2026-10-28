# Seyrix Linux architecture

Seyrix is an Arch Linux-based operating system under active development. This document describes the system represented by the Phase 2D.2 development image. It does not claim production readiness or physical hardware compatibility.

## System foundation

- **Distribution:** Arch Linux, x86-64, using the official signed package repositories.
- **Kernel:** Arch Linux kernel packages; each image records its package versions.
- **Init and package management:** systemd and pacman.
- **Image tooling:** a project-owned Archiso profile derived from the upstream baseline profile.
- **Boot:** upstream Archiso GRUB for UEFI and Syslinux for BIOS. UEFI is the tested path.
- **Live root:** read-only EROFS with a temporary writable overlay in RAM. The development image does not provide a persistent installation.
- **Networking:** systemd-networkd and systemd-resolved for wired networking; iw and iwd are available for manual wireless use.
- **Graphics stack:** upstream Linux graphics infrastructure and package-provided drivers. The project does not provide a custom kernel or GPU driver.
- **Implementation language:** the compositor and desktop shell are native C17 components.

## Compositor and shell

The native compositor implements a Wayland display server using wlroots. The Phase 2D.2 graphical acceptance image used Arch's wlroots0.20 package, version 0.20.2. The compositor owns output and input handling, surface lifecycle, window state, workspaces, and window-management policy.

The desktop presents one unified shell. A separate C17 shell client communicates with the compositor over a bounded private IPC channel. The shell configuration is read from $XDG_CONFIG_HOME/seyrix/shell.conf, falling back to ~/.config/seyrix/shell.conf. It is data in a versioned key=value format, not executable code.

The shell supports a dock or taskbar presentation, top or bottom placement, dock alignment, icon sizing, wallpaper and background color, pinned applications, launcher shortcuts, and window-switcher shortcuts. Configuration is validated and loaded at startup; there is no graphical configuration editor or live reload in this milestone.

The desktop shell includes:

- **Dock/taskbar:** shows pinned and running applications, groups windows by application where identifiers are available, and can activate a running window.
- **Launcher:** searches visible XDG desktop entries and launches them through GLib desktop-entry APIs.
- **Window switcher:** presents windows in most-recently-used order and supports keyboard selection and activation.
- **Workspaces:** provides four workspaces with visible controls and selection state.

Dock auto-hide is represented in configuration but is not implemented; the panel remains visible.

## Window management

The compositor supports three window-management modes:

- **Floating:** windows keep independently positioned geometry.
- **Automatic Tiling:** eligible windows are arranged automatically.
- **Hybrid:** combines tiling and floating behavior, with the user able to change a window's classification. Hybrid is the default mode.

Mode and layout state are maintained per workspace. The Phase 2D.1 report records graphical and protocol-level checks for focus, movement, resizing, workspace migration, fullscreen, maximize, and restoration. Phase 2D.2 graphical acceptance exercised these behaviors as inherited regressions alongside shell integration.

## Development status and limits

| Phase | Recorded status |
|---|---|
| Phase 1, Phase 2A, Phase 2B | Complete according to their reports. |
| Phase 2B.1 | Acceptance incomplete: the normal llvmpipe/Mesa EGL LeakSanitizer finding remains unresolved, and an earlier pre-userspace scheduler panic remains unexplained and unreproduced. |
| Phase 2C, Phase 2C.1, Phase 2C.2 | Acceptance incomplete. Functional work is recorded, but the wlroots/Mesa EGL sanitizer allocation ownership was not established. |
| Phase 2D.1 | Pass for the documented window-management scope. |
| Phase 2D.2 | Pass for staged local checks and the final graphical QEMU acceptance. Phase 2D.3 is upcoming. |

Phase 2D.2 also records Fontconfig/Pango allocations in the sanitized shell UI test. The findings are retained without suppressions or speculative cleanup. The session uses process-group cleanup but does not claim strict containment of processes that deliberately leave the group.

The minimal image does not provide System Settings, a package manager, browser, or file manager. Seyrix has no production installer, supported-hardware matrix, production authentication policy, or release/update process. QEMU acceptance is evidence for the tested virtual configuration only. The repository has not selected a project-wide license; the copied Archiso profile retains its upstream GPL-3.0-or-later notice, and Arch packages retain their package licenses.

For detailed results and limits, see the [Phase 2D.1 report](phase2d1-report.md), [Phase 2D.2 Stage 1 report](phase2d2-local-stage1-report.md), [Phase 2D.2 graphical acceptance report](phase2d2-local-stage2-report.md).

## Further phase work

Phase 2D.3 is upcoming. Its scope and acceptance criteria have not been recorded here, and implementation has not started.
