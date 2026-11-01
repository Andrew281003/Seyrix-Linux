# Phase 2D.3B.5 — System Settings Integration Report

**Acceptance decision: FAIL**

Phase 2D.3B remains unaccepted. A mandatory category-navigation check was exercised and failed: on the 1280×720 run, selecting Desktop & Dock changed the selected sidebar row while the main pane remained on Appearance. At 800×600, the About row was selected while stale Input content remained visible. Other mandatory gates were not completed, so this decision does not imply that those gates failed.

## Initial repository and worktree state

- Reviewed the Phase 2D.3B engineering reports 4b through 4e and the original checklist at docs/phase2d3-checklist.md before implementation. The existing docs/phase2d3b-report.md already classified graphical acceptance as incomplete because earlier captures did not establish that Settings pages were shown.
- The requested development baseline was commit 7bfdc6e5ce889b5a0886737578ba8078dcd5e7ea. Work proceeded in the isolated worktree C:/Users/quake/.codex/worktrees/phase2d3b5-system-settings/Seyrix-Linux on branch codex/phase2d3b5-system-settings.
- Pre-existing uncommitted changes were present in the protected main, Phase 2D2, and diagnostic worktrees. They were left untouched. The diagnostic worktree was at d85e69e0736d2ae586d733926f6d1c9d24f4951e. Two unrelated Phase 2C worktree records were marked prunable and were not changed.
- No reset, clean, rebase, merge, commit, push, or publish operation was performed. Verified ISO and evidence artifacts were preserved.

## Implementation plan and changes

The reviewed work called for integrating the normal system launcher, retaining tracing only as an opt-in diagnostic, adding small launcher/layout/harness regressions, then validating the resulting candidate without replacing stable compositor or shell architecture. Changes remained within that scope.

- Launcher and build integration: build/build-phase2d3b-iso.sh and new build/settings-desktop-entry.sh install and validate the Settings desktop entry. desktop/settings/src/main.c sets single-instance application behavior; tracing remains optional and disabled by default.
- Settings layout and content lifecycle: desktop/settings/src/settings-window.c contains responsive GTK layout/page lifecycle corrections.
- Compositor and read-only IPC integration: desktop/compositor/meson.build and desktop/compositor/src/main.c, output.c, seyrix.h, and window.c contain the reviewed integration corrections. The versioned read-only protocol was not extended with writes.
- Build and integration regressions: tests/settings-desktop-entry-test.sh, tests/qemu-settings-page-check-test.py, tests/settings-render-pixel-compare.py, tests/settings-render-pixel-regression.py, tests/qemu-graphical-readiness-campaign.sh, tests/qemu-graphics-capture.tcl, tests/qemu-graphics-preflight.sh, and related tests/settings-render-trace-* fixtures/helpers preserve optional diagnostic coverage. tests/qemu-graphics.sh, tests/qemu-graphics.exp, and tests/qemu-settings-page-check.py were adjusted for corrected capture/input geometry and assertions.
- The final harness wording reports only that a QMP pixel delta was observed. Pixel change alone is not proof that the Settings window or requested page is visible; the diagnostic gate was found to produce false positives when a terminal disappeared or desktop content changed.

## Launcher integration

**Exercised checks: PASS; complete launcher acceptance: INCOMPLETE.**

The 1280×720 candidate run launched Settings through the installed system entry. The process ran as UID 1000 with the expected Wayland session, XDG runtime, and D-Bus environment. The singleton check passed. Settings IPC probes connected. Desktop-entry validation covered LF and CRLF input. The normal launch did not require the tracing wrapper.

These results establish the launcher path and its observed environment in the exercised run. They do not establish all lifecycle requirements: the full close/reopen campaign, repeated resource-growth checks, and the complete end-to-end graphical sequence were not reached.

## Six-category functional results

