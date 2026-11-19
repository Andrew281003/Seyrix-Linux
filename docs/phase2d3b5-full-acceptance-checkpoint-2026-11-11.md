# Phase 2D.3B.5 Full Acceptance Checkpoint — 2026-11-11

*Updated 2026-11-13 (Europe/Belgrade); run timestamps are UTC.*

**Phase status: INCOMPLETE.** The latest controlled 640×480 session recorded 123 **PASS**, 1 **FAIL**, and 1 **INCOMPLETE**. Its first failure was a stale harness MRU path expectation; the Settings supervisor six-attempt gate was **NOT RUN**, and compositor-log retention **FAIL**. Offline regressions and evidence checks passed; production source and candidate ISO identity remained unchanged. See the latest rerun section below.

## Candidate and provenance

- ISO: `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-gcc16-20261111T164352Z/out/seyrix-phase2d3b-2026.11.11-x86_64.iso`
- SHA-256: `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`
- Build source: `codex/phase2d3b5-system-settings` at `c3b1eeb886cb0caf79da6b51015b3fe986590943`. The builder worktree already had local changes to the Settings window and test harness; these were preserved.
- Before the first full run, all 130 entries in `SHA256SUMS`, all 21 entries in `SHA256SUMS.build-export`, the ISO hash, the saved Settings source patch reconstruction, and all nine recorded source/test input hashes passed. Before the corrected rerun, the seven unchanged inputs passed again, and the two documented harness-only changes were checked against their new hashes. Guest-path screenshot and provenance manifests passed against their byte-identical exported copies.
- No production source was changed and the ISO was not rebuilt.

## Focused recovery evidence

The existing 640×480 IPC-only run is at `phase2d3b5-gcc16-20261111T164352Z/qemu-evidence/run` in the build export. Its targeted compositor-loss and Settings lifecycle stages passed: the manager remained alive, the compositor and Settings process recovered, Input state was restored, a single Settings process completed authenticated HELLO/snapshot/subscription, Wayland traces were collected and attributed, direct Settings termination recovered, intentional close stayed closed, and explicit reopen restored state.

That run's overall result is **INCOMPLETE by design** because it used `SEYRIX_QEMU_SETTINGS_IPC_ONLY=1`. It did not complete the full acceptance campaign.

## Full acceptance run paused at 640×480

The retained run is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-acceptance-checkpoint-20261111T201038Z/qemu-evidence/qemu-graphics-phase2d3b5-full-acceptance-640x480-20261111T201038Z`. The supporting preflight and stop record is `preflight-and-stop-record.txt` in the same export directory. The original build export was left unchanged.

The run passed the 640×480 GRUB request, UEFI boot, compositor and shell graphical readiness, shell startup-failure recovery, output-mode application, and several desktop/manual/terminal setup stages. At the user's request to prepare for a system update, SIGINT was sent to the owned Expect process; its cleanup stopped QEMU. The run ended **INCOMPLETE**, exit status 130, at 2026-11-11 20:13:41 UTC. The QEMU process is recorded as exited. `qemu-exit-status.txt` is absent. The Settings category stage had not started.

The raw-artifact and screenshot manifests both pass in the exported copy. The shared lock owner record is gone; no QEMU or Expect test process remains. The four temporarily executable harness files have been restored to mode `0644`, and the nine source input hashes pass again.

## Resumed 640×480 run and harness mismatch

The resumed run is retained at `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-acceptance-checkpoint-20261111T222401Z/qemu-evidence/qemu-graphics-phase2d3b5-full-acceptance-640x480-resume-20261111T222401Z`. Its evidence manifests pass for 73 raw artifacts and 58 screenshots.

The run passed the Settings six-category matrix, page/lifecycle reopen and close checks, and IPC checks, then failed at the Super+Tab dock-focus assertion. The harness expected the first terminal path to be `/tmp/seyrix-first`, while the fixture records it as `/home/seyrix/.seyrix-first`. The screenshot and serial trace show that Super+Tab selected the correct first terminal. This is a stale harness expectation; the evidence does not establish a compositor product failure. Per the stop-on-failure rule, the broader campaign stopped here.

A harness-only correction was made in builder `tests/qemu-graphics.exp`: the dock-focus regex, captured terminal path regex, and MRU expected path were updated to match the fixture path. Its SHA-256 is `bbc8d42cf4a7dd9346aad9afa6e6b6ba48108b1d3f884c934f0904febcec0d47` and mode is `0644`. The correction was exercised in the subsequent rerun below. No production source or ISO changed.

## Corrected 640×480 rerun before the prompt-wait fix

The corrected run is retained in the builder at `/var/tmp/seyrix-linux/qemu-graphics-phase2d3b5-full-acceptance-640x480-corrected-20261111T225601Z` and exported at `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-acceptance-checkpoint-20261111T222401Z/qemu-evidence/qemu-graphics-phase2d3b5-full-acceptance-640x480-corrected-20261111T225601Z`. Exported raw-artifact manifest **PASS** (76 entries); screenshot manifest **PASS** (60 entries).

The run passed the 640×480 Settings matrix for Appearance, Desktop & Dock, Window Management, Displays, Input, and About, including GTK page identity/mapping/bounds/input/outgoing-unmap checks and pixel-difference captures. It passed all three Settings reopen/close cycles, authenticated IPC/security checks, compositor interaction, resize and recovery stages, and the corrected Super+Tab MRU check (`expected=actual=/home/seyrix/.seyrix-first`). It then passed `isolated layer-shell panel startup` (`PHASE2C_PANEL=passed`) but stopped immediately afterward with: `The unprivileged control shell did not return after isolated layer-shell panel startup.` The expected `SEYRIX-USER#` prompt was not observed.

The run result is **INCOMPLETE**, exit 1; `expect-exit-status.txt` is 1. The QEMU state record says the owned process exited; `qemu-exit-status.txt` is absent because the harness stopped the guest. All test QEMU/Expect/Xvfb processes stopped, the shared-lock owner is absent, and harness modes are restored to `0644`. No other display profile was started.

