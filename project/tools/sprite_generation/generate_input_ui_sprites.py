from __future__ import annotations

import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "Resources" / "sprite" / "ui" / "input"
FONT_CANDIDATES = [
    ROOT / "Resources" / "font" / "MPLUS1p-Medium.ttf",
    ROOT / "Resources" / "sprite" / "meiryo.ttc",
]


def font_path() -> Path | None:
    for path in FONT_CANDIDATES:
        if path.exists():
            return path
    return None


FONT_PATH = font_path()


def load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    if FONT_PATH is None:
        return ImageFont.load_default()
    return ImageFont.truetype(str(FONT_PATH), size)


def text_size(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.ImageFont) -> tuple[int, int]:
    bbox = draw.textbbox((0, 0), text, font=font)
    return bbox[2] - bbox[0], bbox[3] - bbox[1]


def fit_font(draw: ImageDraw.ImageDraw, text: str, max_width: int, max_height: int, start_size: int) -> ImageFont.ImageFont:
    for size in range(start_size, 11, -1):
        font = load_font(size)
        width, height = text_size(draw, text, font)
        if width <= max_width and height <= max_height:
            return font
    return load_font(11)


def draw_centered_text(
    draw: ImageDraw.ImageDraw,
    box: tuple[int, int, int, int],
    text: str,
    fill: tuple[int, int, int, int],
    font: ImageFont.ImageFont,
    stroke_fill: tuple[int, int, int, int] | None = None,
    stroke_width: int = 0,
) -> None:
    x0, y0, x1, y1 = box
    bbox = draw.textbbox((0, 0), text, font=font, stroke_width=stroke_width)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    x = x0 + (x1 - x0 - width) / 2 - bbox[0]
    y = y0 + (y1 - y0 - height) / 2 - bbox[1]
    draw.text((x, y), text, font=font, fill=fill, stroke_width=stroke_width, stroke_fill=stroke_fill)


def new_canvas(width: int, height: int) -> Image.Image:
    return Image.new("RGBA", (width, height), (0, 0, 0, 0))


def draw_soft_shadow(
    image: Image.Image,
    rect: tuple[int, int, int, int],
    radius: int,
    alpha: int = 90,
    blur: int = 5,
    offset: tuple[int, int] = (0, 5),
) -> None:
    shadow = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(shadow)
    x0, y0, x1, y1 = rect
    ox, oy = offset
    draw.rounded_rectangle((x0 + ox, y0 + oy, x1 + ox, y1 + oy), radius=radius, fill=(0, 0, 0, alpha))
    shadow = shadow.filter(ImageFilter.GaussianBlur(blur))
    image.alpha_composite(shadow)


def draw_key_base(
    image: Image.Image,
    rect: tuple[int, int, int, int],
    *,
    selected: bool = False,
    fill: tuple[int, int, int, int] = (234, 247, 255, 255),
    outline: tuple[int, int, int, int] = (45, 83, 112, 255),
    radius: int = 15,
) -> None:
    draw_soft_shadow(image, rect, radius, alpha=75, blur=6, offset=(0, 5))
    draw = ImageDraw.Draw(image)
    x0, y0, x1, y1 = rect
    edge = (128, 226, 255, 255) if selected else outline
    draw.rounded_rectangle(rect, radius=radius, fill=fill, outline=edge, width=4)
    draw.rounded_rectangle((x0 + 6, y0 + 6, x1 - 6, y0 + 22), radius=radius // 2, fill=(255, 255, 255, 70))
    draw.line((x0 + 13, y1 - 13, x1 - 13, y1 - 13), fill=(90, 128, 154, 90), width=2)


def save_image(image: Image.Image, relative_path: str, manifest: list[dict[str, object]], group: str, name: str) -> None:
    path = OUT_DIR / relative_path
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)
    manifest.append(
        {
            "group": group,
            "name": name,
            "path": str(Path("Resources") / "sprite" / "ui" / "input" / relative_path).replace("\\", "/"),
            "width": image.width,
            "height": image.height,
        }
    )


def make_key(label: str, width: int = 96, height: int = 96, selected: bool = False) -> Image.Image:
    image = new_canvas(width, height)
    rect = (8, 8, width - 8, height - 11)
    draw_key_base(image, rect, selected=selected)
    draw = ImageDraw.Draw(image)
    font = fit_font(draw, label, width - 28, height - 28, 46)
    draw_centered_text(draw, rect, label, (26, 55, 77, 255), font, stroke_fill=(255, 255, 255, 180), stroke_width=1)
    return image


