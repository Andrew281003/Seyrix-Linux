# Phase 2D.3B.5 — System Settings Integration Report

**Acceptance decision: INCOMPLETE**

Phase 2D.3B remains unaccepted. In the latest 2026-11-08 focused 640×480 run, replacement Settings PID 2731 restored Input state and completed HELLO, snapshot, and subscription, but matching compositor records stop at peer acceptance, watch readiness, and dispatch. The live Wayland trace sidecar was not exported, so its completeness and attribution are **NOT VERIFIED**. The recovery gate is **INCOMPLETE**; no broader matrix or promotion was started. See the final logging candidate addendum below. Earlier GTK navigation evidence remains as recorded below; its root cause remains **UNKNOWN**.

## 2026-11-03 storage and targeted navigation follow-up

**Decision: INCOMPLETE; do not start the four-resolution matrix.**

- The authoritative cap is `SEYRIX_RETENTION_MAX_BYTES` in `build/retention.sh`, consumed by the shared preflight used by ISO builds and QEMU diagnostics/acceptance. Its default is now 161,061,273,600 bytes (150 GiB). The 30 GiB minimum free-space safety reserve, 16 GiB build reserve, and 4 GiB QEMU reserve are unchanged. `tests/retention-validation.sh` passed all 14 checks, including the cap assertion, overflow rejection, free-space rejection, and build/QEMU reserve checks.
- The active Arch WSL distro was `archlinux-d-verify`; `/root/.cache/seyrix-linux` is on `/dev/sdd` ext4. A fresh diagnostic ISO was built at `/root/.cache/seyrix-linux/phase2d3b5-settings-diagnostic-20261103T5/out/seyrix-phase2d3b-2026.11.03-x86_64.iso` (1,129,283,584 bytes; SHA-256 `926c1520735b900a3305dbf743c082e0265f43f30a8e7051140c8352f0d99694`). The ISO includes the latest opt-in GTK raw-input, focus/target, category guard, page map, and delayed row-bounds traces. All build-integrated compositor (3/3), shell (5/5), Settings (3/3), retention (14/14), page-helper, trace-regression, and Tcl source-completeness checks passed.
- A controlled 1280×720 live trace was run against that ISO at `/root/.cache/seyrix-linux/qemu-settings-trace-final-20261103T221657Z`. The fresh Settings process mapped its initial Appearance stack page and passed the first-buffer attach/commit check. At map time the sidebar rows only had placeholder bounds (`20×16`); after GTK allocation the rows were unmapped and `gtk_widget_compute_bounds` no longer returned usable bounds. The trace records Settings at 720×560 with an initial `GtkText` focus owner. It stopped before any category input, as required by the bounds precondition. QEMU exited; the run is INCOMPLETE. Full test, serial, expect, stage, screenshot, checksum, command, and process-state evidence is retained in that run directory. The exact client-side Wayland request delta was not copied out before the precondition exit; only the first-buffer attach/commit check result was preserved.
- The current evidence locates the observed failure in GTK event targeting by the test: the 1280×720 output did not provide a visible sidebar at the 720-pixel-wide Settings surface, so a sidebar click target did not exist. No category pointer or key event was injected in the controlled run, and no `row-activated`, `row-selected`, `category_sync_active` suppression, post-selection stack change, or post-transition page-pixel evidence was produced. This does not establish a separate signal-dispatch, synchronization-guard, or stack-rendering defect. The earlier apparent stale-page failure is consistent with the harness clicking coordinates derived from output size while the app was in compact layout; the compact dropdown path itself was not tested here.
- No production navigation behavior was changed. The latest edits add opt-in diagnostics and evidence gates only. The new 150 GiB cap applies consistently because ISO builds, QEMU diagnostics, and acceptance runs all call the same preflight implementation. Existing artifacts, ISOs, source files, and evidence were preserved; artifact/evidence retention was enabled for builds and QEMU.
- Remaining blockers: exercise a valid category transition through the UI that is actually mapped at 1280×720, capture both mouse and keyboard focus/input/signal/guard/stack/Wayland/page-pixel evidence, and only then start the four-resolution matrix. The previous broad shell-recovery stage also reports a failure in the preserved QEMU log; it is outside this targeted navigation result and remains an acceptance blocker.

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

## Resume audit — 2026-11-02

**Current verdict: INCOMPLETE.** This resume audit supersedes the earlier FAIL decision as the current status. The earlier category captures remain valid evidence against the older `seyrix-phase2d3b5-candidate.iso`; the latest hooks-enabled ISO did not reach category navigation, so the current implementation is not yet accepted or cleared.

### 1. Recovered starting state

- Main checkout: `C:/Users/quake/Desktop/Coding/Seyrix-Linux`, branch `phase2d1-hybrid-window-management`, HEAD `d1873c68a29b24e285e58568551b030985ef8b98`. Its pre-existing modified and untracked files were left untouched.
- Isolated Phase 2D.3B.5 worktree: `C:/Users/quake/.codex/worktrees/phase2d3b5-system-settings/Seyrix-Linux`, branch `codex/phase2d3b5-system-settings`, HEAD `09d897c3fa5b07a49f7f9e0837b54dac66617324` (also `origin/codex/phase2d3b5-system-settings`). Its tracked files were clean at inspection. The pre-existing untracked `phase2d3b5-trace.patch` was preserved.
- The diagnostic ISO build metadata records source commit `7bfdc6e5ce889b5a0886737578ba8078dcd5e7ea` and `Source worktree: modified`. The build used the then-current isolated worktree contents, but the metadata has no source-tree digest, so the ISO is not claimed to be commit-exact provenance for `09d897c`.
- No commit, merge, rebase, reset, clean, push, promotion, or physical-disk operation was performed.

### 2. Acceptance-hook configuration and confirmed root cause

`desktop/compositor/meson_options.txt` defaults `qemu_acceptance_hooks` to `false`. `build/build-phase2d3b-iso.sh` also defaults `SEYRIX_QEMU_ACCEPTANCE_HOOKS` to false and passes the resulting value to Meson. `desktop/compositor/meson.build` adds `-DSEYRIX_QEMU_ACCEPTANCE_HOOKS=1` only when the option is true. The acceptance harness expected the shell fault-injection fixture, so a default build could not emit or consume that diagnostic fixture. Missing hook events therefore did not establish a production IPC defect.

The retained diagnostic build explicitly used `SEYRIX_QEMU_ACCEPTANCE_HOOKS=1`: Meson records `qemu_acceptance_hooks = true`, the packaged compositor contains the fixture marker, and the QEMU run consumed it (`SHELL_FAULT_FIXTURE=passed`). The separate production compositor check records `qemu_acceptance_hooks = false` and has no marker. The compositor, Shell, and Settings binaries in `staging/usr/bin` match the packaged `profile/airootfs/usr/bin` copies by SHA-256:

- `seyrix-compositor`: `d38cbf8bc814aba343e99d410ad4c8fd4459200acaebb59c24624b40dd7fe935`
- `seyrix-shell`: `e74890e666a54db4dda8d3f3acfd586deec91b7f61faec94b1e78175e21d60e4`
- `seyrix-settings`: `8eb41012470bebb100804b0c9892727bff9efd5012898c759e5528047c0f9b3d`

### 3. Resume changes

Only test infrastructure changed: `tests/qemu-graphics.exp` now clears search with Ctrl+A/Backspace and navigates categories with keyboard focus and selection keys, avoiding guessed icon/sidebar coordinates. I also corrected its socket-path fixture: it no longer treats a listener-path rename plus historical log entries as proof of a same-process GTK reconnect. The full harness now records that required gate as `INCOMPLETE` and stops cleanly until a test actually restarts the listener and closes the existing stream. A dedicated Unix-socket regression demonstrates that renaming/restoring a path preserves an accepted stream. No production behavior changed. The revised harness passed offline regressions but has **not** been exercised in QEMU because retention preflight blocked another run.

### 4. Native and offline regression results