| Category | Result | Evidence |
|---|---|---|
| Appearance | INCOMPLETE | Initial page rendered in the normal-launch 1280×720 run. The run stopped on the next category; a complete page identity and lifecycle pass was not completed. |
| Desktop & Dock | FAIL | At 1280×720, selecting the row changed sidebar selection but left Appearance in the main pane. Evidence: /var/tmp/seyrix-linux/qemu-graphics-20261101T165549Z. |
| Window Management | INCOMPLETE | Layout-only captures showed pixel changes, but did not independently establish correct page identity and content. |
| Displays | INCOMPLETE | Layout-only captures showed pixel changes, but did not independently establish correct page identity and content. |
| Input | INCOMPLETE | Layout-only captures showed pixel changes; the complete content and lifecycle acceptance was not reached. |
| About | FAIL | The 800×600 capture showed About selected while stale/overlapping Input content remained. Evidence: /var/tmp/seyrix-linux/qemu-graphics-20261101T161207Z. |

Pointer and keyboard navigation events were exercised in layout-only runs. Those event results do not override the observed mismatch between selected category and displayed page. Search/filter behavior, focus handling, repeat switching without stale content, and close/reopen were not completed as full acceptance gates. No configuration-write behavior was added.

## Responsive layout and accessibility

| Resolution | Result | Evidence and limit |
|---|---|---|
| 640×480 | INCOMPLETE | Layout-only run /var/tmp/seyrix-linux/qemu-graphics-20261101T155853Z captured all six pages and recorded pixel deltas. It explicitly ended INCOMPLETE and did not establish complete page identity or acceptance. |
| 800×600 | FAIL | Layout-only run /var/tmp/seyrix-linux/qemu-graphics-20261101T161207Z showed stale content for About. |
| 1280×720 | FAIL | Normal-launch run /var/tmp/seyrix-linux/qemu-graphics-20261101T165549Z failed Desktop & Dock page selection/content verification. |
| 1920×1080 | NOT_RUN | No graphical layout run was completed. |

The page-helper regression passed 20 cases across the four target profiles. It validates helper arithmetic only; it is not graphical evidence. Essential-control clipping, focus indicators, and all keyboard-accessible actions were not independently accepted at every target size.

## Read-only IPC and recovery

The QEMU probe suite passed its exercised protocol negotiation, snapshot, subscription/live revision update, malformed and oversized-message rejection, wrong-session rejection, and reconnect checks across 640×480, 800×600, and 1280×720 runs. Native permission/credential tests also passed in the offline test suite. No write operation was added.

These probes do not establish the entire required recovery matrix through the running UI. A full compositor restart with live Settings recovery, endpoint recreation under all failure cases, same-UID credential enforcement in every path, and descriptor/socket cleanup under the complete graphical campaign remain INCOMPLETE. Cached values were not accepted as proof of a live page during the failed visual runs.

## Native tests and resource findings

- Release suites: compositor 3/3 PASS; Shell 5/5 PASS; Settings 3/3 PASS.
- Sanitizer suites: compositor 3/3 PASS; Settings 3/3 PASS; Shell 4/5 PASS. The Shell sanitizer run is not a clean pass.
- Shell leak: 140,632 bytes in 4,817 allocations were reported in Fontconfig/Pango. A minimal Pango/Cairo reproduction reported 30,461 bytes in 916 allocations. Seyrix-owned UI resources were freed; the third-party allocation trace remains documented and unsuppressed.
- Mesa EGL leak: 31,259 bytes in 154 allocations through libEGL_mesa and wlr_egl_create_with_drm_fd during seyrix_server_init (around compositor src/main.c line 291). It remains visible and unsuppressed.
- Headless compositor/Shell lifecycle checks passed. Pointer tests requiring a real pointer were NOT_RUN and are not reported as passes.
- Final harness checks passed: Tcl source completeness, 20-case page-helper regression, and bash syntax. The page-helper regression does not certify GUI behavior.

## QEMU integration campaign

### Cold graphical readiness

