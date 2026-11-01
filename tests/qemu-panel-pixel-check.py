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


def read_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        magic = read_token(stream)
        width = int(read_token(stream))
        height = int(read_token(stream))
        max_value = int(read_token(stream))
    if magic != b"P6" or max_value != 255:
        raise ValueError(f"{path} is not an 8-bit P6 PPM")
    return width, height


def read_pixel(path: Path, x: int, y: int) -> tuple[int, int, int]:
    width, height = read_dimensions(path)
    if not (0 <= x < width and 0 <= y < height):
        raise ValueError(f"pixel ({x},{y}) is outside {width}x{height} image")
    with path.open("rb") as stream:
        for _ in range(4):
            read_token(stream)
        stream.seek((y * width + x) * 3, 1)
        pixel = stream.read(3)
    if len(pixel) != 3:
        raise ValueError(f"{path} does not contain the requested pixel")
    return tuple(pixel)


def is_selected(pixel: tuple[int, int, int]) -> bool:
    return pixel[2] > 120 and pixel[1] > 90


def workspace_button_centers(path: Path) -> tuple[int, int, int, int]:
    width, height = read_dimensions(path)
    y = height - 44
    if y < 0:
        raise ValueError(f"{path} is too short to contain the bottom panel")
    with path.open("rb") as stream:
        for _ in range(4):
            read_token(stream)
        stream.seek(y * width * 3, 1)
        row = stream.read(width * 3)
    if len(row) != width * 3:
        raise ValueError(f"{path} does not contain the complete workspace-button row")

    colored = []
    for x in range(width):
        red, green, blue = row[x * 3 : x * 3 + 3]
        colored.append(red > 40 and green > 55 and blue > 70)
    runs: list[tuple[int, int]] = []
    start = None
    for x, present in enumerate(colored + [False]):
        if present and start is None:
            start = x
        elif not present and start is not None:
            end = x - 1
            if 8 <= end - start + 1 <= 16:
                runs.append((start, end))
            start = None

    for first in range(max(0, len(runs) - 3)):
        group = runs[first : first + 4]
        if all(group[index + 1][0] - group[index][0] == 36
                for index in range(3)):
            return tuple(start + (end - start + 1) // 2
                for start, end in group)
    raise ValueError(f"{path} does not show four centered workspace buttons")


def locate(path: Path) -> int:
    try:
        width, height = read_dimensions(path)
        centers = workspace_button_centers(path)
        colors = tuple(read_pixel(path, x, height - 44) for x in centers)
    except (OSError, ValueError) as error:
        print(f"PANEL_PIXEL_ASSERTION=failed reason={error}")
        return 1
    selected = [index + 1 for index, color in enumerate(colors)
        if is_selected(color)]
    print("PANEL_WORKSPACE_CENTERS=" + ",".join(map(str, centers)))
    print(f"PANEL_WORKSPACE_THREE_X={centers[2]}")
    print(f"PANEL_WORKSPACE_ACTIVE={selected[0] if len(selected) == 1 else 'unknown'}")
    print(f"PANEL_WORKSPACE_SAMPLE_Y={height - 44} IMAGE_WIDTH={width}")
    return 0 if len(selected) == 1 else 1


def main() -> int:
    if len(sys.argv) == 3 and sys.argv[1] == "--locate":
        return locate(Path(sys.argv[2]))
    if len(sys.argv) not in (3, 5):
        print(
            f"Usage: {sys.argv[0]} before.ppm after.ppm "
            "[before-workspace after-workspace]\n"
            f"       {sys.argv[0]} --locate screenshot.ppm",
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
        width, height = read_dimensions(before_path)
        after_width, after_height = read_dimensions(after_path)
        if (width, height) != (after_width, after_height):
            raise ValueError("before and after screenshots have different dimensions")
        # App icons and running windows change the centered panel width.
        before_x = workspace_button_centers(before_path)
        after_x = workspace_button_centers(after_path)
        button_y = height - 44
        before = tuple(read_pixel(before_path, x, button_y) for x in before_x)
        after = tuple(read_pixel(after_path, x, button_y) for x in after_x)
    except (OSError, ValueError) as error:
        print(f"PANEL_PIXEL_ASSERTION=failed reason={error}")
        return 1

    before_selected = [index for index, pixel in enumerate(before) if is_selected(pixel)]
    after_selected = [index for index, pixel in enumerate(after) if is_selected(pixel)]
    for index, (before_pixel, after_pixel) in enumerate(zip(before, after), start=1):
        print(
            f"PANEL_WORKSPACE_BUTTON={index} "
            f"before_point={before_x[index - 1]},{button_y} "
            f"after_point={after_x[index - 1]},{button_y} "
            f"before={before_pixel[0]},{before_pixel[1]},{before_pixel[2]} "
            f"after={after_pixel[0]},{after_pixel[1]},{after_pixel[2]}"
        )
    before_expected = before_selected == [before_workspace]
    after_expected = after_selected == [after_workspace]
    selected_point_changed = sum(abs(a - b) for a, b in zip(
        before[after_workspace], after[after_workspace])) >= 50
    print(
        f"PANEL_WORKSPACE_ACTIVE_BEFORE={[i + 1 for i in before_selected]} "
        f"PANEL_WORKSPACE_ACTIVE_AFTER={[i + 1 for i in after_selected]}"
    )
    if before_expected and after_expected and selected_point_changed:
        print("PANEL_PIXEL_ASSERTION=passed")
        return 0
    print(
        "PANEL_PIXEL_ASSERTION=failed "
        f"expected_before={before_workspace + 1} expected_after={after_workspace + 1} "
        f"before_valid={int(before_expected)} after_valid={int(after_expected)} "
        f"workspace{after_workspace + 1}_pixel_changed={int(selected_point_changed)}"
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())