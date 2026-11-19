# Phase 2D.3B.5 Snapshot Permission Correction — 2026-11-14

**Status: INCOMPLETE.** The sealed snapshot permissions and offline regression were verified, but no QEMU acceptance session ran. One launch-wrapper invocation exited before its preflight could start because its evidence directory was not writable by the builder account. No automatic retry was made.

## Snapshot preparation and permissions

The existing sequence copied `tests/` with `cp -a` and then removed write bits recursively. It did not restore execute permissions before sealing, leaving `qemu-graphics.sh` at `0444`. The corrected fresh snapshot is `/home/builder/phase2d3b5-controlled-acceptance-20261114T103457Z/execution-snapshot`.

The launch wrapper is outside the snapshot and is directly executed by `qemu-lock.sh`. In the snapshot, `qemu-graphics.sh` is passed to Bash by `xvfb-run`; the driver invokes `qemu-lock.sh` through Bash, sources `qemu-graphics-preflight.sh`, and invokes its sibling Expect file with `expect`. Expect reads the sibling `qemu-guest-helper.sh`, which is run inside the guest with Bash. The driver directly invokes `qemu-retain-compositor-log.sh`. The existing graphics preflight also requires execute bits on the driver, Expect file, lock script, and guest helper.

Only those five required runtime scripts received `chmod u+x` before the snapshot was sealed with `chmod -R a-w`:

| Snapshot file | Before sealing | Sealed |
| --- | ---: | ---: |
| `tests/qemu-graphics.sh` | `0744` | `0544` |
| `tests/qemu-graphics.exp` | `0744` | `0544` |
| `tests/qemu-lock.sh` | `0744` | `0544` |
| `tests/qemu-guest-helper.sh` | `0744` | `0544` |
| `tests/qemu-retain-compositor-log.sh` | `0755` | `0555` |

The sourced preflight and `build/retention.sh` were not given execute bits. No writable path remained in the sealed snapshot. The snapshot filesystem is `/` on ext4 with mount options `rw,relatime`; `noexec` is absent.

The new regression, [`qemu-graphics-snapshot-permissions-regression.sh`](../tests/qemu-graphics-snapshot-permissions-regression.sh), checks the wrapper's tag-derived snapshot path, runtime file resolution, execute permissions, read-only sealing, and mount options. The corrected version **PASSed** against the sealed snapshot. Its negative control also **PASSed** by rejecting a fixture after `qemu-graphics.sh` lost its execute bit. The first version of the regression **FAILed** because it compared the wrapper's literal `$artifact_root` instead of resolving the wrapper tag; that test-only defect was fixed, and both attempts are retained in the evidence.

## Hash verification

All five audited content hashes **PASSed** in the sealed snapshot:

| File | SHA-256 |
| --- | --- |
| `tests/qemu-graphics.exp` | `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c` |
| `tests/qemu-graphics.sh` | `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08` |
| `tests/qemu-guest-helper.sh` | `b01e9ade55f60f130ced836c421d525da64f9131a493dc0ec5425eb4869ad041` |
| `tests/qemu-retain-compositor-log.sh` | `4e01c2a09074e12227eb55683cb33b50fba4a8c9611035bd5f7bcb61b7244cd2` |
| `tests/qemu-graphics-harness-regression.tcl` | `b93e8da176f26edfa55aad4561a802bfde0daf4c43e16460bbea210fa92b4f79` |

Additional checks matched: `qemu-lock.sh` `ec1ae3211bbb7a862780fea7581a26a3ee84d4f07b13ce60b7ff9af3008b88f8`; sourced `qemu-graphics-preflight.sh` `ee6011bf2ba05eed0288e7b2347f34a435a7c2f74ab3973d82e72ccb085d4956`.

## Offline verification

The six existing offline regressions **PASSed**: MRU/stop, Settings recovery, capture timeout, general timeout, Settings relaunch, and Settings render trace. Bash syntax, Tcl completeness, and `git diff --check` also **PASSed**. Syntax for the new regression **PASSed**. The initial permission-regression run failed on the test's wrapper-path comparison; after fixing that comparison, the positive and negative controls passed.

## Acceptance launch outcome

The launch wrapper was invoked exactly once through `qemu-lock.sh` and exited 1 before its preflight ran. The host-created evidence root and `preflight/` directory were `0755`; the builder guest saw them as `root:root 0755`, so it could not open `preflight/launch-console.log`. This is the first failed gate. The offline suite and snapshot checks were then performed separately. The acceptance wrapper was not invoked again.

- QEMU acceptance sessions: **0**
- Stage begins/results: **0 / 0**
- PASS / FAIL / INCOMPLETE stage results: **0 / 0 / 0**
- Acceptance stages: **all NOT RUN**
- Settings supervisor recovery: **NOT RUN**
- Guest compositor-log capture: **NOT ATTEMPTED**
- Raw/extracted stage reconciliation: **NOT RUN**

The candidate ISO source remained unchanged at SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`, size 1,129,283,584 bytes. No ISO copy or build was made. The builder worktree HEAD and status matched before and after the offline work. No production source or existing test assertions changed.

Evidence root: `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261114T103457Z`; its 134-entry host-verified manifest **PASSed**. Phase 2D.3B.5 remains **INCOMPLETE**. Any fresh QEMU acceptance launch needs separate authorization; the failed wrapper launch was not retried.
