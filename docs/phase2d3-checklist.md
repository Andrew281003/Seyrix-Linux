# Phase 2D.3 implementation checklist

## 2D.3A — Architecture review

- [ ] Review and approve [phase2d3-architecture.md](phase2d3-architecture.md).
- [ ] Resolve endpoint framing, GTK4 dependency, shell transaction, and display fallback decisions before implementation.

## 2D.3B — Native app foundation

- [ ] Build `seyrix-settings` in C17 with GTK4 and install its desktop entry.
- [ ] Add the compositor-owned, same-uid authenticated, versioned settings endpoint.
- [ ] Show all six categories, basic search, responsive navigation, status/errors, and read-only About data.
- [ ] Verify ordinary-user launch, keyboard/pointer navigation, and 640×480 layout.
- [ ] Reject malformed, stale, oversized, and unauthorized endpoint requests.

## 2D.3C — Appearance and desktop

- [ ] Implement wallpaper and tested presentation modes, color, dock/taskbar mode, position, alignment, icon size, and pins.
- [ ] Coordinate shell changes with private prepare/commit/abort messages and atomic persistence.
- [ ] Display workspace indicators and mark auto-hide as unavailable.
- [ ] Verify live apply, restart persistence, invalid input, missing wallpaper, and shell restart recovery.

## 2D.3D — Window management and input

- [ ] Expose supported per-workspace modes, gaps, split preferences, app-id rules, switcher scope, and shortcuts.
- [ ] Show truthful keyboard/pointer capability information; leave unsupported device controls unavailable.
- [ ] Verify revision conflicts, reconnect snapshots, and existing compositor/window/input regressions.

## 2D.3E — Displays

- [ ] Discover only compositor-advertised outputs, modes, refresh rates, scales, and positions.
- [ ] Test candidates in the backend before temporary apply.
- [ ] Add compositor-owned confirmation timer, explicit keep/revert, persistent commit after confirmation, and hot-unplug fallback.
- [ ] Verify timeout, cancel, app crash, socket disconnect, output removal, rollback failure, and restart recovery in headless and QEMU runs.

## 2D.3F — Integration and acceptance

- [ ] Run all native builds, schema/IPC/permission/persistence tests, WM regressions, and ASan/UBSan/LSan checks.
- [ ] Keep Fontconfig/Pango and wlroots/Mesa diagnostics visible and separately reported; add no suppressions.
- [ ] Pass graphical QEMU interaction, ten cold boots to readiness, two complete end-to-end passes, Weston fallback, and clean shutdown.
- [ ] Record exact files, commands, results, known limits, and retained sanitizer findings in the final report.
- [ ] Do not describe physical multi-monitor or production hardware support without direct acceptance evidence.
