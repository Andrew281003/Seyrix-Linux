# Phase 2D.3B.4e — Reliable End-to-End Settings Verification

**Focused result: PASS after corrected offline replay. Phase 2D.3B remains INCOMPLETE.**

Exactly one QEMU boot was used. The original in-guest runner labeled its capture INCOMPLETE because two harness gates rejected valid GTK/render ordering and it omitted a verified-input PASS marker. The complete live logs and QMP frames were preserved. After correcting the harness, the staged production parser replayed that same capture, correlated the rendering events, and the screenshot comparator passed. No second boot was needed.

## Offline rehearsal

The complete temporary harness snapshot is preserved at `/var/tmp/seyrix-linux/phase2d3b4e-settings-e2e-20261031T215746Z/harness` and copied under [offline-rehearsal/harness-snapshot](evidence/phase2d3b4e-settings-e2e/offline-rehearsal/harness-snapshot/). Its hash manifest is [harness-source-hashes-corrected.sha256](evidence/phase2d3b4e-settings-e2e/offline-rehearsal/harness-source-hashes-corrected.sha256).

- The staged snapshot contained every required script and all five trace fixtures. The required scripts were executable; fixtures were readable.
- The actual staged `qemu-graphics.sh` preflight passed for workflow `phase2d3b`, trace-only mode, the exact ISO hash, GTK display, virtio-vga, and OVMF. It created no QEMU run directory or VM.
- The staged production parser replayed the saved 4c trace as PID 776, 26 client surface operations, client object `wl_surface#16`.
- The staged regression passed PID/UID/environment and session checks; established endpoint ownership; same-PID object correlation; GTK transition order; damage/commit/scene/presentation correlation; missing-helper and missing-fixture preflight rejection; skipped-stage failure handling; and screenshot-comparison checks.
- Bash syntax, Python syntax, Tcl completeness, and the Expect serial-capture regression passed. Evidence output directories were write-probed. The original guest-helper omission was not repeated.

Logs: [corrected staged preflight](evidence/phase2d3b4e-settings-e2e/offline-rehearsal/staged-preflight-corrected.log), [offline regression](evidence/phase2d3b4e-settings-e2e/offline-rehearsal/offline-rehearsal-corrected.log), and [staged source hashes](evidence/phase2d3b4e-settings-e2e/offline-rehearsal/harness-source-hashes-corrected.sha256).

The captured [client trace](evidence/phase2d3b4e-settings-e2e/boot1-corrected-review/qemu-run/settings-render.trace), [compositor log](evidence/phase2d3b4e-settings-e2e/boot1-corrected-review/qemu-run/seyrix-compositor.log), [complete QEMU test log](evidence/phase2d3b4e-settings-e2e/boot1-corrected-review/qemu-run/test.log), [serial log](evidence/phase2d3b4e-settings-e2e/boot1-corrected-review/qemu-run/serial.log), and original [on-guest stage results](evidence/phase2d3b4e-settings-e2e/boot1-corrected-review/qemu-run/stage-results.log) are preserved unmodified.

## QEMU boot and Settings process

Boot count: **1 of 3 authorized**. The existing diagnostic ISO was used read-only at `/var/tmp/seyrix-linux/phase2d3b4a-native-20261031T141141Z/out/seyrix-phase2d3b-2026.10.31-x86_64.iso`; SHA-256 was `7bf470ba904004ddb6e1670d5c162690a2df0e4cd9b16cabecada1b02c91c196`. It was not rebuilt or promoted.

The diagnostic `.desktop` entry was installed temporarily in the logged-in Seyrix user's application directory and launched through `seyrix-shell` so Settings inherited the compositor session:

- Settings PID 757, UID 1000, executable `/usr/bin/seyrix-settings`.
- `SEYRIX_SETTINGS_RENDER_TRACE=1`, `WAYLAND_DEBUG=client`, `XDG_RUNTIME_DIR=/run/user/1000`, `WAYLAND_DISPLAY=wayland-0`.
- Settings and compositor shared session ID 526.
- Settings held an established `u_seq` socket connection to the read-only endpoint (`ss` showed `seyrix-settings`, PID 757). The endpoint stage passed.

Both QMP frames show “Connected to Seyrix compositor; read-only mode.” The initial Appearance frame shows “Solid background”; the Desktop & Dock frame shows the live “Dock” value. The after frame was captured only after the QMP Home/Down/Enter sequence. The launch-failure fallback screenshot was not produced or counted.

## Observed input and rendering sequence

GTK and compositor `monotonic_ns` values below share the guest monotonic clock. The trace's libwayland wall-clock timestamp provides an anchor to the evtest input timestamps; the derived GTK-selection time is approximate.

