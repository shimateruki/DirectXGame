"""チュートリアルの芝生と土台に使う繰り返し可能なPBR素材を生成する。"""

from __future__ import annotations

import json
import uuid
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
TEXTURE_DIR = ROOT / "Resources/texture/PBR/TutorialTerrain"
MODEL_DIR = ROOT / "Resources/3DModel/Stages/tutorial_terrain"
SIZE = 512
TILE_METRES = 4.0


def periodic_noise(cells: int, rng: np.random.Generator) -> np.ndarray:
    """端がつながる格子ノイズを画像サイズへ補間する。"""
    lattice = rng.random((cells, cells), dtype=np.float32)
    coordinates = np.arange(SIZE, dtype=np.float32) * cells / SIZE
    index = np.floor(coordinates).astype(np.int32)
    fraction = coordinates - index
    fade = fraction * fraction * (3.0 - 2.0 * fraction)
    x0, y0 = np.meshgrid(index, index)
    tx, ty = np.meshgrid(fade, fade)
    a = lattice[y0 % cells, x0 % cells]
    b = lattice[y0 % cells, (x0 + 1) % cells]
    c = lattice[(y0 + 1) % cells, x0 % cells]
    d = lattice[(y0 + 1) % cells, (x0 + 1) % cells]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def wrapped_marks(rng: np.random.Generator, count: int, kind: str) -> np.ndarray:
    """短い草筋または土粒を端で折り返して描く。"""
    canvas = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(canvas)
    for _ in range(count):
        x = int(rng.integers(SIZE))
        y = int(rng.integers(SIZE))
        if kind == "grass":
            length = int(rng.integers(2, 8))
            lean = int(rng.integers(-3, 4))
            mark = (x, y, x + lean, y - length)
            shade = int(rng.integers(65, 155))
        else:
            radius = int(rng.integers(1, 4))
            mark = (x - radius, y - radius // 2, x + radius, y + radius // 2 + 1)
            shade = int(rng.integers(50, 140))
        for offset_y in (-SIZE, 0, SIZE):
            for offset_x in (-SIZE, 0, SIZE):
                shifted = (
                    mark[0] + offset_x, mark[1] + offset_y,
                    mark[2] + offset_x, mark[3] + offset_y,
                )
                if kind == "grass":
                    draw.line(shifted, fill=shade, width=1)
                else:
                    draw.ellipse(shifted, fill=shade)
    return np.asarray(canvas, dtype=np.float32) / 255.0


def save_texture(path: Path, pixels: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.clip(pixels, 0, 255).astype(np.uint8), "RGB").save(path, optimize=True)
    meta_path = path.with_name(path.name + ".meta")
    if not meta_path.exists():
        meta = {
            "assetType": "Texture",
            "guid": uuid.uuid4().hex,
            "importSettings": {"colorSpace": "Auto", "generateMipmaps": True},
            "importer": "TextureImporter",
            "source": path.relative_to(ROOT).as_posix(),
            "version": 1,
        }
        meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def build_texture_set(name: str, seed: int) -> None:
    rng = np.random.default_rng(seed)
    broad = periodic_noise(5, rng)
    clumps = periodic_noise(17, rng)
    grains = periodic_noise(72, rng)
    fine = periodic_noise(192, rng)
    if name == "grass":
        blades = wrapped_marks(rng, 4200, "grass")
        variation = (broad - 0.5) * 0.65 + (clumps - 0.5) * 0.44 + (grains - 0.5) * 0.17
        height = 0.48 + variation * 0.24 + (fine - 0.5) * 0.045 + blades * 0.075
        base = np.array([129.0, 153.0, 91.0], dtype=np.float32)
        color = base + variation[..., None] * np.array([52.0, 60.0, 28.0])
        color += blades[..., None] * np.array([10.0, 17.0, 3.0])
        color += (fine - 0.5)[..., None] * np.array([8.0, 9.0, 4.0])
        roughness = 0.86 + (grains - 0.5) * 0.09 - blades * 0.025
    else:
        pebbles = wrapped_marks(rng, 1100, "soil")
        variation = (broad - 0.5) * 0.74 + (clumps - 0.5) * 0.42 + (grains - 0.5) * 0.19
        height = 0.48 + variation * 0.23 + (fine - 0.5) * 0.06 + pebbles * 0.065
        base = np.array([172.0, 145.0, 111.0], dtype=np.float32)
        color = base + variation[..., None] * np.array([54.0, 47.0, 37.0])
        color += pebbles[..., None] * np.array([14.0, 13.0, 12.0])
        color += (fine - 0.5)[..., None] * np.array([11.0, 9.0, 8.0])
        roughness = 0.91 + (grains - 0.5) * 0.07 - pebbles * 0.015

    # 全チャンネルを同じ高さから作り、色・凹凸・陰影の位置を一致させる。
    slope_x = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 1.8
    slope_y = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 1.8
    normal = np.stack((-slope_x, slope_y, np.ones_like(height)), axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    normal = (normal * 0.5 + 0.5) * 255.0

    occlusion = np.clip(0.85 + (height - 0.5) * 0.33, 0.73, 1.0)
    orm = np.stack((occlusion, np.clip(roughness, 0.72, 0.98), np.zeros_like(height)), axis=-1) * 255.0
    save_texture(TEXTURE_DIR / f"tutorial_{name}_albedo.png", color)
    save_texture(TEXTURE_DIR / f"tutorial_{name}_normal.png", normal)
    save_texture(TEXTURE_DIR / f"tutorial_{name}_orm.png", orm)


def build_cube(name: str, width: float, height: float, depth: float) -> None:
    """既存のblockと同じ外形に、面ごとの実寸に合うUVを割り当てる。"""
    faces = [
        ("top", [(-1, 1, -1), (-1, 1, 1), (1, 1, 1), (1, 1, -1)], (0, 1, 0), (width, depth)),
        ("bottom", [(-1, -1, 1), (-1, -1, -1), (1, -1, -1), (1, -1, 1)], (0, -1, 0), (width, depth)),
        ("front", [(1, -1, 1), (1, 1, 1), (-1, 1, 1), (-1, -1, 1)], (0, 0, 1), (width, height)),
        ("back", [(-1, -1, -1), (-1, 1, -1), (1, 1, -1), (1, -1, -1)], (0, 0, -1), (width, height)),
        ("left", [(-1, -1, 1), (-1, 1, 1), (-1, 1, -1), (-1, -1, -1)], (-1, 0, 0), (depth, height)),
        ("right", [(1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1)], (1, 0, 0), (depth, height)),
    ]
    lines = [f"# Tutorial {name} terrain; unit cube with world-density UVs", "o TutorialTerrain"]
    for _, points, _, _ in faces:
        lines.extend(f"v {x} {y} {z}" for x, y, z in points)
    for _, _, _, (horizontal, vertical) in faces:
        u = horizontal / TILE_METRES
        v = vertical / TILE_METRES
        lines.extend((f"vt 0 0", f"vt 0 {v:.6f}", f"vt {u:.6f} {v:.6f}", f"vt {u:.6f} 0"))
    for _, _, normal, _ in faces:
        lines.append("vn {} {} {}".format(*normal))
    for face_index in range(len(faces)):
        base = face_index * 4 + 1
        normal_index = face_index + 1
        lines.append("f " + " ".join(f"{index}/{index}/{normal_index}" for index in (base, base + 1, base + 2)))
        lines.append("f " + " ".join(f"{index}/{index}/{normal_index}" for index in (base, base + 2, base + 3)))
    path = MODEL_DIR / f"tutorial_{name}.obj"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    meta_path = path.with_name(path.name + ".meta")
    if not meta_path.exists():
        meta = {
            "assetType": "Model",
            "guid": uuid.uuid4().hex,
            "importSettings": {"generateTangents": True, "scale": 1.0},
            "importer": "ModelImporter",
            "source": path.relative_to(ROOT).as_posix(),
            "version": 1,
        }
        meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    build_texture_set("grass", 1823)
    build_texture_set("soil", 2819)
    build_cube("grass", 108.0, 0.44, 108.0)
    build_cube("soil", 116.0, 4.0, 116.0)
