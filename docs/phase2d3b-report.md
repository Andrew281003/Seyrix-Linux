# Phase 2D.3B — Final Stabilization and Graphical Acceptance

**Final result: PHASE 2D.3B INCOMPLETE**
**Assessment date: 2026-10-31**

Phase 2D.3B.1 targeted blocker resolution remains **INCOMPLETE**. Existing QMP evidence still shows the first GTK category transition selecting and mapping Desktop & Dock while the displayed content remains Appearance. Targeted Wayland-trace runs did not reach a Settings page click, so the frame and compositor path is still unisolated. Serial diagnostics show that ttyS0 may be ready before the getty deadline or may appear well after it; when late, systemd leaves the serial getty inactive. A targeted udev rule and improved harness diagnostics are present in source, but the udev behavior has not been verified in a fresh ISO.

No fresh ISO, native regression campaign, or full graphical acceptance campaign was run for Phase 2D.3B.1. Neither blocker is considered fixed, and no ISO was promoted.

No Phase 2D.3C work, editable settings, configuration transactions, or unrelated features were started.

## Revision and preservation

- Active worktree: C:\Users\quake\Desktop\Coding\Seyrix-Linux-Phase2D2
- Branch: phase2d3-system-settings
- HEAD: 7002c87bccc3a5881d7e50cdb5a1f0bc5c3a3153
- The active worktree has local Phase 2D.3B changes. They were preserved; no reset, clean, checkout, commit, push, or merge was performed during final stabilization.
- Protected original worktree C:\Users\quake\Desktop\Coding\Seyrix-Linux and the older standalone Seyrix kernel repository were not modified.
- Arch Linux stores its root filesystem on /dev/sdd, backed by E:\WSL\archlinux\ext4.vhdx. No WSL virtual disk or backup was moved, compacted, resized, or deleted.

The existing graphical candidate built at 2026-10-31T06:30Z has SHA-256 13de18ab6a37b7551c9a19321eb621428b5ece602d3de8412e3f377170c22992. It remains unpromoted and was used only for targeted diagnosis. It does not contain the new ttyS0 udev rule. No ISO was built from the Phase 2D.3B.1 source changes, so there is no final ISO provenance for this phase.

The retention pointer still names the previously promoted Phase 2D.3B ISO, SHA-256 274d6064e3afcdd0bc48248c178a96419370635a1289741825b199ffc3fd01ca. The separately verified Phase 2D.2 ISO remains present with SHA-256 9ace6c35c5c67f7bb2e4a8b181fe04d587c9cdda7b49df937abe52f190a49fb2. The failed candidate did not change latest.txt.

## Implementation and IPC architecture

Settings is a native C17 GTK4 Wayland application with Appearance, Desktop & Dock, Window Management, Displays, Input, and About Seyrix categories. It uses GTK widgets and a normal toplevel surface, with a sidebar on wider windows and a compact selector on narrow windows. Search matches setting descriptions. Unsupported controls are visibly disabled. System and compositor information is read-only.

The compositor endpoint is a separate Unix SOCK_SEQPACKET listener at $XDG_RUNTIME_DIR/seyrix/settings-v1.sock. It uses a private session-owned runtime directory, owner-only socket permissions, safe stale-socket checks, and inode-aware cleanup. SO_PEERCRED, uid, and session checks authenticate clients. Protocol 1.0 provides hello, snapshot, subscribe, and unsubscribe operations with request correlation, revisions, bounded clients/subscribers/packets, and explicit errors. Notifications expose bounded state changes. The shell's private IPC remains separate.

The protocol has no setting-write, command-execution, privilege, or arbitrary-file operation. Same-uid processes in the authenticated session can connect; the endpoint does not grant root or system-wide authority.

## Root-cause investigation

### Serial acceptance

The QEMU command line in the earlier 800×600 run and the failing 800×600 diagnostic run uses the same guest hardware configuration: q35/TCG, two vCPUs, 4 GiB, `VGA,edid=on,xres=800,yres=600`, an explicit ISA serial device connected to `serial.log`, and the virtio control port. Only run names, paths, and QMP ports differ. Both guest kernel command lines contain `console=tty0 console=ttyS0,115200n8 video=VGA-1:800x600e`.

