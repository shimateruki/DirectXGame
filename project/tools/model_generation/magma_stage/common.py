"""共通設定とオブジェクト構築。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Iterable, Sequence


PROJECT_ROOT = Path(__file__).resolve().parents[3]
RESOURCE_ROOT = PROJECT_ROOT / "Resources"
SCENE_PATH = RESOURCE_ROOT / "json/3Dobject/stage2_object.json"
PLAYER_PATH = RESOURCE_ROOT / "json/3Dobject/stage2_player.json"
CAMERA_PATH = RESOURCE_ROOT / "json/3Dobject/stage2_camera.json"
MODEL_ROOT = RESOURCE_ROOT / "3DModel/Stages"
GUID_NAMESPACE = uuid.UUID("65767fc8-6689-45b0-bcb9-434a7e26b59b")
MAGMA_DEFEAT_EVENT_ID = 8501
MAGMA_BOSS_EVENT_ID = 8502
MAGMA_BARRIER_COUNT = 4

Vec2 = tuple[float, float]
Vec3 = tuple[float, float, float]

ROCK_MATERIAL = """newmtl basalt_top
Ka 0.055 0.018 0.014
Kd 0.235 0.080 0.052
Ks 0.120 0.050 0.040
Ns 34
d 1.0
illum 2

newmtl basalt_side
Ka 0.018 0.008 0.010
Kd 0.092 0.035 0.041
Ks 0.090 0.035 0.046
Ns 28
d 1.0
illum 2

newmtl obsidian
Ka 0.008 0.005 0.014
Kd 0.032 0.018 0.050
Ks 0.420 0.185 0.260
Ns 104
d 1.0
illum 2

newmtl ember
Ka 0.650 0.080 0.004
Kd 1.000 0.285 0.018
Ks 1.000 0.620 0.120
Ns 120
d 1.0
illum 2

newmtl magma
Ka 0.820 0.055 0.002
Kd 1.000 0.125 0.006
Ks 1.000 0.420 0.040
Ns 128
d 1.0
illum 2

newmtl metal
Ka 0.040 0.028 0.026
Kd 0.210 0.135 0.115
Ks 0.500 0.320 0.260
Ns 86
d 1.0
illum 2

newmtl fortress_floor
Ka 0.030 0.023 0.025
Kd 0.155 0.118 0.116
Ks 0.160 0.120 0.120
Ns 42
d 1.0
illum 2

newmtl fortress_trim
Ka 0.024 0.016 0.018
Kd 0.105 0.066 0.071
Ks 0.245 0.145 0.155
Ns 72
d 1.0
illum 2

newmtl fortress_cliff
Ka 0.012 0.008 0.010
Kd 0.060 0.036 0.042
Ks 0.095 0.060 0.070
Ns 32
d 1.0
illum 2

newmtl iron_grate
Ka 0.022 0.023 0.026
Kd 0.105 0.110 0.122
Ks 0.390 0.410 0.450
Ns 92
d 1.0
illum 2

newmtl iron_hot
Ka 0.180 0.045 0.008
Kd 0.520 0.105 0.018
Ks 0.680 0.260 0.060
Ke 0.180 0.028 0.002
Ns 108
d 1.0
illum 2

newmtl backdrop_near
Ka 0.025 0.010 0.008
Kd 0.145 0.052 0.038
Ks 0.050 0.020 0.014
Ns 18
d 1.0
illum 2

newmtl backdrop_mid
Ka 0.040 0.016 0.012
Kd 0.225 0.086 0.055
Ks 0.070 0.027 0.018
Ns 20
d 1.0
illum 2

newmtl backdrop_far
Ka 0.062 0.026 0.019
Kd 0.315 0.132 0.082
Ks 0.075 0.032 0.021
Ns 18
d 1.0
illum 2