The prompt-wait investigation below confirmed a harness timeout bug. The panel readiness marker passed; this earlier run did not establish a panel or compositor product failure. X11 forwarding failed during setup; the builder's original sshd configuration was restored byte-for-byte, and GTK ran under Xvfb at 2560×1440×24.

Before this run, build/export manifests **PASS** (130/21), ISO SHA-256 **PASS** (`45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`), and Settings source patch reconstruction **PASS**. Seven recorded inputs were unchanged; the two harness files were harness-only changes. `tests/qemu-graphics.exp` SHA-256 was `bbc8d42cf4a7dd9346aad9afa6e6b6ba48108b1d3f884c934f0904febcec0d47`; `tests/qemu-graphics.sh` SHA-256 was `8b2dfd91eafbf662b5f3e6c0a02d7bc3489fd34c539a8c6e0892905dc2cb05d2`. Both files were mode `0644`. No production source changed and the ISO was not rebuilt or promoted.

## Prompt-wait correction and single full 640×480 rerun

The failure mechanism was confirmed in `run_serial_check`: when `stage_timeout` is omitted, the command-result `expect` uses the existing saved timeout. The old code then assigned the empty `stage_timeout` to Expect's global `timeout` immediately before waiting for `SEYRIX-USER#`. Expect 5.45.4 converts that empty timeout with `atoi`, yielding zero; a direct delayed-child probe returned on timeout after 3 ms instead of waiting for its one-second output. This explains the earlier log sequence where `PHASE2C_PANEL=passed` was followed immediately by the control-shell prompt error.

The harness-only correction removed the unconditional empty assignment before the prompt wait and restores `saved_timeout` in the prompt-timeout handler. With no override, the saved timeout remains active. With an explicit `stage_timeout`, the override remains active through the prompt wait. The saved timeout is restored after success and before timeout-error exit. The offline regression passed for omitted timeout (37 seconds), explicit timeout (9 seconds), and restoration (37 seconds). Tcl completeness, `bash -n tests/qemu-graphics.sh`, and `git diff --check` also passed. The direct probe and verification records are under `harness-correction/control-shell-prompt-wait/` in the checkpoint export.

Exactly one full 640×480 acceptance run then used the existing ISO; it started at 2026-11-11 23:47:38 UTC and finished at 2026-11-12 00:14:27 UTC. The control-shell prompt failure did not recur. `PHASE2C_PANEL=passed` and later workspace, shell-recovery, Weston fallback, compositor-restart, and Settings endpoint checks proceeded. The run failed at `reopen the native Settings application after compositor restart`: `PHASE2D3B_LAUNCHER=failed`, `STAGE_RESULT ... FAIL duration_ms=40222` against a 120-second stage timeout. The existing assertion polls up to 120 times at 250 ms for a UID 1000 `seyrix-settings` process whose window is mapped and focused. The failure output contains no Settings PID or map/focus evidence. This is an explicit acceptance-gate failure; the evidence does not establish its product root cause.

