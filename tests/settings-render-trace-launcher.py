#!/usr/bin/env python3
"""Diagnostic-only launcher that timestamps GTK and Wayland client traces."""
import os
import subprocess
import sys
import time

runtime_dir = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
trace_path = os.path.join(runtime_dir, "seyrix-settings-render.trace")
env = os.environ.copy()
env["SEYRIX_SETTINGS_RENDER_TRACE"] = "1"
env["WAYLAND_DEBUG"] = "client"

with open(trace_path, "w", encoding="utf-8", buffering=1) as trace:
    process = subprocess.Popen(
        ["/usr/bin/seyrix-settings"],
        env=env,
        stdout=None,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )
    assert process.stderr is not None
    for line in process.stderr:
        trace.write(
            f"monotonic_ns={time.monotonic_ns()} source=client-stderr "
            f"pid={process.pid} {line}"
        )
    sys.exit(process.wait())
