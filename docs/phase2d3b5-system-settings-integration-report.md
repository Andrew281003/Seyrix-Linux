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