The run result is **INCOMPLETE**, Expect and launcher exit statuses are 1, `.seyrix-failed` is present, and the owned QEMU process exited. `qemu-exit-status.txt` is absent. The post-restart Settings screenshot was not produced because the assertion failed before capture. `stage-results.log` contains 192 **PASS** and one **FAIL**. The exported evidence directory is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-acceptance-checkpoint-20261111T222401Z/qemu-evidence/qemu-graphics-phase2d3b5-full-acceptance-640x480-prompt-wait-fixed-20261111T234738Z`; its raw manifest passes (90 entries) and screenshot manifest passes (74 entries). The manifest SHA-256 values are `dbf3e76a86d773c13e71c666a44cfddb216257321051a50ecd4f8f095538b187` and `db3dd4405a58903d12f817458c3d787d351047734e7f35253aba3b1056cdb07f`; the retained launcher console SHA-256 is `e39e3ae52144932c2221d518133e0eafa61732e5b84e9070c03ccb3c8be0dff5`.

The previous resumed and corrected run manifests were rechecked and pass (73 raw/58 screenshots; 76 raw/60 screenshots). The existing ISO still hashes to `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5` on both host and builder and is 1,129,283,584 bytes. Source HEAD remains `c3b1eeb886cb0caf79da6b51015b3fe986590943`; source reconstruction and all nine pre-run input hashes passed, along with the 130-entry build and 21-entry metadata manifests. No ISO was rebuilt. After the run, builder harness files were restored to mode `0644`; the shared lock is free and no test QEMU, Expect, or Xvfb process remains. Final harness hashes and modes are in `harness-correction/control-shell-prompt-wait/post-run-verification.txt` and its copied `harness-snapshot-20261112T0015Z/SHA256SUMS`.

## Acceptance gate status at the 2026-11-12 checkpoint

| Gate | Result | Evidence / limit |
| --- | --- | --- |
| Source provenance, retained evidence, ISO SHA-256 | **PASS** | Export/build manifests, patch reconstruction, input hashes, and ISO SHA-256 verified before testing. |
| Focused 640×480 compositor and Settings recovery | **PASS (targeted)** | State restoration, authenticated IPC, singleton, cleanup, and attributed Wayland traces passed. The IPC-only run overall remains INCOMPLETE by design. |
| Full 640×480 acceptance | **INCOMPLETE** | Latest run passed the corrected Settings relaunch and endpoint reconnect, then timed out during the production Settings bounded-recovery budget check. See the retained run and failure analysis below. No automatic retry followed. |
| Six Settings categories, page identity, visible rendering, navigation, and input | **PASS (640×480 runs)** | All six categories and stated rendering, identity, bounds, input, unmap, and pixel-difference checks passed. |
| Corrected Super+Tab/MRU dock-focus check | **PASS** | Expected and actual terminal path both `/home/seyrix/.seyrix-first`. |
| Isolated layer-shell panel startup | **PASS** | `PHASE2C_PANEL=passed`; later acceptance stages also proceeded in the prompt-wait-fixed run. |
| Control-shell prompt after panel startup | **PASS (harness regression and full run)** | Empty timeout caused an immediate Expect timeout; the harness fix passed omitted/explicit/restored timeout regression checks, and the full run advanced past this point. |
| 800×600, 1280×720, and 1920×1080 Settings matrix | **NOT RUN** | No runs at these profiles. |
| Compositor/Settings lifecycle, state restoration, authenticated IPC, Wayland attribution | **PASS (targeted checks)** | Prior focused recovery and the latest 640×480 post-restart Settings relaunch/reconnect passed; the overall campaign remains incomplete at the later bounded-recovery gate. |
| Cold-boot campaign | **NOT RUN** | No current-candidate cold-boot campaign. |
| Resource and descriptor stability | **PARTIAL** | `SETTINGS_ENDPOINT_FD_STABILITY=passed cycles=10`; baseline and after were 23 in every cycle. Separate broader compositor resource stress checks (RSS delta, open FDs, process counts) were not reached; overall resource campaign remains incomplete. |
| Full security acceptance | **NOT RUN** | Targeted authenticated IPC checks passed; the wider security matrix was not reached. |
| Weston/TTY fallback | **PASS (latest 640×480 run)** | Weston fallback started, then exited and returned to tty1. |
| Sanitizer acceptance on current source | **NOT RUN** | The candidate ISO is not sanitizer-instrumented; no current-source sanitizer suite was run. |
| Two independent end-to-end sessions | **NOT RUN** | No complete end-to-end session pair. |

Two earlier launch attempts stopped at preflight: one could not open the builder-owned shared lock as restricted root; the next acquired that same lock as its owner and then found the harness execute bits missing. The single successful acceptance launch used the shared lock as `builder` and restored required executable modes afterward; neither check was bypassed. At that checkpoint, no retry had followed the Settings relaunch failure; the later harness correction and run are documented below.

## Settings relaunch investigation — 2026-11-12

The failed stage is a confirmed harness activation omission, not a Settings, compositor, or assertion defect. The sequence in the retained Expect source opens the launcher with Super+Space and types `Settings`, then immediately starts `settings_launcher_command`; unlike the earlier successful launcher sequence, it does not send Return to activate the selected result. The assertion therefore waited for a process that the harness had not requested the launcher to start.

The surrounding stages show the fresh compositor session was ready: the Settings endpoint was recreated, terminal PID 7718 and compositor PID 7704 both had session ID 582, and the terminal passed UID, mapping, focus, runtime-directory, and Wayland checks. The wrong-session probe also passed. The old compositor stopped cleanly; the supervisor logged `event=session-end pid=634`. After the missing activation, the failure diagnostic printed `PHASE2D3B_LAUNCHER=failed` and no `pgrep -a` Settings PID. No post-restart Settings map/focus event appears in the retained logs, and no post-launch screenshot was captured; the retained screenshots show the terminal and wrong-session probe before the launcher attempt. The artifacts do not establish any transient Settings process history, but the missing Return confirms the harness did not submit the launcher activation.

The assertion remains strict: it filters `seyrix-settings` to UID 1000, checks the process UID from `/proc`, and requires matching compositor `Window mapped` and `Window focused` records. It polls 120 times with a 250 ms sleep. The 120-second Expect timeout is a ceiling; this guest-side loop returns failure after 120 iterations (30 seconds of nominal sleep plus process and log-scan overhead). `run_serial_check` received the explicit failure marker at 40.222 seconds, so the stage is recorded as **FAIL**, not timeout.

The earlier IPC-only run exercised a different path with the session supervisor left active during compositor-loss recovery. It passed supervised recovery, direct Settings-process recovery, intentional close, and explicit launcher reopen (`old_pid=14417`, `new_pid=21445`), followed by HELLO, snapshot, and subscription checks. This supports the manager and IPC recovery path but does not cover the fresh-session full-acceptance sequence that omitted Return.

A harness-only fix now sends `qmp_key_chord $qmp {} ret` after typing `Settings` in `/var/tmp/seyrix-phase2d3b5-retest-c3b1eeb/tests/qemu-graphics.exp`. The offline source-order regression returned **PASS** on the corrected harness and failed against the retained pre-fix snapshot as expected. At this point the correction was host-side only; see the continuation below for builder synchronization and full acceptance results.

## Return-key synchronization and latest 640×480 run — 2026-11-12

The builder received the one-line Return-key correction in `tests/qemu-graphics.exp` and the new `tests/qemu-graphics-settings-relaunch-regression.tcl`. The corrected harness was compared against its pre-sync hash to verify the intended single-line delta; unrelated builder worktree changes were preserved. Builder offline checks all **PASS**: launcher source-order regression, timeout regression (`omitted=37`, `explicit=9`, `restored=37`), Tcl completeness, shell syntax, and `git diff --check`. The export manifests (130 and 21 entries) and nine source-input hashes passed. The existing ISO was rehashed and still matches `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`; source commit remains `c3b1eeb886cb0caf79da6b51015b3fe986590943`. No production source changed and no ISO rebuild occurred.

One full session used that ISO at 640×480. Run ID: `qemu-graphics-phase2d3b5-full-acceptance-640x480-settings-return-20261112T205322Z`; it started at 2026-11-12 20:53:50 UTC and ended at 21:19:37 UTC with exit status 1. The corrected stage `reopen the native Settings application after compositor restart` passed in 2461 ms; the endpoint reconnect stage passed in 218 ms. The later `production Settings bounded-recovery budget` stage ended **INCOMPLETE**: after PID 7891 was killed with SIGKILL, the helper recorded its exit but did not find `retry-scheduled next_attempt=2` during its 120-second poll. It emitted `settings-retry-backoff-invalid-attempt-2-delay-missing-elapsed-1330639`; the stage timed out at 194268 ms against a 150-second limit. The stage-results log contains 199 **PASS**, 1 **INCOMPLETE**, and 0 **FAIL** results. The run result is **INCOMPLETE**, `.seyrix-failed` is present, Expect exited 1, and QEMU PID 1169 is recorded as exited.

The retained evidence bundle is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-acceptance-checkpoint-20261111T222401Z/qemu-evidence/qemu-graphics-phase2d3b5-full-acceptance-640x480-settings-return-20261112T205322Z`. Its raw-artifact manifest passes for 91 entries and its screenshot manifest passes for 75 screenshots. It contains stage results, serial and Expect/test logs, screenshots, run metadata, and exit-status evidence. The full guest `/tmp/seyrix-compositor.log` is absent, so the retained evidence does not establish whether the supervisor failed to schedule attempt 2 or the relevant log was unavailable to the test. No production root cause is claimed.

