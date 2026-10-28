# Seyrix-Linux

Seyrix is an Arch Linux-based operating system under active development. Its current x86-64 development image is a live system built with Archiso. The desktop stack consists of a native C17 compositor built on Wayland and wlroots, plus one unified, configurable desktop shell. Seyrix is not production-ready, and QEMU results do not establish physical hardware compatibility.

## Desktop

The compositor manages Wayland surfaces, input, outputs, workspaces, and window state. The shell provides:

- A configurable dock or taskbar with pinned and running applications.
- An application launcher with search over visible XDG desktop entries.
- A most-recently-used window switcher.
- Four workspaces with visible selection controls.
- Floating, Automatic Tiling, and Hybrid window-management modes. Hybrid is the default.

The shell reads user configuration from $XDG_CONFIG_HOME/seyrix/shell.conf, or ~/.config/seyrix/shell.conf when XDG_CONFIG_HOME is unset. Dock auto-hide is not implemented, and there is no graphical settings editor yet.

## Development status

| Milestone | Status |
|---|---|
| Phase 1 — Arch Linux foundation | Complete: the live image passed UEFI boot, interactive login, system checks, and shutdown in QEMU. |
| Phase 2A — Wayland graphics foundation | Complete: the Weston development session passed graphical QEMU input and shutdown checks. |
| Phase 2B — native compositor | Complete: the C17 wlroots compositor passed its initial graphical acceptance. |
| Phase 2B.1 — stability hardening | Acceptance incomplete. The campaign passed its boot and lifecycle checks, but the Mesa EGL LeakSanitizer finding and an earlier pre-userspace scheduler panic remain unresolved. |
| Phase 2C — workspaces and functional stabilization | Acceptance incomplete. Functional progress and later regressions are recorded, while the wlroots/Mesa EGL sanitizer ownership question remains open. |
| Phase 2D.1 — window management | Pass: Floating, Automatic Tiling, and Hybrid behavior passed the documented acceptance checks. |
| Phase 2D.2 — unified desktop shell | Pass: local build and unit checks and the final graphical QEMU acceptance passed. The final campaign recorded ten graphical cold boots and two complete end-to-end passes. |
| Phase 2D.3 | Upcoming; work has not started. |

The Phase 2D.2 QEMU acceptance covered the shell, dock/taskbar, launcher, switcher, workspaces, inherited window management, shell recovery, shutdown, Weston fallback, and compositor cleanup. The report records the exact test boundaries and remaining sanitizer findings.

## Project limitations

Seyrix remains a development system. It has no production installer, supported-hardware matrix, or production release process. The minimal image does not include System Settings, a package manager, browser, or file manager. Dock auto-hide is also incomplete. Known Mesa/EGL and Fontconfig/Pango LeakSanitizer findings remain documented without suppression.

The project has not selected a project-wide license. The Archiso profile retains its upstream GPL-3.0-or-later notice, and redistributed Arch packages retain their package license files.

## Documentation

- [Architecture overview](docs/architecture.md)
- [Roadmap](docs/roadmap.md)
- [Build guide](docs/build-guide.md)
- [Boot architecture](docs/boot-architecture.md)
- [Compositor architecture](docs/compositor-architecture.md)
- [Compositor development guide](docs/compositor-development.md)
- [Phase 1 report](docs/phase1-report.md)
- [Phase 2A report](docs/phase2a-report.md)
- [Phase 2B report](docs/phase2b-report.md)
- [Phase 2B.1 report](docs/phase2b1-report.md)
- [Phase 2C report](docs/phase2c-report.md)
- [Phase 2C.1 report](docs/phase2c1-report.md)
- [Phase 2C.2 report](docs/phase2c2-report.md)
- [Phase 2D.1 report](docs/phase2d1-report.md)
- [Phase 2D.2 Stage 1 report](docs/phase2d2-local-stage1-report.md)
- [Phase 2D.2 graphical acceptance report](docs/phase2d2-local-stage2-report.md)
- [Phase 2D.2 final integration report](docs/phase2d2-final-integration-report.md)
