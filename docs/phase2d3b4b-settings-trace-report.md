# Phase 2D.3B.4b — Settings Input, Focus and Trace Reliability

**Status:** GTK page selection and page pixels changed. Client Wayland protocol capture still needs one focused verification; Phase 2D.3B is not complete.

## Confirmed observations

- Existing ISO: `/var/tmp/seyrix-linux/phase2d3b4a-native-20261031T141141Z/out/seyrix-phase2d3b-2026.10.31-x86_64.iso`; SHA-256 `7bf470ba904004ddb6e1670d5c162690a2df0e4cd9b16cabecada1b02c91c196`. No ISO was rebuilt.
- The ISO contains executable `/usr/bin/seyrix-settings` and `/usr/local/bin/seyrix-settings-render-trace`. Its desktop entry says `Exec=seyrix-settings` and `TryExec=seyrix-settings`. The source entry uses CRLF, which made the build script's exact `sed` rewrites miss. The normal GUI launch therefore bypassed the wrapper and never opened `/run/user/1000/seyrix-settings-render.trace` or set the trace environment.
- In the second authorized boot, `/run/user/1000` was owned by `seyrix:seyrix`, mode `0700`, and writable by that user. Directly invoking the wrapper created `/run/user/1000/seyrix-settings-render.trace` (`seyrix:seyrix`, `0644`, 60,658 bytes at capture). GTK events from PID 798 confirm `SEYRIX_SETTINGS_RENDER_TRACE=1` reached the instrumented app. The wrapper source sets `WAYLAND_DEBUG=client`; the temporary environment probe accidentally inspected a different, untraced Settings PID (740), so that variable was not independently verified from PID 798.
- GTK recorded `gtk_page_map page=appearance selected=appearance`, followed by `gtk_page_selection page=desktop selected=desktop` and Appearance unmapping. QMP clicked pixel `(320,156)` at 640×480, mapping to absolute `(16409,10671)`. `evtest` recorded those pointer coordinates and button press/release, then `KEY_HOME` (102), `KEY_DOWN` (108), and `KEY_ENTER` (28), each press/release. The GTK state change confirms the selection reached the application. The intermediate selector-open screenshot does not show a popup; the focus evidence is the delivered keyboard sequence, GTK selection event, and focused dropdown in the final image.
- The content ROI changed: `SETTINGS_PAGE_DIFF_ROI=10,185-630,265 changed_pixels=6850 total_difference=2121691`. The compositor recorded PID 798 attaching a buffer with damage `22,66-386,81`, then a successful scene commit and `presented=1`. The client submitted and the compositor presented an updated frame; no speculative damage fix is indicated.

The GTK trace and compositor records are in `/var/tmp/seyrix-linux/qemu-graphics-phase2d3b4b-settings-input-retry-20261031T161000Z/test.log`. The guest trace path was volatile and no longer exists after shutdown. Client protocol lines were not preserved: the temporary dumper looked for a client `wl_surface` ID using compositor pointer ID 16. The durable harness now correlates on the GTK trace PID and client object IDs.

## Evidence screenshots

![Appearance before selection](phase2d3b4b-settings-input-evidence/phase2d3b-appearance-640x480.png)

![Desktop & Dock after selection](phase2d3b4b-settings-input-evidence/phase2d3b-desktop-dock-640x480.png)

The selector-open capture is also in `phase2d3b4b-settings-input-evidence/phase2d3b-selector-open.png`.

## Source changes and next action

`build/build-phase2d3b-iso.sh` now normalizes the installed desktop entry before rewriting it and fails if Exec/TryExec do not route through the wrapper. `tests/qemu-graphics.exp` now reports explicit GTK before/after state, records the QMP sequence, checks environment on the PID from GTK trace, captures client protocol events by client object ID, uses post-input compositor baselines, and waits for the trace result before the shell prompt.

The second boot's runner ended `INCOMPLETE`: its temporary launch gate also required the fixed desktop entry, and its old PID lookup selected PID 740; the final expect order consumed the result marker while waiting for the shell prompt. These source harness issues are corrected but have not been exercised. Static checks pass (`bash -n`, Expect completeness, `git diff --check`). The two authorized QEMU boots are used. The next smallest verification is one additional boot of the same ISO with the corrected capture, which requires separate authorization; an ISO rebuild remains unauthorized.