def arrow_points(direction: str, center: tuple[float, float], size: float) -> list[tuple[float, float]]:
    cx, cy = center
    if direction == "up":
        return [(cx, cy - size), (cx - size * 0.75, cy + size * 0.45), (cx + size * 0.75, cy + size * 0.45)]
    if direction == "down":
        return [(cx, cy + size), (cx - size * 0.75, cy - size * 0.45), (cx + size * 0.75, cy - size * 0.45)]
    if direction == "left":
        return [(cx - size, cy), (cx + size * 0.45, cy - size * 0.75), (cx + size * 0.45, cy + size * 0.75)]
    return [(cx + size, cy), (cx - size * 0.45, cy - size * 0.75), (cx - size * 0.45, cy + size * 0.75)]


def make_arrow_key(direction: str) -> Image.Image:
    image = new_canvas(96, 96)
    rect = (8, 8, 88, 85)
    draw_key_base(image, rect)
    draw = ImageDraw.Draw(image)
    draw.polygon(arrow_points(direction, (48, 47), 23), fill=(27, 80, 112, 255))
    return image


def draw_small_key(image: Image.Image, rect: tuple[int, int, int, int], label: str) -> None:
    draw_key_base(image, rect, radius=11, fill=(237, 250, 255, 255))
    draw = ImageDraw.Draw(image)
    font = fit_font(draw, label, rect[2] - rect[0] - 16, rect[3] - rect[1] - 16, 31)
    draw_centered_text(draw, rect, label, (24, 54, 76, 255), font, stroke_fill=(255, 255, 255, 160), stroke_width=1)


def make_wasd() -> Image.Image:
    image = new_canvas(238, 178)
    draw_small_key(image, (88, 10, 150, 72), "W")
    draw_small_key(image, (20, 84, 82, 146), "A")
    draw_small_key(image, (88, 84, 150, 146), "S")
    draw_small_key(image, (156, 84, 218, 146), "D")
    return image


def make_arrows() -> Image.Image:
    image = new_canvas(238, 178)
    draw_small_key(image, (88, 10, 150, 72), "")
    draw_small_key(image, (20, 84, 82, 146), "")
    draw_small_key(image, (88, 84, 150, 146), "")
    draw_small_key(image, (156, 84, 218, 146), "")
    draw = ImageDraw.Draw(image)
    for direction, center in [("up", (119, 42)), ("left", (51, 115)), ("down", (119, 115)), ("right", (187, 115))]:
        draw.polygon(arrow_points(direction, center, 17), fill=(27, 80, 112, 255))
    return image


def make_mouse(kind: str) -> Image.Image:
    image = new_canvas(138, 154)
    draw = ImageDraw.Draw(image)
    body = (36, 13, 102, 121)
    draw_soft_shadow(image, body, 32, alpha=85, blur=6, offset=(0, 5))
    draw.rounded_rectangle(body, radius=32, fill=(235, 248, 255, 255), outline=(39, 77, 107, 255), width=4)
    draw.line((69, 16, 69, 60), fill=(39, 77, 107, 210), width=3)
    draw.line((39, 60, 99, 60), fill=(39, 77, 107, 210), width=3)
    wheel = (61, 37, 77, 61)
    draw.rounded_rectangle(wheel, radius=7, fill=(66, 216, 242, 255), outline=(255, 255, 255, 210), width=2)

    highlight = (255, 224, 96, 210)
    if kind == "left":
        draw.pieslice((38, 15, 70, 62), 180, 360, fill=highlight)
        label = "L"
    elif kind == "right":
        draw.pieslice((68, 15, 100, 62), 180, 360, fill=highlight)
        label = "R"
    elif kind == "middle":
        draw.rounded_rectangle(wheel, radius=7, fill=highlight, outline=(255, 255, 255, 210), width=2)
        label = "M"
    elif kind == "wheel":
        draw.line((69, 25, 69, 7), fill=(255, 224, 96, 235), width=4)
        draw.polygon(arrow_points("up", (69, 7), 8), fill=(255, 224, 96, 235))
        draw.line((69, 73, 69, 93), fill=(255, 224, 96, 235), width=4)
        draw.polygon(arrow_points("down", (69, 94), 8), fill=(255, 224, 96, 235))
        label = "Wheel"
    else:
        for direction, center in [("up", (69, 4)), ("down", (69, 135)), ("left", (19, 70)), ("right", (119, 70))]:
            draw.polygon(arrow_points(direction, center, 10), fill=(255, 224, 96, 230))
        label = "Move"

    font = fit_font(draw, label, 116, 26, 22)
    draw_centered_text(draw, (0, 123, 138, 151), label, (235, 248, 255, 255), font, stroke_fill=(23, 52, 73, 255), stroke_width=3)
    return image