After the run, the shared lock probe was free, no nested QEMU/Expect/Xvfb/test process remained, and all six synchronized harness files were mode `0644`. The lock-owner record identifies PID 1064 for the completed session. No retry, production change, ISO rebuild, or other display-resolution run was made.

## Bounded-recovery investigation — 2026-11-13

The failed gate killed Settings PID 7891 with SIGKILL and recorded its exit. The production supervisor's reap_settings_child path calls schedule_next_attempt for this unexpected exit while OPEN remains requested. That function sets retry_at_ms and logs event=retry-scheduled completed_attempt=1 next_attempt=2. The helper searched for the different substring event=retry-scheduled next_attempt=2, omitting the intervening completed_attempt field, so its polling could not match the production line.

The retained failure marker ended in elapsed-1330639, which indicates the helper found the next attempt's monotonic timestamp even though its retry-marker search failed. Together with the supervisor state machine, this supports that attempt 2 was scheduled and the event was emitted but not observable to the helper's matcher. The complete guest /tmp/seyrix-compositor.log is absent, so the raw event line cannot be independently inspected.

The guest log was not preserved because /tmp/seyrix-compositor.log existed only inside the temporary guest and the host runner retained serial/test output without transferring that file before stopping QEMU. The corrected harness emits the full log at bounded-recovery gate completion on both pass and failure. Its host retention step extracts it as seyrix-compositor.log and records capture completeness in compositor-log-retention.txt before the raw-artifact manifest is written.

The stage was recorded at 194268 ms against 150 seconds. run_serial_capture gave the command-result wait the configured timeout and then started another full timeout while waiting for the control-shell prompt. The helper also performed up to 480 polls at 250 ms, rescanning the log suffix each time without an elapsed-time deadline. The harness now uses a 120-second elapsed polling deadline and one absolute Expect deadline shared by command completion and prompt return.

Offline verification in /var/tmp/seyrix-phase2d3b5-retest-c3b1eeb:

- **PASS:** retry-event format and guest-log retention regression (python3 tests/qemu-graphics-settings-recovery-regression.py).
- **PASS:** Expect stage-deadline regression (tclsh tests/qemu-graphics-capture-timeout-regression.tcl).
- **PASS:** prior timeout and Settings relaunch regressions (tclsh tests/qemu-graphics-timeout-regression.tcl; tclsh tests/qemu-graphics-settings-relaunch-regression.tcl).
- **PASS:** shell syntax, Tcl source completeness, and git diff --check.
- **NOT RUN:** live QEMU verification of retry recovery and guest-to-host log capture. Production code and the existing ISO are unchanged.

## Resume point after investigation — before the controlled rerun (superseded)

Keep Phase 2D.3B.5 **INCOMPLETE**. The next justified step is one approved 640×480 acceptance run using the unchanged candidate ISO and the corrected harness, with the guest compositor log retained at gate completion. Do not proceed to other display profiles or treat offline regressions as full acceptance. Cold-boot, broad security, sanitizer, and two independent end-to-end checks remain **NOT RUN**; broader resource stability remains **PARTIAL**.

## Resume point from 2026-11-12 (superseded)

Investigate the missing attempt-2 supervisor event with adequate retention of the guest compositor log before making code changes or starting another full acceptance run. Do not automatically retry or proceed to 800×600, 1280×720, or 1920×1080. Cold-boot, broad security, sanitizer, and two independent end-to-end checks remain **NOT RUN**; broader resource stability remains **PARTIAL**. Keep Phase 2D.3B.5 **INCOMPLETE** until every mandatory gate passes.

The active workspace checkout remains `phase2d1-hybrid-window-management`; this checkpoint documents the candidate built and tested in the separate builder VM. Existing worktrees, local changes, and prior evidence were preserved.

## Controlled acceptance rerun — 2026-11-13 (previous attempt)

Exactly one full 640×480 acceptance session used the existing ISO and synchronized corrected harness. Run ID: `qemu-graphics-phase2d3b5-full-acceptance-640x480-controlled-rerun-20261113T1325Z`. The exported evidence bundle is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-rerun-20261113T1325Z/qemu-evidence/qemu-graphics-phase2d3b5-full-acceptance-640x480-controlled-rerun-20261113T1325Z`.

The session ended **INCOMPLETE**, Expect exit status 1. The raw `test.log` contains 200 stage results: 199 **PASS**, 0 **FAIL**, and 1 **INCOMPLETE**. One enclosing `production Settings bounded-recovery budget` begin marker has no separate result. The derived `stage-results.log` is itself incomplete: it contains 197 **PASS** and 1 **INCOMPLETE** because two PASS lines carry two `SEYRIX-USER# ` prompt prefixes, while its extraction accepts at most one. Both omitted PASS lines are present in `test.log`. The raw manifest includes both logs.

The exact incomplete gate is `kill six consecutive manager-launched Settings PIDs without issuing another OPEN and verify bounded retry exhaustion`. It reached its configured 150-second limit at 149,760 ms. Runtime output contains `SETTINGS_RECOVERY_BACKOFF index=2 delay_ms=1000 elapsed_ms=1003 scheduled_ms=1306309 attempt_ms=1307312`; this demonstrates the corrected matcher found and validated attempt-2 scheduling, including the supervisor event format with `completed_attempt=1 next_attempt=2`. There is no `SETTINGS_RECOVERY_BUDGET_LAUNCH index=2`, so the full retry budget and recovery assertion remain **INCOMPLETE**.

