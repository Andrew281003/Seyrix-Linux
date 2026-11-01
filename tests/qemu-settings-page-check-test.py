#!/usr/bin/env python3
"""Regression checks for Settings screenshot page-change evidence."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile

HELPER = Path(__file__).with_name("qemu-settings-page-check.py")
PROFILES = ((640, 480), (800, 600), (1280, 720), (1920, 1080))


def roi(width: int, height: int) -> tuple[int, int, int, int]:
    if width < 760:
        window_x, window_y = 0, 0
        window_width, window_height = width, height
        left, top = window_x + 10, window_y + 185
    else:
        window_x, window_y = 0, 0
        window_width, window_height = width, height
        left, top = 210, 138
    right = min(width, window_x + window_width - 10)
    bottom = min(height, window_y + window_height - (80 if width < 760 else 70))
    return left, top, right, bottom


def write_ppm(path: Path, width: int, height: int, changed_region: bool) -> None:
    raster = bytearray(width * height * 3)
    if changed_region:
        left, top, right, bottom = roi(width, height)
        for y in range(top + 5, min(top + 45, bottom)):
            for x in range(left + 5, min(left + 45, right)):
                offset = (y * width + x) * 3
                raster[offset : offset + 3] = b"\xff\xff\xff"
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + raster)


def write_focus_only_ppm(path: Path, width: int, height: int) -> None:
    raster = bytearray(width * height * 3)
    left, top, right, bottom = roi(width, height)
    for y in range(top + 5, min(top + 20, bottom)):
        for x in range(left + 5, min(left + 20, right)):
            offset = (y * width + x) * 3
            raster[offset : offset + 3] = b"\xff\xff\xff"
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + raster)


def run(previous: Path, current: Path, width: int) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(HELPER), str(previous), str(current), str(width)],
        check=False,
        capture_output=True,
        text=True,
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="seyrix-page-check-") as tmp:
        directory = Path(tmp)
        for width, height in PROFILES:
            before = directory / f"{width}x{height}-before.ppm"
            after = directory / f"{width}x{height}-after.ppm"
            write_ppm(before, width, height, False)
            write_ppm(after, width, height, True)
            result = run(before, after, width)
            require(result.returncode == 0 and "SETTINGS_PAGE_SETTLE=changed" in result.stdout,
                    f"{width}x{height}: in-page content change rejected: {result.stdout}{result.stderr}")

            unchanged = run(before, before, width)
            require(unchanged.returncode == 1 and "SETTINGS_PAGE_SETTLE=unchanged" in unchanged.stdout,
                    f"{width}x{height}: unchanged page accepted: {unchanged.stdout}{unchanged.stderr}")

            focus_only = directory / f"{width}x{height}-focus-only.ppm"
            write_focus_only_ppm(focus_only, width, height)
            focus_result = run(before, focus_only, width)
            require(focus_result.returncode == 1 and "SETTINGS_PAGE_SETTLE=unchanged" in focus_result.stdout,
                    f"{width}x{height}: small focus change accepted as a page switch: {focus_result.stdout}{focus_result.stderr}")

            outside = directory / f"{width}x{height}-outside.ppm"
            raster = bytearray(width * height * 3)
            raster[0:3] = b"\xff\xff\xff"
            outside.write_bytes(f"P6\n{width} {height}\n255\n".encode() + raster)
            cursor_only = run(before, outside, width)
            require(cursor_only.returncode == 1 and "SETTINGS_PAGE_SETTLE=unchanged" in cursor_only.stdout,
                    f"{width}x{height}: outside-window pixel accepted as a page change")

            mismatch = run(before, after, width + 1)
            require(mismatch.returncode == 1 and "does not match output" in mismatch.stdout,
                    f"{width}x{height}: output-size mismatch was not rejected")

    print("PAGE_PIXEL_HELPER_REGRESSION=PASS profiles=640x480,800x600,1280x720,1920x1080 cases=20")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
