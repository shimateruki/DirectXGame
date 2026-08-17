from __future__ import annotations

import json
import math
import uuid
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = PROJECT_ROOT / "Resources" / "3DModel" / "Gimmicks" / "stage_gate_lock"
ASSET_NAMESPACE = uuid.UUID("57fa26c7-109a-47cc-bef4-e08b49d05d0d")


class MeshBuilder:
    def __init__(self) -> None:
        self.vertices: list[tuple[float, float, float]] = []
        self.uvs: list[tuple[float, float]] = []
        self.normals: list[tuple[float, float, float]] = []
        self.faces: dict[str, list[tuple[int, int, int]]] = {}

    def add_vertex(
        self,
        position: tuple[float, float, float],
        uv: tuple[float, float],
        normal: tuple[float, float, float],
    ) -> int:
        self.vertices.append(position)
        self.uvs.append(uv)
        self.normals.append(normal)
        return len(self.vertices)

    def add_triangle(self, material: str, a: int, b: int, c: int) -> None:
        self.faces.setdefault(material, []).append((a, b, c))

    def add_box(
        self,
        material: str,
        center: tuple[float, float, float],
        size: tuple[float, float, float],
    ) -> None:
        cx, cy, cz = center
        hx, hy, hz = (value * 0.5 for value in size)
        corners = {
            "lbf": (cx - hx, cy - hy, cz + hz),
            "rbf": (cx + hx, cy - hy, cz + hz),
            "ltf": (cx - hx, cy + hy, cz + hz),
            "rtf": (cx + hx, cy + hy, cz + hz),
            "lbb": (cx - hx, cy - hy, cz - hz),
            "rbb": (cx + hx, cy - hy, cz - hz),
            "ltb": (cx - hx, cy + hy, cz - hz),
            "rtb": (cx + hx, cy + hy, cz - hz),
        }
        quads = [
            ("lbf", "rbf", "rtf", "ltf", (0.0, 0.0, 1.0)),
            ("rbb", "lbb", "ltb", "rtb", (0.0, 0.0, -1.0)),
            ("rbf", "rbb", "rtb", "rtf", (1.0, 0.0, 0.0)),
            ("lbb", "lbf", "ltf", "ltb", (-1.0, 0.0, 0.0)),
            ("ltf", "rtf", "rtb", "ltb", (0.0, 1.0, 0.0)),
            ("lbb", "rbb", "rbf", "lbf", (0.0, -1.0, 0.0)),
        ]
        uv = ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))
        for p0, p1, p2, p3, normal in quads:
            indices = [
                self.add_vertex(corners[name], uv[index], normal)
                for index, name in enumerate((p0, p1, p2, p3))
            ]
            self.add_triangle(material, indices[0], indices[1], indices[2])
            self.add_triangle(material, indices[0], indices[2], indices[3])

    def add_torus(
        self,
        material: str,
        center: tuple[float, float, float],
        axis_u: tuple[float, float, float],
        axis_v: tuple[float, float, float],
        major_radius: float,
        tube_radius: float,
        major_segments: int = 12,
        tube_segments: int = 6,
        start_angle: float = 0.0,
        end_angle: float = math.tau,
    ) -> None:
        ux, uy, uz = axis_u
        vx, vy, vz = axis_v
        nx = uy * vz - uz * vy
        ny = uz * vx - ux * vz
        nz = ux * vy - uy * vx
        rings: list[list[int]] = []
        closed = abs((end_angle - start_angle) - math.tau) < 1.0e-5
        ring_count = major_segments if closed else major_segments + 1

        for major_index in range(ring_count):
            major_t = major_index / major_segments
            major_angle = start_angle + (end_angle - start_angle) * major_t
            radial_x = ux * math.cos(major_angle) + vx * math.sin(major_angle)
            radial_y = uy * math.cos(major_angle) + vy * math.sin(major_angle)
            radial_z = uz * math.cos(major_angle) + vz * math.sin(major_angle)
            ring: list[int] = []
            for tube_index in range(tube_segments):
                tube_t = tube_index / tube_segments
                tube_angle = math.tau * tube_t
                normal = (
                    radial_x * math.cos(tube_angle) + nx * math.sin(tube_angle),
                    radial_y * math.cos(tube_angle) + ny * math.sin(tube_angle),
                    radial_z * math.cos(tube_angle) + nz * math.sin(tube_angle),
                )
                position = (
                    center[0] + radial_x * major_radius + normal[0] * tube_radius,
                    center[1] + radial_y * major_radius + normal[1] * tube_radius,
                    center[2] + radial_z * major_radius + normal[2] * tube_radius,
                )
                ring.append(self.add_vertex(position, (major_t, tube_t), normal))
            rings.append(ring)

        segment_count = major_segments if closed else major_segments
        for major_index in range(segment_count):
            next_major = (major_index + 1) % len(rings)
            for tube_index in range(tube_segments):
                next_tube = (tube_index + 1) % tube_segments
                a = rings[major_index][tube_index]
                b = rings[next_major][tube_index]
                c = rings[next_major][next_tube]
                d = rings[major_index][next_tube]
                self.add_triangle(material, a, b, c)
                self.add_triangle(material, a, c, d)


