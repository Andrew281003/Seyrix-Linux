# Phase 2D.3B.4a — Native ext4 Build Recovery & Live Rendering Diagnosis

**Overall status: INCOMPLETE.** The replacement diagnostic ISO built and passed structural validation. The real Seyrix graphical session and native Settings window were reached on the second and final authorized QEMU boot. The expected page change was not visible, but the GTK/Wayland/compositor trace was missing, so the stale-page root cause is not confirmed. Phase 2D.3B remains incomplete; Phase 2D.3C was not started.

| Area | Result |
|---|---|
| Native ext4 build and ISO validation | PASS |
| Source, harness, and native tests before packaging | PASS |
| Serial recovery and real graphical-session readiness | PASS on boot 2 |
| Settings Appearance → Desktop & Dock visual update | FAIL: no pixel changed and selector remained Appearance |
| GTK4 → Wayland → wlroots → output trace | INCOMPLETE: Settings trace file was absent |
| Confirmed cause of stale-page bug | INCOMPLETE |

## Preservation and previous process state

- Read the prior report at `E:\Seyrix-Phase2D3B4-Diagnostic-20261031\report.md`. It records the previous `pacman` process blocked in `p9_client_rpc` while building through `/mnt/e` (9P/DrvFS). Its parent build processes had exited after the recorded SIGTERM; the D-state `pacman` remained at that time.
- At this run's initial inspection, the Arch WSL distribution was stopped. It was started normally; fresh process inventory showed no `pacman` or `mkarchiso`. No direct manipulation, relocation, compaction, or deletion of the VHDX or backup file was performed, and no WSL terminate/shutdown command was used. Build and QEMU artifacts were written under `/var/tmp` in the running distro's ext4 filesystem, as required.
- The failed diagnostic tree on E: remains present. Both existing development worktrees and their dirty files were retained. No reset, clean, force-checkout, merge, or push was performed.
- The verified Phase 2D.2 ISO remains at `/var/tmp/seyrix-linux/verified/seyrix-phase2d2-2026.10.27-x86_64.iso`, 1,062,240,256 bytes, SHA-256 `9ace6c35c5c67f7bb2e4a8b181fe04d587c9cdda7b49df937abe52f190a49fb2`.
- The verified Phase 2D.3B ISO and `verified/latest.txt` were not changed. The pointer still names `seyrix-phase2d3b-2026.10.31-x86_64.iso`, SHA-256 `274d6064e3afcdd0bc48248c178a96419370635a1289741825b199ffc3fd01ca`. The new ISO was not promoted and no release metadata was updated.
- A WSL Git read of the Windows-mounted source tree later entered `p9_client_rpc`; SIGTERM was sent only to that exact Git process, which exited. Subsequent Git checks used Windows Git. Retention `du` scans also briefly waited in ext4 `__wait_on_buffer` during directory enumeration and recovered naturally; no WSL process was terminated.
- An initial wrapper preflight failed before tests or `mkarchiso` because WSL Git could not resolve the Windows worktree `.git` pointer. Its small managed failed-attempt directory was retained. The corrected flow used Windows Git for the source diff check. Exactly one `mkarchiso` invocation ran.

## Filesystem, retention, and build

`findmnt -T /var/tmp/seyrix-linux` reported `/dev/sdd ext4 /`. `/mnt/e` reported `9p`; the new build tree was under native ext4 at `/var/tmp/seyrix-linux/phase2d3b4a-native-20261031T141141Z`.

The configured controls remained a 40 GiB managed-artifact maximum and 30 GiB minimum free space. The build preflight reserved 8 GiB for build output plus 4 GiB for QEMU, retained existing artifacts, and passed. Before the second QEMU boot, usage was 33,893,588,992 bytes, the QEMU reserve was 4,294,967,296 bytes, and free space was 988,668,592,128 bytes. Final measured managed usage was 33,904,680,960 bytes (about 31.58 GiB), with 988,657,500,160 bytes free (about 920.5 GiB). Both limits were satisfied; no artifact cleanup or protected-artifact deletion occurred.

The single replacement ISO is:

