# Seyrix-Linux Phase 2C.2 — Final Acceptance & Reliability

**Decision: INCOMPLETE.** The final ISO passed the Phase 2C functional workflow and 20/20 independent cold boots. Pixman ASan/UBSan/LSan checks passed. The normal Mesa llvmpipe EGL sanitizer run reproduced the historical 368-byte direct allocation and reported additional Mesa/EGL-path allocations. The ownership investigation did not establish whether Seyrix, wlroots, or Mesa owns the allocation. That is a mandatory acceptance blocker.

No compositor production code was changed. The Phase 2D work was not started.

## 1. Baseline and repository state

- The requested baseline was verified: branch `phase2c1-functional-stabilization`, commit `c19ce09d523a2c9d27a32666e73b75dd4df5b407`.
- The `phase2c2-final-acceptance` branch was created from that commit.
- Recent history at the baseline: `c19ce09` (Phase 2C.1 stabilization), `1053127` (pointer probe and QEMU diagnostics), `df85ee5` (Phase 2C behavior), `dd58adb` (pointer evidence documentation), and `12ef1b3` (Phase 2C foundations).
- Seven pre-existing changes under `config/archiso` were preserved without edits or staging. Windows Git reports these as modified symlink entries; the Arch WSL Git view resolves the expected symlinks and does not report them.
- The ISO, build trees, raw QEMU logs, screenshots, and sanitizer logs remain outside Git under `/root/.cache/seyrix-linux/`. Small manifests and result summaries are included in `docs/evidence/phase2c2/`.

## 2. Mesa EGL allocation investigation

### Runtime and reproduction

The historical report and current reproduction use Mesa `1:26.2.5-1` and wlroots `0.20.2-1`. The current sanitized QEMU guest reported EGL vendor `Mesa Project` and the actual GL renderer `llvmpipe (LLVM 23.1.3, 128 bits)`. The renderer identity came from compositor runtime logs, not from environment variables. The wlroots allocation path is `wlr_egl_create_with_drm_fd`; the guest exposes the GBM platform extensions. Mesa build ID: `f5b4c5531e150775af105a811e633f65e15bbe4f`. wlroots build ID: `2cd3a56136647a9da32b9a682606911aa2498393`.

The sanitizer-enabled compositor run used the Phase 2C QEMU harness with `SEYRIX_QEMU_SANITIZERS=1`. The llvmpipe run with automatic renderer selection completed its compositor shutdown and then failed at process exit with LeakSanitizer. A separate run explicitly requested GLES2; its runtime log also confirmed llvmpipe. Both full logs are retained in `/root/.cache/seyrix-linux/phase2c2-asan-automatic-20261026/` and `/root/.cache/seyrix-linux/phase2c2-asan-llvmpipe-20261026/`.

Reproduction command shape:

```sh
SEYRIX_QEMU_WORKFLOW=phase2c \
SEYRIX_QEMU_SANITIZERS=1 \
SEYRIX_QEMU_GRAPHICS_DIR=/root/.cache/seyrix-linux/phase2c2-asan-automatic-20261026 \
bash tests/qemu-graphics.sh /root/.cache/seyrix-linux/phase2c2-asan-20261026/out/seyrix-phase2c-2026.10.26-x86_64.iso
```

The reported allocations were:

| LeakSanitizer class | Bytes | Allocations |
| --- | ---: | ---: |
| Direct | 368 | 1 |
| Direct | 32 | 1 |
| Indirect | 28,800 | 150 |
| Indirect | 2,048 | 1 |
| Indirect | 11 | 1 |
| **Total** | **31,259** | **154** |

The historical 368-byte allocation is therefore reproducible on the current source and target renderer. The additional allocations also need ownership review; they are not suppressed or discounted.

The current 368-byte allocation stack is:

