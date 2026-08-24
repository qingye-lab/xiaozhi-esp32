#!/usr/bin/env python3
"""Prepare small transparent Xiaoya source frames from generated pose sheets."""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

from PIL import Image


SHEET_COLUMNS = 3
SHEET_ROWS = 2
SOURCE_SIZE = 256
CHARACTER_SIZE = 240


def _is_neutral_background(pixel: tuple[int, int, int, int]) -> bool:
    red, green, blue, _ = pixel
    return (
        min(red, green, blue) >= 205
        and max(red, green, blue) - min(red, green, blue) <= 18
    )


def _clear_connected_background(image: Image.Image) -> Image.Image:
    """Turn only edge-connected neutral matte pixels into real alpha."""
    image = image.convert("RGBA")
    pixels = image.load()
    width, height = image.size
    queue: deque[tuple[int, int]] = deque()
    visited: set[tuple[int, int]] = set()

    for x in range(width):
        queue.extend(((x, 0), (x, height - 1)))
    for y in range(height):
        queue.extend(((0, y), (width - 1, y)))

    while queue:
        x, y = queue.popleft()
        point = (x, y)
        if point in visited or not _is_neutral_background(pixels[x, y]):
            continue
        visited.add(point)
        if x > 0:
            queue.append((x - 1, y))
        if x + 1 < width:
            queue.append((x + 1, y))
        if y > 0:
            queue.append((x, y - 1))
        if y + 1 < height:
            queue.append((x, y + 1))

    for x, y in visited:
        red, green, blue, _ = pixels[x, y]
        pixels[x, y] = (red, green, blue, 0)
    return image


def _split_sheet(path: Path) -> list[Image.Image]:
    sheet = Image.open(path).convert("RGBA")
    frames = []
    for row in range(SHEET_ROWS):
        for column in range(SHEET_COLUMNS):
            left = round(column * sheet.width / SHEET_COLUMNS)
            right = round((column + 1) * sheet.width / SHEET_COLUMNS)
            top = round(row * sheet.height / SHEET_ROWS)
            bottom = round((row + 1) * sheet.height / SHEET_ROWS)
            cell = _clear_connected_background(sheet.crop((left, top, right, bottom)))
            bbox = cell.getchannel("A").getbbox()
            if bbox is None:
                raise ValueError(f"animation sheet has an empty cell: {path}")
            frames.append(cell.crop(bbox))
    return frames


def prepare_sheet(path: Path, output_dir: Path) -> None:
    frames = _split_sheet(path)
    max_width = max(frame.width for frame in frames)
    max_height = max(frame.height for frame in frames)
    scale = min(CHARACTER_SIZE / max_width, CHARACTER_SIZE / max_height)
    output_dir.mkdir(parents=True, exist_ok=True)

    for old_frame in output_dir.glob("*.png"):
        old_frame.unlink()
    for index, frame in enumerate(frames, start=1):
        width = max(1, round(frame.width * scale))
        height = max(1, round(frame.height * scale))
        frame = frame.resize((width, height), Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", (SOURCE_SIZE, SOURCE_SIZE), (0, 0, 0, 0))
        canvas.alpha_composite(frame, ((SOURCE_SIZE - width) // 2, SOURCE_SIZE - 8 - height))
        canvas.save(output_dir / f"{index:02d}.png", optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sheet_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()

    sheets = sorted(args.sheet_dir.glob("*.png"))
    if not sheets:
        raise ValueError(f"no animation sheets found: {args.sheet_dir}")
    for sheet in sheets:
        prepare_sheet(sheet, args.output_dir / sheet.stem)
    print(f"Prepared {len(sheets)} animation families at {SOURCE_SIZE}x{SOURCE_SIZE}")


if __name__ == "__main__":
    main()