The shared absolute Expect deadline behaved as intended across the command and prompt waits. After the attempt-2 backoff marker, the control prompt returned without the helper's expected success or failure sentinel. The gate then timed out without six-attempt completion. The retained output does not establish why the helper returned. A read-only source review found one unproven candidate: with `set -Eeuo pipefail`, the unguarded `pgrep -u 1000 -x seyrix-settings | wc -l` command can terminate the helper if no Settings process exists at that instant. This is a hypothesis only; no product root cause is established.

Settings lifecycle checks before the bounded-recovery gate passed: reopening after compositor restart (2,188 ms), authenticated endpoint snapshot/reconnect (191 ms), closing after reconnection (124 ms), and opening through the packaged desktop entry (1,750 ms). These results do not substitute for the incomplete retry-budget recovery check.

Compositor-log capture is **INCOMPLETE**. `compositor-log-retention.txt` records `Guest compositor log was not emitted by this run.` No guest `seyrix-compositor.log` or begin/end capture framing was retained. The offline retainer round-trip self-check passed with 67,108,850 bytes, but live guest-to-host capture remains unverified. The helper's return without a sentinel also meant no normal failure diagnostic emitted the compositor log.

Evidence validation: the host-side raw-artifact manifest **PASS** (92 entries; SHA-256 `14483aaacf51480f7bde5a784d1c3aa1da486a9031d5055482bf62bab8b2208e`); the screenshot manifest was validated on the builder (**PASS**, 75 screenshots; SHA-256 `ecfc8adeacfed20173626a91e4f70a253d9beca89fee51e8b1a01d1fb7a49262`). The bundle contains screenshots, test/serial/Expect output, run metadata, and process records. Hashes: `test.log` `6e508b63c22c071ee9a8fa2096382d458ee7b6d63da507d004b7897f8af3dd84`; `serial.log` `6d8f8492c869236c1c7b8fcda72fa3f509b8ef29c15904768fb11cb7228b17f5`; `stage-results.log` `be1914b92d5c9a9b401c2f3a835e64a61e2dedd46670a1cb2a6dbd1cff60128c`. `expect-exit-status.txt` records 1 and `qemu-process-state.txt` records the owned QEMU PID exited. `qemu-exit-status.txt` is absent because cleanup terminated the owned QEMU after Expect exited. The run-result record is **INCOMPLETE**.

Preflight and cleanup: source commit `c3b1eeb886cb0caf79da6b51015b3fe986590943`; existing ISO SHA-256 remained `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`. Source/build provenance, prior evidence manifests, offline regressions, syntax/Tcl checks, `git diff --check`, builder harness synchronization, and pre-run lock checks passed. No production source changed; the ISO was not rebuilt or promoted. Harness modes were restored. The shared lock `/tmp/seyrix-qemu-acceptance.lock` is free, its owner record is absent, and no acceptance QEMU/Expect/Xvfb processes remain. A wrapper syntax error was retained as `launcher-wrapper-preflight-error.log`; it occurred before lock acquisition or QEMU launch and was not an acceptance session.

## Controlled acceptance rerun — 2026-11-13 (latest)

Exactly one authorized QEMU session used the existing candidate ISO at 640×480 with TCG and virtio-vga. Run ID: `20261113T162847Z`; interval: `2026-11-13T16:28:47Z`–`16:38:46Z`. Candidate identity remained SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`, 1,129,283,584 bytes, source commit `c3b1eeb886cb0caf79da6b51015b3fe986590943`. The original ISO and guest input copy matched after the run. No rebuild or promotion occurred.

Evidence root: `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-rerun-20261113T161711Z`. Raw run bundle: `qemu-evidence/qemu-graphics-phase2d3b5-full-acceptance-640x480-controlled-rerun-20261113T161711Z`. All 125 stage results reconcile with the raw `test.log`: 123 **PASS**, 1 **FAIL**, 1 **INCOMPLETE**.

The first failure at 16:36:46Z was `verify Super+Tab's default MRU selection activates the other window`. The harness expected `/tmp/seyrix-first`; the observed fixture path was `/home/seyrix/.seyrix-first`. The retained checkpoint identifies this as a stale harness expectation, so this is classified as a harness assertion mismatch; no production defect is established. The harness continued after the failed stage. The owned run was interrupted at 16:38:46Z; `committed geometry and constraints for xdg resize bottom-right normal` ended **INCOMPLETE** with `qemu-eof`. Expect and run-result status were 1, metadata records 141 after stop, and QEMU PID 4413 is recorded exited. No second run or automatic retry occurred.

Settings launch, authenticated read-only IPC, category/page/layout and lifecycle reopen/close stages passed before the stop; shell startup-failure recovery stages passed. The Settings supervisor six-attempt sequence and six PID observations are **NOT RUN** (zero launch/backoff markers). No unexpected guest-helper exit marker was observed. Later window-management, compositor, and acceptance gates remain incomplete.

Compositor-log retention is an explicit **FAIL**: `compositor-log-retention.txt` says the guest compositor log was not emitted. No complete guest log or byte-count/SHA-256-framed capture exists, so host integrity verification was not possible. A bare `COMPOSITOR_LOG_END` without a begin marker is not a complete capture.

Preflight offline regressions **PASS** for zero-process/status handling, strict six-attempt and delay assertions, log-retention success/failure and incomplete-capture rejection, timeout scenarios, and corrected stage extraction. Evidence verification **PASS**: raw artifacts 72/72; guest and portable host screenshot manifests 57/57 each; stage reconstruction identical; top-level evidence manifest 120 entries. Source HEAD and the Settings source hash are unchanged; harness modes were restored. No test QEMU/Expect/Xvfb remains. The lock file remains, but the nonblocking lock probe acquired and released it.

