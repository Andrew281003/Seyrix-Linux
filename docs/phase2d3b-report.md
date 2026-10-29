# Phase 2D.3B — Native System Settings Foundation

**Result: PHASE 2D.3B INCOMPLETE**

The native Settings app, read-only compositor endpoint, ISO integration, native test suites, and static-analysis builds are implemented. The required graphical QEMU acceptance did not complete, so launcher behavior, all-page interaction, 640×480 usability, live endpoint behavior, and session shutdown are not accepted as verified.

## Scope and implementation

This change implements Phase 2D.3B only. It adds `seyrix-settings`, a native C17 GTK4 Wayland application with six categories: Appearance, Desktop & Dock, Window Management, Displays, Input, and About Seyrix. Category navigation supports a persistent sidebar on wide windows and a compact selector on narrow windows. Search matches setting descriptions. Widgets have labels, keyboard focus styling, and keyboard and pointer navigation. Unsupported settings are visibly disabled. The app reports connection, error, offline, and unavailable states, retries with capped exponential backoff, and reads OS, kernel, CPU, memory, and compositor snapshot information without elevation.

The application uses GTK's ordinary toplevel surface. Its desktop entry is installed as a normal launcher application. The About page and compositor snapshot are read-only.

The new compositor endpoint is a separate Unix `SOCK_SEQPACKET` listener at `$XDG_RUNTIME_DIR/seyrix/settings-v1.sock`. It uses a private, session-owned runtime directory, owner-only socket permissions, safe stale-socket checks and inode-aware cleanup. It authenticates peers with `SO_PEERCRED`, checks the peer uid and session id, bounds clients, subscribers, packet sizes, and per-dispatch work, negotiates protocol version 1.0, correlates request ids, carries revisions and explicit error codes, and serves snapshots and bounded change notifications. The shell's inherited private IPC remains separate.

The v1 endpoint exposes only hello, snapshot, subscribe, and unsubscribe operations. It has no write, command-execution, privilege, or arbitrary-file interface. No settings mutations are enabled. The architecture document records future transaction recovery requirements before any later mutation milestone.

`SO_PEERCRED` plus session checks enforce the logged-in session boundary, not process identity. Any process running as the same uid in that session can connect. The endpoint grants no root or system-wide capability; this same-uid limitation is part of the security model.

## Build and dependency versions

The target Arch build used GCC 16.2.1, Meson 1.12.1, Ninja 1.13.2, GTK 4.24.1, GLib 2.90.1, wlroots 0.20.2, Wayland 1.26.0, xkbcommon 1.13.2, wlr-protocols 1.0, QEMU 11.1.2, and OVMF 202608-1. Full package and environment records are in [release-06 evidence](evidence/phase2d3b/build/release-06/).

The release-06 ISO build completed. ISO SHA-256: `101012f840e935a3502f0ccd91fe22744cb2d507ac81bb2085c0e0d9100fd7e6`. Size: 1,122,795,520 bytes. The build, package inventory, and test logs are preserved under `docs/evidence/phase2d3b/build/release-06/`.

## Verification

| Check | Result |
|---|---|
| Release build and image packaging | PASS |
| Native Settings tests | PASS, 3/3 |
| Native compositor tests | PASS, 3/3 |
| Native shell tests | PASS, 5/5 |
| GCC `-fanalyzer`, `-Werror` Settings build | PASS |
| GCC `-fanalyzer`, `-Werror` compositor build | PASS |
| ASan/UBSan/LSan compositor tests | PASS, 3/3 |
| ASan/UBSan/LSan Settings IPC/model/client tests | PASS, 3/3 |
| ASan/UBSan/LSan shell tests | INCOMPLETE, 4/5; known Fontconfig/Pango leak below |
| Graphical QEMU acceptance | INCOMPLETE |

Release-06 native test logs are in `docs/evidence/phase2d3b/build/release-06/`. Final static-analysis setup and compile logs are in `docs/evidence/phase2d3b/static-analysis/final/`. The instrumented test logs are in `docs/evidence/phase2d3b/sanitizers/`.

The shell sanitizer run retained its unsuppressed LeakSanitizer result: 140,884 bytes in 4,817 allocations, with traces through Fontconfig/Pango during shell UI rendering. No sanitizer suppressions were added. This is reported separately from the existing Phase 2C.2 wlroots 0.20.2/Mesa EGL teardown finding, which remains documented in the architecture and prior phase records and was not re-evaluated by this run.

### QEMU acceptance gap

The release-06 image was exercised in multiple graphical QEMU attempts; those attempts do not establish Phase 2D.3B acceptance:

- An earlier run showed a Settings screen at 640×480, but the layout was clipped and it did not verify all required behavior.
- Run `qemu-graphics-20261029T-final-acceptance-09` emitted page-capture markers, but the saved images show terminals, not Settings pages. The launcher stage timed out after 90,331 ms. The normal endpoint probe did not complete negotiation; the subscription checks failed. These captures are not UI evidence.
- Runs 10, 11, and 12 did not reach the harness acceptance stages because `serial-getty@ttyS0` timed out waiting for `/dev/ttyS0`. Runs 10 and 11 reached the graphical target, but no launcher or Settings acceptance steps ran. Run 12 was stopped while boot remained blocked. Their full logs and explicit incomplete results are preserved in the corresponding `qemu-graphics-*` evidence directories.

Therefore the following remain unverified in a successful graphical run: launching Settings from the application launcher, genuine rendering of all six pages, search and category navigation, pointer and keyboard operation, usable layout at 640×480, accurate live read-only state, endpoint subscription and reconnect in the compositor session, existing compositor/shell behavior during Settings use, connection-loss handling in the UI, and clean session shutdown. No PASS claim is made for these checks.

## Remaining work for Phase 2D.3B completion

Repair or adapt the QEMU harness so it can reliably reach its guest control session when `/dev/ttyS0` is unavailable, then run the complete graphical acceptance on the corrected release image. Use real Settings-window screenshots and live endpoint probes. Repeat the compositor/shell regression and shutdown checks in that successful run. Until those checks pass, Phase 2D.3B remains incomplete.

## Revisions

- Branch: `phase2d3-system-settings`
- Expected architecture baseline and verified starting commit: `68aea8cbc99fea82e60e2b7d4592e00cb3156ef4`
- Implementation and evidence commit: `ef3e36f7d118fc7a3819373de128370e212553d8`

The protected original Phase 2D.1 worktree was not modified. No push, merge, rebase, or reset was performed.
