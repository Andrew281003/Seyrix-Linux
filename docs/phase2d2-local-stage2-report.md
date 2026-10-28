# Seyrix-Linux Phase 2D.2 — Stage 2 graphical QEMU acceptance

**Decision: PHASE 2D.2 PASS**

The final Phase 2D.2 image passed the complete graphical acceptance run in QEMU. The run verified the native desktop shell, graphical input, launcher, switcher, dock, inherited window-management behavior, shell recovery, normal shutdown, Weston fallback, and compositor cleanup after both SIGTERM and SIGKILL. No new Seyrix-owned critical defect remains in the tested paths.

Known library memory-retention findings and the boundary of the IPC tests remain visible below. Dock auto-hide remains incomplete.

## 1. Baseline and environment

- Target worktree: `C:\Users\quake\Desktop\Coding\Seyrix-Linux-Phase2D2`
- Target branch at start: `phase2d2-desktop-shell`
- Starting HEAD: `245adc49141c0dee003afa87102d51b7341c5edf`
- Protected original: branch `phase2d1-hybrid-window-management`, HEAD `d1873c68a29b24e285e58568551b030985ef8b98`.
- The target's Stage 1 report already had a one-line text corruption in its working-tree diff. It was left untouched and excluded from the commit. The protected original's seven Archiso edits and seven Phase 2C.2 EGL investigation changes were unchanged; the status snapshot is in [preservation-proof.txt](evidence/phase2d2-local-stage2/preservation-proof.txt).
- Build and VM host: Arch Linux on WSL2, wlroots 0.20.2, QEMU 11.1.2, q35, OVMF UEFI, GTK display through WSLg, QEMU TCG. The guest used 4 GiB, two vCPUs, USB keyboard and tablet input, and a read-only ISO. Only the per-run OVMF variables file was writable; no physical disk was passed through.

## 2. ISO build and identity

The ISO was built in the fresh isolated directory `/var/tmp/seyrix-linux/phase2d2-stage2-final-focusfix-02`. The builder reconstructs tracked Archiso symlinks in a copied profile because this Windows checkout represents those links as text files; it does not change the protected checkout or system-wide files.

- ISO: `/var/tmp/seyrix-linux/phase2d2-stage2-final-focusfix-02/out/seyrix-phase2d2-2026.10.27-x86_64.iso`
- Size: 1,062,240,256 bytes
- SHA-256: `9ace6c35c5c67f7bb2e4a8b181fe04d587c9cdda7b49df937abe52f190a49fb2`
- Installed wlroots: `wlroots0.20 0.20.2-1`
- Guest compositor selected its GLES2 renderer with software rendering enabled; the log identifies `llvmpipe (LLVM 23.1.3, 128 bits)`. Weston fallback also reports llvmpipe. See [run 12 test log](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/test.log).

Build package inventory, tool versions, profile hashes, and ISO hash are preserved in [build-metadata-final-focusfix](evidence/phase2d2-local-stage2/build-metadata-final-focusfix/).

## 3. QEMU command and boot result

The run used the project's locked `tests/qemu-graphics.sh` harness with `SEYRIX_QEMU_WORKFLOW=phase2d2`. The exact QEMU command is preserved in [qemu-command.txt](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/qemu-command.txt); its shell-rendered form was:

```text
/usr/sbin/qemu-system-x86_64 -name seyrix-phase2d2-graphics-test-20261028T000828Z -machine q35\,accel=tcg -cpu qemu64 -smp 2 -m 4G -drive if=pflash\,format=raw\,readonly=on\,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash\,format=raw\,file=/mnt/c/Users/quake/Desktop/Coding/Seyrix-Linux-Phase2D2/docs/evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/OVMF_VARS.fd -cdrom /var/tmp/seyrix-linux/phase2d2-stage2-final-focusfix-02/out/seyrix-phase2d2-2026.10.27-x86_64.iso -boot order=d\,menu=off -device virtio-vga -device qemu-xhci -device usb-kbd -device usb-tablet -netdev user\,id=net0 -device virtio-net-pci\,netdev=net0 -display gtk\,gl=off\,show-cursor=on -serial stdio -qmp tcp:127.0.0.1:55659\,server=on\,wait=off -no-reboot
```

Run 12 booted through OVMF and GRUB, reached the serial login in 59,756 ms, and reached the graphical desktop in 99,975 ms at 640×480. The QEMU process exited after guest poweroff; [run-result.txt](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/run-result.txt) records PASS and [qemu-process-state.txt](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/qemu-process-state.txt) records that the process exited.