| Event | PID / identity | Timestamp and evidence |
|---|---|---|
| Initial Appearance map | Settings 757 | `107480910147`; selected Appearance |
| QMP mouse click | QEMU input | 2026-10-31 22:01:10.655–22:01:10.693 UTC; recorded coordinates 16409,10671 and button down/up |
| QMP Home, Down, Enter | QEMU input | Enter down `22:01:11.463167`, up `22:01:11.564090` UTC; all three key down/up pairs appear in evtest |
| Desktop & Dock map | Settings 757 | `121901775330`; selected Desktop & Dock |
| Desktop & Dock selection | Settings 757 | `121902593289`; approximately `22:01:11.531287` UTC, between Enter down/up |
| Appearance unmap | Settings 757 | `121905575230` |
| Client attach / buffer damage / commit | Settings 757, `wl_surface#16` | `122023817262` / `122023938092` / `122076445071` |
| Compositor buffer-attached surface commit | PID 757, `wl_surface=16`, seq 18 | `122078169843`; buffer damage recorded |
| Successful output scene commit | PID 757, `wl_surface=16`, seq 18 | `122116195331` |
| Output presented | PID 757, `wl_surface=16`, seq 18 | `122124608586`, `presented=1` |

The join uses PID 757 and the client `wl_surface#16` resource matched to the compositor's `wl_surface=16`. The selected server commit, scene commit, and presentation share `surface_seq=18`. No compositor pointer ID was used as a surface identity.

## Screenshot comparison

The QMP Appearance and Desktop & Dock PPM frames differ. The staged comparator reported:

- Full frame: **48,667 / 307,200** changed pixels; total absolute RGB difference **11,398,931**.
- Page-content ROI `[10,194,630,421)`: **21,108 / 140,740** changed pixels; total absolute RGB difference **7,317,435**.
- Screenshot comparison stage: **PASS**.

Original QMP screenshot checksums are in [screenshots.sha256](evidence/phase2d3b4e-settings-e2e/boot1-corrected-review/qemu-run/screenshots.sha256), and all eight screenshot hashes were verified after copying the evidence. The [corrected replay and review](evidence/phase2d3b4e-settings-e2e/boot1-corrected-review/) keeps the original logs and stage results beside the corrected offline stage results.

Appearance-before SHA-256: `95bedb9c4c9bfedb10cff8a37d297ec96cc843ca662c07c1b4163b50c9efcd39`; Desktop & Dock-after SHA-256: `3da5041aea086799c642ef49a6cf96ba9a6b05dc9f41b66bbcfa6b65d9aa11c4`.

![Appearance before input](evidence/phase2d3b4e-settings-e2e/boot1-incomplete/previews/appearance-review.png)

![Desktop & Dock after selection](evidence/phase2d3b4e-settings-e2e/boot1-incomplete/previews/desktop-dock-review.png)

## Harness corrections

- The staged snapshot now includes and validates `qemu-guest-helper.sh` plus all trace fixtures before startup.
- Settings launches from a temporary user-local diagnostic desktop entry through Seyrix Shell, preserving the compositor session ID and enabling the read-only endpoint connection.
- The GTK gate now accepts either destination-map/selection callback order while requiring both to occur after the initial Appearance map and before Appearance unmap.
- Render correlation accepts valid `buffer_damage` as well as `surface_damage`, and selects the first post-selection, buffer-attached server commit that follows the same-PID client attach/damage/commit sequence and has matching scene/presentation records.
- Verified input now emits an explicit PASS stage result. The screenshot comparator requires the endpoint, GTK, input, trace-correlation, and QMP-capture stages.

The original on-guest [run result](evidence/phase2d3b4e-settings-e2e/boot1-corrected-review/qemu-run/run-result.txt) remains INCOMPLETE as recorded at shutdown. The corrected PASS is a separate, reproducible offline replay of the preserved raw traces, evtest records, endpoint evidence, and QMP screenshots; see [reviewed stage results](evidence/phase2d3b4e-settings-e2e/boot1-corrected-review/stage-results-reviewed.log). This distinguishes the harness defect from the observed live transition.

## Remaining acceptance gaps

This focused pass does **not** complete Phase 2D.3B. The ISO's installed Settings desktop entry was not changed; this run used a temporary diagnostic entry. The broader phase still needs normal launcher integration on the corrected image, all six categories, search and responsive navigation, full 640×480 usability, endpoint subscription/reconnect and connection-loss behavior, wider compositor/shell regressions, and the remaining cold-boot and native acceptance campaign. The complete outstanding list remains in [phase2d3b-report.md](phase2d3b-report.md).

No GTK or compositor production source changes were made for this verification. There was no ISO rebuild/promotion, Phase 2D.3C work, push, merge, reset, or protected-artifact cleanup. Existing worktrees, ISOs, WSL VHDX, and backup were preserved. QEMU and guest shutdown completed cleanly. The managed artifact tree measured **33,938,477,056 bytes**; with the 4 GiB run reserve, total usage was **38,233,444,352 bytes**, below the 40 GiB cap by **4,716,228,608 bytes**. Free space was **987,546,976,256 bytes** (about 920 GiB), above the 30 GiB minimum.
