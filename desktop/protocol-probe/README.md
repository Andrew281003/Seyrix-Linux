# Seyrix Phase 2C protocol probe

`seyrix-protocol-probe` is a small C Wayland client used to exercise the
compositor's xdg-shell implementation. It builds stable xdg-shell and unstable
xdg-decoration client bindings from the installed `wayland-protocols` XML with
`wayland-scanner`.

## Build

From the Arch Linux build environment:

```sh
meson setup /tmp/seyrix-protocol-probe-build desktop/protocol-probe \
  --prefix=/usr --wrap-mode=nofallback
ninja -C /tmp/seyrix-protocol-probe-build
```

The executable depends on `wayland-client` at runtime. The generated protocol
code is compiled into the executable; the guest does not need the scanner or
protocol XML files.

## Modes

```sh
# Automatically request maximize, restore, fullscreen, and restore.
SEYRIX_PROBE_LOG=/tmp/phase2c-state.log seyrix-protocol-probe --state-cycle

# Send rapid consecutive state requests and require a final normal configure.
SEYRIX_PROBE_LOG=/tmp/phase2c-rapid.log seyrix-protocol-probe --rapid-state

# Request client-side decoration and verify the compositor's negotiated mode.
SEYRIX_PROBE_LOG=/tmp/phase2c-csd.log seyrix-protocol-probe --client-decoration

# Request maximize and destroy the role before the response is committed.
SEYRIX_PROBE_LOG=/tmp/phase2c-destroy.log \
  seyrix-protocol-probe --destroy-during-transition

# Interactive mode: click the window to open a grabbed popup, then click the
# popup to open a nested popup. Click outside the popup tree to dismiss it.
SEYRIX_PROBE_LOG=/tmp/phase2c-popup.log seyrix-protocol-probe
```

The window requests server-side decorations when the global is available. Each
mode writes configure dimensions, maximize/fullscreen state, configure serials,
acknowledgements, committed buffer sizes, decoration mode, popup configure
geometry, dismissal, and process result to its log. The state-cycle mode exits
with status 0 only after it observes all four state transitions, restored
initial geometry, and negotiated server-side decoration.

The probe's colors identify the parent and popup surfaces. Clicking anywhere
on the toplevel opens a popup; clicking the popup opens a nested popup. The
positioner anchor uses the actual pointer coordinates so edge adjustment can
be observed by clicking near the output boundary.