Detailed evidence is in `postrun/failure-report.txt`, `postrun/acceptance-outcome.txt`, and `postrun/manifest-validation-summary.txt`; `evidence-manifest.sha256` covers all files in the evidence root except itself.

## Earlier resume point (before subsequent controlled attempts)

Keep Phase 2D.3B.5 **INCOMPLETE**. Reconcile the active harness MRU path expectation with the fixture offline, and cover stop-on-failure and abnormal-exit compositor-log retention before requesting fresh authorization for another QEMU run. Do not rebuild or promote the ISO, start another resolution, or infer that the six-attempt recovery gate passed. Cold-boot, broad security, sanitizer, and two independent end-to-end checks remain **NOT RUN**; broader resource stability remains **PARTIAL**.

## Authorized acceptance preflight — 2026-11-13 21:38 UTC

The authorized 640×480 attempt stopped before runtime-file validation or acceptance launch. The host-side `/tmp/seyrix-qemu-acceptance.lock` was acquired and released; a nonblocking reacquisition then succeeded. This does not verify acquisition of the builder guest's acceptance lock.

On host `quack-d`, the physical workspace was `/var/home/quake/Desktop/Coding/Seyrix-Linux`; `/home/builder/Seyrix-Linux` was absent. The builder VM was already running on the host with SSH forwarded to `127.0.0.1:22222`, but batch public-key authentication as `builder` was rejected. `virsh` was unavailable. The guest could not be accessed, so its worktree, acceptance lock, harness hashes, candidate ISO identity, and acceptance-process state remain **UNVERIFIED**.

No acceptance QEMU, Expect, or Xvfb process was launched on the host. No harness synchronization, runtime-file validation, evidence-directory allocation, offline regression tests, syntax checks, or acceptance session was run; these checks remain **NOT RUN**. Host-workspace `git diff --check` was **PASS** (exit status 0); no builder-guest diff check was run. No new stage results or compositor-log capture exist for this attempt; evidence manifests are **NOT RUN**. No production source or ISO was changed, and Phase 2D.3B.5 remains **INCOMPLETE**.

Resume by providing authenticated access to the existing builder VM, or running in an environment where `/home/builder/Seyrix-Linux` is available. Restart the mandatory preflight at builder-side lock acquisition before inspecting runtime files. Do not start QEMU until all preflight checks pass.

## Controlled acceptance preflight — 2026-11-13 (stopped before QEMU)

The builder guest lock `/tmp/seyrix-qemu-acceptance.lock` was acquired before runtime-file validation. The actual launch worktree resolved to `/home/builder/Seyrix-Linux`. All five audited harness hashes matched the November 13 audit: `qemu-graphics.exp` `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c`, `qemu-graphics.sh` `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08`, `qemu-guest-helper.sh` `b01e9ade55f60f130ced836c421d525da64f9131a493dc0ec5425eb4869ad041`, `qemu-retain-compositor-log.sh` `4e01c2a09074e12227eb55683cb33b50fba4a8c9611035bd5f7bcb61b7244cd2`, and `qemu-graphics-harness-regression.tcl` `b93e8da176f26edfa55aad4561a802bfde0daf4c43e16460bbea210fa92b4f79`. The candidate ISO hash and size passed: `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`, 1,129,283,584 bytes. Static inspection of the archived launch wrapper confirmed it targets that worktree's shell driver; the driver invokes its sibling Expect script. No wrapper was launched for this attempt.

The offline preflight **FAILed**, so QEMU was not started. The first failed check was `python3 tests/qemu-graphics-settings-recovery-regression.py`: its builder copy expects a local retention flag, while the audited helper makes the flag global for its EXIT trap. `tclsh tests/qemu-graphics-capture-timeout-regression.tcl` also **FAILed** because its mock uses a fixed Expect argument index after the audited script added an unexpected-helper-exit branch. `bash tests/settings-render-trace-regression.sh` **FAILed** because its assertion expects `elapsed_ms >= retry_delay_ms`, while the helper validates `attempt_ms - scheduled_ms`. Read-only comparison found the three builder test copies stale relative to the passing audited retest copies; no production or audited runtime-harness defect was established.

Other preflight checks **PASSed**: the MRU/stop-on-failure regression, timeout and Settings-relaunch regressions, Bash syntax, Tcl completeness, and `git diff --check`. The locked process check found no acceptance QEMU, Expect, or Xvfb process before those checks. No acceptance session ran: stage counts are 0 **PASS**, 0 **FAIL**, and 0 **INCOMPLETE** because there are no stage results. The Settings supervisor recovery gate is **NOT RUN**, guest compositor-log capture was **NOT ATTEMPTED**, and raw-versus-extracted stage reconciliation is **NOT RUN**. Do not classify the capture as a failure for this attempt.

The preflight-only evidence bundle is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-preflight-20261113T223258Z`; its `evidence-manifest.sha256` **PASSes**. No acceptance-run manifest exists. The advisory lock was released when its existing lock-holder session closed; owner-record cleanup was not reverified. Phase 2D.3B.5 remains **INCOMPLETE**.

**Resume point:** reconcile only the three stale regression-test copies in `/home/builder/Seyrix-Linux`, then rerun the full offline preflight and verify the five audited hashes again. Start exactly one 640×480 session only if every preflight check passes. Do not rebuild the ISO or start another resolution.

## Controlled acceptance lock preflight — 2026-11-13 23:02 UTC (stopped)

One SSH connection used BatchMode and public-key authentication; it produced no password prompt. The builder `qemu-lock` command then timed out after 30 seconds before the launch wrapper ran. Its diagnostic reported token `20261113T222141Z-6715`, owner PID `6715`, start time `2026-11-13T22:21:41Z`, and command `builder acceptance preflight and one authorized run`. The owner process state was not verified, so the lock state remains **UNVERIFIED**. Do not delete the lock file or assume the owner is stale.

Because lock acquisition failed, this attempt did not revalidate the builder runtime hashes, candidate ISO, conflicting-process state, or corrected offline regression overlay. The prior locked preflight recorded all five audited harness hashes matching and the candidate ISO at SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5` and 1,129,283,584 bytes; those are not fresh checks for this attempt. The offline correction overlay was **NOT RUN**. No source or test files were edited, and this attempt started no QEMU, Expect, or Xvfb process.