```text
calloc (libasan.so.8)
libEGL_mesa.so.0 + 0x1a90b
libEGL_mesa.so.0 + 0x98b9
libwlroots-0.20.so + 0x385ba
libwlroots-0.20.so + 0x394e8
wlr_egl_create_with_drm_fd + 0x39752
wlr_gles2_renderer_create_with_drm_fd + 0x3ffb1
libwlroots-0.20.so + 0x37d9f
seyrix_server_init (desktop/compositor/src/main.c:156)
main (desktop/compositor/src/main.c:340)
```

The full symbolized allocation excerpt is in [mesa-egl-368-stack.txt](evidence/phase2c2/mesa-egl-368-stack.txt). The trace identifies the allocation path, but does not alone prove which layer owns cleanup.

### Initialization, ownership, and teardown audit

`seyrix_server_init` creates the display, backend, renderer, allocator, scene, and output layout in dependency order. The sanitizer stack enters wlroots renderer creation from the Seyrix server initializer. On shutdown, `seyrix_server_finish` destroys clients and Seyrix window/output/input resources, then the scene and output layout, allocator, renderer, backend, and display. The teardown order is appropriate for these wlroots-owned objects. Seyrix does not acquire or directly own an EGL display/context, so no missing Seyrix EGL destruction call was identified. Partial-initialization failure also calls `seyrix_server_finish` from `main`.

