# Phase 2D.3B.2 — Root-Cause Investigation

**Assessment date:** 2026-10-31  
**Disposition:** Diagnostic budget exhausted. Phase 2D.3B remains incomplete. No Phase 2D.3C work began.

This investigation used the existing 800×600 candidate ISO only. It did not build or promote an ISO, run full graphical acceptance, or change production source. Two targeted QEMU boots were used, the maximum allowed.

## A. Executive findings

| Blocker | Reproduced | Root cause | Failure layer | Evidence and confidence | Recommended correction |
|---|---|---|---|---|---|
| Serial-getty does not start reliably | **YES** | **PARTIALLY ISOLATED** | In failing boots, systemd does not see the ttyS0 device until about 159–162 seconds after guest boot. Its initial device-dependent getty job has already timed out at 90 seconds. The later device unit activates, but the getty remains inactive. Whether the UART appears late in the kernel or whether its udev event/node is delayed is **UNKNOWN**. | Two new 800×600 boots measured udev initialization at 160.166s and 159.336s, followed by device-unit activation at 163.002s and 160.719s. Both found the getty inactive/dead. Earlier failing logs show the 90-second timeout and dependency failure; a successful same-hardware run had a real login prompt at about 61.7s. High confidence in the late-device/getty lifecycle; low confidence in the cause before udev initialization. | The source SYSTEMD_WANTS rule is a justified candidate for re-requesting the same getty when the late tty device arrives. It is not verified: the tested ISO does not contain it. Verify the rule and unit result in a later authorized targeted experiment before packaging; remove it if it does not fire as intended or causes unwanted serial logins. |
| GTK4 Settings changes selected category but retains Appearance pixels | **YES** for the real Settings application at 800×600 | **UNKNOWN** | The observed failure is after GTK's internal selection and widget mapping. The actual Settings client’s Wayland commit, compositor receipt/damage, output repaint, and captured pixels after commit were not traced. | The existing QMP evidence reports selected/mapped Desktop & Dock and an identical screenshot content ROI (changed_pixels=0). Source inspection found no obvious GtkStack hierarchy/selection invariant violation. A minimal GTK4 stack under headless Weston changed state and sent surface damage/commit with presentation feedback, but its screenshot capture was denied. The Seyrix minimal probe was **NOT REACHED**. Confidence is high in the stale-pixel symptom and low in any GTK-versus-compositor diagnosis. | Do not add redraw calls or change GTK/compositor code. The next experiment should trace the actual Settings surface across the client commit, wlroots surface commit/damage, output-frame scene commit, and one QMP capture, all keyed to the same surface and monotonic timestamps. |

### Confirmed defects versus unresolved hypotheses

- **Confirmed:** failing boots time out waiting for /dev/ttyS0; the tty udev object is initialized only after that wait; dev-ttyS0.device then becomes active while serial-getty@ttyS0.service stays inactive.
- **Confirmed:** the separate virtio control service can become active while the real serial getty is inactive. Its availability does not prove tty readiness.
- **Confirmed:** the actual Settings QMP image remains unchanged after the GTK category state changes.
- **Not confirmed:** late kernel UART registration, late devtmpfs node creation, or delayed udev processing as the source of late tty availability.
- **Not confirmed:** whether GTK/GSK failed to submit updated pixels for the actual Settings click, or the Seyrix compositor failed to receive, damage, repaint, or present them.
- **Not confirmed:** the experimental udev rule. It was absent from the tested ISO.

## B. Serial timeline

Guest monotonic times below are relative to the guest kernel’s monotonic clock. The udev property USEC_INITIALIZED records when udev initialized that device; it is not a timestamp for the earlier kernel probe or exact /dev node creation.

