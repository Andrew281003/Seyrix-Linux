# Seyrix Phase 2C engineering report

**Status: incomplete.** The Phase 2C release builds, UEFI startup, compositor
startup, output configuration, keyboard/pointer input, and multiple mapped
windows were observed in QEMU. The functional window-move check failed because
the final position matched its initial position. The integrated run did not
reach workspace, popup, decoration, or clean-shutdown checks. Separate
workspace-only runs booted the same ISO but stopped during second-window
setup; no workspace transition was functionally verified. Features without
functional evidence are not marked passed.

## Repository and baseline

- Branch: `phase2c-desktop-foundations`.
- Verified Phase 2B.1 baseline: `fe6bde9ba84af7bb6e7f035615c774fdbba98a47`.
- Workspace foundation commit at branch start: `a79a5e5`.
- Delivery commit hash is included in the final delivery summary.
- No push or merge was performed.

The implementation adds a workspace manager, xdg-shell window state handling,
real xdg popup scene trees, negotiated server-side decorations, a layer-shell
panel client, and `wlr-output-management-v1` configuration. The native
workspace manager starts with four workspaces, tracks membership, stacking,
and focus history, and switches by enabling the selected scene root while
leaving clients mapped. Adjacent switching does not wrap past workspace 1 or
4. Super+1..4 selects an existing workspace; Super+5..9 is consumed without
selecting a nonexistent workspace. Moving a window preserves its client and
scene tree. The panel is a separate C process and can request workspace
selection over a private local socket.

See [compositor-architecture.md](compositor-architecture.md) for the module
boundaries and protocol behavior.

## Native workspace architecture

`workspace.c` owns four compositor-wide workspace records. Each record keeps
its own toplevel membership, stacking order, and last eligible focused window.
The workspace manager is separate from scene rendering and toplevel lifetime;
`window.c` registers and removes membership, while each workspace owns a
wlroots scene root under the normal window layer. Switching enables the
selected root and disables the others. Client surfaces stay mapped and their
processes stay alive. Migration reparents the existing scene tree and moves
the existing window record between membership lists.

Focus changes reject hidden and minimized windows. Ordinary scene hit testing
uses the enabled scene graph, and switching clears stale pointer focus.
Transient toplevels join their parent's workspace. Popups are dismissed when
their parent workspace is hidden, and minimized windows stay assigned to
their workspace. The active-workspace index is currently global; the record
model can later be extended to per-output workspace sets without tying
membership to output rendering.

The shortcuts are Super+Left/Right for adjacent workspaces,
Super+Shift+Left/Right to move the focused window, and Super+1..4 to select
an existing workspace. Navigation stops at the first and fourth workspace;
Super+5..9 is consumed because those workspaces do not exist.

The implementation compiled into the final6 ISO. Three one-vCPU TCG
workspace-only attempts reached compositor readiness but stalled during
second-window setup. One attempt observed a second mapped toplevel without
its command marker; none reached a workspace transition. These results do
not establish that switching, migration, focus restoration, hidden-window
input isolation, or shutdown works at runtime.

## Build metadata

The latest release ISO was built on 2026-10-26 using Arch Linux in WSL2:

| Item | Value |
|---|---|
| ISO | `/root/.cache/seyrix-linux/phase2c-final6-20261026/out/seyrix-phase2c-2026.10.26-x86_64.iso` |
| ISO size | 1,039,171,584 bytes |
| SHA-256 | `0316509d033d63f50cce925013807069bba250c8bcb7f58611a1a08307da35d5` |
| Image package count | 241 |
| GCC | 16.2.1 |
| Meson / Ninja | 1.12.1 / 1.13.2 |
| wlroots | 0.20.2-1 (`wlroots-0.20` API) |
| Wayland | 1.26.0 |
| wlr-protocols | r112 |
| Wayland protocols | 1.49 |
| QEMU / OVMF | 11.1.2 / 202608 |

Build manifests, package lists, profile hashes, tool versions, ISO size, and
checksum are preserved in
[`docs/evidence/phase2c/build-final6/`](evidence/phase2c/build-final6/).
The build log reported `issue.pacnew` and `shadow.pacnew`, and GRUB reported
that Global SBAT level 4 differed from the build-time level 6. These warnings
did not stop image creation.

## Functional verification

Integrated graphical tests ran sequentially until a separate QEMU TCG guest
appeared in the shared Arch environment. That second guest conflicted with
the active run; the current run was stopped to avoid continued contention.
Its partial logs and screenshots are retained. The earlier isolated run
reached the stricter move check and recorded an unchanged window position.

