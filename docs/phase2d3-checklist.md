# Phase 2D.3 implementation checklist

## 2D.3A — Architecture review

- [ ] Review and approve [phase2d3-architecture.md](phase2d3-architecture.md).
- [ ] Resolve endpoint framing, GTK4 dependency, shell transaction, and display fallback decisions before implementation.

## 2D.3B — Native app foundation

> Progress: implementation and native checks are recorded in [phase2d3b-report.md](phase2d3b-report.md). Graphical acceptance is incomplete, so the 2D.3B acceptance items below remain unchecked.

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
- [x] Keep Fontconfig/Pango and wlroots/Mesa diagnostics visible and separately reported; add no suppressions. Fresh integrated Shell sanitizer suite passed 5/5 with LSan enabled and no sanitizer errors or suppressions; logs: `/var/tmp/seyrix-linux/phase2d3b5-sanitizer-integ-20261107-01/`. Mesa raw lifecycle evidence is retained under `docs/evidence/phase2c2/egl-lifecycle/`; it was not rerun on the integrated source.
- [ ] Pass graphical QEMU interaction, ten cold boots to readiness, two complete end-to-end passes, Weston fallback, and clean shutdown.
- [ ] Record exact files, commands, results, known limits, and retained sanitizer findings in the final report.
- [ ] Do not describe physical multi-monitor or production hardware support without direct acceptance evidence.

## 2D.3B.5 — Settings recovery and final acceptance

> **Current status: INCOMPLETE.** The authorized recovery contract is supervised process relaunch with state restoration under stock GTK/GDK. See [Phase 2D.3B.5 integration report](phase2d3b5-system-settings-integration-report.md). Passing offline tests does not complete production-lifecycle acceptance.

Prior 640×480 supervised-recovery attempt: all six category page-diff checks, singleton, cross-SID denial, IPC/reconnect, and ten descriptor cycles passed. Reopen cycle 1 stopped because the harness omitted Return after typing Settings; deliberate-close suppression passed. An Enter-key correction and offline ordering regression passed. The same-ISO retry status in this historical checkpoint is superseded by the later recovery and input attempts below and in the report; do not count reopen or production recovery as passed.

Prior popup diagnostic: same-candidate targeted retry exited 0 with popup mapping/configure/ack/buffer commit, Desktop row activation/page mapping/render-pixel checks, compact keyboard focus/activation, process exit, persistent control shell, clean shutdown, shell startup recovery, and UEFI/graphical readiness passing. Aggregate result remained **INCOMPLETE by targeted-diagnostic design**; this was not the full six-category matrix or Settings compositor-loss recovery. The dedicated production supervisor recovery run status in this earlier checkpoint is superseded by the later 640×480 recovery attempt below and in the report. Offline supervisor tests 3/3, session-lock/socket-replacement checks, 32 PING/FD-baseline test, and recovery-helper policy checks passed. Fresh Shell sanitizers 5/5 are clean; standalone Pango/Fontconfig control retains 30,465 bytes/916 allocations, and Mesa EGL retention remains unsuppressed with preserved dependency attribution. See report for evidence details.

Subsequent live attempts: 640×480 production IPC/recovery stopped before recovery when compact category input timed out. At 800×600, GTK selected and mapped Desktop & Dock and passed bounds checks, but the screenshot remained Appearance (zero changed pixels). A separate render probe delivered QMP pointer input to the GTK Desktop label but did not activate the row. These runs were **INCOMPLETE**. The evtest-capture-fix retry status in this earlier checkpoint is superseded by the later retry2 result below and in the report. No recovery or 800×600 rendering pass is claimed; see report evidence paths.

