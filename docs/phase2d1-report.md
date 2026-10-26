# Phase 2D.1 — Hybrid Window Management Report

**Decision: PASS** for Phase 2D.1. The Phase 2C.2 wlroots/Mesa EGL lifecycle finding remains unresolved and Phase 2C.2 remains incomplete; this carried dependency risk is recorded separately below.

## 1. Architecture overview

The compositor now uses one window-management subsystem for Floating, Automatic Tiling (`tiling` in configuration), and Hybrid. `window-management.c` owns policy and geometry decisions; `layout-engine.c` is a C17 layout module independent of Wayland protocol callbacks; `wm-config.c` parses and persists the small user configuration. Existing xdg-shell lifecycle, focus, output, scene, and pointer code calls the subsystem at map, unmap, commit, workspace migration, output change, and state-transition boundaries.

Each window keeps its protocol/process identity, workspace link, classification, normal floating box, and current layout state. Mode changes update classification visibility, scene layering, geometry, and focus without recreating clients or surfaces. Each workspace owns its own split tree, mode/preferences, focus history, window order, and tiled/floating scene layers.

## 2. Git baseline and branch

- Branch: `phase2d1-hybrid-window-management`.
- Verified base and pre-commit `HEAD`: `5e90976cadc6d1e941f8b3f3a9dc3b2bcc24748e` (`test: record Phase 2C.2 acceptance evidence`).
- Inspection found no EGL-investigation commit after that SHA. The EGL report and investigation files were working-tree changes, which remain preserved and are excluded from the Phase 2D.1 commit.
- The seven pre-existing Windows-side `config/archiso` changes were preserved and are excluded from the Phase 2D.1 commit.

## 3. Layout data structures and ownership

`layout-engine.[ch]` implements a bounded binary split tree. Internal nodes store horizontal or vertical split orientation and a ratio; leaves reference windows and store active state and client limits. A workspace owns the root and removes a leaf on unmap/destruction or workspace detach. Removing a leaf compacts its parent into the sibling. Reinsertions target the focused tiled leaf, or the largest active leaf when focus is not tiled, giving a deterministic ordering.

Automatic orientation follows the selected workspace preference; `auto` chooses the longer dimension of the target cell (or usable output area for the first leaf). Horizontal splits divide width and vertical splits divide height. Ratios use a 0–1000 scale, default to 500, and clamp to 50–950. Workspace layout is limited to 256 leaves. Reflow uses the output's panel-adjusted usable area and bounded integer geometry. If client minimum sizes cannot fit, reflow retains the last valid geometry and logs that tiling is temporarily constraint-blocked; it does not emit negative or undersized cells. Client maximums and decoration/titlebar extents are included when fitting the client box.

## 4. Floating mode behavior

Floating windows retain the existing pointer move and edge/corner resize paths, xdg size constraints, focus behavior, and stacking. Initial toplevel placement remains centered. Normal floating geometry is captured before layout geometry replaces it and updated while floating; it is restored when the window returns to a floating presentation. Tiled windows can be explicitly dragged to floating with Super+Alt+left-drag; Alt+right-drag resizes. A mode change cancels an active pointer operation safely. Output/work-area changes clamp floating geometry so at least a titlebar and a reasonable 64-pixel region remain visible.

The final QEMU regression measured actual scene and committed client geometry for pointer movement and all ten resize directions (four edges, four corners, plus inward/outward size constraints). All passed.

## 5. Automatic tiling behavior

The first tile occupies the usable workspace area after configured outer gaps. Each later tile splits a deterministic target leaf and applies the configured inner gap. The split can be horizontal or vertical, can nest beyond two windows, and can be swapped/focused through the tree. Gaps default to 12 logical pixels; zero is valid and each gap is bounded to 0–128. Ratio adjustment is clamped and geometry remains subject to the client's min/max constraints.

The final ISO QEMU run observed three simultaneous tiled surfaces with bounded, pairwise non-overlapping geometry (`WM_TILED_NONOVERLAP=passed count=3`). A keyboard ratio adjustment changed committed client geometry while preserving surface identity. The final ISO's two-window and three-window snapshots, including screenshots, are in the focused QEMU evidence directory listed in section 10.

## 6. Hybrid mode behavior

Hybrid is the safe default. Ordinary top-level xdg toplevels default to tiled; xdg transient children default to floating. Ambiguous standalone windows default to tiled. An explicit app-id configuration rule takes precedence, and the focused-window toggle stores a user override when the app id is usable. No application name heuristics are used.

Each workspace maintains explicit per-window tiled/floating classification. Floating windows occupy the floating scene layer and preserve their own box; tiled leaves reflow independently. In Automatic Tiling, all mapped eligible windows are presented as tiled while their Hybrid classifications are retained. Returning to Hybrid restores those classifications, including manually floating windows. Floating mode presents all windows as floating and likewise retains Hybrid classification. Maximize/fullscreen suspend a tile leaf and restore the prior mode/classification and layout membership on exit.

