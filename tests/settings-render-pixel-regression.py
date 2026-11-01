#!/usr/bin/env python3
"""Offline regression fixtures for the production Settings pixel gate."""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


COMPARE = Path(__file__).with_name("settings-render-pixel-compare.py")
WIDTH = 640
HEIGHT = 480
RASTER_SIZE = WIDTH * HEIGHT * 3


def write_ppm(path: Path, changed: bool) -> None:
    raster = bytearray(RASTER_SIZE)
    if changed:
        offset = (200 * WIDTH + 20) * 3
        raster[offset : offset + 3] = b"\xff\xff\xff"
    path.write_bytes(f"P6\n{WIDTH} {HEIGHT}\n255\n".encode() + raster)


def evidence(input_state: str = "verified", input_result: str = "passed", gtk: str = "passed", selection_pid: int = 776) -> str:
    markers = [
        f"SETTINGS_TRACE_APP_PID=776 CHECKED_PROC_PID=776 UID=1000 NATIVE_EXE=/usr/bin/seyrix-settings TRACE_OWNER=seyrix:seyrix",
        f"SETTINGS_GTK_SELECTION_PID={selection_pid}",
        f"SETTINGS_PAGE_INPUT_SEQUENCE={input_state}",
        f"SETTINGS_INPUT_VERIFICATION_RESULT={input_result}",
        f"SETTINGS_GTK_PAGE_TRANSITION={gtk}",
        "SETTINGS_GTK_PAGE_BEFORE=appearance",
        "SETTINGS_GTK_PAGE_AFTER=desktop",
        "SETTINGS_ENDPOINT_CONNECTION=passed",
        "SETTINGS_TRACE_TIME_CORRELATION=passed",
        "SETTINGS_QMP_BEFORE_CAPTURE=passed",
        "SETTINGS_QMP_AFTER_SELECTION_CAPTURE=passed",
        "SETTINGS_RENDER_TRACE_SCREENSHOT=phase2d3b-appearance-640x480.ppm",
        "SETTINGS_RENDER_TRACE_SCREENSHOT=phase2d3b-desktop-dock-640x480.ppm",
        "STAGE_RESULT name={Settings input verification} result=PASS",
        "STAGE_RESULT name={GTK page selection and map transition} result=PASS",
        "STAGE_RESULT name={Settings read-only endpoint connection} result=PASS",
        "STAGE_RESULT name={GTK-to-Wayland-to-wlroots-to-output trace} result=PASS",
    ]
    return "\n".join(markers) + "\n"


def compare(before: Path, after: Path, log: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(COMPARE),
            "--before",
            str(before),
            "--after",
            str(after),
            "--evidence-log",
            str(log),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def assert_incomplete(result: subprocess.CompletedProcess[str], label: str) -> None:
    output = result.stdout + result.stderr
    if result.returncode != 2 or "result=INCOMPLETE" not in output or "result=PASS" in output:
        raise AssertionError(f"{label} was not rejected as incomplete: rc={result.returncode}\n{output}")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="seyrix-settings-pixel-regression-") as tmp:
        directory = Path(tmp)
        before = directory / "appearance.ppm"
        after = directory / "desktop-dock.ppm"
        log = directory / "evidence.log"
        write_ppm(before, changed=False)
        write_ppm(after, changed=True)

        log.write_text(evidence(), encoding="utf-8")
        result = compare(before, after, log)
        if result.returncode != 0 or "result=PASS" not in result.stdout:
            raise AssertionError(f"verified selection should compare: rc={result.returncode}\n{result.stdout}")

        log.write_text(evidence(input_state="NOT_RUN", input_result="failed", gtk="failed"), encoding="utf-8")
        assert_incomplete(compare(before, after, log), "skipped input")

        log.write_text(evidence(input_state="verified", input_result="failed", gtk="passed"), encoding="utf-8")
        assert_incomplete(compare(before, after, log), "failed input")

        log.write_text(evidence(selection_pid=740), encoding="utf-8")
        assert_incomplete(compare(before, after, log), "selection from an unrelated PID")

        missing_endpoint = evidence().replace("SETTINGS_ENDPOINT_CONNECTION=passed\n", "")
        log.write_text(missing_endpoint, encoding="utf-8")
        assert_incomplete(compare(before, after, log), "missing Settings endpoint connection")

        missing_render_trace = evidence().replace("SETTINGS_TRACE_TIME_CORRELATION=passed\n", "")
        log.write_text(missing_render_trace, encoding="utf-8")
        assert_incomplete(compare(before, after, log), "missing timestamp-correlated render trace")

        log.write_text(evidence(), encoding="utf-8")
        identical = compare(before, before, log)
        if identical.returncode != 1 or "result=FAIL" not in identical.stdout:
            raise AssertionError(f"verified unchanged content should fail: rc={identical.returncode}\n{identical.stdout}")

    print("PASS: pixel comparison requires verified pointer/keyboard input and GTK page transition")
    print("PASS: skipped input, failed input, and mismatched Settings PID cannot report comparison PASS")
    print("PASS: verified unchanged page-content pixels report FAIL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