The earlier 800×600 run reached the user control service in 58.9 seconds, printed the real `seyrix-dev login:` prompt, and verified `serial-getty@ttyS0` active. In the 2026-10-31T09:06Z 800×600 diagnostic run with the same QEMU device and kernel parameters, control became available after 255.3 seconds. The raw serial log shows systemd timing out its 90-second `/dev/ttyS0` start job and then reporting `Dependency failed for Serial Getty on ttyS0`. By the later read-only query, `/dev/ttyS0` exists and its device unit is active/plugged, but `serial-getty@ttyS0.service` is inactive/dead. Udev reports `USEC_INITIALIZED=161676791` and the tty device has the `systemd` tag. This is late, nondeterministic tty device activation relative to the getty's initial device wait; it is not a permanently missing UART. Two 640×480 runs show the same outcome, with `USEC_INITIALIZED=157237948` in the diagnostic run. The standard-VGA retry does not change the QEMU serial hardware or fix the issue.

The unit state shows `serial-getty@ttyS0.service` ordered after and bound to `dev-ttyS0.device`; the device unit reports an empty `Wants=` list. When the device arrives after the initial getty transaction timed out, no device-triggered start retries the getty. The separate `seyrix-qemu-control.service` is activated by its named virtio-serial port and has no ttyS0 dependency; this explains why the guest-control shell can work while the serial login does not. The systemd device start job waits 90 seconds, the harness allows 300 seconds for the real serial prompt, and the guest-control stage is bounded at 240 seconds. One run reached control at 255.3 seconds due to observed stage overhead; another stopped at the 240-second control timeout before the UI session. The shell user cannot read the system journal, so the dependency failure is evidenced by the raw systemd serial output and `systemctl show` state rather than a unit journal. The harness previously searched for the timeout phrase without stripping systemd's ANSI escapes; that caused an unnecessary wait. `tests/qemu-graphics.exp` now normalizes CSI sequences and carriage returns before matching. A focused check against the saved raw log produced `RAW_MARKER=-1` and `NORMALIZED_MARKER=26918`, and the live harness then collected the late-device/getty state instead of timing out blindly.

The source now contains `config/graphics-overlay/airootfs/etc/udev/rules.d/99-seyrix-serial-getty.rules`, which adds a systemd device want for `serial-getty@ttyS0.service` when ttyS0 is added. This is the narrow fix candidate for the late-device race; it does not increase a timeout or change graphics components. It has **not** been packaged or tested against a real 640×480 session. Serial blocker status: **FAIL reproduced; fix INCOMPLETE / unverified**.

### GTK category transition

At 800×600, the real launcher opens the actual Settings process, and the Settings window receives focus. After selecting Desktop & Dock, GTK reports the expected selected row and dropdown, and the Desktop page is mapped at 583×393 while Appearance is unmapped. Yet the genuine QMP screenshot comparison of the content region reports zero changed pixels. The actual window still displays Appearance. This rules out a false harness click or an incorrect selected-page state, but does not isolate whether frame damage is lost in GTK/GSK or later in the rendering/compositor path.

Source inspection confirms that `set_visible_page` calls `gtk_stack_set_visible_child`, GTK allocates the page children, the compositor creates normal wlroots xdg scene surfaces, and output frames use `wlr_scene_output_commit`. This does not prove what happened for the failing click. No `WAYLAND_DEBUG` client commit/damage trace or compositor receipt/presentation trace was captured. The Phase 2D.3B.1 render-only harness mode was attempted against the existing ISO but did not reach the page click: one boot stopped at the serial failure, two early diagnostic modes waited before the tty1 graphical login was established, and the mode moved after graphical-session/output readiness then timed out waiting for the guest control service. These runs are recorded as `FAIL` or `INCOMPLETE`, not as GTK pipeline evidence.

The retained 800×600 QMP screenshot remains direct evidence of the presentation defect, but the rendering root cause and smallest correct fix are **unresolved**. No GTK or compositor code was changed in this targeted resolution attempt. GTK blocker status: **FAIL observed; root cause and fix INCOMPLETE**.

## Native tests, analysis, and sanitizers

