# Seyrix storage cleanup and retention record

Date: 2026-10-30

This cleanup was performed in the active `Seyrix-Linux-Phase2D2` development
worktree. The protected `Seyrix-Linux` worktree was not modified. Source,
Git state, uncommitted work, project reports, and checked-in test evidence were
preserved.

## Relocated Arch WSL verification

The registered `archlinux` distribution points to `E:\WSL\archlinux` and
boots from that location. The active environment remains registered and was
not reset, moved, or unregistered. Its package inventory contains 347
packages. The following engineering tools were present and callable:

- `archiso` 91-1, `git` 2.56.0-1, `gcc` 16.2.1+r23+gd564253eb6c8-1,
  `make` 4.4.1-3, `meson` 1.12.1-2, `ninja` 1.13.2-3, and `pkgconf` 3.0.10-1.
- `qemu-system-x86` 11.1.2-1 and `edk2-ovmf` 202608-1.
- `wayland` 1.26.0-1, `wayland-protocols` 1.49-1,
  `wlr-protocols` r112.a741f0a-1, and `wlroots0.20` 0.20.2-1.
- `wayland-scanner`, `pkg-config`, `mkarchiso`, and `sha256sum` were
  available.

A bounded Meson/Ninja build of `desktop/protocol-probe` completed in 19
seconds. A headless QEMU/OVMF boot of the retained Phase 2D.2 ISO reached the
`seyrix-dev login:` prompt. A secondary smoke-check expression searched for
the older text `seyrix login:` and returned a false negative; the serial
output itself showed the expected boot and login prompt. The QEMU process
started for this smoke test was stopped after the check. No graphical
acceptance campaign was run.

## Generated artifact cleanup

Before deletion, `/var/tmp/seyrix-linux` used 35,653,816,320 allocated bytes
and `/root/.cache/seyrix-linux` used 142,309,974,016 allocated bytes. The
combined, non-overlapping total was 177,963,790,336 bytes. The
[artifact manifest](storage-cleanup-artifact-manifest.tsv) records each
direct-child path, allocated size, modification time, purpose, retention
action, and process-reference check.

Removed 187 positively identified generated build and test directories from
those roots. Their manifest sizes sum to 176,897,294,336 allocated bytes.
This included obsolete Archiso work/staging and output trees, superseded ISOs,
temporary QEMU and headless test runs, old Meson/Ninja trees, logs, screenshots,
and generated test evidence. The pre-delete process and open-file scan found
no remaining users. No disk images with unique or uncertain data were found in
these artifact roots.

Guest filesystem available space changed from 844,673,974,272 bytes before
cleanup to 1,021,567,029,248 bytes after cleanup, a measured increase of
176,893,054,976 bytes (164.70 GiB). The stable ISO and small root-level logs
remain. The temporary smoke build was removed after its result was recorded
here.

## Retained verified release

The latest successfully verified bootable image is Phase 2D.2:

- Path: `/var/tmp/seyrix-linux/verified/seyrix-phase2d2-2026.10.27-x86_64.iso`
- Size: 1,062,240,256 bytes
- SHA-256: `9ace6c35c5c67f7bb2e4a8b181fe04d587c9cdda7b49df937abe52f190a49fb2`
- Previous full graphical verification is recorded in
  [`phase2d2-final-integration-report.md`](phase2d2-final-integration-report.md).

The latest Phase 2D.3B artifacts were interrupted or failed and are not
treated as a verified release. Their source changes, reports, test summaries,
and 23 failed or incomplete QEMU runs remain in the active worktree; the
preserved Phase 2D.3B generated evidence is 619,713,259 bytes across 1,501
files. No Phase 2D.3B development or new acceptance campaign was started for
this cleanup.

## Windows volume readings

The Windows volume readings below were captured before cleanup and after the
cleanup and retention edits. Values are free bytes; the E: VHDX was not
compacted.

| Volume | Before | After | Measured change |
| --- | ---: | ---: | ---: |
| C: | 482,239,385,600 | 483,569,111,040 | +1,329,725,440 |
| E: | 534,068,908,032 | 534,068,903,936 | -4,096 |

No C: files were deleted by this cleanup; its observed free-space increase is
not attributable to Seyrix cleanup and varied during the work. No Windows E:
space was released: the generated files lived inside the active WSL
`ext4.vhdx`, and compaction was explicitly avoided. Inside Arch, the guest
filesystem gained 176,893,054,976 available bytes as measured above.

## WSL backup and restore-test inventory

All of the following remain present; no WSL registration or backup was
removed. The active distro booted successfully, and the verified VHDX backup
was previously restored and hash-checked:

| Item | Path | Size | Last write UTC | State / use |
| --- | --- | ---: | --- | --- |
| Active development VHDX | `E:\WSL\archlinux\ext4.vhdx` | 270,133,100,544 bytes | 2026-10-30 22:08:01 | In-use by running `archlinux`; preserved |
| Restore-test distribution VHDX | `E:\WSL\RestoreTest-192c652249\archlinux-vhdx-backup-20261030T110736627Z-e7bf6ea789dd4abd9e5ace553c8f445f.vhdx` | 270,133,100,544 bytes | 2026-10-30 16:18:15 | Registered as `archlinux-verify-192c652249`; stopped, no active process reference |
| Verified VHDX backup | `E:\WSL\Backups\archlinux-vhdx-backup-20261030T110736627Z-e7bf6ea789dd4abd9e5ace553c8f445f.vhdx` | 270,133,100,544 bytes | 2026-10-30 11:44:03 | Verified readable; no active process reference |
| Incomplete pre-move tar backup | `E:\WSL\Backups\archlinux-pre-move-20261030T011415.tar.gz` | 54,060,933,120 bytes | 2026-10-30 01:39:11 | Incomplete archive; no active process reference |

The restore-test copy and verified backup have matching sizes and a previously
verified matching SHA-256 (`5CB0D2AFD3BFAC4ED6CEA17F12E5A82816EE17B97F2CBD7CAB56ADBC33B519B3`).
The tar archive is the incomplete 54 GB backup.
Potential space recovery is 270,133,100,544 bytes if the restore-test
distribution is unregistered, another 270,133,100,544 bytes if the verified
backup is deleted, and 54,060,933,120 bytes if the incomplete tar is deleted.
Those operations need separate approval and were not performed.

## Older Seyrix kernel project

`C:\Users\quake\Desktop\Coding\Seyrix\build\verification` and its
containing `build` directory are absent. The older kernel repository was not
changed. No physical-device backups, unique disk images, or files of uncertain
ownership were deleted.

## Retention safeguards implemented

The shared helper [`../build/retention.sh`](../build/retention.sh) is wired
into all six Archiso builders, both QEMU harnesses, and the headless
integration runner. [`build-guide.md`](build-guide.md) documents the policy.

- One lock serializes builds and test runs. Managed outputs use a unique,
  direct-child staging directory under `/var/tmp/seyrix-linux` by default,
  which is backed by the E: WSL virtual disk.
- Preflight enforces a 40 GiB artifact budget, at least 30 GiB free space,
  and reserves 16 GiB for builds or 4 GiB for QEMU/headless runs. Limits are
  configurable in bytes through `SEYRIX_RETENTION_MAX_BYTES`,
  `SEYRIX_RETENTION_MIN_FREE_BYTES`,
  `SEYRIX_RETENTION_BUILD_RESERVE_BYTES`, and
  `SEYRIX_RETENTION_QEMU_RESERVE_BYTES`. It refuses to start when the budget
  or free-space reserve cannot be met.
- Build and run markers distinguish active, successful, failed, and
  interrupted outputs. Cleanup checks markers, process IDs, open file handles,
  and working directories. It removes only managed paths outside Git
  worktrees.
- A successful QEMU verification promotes the ISO into `verified/`, checks
  its SHA-256, updates `latest.txt`, and keeps one verified ISO by default.
  Successful staging and temporary test output are removed; a compact run
  summary remains under `history/`. Failed and interrupted evidence is kept.
- Set `SEYRIX_RETENTION_KEEP_ARTIFACTS=1` to retain successful build and test
  outputs while debugging. Custom output inside a Git worktree is never
  automatically cleaned.

Static Bash syntax checks passed on the edited scripts. The bounded build and
headless boot smoke above were the only runtime validation; no large build or
full graphical acceptance campaign was run.

## Final stabilization addendum — 2026-10-31

This addendum supersedes the earlier statement that no full graphical campaign had been run. A bounded graphical campaign ran against the E-backed Arch filesystem. The 800×600 candidate reached the real Seyrix login prompt, launched the actual Settings application, passed live read-only IPC and search checks, then failed its first visible category transition. Two 640×480 runs failed the guest ttyS0 getty stage. Phase 2D.3B remains INCOMPLETE.

The managed-artifact root measured 29,430,091,776 bytes after the campaign, below the 42,949,672,960-byte budget. The final /var/tmp filesystem reading on /dev/sdd was 32,951,980,032 bytes used and 993,156,841,472 bytes available. The 30 GiB free-space threshold and QEMU reserve were maintained. Earlier measurements were 25,055,256,576 bytes of artifact use before the major build and about 30 GiB filesystem use during ISO generation.

The retention fixture suite was rerun and passed 14/14 checks. It verified serialization, budget and free-space rejection, build/QEMU reserves, safe-path rejection for Git worktrees, Windows paths, and the WSL VHDX, active-build protection, retention of failed/interrupted evidence, successful-only ISO promotion, pointer stability after failed verification, and both keep flags. The raw log is docs/evidence/phase2d3b/retention-validation-20261031.log. Fixtures were small and temporary.

The failed candidate ISO was not promoted. latest.txt continues to point to the earlier verified Phase 2D.3B image (SHA-256 274d6064e3afcdd0bc48248c178a96419370635a1289741825b199ffc3fd01ca). The Phase 2D.2 ISO remains preserved (SHA-256 9ace6c35c5c67f7bb2e4a8b181fe04d587c9cdda7b49df937abe52f190a49fb2). The failed candidate and its run evidence remain under /var/tmp/seyrix-linux; the active and backup WSL virtual disks were not modified.

The final full acceptance matrix and evidence paths are recorded in docs/phase2d3b-report.md. No destructive cleanup was run against real release artifacts.