Source references: [`seyrix_server_init`](../desktop/compositor/src/main.c#L132), [`seyrix_server_finish`](../desktop/compositor/src/main.c#L271), and [`main`](../desktop/compositor/src/main.c#L337).

No Seyrix-owned defect was demonstrated, and no production change was justified. The presence of Mesa frames is not treated as proof of Mesa ownership.

### Controlled EGL comparison attempts

- A host WSLg GBM control could not initialize GBM and selected no target llvmpipe renderer. It is not evidence about the target guest.
- A host surfaceless EGL control did initialize Mesa llvmpipe and exited without an LSan report. It used the surfaceless platform, so it does not reproduce the compositor’s DRM-fd/GBM path.
- A standalone ASan/UBSan EGL/GBM helper was built and added at `tests/egl-gbm-lifecycle-control.c`. The first diagnostic ISO packaged it without its executable bit; that attempt is preserved and classified as a packaging failure. The corrected diagnostic ISO did execute the helper, but its EGL/GBM initialization sequence failed before it reported a GL renderer or reached a leak result. That QEMU run’s full Phase 2C functionality passed, but the control result is **INCOMPLETE** for ownership.
- The helper’s failure does not establish an upstream leak. A minimal wlroots lifecycle control has not yet been made to run in the same guest.

Control build and invocation used:

```sh
cc -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O1 \
  -fno-omit-frame-pointer -fsanitize=address,undefined \
  tests/egl-gbm-lifecycle-control.c -o seyrix-egl-gbm-lifecycle-control \
  -lEGL -lGLESv2 -lgbm -ldl

# In the temporary QEMU diagnostic guest:
ASAN_OPTIONS=detect_leaks=1 /usr/local/bin/seyrix-egl-gbm-lifecycle-control
```

No matching issue or fix was identified in the Mesa 26.2.5 release notes or the public search performed for this report. The release notes are available at [Mesa 26.2.5 release notes](https://docs.mesa3d.org/relnotes/26.2.5.html). This negative search is not evidence that Mesa owns the allocation or that no upstream report exists. No upstream issue ID or tracking plan can responsibly be recorded yet.

**Ownership conclusion: unresolved.** The leak criterion is not met. The next useful evidence is a working, runtime-verified GBM EGL control and a minimal wlroots/EGL lifecycle comparison using the same guest packages, followed by upstream tracking only if that comparison isolates the allocation to an upstream component.

## 3. Sanitizer results

| Path | Result | Details |
| --- | --- | --- |
| Pixman headless lifecycle | **PASS** | ASan/UBSan/LSan clean. State transitions, decoration, destroy-during-transition, output scale/disable/restore, panel reassignment, reject-disable-all, and compositor shutdown passed. Pointer-watcher cases were `NOT_RUN` because this headless backend exposed no pointer capability; QEMU pointer checks passed. |
| Normal Mesa llvmpipe EGL compositor | **FAIL — LSan findings** | Runtime llvmpipe confirmed. The process shut down cleanly, then LSan reported 31,259 bytes across 154 allocations, including the 368-byte direct allocation. |
| Direct EGL/GBM control in QEMU | **INCOMPLETE** | The helper failed during its combined EGL initialization sequence before renderer identity or leak ownership could be established. |
| Host WSLg surfaceless EGL control | **PASS, non-equivalent** | Mesa EGL and llvmpipe were confirmed; no LSan report. The platform differs from the compositor DRM/GBM path. |

The compositor was built with ASan/UBSan. The installed Mesa and wlroots shared libraries were not sanitizer-instrumented. LSan still observed process allocations and retained stack traces through those libraries. No ASan invalid-access or UBSan runtime error appeared in the QEMU renderer run. A clean Pixman run does not establish that EGL is leak-free.

Pixman result data: [pixman-sanitizer-results.tsv](evidence/phase2c2/pixman-sanitizer-results.tsv). Raw sanitizer evidence remains under `/root/.cache/seyrix-linux/phase2c2-pixman-asan-20261026/` and `/root/.cache/seyrix-linux/phase2c2-asan-automatic-20261026/`.

## 4. Final ISO and packages

| Item | Value |
| --- | --- |
| Final acceptance ISO | `/root/.cache/seyrix-linux/phase2c2-final-20261026/out/seyrix-phase2c-2026.10.26-x86_64.iso` |
| Size | 1,039,171,584 bytes |
| SHA-256 | `93364bbc29791fc1e812e9ba7310ca806a69b3bd6327e0cbf46cb8936d1f9e33` |
| Guest kernel package | `linux 7.2.11.arch1-1` |
| Mesa | `1:26.2.5-1` |
| wlroots | `0.20.2-1` |
| Package manifest | [final-package-manifest.txt](evidence/phase2c2/final-package-manifest.txt) |
| Checksum record | [final-iso.sha256](evidence/phase2c2/final-iso.sha256) |

The image was built with Archiso `91-1`; host GCC `16.2.1+r23+gd564253eb6c8-1`, Meson `1.12.1-2`, and Ninja `1.13.2-3`. The build logged the existing `.pacnew` and GRUB SBAT warnings; the final ISO booted successfully. The sanitizer and diagnostic ISOs are separate artifacts and are not acceptance images.

## 5. Twenty independent cold boots

Campaign command:

```sh
bash tests/qemu-cold-boot-campaign.sh \
  /root/.cache/seyrix-linux/phase2c2-final-20261026/out/seyrix-phase2c-2026.10.26-x86_64.iso \
  /root/.cache/seyrix-linux/phase2c2-cold-boots-20-20261026
```

The campaign used QEMU `11.1.2-1`, q35, OVMF UEFI (`edk2-ovmf 202608-1`), TCG without KVM, `qemu64`, two vCPUs, 4 GiB RAM, virtio-vga, USB keyboard/tablet, a read-only ISO, and a new OVMF variable file for every run. Runs were sequential under the existing QEMU ownership lock. No failed run was silently retried. Every row below used the same final ISO SHA-256 listed above. Renderer shorthand `llvmpipe 23.1.3/128b` means the runtime string `llvmpipe (LLVM 23.1.3, 128 bits)`. `Client/exit/shutdown` is Wayland client launch, compositor exit, and guest shutdown.

| Run | Start UTC (Oct 26) | Result | Login ms | Compositor ready ms | Startup ms | Renderer | Client/exit/shutdown |
| --- | --- | --- | ---: | ---: | ---: | --- | --- |
| 01 | 14:58:52 | PASS | 58,834 | 82,699 | 15,500 | llvmpipe 23.1.3/128b | P/P/P |
| 02 | 15:00:51 | PASS | 60,514 | 84,602 | 15,508 | llvmpipe 23.1.3/128b | P/P/P |
| 03 | 15:02:52 | PASS | 60,149 | 84,519 | 16,022 | llvmpipe 23.1.3/128b | P/P/P |
| 04 | 15:04:53 | PASS | 59,207 | 83,658 | 15,963 | llvmpipe 23.1.3/128b | P/P/P |
| 05 | 15:06:53 | PASS | 59,781 | 83,986 | 15,931 | llvmpipe 23.1.3/128b | P/P/P |
| 06 | 15:08:53 | PASS | 60,494 | 84,796 | 15,834 | llvmpipe 23.1.3/128b | P/P/P |
| 07 | 15:10:54 | PASS | 60,651 | 85,831 | 16,805 | llvmpipe 23.1.3/128b | P/P/P |
| 08 | 15:12:56 | PASS | 59,248 | 83,221 | 15,605 | llvmpipe 23.1.3/128b | P/P/P |
| 09 | 15:14:55 | PASS | 59,707 | 83,876 | 15,748 | llvmpipe 23.1.3/128b | P/P/P |
| 10 | 15:16:55 | PASS | 59,308 | 83,457 | 15,909 | llvmpipe 23.1.3/128b | P/P/P |
| 11 | 15:18:55 | PASS | 60,596 | 84,536 | 15,721 | llvmpipe 23.1.3/128b | P/P/P |
| 12 | 15:20:56 | PASS | 59,850 | 84,161 | 15,876 | llvmpipe 23.1.3/128b | P/P/P |
| 13 | 15:22:56 | PASS | 59,292 | 83,692 | 16,048 | llvmpipe 23.1.3/128b | P/P/P |
| 14 | 15:24:55 | PASS | 59,413 | 83,657 | 15,695 | llvmpipe 23.1.3/128b | P/P/P |
| 15 | 15:26:55 | PASS | 59,265 | 83,163 | 15,694 | llvmpipe 23.1.3/128b | P/P/P |
| 16 | 15:28:53 | PASS | 63,958 | 87,969 | 15,638 | llvmpipe 23.1.3/128b | P/P/P |
| 17 | 15:30:57 | PASS | 58,264 | 81,724 | 15,362 | llvmpipe 23.1.3/128b | P/P/P |
| 18 | 15:32:55 | PASS | 60,908 | 85,032 | 15,661 | llvmpipe 23.1.3/128b | P/P/P |
| 19 | 15:34:56 | PASS | 59,750 | 83,605 | 15,581 | llvmpipe 23.1.3/128b | P/P/P |
| 20 | 15:36:55 | PASS | 58,937 | 83,760 | 16,571 | llvmpipe 23.1.3/128b | P/P/P |

Campaign result: **20/20 PASS**, zero retries, zero failed shutdowns. Median boot-to-login was 59,729 ms; median compositor readiness from QEMU start was 83,818 ms; median compositor startup was 15,735 ms. Guest `MemTotal` was approximately 3,994,664 KiB. Across the 40 recorded `MemAvailable` samples, the range was 3,597,052–3,625,156 KiB and the median was 3,614,396 KiB. These are software-emulated TCG measurements, not native hardware performance.

The exact per-run timestamps, full renderer strings, result codes, and lifecycle fields are in [cold-boot-matrix.tsv](evidence/phase2c2/cold-boot-matrix.tsv). Full serial logs, QEMU commands, OVMF state copies, warnings, and per-run metadata remain under `/root/.cache/seyrix-linux/phase2c2-cold-boots-20-20261026/`.

## 6. Functional regression matrix

The normal Phase 2C regression used the same final ISO and completed with `Run result: PASS`; all 25 recorded Expect stages were `PASS`, with no failed or incomplete stage. Evidence: [functional-stage-results.log](evidence/phase2c2/functional-stage-results.log) and raw run `/root/.cache/seyrix-linux/qemu-phase2c2-functional-20261026/`.

| Required behavior | Result | Runtime evidence |
| --- | --- | --- |
| Phase 1 serial recovery and root shell | PASS | Serial login, root shell, boot verification, and graphical readiness reached. |
| Weston reference fallback and return to tty1 | PASS | Weston became ready and stable after Seyrix exit; session exited and returned to tty1. |
| Window movement with actual geometry change | PASS | Same mapped window changed position from `-51,26` to `49,76`. |
| Interactive resize edges, corners, constraints | PASS | Top, bottom, left, right, four corners, and in/out constraints passed from grab-start and committed geometry evidence. |
| Workspace switching and no-wrap | PASS | Workspaces 1–4 selected; boundaries at 1 and 4 held with visible clients. |
| Window migration and application persistence | PASS | Focused windows moved both directions; process identities remained stable. |
| Focus restoration and hidden-workspace input isolation | PASS | Per-workspace focus restored; pointer and keyboard checks passed while another workspace was active. |
| Maximize, restore, fullscreen, rapid transitions | PASS | Configure-cycle and rapid state protocol probes passed. |
| Nested xdg-popups and dismissal | PASS | Nested creation, minimize/workspace switch, dismissal, and destruction during active grab passed. |
| Decoration negotiation and controls | PASS | Client decoration protocol probe passed. |
| Panel workspace selection and crash isolation | PASS | Pixel/selection checks passed; compositor input continued after panel crash. |
| Output configuration and supported lifecycle | PASS | QEMU output configuration passed; two-output headless scaling, disable/restore, panel reassignment, and reject-disable-all passed in Pixman. |
| Clean compositor exit and restart | PASS | Stress rerun restarted Seyrix, accepted keyboard input, exited the restarted compositor cleanly, and shut down the guest. |
| Phase 2B/2B.1 lifecycle checks | PASS | Startup, shutdown, client/output teardown, and restart paths completed in the final-ISO harness. |

The separate stress-mode rerun used the same final ISO and completed five lifecycle cycles. It reported `COMPOSITOR_RESTART=passed`, `COMPOSITOR_RESTART_INPUT=passed`, `COMPOSITOR_RESTART_EXIT=passed`, and `CLEAN_SHUTDOWN=passed`; restart startup was 2,743 ms. The stress helper explicitly recorded its same-window movement/resize stress subcheck as `INCOMPLETE` (`requires-same-window-scene-geometry`) in all five cycles. Those stress counts are not treated as functional passes; the standard final-ISO regression above independently verified actual movement and resize geometry.

The first stress-mode attempt was preserved as `INCOMPLETE`: the harness consumed the resize counter before waiting for it, then timed out. The assertion order was corrected in `tests/qemu-graphics.exp`, and the new run passed. This changed only test infrastructure; the final ISO SHA and 20-boot campaign target did not change.

## 7. Historical scheduler panic

The `enqueue_task_fair` panic did not recur in this 20-boot campaign. A scan of all 20 boot logs found no `enqueue_task_fair`, kernel panic, `Oops`, or `BUG:` matches. The guest kernel was `7.2.11-arch1-1`; QEMU was `11.1.2-1` under TCG with two vCPUs. No kernel or scheduler changes were made. The historical event is **not reproduced, not fixed**; it remains a documented external risk. The separate 20/20 result from Phase 2B.1 is not substituted for this campaign.

## 8. Test infrastructure changes

- `tests/qemu-cold-boot-campaign.sh` records runtime renderer identity per boot and fails a run unless the observed renderer is llvmpipe.
- `tests/qemu-graphics.exp` preserves the full compositor log when sanitizer diagnostics exist. Its optional EGL-control stage requires runtime llvmpipe and a clean helper exit to pass.
- `tests/qemu-graphics.exp` now reads the stress resize counter before the later incomplete interaction marker; this avoids consuming the counter in the prior Expect match.
- `tests/egl-gbm-lifecycle-control.c` provides an ASan/UBSan direct EGL/GBM control for follow-up ownership work. It compiled with `-std=c17 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined`; the corrected guest run did not initialize far enough to supply ownership evidence.
- No sanitizer suppressions, weakened functional assertions, compositor behavior changes, or dependency upgrades were introduced.

Validation performed: `bash -n` on the QEMU shell harnesses, Tcl `info complete` on `qemu-graphics.exp`, C17 `-Werror` sanitizer build of the control helper, and `git diff --check`. The two diagnostic control runs and first stress-mode timeout remain preserved in their original evidence directories.

## 9. Evidence index

Small committed evidence:

- [20-boot matrix](evidence/phase2c2/cold-boot-matrix.tsv)
- [Final ISO package manifest](evidence/phase2c2/final-package-manifest.txt)
- [Final ISO checksum](evidence/phase2c2/final-iso.sha256)
- [Phase 2C functional stage log](evidence/phase2c2/functional-stage-results.log)
- [Pixman sanitizer matrix](evidence/phase2c2/pixman-sanitizer-results.tsv)
- [368-byte stack excerpt](evidence/phase2c2/mesa-egl-368-stack.txt)
- [Lifecycle stress rerun summary](evidence/phase2c2/lifecycle-stress-rerun.txt)

Full external artifacts:

- Final build and manifest source: `/root/.cache/seyrix-linux/phase2c2-final-20261026/`
- Functional final-ISO QEMU run: `/root/.cache/seyrix-linux/qemu-phase2c2-functional-20261026/`
- 20 cold boots: `/root/.cache/seyrix-linux/phase2c2-cold-boots-20-20261026/`
- Corrected lifecycle stress rerun: `/root/.cache/seyrix-linux/qemu-phase2c2-lifecycle-stress-rerun-20261026/`
- Initial stress harness timeout: `/root/.cache/seyrix-linux/qemu-phase2c2-lifecycle-stress-20261026/`
- Pixman sanitizer run: `/root/.cache/seyrix-linux/phase2c2-pixman-asan-20261026/`
- EGL/llvmpipe sanitizer run: `/root/.cache/seyrix-linux/phase2c2-asan-automatic-20261026/`
- Direct EGL control attempts and diagnostic ISO runs: `/root/.cache/seyrix-linux/phase2c2-egl-control-20261026/`, `/root/.cache/seyrix-linux/phase2c2-egl-control-run-20261026/`, and `/root/.cache/seyrix-linux/phase2c2-egl-control-permissions-fixed-run-20261026/`

## 10. Acceptance decision

| Criterion | Status |
| --- | --- |
| Mesa allocation fixed or conclusively attributed upstream with reproducible evidence and tracking plan | **NOT MET** — allocation reproduced; owner unknown; direct control did not reach llvmpipe. |
| Normal llvmpipe rendering operational | **MET** — observed in normal final-ISO regression and all 20 boots. |
| Pixman ASan/UBSan/LSan checks pass | **MET**. |
| EGL/llvmpipe sanitizer finding completed and interpreted | **PARTIAL** — leak reproduced and preserved; ownership comparison incomplete. |
| Full Phase 2C functional workflow on the final ISO | **MET**. |
| Same final ISO passes 20/20 independent cold boots | **MET**. |
| Every boot reaches serial recovery, compositor/client readiness, clean exit, and shutdown | **MET**. |
| No unexplained critical kernel/compositor failures during the acceptance boot campaign | **MET** — none observed. |
| Historical scheduler panic accurately tracked | **MET** — not reproduced, not fixed. |
| Reproducible evidence and repository state preserved | **MET**. |

Because the leak ownership criterion remains unresolved, Phase 2C.2 is **INCOMPLETE**. Do not mark Phase 2C accepted until a controlled GBM/EGL comparison establishes ownership and a correction or upstream tracking plan is supported by evidence.
