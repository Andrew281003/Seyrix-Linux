#!/usr/bin/env python3
"""Detect a Settings content-page change in a QEMU PPM screenshot pair."""

from __future__ import annotations

import sys
from pathlib import Path


def token(stream: object) -> bytes:
    while True:
        byte = stream.read(1)
        if not byte:
            raise ValueError("unexpected end of PPM header")
        if byte in b" \t\r\n\v\f":
            continue
        if byte == b"#":
            stream.readline()
            continue
        value = bytearray(byte)
        while True:
            byte = stream.read(1)
            if not byte or byte in b" \t\r\n\v\f":
                if byte == b"\r":
                    next_byte = stream.read(1)
                    if next_byte and next_byte != b"\n":
                        stream.seek(-1, 1)
                return bytes(value)
            value.extend(byte)


def read_image(path: Path) -> tuple[int, int, bytes]:
    with path.open("rb") as stream:
        magic = token(stream)
        width = int(token(stream))
        height = int(token(stream))
        max_value = int(token(stream))
        if magic != b"P6" or max_value != 255:
            raise ValueError(f"{path} is not an 8-bit P6 PPM")
        pixels = stream.read()
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path} contains an incomplete PPM image")
    return width, height, pixels


def main() -> int:
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} previous.ppm current.ppm output-width", file=sys.stderr)
        return 2
    previous_path, current_path = map(Path, sys.argv[1:3])
    try:
        output_width = int(sys.argv[3])
        previous_width, previous_height, previous = read_image(previous_path)
        current_width, current_height, current = read_image(current_path)
        if (previous_width, previous_height) != (current_width, current_height):
            raise ValueError("previous and current screenshots have different dimensions")
        if previous_width != output_width:
            raise ValueError(f"screenshot width {previous_width} does not match output {output_width}")
        if output_width < 760:
            window_x, window_y = 0, 0
            window_width, window_height = output_width, current_height
            left, top = window_x + 10, window_y + 185
        else:
            # The compositor presents the mapped Settings surface at output
            # bounds even though the app requests a smaller default size.
            left, top = 210, 138
            window_x, window_y = 0, 0
            window_width, window_height = output_width, current_height
        right = min(output_width, window_x + window_width - 10)
        bottom = min(current_height, window_y + window_height - (80 if output_width < 760 else 70))
        if left >= right or top >= bottom:
            raise ValueError("Settings content region is outside the screenshot")
        changed_pixels = 0
        total_difference = 0
        for y in range(top, bottom):
            for x in range(left, right):
                offset = (y * output_width + x) * 3
                difference = sum(abs(previous[offset + channel] - current[offset + channel])
                    for channel in range(3))
                total_difference += difference
                if difference > 30:
                    changed_pixels += 1
    except (OSError, ValueError) as error:
        print(f"SETTINGS_PAGE_SETTLE=failed reason={error}")
        return 1

    print(
        f"SETTINGS_PAGE_DIFF_ROI={left},{top}-{right},{bottom} "
        f"changed_pixels={changed_pixels} total_difference={total_difference}"
    )
    if changed_pixels >= 1000 and total_difference >= 100000:
        print("SETTINGS_PAGE_SETTLE=changed")
        return 0
    print("SETTINGS_PAGE_SETTLE=unchanged")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