- Path: `/var/tmp/seyrix-linux/phase2d3b4a-native-20261031T141141Z/out/seyrix-phase2d3b-2026.10.31-x86_64.iso`
- Size: 1,151,631,360 bytes
- SHA-256: `7bf470ba904004ddb6e1670d5c162690a2df0e4cd9b16cabecada1b02c91c196`
- Result: **PASS**. `file` identified bootable ISO 9660; `xorriso` read the El Torito catalog and BIOS/UEFI boot images; the ISO contains `seyrix/boot/x86_64/vmlinuz-linux`. Independent size and SHA-256 checks matched the recorded values.

The build emitted warnings that were left visible: mkarchiso could not change permissions on a missing `/root` path, pacman reported `.pacnew` files for `/etc/issue` and `/etc/shadow`, and `grub-mkstandalone` reported an SBAT level mismatch. The build exited successfully; compiler errors were not suppressed and warning settings were not weakened.

## Source and native test results

All requested pre-build checks passed:

- `git diff --check`, Bash syntax checks for the build and QEMU scripts, Python AST syntax for `settings-render-trace-launcher.py`, and Tcl completeness for `qemu-graphics.exp` — **PASS**.
- Compositor tests: 3/3 — **PASS**.
- Shell tests: 5/5 — **PASS**.
- Settings tests: 3/3 — **PASS**.
- Protocol probe compilation — **PASS**.
- The build script's compositor and Settings test reruns — **PASS**.

After boot 1 exposed the serial-device issue, the diagnostic harness was updated in `tests/qemu-graphics.exp` with a QMP tty2 recovery path and an `@` key mapping. The modified Tcl file passed `info complete` and `git diff --check`. This was a host-side test-harness change only; it was not included in the ISO and is not a production fix.

## QEMU readiness and Settings reproduction

Two QEMU boots were performed, using the same new ISO. The authorized budget was 2; no further boot was started.

### Boot 1 — serial readiness failure

Run directory: `/var/tmp/seyrix-linux/qemu-graphics-phase2d3b4a-20261031T142509Z`.

The readiness harness waited 300 seconds for `seyrix-dev login:`. Systemd output showed a start job for `/dev/ttyS0`, then `Timed out waiting for device /dev/ttyS0` and `Dependency failed for Serial Getty on ttyS0`. QMP reported `VM status: running`; the kernel/VM had not crashed. Systemd subsequently reached Multi-User System and Graphical Interface and started tty1 getty. The QMP timeout screenshot showed the automatic `seyrix` tty1 shell and its `start-seyrix` instructions, not a compositor window. The harness recorded **INCOMPLETE** for this boot; it did not mistake that timeout for a kernel or compositor failure.

### Boot 2 — tty2 recovery and graphical session

Run directory: `/var/tmp/seyrix-linux/qemu-graphics-phase2d3b4a-recovery-20261031T143743Z`.

The same `/dev/ttyS0` dependency failure recurred. The harness used QMP to switch to tty2, log in as root, create `/dev/ttyS0`, trigger udev, reset the failed device/getty units, and start `serial-getty@ttyS0`. The serial login prompt then appeared; `SERIAL_GETTY_RECOVERY_LOGIN=passed` and `BOOT_TO_LOGIN_MS=334196` were recorded. This confirms the combined recovery path restored serial access; the run did not isolate which individual command was necessary.

The harness then reached the real session:

- `TTY1_DEV_SESSION_READY=passed`; loginctl showed the `seyrix` user session on tty1.
- `COMPOSITOR_READY=passed`; `RENDER_SESSION_READY=passed`.
- Seyrix user shell PID 777, UID 1000; compositor PID 690, UID 1000; Weston terminal PID 778.
- Live socket path recorded as `/run/user/1000/wayland-0`.
- `GRAPHICAL_BOOT_MS=443875`; compositor startup after `start-seyrix` was 83,839 ms.
- QMP captured the 640×480 desktop and a visible native System Settings window. The Settings process was PID 866, UID 1000, executable `/usr/bin/seyrix-settings`.

The readiness command checked the session, process identities, and socket path. It did not record a separate socket `stat` owner check or a Wayland protocol connection probe; those remain unverified.

## Settings rendering trace and pixel comparison

