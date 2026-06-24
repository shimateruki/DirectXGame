from __future__ import annotations

import math
import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
MODEL_DIR = ROOT / "Resources" / "3DModel" / "Stages" / "bomb_break_block"
PBR_DIR = ROOT / "Resources" / "texture" / "PBR"


def ensure_dirs() -> None:
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    for folder in ("Albedo", "Normal", "ARM"):
        (PBR_DIR / folder).mkdir(parents=True, exist_ok=True)


def rounded_box(draw: ImageDraw.ImageDraw, xy: tuple[int, int, int, int], radius: int, fill, outline=None, width: int = 1) -> None:
    draw.rounded_rectangle(xy, radius=radius, fill=fill, outline=outline, width=width)


def draw_crack(draw: ImageDraw.ImageDraw, points: list[tuple[int, int]], shadow_width: int, line_width: int) -> None:
    if len(points) < 2:
        return
    draw.line(points, fill=(68, 72, 78, 220), width=shadow_width, joint="curve")
    draw.line([(x - 1, y - 1) for x, y in points], fill=(244, 247, 248, 245), width=line_width, joint="curve")
    draw.line([(x + 1, y + 1) for x, y in points], fill=(180, 195, 205, 180), width=max(1, line_width // 2), joint="curve")


def generate_texture_set() -> None:
    random.seed(173)
    size = 512
    albedo = Image.new("RGBA", (size, size), (112, 120, 130, 255))
    height = Image.new("L", (size, size), 110)
    ao = Image.new("L", (size, size), 235)

    d = ImageDraw.Draw(albedo, "RGBA")
    hd = ImageDraw.Draw(height)
    aod = ImageDraw.Draw(ao)

    tile = size // 3
    gap = 12
    palette = [
        (126, 136, 148, 255),
        (116, 126, 139, 255),
        (139, 146, 154, 255),
        (104, 113, 126, 255),
        (131, 137, 144, 255),
    ]

    d.rectangle((0, 0, size, size), fill=(78, 84, 94, 255))
    for y in range(3):
        for x in range(3):
            x0 = x * tile + gap
            y0 = y * tile + gap
            x1 = (x + 1) * tile - gap
            y1 = (y + 1) * tile - gap
            color = random.choice(palette)
            radius = 28 + random.randint(-4, 8)
            rounded_box(d, (x0, y0, x1, y1), radius, color, (64, 69, 78, 255), 5)
            rounded_box(hd, (x0, y0, x1, y1), radius, 202, 92, 5)
            rounded_box(aod, (x0, y0, x1, y1), radius, 250, 120, 4)

            highlight = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            hl = ImageDraw.Draw(highlight, "RGBA")
            rounded_box(hl, (x0 + 7, y0 + 8, x1 - 10, y0 + 38), max(8, radius // 2), (255, 255, 255, 38))
            albedo.alpha_composite(highlight)

            shade = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            sd = ImageDraw.Draw(shade, "RGBA")
            rounded_box(sd, (x0 + 8, y1 - 36, x1 - 8, y1 - 6), max(8, radius // 2), (30, 34, 42, 42))
            albedo.alpha_composite(shade)

    # Large readable cracks, kept chunky for a toy-like action game look.
    main_cracks = [
        [(68, 116), (130, 158), (190, 138), (255, 204), (324, 188), (396, 254), (470, 236)],
        [(218, 28), (234, 92), (211, 160), (244, 236), (230, 312), (258, 392), (244, 482)],
        [(56, 370), (126, 330), (190, 350), (266, 314), (334, 344), (430, 310)],
    ]
    for crack in main_cracks:
        draw_crack(d, crack, 12, 6)
        hd.line(crack, fill=35, width=13, joint="curve")
        aod.line(crack, fill=80, width=14, joint="curve")

    branch_cracks = [
        [(190, 138), (160, 90), (118, 70)],
        [(255, 204), (285, 245), (330, 265)],
        [(396, 254), (420, 300), (462, 332)],
        [(230, 312), (194, 284), (156, 286)],
        [(266, 314), (292, 278), (292, 230)],
        [(334, 344), (366, 386), (398, 390)],
    ]
    for crack in branch_cracks:
        draw_crack(d, crack, 8, 4)
        hd.line(crack, fill=48, width=9, joint="curve")
        aod.line(crack, fill=100, width=10, joint="curve")

    # Soft grain so it does not look like a flat UI icon.
    noise = np.random.default_rng(11).normal(0, 4.2, (size, size, 1))
    arr = np.array(albedo).astype(np.int16)
    arr[:, :, :3] = np.clip(arr[:, :, :3] + noise, 0, 255)
    albedo = Image.fromarray(arr.astype(np.uint8), "RGBA").filter(ImageFilter.UnsharpMask(radius=1.0, percent=75, threshold=4))

    h = np.array(height).astype(np.float32) / 255.0
    gx = np.gradient(h, axis=1)
    gy = np.gradient(h, axis=0)
    strength = 4.0
    nx = -gx * strength
    ny = gy * strength
    nz = np.ones_like(h)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    normal = np.stack(((nx / length) * 0.5 + 0.5, (ny / length) * 0.5 + 0.5, (nz / length) * 0.5 + 0.5), axis=2)
    normal_img = Image.fromarray(np.clip(normal * 255.0, 0, 255).astype(np.uint8), "RGB")

    orm = Image.new("RGB", (size, size), (235, 215, 0))
    orm_arr = np.array(orm)
    orm_arr[:, :, 0] = np.array(ao)
    orm_arr[:, :, 1] = 216
    orm_arr[:, :, 2] = 0
    orm_img = Image.fromarray(orm_arr, "RGB")

    outputs = {
        MODEL_DIR / "bomb_break_block_albedo.png": albedo.convert("RGB"),
        MODEL_DIR / "bomb_break_block_normal.png": normal_img,
        MODEL_DIR / "bomb_break_block_orm.png": orm_img,
        PBR_DIR / "Albedo" / "bomb_break_block_albedo.png": albedo.convert("RGB"),
        PBR_DIR / "Normal" / "bomb_break_block_normal.png": normal_img,
        PBR_DIR / "ARM" / "bomb_break_block_orm.png": orm_img,
    }
    for path, image in outputs.items():
        image.save(path)


def write_model() -> None:
    obj_path = MODEL_DIR / "bomb_break_block.obj"
    mtl_path = MODEL_DIR / "bomb_break_block.mtl"

    # Six independent faces keep UVs predictable for the stone pattern.
    faces = [
        ((0, 0, 1), [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, 0.5)]),
        ((0, 0, -1), [(0.5, -0.5, -0.5), (-0.5, -0.5, -0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5)]),
        ((1, 0, 0), [(0.5, -0.5, 0.5), (0.5, -0.5, -0.5), (0.5, 0.5, -0.5), (0.5, 0.5, 0.5)]),
        ((-1, 0, 0), [(-0.5, -0.5, -0.5), (-0.5, -0.5, 0.5), (-0.5, 0.5, 0.5), (-0.5, 0.5, -0.5)]),
        ((0, 1, 0), [(-0.5, 0.5, 0.5), (0.5, 0.5, 0.5), (0.5, 0.5, -0.5), (-0.5, 0.5, -0.5)]),
        ((0, -1, 0), [(-0.5, -0.5, -0.5), (0.5, -0.5, -0.5), (0.5, -0.5, 0.5), (-0.5, -0.5, 0.5)]),
    ]
    uvs = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]

    lines: list[str] = ["mtllib bomb_break_block.mtl", "o BombBreakBlock"]
    for _, verts in faces:
        for v in verts:
            lines.append(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}")
    for uv in uvs * len(faces):
        lines.append(f"vt {uv[0]:.6f} {uv[1]:.6f}")
    for normal, _ in faces:
        lines.append(f"vn {normal[0]:.6f} {normal[1]:.6f} {normal[2]:.6f}")

    lines.append("usemtl cracked_stone")
    vertex_base = 1
    uv_base = 1
    for face_index in range(len(faces)):
        normal_index = face_index + 1
        a = vertex_base
        b = vertex_base + 1
        c = vertex_base + 2
        d = vertex_base + 3
        ta = uv_base
        tb = uv_base + 1
        tc = uv_base + 2
        td = uv_base + 3
        lines.append(f"f {a}/{ta}/{normal_index} {b}/{tb}/{normal_index} {c}/{tc}/{normal_index}")
        lines.append(f"f {a}/{ta}/{normal_index} {c}/{tc}/{normal_index} {d}/{td}/{normal_index}")
        vertex_base += 4
        uv_base += 4
    obj_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    mtl_path.write_text(
        "\n".join(
            [
                "newmtl cracked_stone",
                "Ka 0.58 0.60 0.64",
                "Kd 0.88 0.90 0.94",
                "Ks 0.08 0.08 0.08",
                "Ns 18",
                "map_Kd bomb_break_block_albedo.png",
                "",
            ]
        ),
        encoding="utf-8",
    )


def main() -> None:
    ensure_dirs()
    generate_texture_set()
    write_model()
    print(f"generated {MODEL_DIR}")


if __name__ == "__main__":
    main()
