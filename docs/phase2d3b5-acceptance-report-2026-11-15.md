# Phase 2D.3B.5 Evidence Ownership Correction — 2026-11-15

**Current phase status: INCOMPLETE.** This report first records the November 15 readiness checkpoint, when readiness was **READY** for a separately authorized 640×480 run and acceptance stages were still **NOT RUN**. The later one-run acceptance result is recorded at the end of this report.

The first report below records the host-side ownership correction and what was known at that time. The follow-up preserves that history and records the later in-VM verification.

This report continues the [November 14 snapshot permission report](phase2d3b5-acceptance-report-2026-11-14.md); that historical evidence bundle was left unchanged.

## Ownership root cause

`run-builder-vm.sh` bind-mounts the host export directory into a rootless Podman container at `/host-export`. QEMU exports that path over 9p with mount tag `seyrix_export` and `security_model=none`. The guest mounts that tag at `/mnt/seyrix-export`; the artifact export helper creates destination directories with mode `0755`.

The rootless Podman UID and GID maps are `0 -> host 1000` and `1 -> host 524288` (length 65536). Therefore, a host-created directory owned by host UID:GID `1000:1000` appears as `0:0` in the QEMU container. With 9p `security_model=none`, the guest sees that container ownership as `root:root`; the original `0755` mode is retained. The November 14 report records that this prevented the builder from opening `preflight/launch-console.log`.

The builder account is UID:GID `1000:1000`. The VM setup records the account IDs dynamically, prior launch commands enter over SSH as `builder`, and `qemu-lock.sh` executes the target command without changing its identity. Guest-created export files appear on the host as `525287:525287`, which maps back to `1000:1000` through the Podman namespace.

## Fresh run directory correction

Created `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261115T155344Z` and its `preflight/` directory. Both are mode `0700` and owned by UID:GID `1000:1000` in the QEMU container's rootless user namespace (host numeric owner `525287:525287`). There are no extended ACL entries. The host user cannot access these directories directly; the intended namespace identity can. No historical evidence directory was changed.

The host-side Podman user-namespace probe ran as UID:GID `1000:1000` through a bind-mounted alias and passed create, write, read, and delete in both the run root and `preflight/`. Both temporary probe files were removed. The record is retained at `preflight/builder-identity-permission-probe-userns.txt` in the fresh directory.

**Initial report, before the VM follow-up:** This host-side probe did not run inside the builder VM. It verifies the host filesystem permissions under the mapped identity, but it does not verify the guest's 9p mount or actual launcher log initialization.

## Initial report: offline regression and verification (before VM follow-up)

The new [evidence-permissions regression](../tests/qemu-graphics-evidence-permissions-regression.sh) **PASSed** under `podman unshare bash ...`. Its negative controls rejected a root-owned, non-writable evidence root; a non-writable `preflight/`; the wrong launcher UID/GID; an evidence root outside the designated run directory; and a `preflight/` symlink escaping that directory. Its positive case initialized, wrote, and read the launch log as UID:GID `1000:1000`.

At the time of the initial report, the rerun of the existing six-test offline suite was **INCOMPLETE**. Four checks passed (MRU/stop, capture timeout, general timeout, and Settings relaunch). The Settings recovery and render-trace checks could not validate against this desktop checkout because it lacks builder-worktree files `desktop/session-supervisor/main.c` and `build/build-phase2d3b-iso.sh`. These are source/environment mismatches; they do not establish product regressions. The November 14 report records all six checks passing in the builder worktree at that time.