| Run | Timestamp | Observation |
|---|---:|---|
| Successful reference, 800×600, QEMU started 06:18:22Z | 00:58.894 | Unprivileged virtio guest-control shell became available. This is the separate seyrix-qemu-control.service, not the tty getty. |
| Same successful reference | about 01:01.654 | Real seyrix-dev login: prompt appeared 2.760s into the real-serial-prompt stage. REAL_SERIAL_TTY0_GETTY=passed followed. The device was ready early enough for the normal getty transaction. The successful run did not record the udev initialization timestamp. |
| Earlier failing 800×600 reference | 01:30 | Raw systemd serial output says “Timed out waiting for device /dev/ttyS0” followed by “Dependency failed for Serial Getty on ttyS0.” Later query showed the node/device active and the getty inactive. Reported USEC_INITIALIZED=161676791. See docs/phase2d3b-report.md and the retained Phase 2D.3B.1 QEMU evidence. |
| Earlier failing 640×480 diagnostic reference | 01:30 | Same timeout/dependency failure. Later query showed /dev/ttyS0; USEC_INITIALIZED=157237948. Evidence: /var/tmp/seyrix-linux/qemu-graphics-phase2d3b1-serial-diagnostic-2-20261031T090000Z/. |
| Targeted boot 1, QEMU run metadata 10:13:02Z | 02:40.166 | udevadm info reported USEC_INITIALIZED=160165798, DEVNAME=/dev/ttyS0, TAGS=:systemd:. At the query, /dev/ttyS0 existed as major 4, minor 64. |
| Targeted boot 1 | 02:43.002 | dev-ttyS0.device entered active/plugged, Wants= empty. serial-getty@ttyS0.service was inactive/dead, with StateChangeTimestampMonotonic=0; it had not started. |
| Targeted boot 1 | 03:31.548 | seyrix-qemu-control.service was active/running. |
| Targeted boot 1 | 03:59.370 | Guest-control shell became available (BOOT_TO_USER_CONTROL_MS=239370). Serial state capture passed. |
| Targeted boot 2, QEMU run metadata 10:21:26Z | 02:39.336 | Udev reported USEC_INITIALIZED=159335980, DEVNAME=/dev/ttyS0, with systemd tag. |
| Targeted boot 2 | 02:40.719 | dev-ttyS0.device entered active/plugged, Wants= empty. The getty remained inactive/dead, StateChangeTimestampMonotonic=0. |
| Targeted boot 2 | 03:33.828 | The separate guest-control service became active/running. |
| Targeted boot 2 | 04:01.239 | Guest-control shell became available. The tty-state capture passed; the Seyrix graphical-session readiness check later timed out. |

The first **confirmed** divergence is before systemd’s device-dependent getty transaction can complete: successful reference evidence has the login prompt by roughly 62 seconds, while failing boots emit the 90-second device timeout and udev does not initialize ttyS0 until roughly 157–162 seconds. In the two new runs, device-unit activation followed udev initialization by only 1.38–2.84 seconds. That makes a 90-second delay inside systemd’s device-unit activation unlikely.

The missing observation is the timestamp of the kernel's UART/tty registration and the initial kernel uevent, together with an early udev event trace. The targeted boots added loglevel=6 printk.time=1, but the captured serial kernel text contained only the Linux version and command line; it had no ttyS0/8250 probe line. /proc/tty/driver/serial was permission-denied to the unprivileged control shell, and its kernel-journal query returned no matching serial records. Therefore this evidence cannot distinguish late kernel enumeration from an earlier kernel event whose udev/device-node processing was delayed. The exact /dev/ttyS0 creation time was not recorded either.

Harness and scheduling effects are separate from the device failure. Earlier raw logs wrapped the timeout text in ANSI escapes, which defeated a literal matcher; the existing harness normalization strips CSI sequences and carriage returns, as documented in docs/phase2d3b-report.md. In this investigation, boot 1 was cut off by the external timeout while the session-readiness stage was still running, and its metadata said PASS despite an incomplete stage and .seyrix-failed marker. Boot 2 had a pgrep -x process-name check that cannot match names longer than 15 characters. These are harness observation/reporting defects, not evidence that ttyS0 itself worked or failed. Both new boots used QEMU TCG and similar guest-monotonic tty event times, but host scheduling/CPU-steal data was not recorded; scheduling could affect wall-clock duration and cannot be ruled out as a contributor.
### Unit and rule analysis

- The image has a static getty.target.wants/serial-getty@ttyS0.service link. The unit is ordered after and bound to dev-ttyS0.device. When ttyS0 is unavailable during the initial job, the dependency fails; later device appearance alone did not start the getty in these boots.
- The virtio control service is activated by org.seyrix.qemu-control and has no ttyS0 dependency. Its successful shell is not evidence that the serial device or getty is ready.
- The experimental rule matches SUBSYSTEM=="tty", KERNEL=="ttyS0", adds the systemd tag, and requests the same unit instance through SYSTEMD_WANTS. The observed udev properties include the expected tty subsystem/name and the systemd tag, so the match is consistent with the device at issue.
- That request does not make the UART or devnode appear earlier; it can only ask systemd to start the getty when the udev device event arrives. The static getty target and rule request the same unit name, not separate template instances. Whether their jobs coalesce cleanly in this image and whether the rule’s event reaches systemd as expected were not tested.
- Because the rule was not in the ISO, no statement that it fired, restarted the service, or avoided an unwanted activation is supported. It could start a serial login when a ttyS0 appears on hardware where that access is not wanted; verify scope before packaging.