QEMU verified mixed presentation, toggling both directions, identity preservation, deterministic retile geometry, and maximize/fullscreen cycles in Hybrid tiled, Tiling, Floating, and Hybrid floating cases.

## 7. Per-workspace state handling

All four workspaces keep independent mode, split tree, gap/ratio/orientation preferences, window ordering, floating boxes, and focus history. Moving a window detaches it from the source tree and inserts it into the destination tree without restarting its process. Workspace switches only expose the active workspace's scene/input state. QEMU verified independent modes, returning to workspace 1 with its prior state, restoring focus, process-preserving migration in both directions, number-key selection for all four workspaces, and keyboard/pointer isolation of hidden clients. Headless output checks assigned workspaces across two outputs and reassign output when an output is disabled.

## 8. Keyboard and pointer controls

All additions use the existing Super/Logo shortcut registry; existing Seyrix shortcuts remain in place.

| Control | Action |
| --- | --- |
| Super+Shift+F / T / H | Set Floating / Automatic Tiling / Hybrid for the active workspace |
| Super+F | Toggle focused window between tiled and floating; switches to Hybrid first if needed |
| Super+J / K | Focus next / previous tiled neighbor |
| Super+Shift+J / K | Move the focused tile to the next / previous tree position |
| Super+Ctrl+Left / Right | Adjust focused horizontal split ratio |
| Super+Ctrl+Up / Down | Adjust focused vertical split ratio |
| Super+O | Cycle automatic, horizontal, and vertical split orientation |
| Super+Ctrl+R | Reload configuration live |
| Super+Alt+left-drag | Explicitly float a tiled window and move it |
| Alt+left-drag / Alt+right-drag | Move / resize a floating window |

Ratio keyboard changes use 25/1000 increments. Tiled windows do not start an unmodified free-move grab. Interrupted/unmapped grabs are cleared through the existing cursor reset path.

## 9. Configuration format and persistence

Configuration is read from `$XDG_CONFIG_HOME/seyrix/window-management.conf`, falling back to `$HOME/.config/seyrix/window-management.conf`. It is a non-executable versioned `key=value` file. A representative configuration is:

```ini
version=1
default_mode=hybrid
inner_gap=12
outer_gap=12
split_ratio=500
split_orientation=auto
workspace.1.mode=tiling
workspace.1.inner_gap=8
workspace.1.outer_gap=12
workspace.1.split_ratio=500
workspace.1.split_orientation=auto
app_id.7365797269782e6578616d706c65=floating
```

App-id keys encode the UTF-8 bytes as lowercase hex. The parser validates known keys, value ranges, version, workspace indices, and rule bounds; malformed input is ignored and defaults remain safe (Hybrid, gaps 12, ratio 500, automatic orientation). Writes use a temporary file, mode 0600, fsync, and atomic rename. Workspace modes/preferences and app-id classification overrides persist. Mode, ratio, orientation, and classification changes apply live; external edits apply with Super+Ctrl+R. No system-wide or privileged write is used. The graphical settings UI is out of scope.

## 10. Unit test results

The final native build's Meson suite passed **2/2**: `compositor geometry` and `compositor window management`. The latter covers empty/single and multi-leaf layouts, both split axes, nesting, zero and nonzero gaps, ratio clamp/adjustment, min/max constraints, infeasible areas, output reflow, removal/compaction, migration/reinsert, neighbor/swap behavior, and injected allocation failures. Configuration checks cover valid and malformed files, defaults, app-id rules, and persistence. The focused executable covers repeated mode/state and floating restoration cases where possible without a Wayland server.

Final-build Meson test log: `/root/.cache/seyrix-linux/phase2d1-final-build2-20261026/compositor-build/meson-logs/testlog.txt`.

## 11. QEMU functional results

Two serialized QEMU runs used the final ISO under WSL2/QEMU TCG (q35/OVMF, 2 vCPUs, 4 GiB, virtio-vga, 640×480 visible output):

- Focused WM run `/root/.cache/seyrix-linux/phase2d1-qemu-wm5-20261026`: PASS, clean shutdown. Verified Hybrid default with surface geometry; two and three non-overlapping tiles; split-ratio change; mixed Hybrid; classification toggle; mode switches without identity loss; deterministic reinsertion; per-workspace mode independence; fullscreen/maximize protocol cycles in four mode/classification cases; workspace migration and focus restoration.
- Full Phase 2C regression run `/root/.cache/seyrix-linux/phase2d1-qemu-final-full-20261026`: PASS, clean compositor/session exit and guest shutdown. Verified real pointer movement, all resize edges/corners and constraints, four workspaces, migration/process retention, focus restoration, hidden-workspace input isolation, panel workspace selection and crash isolation, xdg popup lifecycle, decoration negotiation, maximize/fullscreen and rapid state transitions.
- Weston reference/fallback run `/root/.cache/seyrix-linux/phase2d1-qemu-weston-final-20261026`: PASS, clean shutdown. Verified Weston session readiness/stability and return to tty1 after exit.

