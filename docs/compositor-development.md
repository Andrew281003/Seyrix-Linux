# Compositor development

## Build environment

Build the compositor, protocol probe, native panel, and Archiso image inside
the Arch Linux WSL distribution. Use active Arch repositories with package
signature checks enabled. Required packages include `archiso`, `base-devel`,
`meson`, `ninja`, `pkgconf`, `wlroots0.20`, `wlr-protocols`,
`wayland-protocols`, `weston`, `wlr-randr`, QEMU, OVMF, and Expect. The build
script verifies that pkg-config resolves `wlroots-0.20` from the 0.20 API
series.

From PowerShell:

```powershell
$repo = 'C:\path\to\Seyrix-Linux'
$repoWsl = (wsl -d archlinux -- wslpath -a $repo).Trim()
wsl -d archlinux -u root -- pacman -S --needed archiso base-devel meson ninja pkgconf wlroots0.20 wlr-protocols wayland-protocols weston wlr-randr qemu-system-x86 edk2-ovmf expect
wsl -d archlinux -u root -- bash "$repoWsl/build/build-phase2c-iso.sh"
```

Use a new `SEYRIX_BUILD_ROOT` for each build; the script deliberately refuses
to reuse a nonempty path. It compiles the C17 compositor, native protocol
probe, and native `seyrix-shell`, stages them into a fresh Archiso profile,
and runs the compositor's deterministic Meson tests before packaging. It records
tool versions, package manifests, profile hashes, source branch/commit/worktree
state, sanitizer setting, ISO size, and checksum under
`/root/.cache/seyrix-linux/phase2c-<UTC timestamp>/`. Set
`SEYRIX_BUILD_SANITIZERS=address,undefined` for an instrumented compositor ISO;
the allowed values are `address`, `undefined`, and `address,undefined`.

## Layer A and Layer B checks

The build's Meson test exercises edge/corner resize geometry, min/max anchor
constraints, title-bar offsets, and wlroots absolute-pointer mapping on a
positioned headless output. Run the compositor's live headless integration
workflow inside Arch WSL as root:

```sh
tests/headless-integration.sh
```

The integration runner builds the current compositor, native probe, and panel
in a unique directory, then starts the actual compositor with two headless
outputs and Pixman. It checks mapped terminal/panel readiness, maximize and
restore, fullscreen and exit, separate rapid state transitions, server/client
decoration negotiation, toplevel destruction, output scale changes, disabling
and restoring one output, panel reassignment, rejection of a zero-enabled-head
configuration, and clean compositor shutdown. Logs and a TSV result matrix are
kept in `/root/.cache/seyrix-linux/headless-<UTC timestamp>/`.

Run the same integration workflow with compositor sanitizers enabled by
setting `SEYRIX_HEADLESS_SANITIZERS=address,undefined`. It scans the completed
compositor log for ASan, UBSan, and LSan reports after clients and the server
exit. The headless path does not exercise USB tablet event delivery, interactive
window movement/resizing, workspace shortcuts and migration, physical output
hotplug, UEFI boot, or guest shutdown; those require Layer C or a separate
physical-output environment.

## Run the Phase 2C QEMU workflow

The integrated workflow boots the ISO with OVMF UEFI, q35, 4 GiB RAM,
virtio-vga, USB keyboard and tablet, GTK with GL disabled, and TCG. It checks
console startup, the compositor, input, windows, protocols, workspace
interactions, process cleanup, and guest shutdown. It also retains serial
output and screenshots in a unique run directory.

```powershell
$env:SEYRIX_QEMU_TCG_THREAD = 'single'
wsl -d archlinux -u root -- bash "$repoWsl/tests/qemu-phase2c.sh" `
  /root/.cache/seyrix-linux/phase2c-<UTC timestamp>/out/seyrix-phase2c-2026.10.26-x86_64.iso
