#!/usr/bin/env python3
"""Compare the QMP Settings screenshots and fail if page-content pixels stay stale."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


WHITESPACE = b" \t\r\n\v\f"
REQUIRED_SELECTION_MARKERS = (
    "SETTINGS_PAGE_INPUT_SEQUENCE=verified",
    "SETTINGS_INPUT_VERIFICATION_RESULT=passed",
    "SETTINGS_GTK_PAGE_TRANSITION=passed",
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
)


def _token(data: bytes, offset: int) -> tuple[bytes, int]:
    while offset < len(data):
        if data[offset] in WHITESPACE:
            offset += 1
        elif data[offset] == ord("#"):
            newline = data.find(b"\n", offset)
            offset = len(data) if newline < 0 else newline + 1
        else:
            break
    start = offset
    while offset < len(data) and data[offset] not in WHITESPACE + b"#":
        offset += 1
    if start == offset:
        raise ValueError("truncated PPM header")
    return data[start:offset], offset


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    magic, offset = _token(data, 0)
    width_token, offset = _token(data, offset)
    height_token, offset = _token(data, offset)
    maxval_token, offset = _token(data, offset)
    if magic != b"P6":
        raise ValueError(f"{path}: expected binary P6 PPM")
    width, height, maxval = int(width_token), int(height_token), int(maxval_token)
    if width <= 0 or height <= 0 or maxval != 255:
        raise ValueError(f"{path}: unsupported dimensions or maxval")
    expected = width * height * 3
    if offset >= len(data) or data[offset] not in WHITESPACE:
        raise ValueError(f"{path}: missing PPM raster separator")
    raster_start = offset + (2 if data[offset:offset + 2] == b"\r\n" else 1)
    while len(data) - raster_start != expected and raster_start < len(data) and data[raster_start] in WHITESPACE:
        raster_start += 1
    if len(data) - raster_start != expected:
        raise ValueError(
            f"{path}: raster size mismatch; expected {expected}, "
            f"found {len(data) - raster_start}"
        )
    return width, height, data[raster_start:]


def compare_region(
    before: bytes, after: bytes, width: int, bounds: tuple[int, int, int, int]
) -> tuple[int, int, int]:
    x0, y0, x1, y1 = bounds
    changed = 0
    total_difference = 0
    pixels = (x1 - x0) * (y1 - y0)
    for y in range(y0, y1):
        row_start = (y * width + x0) * 3
        row_end = (y * width + x1) * 3
        for offset in range(row_start, row_end, 3):
            differences = (
                abs(before[offset] - after[offset]),
                abs(before[offset + 1] - after[offset + 1]),
                abs(before[offset + 2] - after[offset + 2]),
            )
            if differences != (0, 0, 0):
                changed += 1
                total_difference += sum(differences)
    return changed, total_difference, pixels


def validate_selection_evidence(path: Path) -> list[str]:
    try:
        lines = [line.strip() for line in path.read_text(encoding="utf-8", errors="replace").splitlines()]
    except OSError as error:
        return [f"evidence-log-unreadable:{error}"]

    missing = [marker for marker in REQUIRED_SELECTION_MARKERS if marker not in lines]
    launch_pid = None
    for line in lines:
        match = re.search(
            r"SETTINGS_TRACE_APP_PID=([0-9]+) CHECKED_PROC_PID=([0-9]+) ", line
        )
        if match:
            if match.group(1) != match.group(2):
                missing.append("settings-pid-does-not-match-proc-check")
            else:
                launch_pid = match.group(1)
            break
    if launch_pid is None:
        missing.append("verified-settings-pid-missing")
    if launch_pid is not None and f"SETTINGS_GTK_SELECTION_PID={launch_pid}" not in lines:
        missing.append("gtk-selection-pid-does-not-match-launch-pid")
    return missing


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", required=True, type=Path)
    parser.add_argument("--after", required=True, type=Path)
    parser.add_argument("--evidence-log", required=True, type=Path)
    args = parser.parse_args()
    missing_evidence = validate_selection_evidence(args.evidence_log)
    if missing_evidence:
        print(
            "SETTINGS_PIXEL_COMPARISON_INCOMPLETE "
            f"reason=selection-evidence-missing details={';'.join(missing_evidence)}"
        )
        print(
            "STAGE_RESULT name={Settings QMP screenshot content comparison} "
            "result=INCOMPLETE reason=unverified-input-or-selection"
        )
        return 2
    try:
        before_width, before_height, before = read_ppm(args.before)
        after_width, after_height, after = read_ppm(args.after)
        if (before_width, before_height) != (after_width, after_height):
            raise ValueError("QMP screenshots have different dimensions")
        if (before_width, before_height) != (640, 480):
            raise ValueError(
                f"expected 640x480 for the verified input coordinates; "
                f"got {before_width}x{before_height}"
            )
        roi = (10, 194, 630, 421)
        if roi[2] > before_width or roi[3] > before_height:
            raise ValueError("page-content ROI is outside the screenshot")
        full = compare_region(before, after, before_width, (0, 0, before_width, before_height))
        content = compare_region(before, after, before_width, roi)
    except (OSError, ValueError) as error:
        print(f"STAGE_RESULT name={{Settings QMP screenshot content comparison}} result=INCOMPLETE reason={error}")
        return 2

    print(
        "SETTINGS_PIXEL_COMPARISON_FULL_FRAME "
        f"size={before_width}x{before_height} changed_pixels={full[0]} "
        f"total_pixels={full[2]} total_abs_rgb_difference={full[1]}"
    )
    print(
        "SETTINGS_PIXEL_COMPARISON_PAGE_CONTENT_ROI "
        f"bounds=[{roi[0]},{roi[1]},{roi[2]},{roi[3]}) "
        f"changed_pixels={content[0]} total_pixels={content[2]} "
        f"total_abs_rgb_difference={content[1]}"
    )
    if content[0] == 0:
        print(
            "STAGE_RESULT name={Settings QMP screenshot content comparison} "
            "result=FAIL reason=no-page-content-pixel-change"
        )
        return 1
    print(
        "STAGE_RESULT name={Settings QMP screenshot content comparison} "
        "result=PASS"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