newmtl backdrop_lava
Ka 0.680 0.055 0.004
Kd 1.000 0.190 0.012
Ks 0.820 0.300 0.035
Ns 90
d 1.0
illum 2
"""


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def normalize(value: Vec3) -> Vec3:
    length = math.sqrt(max(dot(value, value), 1.0e-12))
    return (value[0] / length, value[1] / length, value[2] / length)


class ObjBuilder:
    """面ごとに正しい法線とUVを持つ小規模OBJを構築する。"""

    def __init__(self, name: str) -> None:
        self.name = name
        self.positions: list[Vec3] = []
        self.uvs: list[Vec2] = []
        self.normals: list[Vec3] = []
        self.faces: list[tuple[str, list[int]]] = []

    def add_face(
        self,
        points: Sequence[Vec3],
        material: str,
        target_normal: Vec3,
        uvs: Sequence[Vec2] | None = None,
    ) -> None:
        if len(points) < 3:
            raise ValueError("面には3頂点以上が必要です")

        face_points = list(points)
        face_uvs = list(uvs) if uvs is not None else [
            (0.5 + point[0] * 0.035, 0.5 - point[2] * 0.035)
            for point in face_points
        ]
        normal = normalize(cross(sub(face_points[1], face_points[0]), sub(face_points[2], face_points[0])))
        if dot(normal, target_normal) < 0.0:
            face_points.reverse()
            face_uvs.reverse()
            normal = normalize(cross(sub(face_points[1], face_points[0]), sub(face_points[2], face_points[0])))

        indices: list[int] = []
        for point, uv in zip(face_points, face_uvs):
            self.positions.append(point)
            self.uvs.append(uv)
            self.normals.append(normal)
            indices.append(len(self.positions))
        self.faces.append((material, indices))

    def write(self, directory: Path) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        obj_path = directory / f"{self.name}.obj"
        mtl_path = directory / f"{self.name}.mtl"

        lines = [f"mtllib {self.name}.mtl", f"o {self.name}"]
        lines.extend(f"v {x:.6f} {y:.6f} {z:.6f}" for x, y, z in self.positions)
        lines.extend(f"vt {u:.6f} {v:.6f}" for u, v in self.uvs)
        lines.extend(f"vn {x:.6f} {y:.6f} {z:.6f}" for x, y, z in self.normals)

        active_material = ""
        for material, indices in self.faces:
            if material != active_material:
                lines.append(f"usemtl {material}")
                active_material = material
            refs = " ".join(f"{index}/{index}/{index}" for index in indices)
            lines.append(f"f {refs}")

        obj_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        mtl_path.write_text(ROCK_MATERIAL, encoding="utf-8", newline="\n")
        ensure_asset_meta(
            obj_path,
            asset_type="Model",
            importer="ModelImporter",
            import_settings={"generateTangents": True, "scale": 1},
        )
        ensure_asset_meta(
            mtl_path,
            asset_type="Binary",
            importer="BinaryImporter",
            import_settings={},
        )


def ensure_asset_meta(
    asset_path: Path,
    *,
    asset_type: str,
    importer: str,
    import_settings: dict,
) -> None:
    """既存GUIDを保持しつつ、新規生成物に決定的な.metaを付与する。"""

    meta_path = asset_path.with_name(asset_path.name + ".meta")
    if meta_path.exists():
        return
    source = asset_path.relative_to(PROJECT_ROOT).as_posix()
    metadata = {
        "assetType": asset_type,
        "guid": uuid.uuid5(GUID_NAMESPACE, source).hex,
        "importSettings": import_settings,
        "importer": importer,
        "source": source,
        "version": 1,
    }
    meta_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def ring_points(
    count: int,
    radius: float,
    y: float,
    *,
    x_scale: float = 1.0,
    z_scale: float = 1.0,
    phase: float = 0.0,
    noise: float = 0.0,
) -> list[Vec3]:
    points: list[Vec3] = []
    for index in range(count):
        angle = phase + math.tau * index / count
        variation = 1.0 + noise * (
            math.sin(angle * 3.0 + 0.7) * 0.55
            + math.sin(angle * 7.0 - 0.9) * 0.28
        )
        points.append((
            math.cos(angle) * radius * x_scale * variation,
            y,
            math.sin(angle) * radius * z_scale * variation,
        ))
    return points


def chamfered_rect_ring(
    width: float,
    depth: float,
    y: float,
    *,
    chamfer: float,
    edge_steps: int = 3,
    roughness: float = 0.0,
    phase: float = 0.0,
) -> list[Vec3]:
    """城塞床用の面取り矩形リングを作る。

    上面リングでは ``roughness=0`` とし、側面側のリングだけに小さな揺らぎを
    与える。これにより歩行面を完全に平らに保ったまま火山岩の側面を表現する。
    """

    half_width = width * 0.5
    half_depth = depth * 0.5
    safe_chamfer = min(max(chamfer, 0.0), half_width * 0.45, half_depth * 0.45)
    corners = [
        (half_width - safe_chamfer, -half_depth),
        (half_width, -half_depth + safe_chamfer),
        (half_width, half_depth - safe_chamfer),
        (half_width - safe_chamfer, half_depth),
        (-half_width + safe_chamfer, half_depth),
        (-half_width, half_depth - safe_chamfer),
        (-half_width, -half_depth + safe_chamfer),
        (-half_width + safe_chamfer, -half_depth),
    ]

    points: list[Vec3] = []
    subdivisions = max(edge_steps, 1)
    for edge_index, start in enumerate(corners):
        end = corners[(edge_index + 1) % len(corners)]
        for step in range(subdivisions):
            t = step / subdivisions
            x = start[0] + (end[0] - start[0]) * t
            z = start[1] + (end[1] - start[1]) * t
            if roughness > 0.0:
                point_index = edge_index * subdivisions + step
                radial_length = math.sqrt(max(x * x + z * z, 1.0e-8))
                offset = roughness * (
                    math.sin(point_index * 1.79 + phase) * 0.62
                    + math.sin(point_index * 0.73 - phase * 0.5) * 0.38
                )
                x += x / radial_length * offset
                z += z / radial_length * offset
            points.append((x, y, z))
    return points


def connect_rings(
    builder: ObjBuilder,
    upper: Sequence[Vec3],
    lower: Sequence[Vec3],
    material: str,
    *,
    inward: bool = False,
    alternate_material: str | None = None,
    center_xz: Vec2 = (0.0, 0.0),
) -> None:
    if len(upper) != len(lower):
        raise ValueError("接続するリングの頂点数が一致しません")
    count = len(upper)
    for index in range(count):
        next_index = (index + 1) % count
        midpoint = (
            upper[index][0] + upper[next_index][0] + lower[index][0] + lower[next_index][0]
            - center_xz[0] * 4.0,
            0.0,
            upper[index][2] + upper[next_index][2] + lower[index][2] + lower[next_index][2]
            - center_xz[1] * 4.0,
        )
        target = normalize(midpoint)
        if inward:
            target = (-target[0], -target[1], -target[2])
        face_material = alternate_material if alternate_material and index % 7 == 3 else material
        builder.add_face(
            [upper[index], upper[next_index], lower[next_index], lower[index]],
            face_material,
            target,
            [(index / count, 0.0), ((index + 1) / count, 0.0),
             ((index + 1) / count, 1.0), (index / count, 1.0)],
        )


def add_top_fan(builder: ObjBuilder, ring: Sequence[Vec3], y: float, material: str) -> None:
    center = (0.0, y, 0.0)
    count = len(ring)
    for index in range(count):
        next_index = (index + 1) % count
        builder.add_face(
            [center, ring[index], ring[next_index]],
            material,
            (0.0, 1.0, 0.0),
            [(0.5, 0.5), (0.5 + ring[index][0] * 0.055, 0.5 - ring[index][2] * 0.055),
             (0.5 + ring[next_index][0] * 0.055, 0.5 - ring[next_index][2] * 0.055)],
        )


def add_normalized_top_fan(
    builder: ObjBuilder,
    ring: Sequence[Vec3],
    y: float,
    material: str,
    width: float,
    depth: float,
) -> None:
    """0～1の範囲に収まるUVで平坦な歩行面を閉じる。"""

    center = (0.0, y, 0.0)
    count = len(ring)
    inv_width = 1.0 / max(width, 1.0e-6)
    inv_depth = 1.0 / max(depth, 1.0e-6)
    for index in range(count):
        next_index = (index + 1) % count
        current = ring[index]
        following = ring[next_index]
        builder.add_face(
            [center, current, following],
            material,
            (0.0, 1.0, 0.0),
            [
                (0.5, 0.5),
                (0.5 + current[0] * inv_width, 0.5 - current[2] * inv_depth),
                (0.5 + following[0] * inv_width, 0.5 - following[2] * inv_depth),
            ],
        )


def add_bottom_fan(builder: ObjBuilder, ring: Sequence[Vec3], center: Vec3, material: str) -> None:
    count = len(ring)
    for index in range(count):
        next_index = (index + 1) % count
        builder.add_face([center, ring[index], ring[next_index]], material, (0.0, -1.0, 0.0))


def add_surface_ribbon(builder: ObjBuilder, points: Sequence[Vec3], width: float, material: str) -> None:
    for start, end in zip(points, points[1:]):
        dx = end[0] - start[0]
        dz = end[2] - start[2]
        length = math.sqrt(max(dx * dx + dz * dz, 1.0e-8))
        px = -dz / length * width
        pz = dx / length * width
        builder.add_face(
            [
                (start[0] + px, start[1], start[2] + pz),
                (end[0] + px, end[1], end[2] + pz),
                (end[0] - px, end[1], end[2] - pz),
                (start[0] - px, start[1], start[2] - pz),
            ],
            material,
            (0.0, 1.0, 0.0),
        )


def rotate_y(point: Vec3, yaw: float) -> Vec3:
    cosine = math.cos(yaw)
    sine = math.sin(yaw)
    return (
        point[0] * cosine + point[2] * sine,
        point[1],
        -point[0] * sine + point[2] * cosine,
    )


def add_box(
    builder: ObjBuilder,
    center: Vec3,
    size: Vec3,
    material: str,
    yaw: float = 0.0,
) -> None:
    hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
    local = [
        (-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
        (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz),
    ]
    vertices = []
    for point in local:
        rotated = rotate_y(point, yaw)
        vertices.append((rotated[0] + center[0], rotated[1] + center[1], rotated[2] + center[2]))

    faces = [
        ([vertices[3], vertices[2], vertices[1], vertices[0]], (0.0, 0.0, -1.0)),
        ([vertices[4], vertices[5], vertices[6], vertices[7]], (0.0, 0.0, 1.0)),
        ([vertices[0], vertices[4], vertices[7], vertices[3]], (-1.0, 0.0, 0.0)),
        ([vertices[5], vertices[1], vertices[2], vertices[6]], (1.0, 0.0, 0.0)),
        ([vertices[3], vertices[7], vertices[6], vertices[2]], (0.0, 1.0, 0.0)),
        ([vertices[0], vertices[1], vertices[5], vertices[4]], (0.0, -1.0, 0.0)),
    ]
    for points, target in faces:
        builder.add_face(points, material, rotate_y(target, yaw))


def add_oriented_box(
    builder: ObjBuilder,
    center: Vec3,
    size: Vec3,
    material: str,
    basis_x: Vec3,
    basis_y: Vec3,
    basis_z: Vec3,
) -> None:
    """直交基底で向きを指定できる閉じた箱を追加する。"""

    hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
    local = [
        (-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
        (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz),
    ]

    def transform(point: Vec3) -> Vec3:
        return (
            center[0] + basis_x[0] * point[0] + basis_y[0] * point[1] + basis_z[0] * point[2],
            center[1] + basis_x[1] * point[0] + basis_y[1] * point[1] + basis_z[1] * point[2],
            center[2] + basis_x[2] * point[0] + basis_y[2] * point[1] + basis_z[2] * point[2],
        )

    vertices = [transform(point) for point in local]
    faces = [
        ([vertices[3], vertices[2], vertices[1], vertices[0]], tuple(-value for value in basis_z)),
        ([vertices[4], vertices[5], vertices[6], vertices[7]], basis_z),
        ([vertices[0], vertices[4], vertices[7], vertices[3]], tuple(-value for value in basis_x)),
        ([vertices[5], vertices[1], vertices[2], vertices[6]], basis_x),
        ([vertices[3], vertices[7], vertices[6], vertices[2]], basis_y),
        ([vertices[0], vertices[1], vertices[5], vertices[4]], tuple(-value for value in basis_y)),
    ]
    for points, target in faces:
        builder.add_face(points, material, target)


def add_cylinder_x(
    builder: ObjBuilder,
    center: Vec3,
    radius: float,
    length: float,
    material: str,
    segments: int = 16,
) -> None:
    """X軸方向に伸びる閉じた円柱を追加する。"""

    left_x = center[0] - length * 0.5
    right_x = center[0] + length * 0.5
    left: list[Vec3] = []
    right: list[Vec3] = []
    for index in range(segments):
        angle = math.tau * index / segments
        y = center[1] + math.cos(angle) * radius
        z = center[2] + math.sin(angle) * radius
        left.append((left_x, y, z))
        right.append((right_x, y, z))

    for index in range(segments):
        next_index = (index + 1) % segments
        midpoint_y = left[index][1] + left[next_index][1] - center[1] * 2.0
        midpoint_z = left[index][2] + left[next_index][2] - center[2] * 2.0
        outward = normalize((0.0, midpoint_y, midpoint_z))
        builder.add_face(
            [left[index], right[index], right[next_index], left[next_index]],
            material,
            outward,
            [(0.0, index / segments), (1.0, index / segments),
             (1.0, (index + 1) / segments), (0.0, (index + 1) / segments)],
        )
        builder.add_face(
            [(left_x, center[1], center[2]), left[next_index], left[index]],
            material,
            (-1.0, 0.0, 0.0),
        )
        builder.add_face(
            [(right_x, center[1], center[2]), right[index], right[next_index]],
            material,
            (1.0, 0.0, 0.0),
        )


def add_cylinder(
    builder: ObjBuilder,
    center: Vec3,
    radius: float,
    height: float,
    material: str,
    segments: int = 12,
    top_material: str | None = None,
) -> None:
    bottom_y = center[1] - height * 0.5
    top_y = center[1] + height * 0.5
    bottom = [(point[0] + center[0], point[1], point[2] + center[2])
              for point in ring_points(segments, radius, bottom_y)]
    top = [(point[0] + center[0], point[1], point[2] + center[2])
           for point in ring_points(segments, radius, top_y)]
    connect_rings(builder, top, bottom, material, center_xz=(center[0], center[2]))

    for index in range(segments):
        next_index = (index + 1) % segments
        builder.add_face(
            [(center[0], top_y, center[2]), top[index], top[next_index]],
            top_material or material,
            (0.0, 1.0, 0.0),
        )
        builder.add_face(
            [(center[0], bottom_y, center[2]), bottom[index], bottom[next_index]],
            material,
            (0.0, -1.0, 0.0),
        )


def add_annulus(
    builder: ObjBuilder,
    outer_radius: float,
    inner_radius: float,
    bottom_y: float,
    top_y: float,
    material: str,
    segments: int = 16,
) -> None:
    outer_bottom = ring_points(segments, outer_radius, bottom_y)
    outer_top = ring_points(segments, outer_radius, top_y)
    inner_bottom = ring_points(segments, inner_radius, bottom_y)
    inner_top = ring_points(segments, inner_radius, top_y)
    connect_rings(builder, outer_top, outer_bottom, material)
    connect_rings(builder, inner_top, inner_bottom, material, inward=True)
    for index in range(segments):
        next_index = (index + 1) % segments
        builder.add_face(
            [outer_top[index], outer_top[next_index], inner_top[next_index], inner_top[index]],
            material,
            (0.0, 1.0, 0.0),
        )
        builder.add_face(
            [inner_bottom[index], inner_bottom[next_index], outer_bottom[next_index], outer_bottom[index]],
            material,
            (0.0, -1.0, 0.0),
        )


def add_tapered_fortress_base(
    builder: ObjBuilder,
    *,
    width: float,
    depth: float,
    top_y: float,
    bottom_y: float,
    chamfer: float,
    top_material: str = "fortress_floor",
    side_material: str = "fortress_cliff",
) -> None:
    """マグマ面へ埋め込むための厚い城塞基礎を生成する。

    上面は完全な平面、側面だけを帯状に分割して小さく揺らがせる。
    最下部まで閉じるため、半透明マグマ越しでも裏側が抜けない。
    """

    depth_span = max(top_y - bottom_y, 0.5)
    top = chamfered_rect_ring(
        width,
        depth,
        top_y,
        chamfer=chamfer,
        edge_steps=4,
    )
    shoulder = chamfered_rect_ring(
        width + 0.40,
        depth + 0.40,
        top_y - min(0.65, depth_span * 0.18),
        chamfer=chamfer + 0.08,
        edge_steps=4,
        roughness=0.035,
        phase=0.35,
    )
    cliff = chamfered_rect_ring(
        width + 1.15,
        depth + 1.10,
        top_y - depth_span * 0.58,
        chamfer=chamfer + 0.18,
        edge_steps=4,
        roughness=0.11,
        phase=1.2,
    )
    submerged = chamfered_rect_ring(
        width + 0.35,
        depth + 0.30,
        bottom_y + 0.32,
        chamfer=chamfer + 0.12,
        edge_steps=4,
        roughness=0.08,
        phase=2.3,
    )

    add_normalized_top_fan(builder, top, top_y, top_material, width, depth)
    connect_rings(builder, top, shoulder, "fortress_trim")
    connect_rings(builder, shoulder, cliff, side_material, alternate_material="basalt_side")
    connect_rings(builder, cliff, submerged, side_material, alternate_material="obsidian")
    add_bottom_fan(builder, submerged, (0.0, bottom_y, 0.0), "obsidian")
