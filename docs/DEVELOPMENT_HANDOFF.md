# Development handoff — 2026-11-08

Development is paused for the Pop!_OS migration. Phase 2D.3B.5 is **INCOMPLETE**.

## Repository checkpoint

- Active worktree: `C:\Users\quake\.codex\worktrees\phase2d3b5-system-settings\Seyrix-Linux`
- Branch: `codex/phase2d3b5-system-settings`
- Base HEAD before this checkpoint: `dd0c9231fe576e34d61cfda6f6d73ed83e1a3103`
- Source/report checkpoint commit: `a30f04b32785c871651bff5cfa1285901de3c8e5`
- Remote: `https://github.com/Andrew281003/Seyrix-Linux.git`, dedicated branch `codex/phase2d3b5-system-settings`
- The verified remote tip is recorded in the portable backup README and final migration report.
- Two untracked files remain outside the source commit: `tests/.trace-regression-6yCQAG.sh` and the repository-root filename `U+F05C` (a private-use Unicode character). Both are retained in the portable backup.
- The separate Coding checkout is at `C:\Users\quake\Desktop\Coding\Seyrix-Linux`, branch `phase2d1-hybrid-window-management`, HEAD `4bc271a7c4497b05266961a1544738e81d1c23c3`. It has its own local changes; do not overwrite them.
- Other dirty Seyrix worktrees exist under `C:\Users\quake\.codex\worktrees\` (`phase2d3b-diagnostic`, `phase2d3b5-smp-investigation`, and `phase2d3b5b-agent1`). Their changes are preserved in the migration backup as patches and untracked-file archives.

## Phase status and resume point

The diagnostic ISO at `/var/tmp/seyrix-linux/phase2d3b5-evidence-pipeline-20261108T085200Z/out/seyrix-phase2d3b-2026.11.08-x86_64.iso` has SHA-256 `b123438cd1638f9aacb39d61887abebaf359a2a46cd888f94ec8a53cde21c7f9`. Its build release suites passed: compositor 4/4, Shell 5/5, Settings 3/3, supervisor 3/3.

The latest bounded 640×480 run is `/var/tmp/seyrix-linux/qemu-graphics-20261108T095000Z/`. Its raw manifest (`a5eda47a4a8ce96275aebfa60c736549ff35dc5a3b1f37ffefd6565b73f3f823`) verifies. It retained attributable Wayland sidecars for PIDs 2707 and 12681. The replacement compositor emitted read/frame/request-dispatch/send records for PID 2707. The client trace lines needed to prove its HELLO, snapshot, and subscription responses are absent from retained host evidence, so matching client completion is not verified and the focused evidence gate remains **INCOMPLETE**. The latest integration report contains the detailed result.

Next: add and offline-check the narrow client-trace capture predicate; then build one fresh diagnostic ISO and run one locked 640×480 recovery. Require matching client and compositor evidence for all three IPC operations and retained, attributable Wayland sidecar evidence. Run the remaining acceptance stages only if that gate passes. No full ISO or QEMU work is authorized during this pause.

## Build environment

The build script requires a root Arch Linux environment: `mkarchiso`, `pacman`, `sha256sum`, `meson`, `ninja`, `pkg-config`, `wayland-scanner`, `git`, `wlroots-0.20`, `wlr-protocols`, and `wayland-protocols`. The current host had the stopped WSL distro `archlinux-d-verify`; its VHDX is `D:\WSL\archlinux\ext4.vhdx` (254,458,986,496 bytes). QEMU acceptance additionally needs `qemu-system-x86_64`, `expect`, Python 3, OVMF firmware, and a graphical display. Recreate the Arch environment on Pop!_OS before attempting an ISO build.

The last candidate used acceptance hooks and Settings render tracing. A future build must use a new, empty artifact directory, for example:

```bash
sudo -E env SEYRIX_ARTIFACT_ROOT=/var/tmp/seyrix-linux \
  SEYRIX_BUILD_ROOT=/var/tmp/seyrix-linux/phase2d3b5-resume-YYYYMMDDTHHMMSSZ \
  SEYRIX_QEMU_ACCEPTANCE_HOOKS=1 SEYRIX_QEMU_SETTINGS_RENDER_TRACE=1 \
  ./build/build-phase2d3b-iso.sh
```

Do not reuse a completed build directory. Preserve logs and checksums for any resumed run.

## Pop!_OS restoration

The portable backup is stored at `Z:\Seyrix-Linux-PopOS-Handoff-20261108` on the separate USB disk labeled `Seyrix`. Verify `backup-files.sha256` before extracting anything. The GitHub branch contains the committed source checkpoint; clone it with:

```bash
git clone --branch codex/phase2d3b5-system-settings \
  https://github.com/Andrew281003/Seyrix-Linux.git \
  "$HOME/src/Seyrix-Linux"
```

The backup also contains the portable repository bundle, uncommitted sibling-worktree patches, the latest QEMU evidence without the large ISO, build metadata/checksums, and this handoff. The Windows Codex authentication file and large Codex caches are deliberately excluded; sign in again on Pop!_OS. No Windows `.ssh` directory or WSL SSH key files were found.

The full `archlinux-d-verify` WSL image and older reproducible build caches were not copied. The WSL VHDX currently resides on the separate `D:` disk; confirm that disk is preserved before relying on it. The diagnostic ISO is reproducible and was not copied. After confirming the USB backup and any desired WSL data are available on Pop!_OS, resume at the client trace capture regression above.
