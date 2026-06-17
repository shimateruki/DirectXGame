from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "Resources" / "sprite" / "ui" / "hud" / "morph_gauge"
FRAME_COUNT = 33
SIZE = 160
SCALE = 3


def canvas() -> Image.Image:
    return Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE), (0, 0, 0, 0))


def downsample(image: Image.Image) -> Image.Image:
    return image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def arc_points(cx: float, cy: float, radius: float, thickness: float, start: float, end: float, steps: int) -> list[tuple[float, float]]:
    outer: list[tuple[float, float]] = []
    inner: list[tuple[float, float]] = []
    for i in range(steps + 1):
        t = i / max(steps, 1)
        a = start + (end - start) * t
        outer.append((cx + math.cos(a) * radius, cy + math.sin(a) * radius))
        inner.append((cx + math.cos(a) * (radius - thickness), cy + math.sin(a) * (radius - thickness)))
    return outer + inner[::-1]


def draw_full_ring(draw: ImageDraw.ImageDraw, color: tuple[int, int, int, int], radius: float, thickness: float) -> None:
    cx = cy = SIZE * SCALE * 0.5
    bbox = (cx - radius, cy - radius, cx + radius, cy + radius)
    draw.ellipse(bbox, outline=color, width=max(1, int(thickness)))


def draw_slime_icon(draw: ImageDraw.ImageDraw) -> None:
    cx = cy = SIZE * SCALE * 0.5
    body = (cx - 31 * SCALE, cy - 20 * SCALE, cx + 31 * SCALE, cy + 31 * SCALE)
    draw.ellipse(body, fill=(29, 186, 224, 255), outline=(213, 255, 255, 210), width=3 * SCALE)
    draw.ellipse((cx - 13 * SCALE, cy - 4 * SCALE, cx - 7 * SCALE, cy + 6 * SCALE), fill=(13, 45, 120, 255))
    draw.ellipse((cx + 7 * SCALE, cy - 4 * SCALE, cx + 13 * SCALE, cy + 6 * SCALE), fill=(13, 45, 120, 255))
    draw.ellipse((cx - 10 * SCALE, cy - 2 * SCALE, cx - 8 * SCALE, cy + 1 * SCALE), fill=(255, 255, 255, 230))
    draw.ellipse((cx + 10 * SCALE, cy - 2 * SCALE, cx + 12 * SCALE, cy + 1 * SCALE), fill=(255, 255, 255, 230))
    draw.arc((cx - 12 * SCALE, cy + 5 * SCALE, cx + 12 * SCALE, cy + 17 * SCALE), 12, 168, fill=(20, 84, 132, 210), width=2 * SCALE)


def draw_back() -> None:
    image = canvas()
    glow = Image.new("RGBA", image.size, (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow, "RGBA")
    draw_full_ring(glow_draw, (55, 216, 240, 110), 66 * SCALE, 13 * SCALE)
    glow = glow.filter(ImageFilter.GaussianBlur(5 * SCALE))
    image = Image.alpha_composite(image, glow)
    draw = ImageDraw.Draw(image, "RGBA")
    draw.ellipse((26 * SCALE, 26 * SCALE, 134 * SCALE, 134 * SCALE), fill=(5, 55, 78, 142))
    draw_full_ring(draw, (155, 240, 255, 132), 61 * SCALE, 7 * SCALE)
    downsample(image).save(OUT_DIR / "back.png")


def draw_frame() -> None:
    image = canvas()
    draw = ImageDraw.Draw(image, "RGBA")
    draw_full_ring(draw, (236, 255, 246, 245), 66 * SCALE, 5 * SCALE)
    draw_full_ring(draw, (61, 214, 237, 240), 58 * SCALE, 3 * SCALE)
    for i in range(8):
        a = -math.pi / 2 + math.tau * i / 8
        cx = SIZE * SCALE * 0.5 + math.cos(a) * 66 * SCALE
        cy = SIZE * SCALE * 0.5 + math.sin(a) * 66 * SCALE
        draw.ellipse((cx - 2.5 * SCALE, cy - 2.5 * SCALE, cx + 2.5 * SCALE, cy + 2.5 * SCALE), fill=(255, 247, 126, 230))
    downsample(image).save(OUT_DIR / "frame.png")


def draw_icon() -> None:
    image = canvas()
    draw = ImageDraw.Draw(image, "RGBA")
    draw.ellipse((42 * SCALE, 42 * SCALE, 118 * SCALE, 118 * SCALE), fill=(220, 255, 244, 215))
    draw_slime_icon(draw)
    downsample(image).save(OUT_DIR / "icon.png")


def draw_fill(index: int) -> None:
    rate = index / float(FRAME_COUNT - 1)
    image = canvas()
    draw = ImageDraw.Draw(image, "RGBA")
    if rate > 0.001:
        cx = cy = SIZE * SCALE * 0.5
        start = -math.pi / 2.0
        end = start + math.tau * rate
        steps = max(8, int(128 * rate))
        if rate < 0.28:
            color = (255, 110, 70, 245)
            edge = (255, 235, 105, 230)
        else:
            color = (45, 211, 239, 245)
            edge = (255, 245, 117, 230)
        draw.polygon(arc_points(cx, cy, 63 * SCALE, 16 * SCALE, start, end, steps), fill=color)
        draw.line(arc_points(cx, cy, 63 * SCALE, 1 * SCALE, end, end + 0.001, 1), fill=edge, width=5 * SCALE)
    downsample(image).save(OUT_DIR / f"fill_{index:02d}.png")


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    draw_back()
    draw_frame()
    draw_icon()
    for index in range(FRAME_COUNT):
        draw_fill(index)
    print(f"generated morph gauge sprites in {OUT_DIR}")


if __name__ == "__main__":
    main()
