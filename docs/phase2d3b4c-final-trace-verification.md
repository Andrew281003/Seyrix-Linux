# Phase 2D.3B.4c — Final Settings Trace Verification

**Overall result: INCOMPLETE.** The single authorized QEMU boot reached the graphical session, launched the installed trace wrapper directly, and preserved the complete client trace. The traced process was UID 1000 and had both requested environment variables. The run did **not** exercise the page switch: the harness expected Wayland object names using `wl_surface@N`, while this image's libwayland emitted `wl_surface#N`. Its false-zero protocol count stopped the QMP input branch. The second screenshot is therefore a fallback capture of the Appearance page, not a post-selection frame. This is a harness failure; it does not establish a Settings rendering or compositor defect.

## Result summary

| Check | Result | Evidence |
|---|---|---|
| Authorized ISO identity and no rebuild | **PASS** | ISO hash and size below |
| Preflight space, retention, process, and static gates | **PASS** | Preflight section |
| QEMU boot, serial login, graphical session | **PASS** | QMP connected; session markers below |
| tty2 serial recovery | **NOT REQUIRED** | Serial login succeeded directly; no recovery marker |
| Direct diagnostic wrapper launch | **PASS** | `SETTINGS_WRAPPER_LAUNCH_REQUESTED=passed` |
| Traced Settings PID, UID, and environment | **PASS** | PID 776, UID 1000, actual executable and `/proc` environment |
| Appearance displayed and selected in GTK | **PASS** | GTK page-map event and QMP image |
| Desktop & Dock input and GTK transition | **INCOMPLETE** | Harness logged `SETTINGS_PAGE_INPUT_SEQUENCE=NOT_RUN`; no page-selection event |
| Complete GTK/Wayland client trace capture | **PASS** | 293 lines, 38,775 bytes; copied before guest shutdown |
| Client surface operations for the page switch | **INCOMPLETE** | Initial Appearance operations exist; no page switch was sent |
| Correlated wlroots update, scene commit, and presentation | **INCOMPLETE** | No per-update compositor records were exported |
| QMP image pixel comparison | **FAIL** | The two captures are identical; the second was not taken after a selection |
| Controlled guest shutdown | **PASS** | `CONTROLLED_RENDER_TRACE_SHUTDOWN=passed`; QEMU exited |
| Full Settings rendering-path acceptance | **INCOMPLETE** | No valid input-to-presentation sequence |

## Preflight and boot accounting

The Phase 2D.3B.4a and 4b reports and their evidence were reviewed. The ISO contains the old Settings desktop entry (`Exec=seyrix-settings`, `TryExec=seyrix-settings`), which bypasses the wrapper. The source-side desktop-entry correction is not packaged in this ISO. The harness recorded those values and launched `/usr/local/bin/seyrix-settings-render-trace` directly; it did not use the desktop entry to activate tracing.

The ISO used was `/var/tmp/seyrix-linux/phase2d3b4a-native-20261031T141141Z/out/seyrix-phase2d3b-2026.10.31-x86_64.iso`, 1,151,631,360 bytes. Its SHA-256 was `7bf470ba904004ddb6e1670d5c162690a2df0e4cd9b16cabecada1b02c91c196` before and during the run. QEMU used the ISO read-only; it was not rebuilt or modified.

Before boot, the used ext4 harness snapshot passed Bash syntax, Python AST, Expect completeness, and `git diff --check`. Its pixel comparator passed against the prior 4b QMP screenshots (18,062 changed pixels in the page-content ROI) and failed closed on an identical-image control. The retention lock was free and process inventories found no QEMU or build conflict.

Two host runner invocations ended before a run directory or QEMU process was created. The first returned exit status 1 without a run directory, QEMU PID, or lock owner; the second showed that Windows-to-ext4 copying had not retained executable mode. The copied host scripts were chmodded before the actual run. No guest started during those attempts. Exactly one QEMU VM boot was executed.

The final measured managed root usage, including the retained QMP-derived PNG views and post-run helper copy, was 32,930,392,218 bytes. Including the 4 GiB run reserve, that is 37,225,359,514 bytes against the 42,949,672,960-byte (40 GiB) cap, leaving 5,724,313,446 bytes of headroom. Free space after the run was 987,568,664,576 bytes, above the 30 GiB minimum. The retention lock was available after shutdown; no QEMU/build processes remained.

