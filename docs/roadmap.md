# Seyrix Linux roadmap

Milestone status reflects the engineering reports and their stated acceptance criteria. Passing a development milestone does not mean that Seyrix is production-ready.

## Current system

Seyrix is an Arch Linux-based operating system in development. The current desktop uses a native C17 Wayland compositor built on wlroots and one configurable shell. The shell provides a dock or taskbar, launcher, window switcher, and four workspaces. The compositor supports Floating, Automatic Tiling, and Hybrid window management.

## Milestone status

| Phase | Status and scope |
|---|---|
| Phase 1 — Arch Linux foundation | Complete. Built an Archiso live image and verified UEFI boot, login, system checks, and shutdown in QEMU. |
| Phase 2A — Wayland graphics foundation | Complete. Verified a graphical Weston session, input, return to tty1, and shutdown in QEMU. |
| Phase 2B — native compositor | Complete. Introduced and graphically verified the native C17 wlroots compositor. |
| Phase 2B.1 — stability hardening | Acceptance incomplete. Boot and lifecycle campaigns passed, while the Mesa EGL LeakSanitizer finding and the earlier pre-userspace scheduler panic remain unresolved. |
| Phase 2C — workspace foundation and stabilization | Acceptance incomplete. Workspace and interaction work is documented, but Phase 2C, 2C.1, and 2C.2 reports retain an unresolved wlroots/Mesa EGL sanitizer ownership issue. |
| Phase 2D.1 — window management | Pass. Floating, Automatic Tiling, and Hybrid modes and their documented regression checks passed. |
| Phase 2D.2 — unified desktop shell | Pass. Local build and unit checks and final graphical QEMU acceptance passed. The acceptance report preserves test boundaries and remaining limitations. |
| Phase 2D.3 | Upcoming. Work has not started. |

Each phase report records its own scope, evidence, and limits. In particular, later successful graphical checks do not close the earlier Phase 2B.1 or Phase 2C sanitizer findings.

## Phase 2D.3 — upcoming

Phase 2D.3 is the next planned milestone. Its requirements, implementation scope, and acceptance checks remain to be defined before work begins. No Phase 2D.3 implementation is included in this roadmap update.

## Project-wide work still needed

The system remains under development and is not production-ready. A supported-hardware matrix, persistent installation and recovery design, production authentication, update and release model, Secure Boot policy, and project-wide licensing need separate decisions and evidence. The current minimal image has no graphical settings editor, package manager, browser, or file manager. Dock auto-hide is not implemented.

See the [architecture overview](architecture.md), [Phase 2D.1 report](phase2d1-report.md), [Phase 2D.2 Stage 1 report](phase2d2-local-stage1-report.md), and [Phase 2D.2 graphical acceptance report](phase2d2-local-stage2-report.md) for current system details and verification evidence.
