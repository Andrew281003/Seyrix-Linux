# Phase 2D.3B.5 Controlled Acceptance Attempt — 2026-11-13

**Status: INCOMPLETE.** The launch wrapper and shell driver were invoked once. The driver stopped in its support-file preflight before Expect or QEMU. No acceptance stage ran, and no retry was made.

## 1. Harness provenance and ISO identity

The five audited runtime files matched their recorded SHA-256 values in the immutable execution snapshot:

- `tests/qemu-graphics.exp`: `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c`
- `tests/qemu-graphics.sh`: `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08`
- `tests/qemu-guest-helper.sh`: `b01e9ade55f60f130ced836c421d525da64f9131a493dc0ec5425eb4869ad041`
- `tests/qemu-retain-compositor-log.sh`: `4e01c2a09074e12227eb55683cb33b50fba4a8c9611035bd5f7bcb61b7244cd2`
- `tests/qemu-graphics-harness-regression.tcl`: `b93e8da176f26edfa55aad4561a802bfde0daf4c43e16460bbea210fa92b4f79`

The launch wrapper SHA-256 was `86a9737fc989512c888b6d6c1790a73497af647210914f34b71923cd662b2fc4`. The executed lock script SHA-256 was `ec1ae3211bbb7a862780fea7581a26a3ee84d4f07b13ce60b7ff9af3008b88f8`. The shell driver, invoked via Bash and stopped in preflight, had SHA-256 `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08`. Its sourced `qemu-graphics-preflight.sh` had SHA-256 `ee6011bf2ba05eed0288e7b2347f34a435a7c2f74ab3973d82e72ccb085d4956`. The sibling Expect script, guest helper, and log retainer were not invoked.

The candidate ISO source and isolated copy both matched SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`, size 1,129,283,584 bytes, before and after the invocation. QEMU did not boot the ISO.

## 2. Preflight and first failure

The builder worktree was `/home/builder/Seyrix-Linux`. The five audited hashes and lock-script hash matched. The process-conflict scans were clear. All nine offline regression, syntax, and diff checks passed. The corrected audited copies of the three stale regression tests ran in an isolated overlay; the builder’s test copies remained unchanged.

The driver then reported:

```text
Required QEMU harness script is not executable: /home/builder/phase2d3b5-controlled-acceptance-20261113T231200Z/execution-snapshot/tests/qemu-graphics.sh
```

The source file mode was `0644`; the read-only snapshot had mode `0444`. The snapshot was sealed before the required executable bits were set. The driver exited 1 before Expect or QEMU.

## 3. Stage counts and required observations

- QEMU acceptance sessions: **0**
- Stage begins/results: **0 / 0**
- PASS / FAIL / INCOMPLETE: **0 / 0 / 0**
- Acceptance stages: **all NOT RUN**
- Super+Tab MRU assertion, Settings categories, IPC/lifecycle, restart behavior: **NOT RUN**
- Six-attempt supervisor recovery, PID identity, retry timing: **NOT RUN**
- Guest compositor-log capture and integrity verification: **NOT ATTEMPTED**; the recovery gate was never reached
- Unexpected helper exit: **NOT OBSERVABLE**; no guest started
- Raw/extracted stage reconciliation: **NOT RUN**; no `test.log` or `stage-results.log` exists

The failed launcher preflight is not an acceptance test-stage failure. No retry occurred.

## 4. Evidence

Evidence root:

`/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261113T231200Z`

Its 167-entry evidence manifest was verified on the host. The builder worktree status, Settings source hash, and ISO source identity were unchanged. No production source, builder test copy, or ISO was modified.

## 5. Remaining requirements and next action

Set executable bits on the required scripts in a fresh isolated snapshot **before** making it read-only, then reverify the five content hashes. A new controlled 640×480 run must still pass all functional, IPC/lifecycle, restart, supervisor-recovery, compositor-log integrity, and stage-reconciliation checks.

No automatic retry was made. Phase 2D.3B.5 remains **INCOMPLETE**.