The readiness campaign recorded 10/10 cold boots reaching graphical readiness at 640×480. The authoritative campaign matrix is /var/tmp/seyrix-linux/evidence/phase2d3b-readiness-20261101T112057Z/campaign.tsv. These boots do not substitute for the complete graphical acceptance passes.

### Graphical runs

- 640×480 layout-only run: startup/recovery, observed launch environment, singleton, IPC probes, navigation input, and shutdown stages passed; six captures recorded pixel changes. Harness result remained INCOMPLETE by design.
- 800×600 layout-only run: launch, IPC, navigation input, and shutdown stages ran; visual inspection identified the About/Input stale-content mismatch. Harness result remained INCOMPLETE by design.
- 1280×720 normal-launch run: launch environment, singleton, and IPC probe stages passed; Desktop & Dock selection/content failed, and the campaign stopped at that stage.
- Two complete end-to-end graphical passes: NOT_RUN.
- 1920×1080 graphical acceptance: NOT_RUN.
- Full Settings close/reopen, compositor/shell stability after all categories, Weston fallback/return-to-TTY, and complete session shutdown acceptance: NOT_RUN or not reached.

Diagnostic runs that used a broad pixel-difference gate were not treated as acceptance evidence. The latest trace run showed the baseline terminal disappearing and the desktop changing; the Settings window itself was not established by that metric. The harness now labels this as an observed QMP pixel delta, not visibility. A corrected offline replay, where used, is supporting evidence only and does not upgrade an incomplete live run.

## Candidate ISO and storage

- Candidate: /var/tmp/seyrix-linux/candidates/seyrix-phase2d3b5-candidate.iso
- Size: 1,122,795,520 bytes
- SHA-256: 82809c2dbe747ec34a8e0dd210193a4e6ea874b75f14f52079dc11a814b1fd65
- Build used the controlled native-ext4 workflow, not Windows DrvFS/9P. No production source changed after the candidate was built; subsequent edits were harness diagnostics and reporting, so the candidate was not rebuilt.
- Storage preflight accounted for 35,782,656,000 bytes used plus a planned 4 GiB reserve, within the 40 GiB managed-artifact budget, with 982,781,902,848 bytes free. No artifacts were deleted. A separate du inventory reported 34,782,023,745 bytes due to its accounting scope.
- The candidate was not promoted. Candidate and QEMU evidence artifacts remain preserved.

## Known limitations and remaining blockers

1. Correct category selection is broken or unreliable in the exercised graphical candidate: Desktop & Dock on 1280×720 and About on 800×600 showed selected rows with stale page content.
2. There are no two complete end-to-end graphical passes, no 1920×1080 GUI run, and no completed Weston/TTY and close/reopen campaign.
3. The Settings rendering trace visibility heuristic generated false positives and remains diagnostic-only. Pixel deltas are not page-identity proof.
4. Shell and Mesa sanitizer findings remain open and unsuppressed; attribution for Fontconfig/Pango is supported by the minimal reproduction, while Mesa EGL allocations remain in the compositor initialization trace.
5. Some pointer, restart/recovery, resource-growth, accessibility, and lifecycle acceptance checks were not executed or adequately verified.

## Final acceptance decision

**FAIL.** The mandatory category navigation and correct visible page requirement was exercised and failed in the 1280×720 and 800×600 evidence. Under the requested result definitions, an exercised mandatory failure requires FAIL even though several other acceptance gates remain INCOMPLETE. Phase 2D.3B is not complete; no Phase 2D.3C work was started.

## Phase 2D.3B.5 diagnostic acceptance configuration correction — 2026-11-02

This addendum records the diagnostic candidate and the latest acceptance work. The acceptance decision remains **FAIL / INCOMPLETE** pending the gates listed below.

### Candidate and hook configuration