## 4. Screenshot evidence

The final run captured 52 raw QEMU PPM screenshots. All 52 entries in `screenshots.sha256` verified against the files. Examples:

- [Wallpaper and desktop item](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/phase2d2-wallpaper-and-desktop-item.ppm)
- [Manual viewer open](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/phase2d2-manual-open.ppm)
- [Dock with grouped applications](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/phase2d2-after-launch.ppm)
- [Launcher search](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/phase2d2-launcher-search.ppm)
- [Window switcher](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/phase2d2-switcher-open.ppm)
- [Weston fallback](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/phase2d2-weston-fallback.ppm)
- [SIGKILL cleanup returning to tty1](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/phase2d2-compositor-crash-cleanup.ppm)

The full screenshot set, serial output, Expect log, command line, run metadata, lock identity, and stage results are in [qemu-graphics-acceptance-12](evidence/phase2d2-local-stage2/qemu-graphics-acceptance-12/).

## 5. Desktop and dock/taskbar matrix

| Area | Result | Evidence |
|---|---|---|
| Background, desktop item, Manual open/close | PASS | `PHASE2D2_MANUAL_OPEN`, `PHASE2D2_BACKGROUND_FOCUS`; raw screenshots linked above |
| Background click preserves terminal keyboard focus | PASS | `PHASE2D2_BACKGROUND_FOCUS` |
| Running apps appear; same-app windows group; active app is highlighted | PASS | two terminals and one grouped dock item in the captured dock view |
| Clicking a running app focuses its window | PASS | `PHASE2D2_DOCK_WINDOW_FOCUS` |
| Workspace indicators and selection | PASS | `PHASE2D2_DOCK_WORKSPACE_SELECT`, four-workspace captures |
| Launcher search, valid unprivileged XDG launch, missing `TryExec`, Escape focus restore | PASS | `PHASE2D2_LAUNCHER_SEARCH_LAUNCH`, `PHASE2D2_LAUNCHER_MISSING_ENTRY`, `PHASE2D2_LAUNCHER_ESCAPE_FOCUS` |
| Window switcher MRU, keyboard navigation, activation, stale-window removal | PASS | `PHASE2D2_SWITCHER_MRU_ORDER`, `PHASE2D2_SWITCHER_KEYBOARD_CYCLE`, `PHASE2D2_SWITCHER_STALE_WINDOW` |
| Output configuration and resize rendering | PASS | ten resize gestures, including edges/corners and constrained in/out cases; `RESIZE_ASSERTION=passed` |

## 6. Phase 2D.1 window-management regressions

The final run passed the inherited hybrid, floating, and automatic tiling probes; split-ratio changes; focus and window movement; workspace migration and persistence; fullscreen/maximize and decorations; popup parent/child behavior; hidden-workspace input isolation; and window destruction while transitions were pending. The full stage transcript is preserved in `test.log` and `stage-results.log`.

## 7. Private shell IPC robustness

The ASan/UBSan compositor suite passed 3/3 tests. The new `compositor-shell-ipc-test` passed 16 assertions at the shared decoder/send boundary: valid workspace, window-ID and action messages; truncated header and body; oversized datagram with `MSG_TRUNC`; wrong protocol version; declared-size mismatch; unknown type; out-of-range workspace; zero window ID; reserved bytes; closed peer without SIGPIPE; nonblocking backpressure; and repeated independent socketpair decode cycles.

The rapid socketpair case tests decoder/send cycles, not process restart. Shell process restart is separately tested by the QEMU lifecycle stages. The private compositor dispatch path is not externally injectable, and no production attack interface was added. A nonzero but unknown window ID was not injected through compositor dispatch; code inspection confirms the lookup returns without activating a missing window. The tests therefore support the shared protocol-helper claim, not complete runtime coverage of every compositor side effect.

## 8. Crash, restart, and session lifecycle

Run 12 passed all of these lifecycle checks:

- Shell executable startup failure: temporarily hide the shell binary after the first shell starts, observe the first restart launch fail and attempt 2 be scheduled, restore the executable, then verify one shell and a mapped panel recover under the same compositor. An EXIT trap restores the guest file if the helper fails.
- Shell SIGKILL after successful startup with launcher open: shell restarts and terminal input focus returns.
- Panel-owner shell crash: the shell process that owns the panel is killed; a new shell and panel map appear while the compositor PID stays the same. There is no independent panel process.
- Restart budget: after at least 60 seconds of stability, five retries passed at 1, 2, 4, 8, and 16 seconds. This reset policy was repeated successfully in runs 08–12.
- Normal compositor exit: clean-stop log, tty1 return, and guest poweroff passed.
- Weston fallback: start and exit both passed. The exit predicate checks Weston and its terminal are absent before PASS.
- SIGTERM: a live compositor, shell, and terminal were verified as children in the session process group; SIGTERM produced the clean-stop log, returned tty1, and left no active process in the group.
- SIGKILL: a fresh live compositor session was killed; the anchor's parent-death handler terminated the session group, and tty1 returned with no active group members.
- Session lock test: the shell sanitizer test suite verified duplicate-session exclusion and that the child does not retain the lock file descriptor.

Process ownership checks verified shell/terminal UID 1000, direct compositor parentage, and the session process-group anchor. Processes that deliberately escape that process group remain outside the guarantee; strict no-orphans containment is not claimed.

## 9. Memory-safety classification

- Stage 2 compositor ASan/UBSan unit suite: 3/3 pass. Shell ASan/UBSan suite: 5/5 pass, including session lock. The Stage 1 headless Pixman ASan/UBSan integration set passed 14 checks. The normal graphical QEMU image itself was not ASan-instrumented.
- The existing sanitized shell UI LSan finding remains visible: 141,472 bytes across 4,817 allocations, with stacks through Fontconfig (`FcValueSave`/font matching) and Pango rendering. A standalone Pango/Cairo control reproduced 30,465 bytes across 916 allocations, with the same result with or without diagnostic `FcFini()`. This supports retained library cache state, but the smaller control does not account for the complete target aggregate; no Seyrix allocation stack was identified in the recorded analysis. No finding was suppressed and no speculative cleanup was added. See [Stage 1 report](phase2d2-local-stage1-report.md) and [sanitized shell log](evidence/phase2d2-local-stage1/shell-asan-meson-testlog.txt).
- The separate Phase 2C.2 wlroots/Mesa EGL teardown LSan finding remains unresolved and unchanged. This Stage 2 guest run was not a sanitizer run and does not clear that dependency finding.

No new ASan invalid-access or UBSan diagnostic attributable to Seyrix appeared in the instrumented tests.

## 10. Reliability campaign

Ten independent QEMU cold boots of the final ISO hash reached graphical startup: runs 03–12. The final full desktop interaction marker and shell crash/restart marker each passed in runs 05–12 (8 cycles); the stable-reset five-retry policy passed in runs 08–12 (5 cycles). The complete run passed in runs 11 and 12; run 12 recorded 111 PASS stage results and no FAIL/INCOMPLETE result. Earlier final-ISO attempts are retained and are not counted as end-to-end PASS runs; several stopped at earlier harness or lifecycle-stage gaps before the final harness fixes. This distinction is reflected in each run's `run-result.txt` rather than hidden.

## 11. Code and harness changes

- Reconstructed Archiso symlinks in an isolated copied profile and corrected image serial/network/time setup.
- Fixed compositor background clicks so they do not clear keyboard focus.
- Centralized bounded shell IPC packet decoding and nonblocking send results; malformed/disconnected shell clients are disconnected and restarted safely.
- Reset the restart budget after a shell remains healthy for at least 60 seconds.
- Added the IPC decoder/send unit test and guest helpers for shell startup failure, panel-owner recovery, compositor SIGTERM/SIGKILL cleanup, and process identity checks.
- Corrected QEMU harness process identity accounting, Weston timing/exit checks, panel-owner expectations, and framebuffer sampling. The final Weston predicate also requires its terminal to be gone.

## 12. Remaining limitations and decision

Dock auto-hide is not implemented. The minimal image still does not provide System Settings, a package manager, browser, or file manager. Unknown nonzero private IPC window IDs were not injected through the compositor dispatch path. The Fontconfig/Pango and wlroots/Mesa findings remain visible as described above. Child processes can escape the session process group, so no strict no-orphans guarantee is made.

**PHASE 2D.2 PASS.** Mandatory graphical behavior, compositor integration, session handling, restart policy, Weston fallback, and inherited window-management regressions were exercised successfully in the final full run. Ten final-image cold boots, eight graphical functional cycles, and eight shell crash/restart cycles reached their stated campaign markers; two end-to-end runs returned full PASS. Changes and this report are committed only on `phase2d2-desktop-shell`; nothing was pushed or merged.