Evidence records configure and committed client geometry, process IDs, surface IDs, screenshots, stage results, serial logs, QEMU command lines, and run metadata. Logs, not compositor log messages alone, were used for geometry assertions. One earlier focused attempt (`phase2d1-qemu-wm4`) failed because a probe incorrectly required a tiled client to restore its original floating dimensions after unmaximize. The probe was corrected to accept layout-managed restore geometry while still checking the protocol state cycle; final rerun `wm5` passed. This was a probe expectation correction, not a compositor defect.

## 12. Regression results

The final-ISO regression passed: pointer move with observed geometry change; all ten resize edge/corner/constraint cases; four workspace switching and no-wrap; workspace process persistence and focus restoration; keyboard and pointer isolation for hidden workspaces; maximize/fullscreen and rapid protocol transitions; nested popup create/dismiss/minimize/workspace switch/destruction; client-decoration negotiation; panel workspace selection and recovery after panel crash; serial login/root shell; Weston fallback and return to tty1; compositor exit and clean guest shutdown. The headless output workflow separately passed output configuration, scale 1.25→1.0, disable/restore, panel reassignment, and rejection of disabling all outputs.

## 13. Sanitizer results

The headless/Pixman integration used AddressSanitizer and UndefinedBehaviorSanitizer with leak detection. It passed state-cycle, rapid-state, client-decoration, destruction-during-transition, two-output assignment/scale/disable/restore, panel reassignment, and clean compositor shutdown with no ASan, UBSan, or LSan diagnostics. The pointer watcher was `NOT_RUN` on the headless backend because that backend did not expose pointer capability; final QEMU pointer movement and resize tests passed with observed geometry.

The layout unit suite passed in the final ISO build. QEMU's EGL renderer lifecycle was not used as a sanitizer acceptance test because the separate Phase 2C.2 dependency-level LSan finding remains open; it is neither suppressed nor counted as a new Seyrix-owned defect.

## 14. Performance and resource observations

The focused and full TCG runs reached the serial login in 55.8 s and 54.6 s, and graphical readiness in 94.1 s and 92.6 s. The full regression recorded compositor RSS of 107,432 KiB at its readiness checkpoint; the focused run recorded 116,196 KiB. These are single-run TCG observations, not native hardware performance claims or a benchmark. Headless Pixman tests completed quickly and showed no sanitizer diagnostics or leaked compositor process. The split tree is capped at 256 leaves and allocates only on insertion/rebalance.

## 15. Known limitations

- No physical multi-monitor hardware was available. Two-output headless assignment, scaling, disable/restore, and panel reassignment were tested; real multi-monitor behavior is not claimed.
- App classification currently supports exact app-id rules only; there is no general rule engine or GUI settings application.
- There are no animations. Tiny output areas that cannot satisfy all client minimums retain the last valid layout until constraints or usable area change.
- Session-level tree topology is rebuilt from live windows at compositor startup; persisted preferences and app-id classifications survive restart, but individual split-tree topology is not serialized.
- QEMU uses a single visible output. Multi-output behavior was verified only through headless output tests.

## 16. Carried Phase 2C EGL issue

The investigation documented in `docs/phase2c2-egl-investigation.md` remains unresolved. With wlroots 0.20.2, when `EGL_KHR_display_reference` is unavailable, wlroots skips `eglTerminate`; Mesa allocations remain after renderer destruction. The direct EGL control terminates cleanly, while the minimal wlroots control reproduces the allocations observed in Seyrix. This evidence does not establish a Seyrix-owned renderer-cleanup defect. No LSan suppression or wlroots patch was added. Phase 2C.2 is still **INCOMPLETE**; this Phase 2D.1 report carries that upstream dependency risk without misclassifying it as a Phase 2D regression.

## 17. Final ISO

- Path: `/root/.cache/seyrix-linux/phase2d1-final-build2-20261026/out/seyrix-phase2c-2026.10.26-x86_64.iso`
- Size: 1,039,171,584 bytes (992 MiB)
- SHA-256: `86bf488bf7583e151b51e871e14be81416f19af21331ac38ce22fd5f7fc3e626`

This exact image was used for the focused WM, full regression, and Weston reference QEMU runs.

## 18. Acceptance decision

**PASS — Phase 2D.1.** The three runtime modes, Hybrid default, more-than-two-window tiling, gaps/ratios, floating restoration, per-workspace state, process-preserving migration, focus/stacking, maximize/fullscreen, native geometry tests, applicable sanitizers, final-ISO QEMU verification, clean shutdown, and preserved evidence all passed. The separate wlroots/Mesa EGL dependency issue remains open and Phase 2C.2 remains incomplete.