Acceptance stage counts are 0 begins and 0 results (0 **PASS**, 0 **FAIL**, 0 **INCOMPLETE**); all acceptance stages are **NOT RUN**. Settings supervisor recovery is **NOT RUN**. Guest compositor-log capture is **NOT ATTEMPTED**, not a capture failure. No raw-stage reconciliation was possible. The evidence root is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261113T225130Z`; the host verified its 27-entry manifest **PASS**. Phase 2D.3B.5 remains **INCOMPLETE**.

**Resume point:** have the lock owner release cleanly, then verify PID `6715` and lock availability before starting another preflight. Do not delete the lock file or claim the process is absent. Once the lock is available, revalidate the worktree, hashes, ISO, conflict scan, and corrected offline suite before any QEMU launch.

## Controlled acceptance preflight — 2026-11-13 23:07 UTC (stopped before tests)

After safely releasing the verified orphan lock keeper, a locked preflight stopped before the regression suite because `/var/tmp/seyrix-linux` was not writable. No QEMU was started. No acceptance results or guest log capture were produced.

## Controlled acceptance launch preflight — 2026-11-13 23:14 UTC (stopped before Expect/QEMU)

Under the builder acceptance lock, the launch worktree was `/home/builder/Seyrix-Linux`. The five audited harness hashes and `qemu-lock.sh` hash matched their recorded values. The source ISO and copied ISO both matched SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5` and size 1,129,283,584 bytes. All nine offline regression, syntax, and diff checks **PASSed** using corrected audited test copies in an isolated overlay; the builder's test copies remained unchanged. The builder status and Settings source hash were unchanged.

The single runner invocation exited 1 before launching Expect or QEMU. `qemu-graphics-preflight.sh` rejected `execution-snapshot/tests/qemu-graphics.sh` because the snapshot file mode was `0444`; the source file was `0644`. The required launcher, Expect, lock, and helper files need their executable bits set before the snapshot becomes read-only. Executed driver SHA-256: `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08`; `qemu-lock.sh`: `ec1ae3211bbb7a862780fea7581a26a3ee84d4f07b13ce60b7ff9af3008b88f8`; sourced preflight module: `ee6011bf2ba05eed0288e7b2347f34a435a7c2f74ab3973d82e72ccb085d4956`.

Evidence root: `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261113T231200Z`; its 167-entry manifest was host-verified **PASS**. Runner exit status was 1; QEMU sessions started: 0. No `test.log` or run directory was created. Acceptance stage counts are 0 begins and 0 results (0 **PASS**, 0 **FAIL**, 0 **INCOMPLETE**); all acceptance stages are **NOT RUN**. Settings recovery is **NOT RUN**, guest compositor-log capture is **NOT ATTEMPTED** (not a capture failure), helper exit is **NOT OBSERVABLE**, and stage reconciliation is **NOT RUN**. The ISO source and copy remained unchanged. No retry was made; Phase 2D.3B.5 remains **INCOMPLETE**.

**Resume point:** fix executable modes in a fresh immutable execution snapshot before another separately authorized one-run attempt. Do not automatically retry, rebuild the ISO, or start another resolution.

## Snapshot permission correction — 2026-11-14 (acceptance not run)

The snapshot copy/seal order was confirmed: `cp -a tests` copied the builder's non-executable modes, then `chmod -R a-w` sealed the snapshot without restoring the modes required by the graphics preflight. A fresh snapshot was prepared at `/home/builder/phase2d3b5-controlled-acceptance-20261114T103457Z/execution-snapshot`. Only `qemu-graphics.sh`, `qemu-graphics.exp`, `qemu-lock.sh`, `qemu-guest-helper.sh`, and the directly invoked `qemu-retain-compositor-log.sh` received `chmod u+x` before sealing. Their sealed modes are respectively `0544`, `0544`, `0544`, `0544`, and `0555`; the snapshot contains no writable paths. Its filesystem is `/` on ext4 with `rw,relatime`, without `noexec`.

The added offline regression is `tests/qemu-graphics-snapshot-permissions-regression.sh` (SHA-256 `20eb97ccd7b391d20cce478bd0190ce6a19280baef8ea2adf85f06ccf1a1dc27`). Its corrected version **PASSed** against the sealed snapshot and its negative control **PASSed** by rejecting a fixture with the `qemu-graphics.sh` execute bit removed. It verified that the wrapper's tag-derived snapshot, lock script, shell driver, sourced preflight, sibling Expect file, guest helper, and log retainer resolve within the intended snapshot. The first version of this new regression failed because it compared a literal `$artifact_root`; that test defect was corrected and retained in the evidence history.

The existing six offline regressions **PASSed**: MRU/stop, Settings recovery, capture timeout, general timeout, Settings relaunch, and Settings render trace. Bash syntax, Tcl completeness, and `git diff --check` also **PASSed**. The new regression's first run is recorded as **FAIL** in `offline-check-status.tsv`; the corrected positive and negative runs are recorded separately as **PASS** in `offline-corrected-retest-status.tsv`. The builder worktree HEAD and status were unchanged.

The five audited snapshot hashes **PASSed**:

- `qemu-graphics.exp`: `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c`
- `qemu-graphics.sh`: `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08`
- `qemu-guest-helper.sh`: `b01e9ade55f60f130ced836c421d525da64f9131a493dc0ec5425eb4869ad041`
- `qemu-retain-compositor-log.sh`: `4e01c2a09074e12227eb55683cb33b50fba4a8c9611035bd5f7bcb61b7244cd2`
- `qemu-graphics-harness-regression.tcl`: `b93e8da176f26edfa55aad4561a802bfde0daf4c43e16460bbea210fa92b4f79`