| Check | Result |
| --- | --- |
| New evidence ownership regression | **PASS** |
| Existing offline suite rerun | **INCOMPLETE** — 4/6 passed; 2 could not validate against this checkout |
| Bash syntax | **PASS** — 25 scripts |
| Tcl/Expect source completeness | **PASS** — 9 files |
| `git diff --check` | **PASS** |
| Sealed execution snapshot write-bit check | **PASS** — no writable paths found |
| November 14 evidence manifest | **PASS** — all 134 entries |
| Recorded harness, lock, and preflight hashes | **PASS** |
| Candidate ISO identity | **PASS** — SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`; 1,129,283,584 bytes |

The existing snapshot's content hashes and 134-entry evidence manifest were reverified. Its filesystem is mounted `rw` on Btrfs, while the snapshot paths themselves remain sealed without write bits. The correction only changed ownership and permissions in the fresh evidence directory; it did not change the existing snapshot or ISO.

## Acceptance readiness at the initial report

No QEMU process was started, no full acceptance wrapper ran, and no ISO build or promotion occurred. The builder VM was not running. Acceptance stages remain **NOT RUN**.

The launcher is **NOT READY** for a separately authorized acceptance run until the builder identity successfully creates, writes, reads, and deletes a probe inside the VM and the actual fresh launcher initializes `preflight/launch-console.log` there. Phase 2D.3B.5 remains **INCOMPLETE**.

## Follow-up: builder VM readiness verification — 2026-11-15

The existing builder VM was started for infrastructure verification. At boot, `/mnt/seyrix-export` was an unmounted directory and `/etc/fstab` contained no 9p entry. The 9p module was available. The share was mounted temporarily, without changing guest configuration:

```sh
sudo mount -t 9p -o trans=virtio,version=9p2000.L seyrix_export /mnt/seyrix-export
```

The resulting mount was `seyrix_export` at `/mnt/seyrix-export`, type `9p`, with options `rw,relatime,access=client,trans=virtio`. The `builder` account's effective UID:GID is `1000:1000` (groups `builder` and `wheel`). The mount root is `root:root 0755`, readable/searchable but not writable by `builder`. The fresh run directory and its `preflight/` subdirectory are both `1000:1000 0700`; builder read, write, and search checks **PASS** for both.

Actual guest probes as `builder` **PASSed** create, write, read, and delete in both directories. Each probe file was owned by `1000:1000` with mode `0600`; both were removed, and the final scan found no temporary probes. The detailed record is `preflight/guest-write-probes.txt`.

### Launcher log initialization

The archived November 14 `launch-once.sh` SHA-256 (`68062376d108ed0453523c4b2719ddb741503e103bacd9e9de9a7528a35b8f07`) and its 134-entry evidence manifest **PASSed**. In the builder VM, only the launcher's lines 25–26 were exercised as `builder`: creation of the preflight/postrun directories and append redirection to `preflight/launch-console.log`. The full wrapper was not invoked. The log was created, written, and read back successfully; it is owned by `1000:1000`, mode `0644`, 153 bytes, and preserved at `preflight/launch-console.log`.

### Offline regressions and provenance

All six existing offline regressions **PASSed** in `/home/builder/Seyrix-Linux` using the established disposable overlay. The three previously audited corrected test copies were SHA-verified and used without editing the builder worktree. The evidence ownership and snapshot permission regressions also **PASSed**. Bash syntax checks **PASSed** for 33 scripts; Tcl completeness and `git diff --check` **PASSed**. Results and logs are in `preflight/offline-check-status.tsv` and the adjacent `regression-*.log` files.

The sealed snapshot at `/home/builder/phase2d3b5-controlled-acceptance-20261114T103457Z/execution-snapshot` **PASSed** its recorded harness hash checks. The `qemu-lock.sh` hash is `ec1ae3211bbb7a862780fea7581a26a3ee84d4f07b13ce60b7ff9af3008b88f8`; the graphics preflight hash is `ee6011bf2ba05eed0288e7b2347f34a435a7c2f74ab3973d82e72ccb085d4956`. Required runtime scripts retain modes `0544`/`0555`, no snapshot path has write bits, and the snapshot filesystem is ext4 mounted `rw,relatime` without `noexec`.

The candidate ISO at `/mnt/seyrix-export/phase2d3b5-gcc16-20261111T164352Z/out/seyrix-phase2d3b-2026.11.11-x86_64.iso` **PASSed** SHA-256 verification: `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5` (1,129,283,584 bytes).

The builder worktree stayed unchanged: HEAD remained `c3b1eeb886cb0caf79da6b51015b3fe986590943`, and before/after status, tracked-diff, and untracked-content fingerprints matched. Production/build source hashes also matched before and after: `desktop/session-supervisor/main.c` (`2aba687152236fe1742fce2cace5ecf1a17e81fb05b401af97f877eee0b0a94b`), `build/build-phase2d3b-iso.sh` (`0d345eab2ae3fa9845ad3955ceae962ce0fa7c7db9e75a2ee11cefc2f90ebafe`), and `desktop/settings/src/settings-window.c` (`470f173d4a1dcde489db36a2a514c7553cd721a2b17e6aee734e5f8b40ca1948`).

### Host evidence access

The guest evidence manifest verifies 38 preflight files. Because the original guest-owned directories are intentionally private to the mapped builder identity, a separate host-review copy is at `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261115T155344Z-host-review`. The copy is mapped to host UID:GID `1000:1000`; its root and `preflight/` are mode `0700`, and no copied path is world-writable. The guest manifest **PASSed** with 38 entries and the complete host package manifest **PASSed** with 51 entries. The original guest directories remain owned by mapped host UID:GID `525287:525287`, mode `0700`; their ownership and permissions were not changed.

### Readiness decision

**READY** for a separately authorized 640×480 acceptance run, with the builder VM and its 9p export mount active. The guest has no persistent 9p mount entry, so the mount command above must be run again after any VM restart. The builder VM remains running with the temporary mount active at handoff.

No Seyrix acceptance QEMU guest was started, no full acceptance wrapper was invoked, and no acceptance retry occurred. Acceptance stages remain **NOT RUN**; no ISO rebuild or promotion occurred. Phase 2D.3B.5 remains **INCOMPLETE**.

## Controlled 640×480 acceptance — 2026-11-15 (one run; phase incomplete)

The separately authorized run used ID `20261115T183456Z`. The run wrapper started at `2026-11-15T18:39:27Z`; acceptance preflight began at `18:41:56Z`, and QEMU launched at approximately `18:41:59Z`. Exactly one runner invocation occurred. It used TCG, virtio-vga, and 640×480. There was no retry or second QEMU acceptance session.

The executed evidence-local launcher SHA-256 was `2d940d03d96b3455fff4add0202ac466f840cebfaf930d3916dafe1377a5ad94`. Compared with the archived November 14 launcher, its run tag and evidence paths were updated, `snapshot_root` was pointed at the already sealed and verified snapshot instead of creating a new snapshot, and the offline suite was conditionally reused only when its recorded effective inputs matched the readiness hashes. The acceptance-stage driver and Expect script came from the unchanged sealed snapshot; no stage assertions, deadlines, or acceptance flow were changed.

### Preflight and provenance

Preflight **PASS**: the builder VM was running, and `/mnt/seyrix-export` was mounted as the 9p share tagged `seyrix_export`. The fresh evidence root and `preflight/` were builder-owned, mode `0700`; builder read/write/search checks and create/write/read/delete probes passed. The host Podman user namespace was used to allocate the directories. The sealed execution snapshot at `/home/builder/phase2d3b5-controlled-acceptance-20261114T103457Z/execution-snapshot` passed recorded SHA-256, read-only mode, required `0544`/`0555` execution modes, and launcher-resolution checks. Its ext4 filesystem was mounted `rw,relatime`; the snapshot paths themselves had no write bits. The candidate ISO remained SHA-256 `45ee62d7f331e75b28a9f19ceddfde5e350d9ff59fe87cbd95faef73773322b5`, 1,129,283,584 bytes, before and after the run. No rebuild, promotion, or production source change occurred.

Effective offline regression inputs matched the readiness checkpoint hashes, so the six offline regressions were not rerun; their prior readiness **PASS** results were reused. Current Bash syntax, Tcl completeness, diff check, and snapshot-permissions regression **PASSed**. The executed launcher SHA-256 was `2d940d03d96b3455fff4add0202ac466f840cebfaf930d3916dafe1377a5ad94`. Verified sealed snapshot/support hashes:

| File | SHA-256 |
| --- | --- |
| `qemu-graphics.exp` | `060083a024ff58c7f6160573e2c089b9e475951e6b40fd4dd22665dd8013c12c` |
| `qemu-graphics.sh` | `614d34b287e678e5548bfde0035d6fd1c93fab7c9ec3356c52cf60a5348bee08` |
| `qemu-guest-helper.sh` | `b01e9ade55f60f130ced836c421d525da64f9131a493dc0ec5425eb4869ad041` |
| `qemu-retain-compositor-log.sh` | `4e01c2a09074e12227eb55683cb33b50fba4a8c9611035bd5f7bcb61b7244cd2` |
| `qemu-graphics-harness-regression.tcl` | `b93e8da176f26edfa55aad4561a802bfde0daf4c43e16460bbea210fa92b4f79` |
| `qemu-lock.sh` | `ec1ae3211bbb7a862780fea7581a26a3ee84d4f07b13ce60b7ff9af3008b88f8` |
| `qemu-graphics-preflight.sh` | `ee6011bf2ba05eed0288e7b2347f34a435a7c2f74ab3973d82e72ccb085d4956` |
| snapshot permission regression | `0851186a8b8056757c108d3d00d52867d464c142d6f798ed5ef2c631ea09a146` |

The run executed `qemu-graphics.sh`, `qemu-graphics.exp`, `qemu-guest-helper.sh`, `qemu-retain-compositor-log.sh`, `qemu-lock.sh`, and `qemu-graphics-preflight.sh` from this verified snapshot. The `qemu-graphics-harness-regression.tcl` hash above was verified as snapshot provenance but its offline regression was not executed because the effective regression inputs matched readiness. The snapshot-permissions regression was executed and **PASSed**.

### Acceptance results

Stage reconciliation **PASSed**: 203 stage begins, 202 results, comprising 201 **PASS**, 1 **FAIL**, and 0 **INCOMPLETE**. Twenty-one static runner call sites remain **NOT RUN**. No stage began after the first definitive failure.

The first failure began at `2026-11-15T19:06:11Z` and ended after 4,212 ms: “kill six consecutive manager-launched Settings PIDs without issuing another OPEN and verify bounded retry exhaustion.” The helper reported `settings-retry-budget-launch-2-invalid-pid-8248`. Attempt 1 validated PID 8086, parent manager 7881, count 1, and observed SIGKILL exit. The retry was scheduled for 1,000 ms and observed after 1,007 ms. Attempt 2 logged launch PID 8248, then strict helper validation rejected it. The six-PID sequence and bounded exhaustion were not established. The retained compositor log contains that launch record but no matching `SEYRIX_SETTINGS event=start pid=8248` before failure. Since the generic helper error does not identify its failed predicate, that log gap is an investigation lead, not an established root cause.

Graphical boot/compositor readiness, native Settings and all six categories, authenticated IPC/lifecycle, corrected Super+Tab MRU, window-management and compositor recovery stages that ran before the stop are included in the 201 passing stage results. The supervisor recovery gate **FAILed** as above; later runner call sites did not run.

Compositor-log capture **PASSed integrity**. The helper marker and retained file both recorded 1,819,255 bytes and SHA-256 `3ee5d05ae3a2c84234bbd9b09e031c66d8d5eaf8beb3cffe8a4d58140c660703`; postrun byte-count and hash verification matched. This is not a capture failure.

The guest evidence manifest **PASSed** with 254 entries. The host-readable review copy is `/var/home/quake/VirtualMachines/seyrix-phase2d3b5-builder/export/phase2d3b5-controlled-acceptance-run-20261115T183456Z-host-review`; its package manifest **PASSed** with 258 entries. QEMU exited, and postrun checks found no acceptance QEMU, Expect, or Xvfb process and no lock owner. The wrapper exited 1 because of the acceptance failure.

**Phase 2D.3B.5 remains INCOMPLETE.** Remaining acceptance gates include passing the six-attempt Settings supervisor recovery with distinct PID observations, retry timing, and bounded exhaustion; completing the remaining stages skipped after the failure; and reconciling a fully passing acceptance run. Resume by inspecting the preserved test and compositor logs for why PID 8248 failed strict manager-launched process validation. Do not launch another acceptance session without separate authorization.
