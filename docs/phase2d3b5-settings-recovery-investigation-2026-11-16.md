# Phase 2D.3B.5 PID 8248 Recovery Investigation — 2026-11-16

**Phase status: INCOMPLETE.** This offline investigation examined the retained acceptance run `20261115T183456Z`. No QEMU session, ISO build, or acceptance rerun was started. No production or harness code was changed.

## Evidence integrity

The host-review bundle is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261115T183456Z-host-review`. SHA-256 verification passed for its 258-entry root package manifest, 254-entry guest evidence manifest, and 93-entry raw-artifact manifest. The screenshot manifest contains 75 entries. Its archived entries use absolute `/home/builder/...` guest paths, so plain host-side `sha256sum -c` cannot resolve those paths in the host-review copy; all 75 values were separately checked against their uniquely matching local basenames and passed. This is a path-mapping issue, not screenshot corruption. The retained [`seyrix-compositor.log`](</var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261115T183456Z-host-review/qemu-evidence/qemu-graphics-phase2d3b5-full-acceptance-640x480-controlled-20261115T183456Z/seyrix-compositor.log>) is 1,819,255 bytes and matches SHA-256 `3ee5d05ae3a2c84234bbd9b09e031c66d8d5eaf8beb3cffe8a4d58140c660703`.

The exact failure is in [`test.log`](</var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261115T183456Z-host-review/qemu-evidence/qemu-graphics-phase2d3b5-full-acceptance-640x480-controlled-20261115T183456Z/test.log:1260>); the raw compositor event ordering is in the retained compositor log. Its `00:00:xx.xxx` prefixes are compositor-session-relative, while supervisor `monotonic_ms` values are monotonic-clock readings. The Settings startup message includes a guest wall-clock time without an explicit timezone.

## PID 8086 → PID 8248 timeline

| Order | Preserved timestamp or line | Event |
| --- | --- | --- |
| 1 | `test.log:1255`, `2026-11-15T19:06:11Z` | The six-attempt recovery stage began with a 150-second timeout. |
| 2 | compositor log lines 6830–6845; test log line 1257 | Manager PID 7881 handled the open request. Attempt 1 was recorded at `monotonic_ms=1426529`; Wayland readiness was logged at line 6844; launch PID 8086 followed at line 6845. The helper recorded `parent=7881`, `count=1`, and marker order `6831 < 6844 < 6845 < 6851`. |
| 3 | compositor log line 6851; guest message time `19:06:11.752` | Settings emitted `SEYRIX_SETTINGS event=start pid=8086`. This confirms the application marker followed its supervisor launch marker. |
| 4 | compositor log lines 6944–6945; test log line 1258 | The helper's SIGKILL of PID 8086 was observed as `signal=9`, `runtime_ms=4237`. The supervisor scheduled attempt 2 with `delay_ms=1000`, `scheduled_ms=1431306`, and `retry_at_ms=1432306`. |
| 5 | compositor log line 6946; test log line 1259 | Attempt 2 was recorded at `monotonic_ms=1432313`. The helper calculated `1432313 - 1431306 = 1007 ms`, matching the recorded 1,007 ms backoff observation. |
| 6 | compositor log lines 6955–6959 | The supervisor logged `wayland-ready` for attempt 2, then `launch pid=8248 attempt=2 display=wayland-0`. This is the final supervisor event in the retained compositor log. |
| 7 | test log lines 1260–1261 | The helper reported `settings-retry-budget-launch-2-invalid-pid-8248` and exited through its generic helper failure path. The failure stage lasted 4,212 ms. |

The retained log has no `SEYRIX_SETTINGS event=start pid=8248`, no `exec Settings` error, and no supervisor exit/reap event for PID 8248 before capture. There is no `/proc` snapshot for that PID. Therefore the record does not establish whether PID 8248 was absent, still between fork and exec, had the wrong process name or UID, had an unexpected parent, or had already exited when the helper sampled it.

## Validation predicate

The run's immutable helper is [`execution-snapshot/tests/qemu-guest-helper.sh`](</var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261115T183456Z-host-review/execution-snapshot/tests/qemu-guest-helper.sh:1765>), SHA-256 `b01e9ade55f60f130ced836c421d525da64f9131a493dc0ec5425eb4869ad041`. It reads the launch, process count, parent PID, and startup markers once, then evaluates one compound predicate at lines 1774–1783. The predicate requires a positive PID and matching attempt, exactly one Settings process, `PPid` equal to manager PID 7881, attempt and Wayland-ready markers, a ready display socket, the application start marker, and the required line ordering. `settings_process_count` at lines 69–76 uses `pgrep -u 1000 -x seyrix-settings`, enforcing UID 1000 and the exact process name at sampling time. The distinct-PID check follows the compound predicate at lines 1784–1785, so it was not reached for PID 8248.

The failed reason does not identify which conjunct was false. The retained log does establish that `app_start` was empty at capture; consequently the nonempty start-marker and start-line checks could not pass. Because the helper retained no values for the other checks, it is not possible to say that this was the only failed conjunct.

## Production versus harness assessment

The matching builder source is [`desktop/session-supervisor/main.c`](</var/tmp/seyrix-phase2d3b5-retest-c3b1eeb/desktop/session-supervisor/main.c:627>), SHA-256 `2aba687152236fe1742fce2cace5ecf1a17e81fb05b401af97f877eee0b0a94b`. `launch_settings` forks at line 627. The child performs setup and calls `exec` at lines 634–658; the parent records `event=launch` at lines 670–674 after `fork` returns. The marker therefore records a forked child PID, not confirmed exec or completed application startup. In contrast, the helper checks the app startup marker immediately after finding the launch marker and does not wait for it.

This ordering exposes a harness timing vulnerability: a valid child can be checked before it has exec'd and emitted its startup event. It does not prove that this was the sole cause of PID 8248's rejection. A genuine child startup/exec failure or another failed process-identity check remains possible because no process snapshot or later child event was retained.

**Classification: C — insufficient evidence to distinguish a harness timing failure from a production child-start failure for this specific PID.** No production defect is established. The harness's missing wait is demonstrable from source, but the recorded error is too coarse to conclude that the PID was otherwise valid.

## Minimal correction and instrumentation

No correction is justified from the retained evidence alone, and none was implemented. Before another acceptance run, the helper should report field-level values at this failure point: `/proc/8248` existence and start time, UID, `comm`, executable, parent PID, exact Settings process count, attempt/ready/launch/start marker presence and line numbers, monotonic sample time, and any child exec-error or supervisor exit event. That would distinguish process absence, wrong identity, early exit, and an asynchronous startup window.

If instrumentation confirms the startup race, the smallest harness correction is to wait, with a bound inside the existing stage deadline, for the exact PID's `SEYRIX_SETTINGS event=start` marker and then re-evaluate all current checks. Keep UID/name, manager-parent, exact process count, attempt and event ordering, distinct PID, retry timing, six-attempt exhaustion, and the current 150-second stage timeout unchanged. Do not accept a PID based only on the supervisor launch marker.

## Offline verification

| Check | Result |
| --- | --- |
| Host-review manifest | **PASS** — SHA-256 verification, 258 entries |
| Guest evidence manifest | **PASS** — SHA-256 verification, 254 entries |
| Raw-artifact manifest | **PASS** — SHA-256 verification, 93 entries |
| Screenshot manifest | **PASS** — 75 SHA-256 values verified by unique basename after guest-path remapping |
| Retained compositor-log SHA-256 | **PASS** — digest and 1,819,255-byte size match |
| `qemu-graphics-settings-recovery-regression.py` | **PASS** — run offline from the builder retest worktree |
| Snapshot `qemu-graphics-harness-regression.tcl` | **PASS** |
| Bash syntax | **PASS** — 24 snapshot shell scripts |
| Tcl source completeness | **PASS** |
| Workspace `git diff --check` | **PASS** |
| QEMU, ISO rebuild, acceptance rerun | **NOT RUN** — outside this investigation's authorization |

The offline regression covers retry progression/timing and evidence retention; it does not reproduce the PID 8248 process-sampling instant. Phase 2D.3B.5 remains **INCOMPLETE**. The smallest justified next action is to add the field-level failure diagnostics above, verify them offline, and request separate authorization before any acceptance rerun.
