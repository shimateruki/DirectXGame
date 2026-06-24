from pathlib import Path
import math
import random

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "Resources" / "sprite" / "particle"


def save_layered(name: str, layers):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    image = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    for layer in layers:
        image = Image.alpha_composite(image, layer)
    image.save(OUT_DIR / name)


def draw_glow_line(points, widths, colors):
    layers = []
    for width, color, blur in zip(widths, colors, [14, 7, 1, 0]):
        layer = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        draw.line(points, fill=color, width=width, joint="curve")
        if blur > 0:
            layer = layer.filter(ImageFilter.GaussianBlur(blur))
        layers.append(layer)
    return layers


def generate_bolt():
    points = [
        (129, 18),
        (102, 83),
        (125, 78),
        (85, 162),
        (126, 148),
        (107, 238),
        (171, 123),
        (136, 134),
        (166, 53),
        (137, 63),
    ]
    widths = [42, 28, 13, 5]
    colors = [
        (51, 225, 255, 80),
        (70, 180, 255, 110),
        (255, 221, 49, 235),
        (255, 255, 226, 255),
    ]
    layers = draw_glow_line(points, widths, colors)

    tip = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    draw = ImageDraw.Draw(tip)
    for x, y in [(129, 18), (107, 238), (85, 162)]:
        draw.ellipse((x - 5, y - 5, x + 5, y + 5), fill=(255, 255, 235, 220))
    layers.append(tip.filter(ImageFilter.GaussianBlur(1.5)))
    save_layered("electric_bolt.png", layers)


def arc_points(start_deg, end_deg, radius, center=(128, 128), jitter=7, count=16):
    points = []
    for i in range(count):
        t = i / (count - 1)
        angle = math.radians(start_deg + (end_deg - start_deg) * t)
        wave = math.sin(t * math.pi * 5.0) * jitter
        r = radius + wave
        points.append((center[0] + math.cos(angle) * r, center[1] + math.sin(angle) * r))
    return points


def generate_arc():
    random.seed(28)
    all_layers = []
    arcs = [
        arc_points(-145, 68, 92, jitter=8),
        arc_points(38, 212, 58, jitter=5, count=12),
    ]
    for points in arcs:
        all_layers.extend(draw_glow_line(
            points,
            [34, 20, 9, 4],
            [
                (45, 220, 255, 58),
                (72, 180, 255, 92),
                (255, 223, 56, 230),
                (255, 255, 230, 250),
            ],
        ))
    spark = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    draw = ImageDraw.Draw(spark)
    for x, y in [(58, 69), (202, 144), (88, 204), (181, 69)]:
        draw.ellipse((x - 4, y - 4, x + 4, y + 4), fill=(255, 250, 154, 220))
    all_layers.append(spark.filter(ImageFilter.GaussianBlur(2)))
    save_layered("electric_arc.png", all_layers)


def generate_pulse():
    layers = []
    for radius, alpha, blur, color in [
        (92, 60, 16, (40, 210, 255)),
        (66, 90, 10, (255, 221, 48)),
        (41, 150, 6, (255, 255, 230)),
    ]:
        layer = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        draw.ellipse((128 - radius, 128 - radius, 128 + radius, 128 + radius), fill=(*color, alpha))
        layers.append(layer.filter(ImageFilter.GaussianBlur(blur)))

    ring = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    draw = ImageDraw.Draw(ring)
    draw.ellipse((52, 52, 204, 204), outline=(255, 228, 73, 220), width=8)
    draw.ellipse((72, 72, 184, 184), outline=(80, 210, 255, 160), width=4)
    layers.append(ring.filter(ImageFilter.GaussianBlur(1.2)))
    save_layered("electric_pulse.png", layers)


if __name__ == "__main__":
    generate_bolt()
    generate_arc()
    generate_pulse()
    print("generated electric particle sprites")
