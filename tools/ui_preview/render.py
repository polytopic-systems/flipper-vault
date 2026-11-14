"""Turn the preview's raw frames into PNG stills, contact sheets and GIFs.

Input: build/frames/<scene>.bin = sequence of 1024-byte u8g2 frames, plus
build/frames/<scene>.txt = one line per frame: "<ms> <keep 0|1>" (frame
duration, and whether to export it as a still). Output goes to the given
directory: <scene>.gif, <scene>-NN.png stills, and <scene>-sheet.png.

    uv run --no-project --with pillow python -I render.py <frames dir> <out dir> [scale]
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

INK = (0x2A, 0x1B, 0x08)
LIT = (0xF4, 0xA4, 0x24)


def frame_image(raw: bytes, scale: int) -> Image.Image:
    img = Image.new("P", (128, 64))
    img.putpalette(list(LIT) + list(INK) + [0] * (254 * 3))
    px = img.load()
    for y in range(64):
        for x in range(128):
            px[x, y] = (raw[x + (y // 8) * 128] >> (y & 7)) & 1
    return img.resize((128 * scale, 64 * scale), Image.NEAREST)


def sheet(images: list[Image.Image], cols: int) -> Image.Image:
    w, h = images[0].size
    gap = 6
    rows = (len(images) + cols - 1) // cols
    out = Image.new("RGB", (cols * w + (cols + 1) * gap, rows * h + (rows + 1) * gap), (24, 24, 24))
    for i, im in enumerate(images):
        out.paste(im.convert("RGB"), (gap + (i % cols) * (w + gap), gap + (i // cols) * (h + gap)))
    return out


def main() -> None:
    frames_dir = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    out_dir.mkdir(parents=True, exist_ok=True)
    for bin_path in sorted(frames_dir.glob("*.bin")):
        name = bin_path.stem
        raw = bin_path.read_bytes()
        meta = [line.split() for line in (frames_dir / f"{name}.txt").read_text().splitlines()]
        frames = [raw[i * 1024 : (i + 1) * 1024] for i in range(len(meta))]
        images = [frame_image(f, scale) for f in frames]
        stills = [im for im, m in zip(images, meta) if m[1] == "1"]
        if len(images) > 1:
            images[0].save(out_dir / f"{name}.gif", save_all=True, append_images=images[1:],
                           duration=[int(m[0]) for m in meta], loop=0, disposal=1, optimize=False)
        for i, im in enumerate(stills):
            suffix = "" if len(stills) == 1 else f"-{i:02d}"
            im.save(out_dir / f"{name}{suffix}.png")
        if len(stills) > 1:
            small = [frame_image(f, 2) for f, m in zip(frames, meta) if m[1] == "1"]
            sheet(small, 4 if len(small) > 4 else len(small)).save(out_dir / f"{name}-sheet.png")


if __name__ == "__main__":
    main()