| Check | Result |
|---|---|
| Candidate native compositor tests | PASS, 3/3 |
| Candidate native desktop-shell tests | PASS, 5/5 |
| Candidate native Settings tests | PASS, 3/3 |
| Settings tests after removal of temporary redraw diagnostics | PASS, 3/3 |
| GCC -fanalyzer / -Werror Settings and compositor builds | PASS in saved static-analysis evidence; not rerun after the final diagnostic cleanup |
| ASan/UBSan/LSan compositor tests | PASS, 3/3 in saved sanitizer evidence |
| ASan/UBSan/LSan Settings IPC/model/client tests | PASS, 3/3 in saved sanitizer evidence |
| ASan/UBSan/LSan shell tests | INCOMPLETE, 4/5 |

The unsuppressed shell LeakSanitizer finding is 140,884 bytes in 4,817 allocations. Traces run through Fontconfig FcValueSave and Pango font rendering during the shell UI test. No suppression was added. This evidence is in docs/evidence/phase2d3b/sanitizers/shell-testlog.txt. It is reported as a library-retention finding; the present evidence does not establish a Phase 2D.3B application-owned leak. The previously documented wlroots 0.20.2/Mesa EGL teardown retention was not re-evaluated in this campaign.

Bash syntax validation passed for build/retention.sh, tests/retention-validation.sh, build/build-phase2d3b-iso.sh, and tests/qemu-graphics.sh.

## Graphical QEMU acceptance matrix

The current 800×600 candidate run is qemu-graphics-top-level-redraw-20261031T063000Z. Results below describe that run unless an older run is explicitly named.

| Area | Result | Evidence / limit |
|---|---|---|
| UEFI boot to graphical control session, 800×600 | PASS in the earlier candidate run; INCOMPLETE in the last targeted run | The earlier run reached graphical acceptance. The last bounded run timed out at the guest-control stage before tty1 login. |
| Real ttyS0 prompt, 800×600 | PASS in the earlier candidate run; FAIL in the 09:06Z diagnostic run | Same QEMU serial device and guest kernel arguments. Late ttyS0 activation left the getty inactive in the diagnostic run. |
| Desktop launcher opens native Settings | PASS in the earlier candidate run | Actual Settings process launched as uid 1000, became visible, and received focus. |
| Search matching and nonmatching query | PASS | Actual Settings screenshots captured for both results. |
| Appearance page initial render | PASS | Genuine application screenshot. |
| Category navigation / all six categories | FAIL | First Appearance → Desktop & Dock transition selected the GTK page but did not change displayed pixels. Remaining page transitions were not run. |
| GTK frame, Wayland commit, compositor receipt, and output presentation trace | INCOMPLETE | Targeted render-only runs did not reach a Settings page click. The observed stale screenshot does not identify the failing pipeline stage. |
| Larger-resolution usability | INCOMPLETE | 800×600 displayed the app in the earlier run, but navigation failure prevents full layout acceptance. |
| 640×480 serial and graphical acceptance | FAIL / INCOMPLETE | Both QEMU display variants failed serial-getty@ttyS0. No real 640×480 Settings session was captured. The new udev rule is unverified. |
| IPC endpoint creation and initial snapshot | PASS | Live compositor/client probe; snapshot included revision 9, active workspace 1, one output, and three keyboards. |
| Protocol version and request correlation | PASS | Live endpoint probe. |
| Subscription and state notification | PASS | Live notification received. |
| Malformed and oversized message rejection | PASS | Live probe. |
| Unauthorized session rejection | PASS | Wrong-session client rejected. A separate live different-uid client was not exercised. |
| Client disconnect handling | PASS | Probe disconnect completed without compositor failure. |
| Endpoint cleanup on application/session shutdown | INCOMPLETE in current run | The harness stopped at the page-transition failure. |
| Compositor restart and Settings reconnect | PASS in earlier run only | qemu-graphics-20261031T024415Z; not repeated on the current candidate. |
| Floating, Automatic Tiling, Hybrid modes | PASS in earlier run only | qemu-graphics-20261031T024415Z; not repeated after current candidate Settings launch. |
| Dock/taskbar, launcher, workspaces | PASS in earlier run; partial current run | Current run checked startup and Settings launch, then stopped at navigation. |
| Repeated open/close and clean shutdown | PASS in earlier run only | Not reached in current candidate run. |
| End-to-end configuration unchanged comparison | INCOMPLETE | A baseline was recorded, but the final comparison stage did not run. The endpoint exposes no write operation. |