## C. GTK rendering timeline

### Existing failing Settings transition

1. The actual Settings application launched from the 800×600 Seyrix desktop and accepted search/input.
2. The selection callback in desktop/settings/src/settings-window.c:412-437 calls gtk_stack_set_visible_child() and updates category selection. Normal pages are added to the GtkStack at :187-190; the stack is created with no transition at :895-898. Search results are added at :667.
3. Existing GTK state evidence reports Desktop & Dock selected and mapped, with Appearance unmapped.
4. The QMP after-selection screenshot comparison reports SETTINGS_PAGE_DIFF_ROI=210,145-790,215 changed_pixels=0 total_difference=0; the displayed area remains Appearance. Evidence: /var/tmp/seyrix-linux/qemu-graphics-top-level-redraw-20261031T063000Z/phase2d3b-appearance.png, phase2d3b-page-current.png, and test.log.
5. No client wl_surface trace for this real Settings switch and no matching compositor surface-commit/damage/output-frame trace exist. This is the earliest boundary where evidence stops. The source inspection is not evidence that the actual client emitted a frame.

### Minimal GTK4 GtkStack probe under headless Weston

The temporary app used a normal Wayland toplevel, a red Appearance page, a blue Desktop & Dock page, one button, and a deterministic 1.8-second switch. Source: %LOCALAPPDATA%/Temp/seyrix-phase2d3b2-gtk-minimal.c; compiled GTK 4.24.1 binary and base64 upload are in /tmp/seyrix-phase2d3b2-gtk/.

- Successful compositor command recorded in weston.log: weston --no-config --backend=headless-backend.so --socket=seyrix-bounded-weston --renderer=pixman --idle-time=0 --width=800 --height=600 --log=/tmp/seyrix-phase2d3b2-gtk/weston.log. Weston 15.0.1 reports a headless output and Pixman renderer. GTK reports GdkWaylandDisplay.
- State trace: before-switch mono_us=167336715 had page appearance; clicked-switch mono_us=167336798 had page desktop-dock; after-switch-250ms mono_us=167587125 still had desktop-dock, mapped, allocated 596×335. The same GDK toplevel and Wayland-surface pointers were retained.
- Client Wayland trace for wl_surface#3 shows the switched page buffer attach at 10:05:02.713348, damage_buffer(26,105,596,335) at 10:05:02.713354, and commit() at 10:05:02.718955. The matching feedback request (#23) is created before that attach and receives presented() at 10:05:02.729071. App state uses CLOCK_MONOTONIC; Weston reports CLOCK_MONOTONIC_RAW, so the event sequence is taken from the client trace and the clocks are not directly subtracted. Saved evidence: /tmp/seyrix-gtk-minimal-state.log and /tmp/seyrix-gtk-minimal-wayland.log.
- This confirms the minimal GTK stack changed state and submitted damaged surface content that headless Weston reported presented. It does not prove the resulting pixels because weston-screenshooter returned Output capture error: unauthorized; the saved 800×600 PNG is not usable as visual evidence. Weston’s Pixman/headless configuration also differs from Seyrix’s wlroots output path.

An initial Weston launch with --backend=/usr/lib/libweston-15/headless-backend.so failed with “unknown backend”; it was corrected to --backend=headless-backend.so before the successful state/Wayland trace. This was a compositor-launch setup failure, not a GTK result.

### Seyrix-side probe

Both targeted QEMU runs reached the virtio control shell and captured serial state, but neither established the graphical session needed to run the temporary app. /tmp/seyrix-compositor.log was absent when checked. The temporary readiness loop also used pgrep -x seyrix-compositor; Linux comm names are limited to 15 characters, so this check emitted “pattern ... longer than 15 characters” and cannot match that name. The second boot’s readiness stage ended INCOMPLETE after 70 seconds. Its fallback diagnostic block did not yield a complete tty1/session inventory. The minimal app under Seyrix is therefore **NOT REACHED**, not a reproduction of the Settings failure.

No missing transition in the failing Settings pipeline is confirmed. The earliest unknown transition is from GTK’s internal page state to a client Wayland buffer commit. If that commit exists, the next unobserved boundaries are compositor receipt, registered surface damage, output repaint/scene commit, and the final QMP pixels.

## D. Experiment log

All two QEMU boots used the retained ISO SHA-256 13de18ab6a37b7551c9a19321eb621428b5ece602d3de8412e3f377170c22992 and this QEMU hardware configuration:

    -machine q35,accel=tcg -cpu qemu64 -smp 2 -m 4G
    OVMF UEFI; read-only ISO
    -device VGA,edid=on,xres=800,yres=600
    virtio-serial port org.seyrix.qemu-control plus explicit ISA serial to serial.log
    USB keyboard/tablet; user-mode network; -display gtk,gl=off