The harness opened the Settings launcher, selected its Settings result, captured the Appearance page, clicked the page selector at `(320,156)`, then sent Home, Down, and Enter for Desktop & Dock. The screenshot after the action still displayed the Appearance selector and Appearance content.

QMP screenshots are retained as:

- `phase2d3b-appearance-640x480.ppm`
- `phase2d3b-desktop-dock-640x480.ppm`
- `phase2d3b-launcher-search.ppm`

All are in the boot 2 run directory above. The Appearance and Desktop & Dock PPMs are 640×480 and byte-for-byte pixel identical: 0 of 307,200 full-frame pixels changed. In the page-content region y=194..421, 0 of 145,920 pixels changed; in y=210..420, 0 of 135,040 changed. The observed expected visual update therefore **FAILS**. The second screenshot still shows “Appearance”; it does not show a Desktop & Dock page.

The event chain is incomplete:

| Boundary | Evidence | Result |
|---|---|---|
| GTK4 selected page and widget mapping | `SETTINGS_PAGE_APPEARANCE_SELECTION` and `SETTINGS_PAGE_APPEARANCE_MAP` were empty; `/run/user/1000/seyrix-settings-render.trace` was missing | INCOMPLETE |
| Settings client Wayland attach, damage, and commit | The `WAYLAND_DEBUG` client trace file was missing | INCOMPLETE |
| Corresponding wlroots surface commit and damage | No Settings surface identity or Settings commit sequence was recorded | INCOMPLETE |
| Seyrix scene/output commit and presentation | No Settings-correlated output commit or presentation event was recorded | INCOMPLETE |
| Screenshot comparison | Both post-action page screenshots were pixel identical | FAIL: no visible update |

The launcher search screenshot showed “Settings” selected, and the native Settings process existed, but the trace-only launch gate returned `SETTINGS_RENDER_TRACE_LAUNCH=failed`; the page-selection and trace files remained absent. The compositor startup trace contains its output setup and terminal mapping, but no Settings-correlated surface sequence. Therefore the earliest confirmed boundary is the page-selection/input step: the expected selection is not visible, and no GTK selection event was captured. The evidence cannot distinguish a QMP input that did not activate the selector from GTK accepting a selection without producing updated content. There is no evidence to assign this to Wayland buffer handling, wlroots, scene damage, or presentation.

## Cause, changes, and remaining work

The serial issue is confirmed separately from the Settings bug: the enabled experimental serial-getty link had no usable `/dev/ttyS0` device-unit readiness until the tty2 recovery sequence ran. This explains the initial harness login timeout; it does not explain the stale Settings page.

The stale-page root cause remains **INCOMPLETE**. No production correction or speculative redraw was added. The pre-build native tests passed, but there is no regression result for a Settings rendering fix because no fix was justified by the available trace. A future fix that changes the ISO will require a separately authorized rebuild. The second boot's QEMU harness ended **INCOMPLETE** after reporting missing Settings trace data; no further boot was performed.

## Git status and preservation

- Original development worktree `C:\Users\quake\Desktop\Coding\Seyrix-Linux`: existing modified configuration/test files and untracked Phase 2C evidence remained present; no files in this worktree were edited for 4a.
- Phase 2D.2 worktree `C:\Users\quake\Desktop\Coding\Seyrix-Linux-Phase2D2`: existing modified build/compositor/Settings/test files and untracked evidence/artifacts remained present; no files in this worktree were edited for 4a.
- Detached diagnostic worktree `C:\Users\quake\.codex\worktrees\phase2d3b-diagnostic\Seyrix-Linux`: pre-existing Phase 2D.3B changes remain uncommitted. This task additionally changed `tests/qemu-graphics.exp` for the observed tty2 recovery and added this report. `tests/qemu-graphics.sh`, the build script, compositor, Settings code, and diagnostic launcher remain as they were at task start.
- The failed E: diagnostic tree remains present. Existing verified ISO files, hashes, and latest pointer remain unchanged. No ISO promotion, release metadata update, commit, merge, or push occurred.

**Phase 2D.3B acceptance remains INCOMPLETE.** The build and real graphical session were recovered, but the GTK-to-output trace needed to identify the exact stale-page cause was not obtained.
