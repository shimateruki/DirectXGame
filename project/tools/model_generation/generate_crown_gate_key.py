from __future__ import annotations

import math
from pathlib import Path

from generate_stage_gate_lock import MeshBuilder, PROJECT_ROOT, write_meta


OUTPUT_DIR = PROJECT_ROOT / "Resources" / "3DModel" / "Gimmicks" / "crown_gate_key"


def _normalize(value: tuple[float, float, float]) -> tuple[float, float, float]:
    x, y, z = value
    length = math.sqrt(x * x + y * y + z * z)
    if length <= 1.0e-8:
        return (0.0, 1.0, 0.0)
    return (x / length, y / length, z / length)


def _face_normal(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
    c: tuple[float, float, float],
) -> tuple[float, float, float]:
    ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    return _normalize(
        (
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        )
    )


def add_triangular_prism(
    builder: MeshBuilder,
    material: str,
    points: tuple[tuple[float, float], tuple[float, float], tuple[float, float]],
    center_z: float,
    depth: float,
) -> None:
    """XY 平面の三角形を Z 方向へ押し出した、閉じた王冠パーツを追加します。"""
    half_depth = depth * 0.5
    front = [(x, y, center_z + half_depth) for x, y in points]
    back = [(x, y, center_z - half_depth) for x, y in points]

    def add_face(vertices: list[tuple[float, float, float]]) -> None:
        normal = _face_normal(vertices[0], vertices[1], vertices[2])
        uvs = ((0.0, 1.0), (1.0, 1.0), (0.5, 0.0), (0.0, 0.0))
        indices = [
            builder.add_vertex(vertex, uvs[index], normal)
            for index, vertex in enumerate(vertices)
        ]
        builder.add_triangle(material, indices[0], indices[1], indices[2])
        if len(indices) == 4:
            builder.add_triangle(material, indices[0], indices[2], indices[3])

    add_face([front[0], front[1], front[2]])
    add_face([back[2], back[1], back[0]])
    for index in range(3):
        next_index = (index + 1) % 3
        add_face([back[index], back[next_index], front[next_index], front[index]])


def build_model() -> MeshBuilder:
    builder = MeshBuilder()

    # 鍵はローカル +Z が差し込み方向です。王冠の正面は XY 平面になります。
    crown_z = -0.92
    builder.add_box("CrownGold", (0.0, 0.02, crown_z), (1.52, 0.28, 0.34))
    builder.add_box("CrownHighlight", (0.0, 0.18, crown_z - 0.01), (1.36, 0.10, 0.37))

    add_triangular_prism(
        builder,
        "CrownGold",
        ((-0.70, 0.12), (-0.23, 0.12), (-0.52, 0.88)),
        crown_z,
        0.34,
    )
    add_triangular_prism(
        builder,
        "CrownGold",
        ((-0.31, 0.12), (0.31, 0.12), (0.0, 1.16)),
        crown_z,
        0.38,
    )
    add_triangular_prism(
        builder,
        "CrownGold",
        ((0.23, 0.12), (0.70, 0.12), (0.52, 0.88)),
        crown_z,
        0.34,
    )

    for x, y, radius in ((-0.52, 0.88, 0.105), (0.0, 1.16, 0.125), (0.52, 0.88, 0.105)):
        builder.add_torus(
            "CrownHighlight",
            (x, y, crown_z - 0.03),
            (1.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            radius,
            0.040,
            12,
            6,
        )

    # 中央の青い宝石はステージセレクトの水色と金色をつなぐアクセントです。
    builder.add_torus(
        "CrownGem",
        (0.0, 0.18, crown_z - 0.205),
        (1.0, 0.0, 0.0),
        (0.0, 1.0, 0.0),
        0.145,
        0.072,
        14,
        7,
    )
    builder.add_box("CrownGem", (0.0, 0.18, crown_z - 0.19), (0.15, 0.15, 0.07))

    # 王冠から鍵先へ続く軸と、回転が読みやすい段付きのカラーです。
    builder.add_box("CrownGold", (0.0, -0.02, 0.20), (0.22, 0.22, 2.05))
    builder.add_torus(
        "CrownHighlight",
        (0.0, -0.02, -0.37),
        (1.0, 0.0, 0.0),
        (0.0, 1.0, 0.0),
        0.19,
        0.055,
        14,
        6,
    )
    builder.add_torus(
        "CrownHighlight",
        (0.0, -0.02, 0.75),
        (1.0, 0.0, 0.0),
        (0.0, 1.0, 0.0),
        0.16,
        0.045,
        14,
        6,
    )

    # 鍵先は横から見ても形が分かるように、左右非対称の二段構成にします。
    builder.add_box("CrownGold", (0.29, -0.02, 1.12), (0.72, 0.23, 0.25))
    builder.add_box("CrownHighlight", (0.48, 0.18, 1.34), (0.34, 0.24, 0.22))
    builder.add_box("CrownGold", (0.56, -0.17, 1.46), (0.30, 0.30, 0.24))
    builder.add_box("KeyShadow", (-0.075, -0.085, 0.31), (0.035, 0.035, 1.72))
    return builder


def write_obj(builder: MeshBuilder, path: Path) -> None:
    lines = ["mtllib crown_gate_key.mtl", "o crown_gate_key"]
    lines.extend(f"v {x:.6f} {y:.6f} {z:.6f}" for x, y, z in builder.vertices)
    lines.extend(f"vt {u:.6f} {v:.6f}" for u, v in builder.uvs)
    lines.extend(f"vn {x:.6f} {y:.6f} {z:.6f}" for x, y, z in builder.normals)
    lines.append("s off")
    for material, faces in builder.faces.items():
        lines.append(f"usemtl {material}")
        lines.extend(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}" for a, b, c in faces)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def write_mtl(path: Path) -> None:
    path.write_text(
        """newmtl CrownGold
Ka 0.20 0.11 0.018
Kd 0.94 0.58 0.075
Ks 0.88 0.66 0.24
Ns 112
d 1.0
illum 2

newmtl CrownHighlight
Ka 0.28 0.18 0.045
Kd 1.00 0.82 0.25
Ks 1.00 0.92 0.58
Ns 150
d 1.0
illum 2

newmtl CrownGem
Ka 0.02 0.16 0.18
Kd 0.10 0.84 0.94
Ks 0.75 1.00 1.00
Ns 132
d 1.0
illum 2

newmtl KeyShadow
Ka 0.055 0.025 0.008
Kd 0.22 0.09 0.018
Ks 0.30 0.16 0.05
Ns 48
d 1.0
illum 2
""",
        encoding="utf-8",
        newline="\n",
    )


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    obj_path = OUTPUT_DIR / "crown_gate_key.obj"
    mtl_path = OUTPUT_DIR / "crown_gate_key.mtl"
    builder = build_model()
    write_obj(builder, obj_path)
    write_mtl(mtl_path)
    write_meta(obj_path, "Model")
    write_meta(mtl_path, "Binary")
    triangle_count = sum(len(faces) for faces in builder.faces.values())
    print(f"Generated {obj_path.relative_to(PROJECT_ROOT).as_posix()}")
    print(f"vertices={len(builder.vertices)} triangles={triangle_count}")


if __name__ == "__main__":
    main()