- Diagnostic candidate ISO: `/var/tmp/seyrix-linux/candidates/seyrix-phase2d3b5-hooks-diagnostic-20261101T200918Z.iso`
- Size: `1,122,795,520` bytes
- SHA-256: `5ad1d9d280bf2f13b16cbb91cc4d5f0fceb467d4ec490ec3ebf1de7c00914a09`
- The build script reads `SEYRIX_QEMU_ACCEPTANCE_HOOKS` and passes `-Dqemu_acceptance_hooks=true|false` to Meson in both release and sanitizer configurations. The Meson option default is `false`.
- The diagnostic build metadata records `Requested hooks: true` and `enabled-marker-present`; the packaged compositor contains `SEYRIX_QEMU_ACCEPTANCE_SHELL_EXEC_FAILURE=consumed`.
- The standalone production compositor check records `qemu_acceptance_hooks: false`; its compile command has no acceptance-hook define, and its compositor binary has no shell-fixture marker. Acceptance hooks remain test-only.
- The candidate was not promoted or pushed. Existing verified ISOs and worktrees were preserved.
- The latest retention preflight used `41,752,862,720` bytes and reserved `536,870,912` bytes under the `42,949,672,960`-byte budget. No cleanup was performed.

### Required targeted GTK IPC checks

Scoped QEMU evidence is in `/var/tmp/seyrix-linux/qemu-graphics-20261101T202710Z/` (`test.log`, `serial.log`, `expect-session.log`, and `run-result.txt`). The scoped harness marks the overall run `INCOMPLETE` by design; each required targeted stage passed:

- The packaged shell fault-injection fixture was consumed, and the shell restart retry sequence was observed: `SHELL_FAULT_FIXTURE=passed`.
- The real GTK Settings process PID `2091`, UID `1000`, session `580`, connected to compositor PID `626` and completed HELLO, snapshot request `2` (revision `9`), and subscription request `3` (revision `9`).
- Compositor restart replaced PID `626` with PID `2750`; the Settings socket was recreated with owner/mode `1000:1000:600`.
- Relaunched real GTK Settings PID `2800` completed HELLO, a fresh snapshot request `2` (revision `9`), and subscription request `3` (revision `9`) in the same session.
- Both IPC sequences are recorded as `SETTINGS_IPC_DIAGNOSTIC_RESULT=passed`. Missing optional diagnostic messages were not used as evidence of IPC failure.

### Latest full campaign

The latest full run is `/var/tmp/seyrix-linux/qemu-graphics-20261101T233708Z/`. It stopped with `Run result: INCOMPLETE`, exit status `1`:

- Shell fixture recovery, desktop and valid-launcher checks, Settings launcher identity, singleton behavior, wrong-session rejection, live GTK Settings HELLO/snapshot, and bounded subscription notification passed.
- The first-terminal identity check failed because QMP typed extra `x` characters into `.seyrix-cwd-verified`, so the expected marker filename was not created. The background-focus test passed.
- The Settings search filter did not visibly clear. The before/after page ROI was identical (`changed_pixels=0 total_difference=0`), and the harness stopped before category navigation and responsive-layout checks. This is a GUI acceptance failure; it does not negate the IPC evidence above.
- Earlier full runs are preserved under `qemu-graphics-20261101T223902Z`, `225555Z`, `231124Z`, and `232736Z`. They document the search-clear and input-harness iterations, including one run stopped by an unmapped Tcl semicolon key. No run completed full acceptance.

### Remaining acceptance gates

1. Stabilize QMP terminal input and pass the per-terminal working-directory identity check.
2. Clear the Settings search entry through a confirmed UI input action and pass the pixel/page identity check.
3. Complete category selection and correct page-content checks for all six categories at the required resolutions.
4. Complete remaining profile, pointer/keyboard navigation, close/reopen, compositor/shell stability, fallback/TTY, shutdown, and lifecycle checks.
5. Complete the required full end-to-end acceptance passes and resolve the earlier category-content failure documented above.

No ISO promotion or push is permitted until all mandatory acceptance gates pass.
