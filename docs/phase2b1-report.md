# Seyrix-Linux Phase 2B.1 — Stability Hardening Report

**Branch:** `phase2b1-stability-hardening`

**Base:** verified Phase 2B commit `248743f`

**Verification status:** the regression campaign passed, but Phase 2B.1 does **not** meet every completion criterion. LeakSanitizer reports one unresolved Mesa EGL allocation with the normal llvmpipe renderer. The original scheduler panic also remains unexplained and unreproduced.

## Executive summary

- The final image completed 20 independent cold boots with no failed boot or retry. Each run reached serial login, compositor readiness, application launch, clean compositor exit, and clean guest shutdown.
- The compositor completed five repeated window lifecycle cycles, unexpected application termination, window destruction during a pointer grab, and compositor exit/restart. No compositor crash, stale focus/grab, or invalid memory access was observed in those paths.
- Phase 1 serial, Phase 2A Weston, and Phase 2B `start-wayland` reference checks passed. `start-wayland` was also rerun against the exact final image.
- The shutdown path was hardened to disconnect output callbacks before renderer teardown and to follow wlroots' dependency order. The fix addresses a compositor shutdown lifecycle hazard; it is not a fix for the kernel panic.
- The scheduler panic happened before userspace. Its `int3` in `enqueue_task_fair` is similar in broad shape to an upstream QEMU MTTCG stale-code report, but the environments and triggers differ. No causal link to Seyrix was found.
- ASan and UBSan found no invalid access or undefined-behavior diagnostic in the exercised paths. LSan is clean with Pixman. With the normal GLES2/llvmpipe renderer, LSan consistently reports a 368-byte direct allocation through `libEGL_mesa.so.0`; this remains unresolved and is not suppressed.
- QEMU reported `llvmpipe (LLVM 23.1.3, 128 bits)`, a software renderer. No hardware acceleration is claimed.

Because the llvmpipe LSan finding is unresolved, the strict sanitizer completion criterion is not met. The 20-boot and functional regression results demonstrate stability for the tested campaign; they do not prove the original kernel panic is fixed.

## Scope, baseline, and parallel review

Work began from the verified Phase 2B commit `248743f` on `phase2b1-stability-hardening`. The original Phase 2B report, compositor architecture, panic log, QEMU command, and test harness were reviewed before code changes. The Phase 2B evidence remains in `docs/evidence/phase2b/`; this phase's raw evidence is in [`docs/evidence/phase2b1/`](evidence/phase2b1/).

Three Codex subagents performed independent reviews:

1. **Kernel diagnostics** confirmed the panic was pre-userspace, checked the complete trace and QEMU configuration, and identified QEMU issue #4669 as a related but non-conclusive MTTCG stale-INT3 report.
2. **Compositor reliability** reviewed listener lifetime, output removal, window destruction, focus/grab state, and renderer ownership. Its findings informed the narrow shutdown-order and listener-disconnection changes below.
3. **Input and rendering** reviewed keyboard/pointer routing, focus/raise behavior, movement, resize, grabs, cursor evidence, and renderer cleanup. It found functional input coverage but no pixel-based stacking or guest-cursor visibility assertion in the existing screenshot method.

The lead retained exclusive ownership of ISO builds and the sequential integrated QEMU campaign so simultaneous boots could not compete for host time or invalidate measurements.

No Linux kernel code was changed. `start-wayland` remains the Weston reference session, and `start-seyrix` remains the manual development launcher. Phase 2C was not started.

## Scheduler panic investigation

### Original failure and environment

The original serial capture records the failure at **1.004813 seconds**, before a serial login prompt or userspace initialization. The guest reports Linux `7.2.11-arch1-1 #1 PREEMPT(full)` with SMP enabled. The host was WSL2 (`6.18.54.1-microsoft-standard-WSL2`) running QEMU `11.1.2-1`, without KVM. The original launch used q35, TCG, `-cpu qemu64`, `-smp 2`, 4 GiB RAM, OVMF UEFI, virtio-vga, QEMU xHCI, USB keyboard/tablet, GTK on WSLg with `gl=off`, and a read-only ISO. The exact command and package metadata are preserved under [`panic-original/`](evidence/phase2b1/panic-original/).