The earlier 02:44Z campaign is retained as historical evidence, not as proof that the current candidate passed the full matrix. It did not contain the visual category-transition check that exposed the current failure.

## Evidence locations

All QEMU output was written under the E-backed Arch filesystem at /var/tmp/seyrix-linux.

- 800×600 original candidate run: /var/tmp/seyrix-linux/qemu-graphics-top-level-redraw-20261031T063000Z/
  - test.log, serial.log, expect-session.log, qemu-command.txt, run-result.txt, qemu-process-state.txt, screenshots.sha256
  - phase2d3b-appearance.ppm/png shows the actual Settings window.
  - phase2d3b-search.ppm/png and phase2d3b-search-no-match.ppm/png show real matching and nonmatching search states.
  - phase2d3b-page-current.ppm/png shows Desktop & Dock highlighted while Appearance content remains; this is failure evidence.
- 800×600 late-tty diagnostic: /var/tmp/seyrix-linux/qemu-graphics-phase2d3b1-gtk-pipeline-20261031T091000Z/
  - test.log, serial.log, qemu-command.txt, run-result.txt. Same VGA/ISA UART configuration and guest command line as the earlier successful serial-stage run; ttyS0 udev activation was reported at 161.7 seconds and the getty remained inactive.
- GTK diagnostic attempts: /var/tmp/seyrix-linux/qemu-graphics-phase2d3b1-gtk-pipeline-direct-20261031T091500Z/, /var/tmp/seyrix-linux/qemu-graphics-phase2d3b1-gtk-pipeline-corrected-20261031T092200Z/, and /var/tmp/seyrix-linux/qemu-graphics-phase2d3b1-gtk-pipeline-session-20261031T092900Z/.
  - Each retains run-result.txt and test.log. None contains Settings transition screenshots or a client protocol trace; the last attempt timed out before the guest-control stage.
- 640×480 serial diagnostic: /var/tmp/seyrix-linux/qemu-graphics-phase2d3b1-serial-diagnostic-2-20261031T090000Z/; test.log and serial.log contain the late tty and inactive-getty state.
- 640×480 virtio-vga run: /var/tmp/seyrix-linux/qemu-graphics-640-20261031T070000Z/
- 640×480 standard-VGA retry: /var/tmp/seyrix-linux/qemu-graphics-640-standard-vga-20261031T071000Z/
  - Each failed run retains run-result.txt and serial.log. Neither contains an accepted Settings screenshot.
- Wayland diagnostic retry: /var/tmp/seyrix-linux/qemu-graphics-wayland-trace-retry-20261031T064500Z/; INCOMPLETE because its wrapper timed out before Settings launch.
- Earlier broader graphical run: /var/tmp/seyrix-linux/history/qemu-graphics-20261031T024415Z.summary

PPM screenshots are original QMP screen captures. PNG files are format conversions of those captures. No terminal or unrelated-window screenshot is presented as Settings evidence.

## Storage and retention verification

The retention helper was inspected before the build/QEMU work. The build/QEMU lock serialized managed campaigns. Preflight enforced the 40 GiB artifact limit, 30 GiB minimum free space, 16 GiB build reserve, and 4 GiB QEMU reserve. The failed candidate remained marked failed and was not promoted.

Storage measurements:

| Point | Artifact-root use | Free space |
|---|---:|---:|
| Before major candidate build | 25,055,256,576 bytes | 997,534,793,728 bytes |
| During ISO build | About 30 GiB filesystem use | About 927 GiB |
| After Phase 2D.3B.1 targeted QEMU captures | About 29.45 GB under /var/tmp/seyrix-linux | About 993.1 GB |

The final filesystem reading was /dev/sdd, 1,081,101,176,832 bytes total, 32,951,980,032 used, 993,156,841,472 available. Artifact use remained below the 42,949,672,960-byte cap and free space remained well above 30 GiB.

The safe fixture suite passed 14/14 checks. It covered configured limits, build/QEMU lock serialization, artifact and free-space rejection, reserves, Git/Windows/VHDX path rejection, active build protection, failed and interrupted evidence retention, latest-pointer protection, successful promotion and staging cleanup, and SEYRIX_RETENTION_KEEP_EVIDENCE=1 / SEYRIX_RETENTION_KEEP_ARTIFACTS=1 behavior. The test uses small temporary fixtures and removes them. Raw output: docs/evidence/phase2d3b/retention-validation-20261031.log.