The diagnostic guest kernel command line added loglevel=6 printk.time=1 to console=tty0 console=ttyS0,115200n8 video=VGA-1:800x600e. No physical disk was passed through.

| Test | Hypothesis and expected observation | Exact command/configuration | Actual observation; status and duration | Evidence |
|---|---|---|---|---|
| Existing reference review: successful serial and failing Settings click | Same QEMU UART can start normally; actual Settings click reproduces stale pixels. Expected a serial prompt and changed content pixels. | Retained candidate, 800×600 QEMU run beginning 2026-10-31 06:18:22Z; QEMU details above. Not run as a new test in this phase. | Serial prompt at ~61.7s; getty check passed. Settings QMP content ROI changed 0 pixels after category selection. Full prior run ended nonzero on the known GTK blocker. | /var/tmp/seyrix-linux/qemu-graphics-top-level-redraw-20261031T063000Z/ |
| Earlier serial-failure evidence review | Device/getty timing varies across retained boots. Expected failing device wait and later tty event. | Prior 800×600/640×480 runs documented in docs/phase2d3b-report.md; no rerun. | 90s timeout and dependency failure; later tty node/device active with inactive getty. USEC_INITIALIZED values in the report and retained logs are 157.238s and 161.677s. | /var/tmp/seyrix-linux/qemu-graphics-phase2d3b1-serial-diagnostic-2-20261031T090000Z/; prior report |
| Weston setup attempt | Start a known Wayland compositor. Expected headless Weston readiness. | weston --no-config --backend=/usr/lib/libweston-15/headless-backend.so --socket=seyrix-bounded-weston --renderer=pixman --idle-time=0 --width=800 --height=600 --log=/tmp/seyrix-phase2d3b2-gtk/weston.log | Failed before app launch: Weston rejected the absolute backend path. The log records the fatal backend error; the separate shell exit code was not retained. | /tmp/seyrix-phase2d3b2-gtk/weston.log |
| Minimal GTK4 stack under Weston | If basic GtkStack switching is faulty independent of Seyrix, expect no changed page or no client damage/commit/presentation. | Corrected Weston command in section C; 800×600 headless/Pixman, socket seyrix-bounded-weston. App: /tmp/seyrix-phase2d3b2-gtk/gtk-stack-pipeline-probe, with XDG_RUNTIME_DIR=/tmp/seyrix-phase2d3b2-gtk/runtime, WAYLAND_DISPLAY=seyrix-bounded-weston, GDK_BACKEND=wayland; app source enables WAYLAND_DEBUG=client. The literal app-launch shell command was not separately persisted; this is the effective environment reconstructed from the test setup and trace. | GTK state changed to Desktop & Dock; client attached/damaged/committed surface content; Weston returned presentation feedback. Visual capture was denied, so pixel comparison is INCOMPLETE. App and compositor exit codes were not retained. The app state log spans 14.360s from its start record to the before-close record. | /tmp/seyrix-gtk-minimal-state.log, /tmp/seyrix-gtk-minimal-wayland.log, /tmp/seyrix-phase2d3b2-gtk/weston.log, screenshooter.log, screenshot PNG |
| Targeted QEMU boot 1: serial timing plus optional Seyrix minimal app | Expected to capture tty/device/getty timeline and reach the minimal app if Wayland became ready. Explicit outer limit 360s, including artifact-retention preflight. | SEYRIX_QEMU_WORKFLOW=phase2d3b SEYRIX_QEMU_RESOLUTION_PROFILE=800x600 SEYRIX_QEMU_SETTINGS_RENDER_DIAGNOSTIC=1 SEYRIX_QEMU_SERIAL_KERNEL_TRACE=1 SEYRIX_QEMU_MINIMAL_GTK_DIAGNOSTIC=1 SEYRIX_QEMU_MINIMAL_GTK_BINARY_B64=/tmp/seyrix-phase2d3b2-gtk/gtk-stack-pipeline-probe.b64 timeout --signal=TERM --kill-after=10s 360s ./tests/qemu-graphics.sh /var/tmp/seyrix-linux/phase2d3b-top-level-redraw-20261031T063000Z/out/seyrix-phase2d3b-2026.10.31-x86_64.iso | Serial state capture passed at QEMU run time 10:17:04Z; udev/device/getty values are in section B. The active Wayland-session wait started but GTK launch was NOT REACHED before the outer timeout. QEMU run duration was 4m43s (10:13:02–10:17:45Z) inside the 360s outer cap; stage duration was 239.370s to control shell and 4.643s for state capture, then the readiness wait was interrupted. The external timeout ended the command; its shell exit code was not retained. run-result.txt says PASS, but .seyrix-failed and incomplete stage log show that this is not a passing run. | /var/tmp/seyrix-linux/qemu-graphics-phase2d3b2-rootcause-20261031T101138Z/ |
| Targeted QEMU boot 2: repeat serial timing and extend boot-to-probe allowance | Expected to capture the same timing and allow the compositor readiness stage to complete. Explicit outer limit 540s. | Same environment and ISO as boot 1, with timeout --signal=TERM --kill-after=10s 540s; exact QEMU command is in that run’s qemu-command.txt. | Serial state capture passed; USEC_INITIALIZED=159.336s, device active at 160.719s, getty inactive. Session readiness timed out after 70.344s. No app upload, Settings click, or graphical acceptance followed. Run duration 5m32s (10:21:26–10:26:58Z), exit status 1. | /var/tmp/seyrix-linux/qemu-graphics-20261031T102012Z/ |

