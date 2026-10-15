"""Generate the stage-one mushroom trampoline OBJ without external modeling dependencies."""

import json
from math import cos, pi, sin
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Resources/3DModel/Gimmicks/mushroom_trampoline"
OUT.mkdir(parents=True, exist_ok=True)
vertices = []
faces = []


def vertex(x, y, z):
    vertices.append((x, y, z))
    return len(vertices)


def lathe(profile, material, sides=32):
    rings = []
    for radius, height in profile:
        rings.append([vertex(radius * cos(2 * pi * i / sides), height,
                             radius * sin(2 * pi * i / sides)) for i in range(sides)])
    for lower, upper in zip(rings, rings[1:]):
        for i in range(sides):
            j = (i + 1) % sides
            faces.append((material, (lower[i], lower[j], upper[j], upper[i])))


def spot(x, y, z, radius):
    # Small domed spots sit on the cap, so they read clearly from the game camera.
    rings = []
    for t in (0.0, 0.34, 0.68, 0.91, 1.0):
        rr = radius * (1 - t * t) ** 0.5
        rings.append([vertex(x + rr * cos(2 * pi * i / 12),
                             y + radius * 0.23 * t,
                             z + rr * sin(2 * pi * i / 12)) for i in range(12)])
    for lower, upper in zip(rings, rings[1:]):
        for i in range(12):
            j = (i + 1) % 12
            faces.append(("ivory_spots", (lower[i], lower[j], upper[j], upper[i])))


# Soft pedestal, tapered stem, and cream-colored underside.
lathe([(0, 0.035), (0.53, 0.035), (0.57, 0.11), (0.46, 0.19)], "stem_base")
lathe([(0.44, 0.17), (0.36, 0.36), (0.29, 0.67), (0.40, 0.86)], "stem_cream")
lathe([(0.22, 0.89), (0.72, 0.85), (1.17, 0.82), (1.34, 0.87)], "gills")
# A distinct gold lip makes the useful bounce surface visible even at a distance.
lathe([(1.31, 0.84), (1.38, 0.87), (1.40, 0.94), (1.34, 1.00)], "gold_rim")
lathe([(1.34, 0.97), (1.30, 1.08), (1.16, 1.22), (0.94, 1.37),
       (0.65, 1.48), (0.34, 1.54), (0.0, 1.56)], "coral_cap")
for r, angle, size, top in (
    (0.63, 0.10, 0.21, 1.48), (0.78, 1.31, 0.17, 1.43),
    (0.87, 2.61, 0.21, 1.39), (0.72, 3.84, 0.16, 1.45),
    (0.94, 5.08, 0.19, 1.34), (0.31, 2.04, 0.13, 1.54),
    (0.41, 4.77, 0.14, 1.52),
):
    spot(r * cos(angle), top, r * sin(angle), size)

materials = """newmtl stem_base
Ka 0.34 0.18 0.18
Kd 0.72 0.42 0.43
Ks 0.15 0.10 0.10
Ns 25
illum 2

newmtl stem_cream
Ka 0.32 0.29 0.20
Kd 0.99 0.88 0.67
Ks 0.20 0.17 0.13
Ns 38
illum 2

newmtl gills
Ka 0.30 0.24 0.19
Kd 0.93 0.75 0.62
Ks 0.19 0.14 0.11
Ns 25
illum 2

newmtl gold_rim
Ka 0.39 0.23 0.06
Kd 1.00 0.71 0.25
Ks 0.42 0.31 0.12
Ns 75
illum 2

newmtl coral_cap
Ka 0.38 0.08 0.17
Kd 0.97 0.30 0.48
Ks 0.45 0.22 0.28
Ns 76
illum 2

newmtl ivory_spots
Ka 0.36 0.34 0.27
Kd 1.00 0.98 0.83
Ks 0.26 0.25 0.22
Ns 48
illum 2
"""
(OUT / "mushroom_trampoline.mtl").write_text(materials, encoding="utf-8")
lines = ["mtllib mushroom_trampoline.mtl", "o mushroom_trampoline"]
lines.extend(f"v {x:.6f} {y:.6f} {z:.6f}" for x, y, z in vertices)
active = None
for material, indices in faces:
    if material != active:
        lines.append(f"usemtl {material}")
        active = material
    lines.append("f " + " ".join(map(str, indices)))
(OUT / "mushroom_trampoline.obj").write_text("\n".join(lines) + "\n", encoding="utf-8")
for extension, asset_type, guid in (
    ("obj", "Model", "b60a1a15ee584ab89aa26cab52072a8b"),
    ("mtl", "Binary", "eca22c518e5f40af8ed8dc429e2441c4"),
):
    source = f"Resources/3DModel/Gimmicks/mushroom_trampoline/mushroom_trampoline.{extension}"
    meta = {
        "assetType": asset_type,
        "guid": guid,
        "importSettings": {"generateTangents": True, "scale": 1.0} if extension == "obj" else {},
        "importer": "ModelImporter" if extension == "obj" else "BinaryImporter",
        "source": source,
        "version": 1,
    }
    (OUT / f"mushroom_trampoline.{extension}.meta").write_text(
        json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