def make_controller_face(label: str, color: tuple[int, int, int, int]) -> Image.Image:
    image = new_canvas(96, 96)
    draw = ImageDraw.Draw(image)
    circle = (10, 10, 86, 86)
    draw_soft_shadow(image, circle, 38, alpha=80, blur=6, offset=(0, 5))
    draw.ellipse(circle, fill=color, outline=(32, 52, 70, 255), width=4)
    draw.ellipse((18, 16, 78, 42), fill=(255, 255, 255, 58))
    font = fit_font(draw, label, 48, 48, 45)
    text_fill = (255, 255, 255, 255) if label != "Y" else (53, 50, 30, 255)
    draw_centered_text(draw, circle, label, text_fill, font, stroke_fill=(0, 0, 0, 120), stroke_width=2)
    return image


def make_pill(label: str, width: int = 140, height: int = 78, fill=(226, 237, 246, 255)) -> Image.Image:
    image = new_canvas(width, height)
    draw = ImageDraw.Draw(image)
    rect = (8, 10, width - 8, height - 12)
    draw_soft_shadow(image, rect, 20, alpha=75, blur=6, offset=(0, 5))
    draw.rounded_rectangle(rect, radius=20, fill=fill, outline=(39, 77, 107, 255), width=4)
    draw.rounded_rectangle((17, 18, width - 17, 32), radius=7, fill=(255, 255, 255, 62))
    font = fit_font(draw, label, width - 30, height - 28, 32)
    draw_centered_text(draw, rect, label, (27, 57, 78, 255), font, stroke_fill=(255, 255, 255, 170), stroke_width=1)
    return image


def make_stick(label: str, pressed: bool = False) -> Image.Image:
    image = new_canvas(112, 112)
    draw = ImageDraw.Draw(image)
    outer = (12, 12, 100, 100)
    inner = (30, 29, 82, 81)
    draw_soft_shadow(image, outer, 44, alpha=75, blur=6, offset=(0, 5))
    draw.ellipse(outer, fill=(207, 222, 232, 255), outline=(39, 77, 107, 255), width=4)
    draw.ellipse(inner, fill=(74, 101, 119, 255), outline=(22, 43, 58, 255), width=3)
    draw.ellipse((38, 34, 73, 51), fill=(255, 255, 255, 45))
    if pressed:
        draw.ellipse((25, 24, 87, 86), outline=(255, 224, 96, 245), width=5)
        label = label + "3"
    font = fit_font(draw, label, 42, 36, 27)
    draw_centered_text(draw, (28, 78, 84, 107), label, (237, 250, 255, 255), font, stroke_fill=(18, 35, 48, 255), stroke_width=2)
    return image


def make_dpad(direction: str | None = None) -> Image.Image:
    image = new_canvas(128, 128)
    draw = ImageDraw.Draw(image)
    draw_soft_shadow(image, (20, 20, 108, 108), 22, alpha=70, blur=6, offset=(0, 5))
    fill = (214, 229, 238, 255)
    active = (255, 224, 96, 255)
    outline = (39, 77, 107, 255)
    pieces = {
        "up": (48, 15, 80, 57),
        "down": (48, 71, 80, 113),
        "left": (15, 48, 57, 80),
        "right": (71, 48, 113, 80),
        "center": (48, 48, 80, 80),
    }
    for name, rect in pieces.items():
        color = active if name == direction else fill
        draw.rounded_rectangle(rect, radius=9, fill=color, outline=outline, width=3)
    return image