The separately checked `qemu-lock.sh` hash is `ec1ae3211bbb7a862780fea7581a26a3ee84d4f07b13ce60b7ff9af3008b88f8`; `qemu-graphics-preflight.sh` is `ee6011bf2ba05eed0288e7b2347f34a435a7c2f74ab3973d82e72ccb085d4956`.

Exactly one launch-wrapper invocation was attempted through `qemu-lock.sh`. It exited 1 before wrapper preflight because the host-created evidence directory was mode `0755` and appeared inside the guest as `root:root 0755`; the `builder` account could not create `preflight/launch-console.log`. This is the first failed gate. No offline check ran in that invocation, and no Expect or QEMU process started. The offline checks and snapshot work were performed separately; the acceptance wrapper was not relaunched.

The candidate ISO source remained SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`, size 1,129,283,584 bytes. No ISO copy or build was made. Acceptance result: **0** QEMU sessions; **0/0** stage begins/results; **0 PASS / 0 FAIL / 0 INCOMPLETE**; all acceptance stages **NOT RUN**. Settings supervisor recovery is **NOT RUN**, compositor-log capture is **NOT ATTEMPTED**, and stage reconciliation is **NOT RUN**.

Evidence root: `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261114T103457Z`; the host-verified manifest contains 134 entries. Historical evidence roots were preserved. Phase 2D.3B.5 remains **INCOMPLETE**. A fresh acceptance launch requires a separate authorization because the one attempted wrapper run failed before preflight and the instruction forbids automatic retry.

**Resume point:** use a fresh evidence root writable by `builder`, rerun the controlled preflight only after separate authorization, and start at most one 640×480 QEMU session if every offline gate passes. Do not rebuild or promote the ISO, reuse the failed launch root as a fresh run, or automatically retry.

## Controlled 640×480 acceptance — 2026-11-15 (latest resume point)

Phase 2D.3B.5 remains **INCOMPLETE**. One authorized run, ID `20261115T183456Z`, used TCG, virtio-vga, and 640×480. Wrapper start: `2026-11-15T18:39:27Z`; acceptance preflight: `18:41:56Z`; QEMU launch: approximately `18:41:59Z`. Exactly one runner invocation occurred, with no retry or additional acceptance QEMU session.

Preflight **PASSed**: builder VM running; 9p tag `seyrix_export` mounted at `/mnt/seyrix-export`; fresh evidence and `preflight/` directories builder-owned mode `0700` and builder access probes passed. The sealed execution snapshot `/home/builder/phase2d3b5-controlled-acceptance-20261114T103457Z/execution-snapshot` passed recorded hashes, no-write-bit checks, required `0544`/`0555` modes, and launcher resolution checks. The candidate ISO remained unchanged before/after at SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5` (1,129,283,584 bytes). Offline inputs matched the readiness hashes; six offline regressions were not rerun and prior readiness **PASS** results were reused. Syntax, Tcl completeness, diff, and snapshot-permissions checks **PASSed**. No ISO rebuild/promotion or production source change occurred.

Acceptance reconciliation **PASSed**: 203 stage begins, 202 results; 201 **PASS**, 1 **FAIL**, 0 **INCOMPLETE**; 21 runner call sites **NOT RUN**. No later stage began after the first failure. At `19:06:11Z`, the Settings supervisor gate (“kill six consecutive manager-launched Settings PIDs without issuing another OPEN and verify bounded retry exhaustion”) failed after 4,212 ms with `settings-retry-budget-launch-2-invalid-pid-8248`. Attempt 1 validated PID 8086, parent 7881, count 1; its SIGKILL exit was observed. Retry timing was scheduled 1,000 ms, observed 1,007 ms. Attempt 2 launch PID 8248 was logged, then rejected by strict validation. Six distinct PID observations and bounded exhaustion were not established. The retained compositor log has no matching `SEYRIX_SETTINGS event=start pid=8248` before failure; treat this as an investigation lead because the generic helper error does not disclose the exact failed predicate.

Compositor-log capture **PASSed integrity**: 1,819,255 bytes, SHA-256 `3ee5d05ae3a2c84234bbd9b09e031c66d8d5eaf8beb3cffe8a4d58140c660703`, matching both the helper marker and postrun verification. Guest evidence manifest **PASS** (254 entries); host-readable review package manifest **PASS** (258 entries) at `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261115T183456Z-host-review`. QEMU and acceptance lock owner are gone; no acceptance QEMU/Expect/Xvfb process remains. Wrapper exit status was 1 due to the acceptance failure.

Exact executed launcher SHA-256: `2d940d03d96b3455fff4add0202ac466f840cebfaf930d3916dafe1377a5ad94`. Snapshot/support hashes: `qemu-graphics.exp` `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c`; `qemu-graphics.sh` `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08`; `qemu-guest-helper.sh` `b01e9ade55f60f130ced836c421d525da64f9131a493dc0ec5425eb4869ad041`; `qemu-retain-compositor-log.sh` `4e01c2a09074e12227eb55683cb33b50fba4a8c9611035bd5f7bcb61b7244cd2`; `qemu-graphics-harness-regression.tcl` `b93e8da176f26edfa55aad4561a802bfde0daf4c43e16460bbea210fa92b4f79`; `qemu-lock.sh` `ec1ae3211bbb7a862780fea7581a26a3ee84d4f07b13ce60b7ff9af3008b88f8`; `qemu-graphics-preflight.sh` `ee6011bf2ba05eed0288e7b2347f34a435a7c2f74ab3973d82e72ccb085d4956`; snapshot-permissions regression `0851186a8b8056757c108d3d00d52867d464c142d6f798ed5ef2c631ea09a146`.

**Resume point:** inspect preserved test/compositor logs and determine why PID 8248 failed strict manager-launched process validation. The remaining gates are a passing six-attempt supervisor recovery and all stages skipped after the first failure, followed by stage/evidence reconciliation. No further acceptance session is authorized by this result; do not declare phase completion.