The exception was `Oops: int3: 0000 [#1] SMP NOPTI`, on CPU 1, PID 34 (`kworker/1:1`), workqueue `events hrtimer_hres_workfn`. The faulting instruction was `enqueue_task_fair+0xb87/0xc60`, reached while waking a task from the hrtimer/static-key work path. The kernel ended with `Kernel panic - not syncing: Fatal exception in interrupt`.

Complete recorded call trace:

```text
enqueue_task_fair+0xb87/0xc60
enqueue_task+0x84/0x180
ttwu_do_activate+0x6e/0x180
try_to_wake_up+0x294/0x820
? hrtimer_run_queues+0x146/0x180
__mutex_unlock_slowpath.isra.0+0x113/0x270
? static_key_enable_cpuslocked+0x61/0x90
static_key_enable+0x1f/0x30
process_one_work+0x19f/0x370
worker_thread+0x1b1/0x310
? __pfx_worker_thread+0x10/0x10
kthread+0xe4/0x120
? __pfx_kthread+0x10/0x10
ret_from_fork+0x2a7/0x330
? __pfx_kthread+0x10/0x10
ret_from_fork_asm+0x1a/0x30
```

Recorded registers and instruction bytes:

```text
RIP     0010:enqueue_task_fair+0xb87/0xc60
Code    ... e9 f9 f5 ff ff 0f <1f> 44 00 00 48 81 bb ...
RSP     0018:ffffcf1700133d00     EFLAGS 00000047
RAX     0000000000000000          RBX    ffff8b88c0324f80
RCX     0000000000000001          RDX    0000000000000003
RSI     0000000000000000          RDI    ffff8b893bc32f80
RBP     ffff8b88c0325000          R08    ffffffffa7dd0e00
R09     ffff8b893bc330c8          R10    ffff8b88c0890090
R11     0000000000000000          R12    0000000000000000
R13     0000000000000001          R14    ffff8b893bc33080
R15     ffff8b893bc32f80
FS      0000000000000000          GS     ffff8b8990e2a000
knlGS   0000000000000000          CS     0010
DS      0000                      ES     0000
CR0     0000000080050033          CR2    0000000000000000
CR3     0000000014e22000          CR4    00000000000006f0
```

At the saved RIP, the byte dump shows the second byte of a multi-byte NOP (`0f 1f ...`). This, together with an `int3` exception in SMP TCG, is a clue compatible with stale translated code or transient text patching. It is not enough to establish that explanation or identify the writer.

### Mode checks and upstream context

The diagnostic runs recorded in [`qemu-modes/`](evidence/phase2b1/qemu-modes/) passed the functional workflow once each in these configurations:

| Configuration | Result |
| --- | --- |
| 1 vCPU, QEMU TCG default thread mode | Pass |
| 2 vCPU, QEMU TCG `thread=single` | Pass |
| 2 vCPU, QEMU TCG default thread mode | 20/20 final-image cold boots pass |

QEMU documents that MTTCG uses one host thread per vCPU by default where the target supports it; `thread=single` uses round-robin vCPU execution. The original command did not explicitly set `thread=`, so its mode was QEMU's default. The SMP=1 and `thread=single` checks show those configurations can boot, but one run per mode is not enough to identify a mode-specific cause.

