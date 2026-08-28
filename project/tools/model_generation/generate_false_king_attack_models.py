"""偽王スライムの攻撃本体モデルを決定的に生成します。

丸いビルボード粒子やPrimitiveだけに頼らず、攻撃の種類をシルエットで
判別できる王冠槍、王威波、掃射刃、突進翼、支配紋章をOBJ/MTLへ出力します。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from generate_ring_burner_model import ObjBuilder, cross, dot, normalize, sub


PROJECT_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_ROOT = PROJECT_ROOT / "Resources" / "3DModel" / "Effects"

Vec3 = tuple[float, float, float]


MATERIALS = {
    "RoyalGold": {
        "kd": (1.00, 0.58, 0.055),
        "ks": (1.00, 0.90, 0.48),
        "ke": (0.28, 0.105, 0.006),
        "roughness": 0.22,
        "metallic": 0.82,
        "preview": (255, 169, 33),
    },
    "RoyalCore": {
        "kd": (1.00, 0.94, 0.72),
        "ks": (1.00, 1.00, 1.00),
        "ke": (0.72, 0.42, 0.08),
        "roughness": 0.13,
        "metallic": 0.24,
        "preview": (255, 244, 195),
    },
    "RoyalViolet": {
        "kd": (0.62, 0.10, 0.92),
        "ks": (0.96, 0.62, 1.00),
        "ke": (0.20, 0.018, 0.42),
        "roughness": 0.18,
        "metallic": 0.48,
        "preview": (186, 55, 244),
    },
    "RoyalDark": {
        "kd": (0.075, 0.045, 0.13),
        "ks": (0.38, 0.22, 0.56),
        "ke": (0.018, 0.006, 0.04),
        "roughness": 0.34,
        "metallic": 0.72,
        "preview": (43, 25, 70),
    },
}


def add_diamond(
    model: ObjBuilder,
    material: str,
    center: Vec3,
    radius_x: float,
    half_y: float,
    radius_z: float,
) -> None:
    x, y, z = center
    top = model.vertex((x, y + half_y, z), (0.5, 0.0), (0.0, 1.0, 0.0))
    bottom = model.vertex((x, y - half_y, z), (0.5, 1.0), (0.0, -1.0, 0.0))
    ring = [
        model.vertex((x + radius_x, y, z), (1.0, 0.5), (1.0, 0.0, 0.0)),
        model.vertex((x, y, z + radius_z), (0.5, 0.5), (0.0, 0.0, 1.0)),
        model.vertex((x - radius_x, y, z), (0.0, 0.5), (-1.0, 0.0, 0.0)),
        model.vertex((x, y, z - radius_z), (0.5, 0.5), (0.0, 0.0, -1.0)),
    ]
    for index in range(4):
        nxt = (index + 1) % 4
        model.face(material, top, ring[index], ring[nxt], False)
        model.face(material, bottom, ring[nxt], ring[index], False)


def rotated_box_y(
    model: ObjBuilder,
    material: str,
    center: Vec3,
    half: Vec3,
    yaw: float,
) -> None:
    cosine = math.cos(yaw)
    sine = math.sin(yaw)

    def transform(local: Vec3) -> Vec3:
        return (
            center[0] + local[0] * cosine + local[2] * sine,
            center[1] + local[1],
            center[2] - local[0] * sine + local[2] * cosine,
        )

    x, y, z = half
    corners = [
        transform((-x, -y, -z)), transform((x, -y, -z)),
        transform((x, y, -z)), transform((-x, y, -z)),
        transform((-x, -y, z)), transform((x, -y, z)),
        transform((x, y, z)), transform((-x, y, z)),
    ]
    faces = (
        (0, 1, 2, 3), (5, 4, 7, 6), (4, 0, 3, 7),
        (1, 5, 6, 2), (3, 2, 6, 7), (4, 5, 1, 0),
    )
    for face in faces:
        a, b, c, d = (corners[index] for index in face)
        normal = normalize(cross(sub(b, a), sub(c, a)))
        model.quad(material, (a, b, c, d), normal)


def build_crown_lance() -> ObjBuilder:
    model = ObjBuilder()
    model.cylinder("RoyalDark", (0.0, 0.08, 0.0), 0.17, 1.85, 20)
    model.cylinder("RoyalCore", (0.0, -0.02, 0.0), 0.085, 2.18, 16)
    add_diamond(model, "RoyalGold", (0.0, -1.30, 0.0), 0.42, 0.72, 0.42)
    model.torus("RoyalGold", (0.0, 0.76, 0.0), 0.43, 0.085, 28, 8)
    model.torus("RoyalViolet", (0.0, 0.86, 0.0), 0.26, 0.055, 24, 7)
    for index in range(6):
        angle = math.tau * index / 6.0
        add_diamond(
            model,
            "RoyalGold" if index % 2 == 0 else "RoyalViolet",
            (math.cos(angle) * 0.47, 1.03, math.sin(angle) * 0.47),
            0.14,
            0.40 if index % 2 == 0 else 0.30,
            0.14,
        )
    add_diamond(model, "RoyalCore", (0.0, 1.28, 0.0), 0.24, 0.35, 0.24)
    return model


def build_royal_wave() -> ObjBuilder:
    model = ObjBuilder()
    model.torus("RoyalDark", (0.0, 0.0, 0.0), 0.90, 0.105, 48, 8)
    model.torus("RoyalGold", (0.0, 0.035, 0.0), 0.98, 0.070, 48, 7)
    model.torus("RoyalViolet", (0.0, 0.075, 0.0), 0.80, 0.050, 40, 6)
    for index in range(16):
        angle = math.tau * index / 16.0
        radius = 1.04
        add_diamond(
            model,
            "RoyalGold" if index % 2 == 0 else "RoyalViolet",
            (math.cos(angle) * radius, 0.15, math.sin(angle) * radius),
            0.075,
            0.25 if index % 2 == 0 else 0.16,
            0.075,
        )
    return model


def build_royal_beam() -> ObjBuilder:
    model = ObjBuilder()
    model.box("RoyalDark", (0.0, 0.0, 0.0), (0.50, 0.11, 1.0))
    model.box("RoyalCore", (0.0, 0.10, 0.0), (0.17, 0.07, 1.03))
    model.box("RoyalGold", (-0.41, 0.10, 0.0), (0.065, 0.075, 1.02))
    model.box("RoyalGold", (0.41, 0.10, 0.0), (0.065, 0.075, 1.02))
    for side in (-1.0, 1.0):
        for index in range(5):
            z = -0.80 + index * 0.40
            add_diamond(
                model,
                "RoyalViolet" if index % 2 else "RoyalGold",
                (side * 0.58, 0.20, z),
                0.12,
                0.22,
                0.12,
            )
    add_diamond(model, "RoyalCore", (0.0, 0.23, 0.0), 0.24, 0.20, 0.24)
    return model


def build_rush_wings() -> ObjBuilder:
    model = ObjBuilder()
    # 前方へ突き出す槍ではなく、ボスの後方に流れる左右の王冠翼です。
    for side in (-1.0, 1.0):
        rotated_box_y(model, "RoyalDark", (side * 0.75, 0.02, -1.20), (0.16, 0.09, 1.55), -side * 0.34)
        rotated_box_y(model, "RoyalGold", (side * 0.82, 0.13, -1.30), (0.065, 0.065, 1.48), -side * 0.34)
        rotated_box_y(model, "RoyalViolet", (side * 1.18, 0.18, -1.65), (0.09, 0.07, 0.92), -side * 0.52)
        for index in range(4):
            add_diamond(
                model,
                "RoyalGold" if index % 2 == 0 else "RoyalViolet",
                (side * (0.72 + index * 0.18), 0.27, -0.65 - index * 0.52),
                0.14,
                0.23,
                0.14,
            )
    model.box("RoyalCore", (0.0, 0.10, -1.55), (0.12, 0.06, 1.48))
    return model


def build_dominion_sigil() -> ObjBuilder:
    model = ObjBuilder()
    model.torus("RoyalDark", (0.0, 0.0, 0.0), 0.92, 0.12, 48, 9)
    model.torus("RoyalGold", (0.0, 0.04, 0.0), 1.06, 0.075, 48, 7)
    model.torus("RoyalViolet", (0.0, 0.09, 0.0), 0.58, 0.060, 36, 7)
    for index in range(8):
        angle = math.tau * index / 8.0
        rotated_box_y(model, "RoyalCore", (math.sin(angle) * 0.46, 0.10, math.cos(angle) * 0.46), (0.035, 0.035, 0.44), angle)
        add_diamond(
            model,
            "RoyalGold" if index % 2 == 0 else "RoyalViolet",
            (math.sin(angle) * 1.16, 0.24, math.cos(angle) * 1.16),
            0.13,
            0.44 if index % 2 == 0 else 0.30,
            0.13,
        )
    add_diamond(model, "RoyalCore", (0.0, 0.22, 0.0), 0.32, 0.28, 0.32)
    return model


MODELS = {
    "false_king_crown_lance": build_crown_lance,
    "false_king_royal_wave": build_royal_wave,
    "false_king_royal_beam": build_royal_beam,
    "false_king_rush_wings": build_rush_wings,
    "false_king_dominion_sigil": build_dominion_sigil,
}


def stable_guid(relative_path: str) -> str:
    return hashlib.md5(f"cg2:false-king-attack:{relative_path}".encode("utf-8")).hexdigest()


def write_meta(asset: Path, asset_type: str, importer: str, settings: dict) -> None:
    relative = asset.relative_to(PROJECT_ROOT).as_posix()
    data = {
        "assetType": asset_type,
        "guid": stable_guid(relative),
        "importSettings": settings,
        "importer": importer,
        "source": relative,
        "version": 1,
    }
    Path(str(asset) + ".meta").write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def write_obj(model: ObjBuilder, path: Path, model_name: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as file:
        file.write(f"# CG2 False King attack model\nmtllib {model_name}.mtl\no {model_name}\n")
        for value in model.vertices:
            file.write(f"v {value[0]:.7f} {value[1]:.7f} {value[2]:.7f}\n")
        for value in model.uvs:
            file.write(f"vt {value[0]:.7f} {value[1]:.7f}\n")
        for value in model.normals:
            file.write(f"vn {value[0]:.7f} {value[1]:.7f} {value[2]:.7f}\n")
        for material, faces in model.faces.items():
            file.write(f"usemtl {material}\n")
            file.write("s 1\n" if material in model.smooth_materials else "s off\n")
            for a, b, c in faces:
                file.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


def write_materials(path: Path) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as file:
        file.write("# CG2 False King attack PBR material set\n\n")
        for name, material in MATERIALS.items():
            kd = material["kd"]
            ks = material["ks"]
            ke = material["ke"]
            file.write(f"newmtl {name}\n")
            file.write(f"Ka {kd[0] * 0.28:.6f} {kd[1] * 0.28:.6f} {kd[2] * 0.28:.6f}\n")
            file.write(f"Kd {kd[0]:.6f} {kd[1]:.6f} {kd[2]:.6f}\n")
            file.write(f"Ks {ks[0]:.6f} {ks[1]:.6f} {ks[2]:.6f}\n")
            file.write(f"Ke {ke[0]:.6f} {ke[1]:.6f} {ke[2]:.6f}\n")
            file.write("Ns 160.000000\nNi 1.450000\nd 1.000000\nillum 2\n")
            file.write(f"Pr {material['roughness']:.6f}\nPm {material['metallic']:.6f}\n\n")


def render_preview(models: dict[str, ObjBuilder], output: Path) -> None:
    panel_width, panel_height = 420, 330
    image = Image.new("RGBA", (panel_width * 3, panel_height * 2), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image, "RGBA")
    camera = (4.6, 3.6, 5.4)
    target = (0.0, 0.0, 0.0)
    forward = normalize(sub(target, camera))
    right = normalize(cross(forward, (0.0, 1.0, 0.0)))
    up = normalize(cross(right, forward))
    light = normalize((-0.46, 0.82, 0.34))

    for model_index, (name, model) in enumerate(models.items()):
        panel_x = model_index % 3 * panel_width
        panel_y = model_index // 3 * panel_height
        draw.rounded_rectangle(
            (panel_x + 8, panel_y + 8, panel_x + panel_width - 8, panel_y + panel_height - 8),
            radius=20,
            fill=(13, 11, 26, 225),
            outline=(212, 150, 54, 210),
            width=2,
        )
        bounds_min = tuple(min(vertex[axis] for vertex in model.vertices) for axis in range(3))
        bounds_max = tuple(max(vertex[axis] for vertex in model.vertices) for axis in range(3))
        extent = max(bounds_max[axis] - bounds_min[axis] for axis in range(3))
        scale = 245.0 / max(extent, 0.2)
        center = tuple((bounds_min[axis] + bounds_max[axis]) * 0.5 for axis in range(3))
        projected: list[tuple[float, float, float]] = []
        for vertex in model.vertices:
            local = tuple(vertex[axis] - center[axis] for axis in range(3))
            projected.append((
                panel_x + panel_width * 0.50 + dot(local, right) * scale,
                panel_y + panel_height * 0.55 - dot(local, up) * scale,
                dot(local, forward),
            ))
        triangles: list[tuple[float, str, tuple[int, int, int], float]] = []
        for material, faces in model.faces.items():
            for face in faces:
                normal = normalize(tuple(sum(model.normals[index - 1][axis] for index in face) for axis in range(3)))
                brightness = 0.52 + 0.48 * max(0.0, dot(normal, light))
                depth = sum(projected[index - 1][2] for index in face) / 3.0
                triangles.append((depth, material, face, brightness))
        triangles.sort(key=lambda item: item[0], reverse=True)
        for _, material, face, brightness in triangles:
            base = MATERIALS[material]["preview"]
            color = tuple(max(0, min(255, int(channel * brightness))) for channel in base) + (255,)
            draw.polygon([(projected[index - 1][0], projected[index - 1][1]) for index in face], fill=color)
        draw.text((panel_x + 24, panel_y + 22), name, fill=(255, 244, 214, 255))

    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(description="偽王スライムの専用攻撃モデルを生成します。")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    built_models: dict[str, ObjBuilder] = {}
    for name, factory in MODELS.items():
        output_dir = OUTPUT_ROOT / name
        output_dir.mkdir(parents=True, exist_ok=True)
        model = factory()
        obj_path = output_dir / f"{name}.obj"
        material_path = output_dir / f"{name}.mtl"
        write_obj(model, obj_path, name)
        write_materials(material_path)
        write_meta(obj_path, "Model", "ModelImporter", {"generateTangents": True, "scale": 1.0})
        write_meta(material_path, "Binary", "BinaryImporter", {})
        built_models[name] = model
        print(f"Generated: {obj_path} ({len(model.vertices)} vertices)")

    if args.preview:
        preview_path = args.preview.resolve()
        render_preview(built_models, preview_path)
        print(f"Preview: {preview_path}")


if __name__ == "__main__":
    main()