Latest continuation: 800×600 retry 2 saw GTK pick the intended label on press without row selection/page transition; guest `evtest` failed with `Permission denied`. No cause or PASS is claimed. Opt-in GDK release tracing and offline regression passed, but that trace change is absent from the ISO. The 640×480 supervised-recovery attempt passed shell fault/IPC startup, pointer press/release acknowledgements, popup configure/ack/commit, Input selection, and state save, then ended **INCOMPLETE before manager recovery** because the harness executed `exit 0` and killed the control shell; its correction is not yet verified. Mesa exact-path LSan control reproduced 31,259 B/154 allocations once and 156,295 B/770 over five cycles. A local guarded `eglTerminate` fix and five-cycle LSan test passed, but integrated candidate/QEMU verification is **NOT RUN**; existing candidate hash predates that source change. See report for evidence and required rebuild/verification. Keep all graphical, lifecycle, Mesa integration, and final acceptance gates unchecked.

New candidate checkpoint: candidate ISO SHA-256 `fd729e3a676e024c843e835b2f0526687d6449a8ec511f08540305d663532afe` was built but **not promoted**. Release compositor/Shell/Settings/supervisor suites passed 4/4, 5/5, 3/3, and 3/3. Offline desktop-entry, page-pixel, render-trace, and GDK input-trace regressions passed. Sanitizer reruns passed compositor 4/4, Settings 3/3, Shell 5/5, supervisor 3/3 without findings/suppressions; initial compositor environment failure and corrected rerun are both documented in the report. The 800×600 candidate render diagnostic ended **INCOMPLETE before render** because its prior-PID check ran before the supervisor launched Settings; no mouse/GTK render result was obtained. Supervisor wait correction is assigned, not yet verified. These results do not change unchecked acceptance gates.

Latest current-candidate 800×600 run: QMP and GTK press/release events reached a GtkLabel inside the Desktop target bounds, but GTK remained on Appearance with no row activation or page transition (`SETTINGS_MOUSE_TRANSITION=failed`). The targeted diagnostic is **INCOMPLETE** at mouse selection; this is not full matrix or acceptance evidence. Evidence path and detailed PASS/FAIL gates are in the report. Acceptance gates remain unchecked.

A GTK mouse-navigation fallback was added after that failure. Its source regression passed 6 assertions; Bash syntax and diff checks passed. No build or QEMU verification has run, so the fallback remains **UNVERIFIED** pending a packaged-candidate build and real guest retest. The 052920 failure remains valid evidence; keep graphical acceptance unchecked.

Current-candidate 640×480 recovery precheck `qemu-graphics-20261107T053927Z`: boot/readiness, shell recovery precheck, manager-launched Settings, authenticated IPC, compact category selection, and state-save passed. The 15-second `settings-recovery-baseline` timed out and the control shell was unavailable afterward; compositor crash and relaunch were **NOT RUN**. This unresolved harness/guest-command timeout is not evidence of supervisor failure. Production recovery remains unchecked; see report.

Follow-up run `qemu-graphics-20261107T055421Z` also passed GTK/IPC/category/state-save, but its 45-second baseline timed out with `settings-state-window-fields-invalid`. Confirmed harness parser bug: `sed '$d'` removed the final `maximized` setting from a sole `[window]` group, exhausting all 40 polls. Although the compositor was killed despite the missing baseline, recovery verification did not complete; run remains **INCOMPLETE** and no supervisor failure is inferred. The parser is corrected to preserve final keys and fail closed before compositor termination; bounded diagnostics and prompt helper-error matching were added. Full offline regression, Bash syntax, Tcl completeness, and diff checks passed. See the subsequent live recovery attempt below; production recovery remains **UNVERIFIED**. Keep recovery unchecked.

Run `qemu-graphics-20261107T061010Z` captured production Settings PID 2201 and baseline (manager 633, compositor 637, category `input`, 720×560, maximized, paned 160), then killed the compositor. Manager recovery timed out after 100 seconds without a result marker. This is **INCOMPLETE**; helper-performance versus lifecycle behavior remains under diagnosis, and neither supervisor failure nor recovery pass is inferred. Keep recovery unchecked; see report for evidence path.