## QEMU and graphical session

- Run directory: `/var/tmp/seyrix-linux/qemu-graphics-phase2d3b4c-final-20261031T163642Z`
- QEMU run ID: `20261031T163742Z`; QEMU PID 356; Expect PID 355.
- QMP connected. QEMU used GTK on WSLg, virtio-vga, TCG, OVMF, 2 vCPUs, and 4 GiB RAM.
- Serial login passed in 60,937 ms. `TTY1_DEV_SESSION_READY=passed`; graphical session readiness passed in 101,723 ms, with compositor startup in 21,778 ms. The tty2 recovery path was not invoked.
- `RENDER_SESSION_READY=passed`: Seyrix shell PID 680 and compositor PID 644 both ran as UID 1000; Wayland socket was `/run/user/1000/wayland-0`.
- Guest poweroff completed. QEMU's process-state record says the process exited. The harness run result is **INCOMPLETE** with exit status 1 because its Settings capture gate failed, not because QEMU or the guest crashed.

## GTK and client trace

The guest helper observed the installed wrapper and native binary as executable and requested the wrapper directly. It obtained the Settings PID from the GTK trace event:

- PID 776, UID 1000, executable `/usr/bin/seyrix-settings`
- `SEYRIX_SETTINGS_RENDER_TRACE=1`
- `WAYLAND_DEBUG=client`
- Trace owner in guest: `seyrix:seyrix`

The GTK trace contains `gtk_page_map page=appearance selected=appearance mapped=1`. The QMP image displays the Appearance page. It also shows “Compositor disconnected; showing last-known state,” so live endpoint state was not accepted in this run.

The helper counted zero client surface operations because its pre-run expression matched `wl_surface@N`. The saved Wayland trace actually uses `wl_surface#16` and `wl_buffer#24`. Offline parsing of the preserved trace with the corrected `[@#]` expression finds 26 client surface operations for object ID 16; 22 occur after the helper's 219-line baseline (6 attach, 9 damage_buffer, and 7 commit operations). The trace sequence includes `wl_surface#16.commit()` at 16:39:32.805253, then `attach(wl_buffer#24)`, `damage_buffer(...)`, and `commit()` at 16:39:43.854414, 16:39:43.854500, and 16:39:43.941039; another attach/damage/commit follows at 16:39:44.695739, 16:39:44.697835, and 16:39:44.753870. These are client operations during the initial Appearance window/rendering activity, not a Desktop & Dock transition. The numeric ID 16 comes only from the client protocol object name `wl_surface#16`; it was not inferred from a compositor pointer. No matching compositor `wl_surface=<resource-id>` event was exported, so this report makes no cross-side identity correlation claim.

Because the launch gate returned false, the harness explicitly logged `SETTINGS_PAGE_INPUT_SEQUENCE=NOT_RUN reason=traced-wrapper-launch-failed`. It sent no QMP click or Home/Down/Enter sequence. The GTK trace has no `gtk_page_selection page=desktop selected=desktop`, no Desktop & Dock map, and no Appearance unmap. The input log has no verified button press/release or keyboard press/release sequence.

The full trace snapshot was copied from the guest before poweroff. Guest `stat` at capture reported owner `seyrix:seyrix`, mode `0644`, size 38,775 bytes, mtime and ctime `2026-10-31 16:39:46.549987777 +0000`. The extracted host copy is 293 lines and exactly 38,775 bytes; SHA-256 `bc2e30cdf90b3f1d24d1d692b8214dc41a257160e3ad29e6c6b4eacc850dd9f`.

The helper recorded pre-interaction baselines of two surface commits, two scene commits, and two output presentations. It did not export raw per-update compositor records: its client-ID parser yielded an empty ID, so the correlated event sections were empty. The compositor's `/tmp/seyrix-compositor.log` was volatile and was not included in the serial capture. Consequently, this run has no preserved wlroots surface update, matching scene commit, or output-present record for the page switch. The baseline counts are not evidence of an updated output.

## QMP screenshots

QMP saved two 640×480 PPM captures, each 921,615 bytes:

