# Seyrix Phase 2C.1 functional stabilization report

**Acceptance: INCOMPLETE.** The final ISO passed the complete functional QEMU
workflow, and focused geometry and sanitized Pixman headless checks passed.
Phase 2C.1 is not marked PASS because the existing 368-byte llvmpipe/Mesa EGL
LeakSanitizer finding remains unresolved and was not revalidated on the final
ISO. The final ISO also did not receive a repeat of the historical 20-boot
cold-start campaign.

## Recovered state

- Repository: `Andrew281003/Seyrix-Linux`.
- Branch: `phase2c1-functional-stabilization`.
- Recovered `HEAD`: `105312749caf1f82a3c7e8edc8065a0b3caedfa5`
  (`fix: stabilize pointer probe and QEMU diagnostics`), following
  `df85ee5` (`fix: stabilize Phase 2C compositor behavior`).
- The working tree already contained the geometry fix, QEMU harness work, and
  compositor documentation changes from the interrupted session. Those were
  preserved and reviewed; they were not recreated from the Phase 2C baseline.
- The Windows Git view also showed seven pre-existing modified
  `config/archiso` paths. They were not edited or staged:
  `etc/localtime`, `system-generators/systemd-gpt-auto-generator`,
  `getty.target.wants/serial-getty@ttyS0.service`,
  `multi-user.target.wants/systemd-networkd.service`,
  `multi-user.target.wants/systemd-resolved.service`,
  `network-online.target.wants/systemd-networkd-wait-online.service`, and
  `sockets.target.wants/systemd-networkd.socket` under
  `config/archiso/airootfs/etc/systemd/` (with `localtime` directly under
  `config/archiso/airootfs/etc/`).
- No Phase 2D work was started. No push or merge was performed.

## Changes completed

### Interactive resize coordinate stability

At pointer-grab start, the compositor now snapshots the client geometry
offsets and effective title-bar height. Resize updates use that captured
coordinate frame while xdg-shell configure/ack/commit changes the live client
geometry. This keeps the opposite edge anchored through client size commits.
The geometry unit test covers a bottom-edge resize after a client geometry
offset change. Compositor diagnostics record grab start, committed geometry,
and resize-frame evidence for the same PID/surface identity.

### QEMU synchronization and assertions

The resize harness now presses at the selected edge and waits for the
compositor's matching `RESIZE_FRAME_START` before moving to the drag endpoint.
It validates the committed client dimensions and anchored scene edge, rather
than treating a gesture-end marker as proof of a resize.

The QEMU scripts also wait for decoration negotiation before considering the
protocol probe ready, take move and migration baselines at the point the
assertion needs them, use distinct focus marker files, compare terminal PID
sets across migration, and verify actual hidden-workspace input isolation and
cleanup. The close test records a two-window baseline and requires exactly
one process/window destruction after each close. The panel pixel checker now
parses only the two image paths when optional workspace arguments are present.

An earlier full QEMU attempt had seven acceptance failures caused by test
synchronization and assertion setup, including a resize endpoint sent before
the grab began and workspace baselines taken after state had already changed.
Those results were kept as diagnostic evidence. The final sequential attempt
passed the corrected checks; the earlier failures were not relabeled as
product passes.

## Verification method and results

Three layers were used: the deterministic compositor geometry test, live
headless compositor integration, and the final UEFI ISO in a single sequential
QEMU TCG run. The QEMU run used the same final ISO for the graphics/workspace
workflow, serial recovery, output configuration, and Weston reference path.