Run `qemu-graphics-20261107T062816Z` passed Input selection/baseline (manager 634, Settings 2218, compositor 638; 720×560, maximized, paned 160). After compositor restart (PID 2755), the timeout line showed Settings PID 2847 alongside unchanged manager PID 634, but no final supervisor record was emitted. Manager ownership, final map, and fresh IPC are unverified. **INCOMPLETE**, not recovery PASS. App/IPC stall versus missed harness events remains unresolved; keep recovery unchecked.

Run `qemu-graphics-20261107T064652Z` on older candidate `fd729e3a676e024c843e835b2f0526687d6449a8ec511f08540305d663532afe` at 640×480 confirmed production manager PID 629 launched child Settings PID 2823 (PPID 629) after compositor PID 2725 readiness; old Settings exited, singleton count was 1, and socket was present. No app-start, endpoint peer, HELLO/snapshot/subscription, or mapped surface appeared by 150s; stage timed out at 200s. **INCOMPLETE**: supervised child launch observed, application recovery not verified. Keep recovery unchecked; see report for full baseline and evidence path.

#### Usage-conservation checkpoint — 2026-11-07

Latest 064652Z evidence is at `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-20261107T064652Z/`; reproduction used `tests/qemu-graphics.sh` with workflow `phase2d3b`, `SEYRIX_QEMU_SETTINGS_IPC_ONLY=1`, and 640×480. Manager readiness-gated child launch was observed, but Settings never reached app-start, fresh IPC, or mapped-surface evidence before timeout. Current candidate SHA predates the GTK mouse fallback; rebuild is required before final campaign.

Current gate summary: release and sanitizer suites **PASS**; offline mouse/recovery regressions **PASS**; initial IPC **PASS**, post-restart IPC/recovery **INCOMPLETE**; full six-category/four-resolution matrix, 10 cold boots on current candidate, Weston/TTY, and two full E2E sessions **NOT RUN**. No candidate promotion or phase PASS. The report lists exact bounded sequential commands, evidence and per-agent outcomes. Overall Phase 2D.3B.5 stays **INCOMPLETE**; all acceptance gates remain open.

Recovery-contract clarifications: old Settings PID 2187 exited status 1; one child was present (partial singleton evidence), but category/window state restoration was **NOT VERIFIED**. Live retry/backoff/loop-guard, intentional-close suppression, and full reopen behavior were **NOT RUN**; offline policy test is PASS only. Full Weston/TTY gate is **NOT RUN** (a limited TTY marker is not acceptance). Documentation agent updated docs only; the main agent normalized both Markdown files to LF after the initial diff check exposed CRLF endings, and the repeat scoped diff check exited 0. No tests/build/QEMU were run by the documentation agent.

- [ ] Verify with the production session supervisor that compositor loss terminates the old Settings process and that a new process starts only after compositor and Wayland readiness.
- [ ] Verify category, window state, and UI preference restoration; singleton enforcement; authenticated fresh IPC, snapshot, and subscriptions; bounded retries/backoff; restart-loop protection; and intentional-close suppression.
- [ ] Preserve evidence of old-process exit and successful new-process recovery.
- [x] Offline Settings native tests (3/3), supervisor policy test (1/1), and integrated Shell sanitizer suite (5/5 with LSan; no errors/suppressions) passed. See the B.5 report for exact evidence references and current run limits.
- [ ] Complete six categories × four resolutions with selection, page identity, mapping/rendering, keyboard, and mouse checks. 640×480 category page-diff subchecks passed in the latest run; reopen remains incomplete and three resolution profiles remain outstanding.
- [ ] Complete lifecycle/security/descriptor checks, Weston/TTY recovery, and two independent full end-to-end QEMU sessions.
- [ ] Report Phase 2D.3B.5 PASS only after every mandatory gate above and the remaining Phase 2D.3B acceptance gates pass.