The active E:\WSL\archlinux\ext4.vhdx and verified backup remain untouched. No Windows files, WSL disks, or files outside managed test fixtures were cleaned.

## Files changed and purpose

The Phase 2D.3B working-tree changes are local and uncommitted at the HEAD above. Principal implementation and validation paths:

- Settings and compositor: desktop/settings/src/settings-window.c; desktop/compositor/meson.build; desktop/compositor/meson_options.txt; desktop/compositor/src/settings-control.c; desktop/compositor/src/shell.c; desktop/compositor/src/window-management.c; desktop/compositor/src/window-state.c; desktop/compositor/src/window.c. These provide the native UI, read-only compositor endpoint, surface/window integration, and build wiring.
- Launcher and probes: desktop/shell/src/main.c; desktop/protocol-probe/src/probe.c; tests/settings-endpoint-probe.c. These integrate the desktop launcher and exercise the live protocol.
- Packaging and retention: build/build-iso.sh; build/build-phase2a-iso.sh; build/build-phase2b-iso.sh; build/build-phase2c-iso.sh; build/build-phase2d2-iso.sh; build/build-phase2d3b-iso.sh; build/retention.sh; docs/build-guide.md. These add shared storage preflight, serialization, artifact retention, and promotion rules across builds.
- QEMU and safety fixtures: config/graphics-overlay/airootfs/etc/systemd/system/seyrix-qemu-control.service; config/graphics-overlay/airootfs/etc/udev/rules.d/95-seyrix-qemu-control.rules; config/graphics-overlay/airootfs/etc/udev/rules.d/99-seyrix-serial-getty.rules; tests/headless-integration.sh; tests/qemu-graphics.exp; tests/qemu-graphics.sh; tests/qemu-guest-helper.sh; tests/qemu-panel-pixel-check.py; tests/qemu-settings-page-check.py; tests/qemu-uefi.sh; tests/retention-validation.sh. The Phase 2D.3B.1 harness changes normalize serial ANSI output, collect read-only UART/getty state after a timeout, and provide a render-only trace mode. The ttyS0 udev rule remains unverified.
- Reports and evidence: docs/phase2d3b-report.md; docs/storage-cleanup-retention-report.md; docs/storage-cleanup-artifact-manifest.tsv; docs/evidence/phase2d3b/. Evidence directories retain build/test logs, sanitizer and static-analysis logs, and graphical runs.

## Phase 2D.3B.1 disposition

| Blocker / acceptance item | Status | Finding |
|---|---|---|
| GTK page pixels | **FAIL / INCOMPLETE** | Stale pixels are confirmed by the existing QMP screenshot. Client damage, Wayland commit, wlroots receipt, and output presentation remain unobserved; no fix was made. |
| 640×480 ttyS0/getty | **FAIL / INCOMPLETE** | ttyS0 is late in failing boots and appears after the getty's device wait expires. The targeted udev `SYSTEMD_WANTS` rule is source-only and unverified. |
| Native regression suites after blocker fixes | **NOT RUN** | Both blockers were not resolved. Previously recorded native results remain historical evidence only. |
| Fresh ISO, SHA-256, and provenance | **NOT RUN** | No final ISO was built from the current source. The existing candidate remains unpromoted. |
| One bounded full graphical acceptance campaign | **NOT RUN** | Required only after both blockers are fixed. |
| 640×480 graphical Settings, six-category navigation, search/IPC/lifecycle, no-write, and desktop regressions | **INCOMPLETE** | No successful Settings session at 640×480; the existing 800×600 page-pixel blocker remains. |
| Verified-release vs failed-candidate retention metadata | **PASS for existing safeguards** | New diagnostic runs remain retained as failed/incomplete evidence, and no latest/promoted pointer changed. |

Phase 2D.3B remains **INCOMPLETE**. No Phase 2D.3C work began. Continue only with a valid GTK/GSK-to-Wayland-to-wlroots/output trace and a real post-fix 640×480 boot. Do not promote an ISO until the full acceptance matrix passes.