- [Appearance capture before the input branch](phase2d3b4c-final-trace-evidence/phase2d3b-appearance-640x480.ppm)
- [Fallback capture labeled Desktop & Dock](phase2d3b4c-final-trace-evidence/phase2d3b-desktop-dock-640x480.ppm)

![QMP Appearance capture before the skipped input sequence](phase2d3b4c-final-trace-evidence/phase2d3b-appearance-640x480.png)

![QMP fallback capture labeled Desktop & Dock; no input was sent](phase2d3b4c-final-trace-evidence/phase2d3b-desktop-dock-640x480.png)
The second file was captured by the launch-failure branch without sending input. Both PPM files have SHA-256 `f4f39bb6b0ab6c8eb01fb5060632626a0a7956d5f276649cdeb37d09fd7f1c54`. The comparator reported 0/307,200 full-frame pixels changed and 0/140,740 pixels changed in ROI `[10,194,630,421)`.

The comparator stage is **FAIL** for “pixel change,” while the page-switch acceptance is **INCOMPLETE** because the input was not run. The unchanged screenshot is not evidence of a compositor rendering bug. Lossless PNG views are also retained: [before](phase2d3b4c-final-trace-evidence/phase2d3b-appearance-640x480.png) and [fallback after](phase2d3b4c-final-trace-evidence/phase2d3b-desktop-dock-640x480.png).

## Post-run harness correction

After preserving the run snapshot, `tests/settings-render-trace-guest.sh` was corrected to accept both `wl_surface#N` and `wl_surface@N`, count actual evtest `Event: time` coordinate records rather than device capability lines, and retain unfiltered post-baseline compositor surface/scene/presentation records. The corrected helper passes `bash -n` and `git diff --check`; its offline parser recognizes the 26 operations and client object ID 16 in this saved trace. Its SHA-256 is `e8cadaab90d3a6db4b5eaad0a23a51c63e66316fe5dcee8bc798e95723f1b140`.

The exact pre-run helper snapshot remains at `/var/tmp/seyrix-linux/phase2d3b4c-final-run-20261031T162547Z/harness/settings-render-trace-guest.sh` with SHA-256 `17ecf81087e06a3dfb9a0c34ec471c8c2caee8c17223df9d5aed9429407395db`. The post-run corrected copy is under `harness-postrun-correction/`. This correction was not guest-boot-tested; the single authorized boot has been used.

## Remaining Phase 2D.3B acceptance

Phase 2D.3B remains **INCOMPLETE**. This run does not close the graphical QEMU acceptance gap. Remaining requirements include:

- Verify launcher integration on an ISO containing the desktop-entry fix, then exercise all six Settings categories, search, responsive navigation, keyboard and pointer navigation, and usable 640×480 layout.
- Verify accurate live read-only endpoint data, subscription/reconnect behavior, connection/error/offline/loss states, and About data from the graphical session.
- Capture a real GTK page selection and map/unmap transition; preserve client-object-ID-correlated wlroots surface commits/damage, scene commits, and output presentation; compare a genuine post-selection QMP frame.
- Rerun the Settings/compositor/shell regressions and Settings/session shutdown checks on the accepted image; verify Weston fallback and return-to-tty behavior.
- Complete the Phase 2D.3 integration campaign on the final image: ten cold boots to graphical readiness, two complete end-to-end passes, and the required native/schema/IPC/permission/persistence/sanitizer checks. Prior native test results remain those recorded in `phase2d3b-report.md`; they were not rerun here. The previously reported shell LSan result (140,884 bytes in 4,817 allocations through Fontconfig/Pango) and separate wlroots/Mesa finding remain visible and were not re-evaluated.

No production rendering files were changed. The ISO was not rebuilt or promoted. No merge, push, or Phase 2D.3C work was performed. Existing worktrees, unrelated dirty changes, verified ISOs, and the WSL VHDX/backup were preserved.

Supporting records: [complete client trace](phase2d3b4c-final-trace-evidence/settings-render.trace), [full test log](phase2d3b4c-final-trace-evidence/test.log), [serial log](phase2d3b4c-final-trace-evidence/serial.log), [QMP screenshot checksum list](phase2d3b4c-final-trace-evidence/screenshots.sha256), [run metadata](phase2d3b4c-final-trace-evidence/run-metadata.txt).