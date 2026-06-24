from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
FADE_ROOT = ROOT / "Resources" / "sprite" / "fade"
WIDTH = 1280
HEIGHT = 720
FRAME_COUNT = 48
SCALE = 3


def smoothstep(value: float) -> float:
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def scaled_canvas() -> Image.Image:
    return Image.new("RGBA", (WIDTH * SCALE, HEIGHT * SCALE), (0, 0, 0, 0))


def downsample(image: Image.Image) -> Image.Image:
    return image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)


def blob_points(cx: float, cy: float, radius: float, phase: float, wobble: float) -> list[tuple[float, float]]:
    points: list[tuple[float, float]] = []
    for i in range(192):
        angle = math.tau * i / 192.0
        wave = (
            math.sin(angle * 3.0 + phase) * 0.55
            + math.sin(angle * 5.0 - phase * 0.7) * 0.32
            + math.sin(angle * 2.0 + phase * 1.4) * 0.18
        )
        r = radius * (1.0 + wave * wobble)
        points.append((cx + math.cos(angle) * r, cy + math.sin(angle) * r))
    return points


def draw_slime_wipe_frame(index: int) -> Image.Image:
    progress = index / float(FRAME_COUNT - 1)
    t = smoothstep(progress)
    image = scaled_canvas()
    if index == 0:
        return downsample(image)

    draw = ImageDraw.Draw(image, "RGBA")
    cx = WIDTH * SCALE * 0.5
    cy = HEIGHT * SCALE * 0.5
    diagonal = math.hypot(WIDTH, HEIGHT) * SCALE

    radius = diagonal * (0.040 + 0.540 * t)
    wobble = 0.038 + 0.018 * (1.0 - t)
    phase = progress * math.tau * 0.85

    outer = blob_points(cx, cy, radius + 7.0 * SCALE, phase, wobble)
    inner = blob_points(cx, cy, radius, phase + 0.18, wobble * 0.82)

    draw.polygon(outer, fill=(20, 104, 142, 255))
    draw.polygon(inner, fill=(45, 204, 238, 255))

    return downsample(image)


def star_points(cx: float, cy: float, outer_radius: float, inner_radius: float, phase: float) -> list[tuple[float, float]]:
    points: list[tuple[float, float]] = []
    for i in range(10):
        radius = outer_radius if i % 2 == 0 else inner_radius
        angle = phase - math.pi / 2.0 + math.tau * i / 10.0
        points.append((cx + math.cos(angle) * radius, cy + math.sin(angle) * radius))
    return points


def crown_points(cx: float, cy: float, radius: float, squash: float = 0.78) -> list[tuple[float, float]]:
    w = radius * 2.35
    h = radius * 1.75 * squash
    left = cx - w * 0.5
    right = cx + w * 0.5
    top = cy - h * 0.5
    bottom = cy + h * 0.5

    return [
        (left + w * 0.04, bottom),
        (left + w * 0.04, top + h * 0.60),
        (left + w * 0.20, top + h * 0.73),
        (left + w * 0.30, top + h * 0.20),
        (left + w * 0.42, top + h * 0.48),
        (left + w * 0.50, top + h * 0.06),
        (left + w * 0.58, top + h * 0.48),
        (left + w * 0.70, top + h * 0.20),
        (left + w * 0.80, top + h * 0.73),
        (right - w * 0.04, top + h * 0.60),
        (right - w * 0.04, bottom),
    ]


def draw_crown_icon(draw: ImageDraw.ImageDraw, cx: float, cy: float, radius: float, alpha: int) -> None:
    points = crown_points(cx, cy, radius, 0.86)
    draw.polygon(points, fill=(250, 255, 226, alpha))
    draw.line(points + [points[0]], fill=(73, 219, 236, min(255, int(alpha * 1.18))), width=max(2, int(radius * 0.08)))

    dot_radius = max(2.0, radius * 0.055)
    for px, py in (points[3], points[5], points[7]):
        draw.ellipse(
            (px - dot_radius, py - dot_radius, px + dot_radius, py + dot_radius),
            fill=(255, 237, 111, min(255, int(alpha * 1.1))),
        )


def draw_crown_iris_frame(index: int) -> Image.Image:
    progress = index / float(FRAME_COUNT - 1)
    t = smoothstep(progress)
    image = scaled_canvas()
    if progress < 0.04:
        return downsample(image)
    if progress >= 0.96:
        return downsample(Image.new("RGBA", image.size, (6, 73, 92, 255)))

    cover_alpha = int(255 * smoothstep((progress - 0.04) / 0.18))
    cover = Image.new("RGBA", image.size, (6, 73, 92, cover_alpha))
    diagonal = math.hypot(WIDTH, HEIGHT) * SCALE
    cx = WIDTH * SCALE * 0.5
    cy = HEIGHT * SCALE * 0.5
    hole_radius = diagonal * (1.08 - t * 1.08)

    if hole_radius > 8.0 * SCALE:
        mask = Image.new("L", image.size, 0)
        mask_draw = ImageDraw.Draw(mask)
        points = crown_points(cx, cy, hole_radius * 0.60, 0.88)
        mask_draw.polygon(points, fill=255)
        alpha = cover.getchannel("A")
        alpha = Image.composite(Image.new("L", image.size, 0), alpha, mask)
        cover.putalpha(alpha)

        line = Image.new("RGBA", image.size, (0, 0, 0, 0))
        line_draw = ImageDraw.Draw(line, "RGBA")
        if hole_radius < diagonal * 0.74:
            edge = crown_points(cx, cy, hole_radius * 0.60, 0.88)
            edge_width = max(3, int((3.5 + 5.5 * t) * SCALE))
            line_draw.line(edge + [edge[0]], fill=(75, 225, 242, int(245 * t)), width=edge_width, joint="curve")
            inner = crown_points(cx, cy, hole_radius * 0.55, 0.88)
            line_draw.line(inner + [inner[0]], fill=(221, 255, 234, int(140 * t)), width=max(1, int(2 * SCALE)), joint="curve")
        cover = Image.alpha_composite(cover, line)
    else:
        draw = ImageDraw.Draw(cover, "RGBA")
        pulse = 1.0 + math.sin(progress * math.tau * 1.5) * 0.06
        draw_crown_icon(draw, cx, cy, 52.0 * SCALE * pulse, 230)

    return downsample(cover)


def write_sequence(name: str, draw_func) -> None:
    out_dir = FADE_ROOT / name
    out_dir.mkdir(parents=True, exist_ok=True)
    for index in range(FRAME_COUNT):
        frame = draw_func(index)
        frame.save(out_dir / f"frame_{index:02d}.png")


def main() -> None:
    write_sequence("slime_wipe", draw_slime_wipe_frame)
    write_sequence("crown_iris", draw_crown_iris_frame)
    print(f"generated {FRAME_COUNT * 2} fade frames in {FADE_ROOT}")


if __name__ == "__main__":
    main()
