# Phase 2D.3B.5 Diagnostic Acceptance Preflight — 2026-11-16

> **Digest correction:** The expected digest in the initial acceptance prompt was mistyped. The authoritative digest is `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c`, which matches the fresh snapshot and verified baseline. The earlier stop was caused by that instruction typo; it was not a snapshot integrity failure. The subsequent resume attempt is recorded in [the resume report](phase2d3b5-acceptance-resume-attempt-2026-11-16.md).

**Phase status: INCOMPLETE.** The diagnostic-enabled harness was prepared and its offline checks passed, but the acceptance run was stopped before launch because the required `qemu-graphics.exp` digest did not match the verified November 15 baseline. The authorization for one 640×480 acceptance run remains unused.

## Preflight identity and evidence

Preflight ID: `20261116T202205Z`. Preflight stopped at `2026-11-16T20:42:57Z`. No acceptance launcher was invoked, and no acceptance QEMU guest was started. The Builder VM was started for preflight and remains running with its `seyrix_export` 9p share mounted at handoff.

The evidence root is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261116T202205Z`. Its 103-entry evidence manifest **PASSed**. The host-readable review copy is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261116T202205Z-host-review`; its 106-entry review manifest **PASSed**.

## Snapshot and hash results

The fresh guest snapshot at `/home/builder/phase2d3b5-controlled-acceptance-20261116T202205Z/execution-snapshot` was copied from the verified snapshot used for the November 15 run: `/home/builder/phase2d3b5-controlled-acceptance-20261114T103457Z/execution-snapshot`. Only `tests/qemu-guest-helper.sh` was replaced with the reviewed diagnostic helper. The new snapshot contains 63 files, has no write bits, and its content matches the baseline everywhere except that helper. All 62 other baseline file hashes **PASSed**.

| File or check | Result |
| --- | --- |
| `qemu-guest-helper.sh` | **PASS** — `a3510bf544727c4168ac6e5351f4c980c8b3fa5c15689c59e830b63edf4b0562` |
| `qemu-graphics.sh` | **PASS** — `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08` |
| `qemu-graphics.exp` | **FAIL** — task digest `060083a024ff58c7f6160573e2c089b9b475951e6b40fd4dd22665dd8013c12c`; verified baseline and fresh snapshot `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c` |
| Remaining baseline support files | **PASS** — 62/62 hashes match |
| Snapshot sealing and executable modes | **PASS** — no writable paths; required executables retain their required modes |

The initial snapshot-permission regression rejected a variable-derived snapshot path. The run-local launcher setting was corrected to the concrete fresh snapshot path, and the preserved rerun passed. The launcher was prepared with the new evidence path and passed static path-resolution checks. Its SHA-256 is `709007872e84d145cb8b02b8738f47faa76c710696b4d767041262915a10432e`. It was not invoked.

## Other preflight checks

| Check | Result |
| --- | --- |
| Builder VM and `seyrix_export` 9p mount | **PASS** |
| Builder write access to the fresh evidence directory | **PASS** |
| Conflicting acceptance process or lock owner | **PASS** — none found |
| Candidate ISO identity | **PASS** — SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`, size 1,129,283,584 bytes |
| Six offline regressions | **PASS** — all six passed in the Builder VM |
| Bash syntax, Tcl completeness, and `git diff --check` | **PASS** |
| Launcher path-resolution and snapshot-permissions regressions | **PASS** |

The initial host-side render-trace regression attempt was **INCOMPLETE** because `expect` was unavailable on the host. The complete six-regression suite was then run in the Builder VM, where `expect` is installed, and **PASSed**.

## Acceptance status and resume point

Acceptance stages are **NOT RUN**; there is no `test.log` or stage-results log for this preflight ID. Static inspection of `qemu-graphics.exp` finds 224 `run_serial_check` call sites. Therefore, as a static estimate only, PASS=0, FAIL=0, INCOMPLETE=0 result records, and all 224 call sites are **NOT RUN**; these are not executed stage results. The Settings recovery result and diagnostic predicates are **NOT RUN**. No compositor log was captured, so its byte count and SHA-256 are **NOT AVAILABLE** for this attempt. No ISO rebuild or promotion occurred, and no production code or acceptance-harness source was changed for this preflight.

The first failed gate is the `qemu-graphics.exp` hash mismatch above. The smallest next action is to confirm which digest is intended. After that is resolved, continue with the still-unused single-run authorization, execute the existing full 640×480 TCG/virtio-vga acceptance workflow once, reconcile raw and extracted stage results, and verify the final evidence manifests and compositor-log integrity. Keep Phase 2D.3B.5 **INCOMPLETE** until the entire acceptance matrix passes.