The preserved resume log is `docs/evidence/phase2d3b5-resume-native-tests.log` (ignored by the repository's `*.log` rule). Direct execution of the existing release test binaries on 2026-11-02 passed all 11 binaries: compositor 3/3, Shell 5/5, Settings 3/3. No release binaries were rebuilt in this resume.

The 2026-11-02 offline harness checks also passed: Bash syntax; Tcl source completeness; 20 screenshot-helper cases at 640×480, 800×600, 1280×720, and 1920×1080; pixel-comparison regressions; Unix socket-path replacement behavior; desktop-entry LF/CRLF checks; render-trace regressions; and all 14 retention-validation cases. The desktop-entry checker emitted its existing multiple-category hint while both line-ending cases passed.

Previously retained sanitizer logs are not a fresh run against this resume. They record compositor 3/3 PASS, Settings 3/3 PASS, and Shell 4/5 PASS. Shell UI reports a Fontconfig/Pango leak of 140,884 bytes across 4,817 allocations; it remains unsuppressed. The retained compositor investigation also records a Mesa EGL leak of 31,259 bytes across 154 allocations. These are unresolved sanitizer findings, not clean sanitizer acceptance.

### 5. QEMU and graphical acceptance

The readiness evidence at `/var/tmp/seyrix-linux/evidence/phase2d3b5-readiness-20261101T112057Z/campaign.tsv` contains 10/10 cold boots reaching graphical readiness at 640×480. A raw inventory found 51 `qemu-graphics-20261101T*` run directories; every `run-result.txt` says `INCOMPLETE` (5 driver exit status 0, 44 status 1, and 2 status 129). Those are diagnostic/retry runs, not 51 acceptance passes. This inventory does not validate the full matrix.

The latest full run, `/var/tmp/seyrix-linux/qemu-graphics-20261101T233708Z/`, used the hooks-enabled ISO at 800×600. Shell fault recovery, normal launcher identity, singleton behavior (PID 2326 stayed the sole process/window for five seconds), wrong-session rejection, live GTK HELLO/snapshot, and probe subscriptions passed. The socket test only moved and restored the pathname; because it did not restart the listener or close the existing stream, its historical-log search did not prove a GTK reconnect. The run then stopped at search clearing before category navigation: the old hardcoded clear-icon click produced zero content pixels changed. The first-terminal marker check also failed because the injected command text contained extra `x` characters. The keyboard-based harness fix has not been rerun.

Older captures from the non-diagnostic candidate still show apparent selected-row/content mismatches at 800×600 and 1280×720. A later GTK trace showed the Desktop page selected and mapped while an earlier narrow screenshot ROI reported no pixel delta, proving that some prior pixel failures were detector false negatives. Because the newer hooks-enabled run stopped before pages, category identity/rendering remains unresolved.

- The required two complete graphical end-to-end sessions: NOT RUN.
- Complete category and search acceptance at all four resolutions: NOT RUN on the current hooks-enabled ISO; 1920×1080 graphical acceptance remains NOT RUN.
- Same-Settings-process GUI IPC disconnect/reconnect: NOT VERIFIED. The older 800×600 log recorded PID 2326 before and after pathname restoration, but the fixture did not disconnect the accepted stream and the old command scanned pre-trigger HELLO/snapshot history. The corrected full harness now fails closed as `INCOMPLETE` at this gate until a real listener restart/client disconnect can be exercised.
- A full compositor restart while that GTK Settings process remains alive: NOT VERIFIED. The 640×480 diagnostic run closed Settings PID 2091 before restarting compositor PID 626; after socket recreation as `1000:1000:600`, it relaunched Settings as PID 2800 and HELLO/snapshot/subscription passed. This is not same-process compositor-restart evidence.
- Weston/TTY recovery, complete window interaction and responsive-layout matrix, duplicate-subscription/resource-growth checks, and complete shutdown acceptance: NOT COMPLETED as a full matrix.

### 6. ISO artifacts and reproducible commands

All three retained ISO candidates are 1,122,795,520 bytes and remain unpromoted:

| ISO | SHA-256 |
|---|---|
| `/var/tmp/seyrix-linux/candidates/seyrix-phase2d3b5-candidate.iso` | `82809c2dbe747ec34a8e0dd210193a4e6ea874b75f14f52079dc11a814b1fd65` |
| `/var/tmp/seyrix-linux/candidates/seyrix-phase2d3b5-navigation-fix.iso` | `c99f41f87794179a0b01a75dae67ac648fb9f0e8b43ff1e1129898b444c2175b` |
| `/var/tmp/seyrix-linux/candidates/seyrix-phase2d3b5-hooks-diagnostic-20261101T200918Z.iso` | `5ad1d9d280bf2f13b16cbb91cc4d5f0fceb467d4ec490ec3ebf1de7c00914a09` |

Build a fresh diagnostic candidate from the isolated worktree with an unused build directory:

```bash
cd /mnt/c/Users/quake/.codex/worktrees/phase2d3b5-system-settings/Seyrix-Linux
sudo env \
  SEYRIX_ARTIFACT_ROOT=/var/tmp/seyrix-linux \
  SEYRIX_BUILD_ROOT=/var/tmp/seyrix-linux/phase2d3b5-resume-<new-UTC-id> \
  SEYRIX_QEMU_ACCEPTANCE_HOOKS=1 \
  ./build/build-phase2d3b-iso.sh
```

Run the full QEMU driver against a candidate with a new graphics directory; repeat at each required resolution:

```bash
SEYRIX_QEMU_WORKFLOW=phase2d3b \
SEYRIX_QEMU_RESOLUTION_PROFILE=800x600 \
SEYRIX_QEMU_GRAPHICS_DIR=/var/tmp/seyrix-linux/qemu-graphics-<new-UTC-id> \
./tests/qemu-graphics.sh \
  /var/tmp/seyrix-linux/candidates/seyrix-phase2d3b5-hooks-diagnostic-20261101T200918Z.iso
```

The complete QEMU invocation and serial/session traces are preserved in each run's `qemu-command.txt`, `run-metadata.txt`, `expect-session.log`, `serial.log`, `test.log`, and `stage-results.log`. The diagnostic IPC run is `/var/tmp/seyrix-linux/qemu-graphics-20261101T202710Z`; its logs correlate Settings PID 2091 with compositor PID 626, then compositor PID 2750 and relaunch PID 2800.

### 7. Storage gate and remaining work

The existing managed artifact limit is 42,949,672,960 bytes (40 GiB), with the default QEMU reserve of 4,294,967,296 bytes (4 GiB). The attempted fresh run's retention preflight recorded 41,786,777,600 bytes already stored and stopped before launching QEMU. No reserve override or artifact deletion was used. Candidate ISOs, run logs, existing worktrees, and the untracked trace patch remain preserved; no physical disk, Windows boot configuration, or WSL storage location was changed.

Remaining work is to rerun the corrected harness when the existing managed artifact budget permits, verify all six categories and all four layouts on the current hooks-enabled ISO, complete two end-to-end sessions and a same-process compositor restart check, finish the lifecycle/recovery matrix, and resolve or explicitly assess the retained sanitizer findings. Until then, the Phase 2D.3B.5 result is **INCOMPLETE**; no ISO promotion, merge, or push is authorized by this report.

## Resume investigation — 2026-11-03

### Checkpoint and changes

- Development checkpoint remains branch `codex/phase2d3b5-system-settings`, commit `f492c8cf44ac4ee44cad485f265d3fbc9c9dc615`. The rendering-diagnostics comparison point is `68f2dc6` in the separate diagnostic worktree.
- Built candidate ISO: `/root/.cache/seyrix-linux/phase2d3b5-acceptance-20261103T185428Z/out/seyrix-phase2d3b-2026.11.03-x86_64.iso`, SHA-256 `16cb34708d110e48dc7dd0cbcc44aa3bc813d459858446b49b54645291d76674` (1,129,283,584 bytes). Artifacts and QEMU evidence stayed on D: WSL ext4.
- No production GTK/compositor source was changed. Uncommitted harness/report changes are limited to `tests/qemu-graphics.exp` and `tests/settings-render-trace-regression.sh`; the harness now accepts Wayland trace object IDs in both `wl_surface@N` and `wl_surface#N` forms, removes the socket-path rename false gate, checks same-PID recovery after compositor restart, fixes the cleanup marker, and improves live trace singleton handling. Wide-layout category input is being checked with live QEMU evidence; none of these edits is committed.

### Live graphical evidence and outcomes

| Run | Profile / mode | Actual result | Evidence |
|---|---|---|---|
| `qemu-graphics-20261103T-settings-same-process-restart-640x480` | 640×480, IPC-only | Boot, compositor startup, authenticated Settings HELLO/snapshot/subscription, compositor stop/socket removal/restart/socket recreation, new-process Settings recovery, compositor cleanup, and clean guest shutdown passed. Same-process recovery **failed**: Settings PID 2105 was gone after compositor restart; a newly launched PID 3000 completed HELLO/snapshot/subscription. Harness cleanup marker mismatch caused a 10-second false timeout, then cleanup itself passed. | `test.log`, `serial.log`, and before/after PPM captures in that run directory. `SETTINGS_IPC_SAME_PROCESS_RECOVERY=failed`, expected PID 2105, actual PID missing. |
| `qemu-graphics-20261103T-full-1280x720-after-input-fix-1` | 1280×720, full flow | Boot, startup, shell recovery, launcher, Settings singleton, IPC endpoint and subscription checks passed. Category flow **failed** at Desktop & Dock: 245 changed pixels, below the page-change threshold. Captured frame showed the Desktop & Dock row selected while the Appearance page remained visible. The run stopped before remaining categories and lifecycle checks. | `test.log`; `phase2d3b-appearance.ppm`; `phase2d3b-page-current.ppm`. |
| `qemu-graphics-20261103T-full-1280x720-output-width-mode-2` | 1280×720, full flow | Same upstream checks passed. Category flow **failed** at Desktop & Dock with 245 changed pixels. The selected-row highlight changed, but Appearance content stayed visible. | `test.log`; screenshots retained in run directory. |
| `qemu-graphics-20261103T-settings-layout-1280x720-click-3` | 1280×720, layout-only | Search clear passed. Direct click selected Desktop & Dock visually, but stack content still showed Appearance (0 content-difference pixels); run failed closed before other categories. | `test.log`; `phase2d3b-appearance.ppm`, `phase2d3b-page-current.ppm` and PNG inspection copy. |
| `qemu-graphics-20261103T-settings-render-trace-1280x720-actual-click` | 1280×720, render diagnostic | Boot and compositor start passed. Diagnostic reported pointer press at `(70,200)`, 21 surface damage requests, 16 attaches, 18 commits and 18 frame requests; it did **not** capture GTK page selection. Inspection showed the traced launch had activated the existing Settings singleton, whose status had become “Compositor disconnected; showing last-known state.” The diagnostic was inconclusive for category state. | `test.log`, `serial.log`, `settings-render-*.ppm`/PNG copies. |
| `qemu-graphics-20261103T-settings-render-trace-1280x720-singleton-fixed-2` | 1280×720, render diagnostic | **Failed before Settings**: GRUB video argument check rejected the observed kernel command line (`video=Virtual-1:1280x720 e` rather than the expected token). No category result. | `test.log`, `serial.log`, GRUB screenshots. |

The earlier 1280×720 full attempt using the requested-window-width navigation branch also failed at Desktop & Dock. Together, these runs locate two harness defects (using requested window geometry for layout selection and relaunching a trace-instrumented GtkApplication while its untraced singleton is alive), but correcting those has not yet produced a passing category transition. The retained screenshot evidence now shows selected-row feedback without a matching content change. The GTK callback/stack path still needs a process-scoped trace from a fresh traced process before any product-source fix is justified.

### Test coverage

- Offline WSL checks passed after the harness edits: Bash syntax; Tcl source completeness; render-trace regression; both Wayland surface-ID formats; fake reconnect gate rejection; QEMU settings page helper’s 20 regression cases across 640×480, 800×600, 1280×720 and 1920×1080.
- The four-resolution offline helper regression is not live graphical acceptance. Live category navigation has **not passed** at any profile. 640×480 did not run the category matrix; 800×600 and 1920×1080 live category runs were not reached. All six categories at all four resolutions remain unverified.
- No two independent full end-to-end sessions completed. The two 1280×720 full attempts stopped in the first category transition. Startup, shutdown, cleanup, endpoint and shell-failure recovery portions passed where listed above; remaining graphical lifecycle checks were skipped after each fail-closed category stage.
- Artifact preflight reported approximately 4.0 GiB planned per QEMU run and over 900 GiB free under `/root/.cache/seyrix-linux`; storage did not block testing. No generated artifacts or evidence were deleted.

### Sanitizer assessment

No fresh sanitizer campaign was run against this resume ISO. Retained evidence remains unsuppressed: shell UI LeakSanitizer reported 140,884 bytes across 4,817 allocations, with stacks in Fontconfig/Pango; the retained minimal Pango/Cairo reproduction reports approximately 30,465 bytes across 916 allocations, supporting third-party library retention rather than a Seyrix-owned UI allocation. The compositor finding is 31,259 bytes across 154 allocations through `libEGL_mesa` and `wlr_egl_create_with_drm_fd`; the prior EGL investigation attributes the cleanup path to wlroots 0.20.2 skipping `eglTerminate` when `EGL_KHR_display_reference` is unavailable. It remains an unresolved dependency-level finding, not established as a Seyrix-owned leak. Neither finding is cleared by this resume.

### Outstanding issues and verdict

1. Capture a fresh-process GTK trace and correlate selected sidebar row, `GtkStack` visible child, mapped/unmapped pages, Wayland commits and QEMU page pixels. The current live UI appears to select Desktop & Dock without changing the visible page; this is the immediate graphical blocker.
2. Same-process compositor restart acceptance fails because Settings PID 2105 exits with the compositor. A new PID recovers successfully. Native GDK/Wayland display recovery across destruction of the compositor connection is unproven and likely requires an architectural recovery decision; do not count relaunch as same-process recovery.
3. Re-run the corrected graphical test matrix at all four resolutions, then two independent full end-to-end sessions, after category navigation is diagnosed. Re-run the targeted restart check with the cleanup-marker correction and retain actual outcomes.
4. Re-run sanitizer suites on the accepted candidate if sanitizer results are part of final promotion criteria; retain both existing findings unsuppressed.

**Phase verdict: INCOMPLETE.** No live graphical category matrix passed; no two full end-to-end sessions completed; same-process recovery failed. Do not promote or declare Phase 2D.3B.5 complete.

## Fresh-process GTK category transition probe — 2026-11-03

- Execution gate passed: this worktree was readable and writable, `archlinux-d-verify` launched, `qemu-system-x86_64` was available, and `/root/.cache/seyrix-linux` accepted writes. The cache is on the distro's D:-backed WSL virtual disk. No WSL installation or disk configuration changed.
- Built a hooks-enabled diagnostic ISO at `/root/.cache/seyrix-linux/phase2d3b5-gtk-transition-20261103T220000Z/out/seyrix-phase2d3b-2026.11.03-x86_64.iso`; SHA-256 `383797f13d6daa3cbfbc31c4289882d134e5edb212924f98b9a651f8440913c4`.
- One controlled 1280×720 QEMU run is preserved at `/root/.cache/seyrix-linux/qemu-graphics-20261103T220000Z-gtk-mouse-keyboard/`. The harness closed the untraced Settings process and confirmed it exited before launching PID 2082 directly with `SEYRIX_SETTINGS_RENDER_TRACE=1` and `WAYLAND_DEBUG=client`. No existing GtkApplication singleton was activated.
- Mouse input reached the Settings Wayland surface: the client trace records pointer enter and left-button press/release; compositor input records the corresponding press at `(70,200)`. The QMP capture still shows Appearance selected and displayed. GTK emitted no sidebar activation, row-selection notification, or stack visible-child notification after either input; only initial Appearance page/window map events appear. Surface buffers continued committing, with damage/attach/commit counts retained in `test.log`.
- The keyboard sequence generated Wayland key press/release events, but the search field retained focus. It did not activate a category, so it is not a valid keyboard-versus-mouse category comparison. The harness's post-input collection marker passed, but the explicit GTK page-selection check failed. The run correctly ends `INCOMPLETE` / exit 1; its pixel delta is not category-transition evidence.
- No production behavior fix is justified yet. Evidence places the unresolved boundary after Wayland input delivery and before GTK sidebar signals; `GtkStack` and page mapping do not change. Exact GTK hit target/focus remains unproven because the keyboard did not focus the sidebar and the mouse event did not produce widget callbacks. No repeat-after-fix run applies because no fix was made.
- Diagnostic code change: `settings-window.c` now emits opt-in `gtk_sidebar_row_activated` trace events. Harness change: `qemu-graphics.exp` captures separate post-mouse and post-keyboard screen frames and GTK event logs. These are diagnostic-only; category navigation remains unresolved.
- Next decisive experiment: in a fresh traced process, focus a known sidebar row using verified keyboard traversal, then activate Desktop & Dock with keyboard; separately click its screenshot-derived widget bounds after confirming GTK pointer focus. Record GTK focus widget, raw event coordinates, row signals, stack property, and mapped page for each. Change production code only if either input reaches the target widget but GTK state fails to follow.

This single run did not pass category navigation. Do not start the resolution matrix until a fresh-process transition passes.

### Follow-up probe status

- The first ISO's live input trace confirmed GTK received pointer enter and left-button press/release, but the post-guard GTK row trace remained empty. At that time, this could not distinguish a missing `GtkListBox` signal from the `category_sync_active` guard.
- Added opt-in traces for row activation and for row-selection callbacks suppressed by `category_sync_active`, then built them into the fresh diagnostic ISO documented above. The latest trace also reports raw GTK input, focus owner, widget target, allocated row bounds, stack selection, and page map state.
- The first follow-up ISO build stopped at the old 40 GiB retention preflight because existing usage plus the planned build exceeded that cap. No artifact was deleted, moved, or overwritten. The cap has since been raised through the shared preflight, and the fresh build and final live run completed their storage gates.
- The latest evidence explains the earlier 1280×720 sidebar clicks: the Settings surface remained 720×560, entered compact layout, and unmapped the sidebar, so output-derived click coordinates did not target a row. No production navigation defect has been demonstrated. A valid UI category transition remains **untested**, so signal dispatch, guard behavior, and page rendering after a valid activation remain undetermined.
- Next decisive run: test the visible compact selector or arrange a Settings surface wider than 760 pixels, then perform the requested verified mouse and keyboard paths on the live widget. Capture the complete Wayland delta and page pixels before any broader matrix. Change production behavior only if valid input reaches a category target and the page state still fails to follow.

## Urgent checkpoint — 2026-11-04

- **GTK navigation: UNRESOLVED.** No confirmed root cause. Current uncommitted implementation queues stack resize/redraw after page changes and forces Cairo in `main.c`; this remains an unverified workaround, not an established fix.
- The trace-disabled candidate build passed native compositor tests (3/3), Settings tests (3/3), the 20-case page-pixel helper at 640×480, 800×600, 1280×720, and 1920×1080, and the offline render-trace regression. ISO: `/root/.cache/seyrix-linux/phase2d3b-20261104T182232Z/out/seyrix-phase2d3b-2026.11.04-x86_64.iso`; SHA-256 `e2447e5938de6bb4b2c8fa8a73a7b317ede14b73105ab90b77c43f2437f6f641`.
- The already-running 1280×720 QEMU acceptance session ended at the first Desktop category transition. The screenshot comparison reported zero content pixels changed; `test.log` says `Settings content did not change after selecting category desktop`, then `PHASE2D3B_PAGE_CAPTURE=desktop result=INCOMPLETE`. Harness stopped its QEMU. Evidence: `/root/.cache/seyrix-linux/qemu-graphics-cairo-layout-production-1280x720-20261104T184300Z/`. This does not establish whether production GTK or the harness caused the failure.
- A prior traced QEMU attempt at the same resolution saw pointer delivery to a `GtkLabel` within the Desktop & Dock row but no row-selection or stack-selection callback. Neither run demonstrated the required category/page identity match. Other resolutions, keyboard category navigation, all-six category cycling, independent fresh-session reproduction, search/focus acceptance, and final sanitizer acceptance were not completed.
- Modified files in the isolated worktree: `build/build-phase2d3b-iso.sh`, `build/retention.sh`, `desktop/settings/src/main.c`, `desktop/settings/src/settings-window.c`, this report, `tests/qemu-graphics.exp`, `tests/retention-validation.sh`, and `tests/settings-render-trace-regression.sh`. The source diff is 657 insertions/84 deletions across 8 files; largest additions are `settings-window.c` (+312) and `qemu-graphics.exp` (+178). No untracked generated logs or artifacts are present in this worktree; evidence and ISO remain under `/root/.cache/seyrix-linux`.
- Branch/worktree: `codex/phase2d3b5-system-settings` at `f492c8c`, `C:/Users/quake/.codex/worktrees/phase2d3b5-system-settings/Seyrix-Linux`. Changes remain uncommitted. No merge, push, or promotion.

### Resume steps

1. Inspect the failed run's `test.log`, `serial.log`, `expect-session.log`, and before/after PPM captures. Determine whether the click targeted the visible category control and whether the Settings PID/page state changed; do not repeat the same pixel-only experiment unchanged.
2. If needed, rebuild a fresh diagnostic ISO with tracing enabled, then run a fresh 1280×720 GTK-only interaction probe before the full matrix:

```bash
cd /mnt/c/Users/quake/.codex/worktrees/phase2d3b5-system-settings/Seyrix-Linux
env SEYRIX_ARTIFACT_ROOT=/root/.cache/seyrix-linux \
  SEYRIX_RETENTION_KEEP_ARTIFACTS=1 SEYRIX_RETENTION_KEEP_EVIDENCE=1 \
  SEYRIX_QEMU_ACCEPTANCE_HOOKS=1 SEYRIX_QEMU_SETTINGS_RENDER_TRACE=1 \
  ./build/build-phase2d3b-iso.sh
```

3. Only after one real category transition is verified, run the required six-category mouse/keyboard/search matrix at 1280×720, then 640×480, 800×600, and 1920×1080, and repeat in a separate fresh QEMU session.

- At checkpoint time, the build and QEMU harness had both exited; the QEMU harness stopped its guest after the failed first category transition. No active test process was intentionally left running.
- **Phase 2D.3B.5: INCOMPLETE.** GTK navigation is unresolved; no acceptance or ISO promotion.


## Targeted GTK probe checkpoint — 2026-11-04

- **Status: INCOMPLETE.** Branch `codex/phase2d3b5-system-settings`, HEAD `f492c8cf44ac4ee44cad485f265d3fbc9c9dc615`. The active worktree has the same eight modified tracked files listed in the urgent checkpoint and no untracked files. No commit, merge, push, or promotion occurred.
- Inspection retained the prior failed QEMU evidence at `/root/.cache/seyrix-linux/qemu-graphics-cairo-layout-production-1280x720-20261104T184300Z/`. That run showed a selected category row without changed page content, but did not establish GTK state. Current forced-Cairo setup and queued stack resize/redraw remain present and unverified.
- This turn's diagnostic edits resolve picked child widgets to their nearest `GtkListBoxRow`, use live mapped sidebar row bounds for wide-layout mouse input, record and close the pre-existing Settings process before launching a fresh traced process, and add callback/stack/page and keyboard-focus gates. The corresponding offline regression was updated. The live run below stopped before exercising these paths.
- **Build: PASS.** Build exited 0. Candidate ISO: `/root/.cache/seyrix-linux/phase2d3b5-targeted-gtk-20261104T202043Z/out/seyrix-phase2d3b-2026.11.04-x86_64.iso`; SHA-256 `480d8ab07ec169620934f9c662dbcb7da8fb5bd0d3e2cdc31f389622c45b7ccc`.
- **Native tests: PASS** — compositor 3/3, Settings 3/3, shell 5/5. **Offline regressions: PASS** — desktop-entry LF/CRLF, 20 Settings page-helper cases, render-trace regression, pixel regression, socket-path replacement, and Tcl source completeness.
- **Graphical probe: INCOMPLETE, exit 1.** Exactly one QEMU run was started for 1280×720. It passed graphical readiness, shell recovery, and active-output-size checks. At `record the existing Settings PID before closing its singleton`, the guest returned `WAYLAND_TRACE_PRIOR_PID=failed`; `run-result.txt` records INCOMPLETE. Artifacts are preserved at `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-targeted-gtk-20261104T202043Z/` (`test.log`, `serial.log`, `expect-session.log`, `stage-results.log`, `run-metadata.txt`, `run-result.txt`, and screenshots).
- The setup failure for this probe is established: the fresh desktop had no running `seyrix-settings` process, while the harness required `pgrep -u 1000 -x seyrix-settings` to return a pre-existing PID before it would launch the fresh traced process. The desktop was visible at 1280×720, but no Settings window or category control was observed. The control layout is therefore **NOT DETERMINED**; mouse and keyboard transitions are **NOT TESTED**. No fresh Settings PID, GTK callback, selected category, GtkStack page, page mapping, or page-related Wayland commits were captured.
- This explains why this diagnostic attempt stopped; the original GTK navigation root cause remains **UNKNOWN** and no product behavior fix is justified. Forced Cairo and redraw/resize workarounds were retained, not validated as fixes. No additional QEMU run or resolution campaign was started under the stop condition.
- The lightweight Codex CLI 0.166.0 check confirmed the `multi_agent` and reasoning-effort-override features, but local evidence did not establish project-local `.codex/agents/*.toml` discovery or the proposed TOML schema. No agent configuration was changed; this report update used the available documentation subagent role.

### Resumption checkpoint

The next authorized diagnostic must first arrange and verify an untraced Settings process before the harness records and closes its PID, then launch and trace a different fresh process. It must verify the actual visible control before input and collect real GTK and page state. The earlier resume steps above predate this probe and are superseded for this task; no second QEMU run was started after the inconclusive first targeted run. Mouse/keyboard category acceptance and all broader acceptance gates remain **NOT RUN**. Phase 2D.3B.5 remains **INCOMPLETE**.

## Targeted GTK verification continuation — 2026-11-04

**Status: mouse navigation PASS; keyboard navigation NOT TESTED; phase INCOMPLETE.**

- Reused the existing diagnostic ISO without rebuilding. Its SHA-256 was verified as `480d8ab07ec169620934f9c662dbcb7da8fb5bd0d3e2cdc31f389622c45b7ccc`.
- Focused offline checks passed: lifecycle gate regressions for absent/existing Settings PID, launch failure, graceful termination failure, singleton reuse, and missing traced process/window; render-trace regressions; and Tcl source completeness. The Tcl checker was run with quoted paths for `qemu-graphics.exp`, `qemu-graphics-capture.tcl`, and `settings-render-trace-serial-regression.exp`.
- One diagnostic QEMU session ran at 1280×720 using the verified ISO. Evidence is preserved in `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-resume-gtk-20261104T214000Z/`; metadata records diagnostic mode enabled and the expected ISO hash. The harness found no Settings process, launched normal Settings PID `2084` (`/usr/bin/seyrix-settings`), verified its mapped window, sent SIGTERM, and confirmed it exited. It then launched distinct traced PID `2192`, confirmed its mapped window, and observed the first Wayland buffer attach/commit.
- The actual visible control was the **sidebar**. GTK reported the mapped Desktop & Dock row at local bounds `20,183,172,34`; the harness clicked its center at screen position `(106,200)`. The GTK input trace identified the picked `GtkLabel` inside the Desktop & Dock row. The row-selection callback then ran with `category_sync_active=0` and `selected_category=desktop`. `GtkStack` reported visible child title `Desktop & Dock`, selected category `desktop`, and visible/mapped state. Deferred page-bounds tracing confirmed the page allocated at `1063×513` and remained visible and mapped.
- After the input, the client recorded 29 surface damage requests, 21 attaches, and 21 commits. Compositor logs contain later `output_present` records for the same Settings PID and surface. The QMP content comparison passed with 37,625 changed pixels in ROI `210,138-1270,650`. The trace and screenshots jointly establish a genuine Appearance → Desktop & Dock mouse navigation result; the pixel delta alone is not used as proof.
- Keyboard setup clicked Appearance again. GTK recorded the Appearance row-selection callback, `GtkStack` selected Appearance, and the page was visible and mapped. Visual inspection of the preserved frames confirms that `settings-render-appearance.ppm` shows Appearance, `settings-render-mouse-desktop.ppm` shows the correct Desktop & Dock content after the mouse transition, and `settings-render-keyboard-before.ppm` shows the Appearance row highlighted while the content still shows Desktop & Dock. The pixel comparison correctly detected that stale content (`SETTINGS_RESTORE_APPEARANCE_PIXELS=INCOMPLETE`) and stopped before issuing keyboard category input. Keyboard category navigation is **NOT TESTED**. The targeted QEMU run is **INCOMPLETE**, exit 1, with the render mismatch as its stopping reason.
- The valid bounded click proves that the fresh traced process can select and render Desktop & Dock through the mapped sidebar. Returning to Appearance produces a GTK-state/render mismatch, so the broader GTK root cause remains **UNKNOWN**. Incorrect target/layout selection was one harness issue, but it does not explain the observed stale Appearance content. No production GTK change or ISO rebuild was made for this continuation. No further QEMU run or resolution matrix was started.
- A separate non-diagnostic 1280×720 run already owned the shared QEMU lock when this continuation began. It was not launched or modified for this probe and stopped INCOMPLETE at its first Desktop transition. Its evidence remains at `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-targeted-gtk-retry-20261104T2124Z/`; it did not supply GTK trace evidence.

**Remaining blockers:** diagnose the observed GTK state/render mismatch when returning to Appearance; after that is resolved, verify one keyboard category transition with confirmed widget focus. GTK root cause remains **UNKNOWN**. No additional QEMU run was started. Phase 2D.3B.5 remains **INCOMPLETE**.

## Environment recovery and reverse-render checkpoint — 2026-11-05

**Status: environment recovered; forward mouse transition PASS; reverse transition render FAIL; root cause NOT ESTABLISHED; Phase 2D.3B.5 INCOMPLETE.**

- The correct checkout is `C:\Users\quake\.codex\worktrees\phase2d3b5-system-settings\Seyrix-Linux`, branch `codex/phase2d3b5-system-settings`, HEAD `f492c8cf44ac4ee44cad485f265d3fbc9c9dc615`. Eight tracked files were already modified. All four requested source, report, and harness files exist there.
- WSL distribution `archlinux-d-verify` is registered. `/root/.cache/seyrix-linux` and requested evidence directory `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-resume-gtk-20261104T214000Z/` are accessible. Evidence includes GTK and Wayland trace excerpts in `test.log` and `expect-session.log`, compositor presentation records, PPM screenshots and hashes, plus `run-metadata.txt` and `run-result.txt`.
- Saved evidence establishes Appearance → Desktop & Dock rendering: fresh Settings PID 2192 selected and mapped the Desktop page at 1063×513; client trace recorded 29 damage requests, 21 attaches, and 21 commits; compositor later presented the same surface; the QMP region changed 37,625 pixels.
- Returning to Appearance changed the selected sidebar row and `GtkStack` visible child. GTK state reported the Appearance page visible, mapped, and allocated at 1063×513. The captured frame still showed Desktop & Dock content, and the pixel comparison returned INCOMPLETE. The previous harness exited before recording the reverse transition's Wayland requests and compositor presentation delta, so those links remain unverified. Existing forced-Cairo and queued-redraw code is not established as a fix. No production source change is justified by this evidence.
- The render-trace offline regression passed after adding capture of reverse GTK, Wayland, and compositor records before the pixel assertion in `tests/qemu-graphics.exp`. Retention preflight `du -sx /root/.cache/seyrix-linux` took about five minutes and entered uninterruptible disk I/O. The bounded QEMU run then started at 1280×720, but was interrupted with SIGINT after confirming no GTK test stage had begun. It stopped at `wait for live GRUB menu and request a 1280x720 framebuffer and DRM mode`, exit 129. Evidence is preserved at `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-reverse-trace-20261105T143939Z/` (`run-metadata.txt`, `run-result.txt`, logs, and screenshots). QEMU verification is **INCOMPLETE**; no GTK transition occurred.

**Resume:** inspect the preserved incomplete run's GRUB/framebuffer evidence and resolve its startup blocker before the next bounded 1280×720 run. Then capture the Desktop & Dock → Appearance GTK, client Wayland, compositor presentation, and pixel evidence before considering a production change. Root cause remains **UNKNOWN**; no production source edits were made during this checkpoint. Keep the full resolution matrix and keyboard acceptance **NOT RUN** until the cause is established. Preserve all existing worktree changes and artifacts.

## QEMU startup recovery — 2026-11-05

**Status: controlled 1280×720 trace captured; reverse transition PASS in SMP=1 probe; overall QEMU runs INCOMPLETE; root causes NOT ESTABLISHED; Phase 2D.3B.5 INCOMPLETE.**

- The preserved run `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-reverse-trace-20261105T143939Z/` ended with exit 129 and `SIGHUP`. Its log began the 10-second GRUB/framebuffer stage, then stopped without a stage result or timeout reason. The run lasted 17 seconds from QEMU start to cleanup. Its GRUB menu/editor captures and the absent timeout result show interruption, not an observed slow-boot timeout.
- One controlled 1280×720 QEMU diagnostic reused the existing ISO without rebuilding guest code: `/root/.cache/seyrix-linux/phase2d3b5-targeted-gtk-20261104T202043Z/out/seyrix-phase2d3b-2026.11.04-x86_64.iso`, SHA-256 `480d8ab07ec169620934f9c662dbcb7da8fb5bd0d3e2cdc31f389622c45b7ccc`. Run evidence is at `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-reverse-trace-recovery-20261105T1516Z/`.
- This run passed the 10-second GRUB/framebuffer stage and recorded `GRUB_VIDEO_REQUEST_SENT=kernel_arg=video=Virtual-1:1280x720e kernel_line=linux`. UEFI boot then failed before guest control-shell readiness. Serial reports kernel `7.2.14-arch1-1`, `Oops: int3` in `run_timer_softirq+0xce/0xf0` during asynchronous `unpack_to_rootfs` shmem writes through `__mem_cgroup_charge`, followed by `Kernel panic - not syncing: Fatal exception in interrupt` at about 1.014765 seconds. This establishes a real guest-kernel boot failure, not slow startup. Evidence does not establish why that kernel panicked or attribute the defect to a specific component. No GTK, compositor, or Settings stage ran.
- The panic occurred in an SMP timer-softirq path. As a bounded hypothesis probe, a second 1280×720 run changed only the vCPU count to one and reused the same ISO. It reached GUI readiness: boot-control passed in 114,702 ms and readiness passed. Evidence is at `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-reverse-trace-smp1-20261105T1532Z/`. This supports SMP as a diagnostic variable, but does not establish the panic's cause.
- Retention preflight took about 6 minutes 32 seconds for the first run and 6 minutes 28 seconds for the SMP=1 run. `du -sx -B1 -- /root/.cache/seyrix-linux` spent over six minutes in Linux `D` state, with wait channel `wait_on_buffer`. First result: 143,647,326,208 bytes used, 4,294,967,296 planned, 832,439,287,808 free. Second result: 143,656,263,680 used, 4,294,967,296 planned, 832,430,350,336 free. Both fit the existing 161,061,273,600-byte cap. No artifacts were deleted. No optimization was justified: changing scan scope or using a cache risks weakening the cap's recursive allocated-byte accounting without evidence that the replacement is equivalent.
- The SMP=1 probe reproduced the forward Appearance → Desktop & Dock transition: Settings PID 2154 selected the Desktop & Dock row; `GtkStack` exposed that page; its 1063×513 page was visible and mapped; compositor presentation and the page pixel transition were captured. The content comparison reported 37,625 changed pixels.
- In the same probe, the return to Appearance completed through each observed layer before pixel comparison: pointer picked a `GtkLabel` with `target_category=appearance`; the row-selected callback ran with `category_sync_active=0` and `selected_category=appearance`; `GtkStack` selected Appearance and reported its title; the page mapped with bounds 1063×513 and visible/mapped state; and Desktop & Dock unmapped. The client trace captured 12 damage requests, 9 attaches, and 10 commits on `wl_surface` 16. The compositor trace captured 10 successful `output_present` records for PID 2154, surface 16, sequences 154–163. `SETTINGS_RESTORE_APPEARANCE_PIXELS=passed` reported zero changed pixels and zero total difference between the Appearance frame and the pre-keyboard frame. The screenshot hashes differ, but the compared page pixels match. Thus reverse GTK, Wayland, compositor, and pixel evidence matches the earlier successful forward transition and demonstrates correct reverse rendering in this probe.
- The SMP=1 run is still **INCOMPLETE**, exit 1. After capturing the target transition evidence, the `traced_app_exit_command` terminated the long-lived guest control shell; the harness then failed its wait for that shell to return. The overall run did not pass. Targeted keyboard focus, row selection, stack selection, and page-pixel checks passed; broader keyboard acceptance remains **NOT RUN**.
- No production correction is justified. Existing forced-Cairo/redraw changes produced correct reverse rendering in this probe but remain unproven as the cause of the earlier stale capture. The previous stale Appearance capture does not reproduce in this probe; GTK root cause remains **UNKNOWN**. No screenshot assertion ran before the required reverse GTK, stack, page, Wayland, and compositor records were captured in the SMP=1 probe.

**Remaining blockers:** establish why the same ISO panicked with two vCPUs but booted with one; fix the harness control-shell exit failure so the diagnostic can finish cleanly; determine whether reverse rendering remains stable across a clean completion. Keep production behavior unchanged until GTK root cause is established. Full resolution, keyboard, lifecycle, and phase acceptance gates remain **NOT RUN**. Phase 2D.3B.5 remains **INCOMPLETE**.

## Control-shell recovery checkpoint — 2026-11-05

**Status: root cause CONFIRMED; offline harness regressions PASS; QEMU diagnostic NOT RUN; Phase 2D.3B.5 INCOMPLETE.**

- The protected worktree at `C:/Users/quake/.codex/worktrees/phase2d3b5-system-settings/Seyrix-Linux` was at `f492c8cf44ac4ee44cad485f265d3fbc9c9dc615` and contained eight modified tracked files. Those changes were preserved; their exact working copies were transferred into a separate detached Agent 1 worktree at `C:/Users/quake/.codex/worktrees/phase2d3b5b-agent1/Seyrix-Linux` for this investigation.
- Root cause is confirmed in `tests/qemu-graphics.exp`: the success branch in `traced_app_exit_command` executed Bash `exit 0`. The command runs in the persistent interactive guest control shell through `run_serial_check`, so the builtin exited that shell. The harness then waited for the `SEYRIX-USER#` prompt, which could no longer return. This accounts for the previous SMP=1 run ending INCOMPLETE after its rendering evidence had been captured.
- The command now breaks out of the polling loop and evaluates the exit condition after the loop. A real `WAYLAND_TRACE_APP_EXIT=failed` marker is still emitted when the process remains alive or its PID is invalid; successful completion no longer exits the control shell.
- `tests/settings-render-trace-serial-regression.exp` now extracts and exercises the production command in a persistent interactive Bash. The absent-PID case verifies the pass marker, returned prompt, and a subsequent command. The live-PID case verifies the failure marker and failure accounting while confirming the shell still accepts a following command. The fixture removes its temporary PID file and closes the shell on exit. Its marker-order subcase intentionally reports a FAIL stage and is expected by the regression; the overall regression script passed.
- **Tcl source completeness: PASS** — `tclsh tests/settings-render-trace-tcl-completeness.tcl tests/qemu-graphics.exp tests/settings-render-trace-serial-regression.exp tests/qemu-graphics-capture.tcl`.
- **Offline render-trace and harness regression: PASS** — `bash tests/settings-render-trace-regression.sh`, ending with `SETTINGS_RENDER_TRACE_OFFLINE_REGRESSION=PASS`; output also confirms production capture, support-file preflight, GTK transition gates, and the new control-shell cases.
- **1280×720 QEMU diagnostic: NOT RUN at this checkpoint.** The shared lock and existing ISO were being checked. No guest-side source change was made for this fix, so the existing diagnostic ISO is eligible for reuse after its identity and the lock are verified. GTK/Wayland/compositor/page-pixel reproduction, control-shell survival in QEMU, and clean QEMU completion remain **NOT RUN**.
- Harness status: the targeted offline regression is **PASS**. Readiness for the full acceptance campaign remains **NOT ESTABLISHED** until a bounded live diagnostic verifies the corrected command in the guest. Rendering reproducibility, forced-Cairo/redraw workaround necessity, the SMP panic cause, the four-resolution matrix, and full Phase 2D.3B.5 acceptance remain unresolved or **NOT RUN**.

**Resume:** verify the shared QEMU lock is free and identify the existing diagnostic ISO by checksum; then run one bounded 1280×720 diagnostic with one vCPU and preserve trace, screenshot, process, and run-result evidence. Do not start the full matrix or declare Phase 2D.3B.5 complete.

## Control-shell fix diagnostic — 2026-11-05

**Status: offline harness PASS; forward mouse target FAIL; QEMU run INCOMPLETE; Phase 2D.3B.5 INCOMPLETE. Full acceptance readiness: NO.**

- Updated `tests/qemu-graphics.exp` so, after the targeted diagnostic passes, the targeted-diagnostic-only path calls the existing bounded `finish_guest_shutdown` helper, reports `FULL_GRAPHICAL_ACCEPTANCE=NOT_RUN reason=targeted-diagnostic-only`, and returns success. This is intended to shut down QEMU before a successful diagnostic-only return without representing full acceptance. The live diagnostic below failed before reaching this branch.
- After this final harness edit, **Tcl source completeness: PASS** and **offline render-trace/harness regression: PASS**. The latter ended with `SETTINGS_RENDER_TRACE_OFFLINE_REGRESSION=PASS`. The regression exercises the production traced-app command on both success and failure paths, verifies the persistent interactive shell accepts a following command, and checks cleanup/failure accounting. It does not substitute for live guest verification.
- The normal QEMU harness acquired and released the shared lock. Retention scanning took about six minutes. One QEMU diagnostic used one vCPU, 1280×720, automatic renderer, and the existing ISO `/root/.cache/seyrix-linux/phase2d3b5-targeted-gtk-20261104T202043Z/out/seyrix-phase2d3b-2026.11.04-x86_64.iso` (SHA-256 `480d8ab07ec169620934f9c662dbcb7da8fb5bd0d3e2cdc31f389622c45b7ccc`). Evidence is preserved at `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-agent1-control-shell-fix/`.
- Guest boot and control-shell readiness passed in 97,349 ms. The harness verified an active 1280×720 output and captured the initial Appearance page. The mouse-navigation stage then failed closed: GTK reported the intended Desktop row at bounds `20,225,172,34`, and the harness clicked screen position `(106,242)`, but GTK input targeted the Window Management label at bounds `30,225,152,18`. GTK selected the `windows` row and stack page; evidence for the intended Desktop selection was absent. The run reports **mouse stage: FAIL** and **targeted diagnostic: INCOMPLETE, reason=mouse-selection-failed; overall exit 1**.
- Before stopping, the harness preserved Appearance, Desktop baseline, stable Desktop, and mouse-Desktop screenshots. The trace contains 25 client `wl_surface` damage requests, 18 attaches, and 18 commits; compositor logs contain 14 `surface_commit` and 13 `output_present` records. These counts show buffer activity, but do not prove the intended Desktop page transition. Since GTK selected a different category, the fail-closed page-pixel comparison did not run.
- **Traced-app control-shell survival in QEMU: NOT RUN.** This run stopped at mouse selection before the traced-app close action. The production command's shell survival and its follow-on command are verified by the offline Expect regression only. Keyboard navigation in this run is **NOT RUN**; the earlier SMP=1 run's targeted keyboard checks remain recorded in the preceding checkpoint. The full resolution matrix is **NOT RUN**. The diagnostic-only success/shutdown/clean-return branch was not reached, so its QEMU shutdown behavior is **NOT RUN**; only its source completeness and offline regression checks passed.
- The new result does not reproduce the earlier SMP=1 run's complete forward and reverse GTK/Wayland/compositor/page-pixel trace; that earlier run was incomplete after the traced app exited the shell. Rendering reproducibility therefore remains **UNKNOWN**. The GTK row-target/bounds mismatch also remains **UNKNOWN**; no production GTK change is justified, and the forced-Cairo/redraw workarounds remain in place pending evidence.

**Acceptance decision:** the harness has a passing focused offline regression, but **is not ready for the full acceptance campaign**. First resolve and verify the mouse target against the live mapped GTK row, then rerun the bounded diagnostic and confirm control-shell survival after traced-app exit and clean diagnostic completion. Keep full acceptance and Phase 2D.3B.5 **INCOMPLETE**.

## GTK bounds and control-shell retry 4 — 2026-11-05

**Status: focused offline regressions PASS; bounded 1280×720 targeted QEMU diagnostic PASS; full-resolution acceptance NOT RUN; Phase 2D.3B.5 INCOMPLETE.** This checkpoint supersedes the previous diagnostic's failed mouse-target result for the latest run while retaining that earlier failure as evidence of an intermittent input issue.

- The stale click was traced to using row bounds captured at map time: responsive GTK layout subsequently moved the Desktop row. The harness now obtains fresh bounds from the mapped widget after layout. In retry 4, Desktop bounds were `x=20,y=183,w=172,h=34`, and the QMP click at `(106,200)` targeted a GTK `GtkLabel` within the Desktop category (`GtkWindow` pointer `(106.1,200.3)`, label bounds `x=30,y=191,w=152,h=18`). GTK selected the Desktop row and corresponding stack page.
- The input parser also expected the wrong trace field order. Actual GTK input records place `space=GtkWindow button=1 positioned=1` before `x` and `y`; the parser now locates the positioned event coordinates. An earlier retry had a QMP pointer press visible at the compositor but no GTK row selection. The harness now records bounded `evtest` output and gates success on both `BTN_LEFT` down/up and GTK row plus stack selection. That earlier attempt is recorded **FAIL**; bounded retry 4 passed the gate.
- Retry 4 result: **PASS**, exit 0, all targeted stage results PASS. It used the existing diagnostic ISO with SHA-256 `712882605cf678661c835f995307cd7fc1cb9bbd8541b7a0817d20e23392e01a`, one vCPU, 1280×720, UEFI boot, and the graphical control shell. Summary evidence: `/root/.cache/seyrix-linux/history/qemu-graphics-agent1-gtk-bounds-20261105-retry4.summary`. Detailed retry-4 artifacts were rotated into this summary; raw run artifacts remain for retries 1–3.
- **Mouse navigation: PASS.** Appearance → Desktop & Dock GTK selection, stack selection, and mapped-page state passed. The mouse ROI comparison reported `changed_pixels=37625,total_difference=11024419`. **Reverse navigation/rendering: PASS.** Desktop & Dock → Appearance selected and mapped the Appearance page, whose ROI matched exactly (`changed_pixels=0,total_difference=0`). Client Wayland attach/damage/commit and compositor scene commit/presentation records were captured for the transitions.
- **Targeted keyboard navigation: PASS** for the exercised pointer/keyboard category transitions and GTK focus/selection checks. This is targeted diagnostic evidence only and does not constitute the full keyboard or accessibility acceptance campaign.
- **Traced application exit and control shell: PASS.** The corrected `traced_app_exit_command` completed without terminating the persistent guest shell; a live follow-up command read `/etc/os-release` successfully. **Guest shutdown: PASS**, with clean bounded shutdown after the diagnostic-only flow. This verifies the diagnostic success/shutdown path that the preceding mouse-failure run did not reach.
- Both focused offline commands were rerun on 2026-11-05 after the latest edits. **Tcl source completeness: PASS** (`TCL_SOURCE_COMPLETENESS=PASS`). **Serial regression: PASS** (`TRACED_APP_EXIT_COMMAND=PASS control-shell-survival=PASS failure-reporting=PASS cleanup=PASS`). The aggregate **offline render-trace regression: PASS**, ending with `SETTINGS_RENDER_TRACE_OFFLINE_REGRESSION=PASS`.
- Earlier retry evidence remains: retry 1 failed mouse targeting because stale map-time bounds pointed into Window Management; one later retry intermittently delivered the QMP press without a GTK row selection. Retry 4's refreshed mapped bounds and evdev plus GTK gates passed, but a single successful bounded retry does not establish repeatability under broader acceptance load.

**Remaining scope and risk:** the full four-resolution acceptance matrix is **NOT RUN**. Mouse/keyboard behavior beyond the targeted transitions, launch/lifecycle acceptance, and repeatability under full acceptance load remain **NOT RUN**. The successful run used one vCPU and one 1280×720 configuration; SMP panic causality is still unresolved. Retain the Cairo/redraw changes pending further evidence. The harness is ready for the full acceptance campaign, but **Phase 2D.3B.5 remains INCOMPLETE** and no phase completion is claimed.

## GTK bounds and control-shell retry 5 — 2026-11-05

**Status: second consecutive targeted 1280×720 QEMU diagnostic PASS after retry 4; full acceptance NOT RUN; Phase 2D.3B.5 INCOMPLETE.**

- Retry 5 exited 0; every stage passed, including clean guest shutdown. It reused the same diagnostic ISO (SHA-256 `712882605cf678661c835f995307cd7fc1cb9bbd8541b7a0817d20e23392e01a`) at 1280×720 with one vCPU. Summary evidence: `/root/.cache/seyrix-linux/history/qemu-graphics-agent1-gtk-bounds-20261105-retry5.summary`.
- GTK row bounds and click translation passed. The mouse transition to Desktop & Dock passed, followed by reverse pointer navigation to Appearance with exact page-pixel restoration. Keyboard navigation to Desktop & Dock passed with the forward page-pixel change recorded.
- The traced Settings application exited while the persistent control shell survived; a follow-up guest command passed. Clean shutdown completed successfully.
- Retry 5 is the second consecutive targeted QEMU diagnostic PASS following retry 4. The earlier intermittent retry 3, where QMP pointer activity lacked a GTK row selection, remains part of the history and is not erased by these passes.

**Limit:** two consecutive bounded runs at one resolution and one vCPU provide targeted repeatability evidence only. Full-resolution acceptance and broader lifecycle/accessibility coverage remain **NOT RUN**; the SMP panic cause and reliability under full acceptance load remain unresolved. The full acceptance campaign and Phase 2D.3B.5 are **INCOMPLETE**.

## GTK unmap-gate retry 6 — 2026-11-05

**Status: focused offline regressions PASS; third consecutive targeted 1280×720 QEMU diagnostic PASS; broad/full acceptance NOT RUN; Phase 2D.3B.5 INCOMPLETE.**

- Retry 6 exited 0 with every stage and clean shutdown passing. It used the same ISO (SHA-256 `712882605cf678661c835f995307cd7fc1cb9bbd8541b7a0817d20e23392e01a`), one vCPU, and 1280×720. Summary evidence: `/root/.cache/seyrix-linux/history/qemu-graphics-agent1-gtk-bounds-20261105-retry6.summary`. The raw `test.log` was inspected before rotation.
- The harness and regression now gate both outgoing GTK page identities: Appearance must emit `gtk_page_unmap` with `selected=desktop,mapped=0` on Appearance → Desktop & Dock; Desktop must emit `gtk_page_unmap` with `selected=appearance,mapped=0` on Desktop & Dock → Appearance. The return gate also requires the Appearance page to be selected, mapped, and visible.
- Raw retry-6 evidence confirmed the pointer target and coordinate translation: GtkWindow pointer `(106.1,200.3)` picked the Desktop `GtkLabel` at `(30,191,152,18)` within the Desktop sidebar row `(20,183,172,34)`; computed `dx=0.1,dy=0.3,inside=1`. The mouse stage passed. Appearance unmapped with Desktop selected; the reverse stage passed; Desktop unmapped with Appearance selected; and the return Appearance mapped/visible check passed.
- **Page pixels: PASS.** The forward Desktop ROI comparison reported 37,625 changed pixels and total difference 11,024,419. The restored Appearance ROI reported 0 changed pixels and total difference 0. Wayland attach/damage/commit plus compositor surface commits, successful scene commits, and successful output presentations were captured and inspected.
- **Traced-app exit and control shell: PASS.** `WAYLAND_TRACE_APP_EXIT=passed`; live `GUEST_CONTROL_SHELL_FOLLOWUP=passed`; clean guest shutdown passed.
- After tightening the unmap gates, **Tcl source completeness: PASS**; the shell serial regression passed its control-shell survival, failure-reporting, and cleanup checks; and the aggregate render-trace regression ended with `SETTINGS_RENDER_TRACE_OFFLINE_REGRESSION=PASS`.
- Retry 6 is the third consecutive targeted diagnostic PASS after retries 4 and 5. The intermittent retry 3 mouse press without GTK row selection remains part of the history and limits the reproducibility claim.

**Readiness and remaining risk:** the targeted diagnostic harness is ready. Broad/full acceptance, the four-resolution matrix, and wider lifecycle/accessibility coverage remain **NOT RUN**. These three consecutive bounded passes used one ISO, one vCPU, and one resolution; they do not establish behavior under the full acceptance load. The SMP panic cause remains **UNKNOWN**. Phase 2D.3B.5 remains **INCOMPLETE**; no phase completion is claimed.

## Full acceptance resume checkpoint — 2026-11-05

**Status: prerequisites PASS; compact mouse navigation PASS; compact keyboard harness correction under retest; same-process recovery source change UNBUILT; Phase 2D.3B.5 INCOMPLETE.**

- Primary worktree write/readback succeeded. The Agent 1 worktree and its Settings source were readable; it has no root `AGENTS.md`. `archlinux-d-verify` launched, `/root/.cache/seyrix-linux` passed a create/read/remove check, and the shared QEMU lock was acquired normally.
- ISO provenance remains distinct: the Nov 4 image SHA-256 is `480d8ab07ec169620934f9c662dbcb7da8fb5bd0d3e2cdc31f389622c45b7ccc`; the Nov 5 verified image SHA-256 is `712882605cf678661c835f995307cd7fc1cb9bbd8541b7a0817d20e23392e01a`. The Nov 5 image was used for this turn’s diagnostics; the hashes are not interchangeable.
- **Offline regressions: PASS** — Tcl source completeness, render-trace regression, retention validation (14/14), and scoped `git diff --check`. The renderer regression includes the expected deliberate failure fixtures while its aggregate result passes.
- **Run `20261105T204017Z`: INCOMPLETE**, 640×480, two vCPUs, Nov 5 ISO. The compact mouse event reached the compositor and GTK. QMP acknowledged press and release; GTK changed the selector to Desktop; the Desktop stack page became selected, visible, and mapped; Appearance unmapped; and page pixels changed. The run then failed a compact keyboard focus assertion because the harness expected a sidebar row; GTK reported the compact dropdown’s internal `GtkToggleButton` as the focus target. The harness was corrected to check that control identity while retaining the later Window Management transition and visible stack-page gate.
- **Corrected-harness retest: NOT YET RESULTED at this checkpoint.** It is waiting for the normal retention preflight under lock token `20261105T205233Z-1008-24019`. No run ID had yet been allocated when this checkpoint was written. Preserve the earlier failure evidence.
- **Same-process compositor recovery: historical FAIL; current change unverified.** Prior evidence recorded Settings exiting during compositor restart, with recovery only after a new Settings process launched. A working-tree change in `desktop/settings/src/main.c` now separates Settings into its own process group without changing its session ID, then attempts a bounded 30-second GDK Wayland display reopen and GTK window recreation after `GdkDisplay::closed`. It passes a strict syntax-only compile against GTK 4.24.1 headers, but is not yet built or guest-verified. No root cause for the earlier SMP `run_timer_softirq` panic is claimed.
- **Pending mandatory gates:** build and identify a fresh candidate ISO; verify same-PID GDK/Wayland remapping and fresh IPC HELLO/snapshot/subscription; complete all six categories at 640×480, 800×600, 1280×720, and 1920×1080 with GTK identity and mapping evidence; lifecycle, security/IPC and compositor recovery; Weston/TTY/shutdown; sanitizer outcomes; and two independent full end-to-end QEMU sessions. Phase 2D.3B.5 remains **INCOMPLETE**.

## Compact keyboard diagnostic resume — 2026-11-05

**Status: prerequisites PASS; compact mouse transition PASS; compact keyboard transition FAIL; updated key sequence awaits live verification; Phase 2D.3B.5 INCOMPLETE.**

- The primary worktree write probe, Agent 1 worktree/source read, `archlinux-d-verify` launch, evidence-root read/write check, and normal shared QEMU lock acquisition/release all passed.
- ISO provenance was rechecked. The Nov 4 diagnostic ISO is 1,129,283,584 bytes with SHA-256 `480d8ab07ec169620934f9c662dbcb7da8fb5bd0d3e2cdc31f389622c45b7ccc`. The separate Nov 5 verified ISO is also 1,129,283,584 bytes and has SHA-256 `712882605cf678661c835f995307cd7fc1cb9bbd8541b7a0817d20e23392e01a`. They are distinct artifacts.
- Run `20261105T211748Z` is **INCOMPLETE**, exit 1, at 640×480 with two vCPUs and the Nov 5 ISO. Mouse press/release acknowledgements and a fresh compositor pointer event were captured; GTK selected Desktop & Dock, selected and mapped the corresponding stack page, and unmapped Appearance. The mouse page-pixel comparison passed. Keyboard focus on the compact `GtkToggleButton` after Space also passed. The following Down/Enter sequence selected Appearance instead of Window Management, so the keyboard transition gate correctly failed. No keyboard transition pass is claimed. Evidence remains at `/root/.cache/seyrix-linux/qemu-graphics-20261105T211748Z/`.
- The compact diagnostic sequence was changed from Down to Up, and its static regression expectation was updated. This is a testable harness hypothesis based on the recorded GTK result; **live verification is NOT RUN**. Offline render-trace regression and Tcl completeness passed; retention validation passed 14/14; `main.c` GTK syntax compilation and `git diff --check` passed. Git reported line-ending conversion warnings for the report and Tcl harness.
- Same-process compositor recovery source and QEMU assertions remain **UNBUILT / NOT RUN**. The six-category, four-resolution matrix, lifecycle and IPC gates, sanitizer campaign, Weston/TTY checks, and two complete end-to-end sessions remain **NOT RUN**. The SMP `run_timer_softirq+0xce/0xf0` panic remains unexplained. Phase 2D.3B.5 remains **INCOMPLETE**.
## Retention guard and source reconciliation checkpoint — 2026-11-05

**Status: scoped offline regressions PASS; premature temporary ISO promotion recorded and guarded; full acceptance and Phase 2D.3B.5 remain INCOMPLETE.**

- The primary worktree remains at `f492c8c`, modified and writable. Agent 1’s worktree is readable; WSL launches, the evidence root accepted a write probe, and the shared QEMU lock wrapper exited 0. The primary retains the validated Agent 1 GTK/harness integration: `settings-window.c` differs from Agent 1 only by `(void)window`; build-script and serial-regression hashes match Agent 1. The retention change adds a Phase 2D.3B promotion guard and a 15-case regression.
- **Offline verification: PASS** — scoped `git diff --check`; retention validation 15/15; settings render-trace offline regression, including Tcl completeness and persistent guest-shell checks.
- ISO provenance was rechecked. Nov 4 diagnostic ISO: `/root/.cache/seyrix-linux/phase2d3b5-targeted-gtk-20261104T202043Z/out/seyrix-phase2d3b-2026.11.04-x86_64.iso`, SHA-256 `480d8ab07ec169620934f9c662dbcb7da8fb5bd0d3e2cdc31f389622c45b7ccc`. Earlier Nov 5 cache ISO: `/root/.cache/seyrix-linux/verified/seyrix-phase2d3b-2026.11.05-x86_64.iso`, SHA-256 `712882605cf678661c835f995307cd7fc1cb9bbd8541b7a0817d20e23392e01a`. Fresh Nov 5 ISO: `/var/tmp/seyrix-linux/verified/seyrix-phase2d3b-2026.11.05-x86_64.iso`, SHA-256 `e55c9bea86897f9c97f80ec1bcea04e1f5e3410271c1084c07f89381c6bf13b1` (1,129,283,584 bytes). These are separate artifacts.
- The fresh ISO reached the temporary `verified` path prematurely under the prior retention behavior; the build root and raw sanitizer files were also cleaned, limiting post-build sanitizer evidence. This turn added and tested a retention guard: Phase 2D.3B targeted runs without `FULL_GRAPHICAL_ACCEPTANCE=PASS` cannot promote an ISO or clean the build. This guard’s regression is covered by the 15/15 retention suite. No Phase 2D.3B.5 ISO promotion is authorized.
- **NOT RUN / INCOMPLETE:** the full six-category, four-resolution matrix; lifecycle and IPC acceptance; sanitizer rerun against the accepted source candidate; Weston/TTY recovery; same-process compositor-recovery guest verification; and two complete independent end-to-end sessions. The fresh ISO hash does not establish acceptance. Phase 2D.3B.5 remains **INCOMPLETE**.
## Same-process compositor recovery and sanitizer checkpoint — 2026-11-06

**Status: targeted IPC runs INCOMPLETE at the same-PID recovery gate; release tests PASS; Shell ASan suite FAIL; fresh ISO candidate NOT ACCEPTED; Phase 2D.3B.5 INCOMPLETE.**

- Targeted IPC run `qemu-graphics-20261105T222930Z` used ISO SHA-256 `e55c9bea86897f9c97f80ec1bcea04e1f5e3410271c1084c07f89381c6bf13b1`, two vCPUs, and 640×480. Guest boot, initial GTK Settings mapping as UID 1000, IPC HELLO, snapshot, and subscription passed. The compositor stopped and restarted cleanly and the guest shell became ready. The same-PID gate failed because PID 2099 was absent; a relaunched Settings process (PID 6334) passed fallback IPC checks. Guest QEMU shutdown passed. Overall run: **INCOMPLETE** due to same-process recovery failure.
- Instrumented run `qemu-graphics-20261105T224335Z` used the same ISO and profile. Settings started as PID/PGID 2082, SID 578, with `WAYLAND_DISPLAY=wayland-0`. After compositor restart, the display socket name was reused, but socket and shell readiness arrived about 41 seconds after stop. The original PID was gone by the same-PID gate; no coredump was found, and the journal query from the guest user shell returned no useful messages. New-PID fallback IPC and clean guest shutdown passed; same-PID recovery failed, so the run is **INCOMPLETE**.
- These observations support, but do not prove, the hypothesis that the prior 30-second Settings reconnect deadline expired before the compositor became ready. Since PGID equaled the Settings PID, process-group termination is less likely, but is not conclusively excluded. No root cause is claimed.
- The harness now captures PID, PGID, SID, `WAYLAND_DISPLAY`, display sockets, journal output, and coredump status around restart, and fails closed unless the original PID remains. Its offline render-trace/Tcl regression passed. The Settings reconnect deadline was changed from 30 to 120 seconds; focused GTK syntax compilation and the offline regression passed. This source change has not yet been verified by a live guest recovery run.
- Fresh build root: `/var/tmp/seyrix-linux/phase2d3b-20261105T225303Z`. ISO: `/var/tmp/seyrix-linux/phase2d3b-20261105T225303Z/out/seyrix-phase2d3b-2026.11.05-x86_64.iso`, SHA-256 `4f9db1dfd66967bef0e15e42e0ab4d35586b71d7b46131c07f41e99652a40372`, size 1,129,283,584 bytes. Release Meson tests passed: Compositor 3/3, Shell 5/5, Settings 3/3. Page-pixel regression passed 20 cases; desktop-entry line endings and offline trace regression passed.
- ASan results: Compositor 3/3 **PASS**; Settings 3/3 **PASS**; Shell 4/5 **FAIL** on UI rendering. The Shell finding reports 141,332 bytes across 4,817 allocations, rooted in Fontconfig/Pango text rendering. This is an observed sanitizer failure and is not counted as a pass. Raw sanitizer logs were retained.
- The build used `qemu_acceptance_hooks=false`; this image is not established as suitable for the complete QEMU campaign and was not promoted. Full six-category/four-resolution graphical acceptance, lifecycle and complete IPC security acceptance, Weston/TTY recovery, live verification of same-process reconnection, and two independent full end-to-end sessions remain **NOT RUN**. Phase 2D.3B.5 remains **INCOMPLETE**.
## Full acceptance resume — same-process recovery result — 2026-11-06

**Status: targeted two-vCPU IPC run INCOMPLETE; same-process compositor recovery FAIL; close-path correction and rebuilt candidate NOT YET VERIFIED; Phase 2D.3B.5 INCOMPLETE.**

- Access prerequisites were rechecked: the primary worktree was writable; Agent 1 file hashes were readable; `archlinux-d-verify` launched; `/root/.cache/seyrix-linux` was readable and writable, and a `mktemp` write probe succeeded and was removed; `/var/tmp/seyrix-linux` was writable; and the shared-lock probe reported available.
- The tested ISO was `/var/tmp/seyrix-linux/phase2d3b-20261106T001542Z/out/seyrix-phase2d3b-2026.11.06-x86_64.iso`, SHA-256 `a7c186a063ef7f771630f04abc246769cff73b3abe75f14d2f9b18d55d3efc60`. This image predates the close-request change described below.
- Two-vCPU, 640×480 targeted run `qemu-graphics-20261106T061500Z` booted and passed graphical readiness, shell restart recovery, initial real GTK Settings mapping as UID 1000, and initial HELLO/snapshot/subscription. After compositor restart, the original Settings PID 2158 was absent. Its capture contained only `Gdk-Message: Lost connection to Wayland compositor.` The guest-user journal query was denied and reported no coredump. The relaunched GTK Settings PID 2750 mapped and passed fresh HELLO/snapshot/subscription. Settings close and guest shutdown passed. The same-process gate failed; the targeted run and overall acceptance remain **INCOMPLETE**. Evidence: `/var/tmp/seyrix-linux/qemu-graphics-20261106T061500Z/`.
- The run exposed a harness classification bug: the success expression matched both `SETTINGS_IPC_SAME_PROCESS_RECOVERY=passed` and `=failed`, so its substage said PASS while a separate aggregate gate said FAIL. The source now matches only `=passed`, with `=failed` handled by the failure expression. An offline regression assertion covers this correction; the corrected result classification has not yet been live-verified.
- A candidate Settings fix now defers a GTK window-close decision by 250 ms, checks whether the GDK display closed, and only quits for a normal close. This is based on the observed process exit after compositor loss and the current immediate `g_application_quit()` close-request path. It has **not yet been built or live-tested**; the trigger mechanism remains a hypothesis until captured in a new run.
- The optimized same-process harness wait and recovery-result matcher passed the focused offline render-trace/Tcl regression. Earlier retry 3's intermittent mouse activation failure and the kernel `int3` at `run_timer_softirq+0xce/0xf0` remain documented and unresolved.

**Remaining mandatory gates:** rebuild and verify the close-path candidate; complete all six Settings categories at all four resolutions; finish lifecycle, security/IPC, same-process recovery, Weston/TTY, and sanitizer acceptance; and complete two independent full end-to-end QEMU sessions. These are **NOT RUN** or **INCOMPLETE**. No ISO was promoted and no Phase 2D.3B.5 acceptance pass is claimed.
## GTK Wayland hard-exit and acceptance boundary — 2026-11-06

**Status: same-process recovery FAIL on stock GTK 4.24.1; new-process relaunch verified only as a test-harness fallback; Phase 2D.3B.5 INCOMPLETE at an acceptance-contract decision boundary.**

- Targeted run `qemu-graphics-20261106T070000Z` used two vCPUs, 640×480, and `/var/tmp/seyrix-linux/phase2d3b-20261106T053800Z/out/seyrix-phase2d3b-2026.11.06-x86_64.iso` (SHA-256 `3da1c6a620eeac72eb975a5a661c51d4fdaca83e910ebe5b0cfa8ced82463c07`). Boot, graphical readiness, shell executable restart recovery, real Settings mapping as UID 1000 (PID 2164), and initial HELLO/snapshot/subscription passed. After compositor restart, the original PID/start/map/IPC gate failed; the capture records `Gdk-Message: Error flushing display: Broken pipe`, the original process is gone, and no close-callback message was recorded. New PID 2768 mapped and passed fresh HELLO/snapshot/subscription. Settings close and clean guest shutdown passed. Overall run: **INCOMPLETE**. Evidence: `/var/tmp/seyrix-linux/qemu-graphics-20261106T070000Z/`.
- The tested 053800 image contains the 250 ms close-request deferral candidate. It did not preserve the Settings process. The prior `qemu-graphics-20261106T061500Z` run with the immediate-close build also failed the same-process gate. Build metadata identifies commit `f492c8cf44ac4ee44cad485f265d3fbc9c9dc615`, a modified source worktree, GTK 4.24.1, and QEMU hooks/render tracing enabled.
- All retained ISO artifacts were directly rehashed. Their distinct hashes are: Nov 4 diagnostic `480d8ab07ec169620934f9c662dbcb7da8fb5bd0d3e2cdc31f389622c45b7ccc`; Agent 1 retry `712882605cf678661c835f995307cd7fc1cb9bbd8541b7a0817d20e23392e01a`; fresh Nov 5 verified `e55c9bea86897f9c97f80ec1bcea04e1f5e3410271c1084c07f89381c6bf13b1`; Nov 6 001542 `a7c186a063ef7f771630f04abc246769cff73b3abe75f14d2f9b18d55d3efc60`; and Nov 6 053800 `3da1c6a620eeac72eb975a5a661c51d4fdaca83e910ebe5b0cfa8ced82463c07`. Their paths are recorded in this report; the hashes establish separate builds.
- The GTK 4.24.1 Wayland backend source establishes the relevant boundary: on display flush failure and on event-source `G_IO_ERR`/`G_IO_HUP`, `gdkeventsource.c` calls `_exit(1)` directly. That bypasses application-level close and display callbacks, so a Settings-only fix cannot recover that process after this backend path. Source: [GTK 4.24.1 `gdk/wayland/gdkeventsource.c` lines 148–180](https://github.com/GNOME/gtk/blob/4.24.1/gdk/wayland/gdkeventsource.c#L148-L180).
- Release tests for the close-deferral build passed: Compositor 3/3, Shell 5/5, Settings 3/3. ASan/UBSan: Compositor 3/3 **PASS**, Settings 3/3 **PASS**, Shell 4/5 **FAIL** on UI rendering with 141,332 bytes across 4,817 Fontconfig/Pango text-rendering allocations. This remains an unsuppressed sanitizer failure, not an acceptance exception. The previously observed Mesa EGL retention (31,259 bytes across 154 allocations) remains unresolved.
- The current worktree has restored `settings-window.c` to Agent 1's content plus the primary immediate-close-request change (58,994 bytes); the current focused `tests/settings-render-trace-regression.sh` run passed, including the recovery matcher and bounded-log-wait assertions. After restoration, source mtime was 07:00:34 +0100 and the Settings object mtime was 07:01:30 +0100; `ninja` reported no work and `meson test -C .../settings-build --print-errorlogs` passed Settings 3/3. The current staged Settings executable SHA-256 is `e6b19286326afa9f97182b5beb2fdd26b1fc864a4e6005c7cdf456669f27e1fa`, matching the staged executable from the Nov 6 001542 build. This verifies the native Settings suite for the restored source; compositor and shell release suites were not rerun for that source. These results do not change which ISO was used in the failed QEMU run.
- Same-process recovery under stock GTK is **FAIL**. The currently evidenced new-process relaunch was performed by the test harness after compositor readiness, with a new PID, mapped window, and fresh IPC handshake; it does not satisfy the original same-process gate. Completing this gate requires an explicit contract decision: revise it to that supervised-relaunch lifecycle, or authorize and own a maintainable GTK/GDK backend patch or fork. No fallback is silently counted as a same-process pass.

- `git diff --check` exited 0 with CRLF/LF conversion warnings only.

**Still NOT RUN / INCOMPLETE:** the six-category × four-resolution acceptance matrix; broader lifecycle, IPC security, and descriptor-growth gates; Weston/TTY recovery; and two independent complete end-to-end sessions. The earlier intermittent mouse activation failure and SMP kernel `int3` at `run_timer_softirq+0xce/0xf0` remain unresolved. No Phase 2D.3B.5 acceptance pass is claimed.

## Authorized supervised Settings recovery and acceptance checkpoint — 2026-11-06

**Status: revised recovery contract implemented and offline-verified; full Phase 2D.3B.5 acceptance INCOMPLETE.** The explicitly approved lifecycle decision replaces same-process recovery with supervised process relaunch and state restoration. Stock GTK/GDK remains in use; no GTK fork or patch is part of this contract.

- The recovery supervisor implementation includes compositor/display readiness gating, category/window/UI preference restoration, singleton handling, authenticated IPC reconnection with fresh snapshots/subscriptions, bounded retry/backoff and loop protection, and suppression after intentional user closure. Offline native Settings tests passed 3/3; the supervisor policy test passed 1/1. These are offline results and do not substitute for production lifecycle-manager verification.
- Native ASan/UBSan/LSan suites passed, including the Pango/Fontconfig leak fix. Mesa EGL retained resources were reproducibly attributed to wlroots 0.20.2; no Seyrix-owned Mesa leak was confirmed. The dependency attribution does not waive sanitizer acceptance; associated evidence must remain available and unsuppressed.
- Current ISO candidate: `/var/tmp/seyrix-linux/phase2d3b5-candidate-atomic-trace-r2-20261106/out/seyrix-phase2d3b-2026.11.06-x86_64.iso`; SHA-256 `eb767fc65ef3349a9f93067aa84f76a2d8f39eefb02d669f0e50e010a7cef80c`.
- Graphical matrix status: 640×480 passed all six category and profile checks. At 800×600, the IPC socket descriptor loop passed 10 cycles at its exact 23-socket baseline after narrowing the check to sockets; an earlier total-FD mismatch was due to unrelated GTK/shell shared-memory descriptors. The GTK selection changed to Desktop and allocated its page, but QMP pixels still showed Appearance; production rendering is under investigation, so 800×600 remains **INCOMPLETE**. The optional render diagnostic was **INCOMPLETE** because layout-only mode had no Settings process at its precondition; this is not classified as an acceptance failure. The 1280×720 run was active at checkpoint time. 1920×1080 and two independent complete E2E sessions were **NOT RUN**.
- Still required: verify recovery through the real production lifecycle manager, preserving evidence that the old process exited and the new process recovered; finish the four-resolution/six-category graphical checks; finish lifecycle and security checks; verify Weston/TTY recovery; and complete two independent full end-to-end QEMU sessions. Do not use test-harness-controlled relaunch as production lifecycle evidence.

The authorized contract change does not relax any other acceptance criterion. Phase 2D.3B.5 remains **INCOMPLETE** until all mandatory gates pass. No final PASS is claimed.

## Interim verification refresh — 2026-11-07

**Status: sanitizer Shell suite PASS; offline checks and harness update are scoped results only; live QEMU acceptance remains INCOMPLETE / NOT RUN.** This entry supersedes earlier checkpoint statements about outstanding Fontconfig/Pango sanitizer errors and the prior expectation of same-process recovery. Earlier run records remain historical evidence of those earlier outcomes.

- **Shell sanitizers: PASS, 5/5.** Fresh integrated run at `/var/tmp/seyrix-linux/phase2d3b5-sanitizer-integ-20261107-01/`; LSan enabled, no sanitizer errors or suppressions. Evidence includes `commands.txt`, `run-metadata.txt`, `build.log`, `test.log`, and `shell-build/meson-logs/testlog.txt`.
- Preserved Mesa EGL lifecycle evidence is under `docs/evidence/phase2c2/egl-lifecycle/`. This evidence was **not rerun on the integrated source**; it remains separately attributed to wlroots 0.20.2 and does not establish integrated-source Mesa acceptance.
- The authorized Settings recovery design remains supervised relaunch with state restoration on stock GTK/GDK. Offline Settings native tests (3/3) and supervisor policy test (1/1) were reported passed in the prior checkpoint. They do not establish recovery through the real production lifecycle manager.
- The QMP test harness now waits 100 ms after pointer motion and before mouse button-down (retaining the existing 100 ms press hold). The targeted render probe records the launcher map count, clicks the panel, captures `settings-render-launcher-menu.ppm`, and requires a newly mapped `seyrix-launcher` layer before clicking the Settings row; it exits early when that precondition is not met. Offline checks **PASS**: `bash tests/settings-render-trace-regression.sh`, `bash -n` on the harness, Tcl completeness, and scoped diff-check. Retry 7 evidence at `/var/tmp/seyrix-linux/qemu-graphics-2d3b5-render-trace-1280-retry7-smp1-20261107/` showed the launcher remained closed. The new delay has **NOT been tested live**, so this probe is **INCOMPLETE**, not an acceptance failure or pass.
- Live production-lifecycle compositor-restart recovery, six-category × four-resolution graphical acceptance, lifecycle/security checks, Weston/TTY recovery, and two independent complete QEMU sessions are **INCOMPLETE or NOT RUN**. No overall PASS is claimed.

## Supervised recovery 640×480 attempt — 2026-11-07

**Status: six-category visual checks and several lifecycle/security gates passed; Settings reopen cycle 1 failed due to a harness input omission; same-ISO retry IN PROGRESS; Phase 2D.3B.5 INCOMPLETE.**

- Candidate ISO SHA-256: `be388da1663f65f4b5e1657ab890e0e6c5f3882c2c8b9e5913b46c76771f4090`.
- Run `/var/tmp/seyrix-linux/qemu-graphics-phase2d3b5-supervised-640x480-20261107T002500Z/` reached the full six-category GTK matrix and passed all page pixel-difference checks, singleton validation, cross-session-ID denial, IPC/reconnect checks, and ten descriptor cycles. It then failed at Settings reopen cycle 1.
- Retained evidence and source inspection show the harness opened the launcher and typed “Settings” but omitted Return. Deliberate-close suppression passed. This establishes a harness trigger omission for that reopen failure; it does not establish successful reopen or production supervisor recovery.
- The harness was corrected to send Enter before the reopen check. Its offline ordering regression passed. Live verification of that correction is **NOT YET AVAILABLE**.
- Same-ISO retry is in progress at `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-phase2d3b5-supervised-640x480-reopen-retry1/`. No result is recorded here pending completion.

The 640×480 category/pixel subchecks are **PASS** for this run, while the run as a whole and Settings reopen gate are **INCOMPLETE/FAIL at the harness step**. Other resolutions, full production lifecycle recovery, remaining lifecycle/security checks, Weston/TTY recovery, and two independent full E2E sessions remain **INCOMPLETE or NOT RUN**. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

## Supervised recovery popup gate — 2026-11-07

**Status: offline lifecycle checks PASS; popup diagnostic QEMU run INCOMPLETE at a host trace parser mismatch; same-candidate retry IN PROGRESS; Phase 2D.3B.5 INCOMPLETE.** The approved contract remains supervised relaunch on stock GTK/GDK. Production source and the ISO candidate are unchanged in this checkpoint.

- Candidate ISO: `/var/tmp/seyrix-linux/candidates/seyrix-phase2d3b5-supervised-final-20261107T015000Z.iso`; SHA-256 `be6b730b6f338ac213350306faa4dd18b6a29b0c4d27c1e88e91f645bdd75696`.
- Fresh integrated Shell ASan/UBSan/LSan suite: **PASS 5/5**, no findings. A standalone Fontconfig/Pango control still retains 30,465 bytes across 916 allocations; this control result is distinct from the clean current Shell suite. Mesa EGL retention remains unsuppressed and attributed by preserved evidence to wlroots/Mesa teardown; this does not clear the compositor's full sanitizer acceptance.
- Offline lifecycle evidence: supervisor tests **PASS 3/3**; session-lock and socket-replacement checks passed; supervisor control test completed 32 PINGs and restored the descriptor baseline. Recovery helper verified successful status `0` for both snapshot opcode `2` and subscription opcode `3`; retry schedule is 1/2/4/8/16 seconds; manager PGID is distinct from the managed process group.
- The `640x480-popup-gate` diagnostic booted and mapped real manager-launched GTK Settings. First-frame and pixel-delta checks passed. The run stopped **INCOMPLETE** because the host trace parser expected Wayland object IDs prefixed with `@`. Preserved trace evidence establishes that the click opened `xdg_popup#27`, received configure `(10,173,620,228)`, was acknowledged, and committed/attached a buffer on surface `#24`.
- The host parser now accepts both `@` and `#` object-ID forms. A synthetic pass/missing-commit test and replay against the preserved real trace both passed. These validate parsing only, not the live popup gate.
- Same-candidate QEMU retry `...640x480-popup-gate-retry1` is **IN PROGRESS**. Its result is pending and is not reported here.

The popup diagnostic and its parser correction do not complete production lifecycle recovery or the graphical acceptance matrix. Other resolution profiles, remaining lifecycle/security checks, Weston/TTY recovery, and two independent complete E2E sessions remain **INCOMPLETE or NOT RUN**. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

## Popup-gate retry result — 2026-11-07

**Status: targeted popup-gate evidence PASS; run exit status 0 but aggregate result INCOMPLETE by diagnostic design; production supervisor recovery QEMU run IN PROGRESS; Phase 2D.3B.5 INCOMPLETE.** Candidate hash and path are unchanged from the preceding checkpoint.

- Run `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-phase2d3b5-integrated-640x480-popup-gate-retry1` exited with status 0. It was a targeted diagnostic and does not count as full graphical acceptance.
- **PASS:** manager-launched GTK Settings mapped; first-frame and pixel-delta checks passed; `xdg_popup#27` configured at `(10,173,620,228)`, was acknowledged, and surface `#24` attached/committed a buffer. QMP clicked the popup-derived Desktop row at `(320,230)`. GTK selected the category/page, Desktop & Dock mapped, Appearance unmapped, and render-pixel comparison passed. Compact keyboard focus and activation passed. GTK process exit, persistent guest control shell, clean shutdown, shell startup recovery, and UEFI/graphical readiness passed.
- This run did **not** execute the full six-category matrix or production Settings compositor-loss recovery. A dedicated real production supervisor recovery run, `...supervised-640x480-recovery-acceptance`, is **IN PROGRESS**; no result is claimed.

The targeted popup-gate items above are **PASS** only within this diagnostic run. Production lifecycle recovery, remaining graphical profiles and categories, remaining lifecycle/security gates, Weston/TTY recovery, and two independent full E2E sessions remain **INCOMPLETE or NOT RUN**. Phase 2D.3B.5 remains **INCOMPLETE**.

## Recovery and 800×600 rendering follow-up — 2026-11-07

**Status: production IPC/recovery run INCOMPLETE before reaching recovery; 800×600 full run and render diagnostic INCOMPLETE; diagnostic retry IN PROGRESS; Phase 2D.3B.5 INCOMPLETE.**

- The 640×480 production IPC/recovery run stopped **before recovery** because compact category input timed out. Evidence: `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-phase2d3b5-supervised-640x480-recovery-acceptance-retry1/`. No compositor-loss recovery result is claimed from this run.
- The 800×600 full run reached GTK category selection, page mapping, and bounds checks for Desktop & Dock, but the screenshot still showed Appearance with zero changed pixels. The run is **INCOMPLETE** at the rendering gate. Evidence: `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-phase2d3b5-full-800x600-recovery-retry1/`.
- The separate 800×600 render diagnostic is **INCOMPLETE**: QMP pointer input reached the GTK Desktop label, but row activation failed. Evidence: `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-phase2d3b5-render-diagnostic-800x600-retry1/`.
- A retry with the evtest capture fix is **IN PROGRESS**. Its outcome is pending and not reported here.

These runs do not establish the Settings compositor-loss recovery contract or a successful 800×600 Desktop render. Remaining graphical profiles/categories, lifecycle and security gates, Weston/TTY recovery, and two independent full end-to-end sessions remain **INCOMPLETE or NOT RUN**. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

## Input diagnostic and supervised-recovery continuation — 2026-11-07

**Status: 800×600 render diagnostic retry INCOMPLETE; 640×480 recovery attempt stopped before manager recovery; scoped Mesa LSan investigation and local fix PASS but integrated candidate verification NOT RUN; Phase 2D.3B.5 INCOMPLETE.**

- 800×600 render diagnostic retry 2: GTK picked the intended label on pointer press, but no row selection or page transition followed. Guest-side `evtest` capture failed with `Permission denied`. No root cause is established and no navigation/rendering PASS is claimed. This does not contradict the separate QMP event delivery evidence; the input path remains under investigation.
- An opt-in GDK button-release trace and its offline regression passed. This diagnostic change is **not included in the current ISO**, so no live result for that trace is claimed.
- 640×480 supervised recovery retry evidence is at `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-20261107T043940Z/`. Shell fault recovery and IPC startup passed; QMP press/release acknowledgements passed; popup configure/ack/attach/commit passed; Input category selection and state save passed. The run ended **INCOMPLETE before manager recovery** because the harness issued `exit 0`, terminating its guest control shell. The harness correction has been assigned and is **NOT YET VERIFIED**. This run does not verify production supervisor relaunch or restoration.
- Mesa LSan investigation reproduced the exact wlroots GLES2 teardown path: one cycle retained 31,259 bytes in 154 allocations; five cycles retained 156,295 bytes in 770 allocations. In the local worktree, guarded app-level `eglTerminate` plus a five-cycle LSan test passed. The source changed and was built locally, but the integrated candidate and QEMU sanitizer behavior are **NOT YET VERIFIED**. The current ISO remains the prior candidate (`be6b730b6f338ac213350306faa4dd18b6a29b0c4d27c1e88e91f645bdd75696`); it does not contain this source change. A new candidate is required before final acceptance.

The updated render diagnostic, corrected production recovery harness, Mesa source change, and rebuilt integrated candidate still require their appropriate verification. Six-category/four-resolution graphical acceptance, production compositor-loss recovery, remaining lifecycle/security gates, Weston/TTY recovery, and two independent full E2E sessions remain **INCOMPLETE or NOT RUN**. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

## Supervised final candidate and sanitizer rerun — 2026-11-07

**Status: candidate build and offline/release/sanitizer suites PASS; candidate not promoted; current QEMU render diagnostic IN PROGRESS; Phase 2D.3B.5 INCOMPLETE.**

- Build root: `/var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z`.
- Candidate ISO: `/var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z/out/seyrix-phase2d3b-2026.11.07-x86_64.iso`; size 1,129,283,584 bytes; SHA-256 `fd729e3a676e024c843e835b2f0526687d6449a8ec511f08540305d663532afe`. It was **not promoted**. This candidate includes the EGL cleanup and GDK release trace.
- Release tests **PASS**: compositor 4/4 (including EGL lifecycle), Shell 5/5, Settings 3/3, supervisor 3/3. Offline desktop-entry checks, 20 page-pixel cases, render-trace regression (press/release gates plus embedded Bash/Tcl), and GDK input-trace regression also passed.
- Sanitizers **PASS after corrected rerun**: Settings ASan/UBSan/LSan 3/3, Shell 5/5, supervisor 3/3, compositor 4/4, no findings or suppressions. The initial compositor sanitizer run failed because the EGL lifecycle test lacked the software-renderer environment. The Meson test environment was corrected/reconfigured and the full compositor sanitizer suite then passed. Preserve the initial failure log/metadata as history; corrected rerun log: `/var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z/metadata/sanitizer-compositor-rerun.log`.
- The `desktop/compositor/meson.build` correction changes test environment only and occurred after the ISO build; it does not change the packaged production binaries. The current ISO contains the production EGL cleanup and GDK release trace.
- Current candidate QEMU render diagnostic is **IN PROGRESS** at `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-20261107T051711Z/`. No result is claimed until that run completes.

These candidate and offline test results do not satisfy the remaining graphical or production lifecycle gates. All graphical matrix, real supervisor compositor-loss recovery, lifecycle/security, Weston/TTY, and two independent full E2E acceptance gates remain **INCOMPLETE or NOT RUN**. Phase 2D.3B.5 remains **INCOMPLETE**.

### Candidate 800×600 render-diagnostic result — 2026-11-07

Run `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-20261107T051711Z/` ended **INCOMPLETE before the render diagnostic**. Launcher mapping passed. The `render_trace_prior_pid_command` check sampled before the launcher child existed and reported the PID missing; afterward supervisor PID 631 logged readiness and launched Settings PID 2218. No mouse activation, GTK category transition, or render result was produced. This is a harness timing failure, not an application failure and not an acceptance pass. Recovery-supervisor harness wait correction is assigned and **NOT YET VERIFIED**. Candidate remains SHA-256 `fd729e3a676e024c843e835b2f0526687d6449a8ec511f08540305d663532afe`. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

### Current-candidate 800×600 mouse-transition result — 2026-11-07

Run `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-20261107T052920Z/` (retained `test.log`; exec exit 1) passed boot/startup, shell startup failure recovery, launcher and manager-launched Settings mapping, first frame/visible render, and GTK baseline checks. Raw QMP press/release and GTK button-press/button-release both passed. The tested QMP coordinates resolved to a `GtkLabel` within the Desktop category target bounds. However, `selected_category` remained `appearance`; no row activation or page transition followed. `SETTINGS_MOUSE_TRANSITION=failed`; the targeted render diagnostic is **INCOMPLETE**, reason `mouse-selection-failed`. This evidence distinguishes event delivery and target hit from successful category navigation. The run is not the full matrix or full acceptance campaign. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

### GTK mouse-navigation fallback — pending packaged verification

A GTK mouse fallback has since been added in `desktop/settings/src/settings-window.c` to address the observed raw button release reaching the target label without native `GtkListBox` row activation. The source regression `tests/settings-input-trace-regression.sh` passed all 6 assertions; Bash syntax and scoped diff checks passed. This is **offline/source-only verification**: no build or QEMU run has verified the change. A packaged candidate and real guest retest are required before mouse navigation can be accepted. Preserve the 052920 failure evidence above; do not treat the fallback as a fix until live verification passes. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

### Current-candidate 640×480 recovery precheck — 2026-11-07

Run `qemu-graphics-20261107T053927Z` is **INCOMPLETE** before compositor crash/relaunch. Boot and readiness, shell recovery precheck, manager-launched GTK Settings, authenticated HELLO/snapshot/subscription, compact category selection, and state-save checks all passed. The `settings-recovery-baseline` command timed out after 15 seconds; the guest control shell was unavailable at the following step. Compositor crash and Settings relaunch were therefore **NOT RUN**. The timeout is an unresolved harness/guest-command issue under investigation; it is not evidence of a supervisor failure or a recovery pass. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

### Current-candidate 640×480 recovery-baseline follow-up — 2026-11-07

Run `qemu-graphics-20261107T055421Z` recorded GTK mapping, authenticated IPC, category selection, and state save as passed. The recovery baseline timed out after 45 seconds and emitted `settings-state-window-fields-invalid`. The harness nevertheless killed the compositor despite the missing baseline; recovery verification was **NOT COMPLETED**, and the overall run is **INCOMPLETE**. Do not infer supervisor failure or recovery success.

The confirmed harness cause is that its state parser used `sed '$d'` on a sole `[window]` group, removing the final serialized setting (`maximized`) and causing all 40 polling attempts to fail. Harness correction and regression are **IN PROGRESS**. Preserve this run's evidence; keep the recovery acceptance gate unchecked until a corrected live run completes the recovery checks.

### Recovery harness parser correction — offline verification only

The recovery state parser now filters section headers while preserving the final key in a `[window]` group. Bounded diagnostics were added; a missing/invalid baseline now aborts before compositor termination, and helper errors are matched promptly. Offline regression covers `[window]` as the last group and `[window]` followed by another group. The full regression, Bash syntax check, Tcl completeness check, and scoped diff check **PASS** per the recovery agent. No QEMU rerun has occurred: production recovery remains **UNVERIFIED**, and the recovery acceptance gate stays unchecked. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

### Production recovery QEMU follow-up — 2026-11-07

Run `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-20261107T061010Z/` is **INCOMPLETE**. The real production Settings process (PID 2201) baseline was captured: manager PID 633, compositor PID 637, category `input`, width 720, height 560, maximized `1`, and paned position `160`. Compositor PID 637 was killed. Manager recovery verification then timed out after 100 seconds without a result marker. This does not establish either supervisor failure or successful recovery; helper-performance versus production-lifecycle behavior remains under diagnosis. Preserve all run evidence. Production recovery remains **UNVERIFIED** and the overall phase remains **INCOMPLETE**.

### Production recovery QEMU follow-up — 2026-11-07, 062816Z

Run `qemu-graphics-20261107T062816Z` passed input-category selection and baseline capture: manager PID 634, old Settings PID 2218, compositor PID 638, category `input`, 720×560, maximized `1`, paned position `160`. After compositor restart as PID 2755, the retained timeout line showed one `seyrix-settings` process (PID 2847) alongside the unchanged manager PID 634. The final supervisor recovery record was not emitted, so parent/manager launch ownership is unverified. The helper timed out after collecting only 9 filtered lifecycle/IPC lines; ownership, final window mapping, and fresh IPC remain unverified. The run is **INCOMPLETE**, not a recovery PASS. It remains unclear whether app activation/IPC stalled or the harness stream missed events. Preserve evidence and keep production recovery gates unchecked.

### Production manager recovery QEMU evidence — 2026-11-07, 064652Z

Run `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-20261107T064652Z/` used the older candidate SHA-256 `fd729e3a676e024c843e835b2f0526687d6449a8ec511f08540305d663532afe` at 640×480. Baseline: manager PID 629, Settings PID 2187, compositor PID 633, category `input`, 720×560, maximized `1`, paned position `160`. Compositor SIGKILL passed; old Settings PID 2187 exited with status 1. Production manager PID 629 authenticated to restarted compositor PID 2725, logged Wayland readiness, and launched attempt 2 as child PID 2823. Process tree evidence confirms PPID 629; singleton count was 1 and the Settings socket was present.

This proves a supervised child launch after readiness, but not a recovered Settings session: by 150 seconds there was no app-start record, accepted endpoint peer, HELLO/snapshot/subscription, or mapped surface. The stage timed out at 200 seconds and the run is **INCOMPLETE**. Do not claim recovery success; investigate the child startup/IPC path while preserving this evidence.

## Usage-conservation checkpoint — 2026-11-07

**Overall: Phase 2D.3B.5 INCOMPLETE.** The approved Settings recovery contract remains supervised relaunch with state restoration on stock GTK/GDK. Preserve prior evidence and historical run outcomes; this checkpoint summarizes current gate state and the next bounded campaign.

### Latest 064652Z reproduction and result

- Reproduction invocation recorded by `qemu-lock-owner.txt`: `tests/qemu-graphics.sh /var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z/out/seyrix-phase2d3b-2026.11.07-x86_64.iso`. Run metadata: `SEYRIX_QEMU_WORKFLOW=phase2d3b`, `SEYRIX_QEMU_SETTINGS_IPC_ONLY=1`, `SEYRIX_QEMU_RESOLUTION_PROFILE=640x480`; readiness-only, layout-only, render diagnostic, and sanitizer-only modes were 0; 2 vCPUs, 4 GiB RAM, q35/TCG, GTK display with GL off, USB keyboard/tablet. The full QEMU argv is retained in `/var/tmp/seyrix-linux-acceptance-phase2d3b5-20261107/qemu-graphics-20261107T064652Z/qemu-command.txt`; command, metadata, logs, screenshots, and run result are in that evidence directory.
- Candidate ISO SHA-256 was `fd729e3a676e024c843e835b2f0526687d6449a8ec511f08540305d663532afe`. It predates the GTK mouse fallback; there is no newer candidate containing that fallback.
- Before recovery, baseline and initial authenticated HELLO/snapshot/subscription passed. Compositor SIGKILL, old Settings exit, manager authentication/readiness, attempt-2 child PID/PPID, singleton count, and socket presence were observed. **INCOMPLETE:** at 150 seconds no app-start, accepted endpoint peer, HELLO/snapshot/subscription, or mapped surface was observed; the stage timed out at 200 seconds. This is not recovery PASS. Follow-up investigation does not yet establish whether the child stalls during GApplication registration/window construction or harness logs miss startup events.

### Current gate states

| Gate | State | Evidence / limit |
|---|---|---|
| Native release suites | PASS | Candidate build: compositor 4/4, Shell 5/5, Settings 3/3, supervisor 3/3. |
| ASan/UBSan/LSan suites | PASS | Compositor 4/4, Settings 3/3, Shell 5/5, supervisor 3/3; no findings/suppressions. Initial compositor run failed only from missing software-renderer test environment; corrected rerun is `/var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z/metadata/sanitizer-compositor-rerun.log`. |
| Offline Settings/input/recovery regressions | PASS | Mouse input trace: 6 assertions; recovery supervisor/policy and trace/Bash/Tcl regressions passed per subagent results. No build/QEMU verification of the GTK fallback. |
| 640×480 production compositor-loss recovery | INCOMPLETE | Manager launched a child after readiness, but no app-start, post-restart endpoint peer/IPC, or mapped surface by timeout. Evidence above. |
| IPC authentication/reconnect | PASS before compositor loss; INCOMPLETE after restart | Initial HELLO/snapshot/subscription succeeded. Fresh post-restart handshake/subscription was not observed. |
| Six categories × four resolutions | NOT RUN as a complete production matrix | Earlier partial runs include a 640×480 category submatrix and failed 800×600 Desktop mouse transition. Current candidate predates mouse fallback. |
| 10 cold boots on current candidate | NOT RUN | Prior 10/10 evidence is from an earlier candidate and does not transfer to current source/candidate. |
| Weston/TTY recovery on current candidate | NOT RUN | No current-candidate result in this checkpoint. |
| Two independent full E2E sessions on current candidate | NOT RUN | No complete sessions recorded. |
| Candidate promotion / Phase 2D.3B.5 PASS | NOT RUN | No promotion; final acceptance remains INCOMPLETE. |

### Parallel workstream outcomes

- **Agent 1, supervisor:** readiness-gated production supervisor and state restoration source are present. Latest QEMU proves manager readiness, child PPID, and singleton only; it does not prove app recovery. Bounded timeout diagnostics and offline regression passed; no production root cause or fix is established.
- **Agent 2, sanitizers/EGL:** Seyrix-owned EGL cleanup is implemented. Corrected software-rendered compositor sanitizer suite 4/4 passed, as did Settings 3/3, Shell 5/5, and supervisor 3/3, without suppressions. The initial environment-only compositor test failure remains preserved as history.
- **Agent 3, GTK:** GTK mouse fallback and its six-assertion input trace regression passed offline, plus Bash syntax/diff checks. No rebuild or QEMU verification of this fallback has occurred; the full six-category/four-resolution production matrix is NOT RUN.
- **Agent 4, lifecycle/IPC:** read-only audit, no source changes or QEMU. Evidence localizes the latest gap before IPC but cannot distinguish GApplication registration from window construction. Smallest next diagnostic: on a bounded retry, capture the child Settings process `wchan`, syscall, and open FDs at the timeout and add ordered startup checkpoints around application registration and window construction before presuming a production fix. Helper log-rescan issue was corrected; no production failure is established by the incomplete traces.

### Bounded next campaign commands (sequential; do not run concurrently)

Use the one shared `tests/qemu-lock.sh` lock and the same verified candidate/source for the whole campaign. The current ISO is not suitable as the final candidate because it predates the mouse fallback; rebuild after source integration before these acceptance runs.

Four-resolution full graphical matrix, one profile at a time (each harness invocation owns one bounded QEMU session and records fresh evidence):

```bash
iso=/path/to/rebuilt/seyrix-phase2d3b-candidate.iso
for profile in 640x480 800x600 1280x720 1920x1080; do
  SEYRIX_QEMU_WORKFLOW=phase2d3b SEYRIX_QEMU_RESOLUTION_PROFILE="$profile" \
    tests/qemu-graphics.sh "$iso" || exit $?
done
```

Ten cold-boot readiness campaign (script performs ten sequential 640×480 boots and requires clean shutdown for each):

```bash
tests/qemu-graphical-readiness-campaign.sh "$iso"
```

Two independent full end-to-end sessions, sequential through the shared lock:

```bash
for session in 1 2; do
  SEYRIX_QEMU_WORKFLOW=phase2d3b SEYRIX_QEMU_RESOLUTION_PROFILE=640x480 \
    tests/qemu-graphics.sh "$iso" || exit $?
done
```

Relevant offline regressions and sanitizer reruns (using the current integrated build directories and the software-renderer environment configured for the compositor lifecycle test):

```bash
bash tests/settings-input-trace-regression.sh
bash tests/settings-render-trace-regression.sh
meson test -C /var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z/compositor-sanitizer-build --print-errorlogs
meson test -C /var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z/settings-sanitizer-build --print-errorlogs
meson test -C /var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z/shell-sanitizer-build --print-errorlogs
meson test -C /var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z/session-supervisor-sanitizer-build --print-errorlogs
```

These are the documented bounded next actions, not completed work. No command above was run for this checkpoint. Keep all remaining acceptance gates open and Phase 2D.3B.5 **INCOMPLETE**.

### Recovery-contract gate clarification

For the 064652Z run, old Settings PID 2187 exited with status 1. The manager launched one child and singleton count was 1 (**partial singleton evidence only**); restored category/window state was **NOT VERIFIED**. Retry/backoff and restart-loop protection, intentional-close suppression, and full close/reopen behavior were **NOT RUN** in this live recovery attempt. The offline supervisor policy test remains **PASS** but does not replace these production lifecycle checks. The full Weston/TTY recovery gate is **NOT RUN**; a limited TTY marker may have appeared before the recovery stage and is not acceptance evidence.

Documentation subagent outcome: report/checklist update only; the initial scoped diff check exposed CRLF endings, so the main agent normalized these two Markdown files to LF and the repeat `git diff --check` exited 0. No tests, builds, or QEMU runs were performed by the documentation subagent.

### Active diagnostic closure — 2026-11-07

The 064652Z QEMU process is terminal (`run-result.txt`: `INCOMPLETE`, exit status 1); no QEMU system process remained at checkpoint. This diagnosis is **INCONCLUSIVE**, not a production recovery PASS or confirmed product defect. The supervisor completed its compositor/Wayland readiness check and launched child PID 2823 under manager PID 629, but the retained run lacks the child wait channel, syscall, FD targets, and full client-stderr trace needed to locate the stall. The helper's incremental stream filters `SEYRIX_SETTINGS_TRACE`, so its missing app-start marker cannot establish that the client emitted no trace.

Source review places the unobserved boundary before Settings IPC: `on_activate()` constructs the Settings window before scheduling the IPC client; `event=start` is logged only after construction, client start, present, and state save. The present evidence cannot distinguish GApplication registration from GTK/window construction. No production fix is supported yet. Before a future recovery rerun, capture the child's `wchan`, syscall, FD targets, and bounded raw `source=client-stderr` lines at timeout; add startup checkpoints only if those observations still leave the boundary unclear. This diagnostic was not implemented or rerun in this checkpoint. No QEMU, build, or tests were started; the full acceptance objective remains active and incomplete.

### Bounded recovery observability follow-up — 2026-11-07

**Result: production recovery INCOMPLETE; child startup and mapping PASS; fresh Settings IPC NOT OBSERVED; no root cause or production fix established.**

Run `/var/tmp/seyrix-linux/qemu-graphics-20261107T170517Z/` used the preserved candidate ISO (SHA-256 `fd729e3a676e024c843e835b2f0526687d6449a8ec511f08540305d663532afe`) under shared QEMU lock token `20261107T170514Z-288-1229`. Reproduction:

```bash
SEYRIX_QEMU_SETTINGS_IPC_ONLY=1 SEYRIX_QEMU_WORKFLOW=phase2d3b SEYRIX_QEMU_RESOLUTION_PROFILE=640x480 \
  tests/qemu-graphics.sh /var/tmp/seyrix-linux/phase2d3b5-supervised-final-20261107T045300Z/out/seyrix-phase2d3b-2026.11.07-x86_64.iso
```

Boot, initial production Settings mapping and HELLO/snapshot/subscription, compact Input-category selection, saved state, and compositor SIGKILL **PASS**. The old Settings PID 2136 exited with status 1. Manager PID 636 recovered compositor PID 2634 and launched child PID 2722 as attempt 2; the child had PPID 636 and was the sole Settings process. The child logged `event=start` with restored Input state (720×560, maximized), and the compositor logged its System Settings window mapped. Ongoing Wayland frame, attach, and commit traffic appears in captured child stderr. This run demonstrates startup and mapping after recovery; earlier missing signals remain inconclusive.

No endpoint accepted-peer, frame, or response was observed for child PID 2722 after recovery. Fresh Settings IPC therefore remains **INCOMPLETE**. In this run, the child did start and map, so it was not stalled before GTK activation or window mapping; the unresolved boundary is later in the Settings IPC connection/negotiation path. Earlier missing startup/map observations remain inconclusive. At timeout, `wchan` was `poll_schedule_timeout.constprop.0`; FD targets were recorded (including three socket descriptors). Reading `/proc/2722/syscall` returned `Operation not permitted`, so syscall is **NOT CAPTURED**. The run helper exited after 151,054 ms; recovery verification returned **FAIL**, and the overall QEMU result is **INCOMPLETE**. Evidence does not yet establish why the client failed to negotiate. No production code fix was made.

The timeout helper now records child wait channel, syscall-read status, FD targets, and up to 80 PID-filtered `source=client-stderr` lines. Bash syntax check **PASS**; live diagnostic capture produced the evidence above. Do not continue acceptance QEMU runs until recovery and fresh IPC pass. Resume by mapping the captured socket descriptors to the Wayland and Settings endpoints, resolving the client connection/negotiation boundary, and running focused IPC regressions before another bounded recovery attempt. Overall Phase 2D.3B.5 remains **INCOMPLETE**.

### IPC connection/negotiation follow-up — 2026-11-07

**Result: connection attempt and compositor acceptance PASS; HELLO and fresh IPC INCOMPLETE; no protocol rejection, production root cause, or production fix established.**

One bounded 640×480 retest was run in `/var/tmp/seyrix-linux/qemu-graphics-20261107T172718Z/` with the preserved candidate ISO (SHA-256 `fd729e3a676e024c843e835b2f0526687d6449a8ec511f08540305d663532afe`). Boot, saved Input baseline, compositor SIGKILL, manager readiness, child PID 2732 launch under manager PID 638, restored Input state (720×560, maximized), and window mapping **PASS**. Positive evidence shows the compositor accepted peer UID 1000, PID 2732, session 582. This proves the replacement attempted and successfully connected to the Settings endpoint; the failure was not a missing or failed connection attempt.

The 151,220 ms recovery stage ended without a Settings frame (including HELLO), snapshot, subscription, protocol rejection, or peer-close event for PID 2732. The evidence distinguishes the issue from failed connect, but does not establish whether the client failed to send HELLO or the send/receive observation was lost. No protocol rejection was observed. FDs 5, 7, 8, and 9 were sockets with inodes 11108, 11109, 11144, and 11147. The retained timeout capture **does contain** `ss -xapne` rows: fd 9 is `u_seq ESTAB` with client inode 11147 and peer inode 11148, identifying the Settings `SOCK_SEQPACKET` connection. The compositor-side FD number for inode 11148 was not captured. The earlier PID 2722 descriptor evidence likewise lacks endpoint-to-inode mapping.

Focused offline Settings tests **PASS 3/3** (IPC codec, snapshot model, client reconnect); helper `bash -n` **PASS**. No production change is supported. Recovery, fresh post-restart IPC, and overall Phase 2D.3B.5 remain **INCOMPLETE**; preserve all broader gates and prior evidence.

### Focused IPC handshake observability — 2026-11-07

**Result: targeted diagnostic instrumentation and offline checks PASS; QEMU handshake diagnosis and production recovery remain INCOMPLETE; no production behavior fix or root cause established.**

The prior timeout evidence now identifies fd 9 as the accepted Settings `SOCK_SEQPACKET` endpoint (client inode 11147, peer inode 11148). The row showed zero receive and send queue lengths at timeout. This corrects the earlier report statement that `ss` returned no rows. The compositor had accepted PID 2732, but the captured run still cannot establish whether the client attempted HELLO serialization/write, whether the compositor read it, or whether any event evidence was omitted.

The replacement client now supports optional `SEYRIX_SETTINGS_IPC_TRACE=1` diagnostics. Its stderr records monotonic time, PID, attempt, fd and inode, connect result and peer credentials, HELLO wire bytes, write result/errno, receive results, frame decoding, HELLO response, and snapshot/subscription state. The compositor’s acceptance-hook logs now include monotonic time, connection ID, peer PID, fd and inode, event-source registration, callback dispatch, `recvmsg` results (including EAGAIN), parser outcome, request dispatch, sends, and removal. IPC-only QEMU launch passes the trace flag to the supervised child. The timeout helper emits bounded client and server IPC trace sections and resolves the `u_seq` peer inode to compositor FD numbers by scanning same-session `/proc` entries without privileged access.

Offline verification:

- Settings IPC codec, snapshot model, and client reconnect tests **PASS 3/3**. The reconnect test ran with client IPC tracing enabled and observed successful 28-byte HELLO writes, followed by snapshot and subscription requests and responses on both connections.
- `tests/settings-render-trace-regression.sh` and Bash syntax checks for that script and `tests/qemu-guest-helper.sh` **PASS**.
- An unprivileged AF_UNIX `SOCK_SEQPACKET` `ss`/`/proc` inode-to-peer-FD probe **PASS**.
- Hooks-enabled compositor compile **PASS** in `/var/tmp/seyrix-linux/phase2d3b5-compositor-ipc-CAbFaW`; production hooks-disabled compositor compile **PASS** in `/var/tmp/seyrix-linux/phase2d3b5-compositor-prod-8uuh0R` after correcting an unused-parameter warning found by that build.
- The Settings client source and reconnect test **compiled** in `/var/tmp/seyrix-linux/ipc-check-9PcW8V`. Building the complete `seyrix-settings` target is **INCOMPLETE**: `-Werror` stopped on existing enum-versus-int signedness warnings in `settings-window.c` lines 985 and 1013. Those unrelated warnings were not changed.
- QEMU with the new instrumentation **NOT RUN**. The preserved ISO predates these diagnostics, so it cannot resolve the handshake boundary.

No production protocol behavior changed. The missing post-restart HELLO remains **INCONCLUSIVE**: this evidence does not distinguish an absent client send from an event/read instrumentation gap in the old ISO, and it does not prove parser failure. Resume by building a fresh, unpromoted diagnostic ISO with these changes, then run one bounded IPC-only recovery trace. Correlate client monotonic timestamps, PID, attempt, fd/inode and write result with compositor monotonic timestamps, PID, connection ID, fd/inode and read/dispatch result; retain the timeout queue and peer-FD capture. Keep all existing acceptance gates open and Phase 2D.3B.5 **INCOMPLETE**.

### IPC recovery trace follow-up — 2026-11-07

**Result: replacement client HELLO/snapshot/subscription PASS by client trace; supervised compositor-loss recovery gate INCOMPLETE because its captured server stream lacks per-peer records; no gate relaxation or production fix.**

The unpromoted diagnostic candidate was `/var/tmp/seyrix-linux/phase2d3b-ipc-diag-ccwrap-20261107T1952Z/out/seyrix-phase2d3b-2026.11.07-x86_64.iso` (SHA-256 `d81960964891b0fd98e28dd2caae3648cbb2ee305b00ab4196179d953eb7fefd`). Diagnostic image build **PASS** with the acceptance hooks and render tracing enabled. Existing `settings-window.c` enum-versus-int `-Werror=sign-compare` warnings were demoted only through the task-local compiler wrapper (`-Wno-error=sign-compare`); source was not changed for these warnings. Build-time release suites **PASS**: compositor 4/4, Shell 5/5, Settings 3/3, supervisor 3/3. The ISO remains unpromoted. The first run, `/var/tmp/seyrix-linux/qemu-graphics-20261107T200320Z/`, ended **INCOMPLETE** before compositor startup: the captured TTY screenshot shows QMP input mangled the long `start-seyrix` command, and there was no compositor process, endpoint, or log. The IPC-only launcher was then changed to write `/tmp/seyrix-ipc-launch` over the serial channel and type `bash /tmp/seyrix-ipc-launch`; its regression and Bash/Tcl checks **PASS**.

The second run, `/var/tmp/seyrix-linux/qemu-graphics-20261107T201710Z/`, used the same ISO and ended **INCOMPLETE** (exit status 1) at the supervised compositor-loss recovery gate; no ISO promotion occurred. Launcher and graphical readiness **PASS**. Initial live Settings PID 2126 (manager 637, compositor 641) completed HELLO, snapshot, and subscription. The compositor trace recorded connection ID 2, fd 71, inode 10786, and successful handling/responses for HELLO opcode 1 request 1, GET_STATE opcode 2 request 2, and SUBSCRIBE opcode 3 request 3.

After compositor PID 641 was killed, manager PID 637 recovered compositor PID 2606 and launched replacement Settings PID 2692 as attempt 2. The replacement restored Input state (720×560, maximized), mapped its window, and connected on client fd 9/inode 11627; `SO_PEERCRED` identified compositor PID 2606, UID 1000. Client tracing confirmed HELLO serialization (28 bytes; wire hex `53595258010000010000000100000004000000000000000001010000`) and a complete 28/28-byte write with errno 0. It then received and decoded a valid 30-byte HELLO response (error 0, negotiated), sent the 24-byte snapshot request and decoded the 260-byte response (revision 9, error 0), then sent the 24-byte subscription request and decoded its 260-byte response (revision 9, subscribed). Thus, for PID 2692, an absent HELLO, failed client write, unread request, and protocol-parser failure are ruled out by the client’s successful decoded responses.

Unprivileged `ss`/`/proc` capture mapped the connection’s peer inode: Settings fd 9/inode 11627, compositor PID 2606, peer inode 11628, compositor fd 73 (also listed as fd 74). However, the recovery verifier’s `fresh` server-log stream contains no accept/frame/send records for PID 2692, so its acceptance marker is blank, HELLO is 0, and snapshot/subscription requests remain missing. It did record the supervisor PID 637’s separate connection (connection ID 1, fd 59, inode 11418) and successful HELLO; that peer does not satisfy the replacement Settings gate. The precise reason for PID 2692’s missing server-side records—logger omission or a stream/capture path gap—remains **UNRESOLVED**. The recovery gate must therefore remain **INCOMPLETE** despite the client-side protocol success.

Offline checks before QEMU: Settings tests **PASS 6/6** (including traced client reconnect), `tests/settings-render-trace-regression.sh` **PASS**, Bash syntax checks for the helper and regression **PASS**, hooks-enabled and production hooks-disabled compositor compiles **PASS**, unprivileged socket peer mapping **PASS**, and scoped `git diff --check` **PASS**. No production fix was made, and the broader acceptance gates remain unchanged.

Resume by tracing why the raw server event stream for PID 2692 is absent from `fresh` despite the successful client responses and peer-FD mapping. Improve bounded server-log capture without weakening its acceptance predicate, run a focused offline regression, then make one bounded IPC-only rerun if needed. Phase 2D.3B.5 remains **INCOMPLETE**.

### IPC observability and focused recovery — 2026-11-07

**Result: initial IPC and supervised relaunch PASS; recovery acceptance INCOMPLETE because one matching compositor subscription record was not captured. No protocol defect was demonstrated. The broader matrix was not started and promotion is not ready.**

The prior `qemu-graphics-20261107T201710Z` evidence for replacement Settings PID 2692 showed successful client-side HELLO, snapshot, and subscription replies, with fd/inode correlation to compositor PID 2606, while the retained compositor stream had no corresponding accepted-peer/frame/send records. Source review confirmed that the compositor hooks are compiled only with `SEYRIX_QEMU_ACCEPTANCE_HOOKS`, log accepted-peer, receive/dispatch/frame, send, and removal paths at `WLR_INFO`, and use `SO_PEERCRED`, session checks, and socket identity. The diagnostic build included the hooks; initial compositor-side records were present. The client decoded successful responses, so no production protocol change was justified.

The shared capture path was also reviewed: `start-seyrix` appends compositor and supervisor stderr to `/tmp/seyrix-compositor.log`; before the latest edit, supervisor `trace_emit_line` wrote each record in separate prefix, payload, and newline stdio operations, while `WAYLAND_DEBUG=client` added high-volume records to the same stream. This made line interleaving/capture loss plausible, but did not prove the precise loss mechanism for every missing server event.

Scoped observability changes were made in `desktop/session-supervisor/main.c`, `tests/qemu-graphics.exp`, `tests/qemu-guest-helper.sh`, and `tests/settings-render-trace-regression.sh`:

- IPC tracing is independent of Wayland protocol tracing. In IPC diagnostic mode, protocol lines beginning with `[` go to a private XDG runtime sidecar (mode 0600; no-follow open, regular-file/owner/link/mode validation before truncation). The compact popup checker reads that sidecar from a checked baseline while the compositor log retains IPC records.
- The recovery stream now retains every `Settings endpoint ` event while keeping its exact accept/frame/send acceptance predicates.
- After the QEMU run described below, supervisor stderr trace records were changed to use one constructed write loop to reduce interleaving with compositor log records. That final writer edit has compile/offline evidence only; the QEMU ISO predates it.

The diagnostic ISO was built before the final atomic-writer edit:

- ISO: `/var/tmp/seyrix-linux/phase2d3b5-ipc-sidecar-20261107T213135Z/out/seyrix-phase2d3b-2026.11.07-x86_64.iso`
- SHA-256: `4326e3a241f8c6edeaed5b3b6f45557cd144185840b204e469d6f0b8e099a582`
- Build command: `CC=/var/tmp/seyrix-linux/phase2d3b-ipc-diag-20261107T1945Z/diagnostic-cc SEYRIX_ARTIFACT_ROOT=/var/tmp/seyrix-linux SEYRIX_BUILD_ROOT=/var/tmp/seyrix-linux/phase2d3b5-ipc-sidecar-20261107T213135Z SEYRIX_QEMU_ACCEPTANCE_HOOKS=1 SEYRIX_QEMU_SETTINGS_RENDER_TRACE=1 ./build/build-phase2d3b-iso.sh`
- Acceptance hooks and render tracing were enabled. Release suites passed: compositor 4/4, Shell 5/5, Settings 3/3, and supervisor 3/3. The build-integrated desktop-entry, QEMU helper/Tcl, Settings render/input regression checks also passed. Existing `settings-window.c` enum/int signedness warnings were demoted only via the task-local compiler wrapper; archiso root-directory and GRUB SBAT warnings were emitted. No ISO promotion occurred.

One bounded IPC-only QEMU run used shared lock token `20261107T214037Z-257-3568` and retained evidence at `/var/tmp/seyrix-linux/qemu-graphics-20261107T214000Z/`. Command:

```bash
SEYRIX_RETENTION_KEEP_EVIDENCE=1 SEYRIX_RETENTION_KEEP_ARTIFACTS=1 \
SEYRIX_QEMU_SETTINGS_IPC_ONLY=1 SEYRIX_QEMU_WORKFLOW=phase2d3b \
SEYRIX_QEMU_RESOLUTION_PROFILE=640x480 \
SEYRIX_QEMU_GRAPHICS_DIR=/var/tmp/seyrix-linux/qemu-graphics-20261107T214000Z \
  tests/qemu-graphics.sh \
  /var/tmp/seyrix-linux/phase2d3b5-ipc-sidecar-20261107T213135Z/out/seyrix-phase2d3b-2026.11.07-x86_64.iso
```

Boot/readiness, the shell restart fixture, initial authenticated Settings PID 2133 HELLO/snapshot/subscription, popup configure/commit via the sidecar, Input-category save, state baseline, and compositor termination **PASS**. The saved pre-recovery state was Input, 720×560, maximized, paned position 160 (manager PID 634, compositor PID 638, Settings PID 2133). The same manager recovered compositor PID 2615 and launched exactly one replacement Settings PID 2704 (PPID 634), which restored state and mapped. Socket correlation identified client fd 9/inode 11898, peer credentials PID 2615/UID 1000, peer inode 11899, compositor fds 71 and 75.

Client diagnostics confirm a full replacement handshake: 28-byte HELLO and valid decoded reply, snapshot request and decoded 260-byte response, then a 24-byte subscription write and decoded 260-byte subscribed response. The compositor capture contains the accepted peer plus HELLO and snapshot frame/send records, but no request-3 subscription frame/send record in the recovery verifier's `fresh` stream. The verifier therefore returned `SETTINGS_SUPERVISOR_RECOVERY=failed` with `subscribe_request=missing`; run result is **INCOMPLETE**, exit status 1. This is evidence of an observability gap, not an IPC protocol failure. The recovery gate did not pass, so no additional QEMU runs or Stage 3 matrix were started.

After preserving the run, the following verification passed against the final atomic-writer source: Bash syntax for the Settings trace regression, QEMU helper, and QEMU driver; `tests/settings-render-trace-regression.sh` (`SETTINGS_RENDER_TRACE_OFFLINE_REGRESSION=PASS`); isolated supervisor build at `/var/tmp/seyrix-linux/phase2d3b5-atomic-trace-compile-20261107T220000Z/session-supervisor-build`; supervisor tests 3/3. One ad hoc Meson command attempt ended before Meson due nested-shell quoting and left a marked failed scratch directory; the corrected isolated build/test command passed.

| Acceptance gate | Current result | Evidence / limit |
|---|---|---|
| Release suites | PASS | 4/4 compositor, 5/5 Shell, 3/3 Settings, 3/3 supervisor on the diagnostic ISO build; supervisor 3/3 rebuilt after the final writer edit. |
| Offline trace/input/Tcl regressions | PASS | Latest render trace regression and Bash syntax checks passed. |
| 640×480 compositor-loss recovery | INCOMPLETE | One replacement restored state and completed client IPC, but its compositor subscription frame/send records were absent from the verifier stream. |
| Six categories × four resolutions | NOT RUN | Focused recovery did not pass. |
| Ten cold boots on current candidate | NOT RUN | No current-candidate campaign. |
| Weston/TTY fallback | NOT RUN | No current-candidate acceptance run. |
| Lifecycle/security/supervisor full acceptance | INCOMPLETE | Several startup/restart checks passed in the focused run; the recovery gate failed and the wider lifecycle/security matrix was not run. |
| Two independent complete E2E sessions | NOT RUN | No complete sessions. |
| Sanitizer suites on current source | NOT RUN | Existing prior results do not include the final supervisor writer edit. |
| Phase 2D.3B.5 promotion readiness | INCOMPLETE | Not ready; no merge, push, or ISO promotion. |

**Resume point:** the final atomic-writer change still needs a fresh diagnostic ISO and one focused recovery run after work resumes. Start the full matrix only if that focused gate passes. Preserve this run and all earlier artifacts; no protocol changes, physical disk writes, merge, push, or promotion occurred.

## Final logging candidate and focused recovery — 2026-11-08

**Status: build PASS; focused recovery gate FAIL/INCOMPLETE; acceptance matrix NOT RUN; Phase 2D.3B.5 INCOMPLETE.**

### Candidate build

- Source revision: branch `codex/phase2d3b5-system-settings`, HEAD `dd0c9231fe576e34d61cfda6f6d73ed83e1a3103`, with the captured working-tree state. The WSL Git diff SHA-256 is `07c4a3f4e3ef3861f8a0549a846a8fe100facfacc131d03eda3dbea363dfdd0d`; the tracked-source manifest SHA-256 is `ca8da13528972409277ce7283e672d865e0b048b862a830000a847c525407b37`. WSL Git showed broader line-ending-visible changes than the Windows Git view; these digests identify the actual source view used for this build and do not imply every line-ending-visible difference was intentionally authored.
- Source snapshot, diff, tracked-file hash list, compiler-wrapper checksum, and build metadata are retained under `/var/tmp/seyrix-linux/phase2d3b5-final-supervisor-log-20261108T080000Z/metadata/`.
- Build command:

  ```bash
  CC=/var/tmp/seyrix-linux/phase2d3b-ipc-diag-20261107T1945Z/diagnostic-cc SEYRIX_ARTIFACT_ROOT=/var/tmp/seyrix-linux SEYRIX_BUILD_ROOT=/var/tmp/seyrix-linux/phase2d3b5-final-supervisor-log-20261108T080000Z SEYRIX_QEMU_ACCEPTANCE_HOOKS=1 SEYRIX_QEMU_SETTINGS_RENDER_TRACE=1 SEYRIX_RETENTION_KEEP_ARTIFACTS=1 /mnt/c/Users/quake/.codex/worktrees/phase2d3b5-system-settings/Seyrix-Linux/build/build-phase2d3b-iso.sh
  ```

- Candidate ISO: `/var/tmp/seyrix-linux/phase2d3b5-final-supervisor-log-20261108T080000Z/out/seyrix-phase2d3b-2026.11.08-x86_64.iso`; 1,129,283,584 bytes; SHA-256 `227fb358fb380b4816fdb0a09fb33be7dde0ce1c4181aa61c3d0bf586d09a003`. The ISO checksum is also in `metadata/iso.sha256`. The candidate was not promoted.
- Release suites **PASS**: compositor 4/4, Shell 5/5, Settings 3/3, session supervisor 3/3. Build-integrated desktop-entry, 20-case page-helper, Settings render/input trace, and Tcl completeness checks **PASS**. Existing Settings signedness warnings at `settings-window.c:985,1013` were demoted by the task-local compiler wrapper; archiso `.pacnew` and GRUB SBAT warnings were emitted.

### Focused 640×480 QEMU recovery

One IPC-only recovery run used the shared lock `/tmp/seyrix-qemu-acceptance.lock`, token `20261108T081257Z-275-10783`. Evidence is retained at `/var/tmp/seyrix-linux/qemu-graphics-20261108T081240Z/`; `raw-artifacts.sha256` (SHA-256 `b821e57ba452c289f310c0ae6abdeb098fb05dc9ba1e4a154a977386f54e7d9e`) was generated and all listed checksums verified. The raw `test.log` SHA-256 is `ed9621749020002453a6137068b3814e61efb8d41e6099a416f08b05b32c3f8b`, `serial.log` is `f1849742e476deadecfeefeff2adfcdbcc3c46343cbe3f42e099bec57f1f034c`, and `expect-session.log` is `0a9b68afb0921ee6e2e85a6ef7a9d52dfa7f14bab8ebcf79c91e64b814480168`. The command and run metadata are retained in that directory. The run ended with exit status 1 and overall result **INCOMPLETE**; the recovery stage failed after 152,831 ms.

```bash
SEYRIX_RETENTION_KEEP_EVIDENCE=1 SEYRIX_RETENTION_KEEP_ARTIFACTS=1 \
SEYRIX_ARTIFACT_ROOT=/var/tmp/seyrix-linux \
SEYRIX_QEMU_LOCK_FILE=/tmp/seyrix-qemu-acceptance.lock \
SEYRIX_QEMU_SETTINGS_IPC_ONLY=1 SEYRIX_QEMU_WORKFLOW=phase2d3b \
SEYRIX_QEMU_RESOLUTION_PROFILE=640x480 \
SEYRIX_QEMU_GRAPHICS_DIR=/var/tmp/seyrix-linux/qemu-graphics-20261108T081240Z \
  tests/qemu-graphics.sh \
  /var/tmp/seyrix-linux/phase2d3b5-final-supervisor-log-20261108T080000Z/out/seyrix-phase2d3b-2026.11.08-x86_64.iso
```

Boot/readiness, shell fixture recovery, initial Settings IPC, Input selection/state save, and compositor termination **PASS**. Initial Settings PID 2158 completed HELLO, snapshot, and subscription with matching client/server records. The saved state was Input, 720×560, maximized, paned position 160. After compositor PID 643 was killed, the same manager PID 639 restarted the compositor as PID 2643 and launched exactly one replacement Settings PID 2731 (PPID 639). The replacement restored the saved state, mapped its window, and connected to the authenticated endpoint as UID 1000; the compositor identified the peer as PID 2731 on connection ID 2, fd 71, inode 11821.

Client-side trace evidence in `test.log` lines 398–427 (also in the timeout raw-stderr section) proves that PID 2731 completed fresh HELLO, snapshot, and subscription exchanges: request 1 received a valid 30-byte response with error 0; request 2 received a decoded 260-byte snapshot at revision 9; request 3 received a decoded 260-byte subscribed response at revision 9. The compositor capture for this same peer contains accepted-peer, watch-ready, and dispatch (`mask=0x1`) records, but no matching read, frame, request-dispatch, or send records. The recovery verifier therefore reports `hello=0` and missing snapshot/subscription requests in its server stream, and the matching client/server evidence gate **FAILS**. The successful client responses do not establish a product protocol failure; the server-side observation gap remains unresolved. The final single-write supervisor logger change did not close it.

The guest private Wayland trace sidecar `/run/user/1000/seyrix-settings-wayland-trace.log` was not exported into the host evidence directory. Consequently, live sidecar completeness, PID attribution, and truncation checks are **INCOMPLETE / NOT VERIFIED**. Captured host logs and screenshots remain preserved and checksummed. The run-owned QEMU process exited during harness cleanup after the failed stage; `qemu-exit-status.txt` is absent. The shared QEMU lock was released, and no QEMU process remained active.

### Current acceptance decision

| Gate | Result | Evidence / limit |
|---|---|---|
| Fresh candidate build and release suites | PASS | Candidate and metadata above; candidate not promoted. |
| Initial authenticated Settings IPC | PASS | PID 2158 client/server HELLO, snapshot, subscription. |
| Replacement singleton, restored state, and mapped window | PASS | One replacement PID 2731 under unchanged manager PID 639. |
| Replacement client HELLO, snapshot, subscription | PASS (client evidence) | PID 2731 decoded all successful responses. |
| Matching replacement compositor records | FAIL / INCOMPLETE | Server capture stops after accepted-peer, watch-ready, and dispatch; no read/frame/send records. |
| Live Wayland trace sidecar integrity | INCOMPLETE / NOT VERIFIED | Sidecar was not exported to host evidence. |
| Six categories × four resolutions | NOT RUN | Focused recovery gate did not pass. |
| Ten cold boots | NOT RUN | No campaign on this candidate. |
| Weston/TTY fallback | NOT RUN | No current-candidate result. |
| Lifecycle, security, resource growth, supervisor acceptance | INCOMPLETE / NOT RUN | Broader gates were not started. |
| Final-source sanitizer suites | NOT RUN | Not reached after the focused recovery failure. |
| Two independent end-to-end sessions | NOT RUN | Not reached. |
| Promotion readiness | INCOMPLETE | Not ready; no merge, push, or ISO promotion. |

**Final decision: INCOMPLETE.** The focused recovery acceptance did not pass its matching server-evidence and live-sidecar gates. No acceptance matrix or other expensive acceptance campaigns were started. No production or harness source changes were made during this run; no protocol or GTK changes, physical disk writes, merge, push, or promotion occurred.

**Resume point:** retain this candidate and run evidence. Resolve the missing per-peer compositor read/frame/send evidence and export/validate the live Wayland sidecar with explicit completeness and attribution checks, then rerun only the focused 640×480 recovery gate. Start the broader acceptance stages only after that gate passes.

### Emergency migration checkpoint — 2026-11-08

**Status: Phase 2D.3B.5 remains INCOMPLETE.** No builds, tests, or QEMU runs were started for this handoff.

The current diagnostic ISO is `/var/tmp/seyrix-linux/phase2d3b5-evidence-pipeline-20261108T085200Z/out/seyrix-phase2d3b-2026.11.08-x86_64.iso` (SHA-256 `b123438cd1638f9aacb39d61887abebaf359a2a46cd888f94ec8a53cde21c7f9`). Build-time release suites passed: compositor 4/4, Shell 5/5, Settings 3/3, and session supervisor 3/3. The ISO is not included in the migration archive because it is a large, reproducible artifact; its checksum and build metadata are preserved separately.

The final focused 640×480 run is `/var/tmp/seyrix-linux/qemu-graphics-20261108T095000Z/`. Its raw manifest SHA-256 is `a5eda47a4a8ce96275aebfa60c736549ff35dc5a3b1f37ffefd6565b73f3f823`; `sha256sum -c raw-artifacts.sha256` passed for every listed file. The guest sidecars were exported and retained for PID 2707 (216,373 bytes, SHA-256 `04059e6264fc0bced12fc30d07a2a6bc83039fcbdd85d63d7503df718d6b2a51`) and PID 12681 (338,703 bytes, SHA-256 `3b81ebe8cf183c1da15ee252791819760c4ba4624b642379fc4aff167f0feac4`).

Replacement PID 2707 restored Input state, completed client IPC according to the guest-side trace, and produced compositor read/frame/request-dispatch/send records. The retained host `test.log` does not contain the matching client HELLO, snapshot, and subscription trace lines for PID 2707, so the matching client/server evidence gate remains **INCOMPLETE**. The IPC-only run result is also `INCOMPLETE` by design; exit status 0 means its targeted lifecycle campaign ended cleanly, not that the full acceptance gate passed. The sidecar collection gate passed. Six-category resolution coverage, cold boots, fallback, sanitizer reruns, and full lifecycle/security acceptance remain NOT RUN.

The source tree still needs a focused offline check that requires the three matching client response events in the retained recovery log. Then build one fresh diagnostic ISO, run one bounded locked 640×480 recovery, and require matching client and compositor HELLO/snapshot/subscription evidence plus retained attributable sidecar evidence. Only after that focused gate passes should the remaining acceptance stages resume. Do not change IPC protocol behavior to satisfy logging.