The temporary QEMU diagnostic edits to tests/qemu-graphics.exp were restored byte-for-byte after the runs. The restored file’s SHA-256 matches the pre-run backup.

## E. Recommended fixes

Ranked by evidence and expected diagnostic value:

1. **GTK — instrument the actual failing surface at the protocol boundary.** In one targeted session, record the selected page event and wl_surface identity in the Settings client; correlate its buffer attach/damage/commit with wlroots surface commit, surface damage, handle_output_frame()/wlr_scene_output_commit(), then capture one QMP frame. This identifies the earliest missing boundary without a speculative redraw. Use a readiness check based on the actual Wayland socket and a process match that is not limited by comm length.
2. **Serial — test the device-triggered getty request without rebuilding an ISO.** The late tty event and dead getty justify testing the existing rule candidate, but not promoting it. In a future bounded boot, inject/enable the exact rule in a diagnostic-only overlay and capture the udev add properties plus systemd’s device/getty job transitions. Also capture early kernel tty registration so kernel delay can be separated from udev delay. Keep the rule only if the same event starts exactly one intended serial getty; otherwise remove it.
3. **Diagnostic harness — correct the session-ready process match and status propagation.** The pgrep -x seyrix-compositor check cannot match a comm name longer than 15 characters. Use a validated executable-path/process check plus the actual runtime Wayland socket; ensure the outer timeout cannot be recorded as a pass. This is diagnostic harness work, not a production correction.
4. **Do not change GTK or the compositor yet.** The GTK state path is structurally plausible, while the first failing transition has not been observed. Arbitrary redraw calls, layout changes, renderer replacement, timeout increases, and acceptance-test changes have no supporting evidence.

**Single most informative next experiment:** trace one real Settings category change on Seyrix from the GTK client’s Wayland commit through wlroots surface damage and the next output scene commit, then capture the same frame with QMP. This directly separates a client frame-submission failure from a compositor presentation failure. It should be a single bounded diagnostic session, not an acceptance campaign.

## F. Preservation report

- Active worktree: C:\Users\quake\Desktop\Coding\Seyrix-Linux-Phase2D2; branch phase2d3-system-settings. Its pre-existing Phase 2D.3B modifications and untracked evidence remain. This report is the only new worktree artifact from Phase 2D.3B.2.
- tests/qemu-graphics.exp was temporarily instrumented for the two diagnostic runs, then restored to its saved pre-run bytes: SHA-256 909AD0A0B7C1847D707DC421D34E349D75D333D5391A9517BA8D494740D81528.
- Protected original worktree C:\Users\quake\Desktop\Coding\Seyrix-Linux remains on its pre-existing phase2d1-hybrid-window-management branch and dirty state. No file was written there by this investigation; its harness timestamp remained 2026-10-26 21:12:54. An earlier patch attempt targeting that worktree was rejected before writing.
- Retained ISO SHA-256 13de18ab6a37b7551c9a19321eb621428b5ece602d3de8412e3f377170c22992 remains unpromoted. No build or ISO replacement occurred.
- The two new QEMU evidence directories total about 10.0 MB. The latest retention preflight reported 29,457,698,816 managed bytes, 4,294,967,296 bytes planned, and 993,121,947,648 bytes free. A post-run filesystem query reported 993,116,880,896 bytes available. This stays inside the 40 GiB managed-artifact budget and well above the 30 GiB free-space floor.
- No QEMU process remained after run 2; the run wrapper released its lock. No WSL VHDX or backup was moved, compacted, resized, or deleted. No reset, clean, commit, push, merge, or ISO promotion was performed.