```

Single-threaded TCG has been more reliable for the serial login prompt under
this WSL2 host. Run only one graphical QEMU test at a time to avoid competing
TCG guests. The QEMU scripts enter `tests/qemu-lock.sh`, which uses a bounded
`flock` wait and refuses to start while any QEMU system process is already
running. It records the lock owner and run token. Each run has a unique QEMU
`-name`, logs and screenshots are retained under its evidence directory, and
Expect cleanup rechecks the exact run identity before signaling QEMU. A signal
or cleanup must never be used to stop an unrelated guest. The cold-boot
campaign holds the same lock across its sequential runs.

Do not treat a successful boot or protocol advertisement as functional
evidence for a window interaction; inspect the per-test stage results and
screenshots. A command marker proves only that the shell command ran. Window
readiness requires both the expected client process and a compositor map
record. Movement requires the same mapped `(pid, surface)` to show a measured
scene-coordinate change, accompanied by evdev and compositor button/gesture
evidence. Failed runs retain their logs, process state, timestamps, and
screenshots; diagnostic retries remain separate runs.

For a focused workspace run, set `SEYRIX_QEMU_WORKSPACE_ONLY=1` before
invoking `tests/qemu-phase2c.sh`. This mode starts its workspace checks after
compositor readiness and records workspace-specific markers and screenshots.
The latest recorded attempts stopped during second-window setup, so this mode
is currently a diagnostic aid rather than passing release evidence; see the
Phase 2C report.

The noninteractive Phase 1 regression remains available with
`tests/qemu-uefi.sh`. The Weston reference workflow uses the existing
`tests/qemu-graphics.sh` path with its default Phase 2A workflow. Save both
logs and screenshots if those regressions are rerun against a Phase 2C ISO.

## Start the native session in the guest

Log in to the unprivileged `seyrix` account on local tty1. Then run:

```sh
start-seyrix
```

`start-seyrix` checks the account, tty, runtime directory, and active
systemd-logind session. It uses libseat's logind backend and sets
`LIBGL_ALWAYS_SOFTWARE=1` plus `WLR_RENDERER_ALLOW_SOFTWARE=1` for QEMU. The
compositor starts `seyrix-shell` as a separate native process and
`weston-terminal` as its initial application. Compositor diagnostics are
written to `/tmp/seyrix-compositor.log`.

**Super+Return** opens another terminal; **Super+Q** sends a close request to
the focused application; **Super+Shift+Q** exits Seyrix. Workspace shortcuts
and minimize behavior are documented in
[compositor-architecture.md](compositor-architecture.md).

The Weston reference remains available with `start-wayland`. Neither desktop
session is a mandatory boot path. The serial root recovery terminal remains
enabled.

## Evidence and known risks

Keep raw serial logs, compositor logs, QEMU command lines, run metadata,
screenshots, build metadata, and checksums under `docs/evidence/phase2c/`.
Record whether QEMU used default or single-thread TCG and distinguish
application startup from boot-to-compositor readiness. Record renderer
identity, package count, resident memory, and ISO size from the guest/build
artifacts. Do not report FPS or frame-time data unless those measurements are
added and collected.

For Phase 2C.1, retain the Layer A/B result directories and each Layer C run
directory as separate evidence. Mark checks PASS, FAIL, INCOMPLETE, or NOT RUN;
never upgrade a failed interaction to PASS because a later diagnostic retry
succeeded.

The Phase 2B.1 report remains the source for the unresolved scheduler panic
and Mesa EGL sanitizer allocation. The earlier `enqueue_task_fair` panic was
not reproduced during its 20-boot campaign; it is not considered fixed. The
normal llvmpipe path previously reported one 368-byte Mesa EGL allocation,
while the Pixman sanitizer baseline was clean. Retest both paths after
renderer or lifecycle changes, and preserve the Mesa finding without
suppression or automatic attribution to Seyrix.

## Troubleshooting

- If Meson cannot find wlroots or protocols, check `pkg-config
  --modversion wlroots-0.20`, `pkg-config --modversion wlr-protocols`, and
  `pkg-config --modversion wayland-protocols` in Arch.
- If startup reports that `XDG_RUNTIME_DIR` is missing, start from the local
  tty1 logind session. Do not run the compositor as root.
- If no output appears, inspect `/tmp/seyrix-compositor.log` for backend,
  renderer, seat, and output errors. The compositor exits if no usable output
  is discovered.
- If the shell panel exits, the compositor should remain available. Inspect
  the compositor log for the panel socket lifecycle and the child status.
- Keep QEMU `gl=off`; software rendering is expected for this test path.