| Area | Result | Evidence |
|---|---|---|
| UEFI boot and serial root session | Passed on partial runs | `qemu-workspace-runtime-failure` and `qemu-move-position-failure` logs |
| Seyrix compositor startup | Passed on partial runs | Startup marker and wlroots/Mesa renderer logs |
| Native panel process startup | Process and layer surface observed | Compositor log; panel click was not reached by the workflow |
| Output test/apply, position, scale 1.25, restore | Passed | `OUTPUT_CONFIGURATION=passed` on the final5 run |
| Keyboard input | Passed | `KEYBOARD_INPUT=passed` |
| Pointer input | Passed | `POINTER_INPUT=passed` with USB-tablet events |
| Two simultaneous terminal windows | Passed | `WINDOWS_COEXIST=passed` |
| Window move | Failed | Final5 initial and final scene positions were both `-51,26` |
| Window resize | Incomplete | Not reached by the final5 workflow; an earlier run's old resize assertion also failed |
| Maximize/fullscreen and rapid transitions | Not reached | Protocol probe workflow follows the failed interaction gate |
| Popup and nested popup behavior | Not reached | Protocol probe workflow not run |
| Decoration negotiation and controls | Not reached | Client-decoration probe not run |
| Workspace switching, migration, focus restore, and hidden-input isolation | Incomplete | The isolated run spawned and mapped a second terminal but did not observe its command marker; it stopped before switching. See `qemu-workspace-second-window-failure/` |
| Panel workspace selection | Not reached | Pointer-selection path is implemented but not runtime-verified |
| Window/output removal, app persistence, compositor exit, guest shutdown | Not verified | Integrated workflow stopped before these checks |
| Weston reference and Phase 1 serial regression | Not run against this ISO | No regression result claimed |
| Sanitizer reruns | Not run | No new sanitizer conclusion claimed |

The move assertion had previously passed when it only checked for a gesture-end
log. That was a false positive: the log was emitted even without a changed
position. The harness was tightened to compare initial and final geometry and
to print pointer-gesture diagnostics. Runtime evidence still needs to
establish a working move before the remaining scenarios can be treated as
verified.

The workspace-only workflow was run three times against the final6 ISO with
one TCG vCPU. All three boots reached the compositor. One run logged two
terminal processes and a second mapped toplevel, but its terminal command
marker was absent. The corrected retry that waits for the second toplevel to
map stalled before reporting that window as ready. No attempt reached a
workspace switch, migration, focus restoration, hidden-window input check,
compositor exit, or guest shutdown. Raw output is retained under
`qemu-workspace-only-stall/`,
`qemu-workspace-second-window-failure/`, and `qemu-workspace-final-stall/`.
These attempts are diagnostic only and do not count as functional workspace
evidence.

## Performance and runtime observations

The final5 ISO run used QEMU q35/OVMF, 2 vCPUs, 4 GiB RAM, one virtio-vga
output, GTK with GL disabled, TCG single-thread mode, and llvmpipe. Measured
values from that partial run:

- Boot-to-compositor readiness: 127,442 ms from QEMU launch, as measured by
  the harness.
- Compositor startup: 18,980 ms in the compositor marker; 26,082 ms from
  launcher invocation to ready marker in the harness.
- Compositor resident memory after startup: 117,292 KiB.
- Renderer: `llvmpipe (LLVM 23.1.3, 128 bits)`, Mesa 26.2.5.
- Image package count: 241.
- ISO size: 1,039,171,584 bytes.

These are observations from one partial TCG run, not a performance benchmark.
No FPS or frame-time figures were collected. Runtime logs also contained
missing optional `dnd-*` cursor warnings and a Weston Terminal
`Unknown parameter: ?2004` message.

## Known limitations and carried risks

- Workspace and panel click behavior has compiled but lacks functional QEMU
  evidence. Three isolated tests stopped during second-window setup, before
  workspace switching or migration ran.
- QEMU exposed one output. Output hotplug/removal recovery and physical
  multi-monitor behavior remain unverified. No physical multi-monitor claim
  is made.
- Per-monitor workspaces, animated switching, persistent output profiles,
  broad HiDPI behavior, Xwayland, and a launcher/taskbar are outside this
  implementation.
- The prior intermittent pre-userspace `enqueue_task_fair` scheduler panic
  remains unresolved. Phase 2B.1 did not reproduce it in a 20-boot campaign;
  this report does not claim it is fixed.
- The normal llvmpipe path previously showed one unresolved 368-byte Mesa EGL
  LeakSanitizer allocation. It was not retested here and is not attributed to
  Seyrix. The clean Phase 2B.1 Pixman sanitizer baseline is preserved.
- Because the new compositor and lifecycle code was not exercised under
  sanitizers, this phase has no evidence ruling out new memory-safety or
  undefined-behavior findings.

## Evidence index

- `build-final5/`, `build-final6/`: release build metadata.
- `qemu-workspace-runtime-failure/`: startup, input, multiple-window evidence
  and the earlier failed resize attempt.
- `qemu-move-position-failure/`: unchanged move position from the stricter
  run.
- `qemu-panel-startup-failure/`: first event-driven panel-pointer attempt
  exited before advertising a mapped panel; the follow-up removed the startup
  pointer-capability requirement.
- `qemu-parallel-guest-interruption/`: partial latest run stopped after a
  second TCG guest appeared.
- `qemu-workspace-only-stall/` and
  `qemu-workspace-second-window-failure/` and `qemu-workspace-final-stall/`:
  serial logs, test output, command lines, and QEMU metadata from three
  incomplete workspace-only runs.
- `qemu-serial-missing-device/` and `phase2b1-serial-control/`: default-TCG
  serial-console failure and the Phase 2B.1 control evidence.

Phase 2C remains incomplete until the failed move interaction is corrected,
workspace and lifecycle cases pass in QEMU, the regression suites and
sanitizer checks are run, and the final ISO exits and shuts down cleanly.
