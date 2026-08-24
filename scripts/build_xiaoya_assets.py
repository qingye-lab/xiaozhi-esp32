#!/usr/bin/env python3
"""Build the board-local Xiaoya GIF collection from transparent source frames."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


CANVAS_SIZE = 176
SOURCE_SIZE = 256
SOURCE_FRAMES = 6
FRAME_DURATION_MS = 90
MAX_RUNTIME_BYTES = 2_500_000

EMOTION_FAMILIES = {
    "angry": "angry",
    "confident": "happy",
    "confused": "thinking",
    "cool": "happy",
    "crying": "sad",
    "delicious": "happy",
    "embarrassed": "sad",
    "funny": "surprised",
    "happy": "happy",
    "kissy": "loving",
    "laughing": "happy",
    "loving": "loving",
    "neutral": "idle",
    "relaxed": "sleepy",
    "sad": "sad",
    "shocked": "surprised",
    "silly": "surprised",
    "sleepy": "sleepy",
    "surprised": "surprised",
    "thinking": "thinking",
    "winking": "idle",
}

STARTUP_ALIASES = {"robot_2": "idle"}


def _load_source_frames(source_dir: Path, family: str) -> list[Image.Image]:
    paths = sorted((source_dir / "frames" / family).glob("*.png"))
    if len(paths) != SOURCE_FRAMES:
        raise ValueError(
            f"{family} must contain {SOURCE_FRAMES} transparent PNG frames, got {len(paths)}"
        )

    frames = []
    for path in paths:
        with Image.open(path) as image:
            if image.size != (SOURCE_SIZE, SOURCE_SIZE) or "A" not in image.getbands():
                raise ValueError(f"invalid transparent source frame: {path}")
            frame = image.convert("RGBA")
        alpha = frame.getchannel("A")
        if alpha.getextrema()[0] != 0 or alpha.getbbox() is None:
            raise ValueError(f"source frame does not contain real transparency: {path}")
        frames.append(frame.resize((CANVAS_SIZE, CANVAS_SIZE), Image.Resampling.LANCZOS))
    return frames


def _frame_durations(family: str) -> list[int]:
    if family == "idle":
        return [650, 90, 90, 90, 140, 300]
    return [FRAME_DURATION_MS] * SOURCE_FRAMES


def _palettize(frame: Image.Image) -> Image.Image:
    alpha = frame.getchannel("A")
    palette = frame.convert("RGB").quantize(
        colors=191, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE
    )
    transparent = alpha.point(lambda value: 255 if value <= 8 else 0)
    palette.paste(255, mask=transparent)
    palette.info["transparency"] = 255
    return palette


def build_collection(source_dir: Path, output_dir: Path) -> int:
    output_dir.mkdir(parents=True, exist_ok=True)
    asset_families = {**EMOTION_FAMILIES, **STARTUP_ALIASES}
    sources = {
        family: _load_source_frames(source_dir, family)
        for family in sorted(set(asset_families.values()))
    }

    for old_file in output_dir.glob("*.gif"):
        old_file.unlink()
    for emotion, family in asset_families.items():
        frames = [_palettize(frame) for frame in sources[family]]
        frames[0].save(
            output_dir / f"{emotion}.gif",
            save_all=True,
            append_images=frames[1:],
            duration=_frame_durations(family),
            loop=0,
            optimize=True,
            disposal=2,
            transparency=255,
        )

    total_bytes = sum(path.stat().st_size for path in output_dir.glob("*.gif"))
    if total_bytes > MAX_RUNTIME_BYTES:
        raise ValueError(
            f"Xiaoya runtime assets exceed {MAX_RUNTIME_BYTES} bytes: {total_bytes}"
        )
    return total_bytes


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    total_bytes = build_collection(args.source_dir, args.output_dir)
    print(
        f"Built {len(EMOTION_FAMILIES)} emotions plus "
        f"{len(STARTUP_ALIASES)} startup alias ({total_bytes} bytes)"
    )


if __name__ == "__main__":
    main()
