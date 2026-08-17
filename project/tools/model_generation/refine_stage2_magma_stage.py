#!/usr/bin/env python3
"""Stage 2 の火山城塞ルート用モデルとシーンを再構築する。

通常実行では専用OBJ/MTL、Stage 2 のシーン、プレイヤー、カメラを更新する。
``--models-only`` を指定した場合はシーンJSONを変更せず、指定モデルだけを生成できる。
``--scene-only`` を指定した場合はモデルを再生成せず、シーンJSONだけを更新する。
同じ入力から同じGUIDを生成するため、何度実行しても差分が増えない。
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Iterable, Sequence


PROJECT_ROOT = Path(__file__).resolve().parents[2]
RESOURCE_ROOT = PROJECT_ROOT / "Resources"
SCENE_PATH = RESOURCE_ROOT / "json/3Dobject/stage2_object.json"
PLAYER_PATH = RESOURCE_ROOT / "json/3Dobject/stage2_player.json"
CAMERA_PATH = RESOURCE_ROOT / "json/3Dobject/stage2_camera.json"
MODEL_ROOT = RESOURCE_ROOT / "3DModel/Stages"
GUID_NAMESPACE = uuid.UUID("65767fc8-6689-45b0-bcb9-434a7e26b59b")

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


def build_magma_fortress_foundation() -> ObjBuilder:
    """広い平坦面と、マグマへ沈む厚い側壁を持つ城塞基礎。"""

    builder = ObjBuilder("magma_fortress_foundation")
    add_tapered_fortress_base(
        builder,
        width=32.0,
        depth=24.0,
        top_y=0.0,
        bottom_y=-4.8,
        chamfer=1.35,
    )

    # 壁際の低い縁石。中央の歩行面は広く空ける。
    add_box(builder, (0.0, 0.22, -11.35), (27.5, 0.44, 0.52), "fortress_trim")
    add_box(builder, (0.0, 0.22, 11.35), (27.5, 0.44, 0.52), "fortress_trim")
    return builder


def build_basalt_causeway() -> ObjBuilder:
    """城塞区画をつなぐ、長く幅広い玄武岩の土手道。"""

    builder = ObjBuilder("basalt_causeway")
    add_tapered_fortress_base(
        builder,
        width=28.0,
        depth=8.0,
        top_y=0.0,
        bottom_y=-3.2,
        chamfer=0.65,
    )
    # 橋端の金属補強。床面よりわずかに低くして移動を妨げない。
    for x in (-12.0, -6.0, 0.0, 6.0, 12.0):
        add_box(builder, (x, -0.03, 0.0), (0.26, 0.12, 7.25), "iron_hot")
    return builder


def build_metal_grate_platform() -> ObjBuilder:
    """隙間からマグマが見える、閉メッシュ部材のみの金属グレート。"""

    builder = ObjBuilder("metal_grate_platform")
    # 外周フレームと下側の梁。
    # 梁同士の境界辺を完全一致させず、各部材を独立した閉メッシュに保つ。
    add_box(builder, (0.0, -0.38, -4.65), (11.6, 0.76, 0.70), "fortress_trim")
    add_box(builder, (0.0, -0.38, 4.65), (11.6, 0.76, 0.70), "fortress_trim")
    add_box(builder, (-5.65, -0.38, 0.0), (0.70, 0.76, 8.6), "fortress_trim")
    add_box(builder, (5.65, -0.38, 0.0), (0.70, 0.76, 8.6), "fortress_trim")

    for z in (-3.0, 0.0, 3.0):
        add_box(builder, (0.0, -0.52, z), (11.0, 0.36, 0.30), "iron_hot")

    # 独立した細い桟はすべて箱として閉じる。上端はY=0で統一する。
    for index in range(13):
        x = -4.8 + index * 0.8
        add_box(builder, (x, -0.09, 0.0), (0.34, 0.18, 8.7), "iron_grate")
    return builder


def build_fortress_plaza() -> ObjBuilder:
    """段差付きの砦広場。外周床と中央壇を別の閉メッシュで構成する。"""

    builder = ObjBuilder("fortress_plaza")
    add_tapered_fortress_base(
        builder,
        width=26.0,
        depth=22.0,
        top_y=0.0,
        bottom_y=-3.8,
        chamfer=1.15,
    )

    # 中央壇と正面階段。1段15cm相当でスライムも登りやすい寸法にする。
    add_box(builder, (0.0, 0.30, 1.8), (14.0, 0.60, 10.0), "fortress_floor")
    step_depth = 1.35
    for index in range(3):
        height = 0.15 * (index + 1)
        z = -4.55 - index * step_depth
        add_box(builder, (0.0, height * 0.5, z), (14.0, height, step_depth + 0.08), "fortress_trim")

    for x in (-11.9, 11.9):
        add_box(builder, (x, 0.42, 0.0), (0.65, 0.84, 17.2), "fortress_trim")
    return builder


def build_fortress_gate() -> ObjBuilder:
    """広い開口を保った火山城塞の門。"""

    builder = ObjBuilder("fortress_gate")
    for x in (-5.4, 5.4):
        add_box(builder, (x, 4.0, 0.0), (3.0, 8.0, 3.4), "fortress_cliff")
        add_box(builder, (x, 0.40, 0.0), (4.0, 0.80, 4.2), "fortress_trim")
        add_box(builder, (x, 6.05, -1.76), (1.0, 3.1, 0.18), "iron_hot")
    add_box(builder, (0.0, 7.35, 0.0), (13.8, 2.2, 3.4), "fortress_cliff")
    add_box(builder, (0.0, 8.62, 0.0), (14.8, 0.38, 3.8), "fortress_trim")
    for x in (-6.4, -3.2, 0.0, 3.2, 6.4):
        add_box(builder, (x, 9.28, 0.0), (1.5, 1.32, 3.4), "fortress_trim")
    return builder


def build_fortress_pillar() -> ObjBuilder:
    """門・土手道・砦広場に共用できる城塞支柱。"""

    builder = ObjBuilder("fortress_pillar")
    add_box(builder, (0.0, 0.40, 0.0), (4.2, 0.80, 4.2), "fortress_trim")
    add_box(builder, (0.0, 4.55, 0.0), (2.45, 7.50, 2.45), "fortress_cliff")
    add_box(builder, (0.0, 8.55, 0.0), (3.55, 0.70, 3.55), "fortress_trim")
    for y in (2.0, 5.0, 7.6):
        add_box(builder, (0.0, y, -1.27), (2.75, 0.28, 0.16), "iron_hot")
    return builder


def build_fortress_ramp() -> ObjBuilder:
    """Scene側のZ回転で坂にする、ローカル上面が水平な城塞橋。"""

    builder = ObjBuilder("fortress_ramp")
    add_tapered_fortress_base(
        builder,
        width=30.0,
        depth=11.2,
        top_y=0.0,
        bottom_y=-3.4,
        chamfer=0.72,
    )
    # 端部の補強帯も水平面内に置き、OBBの歩行面と干渉させない。
    for x in (-12.0, -6.0, 0.0, 6.0, 12.0):
        add_box(builder, (x, -0.04, 0.0), (0.28, 0.14, 10.15), "iron_hot")
    return builder


def build_fortress_grate_drum() -> ObjBuilder:
    """回転床用の円筒金網。外周の桟はすべて独立した閉メッシュ。"""

    builder = ObjBuilder("fortress_grate_drum")
    length = 18.0
    radius = 3.05
    beam_width = 0.34
    radial_segments = 18

    # 回転軸と両端ハブ。
    add_cylinder_x(builder, (0.0, 0.0, 0.0), 0.48, 19.2, "iron_hot", 16)
    add_cylinder_x(builder, (-8.65, 0.0, 0.0), 0.86, 0.70, "fortress_trim", 16)
    add_cylinder_x(builder, (8.65, 0.0, 0.0), 0.86, 0.70, "fortress_trim", 16)

    # X方向の縦桟。外周上端が連続して見える密度にする。
    for index in range(radial_segments):
        angle = math.tau * index / radial_segments
        radial = (0.0, math.cos(angle), math.sin(angle))
        tangent = (0.0, -math.sin(angle), math.cos(angle))
        center = (0.0, radial[1] * radius, radial[2] * radius)
        add_oriented_box(
            builder,
            center,
            (length, beam_width, beam_width),
            "iron_grate",
            (1.0, 0.0, 0.0),
            radial,
            tangent,
        )

    # 周方向のリング桟。短い閉じた箱を重ねて円形を構成する。
    chord_length = math.tau * radius / radial_segments * 1.08
    for x in (-8.3, -5.5, -2.75, 0.0, 2.75, 5.5, 8.3):
        for index in range(radial_segments):
            angle = math.tau * (index + 0.5) / radial_segments
            radial = (0.0, math.cos(angle), math.sin(angle))
            tangent = (0.0, -math.sin(angle), math.cos(angle))
            center = (x, radial[1] * radius, radial[2] * radius)
            add_oriented_box(
                builder,
                center,
                (chord_length, beam_width, beam_width),
                "fortress_trim",
                tangent,
                radial,
                (1.0, 0.0, 0.0),
            )
    return builder


def build_volcanic_island() -> ObjBuilder:
    builder = ObjBuilder("volcanic_island")
    segments = 24
    rings = [
        ring_points(segments, 8.45, 0.0, x_scale=1.04, z_scale=0.96, noise=0.045),
        ring_points(segments, 9.05, -1.35, x_scale=1.03, z_scale=0.97, noise=0.055),
        ring_points(segments, 8.25, -4.2, x_scale=1.02, z_scale=0.98, noise=0.060),
        ring_points(segments, 6.75, -8.2, x_scale=1.00, z_scale=0.96, noise=0.065),
        ring_points(segments, 4.55, -12.2, x_scale=0.98, z_scale=1.02, noise=0.060),
        ring_points(segments, 2.10, -16.0, x_scale=1.02, z_scale=0.98, noise=0.050),
    ]
    add_top_fan(builder, rings[0], 0.0, "basalt_top")
    for band, (upper, lower) in enumerate(zip(rings, rings[1:])):
        material = "basalt_side" if band < 3 else "obsidian"
        alternate = "obsidian" if band < 2 else ("ember" if band == 4 else None)
        connect_rings(builder, upper, lower, material, alternate_material=alternate)
    add_bottom_fan(builder, rings[-1], (0.0, -18.0, 0.0), "magma")

    cracks = [
        [(-7.8, 0.055, -1.8), (-5.2, 0.06, -1.0), (-3.4, 0.065, -1.8), (-1.2, 0.07, -0.8)],
        [(6.9, 0.055, 2.1), (4.7, 0.06, 1.0), (3.2, 0.065, 1.7), (1.1, 0.07, 0.7)],
        [(-1.8, 0.055, 7.0), (-1.2, 0.06, 4.8), (-2.0, 0.065, 3.1)],
        [(2.8, 0.055, -7.1), (2.1, 0.06, -4.9), (3.0, 0.065, -3.0)],
    ]
    for index, crack in enumerate(cracks):
        add_surface_ribbon(builder, crack, 0.07 + index * 0.012, "ember")
    return builder


def build_obsidian_raft() -> ObjBuilder:
    builder = ObjBuilder("obsidian_raft")
    segments = 12
    top = ring_points(segments, 3.25, 0.38, x_scale=1.05, z_scale=0.95, phase=0.12, noise=0.075)
    edge = ring_points(segments, 3.55, -0.10, x_scale=1.02, z_scale=0.98, phase=0.12, noise=0.085)
    lower = ring_points(segments, 2.55, -1.75, x_scale=0.96, z_scale=1.04, phase=0.12, noise=0.080)
    tip = ring_points(segments, 0.75, -3.35, x_scale=1.10, z_scale=0.90, phase=0.12, noise=0.040)
    add_top_fan(builder, top, 0.38, "obsidian")
    connect_rings(builder, top, edge, "obsidian", alternate_material="basalt_top")
    connect_rings(builder, edge, lower, "basalt_side", alternate_material="obsidian")
    connect_rings(builder, lower, tip, "basalt_side", alternate_material="ember")
    add_bottom_fan(builder, tip, (0.0, -3.75, 0.0), "obsidian")
    add_surface_ribbon(builder, [(-2.7, 0.42, -0.7), (-0.8, 0.43, -0.2), (0.9, 0.43, -0.8)], 0.055, "ember")
    add_surface_ribbon(builder, [(0.2, 0.425, 2.5), (0.5, 0.43, 1.2), (1.4, 0.43, 0.4)], 0.05, "ember")
    return builder


def build_volcanic_spire() -> ObjBuilder:
    builder = ObjBuilder("volcanic_spire")
    segments = 20
    outer = [
        ring_points(segments, 7.8, 0.0, noise=0.055),
        ring_points(segments, 6.7, 4.2, phase=0.05, noise=0.060),
        ring_points(segments, 5.3, 8.7, phase=0.10, noise=0.055),
        ring_points(segments, 4.1, 12.6, phase=0.15, noise=0.045),
    ]
    for band, (lower, upper) in enumerate(zip(outer, outer[1:])):
        connect_rings(builder, upper, lower, "basalt_side", alternate_material="ember" if band == 1 else "obsidian")
    inner = ring_points(segments, 2.25, 11.7, phase=0.15, noise=0.025)
    for index in range(segments):
        next_index = (index + 1) % segments
        builder.add_face(
            [outer[-1][index], outer[-1][next_index], inner[next_index], inner[index]],
            "basalt_top",
            (0.0, 1.0, 0.0),
        )
    crater = ring_points(segments, 1.15, 10.4, phase=0.15)
    connect_rings(builder, inner, crater, "obsidian", inward=True, alternate_material="ember")
    add_top_fan(builder, crater, 10.42, "magma")
    add_bottom_fan(builder, outer[0], (0.0, -0.25, 0.0), "basalt_side")
    return builder


def build_volcanic_backdrop_ridge() -> ObjBuilder:
    """遠景用の層状火山岩稜線を1メッシュへまとめる。

    単純な円錐ではなく、張り出した岩棚と侵食された段丘を交互に積み、
    シルエットだけでも火山盆地の岩壁に見える形へする。発光面はスカイ
    ボックス側の細い溶岩脈へ任せ、山肌へ大きな発光ポリゴンを置かない。
    """

    builder = ObjBuilder("volcanic_backdrop_ridge")
    massif_specs = (
        (-48.0, 1.0, 20.0, 10.0, 24.0, 0.10, -2.2),
        (-29.0, -2.5, 25.0, 12.5, 39.0, 0.31, 3.4),
        (0.0, 2.0, 32.0, 15.0, 49.0, -0.14, -4.0),
        (31.0, -2.0, 27.0, 12.0, 36.0, 0.22, 3.0),
        (52.0, 2.5, 21.0, 10.5, 26.0, -0.27, -2.0),
    )
    tier_specs = (
        # height, x radius, z radius, x drift, z drift, phase, material
        (0.00, 1.08, 1.08, 0.00, 0.00, 0.00, "backdrop_near"),
        (0.08, 1.12, 1.11, -0.02, -0.01, 0.02, "backdrop_near"),
        (0.23, 0.92, 0.88, 0.04, 0.01, -0.03, "backdrop_mid"),
        (0.30, 0.98, 0.93, 0.02, 0.00, 0.04, "backdrop_mid"),
        (0.49, 0.71, 0.68, 0.10, -0.02, -0.02, "backdrop_near"),
        (0.56, 0.77, 0.73, 0.07, -0.01, 0.03, "backdrop_mid"),
        (0.73, 0.51, 0.50, 0.16, 0.02, -0.04, "backdrop_near"),
        (0.80, 0.57, 0.56, 0.12, 0.01, 0.02, "backdrop_far"),
        (0.94, 0.33, 0.34, 0.22, -0.01, -0.02, "backdrop_mid"),
        (0.98, 0.38, 0.37, 0.17, 0.00, 0.03, "backdrop_far"),
    )

    for center_x, center_z, radius_x, radius_z, height, phase, lean in massif_specs:
        rings: list[list[Vec3]] = []
        materials: list[str] = []
        for tier_index, (
            height_ratio,
            x_ratio,
            z_ratio,
            x_drift,
            z_drift,
            ring_phase,
            material,
        ) in enumerate(tier_specs):
            y = height * height_ratio
            local_center_x = center_x + radius_x * x_drift + lean * height_ratio
            local_center_z = center_z + math.sin(phase * 4.0 + tier_index * 0.61) * radius_z * z_drift
            ring = ring_points(
                16,
                1.0,
                y,
                x_scale=radius_x * x_ratio,
                z_scale=radius_z * z_ratio,
                phase=phase + ring_phase,
                noise=0.090 if height_ratio < 0.6 else 0.060,
            )
            edge_roughness = 0.0 if tier_index == len(tier_specs) - 1 else (
                0.28 + height * (0.007 if height_ratio < 0.65 else 0.004)
            )
            rings.append([
                (
                    x + local_center_x,
                    point_y + edge_roughness * (
                        math.sin(vertex_index * 1.47 + phase * 5.0)
                        + math.sin(vertex_index * 0.61 - phase * 3.0) * 0.42
                    ),
                    z + local_center_z,
                )
                for vertex_index, (x, point_y, z) in enumerate(ring)
            ])
            materials.append(material)

        for band, (lower, upper) in enumerate(zip(rings, rings[1:])):
            connect_rings(
                builder,
                upper,
                lower,
                materials[band + 1],
                center_xz=(center_x, center_z),
            )

        crown = rings[-1]
        crown_center = (
            sum(point[0] for point in crown) / len(crown),
            height * 0.982,
            sum(point[2] for point in crown) / len(crown),
        )
        for index in range(len(crown)):
            next_index = (index + 1) % len(crown)
            builder.add_face(
                [crown_center, crown[index], crown[next_index]],
                "backdrop_far",
                (0.0, 1.0, 0.0),
            )
        add_bottom_fan(
            builder,
            rings[0],
            (center_x, -1.4, center_z),
            "backdrop_near",
        )

    return builder


def build_magma_vent() -> ObjBuilder:
    builder = ObjBuilder("magma_vent")
    add_annulus(builder, 3.4, 2.05, 0.0, 0.72, "basalt_side", 16)
    add_cylinder(builder, (0.0, 0.36, 0.0), 2.02, 0.28, "magma", 16, "magma")
    for index in range(8):
        angle = math.tau * index / 8.0 + 0.2
        add_box(
            builder,
            (math.cos(angle) * 2.75, 0.72, math.sin(angle) * 2.75),
            (1.35, 0.85 + (index % 3) * 0.14, 1.0),
            "obsidian" if index % 2 else "basalt_top",
            -angle,
        )
    return builder


def build_caldera_arch() -> ObjBuilder:
    builder = ObjBuilder("caldera_arch")
    add_box(builder, (-5.1, 3.0, 0.0), (2.2, 6.0, 2.5), "basalt_side", -0.08)
    add_box(builder, (5.1, 3.0, 0.0), (2.2, 6.0, 2.5), "basalt_side", 0.08)
    for index in range(7):
        angle = math.pi * index / 6.0
        center = (math.cos(angle) * 5.0, 5.4 + math.sin(angle) * 5.0, 0.0)
        add_box(builder, center, (2.15, 1.55, 2.5), "obsidian" if index % 2 else "basalt_top", angle - math.pi * 0.5)
    add_box(builder, (0.0, 10.7, 0.0), (1.0, 0.35, 3.0), "ember", 0.0)
    return builder


def build_forge_temple() -> ObjBuilder:
    builder = ObjBuilder("forge_temple")
    add_cylinder(builder, (0.0, 0.65, 0.0), 7.2, 1.3, "basalt_side", 16, "basalt_top")
    add_cylinder(builder, (0.0, 1.55, 0.0), 5.9, 0.5, "obsidian", 16, "obsidian")
    for x in (-4.4, 4.4):
        for z in (-4.0, 4.0):
            add_cylinder(builder, (x, 4.8, z), 0.72, 6.4, "metal", 10, "ember")
    add_box(builder, (0.0, 8.25, 0.0), (11.5, 0.8, 10.5), "basalt_side")
    add_box(builder, (0.0, 9.0, 0.0), (8.8, 0.7, 8.0), "obsidian", 0.08)
    add_cylinder(builder, (0.0, 11.1, 0.0), 2.3, 4.0, "basalt_side", 12, "basalt_top")
    add_cylinder(builder, (0.0, 13.35, 0.0), 1.3, 0.5, "ember", 12, "magma")
    add_box(builder, (0.0, 4.0, 5.35), (4.2, 5.0, 0.65), "ember")
    return builder


MODEL_BUILDERS = {
    "volcanic_island": build_volcanic_island,
    "obsidian_raft": build_obsidian_raft,
    "volcanic_spire": build_volcanic_spire,
    "volcanic_backdrop_ridge": build_volcanic_backdrop_ridge,
    "magma_vent": build_magma_vent,
    "caldera_arch": build_caldera_arch,
    "forge_temple": build_forge_temple,
    "magma_fortress_foundation": build_magma_fortress_foundation,
    "basalt_causeway": build_basalt_causeway,
    "metal_grate_platform": build_metal_grate_platform,
    "fortress_plaza": build_fortress_plaza,
    "fortress_gate": build_fortress_gate,
    "fortress_pillar": build_fortress_pillar,
    "fortress_ramp": build_fortress_ramp,
    "fortress_grate_drum": build_fortress_grate_drum,
}


def quaternion_from_euler(rotation: Sequence[float]) -> list[float]:
    x, y, z = rotation
    sx, cx = math.sin(x * 0.5), math.cos(x * 0.5)
    sy, cy = math.sin(y * 0.5), math.cos(y * 0.5)
    sz, cz = math.sin(z * 0.5), math.cos(z * 0.5)
    return [
        sx * cy * cz + cx * sy * sz,
        cx * sy * cz - sx * cy * sz,
        cx * cy * sz + sx * sy * cz,
        cx * cy * cz - sx * sy * sz,
    ]


def set_transform(
    obj: dict,
    position: Sequence[float],
    scale: Sequence[float],
    rotation: Sequence[float] = (0.0, 0.0, 0.0),
) -> None:
    obj["position"] = [float(value) for value in position]
    obj["scale"] = [float(value) for value in scale]
    obj["rotation"] = [float(value) for value in rotation]
    obj["quaternion"] = quaternion_from_euler(rotation)


def set_collider(obj: dict, size: Sequence[float], center: Sequence[float], collider_type: int = 3) -> None:
    collider = obj.setdefault("collider", {})
    collider["type"] = collider_type
    collider["size"] = [float(value) for value in size]
    collider["center"] = [float(value) for value in center]
    collider["rotation"] = [0.0, 0.0, 0.0]


class SceneBuilder:
    MODEL_COLLIDERS = {
        # ColliderConfig::size は全長ではなく中心から各面までの半寸法。
        "magma_fortress_foundation": ((16.0, 2.4, 12.0), (0.0, -2.4, 0.0)),
        "basalt_causeway": ((14.0, 1.6, 4.0), (0.0, -1.6, 0.0)),
        "metal_grate_platform": ((5.8, 0.38, 5.0), (0.0, -0.38, 0.0)),
        "fortress_plaza": ((13.0, 1.9, 11.0), (0.0, -1.9, 0.0)),
        "fortress_pillar": ((2.1, 4.45, 2.1), (0.0, 4.45, 0.0)),
        "fortress_ramp": ((15.0, 1.7, 5.6), (0.0, -1.7, 0.0)),
        "fortress_grate_drum": ((9.6, 3.22, 3.22), (0.0, 0.0, 0.0)),
    }

    def __init__(self, scene: dict) -> None:
        self.scene = scene
        self.source = {obj.get("name", ""): obj for obj in scene.get("objects", [])}
        source_aliases = {
            # 生成後の名前を先に探し、旧シーンからの初回実行にも対応する。
            "Stage2_Island_StartShelf": ("Stage2_StartFoundation", "Stage2_Island_Start"),
            "Stage2_Ridge_BomberGuard": ("Stage2_EmberLandingBomber",),
            "Stage2_Cannon_EmberLanding_A": ("Stage2_EmberLandingCannon",),
            "Stage2_CalderaBase_ThunderGuard": ("Stage2_MainCalderaThunderGuard",),
            "Stage2_Ridge_FireGuard_A": ("Stage2_MainCalderaFireGuard",),
            "Stage2_Rim_FireGuard": ("Stage2_RimFireGuard",),
            "Stage2_Rim_BomberGuard": ("Stage2_RimBomberGuard",),
            "Stage2_Cannon_Rim_A": ("Stage2_RimCannon",),
            "Stage2_Final_FireGuard_A": ("Stage2_FinalFireGuard",),
            "Stage2_Final_ThunderGuard": ("Stage2_FinalThunderGuard",),
            "Stage2_Final_BomberGuard": ("Stage2_FinalBomberGuard",),
            "Stage2_RiverCoin_A_01": ("Stage2_StartCoin_01", "Stage2_RaftRouteCoin_01"),
        }
        for alias, generated_names in source_aliases.items():
            if alias in self.source:
                continue
            for generated_name in generated_names:
                if generated_name in self.source:
                    self.source[alias] = self.source[generated_name]
                    break
        self.objects: list[dict] = []

    def clone(self, source_name: str, name: str) -> dict:
        if source_name not in self.source:
            raise KeyError(f"複製元がありません: {source_name}")
        obj = copy.deepcopy(self.source[source_name])
        obj["name"] = name
        obj["guid"] = str(uuid.uuid5(GUID_NAMESPACE, name))
        obj["parentGuid"] = ""
        obj["parentName"] = ""
        self.objects.append(obj)
        return obj

    @staticmethod
    def configure_fortress_material(obj: dict) -> None:
        obj.update({
            "blendMode": 0,
            "materialType": 0,
            "color": [1.0, 1.0, 1.0, 1.0],
            "emissive": 1.03,
            "roughness": 0.48,
            "metallic": 0.18,
            "envIntensity": 1.25,
            "enableLighting": True,
            "enableNormalMap": False,
        })

    @staticmethod
    def configure_as_model(obj: dict) -> None:
        obj["type"] = "Model"
        obj["gimmickType"] = ""
        obj["enemyType"] = ""
        obj["itemType"] = ""
        obj.pop("param", None)
        obj["isStatic"] = True

    def add_fortress_model(
        self,
        name: str,
        model: str,
        position: Sequence[float],
        scale: Sequence[float],
        rotation: Sequence[float] = (0.0, 0.0, 0.0),
        collide: bool = True,
        cast_shadow: bool = True,
    ) -> dict:
        obj = self.clone("Stage2_Island_StartShelf", name)
        self.configure_as_model(obj)
        self.configure_fortress_material(obj)
        obj["modelName"] = f"Stages/{model}"
        obj["castShadow"] = bool(cast_shadow)
        set_transform(obj, position, scale, rotation)
        if collide:
            size, center = self.MODEL_COLLIDERS[model]
            set_collider(obj, size, center)
            obj["collisionAttribute"] = 4
            obj["collisionMask"] = 4294967295
        else:
            set_collider(obj, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0)
            obj["collisionAttribute"] = 0
            obj["collisionMask"] = 0
        return obj

    def add_backdrop_ridge(
        self,
        name: str,
        position: Sequence[float],
        scale: Sequence[float],
        yaw: float,
        tint: Sequence[float],
    ) -> dict:
        """プレイ領域に干渉しない遠景用岩山を追加する。"""

        obj = self.add_fortress_model(
            name,
            "volcanic_backdrop_ridge",
            position,
            scale,
            (0.0, yaw, 0.0),
            collide=False,
            cast_shadow=False,
        )
        obj.update({
            "color": list(tint),
            "emissive": 0.28,
            "roughness": 0.96,
            "metallic": 0.02,
            "envIntensity": 0.18,
        })
        return obj

    def add_foundation_skirt(
        self,
        name: str,
        center: Sequence[float],
        footprint: Sequence[float],
        yaw: float = 0.0,
    ) -> dict:
        # 上端の縁石を床面直下へ収めつつ、底をマグマ面より下へ沈める。
        floor_y = float(center[1])
        bottom_y = -9.25
        vertical_scale = (floor_y - 0.12 - bottom_y) / (4.8 + 0.44)
        top_y = bottom_y + 4.8 * vertical_scale
        return self.add_fortress_model(
            f"{name}_Substructure",
            "magma_fortress_foundation",
            (center[0], top_y, center[2]),
            (footprint[0] / 32.0, vertical_scale, footprint[1] / 24.0),
            (0.0, yaw, 0.0),
            collide=False,
            cast_shadow=False,
        )

    def add_keep(
        self,
        name: str,
        model: str,
        center: Sequence[float],
        footprint: Sequence[float],
        yaw: float = 0.0,
    ) -> dict:
        if model == "magma_fortress_foundation":
            # 基礎モデル自体の底面をマグマ面より下へ通す。中央上面はY=0なので平坦さを保てる。
            vertical_scale = (float(center[1]) + 9.25) / 4.8
            nominal_width = 32.0
            nominal_depth = 24.0
        else:
            # 広場の階段を縦に引き伸ばさず、直下の基礎だけをマグマ底へ延ばす。
            self.add_foundation_skirt(name, center, footprint, yaw)
            vertical_scale = 1.0
            nominal_width = 26.0
            nominal_depth = 22.0
        return self.add_fortress_model(
            name,
            model,
            center,
            (footprint[0] / nominal_width, vertical_scale, footprint[1] / nominal_depth),
            (0.0, yaw, 0.0),
        )

    def add_causeway(
        self,
        name: str,
        center: Sequence[float],
        footprint: Sequence[float],
        yaw: float = 0.0,
    ) -> dict:
        vertical_scale = (float(center[1]) + 9.25) / 3.2
        return self.add_fortress_model(
            name,
            "basalt_causeway",
            center,
            (footprint[0] / 28.0, vertical_scale, footprint[1] / 8.0),
            (0.0, yaw, 0.0),
        )

    def add_causeway_between(
        self,
        name: str,
        start: Sequence[float],
        end: Sequence[float],
        width: float,
    ) -> dict:
        run_x = float(end[0]) - float(start[0])
        run_z = float(end[2]) - float(start[2])
        length = math.hypot(run_x, run_z)
        if length <= 0.0:
            raise ValueError(f"土手道の距離が不正です: {name}")
        if abs(float(end[1]) - float(start[1])) > 0.05:
            raise ValueError(f"土手道の両端に高低差があります: {name}")
        center = tuple((float(start[index]) + float(end[index])) * 0.5 for index in range(3))
        yaw = -math.atan2(run_z, run_x)
        return self.add_causeway(name, center, (length, width), yaw)

    def add_ramp(
        self,
        name: str,
        start: Sequence[float],
        end: Sequence[float],
        width: float,
    ) -> dict:
        run_x = float(end[0]) - float(start[0])
        run_z = float(end[2]) - float(start[2])
        horizontal_length = math.hypot(run_x, run_z)
        if horizontal_length <= 0.0:
            raise ValueError(f"坂の水平距離が不正です: {name}")
        height_delta = float(end[1]) - float(start[1])
        slope = math.atan2(height_delta, horizontal_length)
        slope_length = math.hypot(horizontal_length, height_delta)
        yaw = -math.atan2(run_z, run_x)
        midpoint = tuple((float(start[index]) + float(end[index])) * 0.5 for index in range(3))
        ramp = self.add_fortress_model(
            name,
            "fortress_ramp",
            midpoint,
            (slope_length / 30.0, 1.0, width / 11.2),
            (0.0, yaw, slope),
        )
        # 傾斜床自体をマグマ面まで引き伸ばすとOBBも斜め下へ肥大化するため、
        # 支持構造は独立した柱で表現する。
        self.add_pillar(f"{name}_StartSupport", start, float(start[1]) - 2.9)
        self.add_pillar(f"{name}_EndSupport", end, float(end[1]) - 2.9)
        return ramp

    def add_pillar(self, name: str, position: Sequence[float], support_top_y: float) -> dict:
        base_y = -9.25
        vertical_scale = max((support_top_y - base_y) / 8.9, 0.2)
        return self.add_fortress_model(
            name,
            "fortress_pillar",
            (position[0], base_y, position[2]),
            (1.15, vertical_scale, 1.15),
            collide=False,
            cast_shadow=False,
        )

    def add_gate(self, name: str, position: Sequence[float], scale: Sequence[float]) -> dict:
        # 開口を一枚のOBBで塞がないよう、門は見た目専用にする。
        return self.add_fortress_model(
            name,
            "fortress_gate",
            position,
            scale,
            (0.0, math.pi * 0.5, 0.0),
            collide=False,
        )

    def add_stage_entry_gate(self) -> dict:
        obj = self.clone("Stage2_MagmaSea", "Stage2_EntranceGate")
        obj.update({
            "type": "Gimmick",
            "gimmickType": "StageGate",
            "enemyType": "",
            "itemType": "",
            "modelName": "Gimmicks/crown_stage_gate",
            "isStatic": False,
            "castShadow": False,
            "blendMode": 1,
            "materialType": 22,
            "color": [0.35, 0.75, 1.0, 1.0],
            "emissive": 2.0,
            "roughness": 0.5,
            "metallic": 0.0,
            "enableEnvMap": False,
            "collisionAttribute": 16,
            "collisionMask": 1,
            "eventID": 0,
            "targetID": -1,
            "myEventID": -1,
        })
        set_transform(
            obj,
            (-471.0, 4.39, -120.0),
            (2.05, 2.05, 0.94),
            (0.0, math.pi * 0.5, 0.0),
        )
        set_collider(obj, (0.72, 0.78, 0.20), (0.0, 0.15, 0.0))
        obj["param"] = {
            "actionMode": 1,
            "gimmickType": "StageGate",
            "targetScene": "SELECT",
            "startActive": True,
            "returnOnOff": True,
            "moveSpeed": 6.0,
            "speed": 1.0,
            "hp": 100.0,
            "maxHp": 100.0,
        }
        obj["waterParam"] = {
            "billboardScale": 1.48,
            "effectIntensity": 1.72,
            "effectScale": 1.08,
            "effectScaleX": 1.198,
            "effectScaleY": 1.215,
            "effectScaleZ": 0.425,
            "effectSoftness": 0.62,
            "effectType": 1.0,
            "flowSpeedX": 0.0,
            "flowSpeedY": 0.0,
            "waveFrequency": 18.0,
            "waveHeight": 1.1,
            "waveSpeed": 1.8,
        }

        gate_position = obj["position"]
        gate_yaw = float(obj["rotation"][1])
        side = (math.cos(gate_yaw), 0.0, -math.sin(gate_yaw))

        def make_visual(
            name: str,
            model_name: str,
            position: Sequence[float],
            scale: Sequence[float],
            rotation: Sequence[float],
        ) -> dict:
            visual = self.clone("Stage2_MagmaSea", name)
            self.configure_as_model(visual)
            visual.update({
                "modelName": model_name,
                "collisionAttribute": 0,
                "collisionMask": 0,
                "collider": {
                    "center": [0.0, 0.0, 0.0],
                    "rotation": [0.0, 0.0, 0.0],
                    "size": [1.0, 1.0, 1.0],
                    "type": 0,
                },
            })
            visual.pop("waterParam", None)
            set_transform(visual, position, scale, rotation)
            return visual

        pad = make_visual(
            "Stage2_EntryDecor_Pad",
            "Stages/stage_select_gate_pad",
            (gate_position[0], gate_position[1] - 2.27, gate_position[2]),
            (1.0, 1.0, 1.0),
            (0.0, 0.0, 0.0),
        )
        pad.update({
            "blendMode": 1,
            "castShadow": False,
            "color": [0.22, 0.24, 0.30, 1.0],
            "emissive": 0.18,
            "enableEnvMap": False,
            "roughness": 0.42,
        })

        frame = make_visual(
            "Stage2_EntryDecor_Frame",
            "Stages/gate",
            (gate_position[0], gate_position[1] + 0.82, gate_position[2]),
            (1.0, 1.0, 1.0),
            (0.0, gate_yaw - math.pi * 0.5, math.pi * 0.5),
        )
        frame.update({
            "blendMode": 1,
            "castShadow": True,
            "color": [1.0, 1.0, 1.0, 1.0],
            "emissive": 1.0,
            "enableEnvMap": False,
            "roughness": 0.62,
        })

        for side_index, side_sign in enumerate((-1.0, 1.0), start=1):
            offset = tuple(component * 4.16 * side_sign for component in side)
            brazier_position = (
                gate_position[0] + offset[0],
                gate_position[1] - 2.28,
                gate_position[2] + offset[2],
            )
            brazier = make_visual(
                f"Stage2_EntryDecor_Brazier_{side_index}",
                "Gimmicks/brazier",
                brazier_position,
                (1.55, 1.55, 1.55),
                (0.0, gate_yaw, 0.0),
            )
            brazier.update({
                "castShadow": True,
                "collisionAttribute": 4,
                "collisionMask": 4294967295,
                "collider": {
                    "center": [0.0, 0.52, 0.0],
                    "rotation": [0.0, 0.0, 0.0],
                    "size": [0.72, 0.55, 0.72],
                    "type": 2,
                },
                "emissive": 0.10,
                "enableEnvMap": True,
                "envIntensity": 0.18,
                "roughness": 0.72,
            })
            flame = make_visual(
                f"Stage2_EntryDecor_Flame_{side_index}",
                "Effects/flame",
                (brazier_position[0], gate_position[1] - 0.03, brazier_position[2]),
                (1.0, 1.0, 1.0),
                (0.0, 0.0, 0.0),
            )
            flame.update({
                "type": "Effect",
                "isStatic": False,
                "materialType": 11,
                "blendMode": 1,
                "castShadow": False,
                "color": [1.0, 0.20, 0.02, 0.84],
                "emissive": 2.15,
                "enableLighting": False,
                "enableEnvMap": False,
                "roughness": 0.72,
                "waterParam": {
                    "billboardScale": 0.74,
                    "effectIntensity": 0.88,
                    "effectScale": 0.96,
                    "effectScaleX": 0.90,
                    "effectScaleY": 1.15,
                    "effectScaleZ": 0.82,
                    "effectSoftness": 0.36,
                    "effectType": 4.0,
                    "flowSpeedX": 0.03 * side_sign,
                    "flowSpeedY": 0.48,
                    "waveFrequency": 2.2,
                    "waveHeight": 0.5,
                    "waveSpeed": 1.55,
                },
            })
        return obj

    def add_grate(
        self,
        name: str,
        position: Sequence[float],
        gimmick_type: str = "",
        speed: float = 0.0,
        action_mode: int = 0,
        move_amount: float = 0.0,
        rotation: Sequence[float] = (0.0, 0.0, 0.0),
        event_id: int = -1,
        start_active: bool = True,
    ) -> dict:
        if not gimmick_type:
            return self.add_fortress_model(
                name,
                "metal_grate_platform",
                position,
                (12.0 / 11.6, 1.0, 12.0 / 10.0),
            )

        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = gimmick_type
        obj["modelName"] = "Stages/metal_grate_platform"
        self.configure_fortress_material(obj)
        obj["isStatic"] = False
        set_transform(obj, position, (12.0 / 11.6, 1.0, 12.0 / 10.0), rotation)
        size, center = self.MODEL_COLLIDERS["metal_grate_platform"]
        set_collider(obj, size, center)
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        param = obj.setdefault("param", {})
        param["gimmickType"] = gimmick_type
        obj["myEventID"] = int(event_id)
        param["startActive"] = bool(start_active)
        param["returnOnOff"] = True
        if gimmick_type == "RotatingFloor":
            param["actionMode"] = int(action_mode)
            param["speed"] = float(speed)
        elif gimmick_type == "MovingFloor":
            param["actionMode"] = int(action_mode)
            param["speed"] = float(speed)
            param["moveAmount"] = float(move_amount)
        return obj

    def add_switch(self, name: str, position: Sequence[float], target_id: int) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "TimedSwitch"
        obj["modelName"] = "Gimmicks/star_dash_panel"
        obj["isStatic"] = False
        obj["castShadow"] = False
        obj["materialType"] = 24
        obj["color"] = [0.25, 0.75, 1.0, 1.0]
        obj["emissive"] = 1.0
        set_transform(obj, position, (0.9, 0.45, 0.9))
        set_collider(obj, (2.2, 0.18, 0.9), (0.0, -0.08, 0.0))
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        obj["targetID"] = int(target_id)
        obj["myEventID"] = -1
        obj.setdefault("param", {})["gimmickType"] = "TimedSwitch"
        return obj

    def add_receiver_grate(
        self,
        name: str,
        position: Sequence[float],
        event_id: int,
    ) -> dict:
        obj = self.add_grate(name, position, "EventReceiver")
        obj["myEventID"] = int(event_id)
        obj["targetID"] = -1
        obj.setdefault("param", {}).update({
            "gimmickType": "EventReceiver",
            "actionMode": 0,
            "startActive": False,
            "returnOnOff": False,
            "moveSpeed": 6.0,
        })
        return obj

    def add_breakable_block(
        self,
        name: str,
        position: Sequence[float],
        scale: Sequence[float],
    ) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "BreakableBlock"
        obj["modelName"] = "Stages/bomb_break_block"
        obj["isStatic"] = False
        obj.update({
            "texturePath": "Resources/3DModel/Stages/bomb_break_block/bomb_break_block_albedo.png",
            "normalMapPath": "Resources/3DModel/Stages/bomb_break_block/bomb_break_block_normal.png",
            "ormMapPath": "Resources/3DModel/Stages/bomb_break_block/bomb_break_block_orm.png",
            "enableNormalMap": True,
            "materialType": 0,
            "color": [1.0, 1.0, 1.0, 1.0],
            "roughness": 0.72,
            "metallic": 0.0,
        })
        set_transform(obj, position, scale)
        # bomb_break_block は一辺1mなので、ColliderConfigには半寸法0.5mを渡す。
        set_collider(obj, (0.5, 0.5, 0.5), (0.0, 0.0, 0.0))
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        obj.setdefault("param", {})["gimmickType"] = "BreakableBlock"
        return obj

    def add_dash_panel(self, name: str, position: Sequence[float], yaw: float) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "DashPanel"
        obj["modelName"] = "Gimmicks/star_dash_panel"
        obj["isStatic"] = False
        obj["castShadow"] = False
        obj.update({
            "materialType": 24,
            "color": [0.25, 0.95, 1.0, 1.0],
            "roughness": 0.62,
            "metallic": 0.56,
            "emissive": 1.0,
        })
        set_transform(obj, position, (1.35, 1.0, 1.1), (0.0, yaw, 0.0))
        set_collider(obj, (2.2, 0.18, 0.9), (0.0, -0.08, 0.0))
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        obj.setdefault("param", {})["gimmickType"] = "DashPanel"
        return obj

    def add_star_coin(self, name: str, position: Sequence[float], index: int) -> dict:
        obj = self.clone("Stage2_Island_StartShelf", name)
        self.configure_as_model(obj)
        obj["modelName"] = "Gimmicks/star"
        obj["isStatic"] = False
        obj["castShadow"] = False
        obj.update({
            "eventID": 7,
            "targetID": int(index),
            "myEventID": -1,
            "collisionAttribute": 16,
            "collisionMask": 4294967295,
            "materialType": 0,
            "color": [1.0, 0.74, 0.08, 1.0],
            "emissive": 1.65,
            "roughness": 0.32,
            "metallic": 0.18,
        })
        set_transform(obj, position, (0.05, 0.05, 0.05))
        set_collider(obj, (28.62, 5.73, 27.22), (0.0, 0.0, -2.87), 2)
        return obj

    def add_rotating_drum(
        self,
        name: str,
        position: Sequence[float],
        rotation: Sequence[float] = (0.0, 0.0, 0.0),
    ) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "RotatingFloor"
        obj["modelName"] = "Stages/fortress_grate_drum"
        self.configure_fortress_material(obj)
        obj["isStatic"] = False
        set_transform(obj, position, (1.0, 1.0, 1.0), rotation)
        size, center = self.MODEL_COLLIDERS["fortress_grate_drum"]
        set_collider(obj, size, center)
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        param = obj.setdefault("param", {})
        param.update({
            "gimmickType": "RotatingFloor",
            "actionMode": 0,
            "speed": 28.0,
            "startActive": True,
            "returnOnOff": True,
        })
        return obj

    def add_geyser(
        self,
        name: str,
        position: Sequence[float],
        target_floor_y: float,
        warning: float,
        interval: float,
        initial_delay: float,
        radius: float,
    ) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "MagmaGeyser"
        obj["modelName"] = "Stages/magma_vent"
        self.configure_fortress_material(obj)
        obj["roughness"] = 0.72
        obj["metallic"] = 0.08
        obj["isStatic"] = False
        obj["castShadow"] = False
        set_transform(obj, position, (0.82, 0.82, 0.82))
        height = float(target_floor_y) + 3.0 - float(position[1])
        set_collider(
            obj,
            (radius, height * 0.5, radius),
            (0.0, 0.72 + height * 0.5, 0.0),
            4,
        )
        obj["collisionAttribute"] = 0
        obj["collisionMask"] = 0
        obj.setdefault("param", {}).update({
            "gimmickType": "MagmaGeyser",
            "speed": 4.0,
            "shakeDuration": float(warning),
            "fallDuration": 1.15,
            "interval": float(interval),
            "moveSpeed": float(initial_delay),
            "moveAmount": height,
            "detectionRange": float(radius),
            "gravity": 5.5,
            "jumpPower": 15.0,
            "maxFallSpeed": 110.0,
            "startActive": True,
            "returnOnOff": True,
        })
        return obj

    def add_visual_pillar_pair(
        self,
        prefix: str,
        position: Sequence[float],
        support_top_y: float,
        x_offset: float = 0.0,
        z_offset: float = 0.0,
    ) -> None:
        offsets = ((-x_offset, -z_offset), (x_offset, z_offset)) if x_offset or z_offset else ((0.0, 0.0),)
        for index, (offset_x, offset_z) in enumerate(offsets, start=1):
            self.add_pillar(
                f"{prefix}_Support_{index:02d}",
                (position[0] + offset_x, position[1], position[2] + offset_z),
                support_top_y,
            )

    def add_enemy(self, source_name: str, name: str, position: Sequence[float], yaw: float = 0.0) -> dict:
        obj = self.clone(source_name, name)
        set_transform(obj, position, (1.0, 1.0, 1.0), (0.0, yaw, 0.0))
        return obj

    def add_coin(self, name: str, position: Sequence[float]) -> dict:
        obj = self.clone("Stage2_RiverCoin_A_01", name)
        obj["castShadow"] = False
        set_transform(obj, position, (0.065, 0.065, 0.065))
        return obj

def add_coin_curve(
    builder: SceneBuilder,
    prefix: str,
    start: Sequence[float],
    end: Sequence[float],
    count: int,
    arc_height: float = 0.0,
) -> None:
    for index in range(count):
        t = (index + 1) / (count + 1)
        position = [start[axis] + (end[axis] - start[axis]) * t for axis in range(3)]
        position[1] += math.sin(math.pi * t) * arc_height
        builder.add_coin(f"{prefix}_{index + 1:02d}", position)


def build_scene() -> None:
    scene = json.loads(SCENE_PATH.read_text(encoding="utf-8-sig"))
    builder = SceneBuilder(scene)

    magma = builder.clone("Stage2_MagmaSea", "Stage2_MagmaSea")
    set_transform(magma, (20.0, -10.0, 0.0), (600.0, 2.0, 255.0))
    magma["color"] = [1.0, 0.20, 0.01, 1.0]
    magma["emissive"] = 1.55
    magma["roughness"] = 0.42
    magma["isStatic"] = True
    magma["castShadow"] = False

    # スカイボックスより手前に視差を持つ岩山を置き、火山盆地の奥行きを作る。
    # 当たり判定と影を無効化し、同一メッシュを共有して描画負荷を抑える。
    backdrop_ridges = (
        ("Stage2_Backdrop_NorthWest", (-405.0, -9.2, 490.0), (1.92, 1.08, 1.82), 0.16, (0.66, 0.39, 0.27, 1.0)),
        ("Stage2_Backdrop_NorthMidA", (-105.0, -9.4, 515.0), (1.72, 0.94, 1.68), -0.10, (0.57, 0.31, 0.23, 1.0)),
        ("Stage2_Backdrop_NorthMidB", (205.0, -9.1, 500.0), (2.02, 1.14, 1.90), 0.24, (0.64, 0.36, 0.25, 1.0)),
        ("Stage2_Backdrop_NorthEast", (510.0, -9.3, 470.0), (1.80, 0.99, 1.72), -0.20, (0.54, 0.29, 0.22, 1.0)),
        ("Stage2_Backdrop_SouthWest", (-430.0, -9.1, -485.0), (1.86, 1.02, 1.78), -0.18, (0.60, 0.33, 0.23, 1.0)),
        ("Stage2_Backdrop_SouthMidA", (-120.0, -9.4, -520.0), (1.68, 0.92, 1.64), 0.12, (0.51, 0.27, 0.21, 1.0)),
        ("Stage2_Backdrop_SouthMidB", (195.0, -9.2, -510.0), (1.98, 1.11, 1.88), -0.25, (0.63, 0.35, 0.24, 1.0)),
        ("Stage2_Backdrop_SouthEast", (515.0, -9.3, -475.0), (1.78, 0.97, 1.70), 0.20, (0.56, 0.30, 0.22, 1.0)),
    )
    for name, position, scale, yaw, tint in backdrop_ridges:
        builder.add_backdrop_ridge(name, position, scale, yaw, tint)

    # 主経路を大きく蛇行させ、同じ形の広場が一直線に並ばない火山城塞にする。
    builder.add_keep(
        "Stage2_StartFoundation", "magma_fortress_foundation",
        (-430.0, 0.0, -120.0), (90.0, 62.0), 0.12,
    )
    builder.add_stage_entry_gate()

    sinking_grates = (
        ("Stage2_SinkingGrate_01", (-390.0, 0.0, -104.0)),
        ("Stage2_SinkingGrate_02", (-377.0, 1.0, -86.0)),
        ("Stage2_SinkingGrate_03", (-365.0, 2.0, -67.0)),
        ("Stage2_SinkingGrate_04", (-353.0, 3.0, -48.0)),
    )
    for name, position in sinking_grates:
        builder.add_grate(name, position, "SinkingFloor")
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)

    builder.add_keep("Stage2_GatehouseKeep", "fortress_plaza", (-330.0, 4.0, -22.0), (80.0, 58.0), -0.18)
    builder.add_gate("Stage2_GatehouseGate", (-354.0, 4.0, -43.0), (2.0, 1.45, 1.2))

    builder.add_ramp("Stage2_GatehouseRise", (-293.0, 4.0, -5.0), (-276.0, 8.0, 30.0), 16.0)
    builder.add_causeway_between("Stage2_AshCauseway", (-276.0, 8.0, 30.0), (-256.0, 8.0, 62.0), 13.0)
    builder.add_keep("Stage2_AshCourtyard", "magma_fortress_foundation", (-220.0, 8.0, 94.0), (92.0, 62.0), 0.18)

    # スターコイン1: スイッチで入口と4枚の傾斜回転床が同時に起動する時計仕掛け。
    clockwork_event = 8301
    builder.add_switch("Stage2_ClockworkSwitch", (-244.0, 8.55, 113.0), clockwork_event)
    clockwork_entry = (-224.0, 8.5, 133.0)
    builder.add_receiver_grate("Stage2_ClockworkEntry", clockwork_entry, clockwork_event)
    builder.add_visual_pillar_pair("Stage2_ClockworkEntry", clockwork_entry, 7.76)
    clockwork_floors = (
        ("Stage2_ClockworkRotator_01", (-238.0, 9.0, 148.0), 16.0, 0, (0.10, 0.0, 0.0)),
        ("Stage2_ClockworkRotator_02", (-252.0, 9.5, 160.0), -19.0, 2, (0.0, 0.0, -0.18)),
        ("Stage2_ClockworkRotator_03", (-268.0, 10.0, 168.0), 22.0, 0, (-0.14, 0.0, 0.0)),
        ("Stage2_ClockworkRotator_04", (-284.0, 10.5, 181.0), -17.0, 2, (0.0, 0.0, 0.16)),
    )
    for name, position, speed, action_mode, rotation in clockwork_floors:
        builder.add_grate(
            name, position, "RotatingFloor", speed, action_mode, 0.0,
            rotation, clockwork_event, False,
        )
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)
    builder.add_geyser("Stage2_ClockworkCoreGeyser", (-260.0, -8.0, 166.0), 10.0, 1.45, 3.4, 0.6, 2.3)
    builder.add_keep("Stage2_StarBalcony_01", "fortress_plaza", (-304.0, 10.5, 205.0), (38.0, 32.0), -0.25)
    builder.add_star_coin("Stage2_StarCoin_01", (-304.0, 14.0, 205.0), 0)

    # 灰の中庭から高所チェックポイントへ、軸の異なる床を曲線状に渡る。
    ascent_grates = (
        ("Stage2_AscentMovingGrate", (-164.0, 9.5, 116.0), "MovingFloor", 0.85, 2, 4.0, (0.0, 0.12, 0.0)),
        ("Stage2_AscentRotatingGrate", (-149.0, 11.5, 129.0), "RotatingFloor", 14.0, 2, 0.0, (0.0, 0.0, 0.08)),
        ("Stage2_AscentSeesawGrate", (-134.0, 13.5, 141.0), "SeesawFloor", 0.0, 0, 0.0, (0.0, -0.12, 0.0)),
    )
    for name, position, gimmick_type, speed, action_mode, move_amount, rotation in ascent_grates:
        builder.add_grate(name, position, gimmick_type, speed, action_mode, move_amount, rotation)
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)
    builder.add_keep("Stage2_CheckpointKeep", "fortress_plaza", (-78.0, 15.0, 158.0), (90.0, 64.0), 0.28)

    builder.add_ramp("Stage2_CheckpointDescent", (-34.0, 15.0, 142.0), (-19.0, 18.0, 116.0), 17.0)
    builder.add_causeway_between("Stage2_ObliqueCauseway", (-19.0, 18.0, 116.0), (7.0, 18.0, 80.0), 13.0)
    builder.add_keep("Stage2_EastJunction", "magma_fortress_foundation", (45.0, 18.0, 55.0), (84.0, 58.0), -0.26)

    # 南へ折り返すマグマ噴出区間。床種と方向を変えて一直線の見え方を避ける。
    hazard_grates = (
        ("Stage2_HazardRotator", (82.0, 18.0, 17.0), "RotatingFloor", 13.0, 0, 0.0, (0.08, 0.15, 0.0)),
        ("Stage2_HazardMovingGrate", (94.0, 18.5, -3.0), "MovingFloor", 0.90, 3, 4.5, (0.0, -0.18, 0.0)),
    )
    for name, position, gimmick_type, speed, action_mode, move_amount, rotation in hazard_grates:
        builder.add_grate(name, position, gimmick_type, speed, action_mode, move_amount, rotation)
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)
    builder.add_ramp("Stage2_HazardRamp", (100.0, 18.5, -12.0), (113.0, 21.0, -45.0), 12.0)
    builder.add_causeway_between("Stage2_HazardCauseway", (113.0, 21.0, -45.0), (126.0, 21.0, -76.0), 12.0)
    for args in (
        ("Stage2_HazardGeyser_01", (87.0, -8.0, 8.0), 19.0, 1.35, 3.00, 0.00, 2.15),
        ("Stage2_HazardGeyser_02", (105.0, -8.0, -28.0), 20.0, 1.20, 2.80, 0.85, 2.00),
        ("Stage2_HazardGeyser_03", (121.0, -8.0, -61.0), 21.0, 1.40, 3.25, 1.45, 2.20),
    ):
        builder.add_geyser(*args)
    builder.add_keep("Stage2_SouthBastion", "fortress_plaza", (140.0, 21.0, -105.0), (94.0, 66.0), 0.14)

    # スターコイン2: ボム能力で二段の壁を破壊しないと宝物庫へ入れない。
    builder.add_causeway_between("Stage2_BombVaultCauseway", (140.0, 21.0, -137.0), (140.0, 21.0, -188.0), 14.0)
    wall_x = (135.2, 138.4, 141.6, 144.8)
    for row, y in enumerate((22.6, 25.8), start=1):
        for column, x in enumerate(wall_x, start=1):
            builder.add_breakable_block(
                f"Stage2_BombVaultWall_{row:02d}_{column:02d}",
                (x, y, -170.0), (3.2, 3.2, 3.2),
            )
    builder.add_keep("Stage2_StarBalcony_02", "fortress_plaza", (140.0, 21.0, -205.0), (44.0, 34.0), 0.08)
    builder.add_star_coin("Stage2_StarCoin_02", (140.0, 25.0, -205.0), 1)

    builder.add_ramp("Stage2_ForgeRise", (183.0, 21.0, -120.0), (222.0, 31.0, -145.0), 18.0)
    builder.add_keep("Stage2_UpperForge", "magma_fortress_foundation", (270.0, 31.0, -165.0), (96.0, 68.0), -0.12)
    builder.add_gate("Stage2_UpperForgeGate", (226.0, 31.0, -146.0), (2.1, 1.55, 1.25))

    # 三本の回転ドラムを斜めに連ね、北側の塔へ上がる。
    drum_route = (
        ("Stage2_ForgeDrum_01", (329.0, 35.0, -106.0), -1.08, 24.0),
        ("Stage2_ForgeDrum_02", (346.0, 36.5, -79.0), -1.02, -29.0),
        ("Stage2_ForgeDrum_03", (362.0, 38.0, -52.0), -1.06, 21.0),
    )
    builder.add_ramp("Stage2_ForgeExitRamp", (314.0, 31.0, -139.0), (321.0, 35.0, -119.0), 14.0)
    for name, position, yaw, speed in drum_route:
        drum = builder.add_rotating_drum(name, position, (0.0, yaw, 0.0))
        drum["param"]["speed"] = speed
        builder.add_visual_pillar_pair(name, position, position[1] - 5.20, 5.5, 0.0)
    builder.add_keep("Stage2_NorthTower", "fortress_plaza", (375.0, 39.0, -8.0), (90.0, 64.0), 0.20)

    # スターコイン3: スイッチで北西へ曲がる出現橋を連動させ、噴出タイミングを読む。
    star_bridge_event = 8402
    builder.add_switch("Stage2_StarBridgeSwitch", (354.0, 39.55, 12.0), star_bridge_event)
    receiver_grates = (
        ("Stage2_StarBridgeReceiver_01", (359.0, 39.5, 34.0)),
        ("Stage2_StarBridgeReceiver_02", (346.0, 40.5, 53.0)),
        ("Stage2_StarBridgeReceiver_03", (334.0, 41.5, 74.0)),
        ("Stage2_StarBridgeReceiver_04", (329.0, 42.5, 97.0)),
        ("Stage2_StarBridgeReceiver_05", (335.0, 43.5, 120.0)),
        ("Stage2_StarBridgeReceiver_06", (345.0, 44.5, 143.0)),
    )
    for name, position in receiver_grates:
        builder.add_receiver_grate(name, position, star_bridge_event)
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)
    star_final_floor = (353.0, 45.0, 160.0)
    builder.add_grate("Stage2_StarBridgeMovingFinish", star_final_floor, "MovingFloor", 0.8, 2, 4.0)
    builder.add_visual_pillar_pair("Stage2_StarBridgeMovingFinish", star_final_floor, 44.26)
    for args in (
        ("Stage2_StarBridgeGeyser_01", (340.0, -8.0, 64.0), 42.0, 1.25, 3.10, 0.30, 2.00),
        ("Stage2_StarBridgeGeyser_02", (332.0, -8.0, 108.0), 44.0, 1.40, 3.35, 1.10, 2.10),
        ("Stage2_StarBridgeGeyser_03", (349.0, -8.0, 151.0), 46.0, 1.20, 2.85, 1.75, 2.05),
    ):
        builder.add_geyser(*args)
    builder.add_keep("Stage2_StarBalcony_03", "fortress_plaza", (350.0, 45.0, 183.0), (46.0, 38.0), -0.20)
    builder.add_star_coin("Stage2_StarCoin_03", (350.0, 49.0, 183.0), 2)

    # ゴールへは北東へ折れ、シーソー、横移動床、最後の坂で高さを変える。
    builder.add_causeway_between("Stage2_GoalApproachCauseway", (412.0, 39.0, 10.0), (430.0, 39.0, 39.0), 13.0)
    goal_grates = (
        ("Stage2_GoalSeesaw", (438.0, 39.5, 53.0), "SeesawFloor", 0.0, 0, 0.0, (0.0, -0.18, 0.0)),
        ("Stage2_GoalMovingGrate", (449.0, 41.0, 71.0), "MovingFloor", 0.75, 3, 4.5, (0.0, 0.12, 0.0)),
    )
    for name, position, gimmick_type, speed, action_mode, move_amount, rotation in goal_grates:
        builder.add_grate(name, position, gimmick_type, speed, action_mode, move_amount, rotation)
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)
    builder.add_ramp("Stage2_GoalRise", (456.0, 41.0, 82.0), (469.0, 45.0, 103.0), 15.0)
    builder.add_keep("Stage2_GoalKeep", "fortress_plaza", (505.0, 45.0, 130.0), (100.0, 70.0), -0.10)
    builder.add_gate("Stage2_GoalGate", (469.0, 45.0, 105.0), (2.2, 1.65, 1.3))
    builder.add_dash_panel("Stage2_GoalDashPanel", (489.0, 45.55, 119.0), 0.58)

    # 敵と砲台は回避空間のある固定床だけに置く。
    builder.add_enemy("Stage2_Start_FireSlime", "Stage2_Start_FireSlime", (-340.0, 6.0, -20.0), 1.7)
    builder.add_enemy("Stage2_Ridge_FireGuard_A", "Stage2_MainCalderaFireGuard", (-232.0, 10.0, 82.0), 1.1)
    builder.add_enemy("Stage2_CalderaBase_ThunderGuard", "Stage2_MainCalderaThunderGuard", (-80.0, 17.0, 160.0), -2.1)
    builder.add_enemy("Stage2_Rim_FireGuard", "Stage2_RimFireGuard", (39.0, 20.0, 50.0), 0.8)
    builder.add_enemy("Stage2_Ridge_BomberGuard", "Stage2_EmberLandingBomber", (118.0, 23.0, -98.0), -1.5)
    builder.add_enemy("Stage2_Rim_BomberGuard", "Stage2_RimBomberGuard", (160.0, 23.0, -112.0), -2.2)
    builder.add_enemy("Stage2_Final_ThunderGuard", "Stage2_FinalThunderGuard", (258.0, 33.0, -160.0), -2.4)
    builder.add_enemy("Stage2_Final_BomberGuard", "Stage2_FinalBomberGuard", (292.0, 33.0, -174.0), 2.7)
    builder.add_enemy("Stage2_Final_FireGuard_A", "Stage2_FinalFireGuard", (390.0, 41.0, -5.0), 0.9)

    gatehouse_cannon = builder.clone("Stage2_Cannon_EmberLanding_A", "Stage2_EmberLandingCannon")
    set_transform(gatehouse_cannon, (-311.0, 4.8, -10.0), (1.0, 1.0, 1.0), (0.0, 0.35, 0.0))
    gatehouse_cannon["param"]["interval"] = 2.5
    gatehouse_cannon["param"]["detectionRange"] = 40.0

    upper_cannon = builder.clone("Stage2_Cannon_Rim_A", "Stage2_RimCannon")
    set_transform(upper_cannon, (524.0, 45.8, 142.0), (1.0, 1.0, 1.0), (0.0, -2.25, 0.0))
    upper_cannon["param"]["interval"] = 2.8
    upper_cannon["param"]["detectionRange"] = 44.0

    goal = builder.clone("goal", "goal")
    set_transform(goal, (535.0, 48.0, 135.0), (2.8, 2.8, 2.8))

    # 通常コインは曲がり角だけを案内し、スター分岐は入口だけ示して探索性を残す。
    add_coin_curve(builder, "Stage2_StartCoin", (-465.0, 2.0, -120.0), (-395.0, 2.0, -106.0), 7)
    for index, (_, position) in enumerate(sinking_grates, start=1):
        builder.add_coin(f"Stage2_SinkingCoin_{index:02d}", (position[0], position[1] + 2.2, position[2]))
    add_coin_curve(builder, "Stage2_GatehouseCoin", (-318.0, 6.0, -4.0), (-280.0, 10.0, 29.0), 6, 1.2)
    add_coin_curve(builder, "Stage2_AshCoin", (-260.0, 10.0, 59.0), (-182.0, 10.0, 105.0), 7)
    for index, (_, position, _, _, _, _, _) in enumerate(ascent_grates, start=1):
        builder.add_coin(f"Stage2_AscentCoin_{index:02d}", (position[0], position[1] + 2.2, position[2]))
    add_coin_curve(builder, "Stage2_CheckpointCoin", (-116.0, 17.0, 153.0), (-25.0, 20.0, 124.0), 7, 1.0)
    add_coin_curve(builder, "Stage2_JunctionCoin", (4.0, 20.0, 84.0), (75.0, 20.0, 26.0), 7)
    for index, (_, position, _, _, _, _, _) in enumerate(hazard_grates, start=1):
        builder.add_coin(f"Stage2_HazardCoin_{index:02d}", (position[0], position[1] + 2.2, position[2]))
    add_coin_curve(builder, "Stage2_SouthCoin", (105.0, 21.0, -35.0), (177.0, 23.0, -112.0), 8, 1.1)
    add_coin_curve(builder, "Stage2_ForgeCoin", (190.0, 24.0, -124.0), (311.0, 33.0, -141.0), 8, 1.2)
    for index, (_, position, _, _) in enumerate(drum_route, start=1):
        builder.add_coin(f"Stage2_ForgeDrumCoin_{index:02d}", (position[0], position[1] + 4.6, position[2]))
    add_coin_curve(builder, "Stage2_NorthCoin", (370.0, 41.0, -43.0), (425.0, 41.0, 34.0), 7)
    for index, (_, position, _, _, _, _, _) in enumerate(goal_grates, start=1):
        builder.add_coin(f"Stage2_GoalGrateCoin_{index:02d}", (position[0], position[1] + 2.2, position[2]))
    add_coin_curve(builder, "Stage2_GoalCoin", (465.0, 44.0, 98.0), (535.0, 49.5, 135.0), 8, 1.2)
    add_coin_curve(builder, "Stage2_StarHint_01", (-247.0, 10.0, 117.0), (-225.0, 11.0, 132.0), 3, 0.6)
    add_coin_curve(builder, "Stage2_StarHint_02", (140.0, 23.0, -139.0), (140.0, 23.0, -158.0), 3)
    add_coin_curve(builder, "Stage2_StarHint_03", (355.0, 41.0, 14.0), (358.0, 42.0, 31.0), 3, 0.6)

    route_waypoints = [
        (-465.0, 0.0, -120.0), (-395.0, 0.0, -106.0),
        (-390.0, 0.0, -104.0), (-377.0, 1.0, -86.0), (-365.0, 2.0, -67.0),
        (-353.0, 3.0, -48.0), (-330.0, 4.0, -22.0), (-293.0, 4.0, -5.0),
        (-276.0, 8.0, 30.0), (-256.0, 8.0, 62.0), (-220.0, 8.0, 94.0),
        (-174.0, 8.0, 111.0), (-164.0, 9.5, 116.0), (-149.0, 11.5, 129.0),
        (-134.0, 13.5, 141.0), (-78.0, 15.0, 158.0), (-34.0, 15.0, 142.0),
        (-19.0, 18.0, 116.0), (7.0, 18.0, 80.0), (45.0, 18.0, 55.0),
        (82.0, 18.0, 17.0), (94.0, 18.5, -3.0), (100.0, 18.5, -12.0),
        (113.0, 21.0, -45.0), (126.0, 21.0, -76.0), (140.0, 21.0, -105.0),
        (183.0, 21.0, -120.0), (222.0, 31.0, -145.0), (270.0, 31.0, -165.0),
        (314.0, 31.0, -139.0), (321.0, 35.0, -119.0), (329.0, 35.0, -106.0),
        (346.0, 36.5, -79.0), (362.0, 38.0, -52.0), (375.0, 39.0, -8.0),
        (412.0, 39.0, 10.0), (430.0, 39.0, 39.0), (438.0, 39.5, 53.0),
        (449.0, 41.0, 71.0), (456.0, 41.0, 82.0), (469.0, 45.0, 103.0),
        (505.0, 45.0, 130.0), (535.0, 48.0, 135.0),
    ]
    route_length = sum(math.dist(start, end) for start, end in zip(route_waypoints, route_waypoints[1:]))
    if not 1080.0 <= route_length <= 1450.0:
        raise ValueError(f"Stage 2 の主経路長が想定外です: {route_length:.1f} m")

    route_z = [point[2] for point in route_waypoints]
    branch_z = route_z + [205.0, -205.0, 183.0]
    if max(branch_z) - min(branch_z) < 360.0:
        raise ValueError("Stage 2 の横方向利用が不足しています")

    names = [obj.get("name", "") for obj in builder.objects]
    guids = [obj.get("guid", "") for obj in builder.objects]
    if len(names) != len(set(names)):
        raise ValueError("Stage 2 に重複したオブジェクト名があります")
    if len(guids) != len(set(guids)):
        raise ValueError("Stage 2 に重複したGUIDがあります")

    forbidden_gimmicks = {"LaunchStar", "ChainCollapseFloor"}
    present_forbidden = sorted({obj.get("gimmickType", "") for obj in builder.objects} & forbidden_gimmicks)
    if present_forbidden:
        raise ValueError(f"必須経路に禁止ギミックがあります: {', '.join(present_forbidden)}")

    required_gimmicks = {
        "SinkingFloor", "MovingFloor", "RotatingFloor", "SeesawFloor",
        "TimedSwitch", "EventReceiver", "BreakableBlock", "DashPanel", "MagmaGeyser",
    }
    gimmick_names = {obj.get("gimmickType", "") for obj in builder.objects}
    missing_gimmicks = sorted(required_gimmicks - gimmick_names)
    if missing_gimmicks:
        raise ValueError(f"Stage 2 の必須ギミックが不足しています: {', '.join(missing_gimmicks)}")

    star_coins = [obj for obj in builder.objects if obj.get("eventID") == 7]
    if sorted(obj.get("targetID") for obj in star_coins) != [0, 1, 2]:
        raise ValueError("Stage 2 のスターコイン番号は0、1、2を1枚ずつ指定してください")
    star_positions = [obj["position"] for obj in sorted(star_coins, key=lambda obj: obj["targetID"])]
    if max(position[0] for position in star_positions) - min(position[0] for position in star_positions) < 600.0:
        raise ValueError("Stage 2 のスターコインが左右へ十分に分散していません")

    linked_rotators = [
        obj for obj in builder.objects
        if obj.get("gimmickType") == "RotatingFloor" and obj.get("myEventID") == clockwork_event
    ]
    bomb_walls = [obj for obj in builder.objects if obj.get("name", "").startswith("Stage2_BombVaultWall_")]
    linked_bridge = [obj for obj in builder.objects if obj.get("myEventID") == star_bridge_event]
    if len(linked_rotators) != 4 or len(bomb_walls) != 8 or len(linked_bridge) < 6:
        raise ValueError("Stage 2 のスターコイン攻略ギミックが不足しています")

    forbidden_route_models = {"Stages/volcanic_island", "Stages/obsidian_raft"}
    present_old_models = sorted({obj.get("modelName", "") for obj in builder.objects} & forbidden_route_models)
    if present_old_models:
        raise ValueError(f"旧火山足場モデルが残っています: {', '.join(present_old_models)}")

    required_models = {
        "Stages/magma_fortress_foundation", "Stages/basalt_causeway",
        "Stages/metal_grate_platform", "Stages/fortress_plaza", "Stages/fortress_gate",
        "Stages/fortress_pillar", "Stages/fortress_ramp", "Stages/fortress_grate_drum",
        "Stages/volcanic_backdrop_ridge", "Stages/magma_vent",
        "Stages/bomb_break_block", "Gimmicks/star", "Gimmicks/crown_stage_gate",
        "Gimmicks/star_dash_panel", "Stages/stage_select_gate_pad", "Stages/gate",
        "Gimmicks/brazier", "Effects/flame",
    }
    model_names = {obj.get("modelName", "") for obj in builder.objects}
    missing_models = sorted(required_models - model_names)
    if missing_models:
        raise ValueError(f"Stage 2 の必須モデル参照が不足しています: {', '.join(missing_models)}")

    entrance_gate = next(
        (obj for obj in builder.objects if obj.get("name") == "Stage2_EntranceGate"),
        None,
    )
    entrance_param = entrance_gate.get("param", {}) if entrance_gate else {}
    if (
        not entrance_gate
        or entrance_gate.get("gimmickType") != "StageGate"
        or entrance_gate.get("modelName") != "Gimmicks/crown_stage_gate"
        or entrance_param.get("actionMode") != 1
        or entrance_param.get("targetScene") != "SELECT"
    ):
        raise ValueError("Stage 2 の入口ゲート設定が不正です")

    expected_entry_models = {
        "Stage2_EntryDecor_Pad": "Stages/stage_select_gate_pad",
        "Stage2_EntryDecor_Frame": "Stages/gate",
        "Stage2_EntryDecor_Brazier_1": "Gimmicks/brazier",
        "Stage2_EntryDecor_Brazier_2": "Gimmicks/brazier",
        "Stage2_EntryDecor_Flame_1": "Effects/flame",
        "Stage2_EntryDecor_Flame_2": "Effects/flame",
    }
    objects_by_name = {entry.get("name", ""): entry for entry in builder.objects}
    for name, model_name in expected_entry_models.items():
        if objects_by_name.get(name, {}).get("modelName") != model_name:
            raise ValueError(f"{name} の入口ゲート外装が不足または不正です")

    backdrop_objects = [
        obj for obj in builder.objects
        if obj.get("name", "").startswith("Stage2_Backdrop_")
    ]
    if len(backdrop_objects) != 8:
        raise ValueError("Stage 2 の遠景岩山は外周8か所に配置してください")
    if any(
        obj.get("collider", {}).get("type") != 0
        or obj.get("collisionAttribute") != 0
        or obj.get("collisionMask") != 0
        or obj.get("castShadow") is not False
        for obj in backdrop_objects
    ):
        raise ValueError("Stage 2 の遠景岩山に当たり判定または影が設定されています")

    scene["objects"] = builder.objects
    SCENE_PATH.write_text(
        json.dumps(scene, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def update_player_and_camera() -> None:
    player_json = json.loads(PLAYER_PATH.read_text(encoding="utf-8-sig"))
    player = player_json["objects"][0]
    set_transform(player, (-465.0, 2.2, -120.0), (1.0, 1.0, 1.0), (0.0, math.pi * 0.5, 0.0))
    PLAYER_PATH.write_text(
        json.dumps(player_json, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    camera_json = json.loads(CAMERA_PATH.read_text(encoding="utf-8-sig"))
    camera = camera_json["objects"][0]
    set_transform(camera, (-486.0, 16.0, -142.0), (1.0, 1.0, 1.0), (-0.22, 0.82, 0.0))
    CAMERA_PATH.write_text(
        json.dumps(camera_json, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def validate_generated_models(names: Iterable[str]) -> None:
    for name in names:
        obj_path = MODEL_ROOT / name / f"{name}.obj"
        lines = obj_path.read_text(encoding="utf-8").splitlines()
        vertex_count = sum(line.startswith("v ") for line in lines)
        uv_count = sum(line.startswith("vt ") for line in lines)
        normal_count = sum(line.startswith("vn ") for line in lines)
        face_count = sum(line.startswith("f ") for line in lines)
        if min(vertex_count, uv_count, normal_count, face_count) <= 0:
            raise ValueError(f"生成モデルの属性が不足しています: {name}")
        if not all("/" in line for line in lines if line.startswith("f ")):
            raise ValueError(f"UV/法線参照のない面があります: {name}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Stage 2 火山城塞アセット生成")
    mode_group = parser.add_mutually_exclusive_group()
    mode_group.add_argument(
        "--models-only",
        action="store_true",
        help="シーン、プレイヤー、カメラを変更せずモデルだけを生成する",
    )
    mode_group.add_argument(
        "--scene-only",
        action="store_true",
        help="モデル、プレイヤー、カメラを変更せずシーンだけを生成する",
    )
    parser.add_argument(
        "--model",
        action="append",
        choices=sorted(MODEL_BUILDERS),
        help="生成するモデル名。複数指定可能。省略時は全モデルを生成する",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    selected_names = args.model or list(MODEL_BUILDERS)
    if not args.scene_only:
        for name in selected_names:
            MODEL_BUILDERS[name]().write(MODEL_ROOT / name)
        validate_generated_models(selected_names)

    if args.scene_only:
        build_scene()
        print("Stage 2 のシーンを更新しました。")
    elif not args.models_only:
        build_scene()
        update_player_and_camera()
        print("Stage 2 のモデル、シーン、プレイヤー、カメラを更新しました。")
    else:
        print(f"Stage 2 モデルを生成しました: {', '.join(selected_names)}")


if __name__ == "__main__":
    main()