| Area | Result | Evidence |
|---|---|---|
| Layer A geometry/focused test | **PASS** | `meson test` in `/root/.cache/seyrix-linux/phase2c1-final-20261026/compositor-build`: 1/1 `compositor geometry`, no failures. Ninja reported no work. |
| Actual pointer-driven window movement | **PASS** | Final QEMU log records scene position `-51,26` before and `49,76` after the gesture for the same window; `WINDOW_MOVE=passed`. |
| Resize edges, corners, and constraints | **PASS** | Final QEMU run checked top, bottom, left, right, all four corners, and min/max bottom-right constraints using matching grab-start and committed geometry evidence. |
| Four workspace selection and no-wrap | **PASS** | QEMU selected workspaces 1–4, checked boundary behavior at 1 and 4, and observed actual panel indicator changes for workspace selection. |
| Window migration and client persistence | **PASS** | QEMU migrated focused windows right and left, verified focus restoration and unchanged process identities, and checked applications remained alive. |
| Focus and hidden-workspace input isolation | **PASS** | QEMU restored per-workspace focus and used keyboard and pointer watcher checks while a different workspace was active. |
| Window states and rapid transitions | **PASS** | Maximize/fullscreen configure-cycle and rapid state-transition protocol probes passed. |
| Popups and lifecycle | **PASS** | Nested popup, dismissal/workspace transitions, and client destruction during an active popup grab completed successfully. |
| Decorations | **PASS** | Client-side decoration negotiation and protocol probe passed. |
| Panel workspace selection | **PASS** | Pixel comparison observed the selected indicator change from workspace 1 to workspace 3; all four button targets were exercised. Panel crash isolation and continued compositor input also passed. |
| Output configuration | **PASS** | Final QEMU output configuration passed. The two-output headless run applied scale, disabled/restored one output, reassigned the panel, and rejected disabling all outputs. |
| Sanitized headless lifecycle (Pixman) | **PASS** | `/root/.cache/seyrix-linux/headless-phase2c1-final-20261026T141000Z/results.tsv`: lifecycle, output, shutdown, and ASan/UBSan/LSan scan passed. Pointer watcher cases were `NOT_RUN` on this backend because it exposes no pointer capability; QEMU pointer checks passed. |
| Phase 1 serial recovery on final ISO | **PASS** | Final QEMU serial readiness marker passed; login took 80,096 ms. |
| Weston reference session / return to serial | **PASS** | Final QEMU workflow launched the reference session after Seyrix exit, exercised it, returned to tty1, and shut down cleanly. |
| 20-boot cold-start campaign on final ISO | **NOT RUN** | The prior Phase 2B.1 campaign passed 20/20 on its own final image; it was not repeated against this Phase 2C.1 ISO. |
| Normal llvmpipe LeakSanitizer check on final ISO | **INCOMPLETE** | The inherited 368-byte direct Mesa EGL allocation remains unresolved. The final QEMU acceptance run was not sanitizer-instrumented, and the successful headless sanitizer run used Pixman. |
| Scheduler panic | **INCOMPLETE / unresolved** | The pre-userspace `enqueue_task_fair` panic remains unexplained and unreproduced; this work made no kernel changes and does not claim a fix. |

### Renderer setup experiment

A separate, temporary attempt to run the headless integration script with
`WLR_RENDERER=gles2` did not reach compositor readiness. WSLg reported a D3D12
NVIDIA renderer, GBM buffer allocation failed for both headless outputs, and
the compositor exited because no output was active. LeakSanitizer then
reported 131,072 bytes from two allocations with an unknown module and 512
bytes in the WSL NVIDIA user-mode driver. This did not exercise the intended
llvmpipe path and is not attributed to Seyrix or treated as evidence about the
known 368-byte Mesa EGL allocation. The optional renderer switch was removed
from the harness; the supported headless integration command remains Pixman.

## Final ISO and QEMU evidence

| Item | Value |
|---|---|
| ISO | `/root/.cache/seyrix-linux/phase2c1-qemu-fixes-20261026T1332Z/out/seyrix-phase2c-2026.10.26-x86_64.iso` |
| Size | 1,039,171,584 bytes |
| SHA-256 | `ed8736a1ea4b64d2e75d701bbda1da6184ad4b84d1675bcd2935e5e07bd973d2` |
| Final QEMU run | `/root/.cache/seyrix-linux/qemu-graphics-20261026T140238Z/` |
| QEMU result | `PASS`, exit status 0; started 2026-10-26 14:02:38 UTC and finished 14:09:14 UTC |
| Configuration | QEMU 11.1.2, q35/OVMF UEFI, TCG single-thread, 2 vCPU, 4 GiB, GTK on WSLg, USB keyboard/tablet, read-only ISO only |
| Graphical readiness | 114,749 ms from QEMU launch |

The build completed with packaging/profile warnings for `issue.pacnew`,
`shadow.pacnew`, and the GRUB SBAT level. These did not prevent ISO creation or
boot. The QEMU run recorded clean compositor exit, session exit, and guest
shutdown. There was no concurrent QEMU campaign; no QEMU process remained
after the run.

## Regression and remaining limitations

- Phase 1 serial recovery and the Weston reference path passed on this ISO.
  The historical Phase 2B.1 20-boot campaign remains evidence for the earlier
  image only; it does not establish 20/20 boot reliability for this ISO.
- The Pixman sanitizer run establishes clean ASan/UBSan/LSan behavior for the
  exercised headless paths. It does not clear the historical llvmpipe/Mesa
  EGL leak. No sanitizer suppression or kernel workaround was introduced.
- The `enqueue_task_fair` panic remains a carried risk. The Phase 2B.1 report
  documented 20 successful cold boots with no recurrence, but not a cause or
  resolution.
- The QEMU run used a single virtual output under TCG; physical multi-monitor
  behavior and hardware-accelerated runtime behavior were not verified.
- The QEMU acceptance run is one end-to-end run. This report does not claim a
  repeated cold-boot campaign for the final ISO.

## Acceptance decision

Functional movement, resizing, workspace behavior, focus/input isolation,
window states, popups, decorations, panel selection, output handling, serial
recovery, Weston reference behavior, and clean shutdown have final-ISO
functional evidence. Focused geometry and Pixman sanitizer checks also pass.
The phase remains **INCOMPLETE** because the required normal llvmpipe
LeakSanitizer finding remains unresolved and the final ISO has no repeated
cold-boot campaign. Do not interpret this report as resolving either carried
risk.