def normalize_xy(x: float, y: float) -> tuple[float, float, float]:
    length = math.sqrt(x * x + y * y)
    return (x / length, y / length, 0.0)


def add_chain(
    builder: MeshBuilder,
    start: tuple[float, float, float],
    end: tuple[float, float, float],
) -> None:
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    dz = end[2] - start[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    direction = (dx / length, dy / length, dz / length)
    in_plane_perpendicular = normalize_xy(-direction[1], direction[0])
    link_spacing = 0.205
    link_count = max(2, int(length / link_spacing) + 1)

    for index in range(link_count):
        t = index / (link_count - 1)
        center = (
            start[0] + dx * t,
            start[1] + dy * t,
            start[2] + dz * t,
        )
        secondary_axis = (0.0, 0.0, 1.0) if index % 2 == 0 else in_plane_perpendicular
        builder.add_torus(
            "ChainBronze",
            center,
            direction,
            secondary_axis,
            0.125,
            0.034,
            10,
            6,
        )


def build_model() -> MeshBuilder:
    builder = MeshBuilder()
    # ゲートのローカル+Zが島の内側（プレイヤーが見る正面）なので、その手前へ配置します。
    chain_depth = 0.64
    add_chain(builder, (-1.42, 0.88, chain_depth), (1.42, -0.68, chain_depth))
    add_chain(builder, (-1.42, -0.68, chain_depth - 0.015), (1.42, 0.88, chain_depth - 0.015))

    # 南京錠はゲート中央で鎖の交点を覆う大きさにし、遠景からも封鎖状態を読めるようにします。
    builder.add_box("LockGold", (0.0, -0.03, 0.82), (0.58, 0.52, 0.26))
    builder.add_box("LockGold", (0.0, 0.20, 0.82), (0.66, 0.14, 0.28))
    builder.add_box("LockShadow", (0.0, -0.10, 0.965), (0.105, 0.17, 0.018))

    # 上半円と脚を分け、板状ではなく奥行きのあるシャックルにします。
    builder.add_torus(
        "LockGold",
        (0.0, 0.25, 0.82),
        (1.0, 0.0, 0.0),
        (0.0, 1.0, 0.0),
        0.245,
        0.052,
        12,
        7,
        0.0,
        math.pi,
    )
    builder.add_box("LockGold", (-0.245, 0.175, 0.82), (0.104, 0.19, 0.104))
    builder.add_box("LockGold", (0.245, 0.175, 0.82), (0.104, 0.19, 0.104))
    return builder


def write_obj(builder: MeshBuilder, path: Path) -> None:
    lines = ["mtllib stage_gate_lock.mtl", "o stage_gate_lock"]
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
        """newmtl ChainBronze
Ka 0.10 0.075 0.035
Kd 0.40 0.285 0.105
Ks 0.36 0.25 0.09
Ns 68
d 1.0
illum 2

newmtl LockGold
Ka 0.20 0.12 0.025
Kd 0.88 0.55 0.10
Ks 0.72 0.50 0.16
Ns 92
d 1.0
illum 2

newmtl LockShadow
Ka 0.015 0.012 0.010
Kd 0.055 0.045 0.035
Ks 0.04 0.03 0.02
Ns 18
d 1.0
illum 2
""",
        encoding="utf-8",
        newline="\n",
    )


def write_meta(path: Path, asset_type: str) -> None:
    relative = path.relative_to(PROJECT_ROOT).as_posix()
    if asset_type == "Model":
        data = {
            "assetType": "Model",
            "guid": uuid.uuid5(ASSET_NAMESPACE, relative).hex,
            "importSettings": {"generateTangents": True, "scale": 1.0},
            "importer": "ModelImporter",
            "source": relative,
            "version": 1,
        }
    else:
        data = {
            "assetType": "Binary",
            "guid": uuid.uuid5(ASSET_NAMESPACE, relative).hex,
            "importSettings": {},
            "importer": "BinaryImporter",
            "source": relative,
            "version": 1,
        }
    path.with_name(path.name + ".meta").write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    obj_path = OUTPUT_DIR / "stage_gate_lock.obj"
    mtl_path = OUTPUT_DIR / "stage_gate_lock.mtl"
    builder = build_model()
    write_obj(builder, obj_path)
    write_mtl(mtl_path)
    write_meta(obj_path, "Model")
    write_meta(mtl_path, "Binary")
    print(f"Generated {obj_path.relative_to(PROJECT_ROOT).as_posix()}")
    print(f"vertices={len(builder.vertices)} triangles={sum(len(faces) for faces in builder.faces.values())}")


if __name__ == "__main__":
    main()