def make_contact_sheet(manifest: list[dict[str, object]]) -> Image.Image:
    thumbs = []
    for item in manifest:
        path = ROOT / item["path"]
        if item["group"] == "preview":
            continue
        image = Image.open(path).convert("RGBA")
        image.thumbnail((72, 72), Image.Resampling.LANCZOS)
        thumbs.append((item["name"], image.copy()))

    cell_w, cell_h = 132, 112
    cols = 8
    rows = math.ceil(len(thumbs) / cols)
    sheet = Image.new("RGBA", (cols * cell_w, max(1, rows) * cell_h), (26, 44, 58, 255))
    draw = ImageDraw.Draw(sheet)
    font = load_font(13)
    for index, (name, thumb) in enumerate(thumbs):
        col = index % cols
        row = index // cols
        x = col * cell_w
        y = row * cell_h
        draw.rounded_rectangle((x + 7, y + 7, x + cell_w - 7, y + cell_h - 7), radius=9, fill=(42, 65, 82, 255), outline=(95, 142, 171, 210), width=1)
        sheet.alpha_composite(thumb, (x + (cell_w - thumb.width) // 2, y + 10))
        short_name = name if len(name) <= 17 else name[:16] + "."
        draw_centered_text(draw, (x + 6, y + 82, x + cell_w - 6, y + 106), short_name, (238, 249, 255, 255), font)
    return sheet


def main() -> None:
    manifest: list[dict[str, object]] = []

    letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    for letter in letters:
        save_image(make_key(letter), f"keyboard/key_{letter.lower()}.png", manifest, "keyboard", f"key_{letter.lower()}")
    for number in "0123456789":
        save_image(make_key(number), f"keyboard/key_{number}.png", manifest, "keyboard", f"key_{number}")

    special_keys = {
        "space": ("Space", 236, 92),
        "enter": ("Enter", 154, 92),
        "tab": ("Tab", 136, 92),
        "esc": ("Esc", 122, 92),
        "shift": ("Shift", 162, 92),
        "ctrl": ("Ctrl", 136, 92),
        "alt": ("Alt", 122, 92),
        "backspace": ("Backspace", 218, 92),
        "delete": ("Delete", 164, 92),
        "minus": ("-", 96, 96),
        "plus": ("+", 96, 96),
        "slash": ("/", 96, 96),
    }
    for name, (label, width, height) in special_keys.items():
        save_image(make_key(label, width, height), f"keyboard/key_{name}.png", manifest, "keyboard", f"key_{name}")

    for index in range(1, 13):
        save_image(make_key(f"F{index}", 104, 80), f"keyboard/key_f{index}.png", manifest, "keyboard", f"key_f{index}")

    for direction in ["up", "down", "left", "right"]:
        save_image(make_arrow_key(direction), f"keyboard/key_arrow_{direction}.png", manifest, "keyboard", f"key_arrow_{direction}")

    save_image(make_wasd(), "combo/combo_wasd.png", manifest, "combo", "combo_wasd")
    save_image(make_arrows(), "combo/combo_arrows.png", manifest, "combo", "combo_arrows")

    for kind in ["left", "right", "middle", "wheel", "move"]:
        save_image(make_mouse(kind), f"mouse/mouse_{kind}.png", manifest, "mouse", f"mouse_{kind}")

    face_buttons = {
        "a": (68, 198, 88, 255),
        "b": (226, 76, 70, 255),
        "x": (72, 145, 232, 255),
        "y": (246, 220, 78, 255),
    }
    for label, color in face_buttons.items():
        save_image(make_controller_face(label.upper(), color), f"controller/pad_{label}.png", manifest, "controller", f"pad_{label}")

    for name in ["lb", "rb", "lt", "rt"]:
        label = name.upper()
        fill = (210, 225, 236, 255) if name.startswith("l") else (226, 236, 244, 255)
        save_image(make_pill(label, 136, 78, fill), f"controller/pad_{name}.png", manifest, "controller", f"pad_{name}")

    for name, label in [("start", "Start"), ("back", "Back"), ("menu", "Menu"), ("view", "View")]:
        save_image(make_pill(label, 150, 76, (225, 237, 245, 255)), f"controller/pad_{name}.png", manifest, "controller", f"pad_{name}")

    save_image(make_stick("L"), "controller/pad_l_stick.png", manifest, "controller", "pad_l_stick")
    save_image(make_stick("R"), "controller/pad_r_stick.png", manifest, "controller", "pad_r_stick")
    save_image(make_stick("L", pressed=True), "controller/pad_l3.png", manifest, "controller", "pad_l3")
    save_image(make_stick("R", pressed=True), "controller/pad_r3.png", manifest, "controller", "pad_r3")

    save_image(make_dpad(), "controller/pad_dpad.png", manifest, "controller", "pad_dpad")
    for direction in ["up", "down", "left", "right"]:
        save_image(make_dpad(direction), f"controller/pad_dpad_{direction}.png", manifest, "controller", f"pad_dpad_{direction}")

    preview = make_contact_sheet(manifest)
    save_image(preview, "input_ui_contact_sheet.png", manifest, "preview", "input_ui_contact_sheet")

    manifest_path = OUT_DIR / "input_ui_manifest.json"
    manifest_path.write_text(json.dumps({"sprites": manifest}, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"Generated {len(manifest) - 1} input UI sprites.")
    print(f"Output: {OUT_DIR}")


if __name__ == "__main__":
    main()