[QEMU issue #4669](https://gitlab.com/qemu-project/qemu/-/work_items/4669) reports stale translated instructions and an `int3` during guest kernel text patching under MTTCG. Its reported environment used QEMU 10.2.1/11.1.50, a Linux 7.3-rc5 ftrace workload, and four vCPUs. That differs materially from this QEMU 11.1.2 / Linux 7.2.11 boot and does not establish a match. [QEMU's MTTCG documentation](https://www.qemu.org/docs/master/devel/multi-thread-tcg.html) describes the relevant execution modes. No upstream report matching this exact `enqueue_task_fair` failure was identified.

The panic did not recur in either the earlier Phase 2B campaign or the 20 independent boots on the final Phase 2B.1 image. Those successful runs lower its observed frequency but do not explain it. There is no evidence implicating compositor code: the failure precedes userspace, and the compositor does not appear in the trace. **Status: unresolved and unreproduced; not claimed fixed.** No kernel workaround was applied.

## Cold-boot reliability campaign

The final campaign used the verified Phase 2B QEMU configuration against the final ISO (SHA-256 `1ed4a50f8ff24fc1e5008f56f2beb48c8fe4ad1dc977e67e797039ceb3b8416b`). Every run launched a new QEMU process and used a fresh copy of OVMF variables. Runs were sequential, and the harness did not retry failed runs. It preserves a separate `test.log`, serial transcript, QEMU command, run metadata, warnings/errors extract, result, and firmware variables for each run. The summary is [`campaign.tsv`](evidence/phase2b1/cold-boots-20-final/campaign.tsv).

Configuration: QEMU 11.1.2-1, q35, TCG default thread mode, qemu64, 2 vCPUs, 4 GiB, OVMF UEFI, read-only ISO, 640×480 virtio-vga, xHCI USB keyboard/tablet, GTK WSLg display with `gl=off` and `show-cursor=on`, user-mode network. The guest kernel was `7.2.11-arch1-1`.

All times are measured from QEMU start. `Compositor ready` is the graphical boot time; `Compositor start` is measured from issuing `start-seyrix` to its readiness marker.

| Boot | Result | Serial login ms | Compositor ready ms | Compositor start ms | App launch | Compositor exit | Guest shutdown |
| --- | --- | ---: | ---: | ---: | --- | --- | --- |
| 01 | Pass | 56,387 | 78,575 | 14,234 | Pass | Pass | Pass |
| 02 | Pass | 56,654 | 80,167 | 15,537 | Pass | Pass | Pass |
| 03 | Pass | 56,969 | 79,609 | 14,697 | Pass | Pass | Pass |
| 04 | Pass | 56,538 | 79,981 | 15,450 | Pass | Pass | Pass |
| 05 | Pass | 56,796 | 80,466 | 15,664 | Pass | Pass | Pass |
| 06 | Pass | 56,167 | 79,600 | 15,511 | Pass | Pass | Pass |
| 07 | Pass | 57,035 | 80,301 | 15,474 | Pass | Pass | Pass |
| 08 | Pass | 56,673 | 79,812 | 15,168 | Pass | Pass | Pass |
| 09 | Pass | 56,799 | 79,863 | 15,223 | Pass | Pass | Pass |
| 10 | Pass | 56,530 | 78,963 | 14,648 | Pass | Pass | Pass |
| 11 | Pass | 56,400 | 78,603 | 14,374 | Pass | Pass | Pass |
| 12 | Pass | 56,800 | 79,953 | 15,178 | Pass | Pass | Pass |
| 13 | Pass | 56,455 | 79,209 | 14,709 | Pass | Pass | Pass |
| 14 | Pass | 56,927 | 80,265 | 15,457 | Pass | Pass | Pass |
| 15 | Pass | 56,555 | 79,324 | 14,811 | Pass | Pass | Pass |
| 16 | Pass | 56,706 | 79,727 | 15,199 | Pass | Pass | Pass |
| 17 | Pass | 57,839 | 80,313 | 14,366 | Pass | Pass | Pass |
| 18 | Pass | 56,432 | 79,057 | 14,443 | Pass | Pass | Pass |
| 19 | Pass | 56,626 | 79,744 | 15,284 | Pass | Pass | Pass |
| 20 | Pass | 58,065 | 81,448 | 15,413 | Pass | Pass | Pass |

| Metric | Min | Median | Mean | Max |
| --- | ---: | ---: | ---: | ---: |
| Boot to serial login | 56,167 ms | 56,663.5 ms | 56,767.7 ms | 58,065 ms |
| Boot to compositor ready | 78,575 ms | 79,778 ms | 79,749.0 ms | 81,448 ms |
| Compositor startup | 14,234 ms | 15,188.5 ms | 15,042.0 ms | 15,664 ms |

The per-run `test.log` files contain the full output. The campaign warning extracts and counts show:

| Observed item | Final campaign count | Detail |
| --- | ---: | --- |
| Arch package database warning | 20 | One `core` sync-database-not-present warning per run during package inspection; no package installation or boot failed. |
| libinput button-debounce timing | 33 | Scheduled expiry was 20–26 ms in the past. |
| libinput event-processing lag | 19 | Reported lag was 23–51 ms. Keyboard and pointer checks passed in all runs. |
| Missing drag-and-drop cursor shapes | Present in each run | `dnd-move`, `dnd-copy`, and `dnd-none` requests could not load from the configured cursor theme. This does not prove the default cursor shape is invisible. |
| `Unknown parameter: ?2004` | 220 | Eleven occurrences per run in the captured test transcript. The keyboard/application checks still passed; the source of this console message was not established. |
| Kernel panic / boot failure | 0 | No cold boot failed or panicked. |

The repeated libinput diagnostics are preserved as observed timing warnings. They may reflect scheduling delay in this WSL/TCG test environment; this campaign does not establish their cause. QEMU screenshot capture omits the live guest cursor, so the saved images do not independently prove the guest cursor's visibility. The GTK display was launched with `show-cursor=on`.

## Compositor lifecycle and input stress

The final normal-renderer run is preserved under [`stress-final-standard/`](evidence/phase2b1/stress-final-standard/). It used QEMU TCG and wlroots' automatic GLES2 selection, which logged `llvmpipe (LLVM 23.1.3, 128 bits)`.

The harness exercised five cycles of opening two application windows, checking focus, moving and resizing windows, and closing them. It also killed an application unexpectedly and recovered, destroyed a window while a pointer interaction was active, verified focus/grab recovery, exited the compositor, restarted it, checked keyboard input after restart, exited it again, and shut the guest down cleanly. No compositor crash or stale pointer grab/focus was observed. Process checks after stress reported one compositor, zero terminal processes, and 58 compositor file descriptors.

Window focus, pointer movement/clicks, move/resize and raise-on-focus behavior were exercised, and PPM screenshots were retained. There is no automated pixel-level overlap oracle, so stacking is supported by the focus/input checks and screenshots rather than a pixel assertion. Pointer events reached the compositor; a QMP screenshot cannot verify the guest cursor image. Rendering remained operational through all tested cycles, and the compositor logged a clean stop without the prior “No outputs remain” shutdown path.

## Sanitizer results

ASan, UBSan, and leak detection were enabled in an instrumented compositor build using `ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1`.

| Renderer / teardown | Result |
| --- | --- |
| llvmpipe, original teardown ordering | Lifecycle checks and compositor exit passed; LSan reported a 368-byte direct leak in one allocation through `libEGL_mesa.so.0`. The harness stopped before restart and guest shutdown because it treats sanitizer output as failure. |
| llvmpipe, partial allocator/renderer reorder | Rejected experiment: LSan reported 31,259 bytes in 154 allocations while the scene still existed. This ordering was not retained. |
| llvmpipe, final full teardown ordering | Lifecycle and compositor stop passed; the same 368-byte direct `libEGL_mesa.so.0` allocation remained. No ASan invalid-access or UBSan runtime diagnostic was found. |
| Pixman, final full teardown ordering | Five-cycle stress, unexpected app kill, active-grab destruction/recovery, compositor exit/restart, post-restart input, sanitizer scan, and clean guest shutdown all passed with `SANITIZER_FINDINGS=none`. |

The final Pixman sanitizer run recorded RSS of 44,988 KiB before stress and 58,912 KiB after stress (+13,924 KiB), with sanitizer instrumentation enabled. The normal llvmpipe path's 368-byte LSan result is not suppressed or labeled a Seyrix-owned leak: the allocation trace resolves into Mesa EGL through wlroots' EGL renderer setup. However, it is still a real unresolved sanitizer finding in a tested path, so criterion 3 is not satisfied. No matching upstream Mesa issue was identified during this investigation.

## Fixes and regression results

### Changes made

- `seyrix_server_finish()` now destroys connected clients and managed child processes before scene/rendering-resource teardown. It destroys the scene and output layout, then allocator and renderer, before destroying the backend and display, following the dependency order in wlroots 0.20.2's [tinywl example](https://sources.debian.org/src/wlroots/0.20.2-1/tinywl/tinywl.c).
- Before backend destruction, `backend_started` is cleared so the final output-destroy callback does not treat orderly teardown as an unexpected loss of the last output.
- `seyrix_output_finish()` disconnects per-output frame, request-state, and commit listeners before renderer destruction. Output destroy now tolerates the scene/layout already being dismantled.
- The QEMU test harness gained explicit SMP/TCG thread and renderer selection, sanitizer launch/check support, startup timing, and repeatable lifecycle stress controls. The new cold-boot driver requires at least 20 runs, uses a unique evidence path and OVMF variables per run, refuses overwrites, and does not retry.
- `start-wayland` and `start-seyrix` behavior and roles were preserved.

### Regression matrix

| Regression | Image / path | Result and measured times | Evidence |
| --- | --- | --- | --- |
| Phase 1 UEFI serial login and shutdown | Phase 1 image | Pass; serial login 52,133 ms; clean shutdown | [`regression-phase1-uefi/`](evidence/phase2b1/regression-phase1-uefi/) |
| Phase 2A Weston session | Phase 2A image | Pass; login 57,783 ms; graphical readiness 77,614 ms; keyboard input and clean shutdown passed | [`regression-phase2a-weston/`](evidence/phase2b1/regression-phase2a-weston/) |
| Phase 2B `start-wayland` reference | Final Phase 2B.1 image | Pass; login 56,162 ms; Weston readiness 75,165 ms; keyboard input, session exit, and clean shutdown passed | [`regression-start-wayland-final/`](evidence/phase2b1/regression-start-wayland-final/) |
| Phase 2B `start-seyrix`, app input/window management, compositor exit and guest shutdown | Final Phase 2B.1 image | Pass in all 20 cold boots; additional stress results above | [`cold-boots-20-final/`](evidence/phase2b1/cold-boots-20-final/) and [`stress-final-standard/`](evidence/phase2b1/stress-final-standard/) |

QEMU reported software rendering through llvmpipe for the normal compositor run. No hardware acceleration is claimed. `start-wayland` remains an independent Weston reference launcher; `start-seyrix` remains a manual development launcher.

## Performance and memory observations

These are the actual measurements recorded by the harness; no FPS or frame-time measurement was available.

| Measure | Observed value |
| --- | --- |
| Final-image boot to serial login | 56,167–58,065 ms; mean 56,767.7 ms |
| Final-image boot to compositor ready | 78,575–81,448 ms; mean 79,749.0 ms |
| `start-seyrix` to compositor-ready marker | 14,234–15,664 ms; mean 15,042.0 ms |
| Normal stress RSS before / after / change | 123,568 / 125,412 / +1,844 KiB |
| Normal stress process and descriptor counts | 1 compositor, 0 terminal processes, 58 compositor FDs after stress |
| Normal renderer | `llvmpipe (LLVM 23.1.3, 128 bits)` |
| Sanitized Pixman RSS before / after / change | 44,988 / 58,912 / +13,924 KiB |
| Sanitized renderer | Pixman software renderer |

The sanitizer RSS delta is reported separately because instrumentation changes memory use. Neither renderer result implies hardware acceleration.

## Acceptance status and remaining risks

| Completion criterion | Status |
| --- | --- |
| 20 independent cold boots all succeed | **Pass:** 20/20, no retries |
| Compositor lifecycle stress passes | **Pass for exercised paths:** no crash or stale focus/grab; stacking and cursor visibility lack a pixel-level assertion |
| No unresolved sanitizer failures in tested paths | **Not met:** one 368-byte llvmpipe/Mesa EGL LSan allocation remains; Pixman sanitizer path is clean |
| Phase 1 and Phase 2A regressions pass | **Pass** |
| Scheduler panic investigated and resolution status documented | **Investigated; unresolved and unreproduced. No fix claimed.** |

The campaign therefore demonstrates successful cold-boot, lifecycle, and historical regression behavior, but Phase 2B.1 does not qualify as a full acceptance pass while the llvmpipe LeakSanitizer finding remains unresolved. The kernel scheduler panic is separately still unexplained. The input timing and missing drag-cursor-shape warnings also remain observations to track.

## Reproduction commands

Run these from the repository in the Arch WSL environment used for the evidence. Set `ISO` to the final ISO path recorded in `build-final/iso.sha256`.

```bash
ISO=/path/to/seyrix-phase2b-2026.10.25-x86_64.iso

# Standard Seyrix graphical session
SEYRIX_QEMU_WORKFLOW=phase2b tests/qemu-graphics.sh "$ISO"

# SMP=1 and SMP=2 with single-threaded TCG mode
SEYRIX_QEMU_WORKFLOW=phase2b SEYRIX_QEMU_SMP=1 tests/qemu-graphics.sh "$ISO"
SEYRIX_QEMU_WORKFLOW=phase2b SEYRIX_QEMU_SMP=2 \
  SEYRIX_QEMU_TCG_THREAD=single tests/qemu-graphics.sh "$ISO"

# Lifecycle stress on the standard renderer
SEYRIX_QEMU_WORKFLOW=phase2b SEYRIX_QEMU_STRESS=1 \
  tests/qemu-graphics.sh "$ISO"

# Instrumented Pixman path (clean ASan/UBSan/LSan run)
SEYRIX_QEMU_WORKFLOW=phase2b SEYRIX_QEMU_STRESS=1 \
  SEYRIX_QEMU_SANITIZERS=1 SEYRIX_QEMU_RENDERER=pixman \
  tests/qemu-graphics.sh "$ISO"

# Twenty independent, sequential cold boots; evidence path must be new
tests/qemu-cold-boot-campaign.sh "$ISO" /path/to/new-evidence-dir 20
```

The original scheduler-panic launch and the captured failure are preserved in [`panic-original/`](evidence/phase2b1/panic-original/). Repeating that command does not guarantee reproduction.

## Evidence and integrity

- [`panic-original/`](evidence/phase2b1/panic-original/) — full serial panic, original QEMU command, and environment metadata.
- [`qemu-modes/`](evidence/phase2b1/qemu-modes/) — SMP=1 default and SMP=2 `thread=single` diagnostic runs.
- [`cold-boots-20-final/`](evidence/phase2b1/cold-boots-20-final/) — all 20 isolated boots, per-run logs/results, warning extracts, firmware variables, and campaign TSV.
- [`stress-final-standard/`](evidence/phase2b1/stress-final-standard/) — final normal-renderer lifecycle stress and memory/process measurements.
- [`sanitizer-stress-full-order/`](evidence/phase2b1/sanitizer-stress-full-order/) and [`sanitizer-stress-pixman-full-order/`](evidence/phase2b1/sanitizer-stress-pixman-full-order/) — final teardown sanitizer runs and their diagnostics.
- [`regression-phase1-uefi/`](evidence/phase2b1/regression-phase1-uefi/), [`regression-phase2a-weston/`](evidence/phase2b1/regression-phase2a-weston/), and [`regression-start-wayland-final/`](evidence/phase2b1/regression-start-wayland-final/) — historical and final-image reference-session checks.
- [`build-final/`](evidence/phase2b1/build-final/) — final ISO metadata and checksum. The ISO is retained in the Arch WSL build cache and is not committed.
- [`SHA256SUMS`](evidence/phase2b1/SHA256SUMS) — integrity manifest for the preserved Phase 2B.1 evidence files.

The full set of PPM screen dumps and per-run `OVMF_VARS.fd` copies remains in the evidence tree and is covered by `SHA256SUMS` and each screenshot manifest. These generated binary artifacts total about 583 MB and are excluded from Git; the run logs, measurements, metadata, and checksums are committed.

No changes were pushed or merged.
