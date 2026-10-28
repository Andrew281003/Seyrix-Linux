#!/usr/bin/env python3
"""Check that the native panel visibly changes its selected workspace button."""

from __future__ import annotations

import sys
from pathlib import Path


def read_token(stream) -> bytes:
    while True:
        byte = stream.read(1)
        if not byte:
            raise ValueError("unexpected end of PPM header")
        if byte in b" \t\r\n\v\f":
            continue
        if byte == b"#":
            stream.readline()
            continue
        token = bytearray(byte)
        while True:
            byte = stream.read(1)
            if not byte or byte in b" \t\r\n\v\f":
                if byte == b"\r":
                    next_byte = stream.read(1)
                    if next_byte and next_byte != b"\n":
                        stream.seek(-1, 1)
                return bytes(token)
            token.extend(byte)


def read_pixel(path: Path, x: int, y: int) -> tuple[int, int, int]:
    with path.open("rb") as stream:
        magic = read_token(stream)
        width = int(read_token(stream))
        height = int(read_token(stream))
        max_value = int(read_token(stream))
        if magic != b"P6" or max_value != 255:
            raise ValueError(f"{path} is not an 8-bit P6 PPM")
        if not (0 <= x < width and 0 <= y < height):
            raise ValueError(f"pixel ({x},{y}) is outside {width}x{height} image")
        payload = stream.read()
    expected_size = width * height * 3
    if len(payload) != expected_size:
        raise ValueError(f"{path} has {len(payload)} pixel bytes; expected {expected_size}")
    offset = (y * width + x) * 3
    return tuple(payload[offset : offset + 3])


def is_selected(pixel: tuple[int, int, int]) -> bool:
    return pixel[2] > 120 and pixel[1] > 90


def main() -> int:
    if len(sys.argv) not in (3, 5):
        print(
            f"Usage: {sys.argv[0]} before.ppm after.ppm "
            "[before-workspace after-workspace]",
            file=sys.stderr,
        )
        return 2
    before_path, after_path = map(Path, sys.argv[1:3])
    before_workspace, after_workspace = (0, 2)
    if len(sys.argv) == 5:
        before_workspace, after_workspace = (int(value) - 1 for value in sys.argv[3:5])
    if not (0 <= before_workspace < 4 and 0 <= after_workspace < 4):
        print("Workspace selections must be numbered 1 through 4.", file=sys.stderr)
        return 2
    try:
        with before_path.open("rb") as stream:
            magic = read_token(stream)
            width = int(read_token(stream))
            height = int(read_token(stream))
            max_value = int(read_token(stream))
        with after_path.open("rb") as stream:
            after_magic = read_token(stream)
            after_width = int(read_token(stream))
            after_height = int(read_token(stream))
            after_max_value = int(read_token(stream))
        if (magic, max_value) != (b"P6", 255) or (after_magic, after_max_value) != (b"P6", 255):
            raise ValueError("screenshots must be 8-bit P6 PPM images")
        if (width, height) != (after_width, after_height):
            raise ValueError("before and after screenshots have different dimensions")
        # The four compact workspace buttons sit in the centered 214 px panel.
        panel_left = (width - 214) // 2
        button_x = tuple(panel_left + offset for offset in (24, 59, 95, 131))
        button_y = height - 44
        before = tuple(read_pixel(before_path, x, button_y) for x in button_x)
        after = tuple(read_pixel(after_path, x, button_y) for x in button_x)
    except (OSError, ValueError) as error:
        print(f"PANEL_PIXEL_ASSERTION=failed reason={error}")
        return 1

    before_selected = [index for index, pixel in enumerate(before) if is_selected(pixel)]
    after_selected = [index for index, pixel in enumerate(after) if is_selected(pixel)]
    for index, (before_pixel, after_pixel) in enumerate(zip(before, after), start=1):
        print(
            f"PANEL_WORKSPACE_BUTTON={index} point={button_x[index - 1]},{button_y} "
            f"before={before_pixel[0]},{before_pixel[1]},{before_pixel[2]} "
            f"after={after_pixel[0]},{after_pixel[1]},{after_pixel[2]}"
        )
    before_expected = before_selected == [before_workspace]
    after_expected = after_selected == [after_workspace]
    # Workspace 3 is sampled at its button interior in the bottom-centered panel.
    selected_point_changed = sum(abs(a - b) for a, b in zip(before[2], after[2])) >= 50
    print(
        f"PANEL_WORKSPACE_ACTIVE_BEFORE={before_selected} "
        f"PANEL_WORKSPACE_ACTIVE_AFTER={after_selected}"
    )
    if before_expected and after_expected and selected_point_changed:
        print("PANEL_PIXEL_ASSERTION=passed")
        return 0
    print(
        "PANEL_PIXEL_ASSERTION=failed "
        f"expected_before={before_workspace + 1} expected_after={after_workspace + 1} "
        f"before_valid={int(before_expected)} after_valid={int(after_expected)} "
        f"workspace3_pixel_changed={int(selected_point_changed)}"
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
