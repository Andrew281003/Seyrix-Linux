# Phase 2D.3B.5 Acceptance Resume Attempt — 2026-11-16

**Phase status: INCOMPLETE.** The corrected snapshot digest passed, but the run-local preflight failed before invoking the acceptance runner. No acceptance QEMU session started, and no retry occurred.

## Corrected digest and preflight

The authoritative SHA-256 for `tests/qemu-graphics.exp` is `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c`. The guest snapshot and host review copy matched it. The earlier digest mismatch was solely an instruction typo.

The 63-file execution snapshot remained sealed with no writable paths; all 62 unchanged support-file hashes matched their verified baseline. The diagnostic-enabled `qemu-guest-helper.sh` hash was `a3510bf544727c4168ac6e5351f4c980c8b3fa5c15689c59e830b63edf4b0562`. The Builder VM, `seyrix_export` 9p mount, evidence-directory write probe, free acceptance lock, absence of conflicting guest acceptance processes, and candidate ISO identity all **PASSed**. The candidate ISO remained SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5` (1,129,283,584 bytes), including the post-attempt check. No ISO rebuild or promotion occurred.

The prepared launcher had an artifact-root check that rejected the directory containing the verified snapshot and offline-regression verification bundle. A run-local resume copy retained the sealed snapshot and added a fail-closed allowlist for those two existing directories, plus a separate expected-hash manifest for the diagnostic helper. The original launcher hash is `709007872e84d145cb8b02b8738f47faa76c710696b4d767041262915a10432e`; the resume copy hash is `82ffb66cac9fec258bb680aaf552f734bab165f349572e07b74d40c423d3e406`. The acceptance driver, Expect script, helper in the snapshot, predicates, retry timing, deadlines, and compositor-log capture source were not changed.

## Preflight execution result

One locked resume-wrapper invocation exited with status 80. Its final status records `Acceptance runner invoked: 0`; fail-fast stopped before the ISO copy and QEMU launch.

| Check | Result |
| --- | --- |
| Six offline regressions | **FAIL** — 5 passed; Settings recovery exited 1 |
| Bash syntax | **PASS** |
| Settings recovery regression | **FAIL** — the regression could not find a diagnostic function in the offline overlay's worktree `qemu-guest-helper.sh` and raised `StopIteration` |
| Snapshot-permission syntax check | **FAIL** — exit 127 because `preflight/qemu-graphics-snapshot-permissions-regression.sh` was absent |
| Tcl completeness | **PASS** |
| `git diff --check` | **PASS** |
| Direct snapshot permission/path regression | **PASS** — run before the wrapper invocation |
| Candidate ISO after attempt | **PASS** — digest and size unchanged |

The failing recovery regression read worktree helper hash `b01e9ade55f60f130ced836c421d525da64f9131a493dc0ec5425eb4869ad041`. The diagnostic helper with hash `a3510bf544727c4168ac6e5351f4c980c8b3fa5c15689c59e830b63edf4b0562` remained intact in the sealed execution snapshot but was not copied into the offline overlay. The field-level acceptance diagnostics were therefore **NOT RUN**.

## Acceptance status and evidence

No `test.log` or acceptance `stage-results.log` exists. There are zero executed acceptance-stage records: PASS=0, FAIL=0, INCOMPLETE=0. The script contains 224 static `run_serial_check` call sites; all 224 are **NOT RUN**. The Settings acceptance recovery stage is **NOT RUN**. No compositor log was captured, so its size and SHA-256 are **NOT AVAILABLE**.

The final guest evidence manifest **PASSed** with 172 entries. The host-readable review copy at `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261116T202205Z-host-review-resume-20261116T2135Z` has a **PASSing** 175-entry review manifest. The preserved offline Settings recovery log is available at [`regression-settings-recovery.log`](</var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261116T202205Z-host-review-resume-20261116T2135Z/preflight/regression-settings-recovery.log>).

## Source hashes and resume gates

The following acceptance sources were verified in the sealed snapshot but were **NOT EXECUTED** by QEMU:

| Source | SHA-256 |
| --- | --- |
| `qemu-graphics.sh` | `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08` |
| `qemu-graphics.exp` | `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c` |
| Diagnostic `qemu-guest-helper.sh` | `a3510bf544727c4168ac6e5351f4c980c8b3fa5c15689c59e830b63edf4b0562` |
| `qemu-retain-compositor-log.sh` | `4e01c2a09074e12227eb55683cb33b50fba4a8c9611035bd5f7bcb61b7244cd2` |
| `qemu-graphics-preflight.sh` | `ee6011bf2ba05eed0288e7b2347f34a435a7c2f74ab3973d82e72ccb085d4956` |

The run-local lock script (`qemu-lock.sh`, `ec1ae3211bbb7a862780fea7581a26a3ee84d4f07b13ce60b7ff9af3008b88f8`) and the following offline tests did execute:

| Offline input | SHA-256 |
| --- | --- |
| `qemu-graphics-harness-regression.tcl` | `b93e8da176f26edfa55aad4561a802bfde0daf4c43e16460bbea210fa92b4f79` |
| `qemu-graphics-settings-recovery-regression.py` | `0b644b34374187fb37ff72409d98ace60b24e867ae02915252b5478a13081203` |
| `qemu-graphics-capture-timeout-regression.tcl` | `27d1c4635069793fc6f3b798d9a9dfe70b43b0330cf777339bf4397b132cab1f` |
| `qemu-graphics-timeout-regression.tcl` | `8514431e0cb58ebf26f3e4480b28d95f48014b57d553e843a339a7623966b277` |
| `qemu-graphics-settings-relaunch-regression.tcl` | `0c76102508a481ff28dfd7d8e3aa1df9e8f8e89b84a7133722b2fc630c347632` |
| `settings-render-trace-regression.sh` | `0a03c32fd66663f897e4e58bb280c6be7b44fa30d4fb627e83f58d9096002361` |
| Helper read by the failing offline regression | `b01e9ade55f60f130ced836c421d525da64f9131a493dc0ec5425eb4869ad041` |

Before another launcher invocation, repair the run-local offline overlay so the Settings recovery regression reads the preserved diagnostic helper, and provide the missing snapshot-permission regression input. Then revalidate the preflight. The no-retry restriction remains in force; this report does not authorize another launcher invocation. No acceptance QEMU session occurred, so the single 640×480 QEMU authorization remains unused. Keep Phase 2D.3B.5 **INCOMPLETE** until every acceptance gate passes.
