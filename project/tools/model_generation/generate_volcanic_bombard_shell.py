"""大型迫撃砲の溶岩弾を、球面モデルとPBR用テクスチャから生成する。"""

from __future__ import annotations

import json
import math
from pathlib import Path
from uuid import uuid4

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Resources/3DModel/Gimmicks/volcanic_bombard_shell"
NAME = "volcanic_bombard_shell"
LONGITUDE = 64
LATITUDE = 32
TEXTURE_WIDTH = 1024
TEXTURE_HEIGHT = 512


def smoothstep(low: float, high: float, value: np.ndarray) -> np.ndarray:
    t = np.clip((value - low) / (high - low), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def surface_noise(x, y, z):
    # 球面上の3D座標だけを使い、UVの継ぎ目に模様の切れ目を作らない。
    return (
        0.49 * np.sin(8.7 * x + 4.1 * y - 2.8 * z)
        * np.sin(5.5 * z - 3.4 * x + 0.7 * y)
        + 0.30 * np.sin(17.0 * x - 9.4 * z + 3.2 * y)
        * np.cos(11.5 * y + 4.2 * z)
        + 0.21 * np.sin(31.5 * z + 12.0 * x - 7.3 * y)
        * np.cos(26.0 * y - 4.4 * x)
    )


def build_texture() -> None:
    u = (np.arange(TEXTURE_WIDTH, dtype=np.float32) + 0.5) / TEXTURE_WIDTH
    v = (np.arange(TEXTURE_HEIGHT, dtype=np.float32) + 0.5) / TEXTURE_HEIGHT
    longitude, latitude = np.meshgrid(u * math.tau, v * math.pi)
    x = np.sin(latitude) * np.cos(longitude)
    y = np.cos(latitude)
    z = np.sin(latitude) * np.sin(longitude)
    rough = surface_noise(x, y, z)

    # 歪ませた球面上のボロノイ境界を、岩殻の割れ目として使う。
    wx = x + 0.085 * rough
    wy = y + 0.063 * surface_noise(y, z, x)
    wz = z + 0.085 * surface_noise(z, x, y)
    length = np.sqrt(wx * wx + wy * wy + wz * wz)
    wx, wy, wz = wx / length, wy / length, wz / length
    rng = np.random.default_rng(80241)
    seeds = rng.normal(size=(29, 3))
    seeds /= np.linalg.norm(seeds, axis=1)[:, None]
    best = np.full(x.shape, -2.0, dtype=np.float32)
    second = np.full(x.shape, -2.0, dtype=np.float32)
    for sx, sy, sz in seeds:
        dot = wx * sx + wy * sy + wz * sz
        second = np.maximum(second, np.minimum(best, dot))
        best = np.maximum(best, dot)
    cell_gap = best - second
    fissure_halo = 1.0 - smoothstep(0.016, 0.075, cell_gap)
    fissure_core = 1.0 - smoothstep(0.008, 0.032, cell_gap)

    # 大きさの違う溶岩窓を数個だけ作り、単なる格子模様に見えないようにする。
    hot = np.zeros(x.shape, dtype=np.float32)
    hot_core = np.zeros(x.shape, dtype=np.float32)
    for index, (sx, sy, sz) in enumerate(seeds[[0, 6, 10, 19, 25]]):
        dot = wx * sx + wy * sy + wz * sz
        radius = (0.18, 0.14, 0.23, 0.19, 0.17)[index]
        hot = np.maximum(hot, smoothstep(math.cos(radius + 0.11),
                                         math.cos(radius - 0.08), dot))
        hot_core = np.maximum(hot_core, smoothstep(math.cos(radius + 0.015),
                                                   math.cos(radius - 0.075), dot))
    hot *= np.clip(0.86 + rough * 0.38, 0.55, 1.0)
    glow = np.maximum(hot, fissure_halo * 0.85)
    core = np.maximum(hot_core * 0.9, fissure_core * 0.75)

    # 岩は暗い紫褐色、熱い縁は橙、中心は黄にする。暗い輪郭を残して球形を読ませる。
    rock = np.stack((35 + 16 * rough, 28 + 11 * rough, 43 + 20 * rough), axis=-1)
    burnt = np.array([121.0, 40.0, 27.0], dtype=np.float32)
    lava = np.array([246.0, 101.0, 10.0], dtype=np.float32)
    core_color = np.array([255.0, 202.0, 44.0], dtype=np.float32)
    color = rock * (1.0 - glow[..., None]) + burnt * glow[..., None]
    color = color * (1.0 - (glow * 0.68)[..., None]) + lava * (glow * 0.68)[..., None]
    color = color * (1.0 - core[..., None]) + core_color * core[..., None]
    Image.fromarray(np.clip(color, 0, 255).astype(np.uint8), "RGB").save(OUT / "basecolor.png")

    # 浮いた岩殻と熱い裂け目の高低差を、法線マップにも反映する。
    height = rough * 0.13 - glow * 0.25
    dv, du = np.gradient(height)
    normal_x = np.clip(0.5 - du * 1.2, 0.0, 1.0)
    normal_y = np.clip(0.5 - dv * 1.2, 0.0, 1.0)
    normal_z = np.sqrt(np.maximum(0.0, 1.0 - (normal_x * 2.0 - 1.0) ** 2
                                  - (normal_y * 2.0 - 1.0) ** 2))
    normal = np.stack((normal_x * 255.0, normal_y * 255.0,
                       (normal_z * 0.5 + 0.5) * 255.0), axis=-1)
    Image.fromarray(np.clip(normal, 0, 255).astype(np.uint8), "RGB").save(OUT / "normal.png")

    ambient_occlusion = np.clip(220 + rough * 20 - glow * 10, 0, 255)
    roughness = np.clip(212 - glow * 105 + rough * 18, 0, 255)
    metallic = np.zeros_like(ambient_occlusion)
    orm = np.stack((ambient_occlusion, roughness, metallic), axis=-1)
    Image.fromarray(orm.astype(np.uint8), "RGB").save(OUT / "orm.png")


def build_mesh() -> None:
    vertices = []
    texcoords = []
    normals = []
    faces = []

    def vertex(u: float, v: float):
        latitude = v * math.pi
        longitude = u * math.tau
        dx = math.sin(latitude) * math.cos(longitude)
        dy = math.cos(latitude)
        dz = math.sin(latitude) * math.sin(longitude)
        n = float(surface_noise(dx, dy, dz))
        radius = 1.0 + 0.055 * n
        vertices.append((dx * radius, dy * radius, dz * radius))
        texcoords.append((u, 1.0 - v))
        normals.append((dx, dy, dz))
        return len(vertices)

    north = vertex(0.5, 0.0)
    rings = []
    for j in range(1, LATITUDE):
        rings.append([vertex(i / LONGITUDE, j / LATITUDE)
                      for i in range(LONGITUDE + 1)])
    south = vertex(0.5, 1.0)
    for i in range(LONGITUDE):
        faces.append((north, rings[0][i + 1], rings[0][i]))
    for upper, lower in zip(rings, rings[1:]):
        for i in range(LONGITUDE):
            faces.append((upper[i], upper[i + 1], lower[i + 1]))
            faces.append((upper[i], lower[i + 1], lower[i]))
    for i in range(LONGITUDE):
        faces.append((rings[-1][i], rings[-1][i + 1], south))

    with (OUT / f"{NAME}.obj").open("w", encoding="ascii", newline="\n") as stream:
        stream.write(f"mtllib {NAME}.mtl\no {NAME}\n")
        for x, y, z in vertices:
            stream.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
        for u, v in texcoords:
            stream.write(f"vt {u:.6f} {v:.6f}\n")
        for x, y, z in normals:
            stream.write(f"vn {x:.6f} {y:.6f} {z:.6f}\n")
        stream.write("usemtl magma_shell\n")
        for face in faces:
            stream.write("f " + " ".join(f"{i}/{i}/{i}" for i in face) + "\n")
    print(f"{OUT / (NAME + '.obj')} triangles={len(faces)}")


MTL = """newmtl magma_shell
Ka 0.12 0.07 0.07
Kd 1 1 1
Ks 0.17 0.13 0.11
Ns 24
Pr 0.82
Pm 0
illum 2
map_Kd basecolor.png
norm normal.png
map_Bump normal.png
map_Pr orm.png
"""


def write_metadata(name: str, asset_type: str, importer: str, settings: dict) -> None:
    # 再生成時はGUIDを保持し、エディター側のアセット参照を壊さない。
    metadata_path = OUT / f"{name}.meta"
    if metadata_path.exists():
        return
    metadata = {
        "assetType": asset_type,
        "guid": uuid4().hex,
        "importSettings": settings,
        "importer": importer,
        "source": f"Resources/3DModel/Gimmicks/volcanic_bombard_shell/{name}",
        "version": 1,
    }
    metadata_path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8", newline="\n")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    build_texture()
    build_mesh()
    (OUT / f"{NAME}.mtl").write_text(MTL, encoding="ascii", newline="\n")
    write_metadata(f"{NAME}.obj", "Model", "ModelImporter",
                   {"generateTangents": True, "scale": 1.0})
    write_metadata(f"{NAME}.mtl", "Binary", "BinaryImporter", {})
    for image_name in ("basecolor.png", "normal.png", "orm.png"):
        write_metadata(image_name, "Texture", "TextureImporter",
                       {"colorSpace": "Auto", "generateMipmaps": True})


if __name__ == "__main__":
    main()
