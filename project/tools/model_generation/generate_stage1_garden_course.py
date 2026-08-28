#!/usr/bin/env python3
"""Stage 1用の立体的な庭園遺跡モデルを生成する。"""

from __future__ import annotations

import argparse
import json
import math
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[2]
MODEL_ROOT = ROOT / "Resources/3DModel/Stages"
GUID_NAMESPACE = uuid.UUID("69564a95-05ab-4fc5-9b0a-b0fb95ff0cbd")


Vec3 = tuple[float, float, float]


@dataclass
class Mesh:
    name: str
    # ワールド座標を直接埋め込む地形は、Assimpの左手系変換でZが反転する前提で
    # OBJ側を右手系へ事前変換する。ローカル原点中心の汎用モデルは従来どおり出力する。
    preconvert_engine_space: bool = False
    vertices: list[Vec3] = field(default_factory=list)
    texcoords: list[tuple[float, float]] = field(default_factory=list)
    normals: list[Vec3] = field(default_factory=list)
    faces: list[tuple[str, list[tuple[int, int, int]]]] = field(default_factory=list)

    def add_face(
        self,
        points: Iterable[Vec3],
        material: str,
        normal: Vec3 | None = None,
        uvs: Iterable[tuple[float, float]] | None = None,
    ) -> None:
        polygon = list(points)
        if len(polygon) < 3:
            return
        polygon_uvs = list(uvs) if uvs is not None else None
        if polygon_uvs is not None and len(polygon_uvs) != len(polygon):
            raise ValueError("頂点数とUV数が一致していません。")
        if normal is None:
            normal = face_normal(polygon[0], polygon[1], polygon[2])
        normal_index = len(self.normals) + 1
        self.normals.append(normal)
        indices: list[tuple[int, int, int]] = []
        for index, point in enumerate(polygon):
            vertex_index = len(self.vertices) + 1
            self.vertices.append(point)
            self.texcoords.append(polygon_uvs[index] if polygon_uvs is not None else project_uv(point, normal))
            indices.append((vertex_index, vertex_index, normal_index))
        self.faces.append((material, indices))

    def write(self, output_dir: Path) -> None:
        output_dir.mkdir(parents=True, exist_ok=True)
        obj_path = output_dir / f"{self.name}.obj"
        lines = [f"mtllib {self.name}.mtl", f"o {self.name}"]
        z_sign = -1.0 if self.preconvert_engine_space else 1.0
        lines.extend(f"v {x:.6f} {y:.6f} {z * z_sign:.6f}" for x, y, z in self.vertices)
        lines.extend(f"vt {u:.6f} {v:.6f}" for u, v in self.texcoords)
        lines.extend(f"vn {x:.6f} {y:.6f} {z * z_sign:.6f}" for x, y, z in self.normals)
        current_material = ""
        for material, indices in self.faces:
            if material != current_material:
                lines.append(f"usemtl {material}")
                current_material = material
            lines.append("f " + " ".join(f"{v}/{vt}/{vn}" for v, vt, vn in indices))
        obj_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")

        mtl_path = output_dir / f"{self.name}.mtl"
        mtl_path.write_text(MATERIAL_LIBRARY, encoding="utf-8", newline="\n")
        write_meta(obj_path, "Model", "ModelImporter", {"generateTangents": True, "scale": 1.0})
        write_meta(mtl_path, "Binary", "BinaryImporter", {})


def face_normal(a: Vec3, b: Vec3, c: Vec3) -> Vec3:
    ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    cross = (
        ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0],
    )
    length = math.sqrt(sum(value * value for value in cross))
    if length < 1.0e-7:
        return (0.0, 1.0, 0.0)
    return tuple(value / length for value in cross)


def project_uv(point: Vec3, normal: Vec3) -> tuple[float, float]:
    ax, ay, az = (abs(value) for value in normal)
    if ay >= ax and ay >= az:
        return (point[0] * 0.08, point[2] * 0.08)
    if ax >= az:
        return (point[2] * 0.08, point[1] * 0.08)
    return (point[0] * 0.08, point[1] * 0.08)


def write_meta(path: Path, asset_type: str, importer: str, settings: dict[str, object]) -> None:
    relative = path.relative_to(ROOT).as_posix()
    data = {
        "assetType": asset_type,
        "guid": uuid.uuid5(GUID_NAMESPACE, relative).hex,
        "importSettings": settings,
        "importer": importer,
        "source": relative,
        "version": 1,
    }
    Path(f"{path}.meta").write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def add_oriented_box(
    mesh: Mesh,
    center: Vec3,
    half_size: Vec3,
    yaw: float,
    top_material: str,
    side_material: str | None = None,
    bottom_material: str | None = None,
) -> None:
    side_material = side_material or top_material
    bottom_material = bottom_material or side_material
    cx, cy, cz = center
    hx, hy, hz = half_size
    cosine = math.cos(yaw)
    sine = math.sin(yaw)

    def transform(x: float, y: float, z: float) -> Vec3:
        return (
            cx + x * cosine + z * sine,
            cy + y,
            cz - x * sine + z * cosine,
        )

    p = {
        "lbf": transform(-hx, -hy, -hz),
        "rbf": transform(hx, -hy, -hz),
        "rtf": transform(hx, hy, -hz),
        "ltf": transform(-hx, hy, -hz),
        "lbb": transform(-hx, -hy, hz),
        "rbb": transform(hx, -hy, hz),
        "rtb": transform(hx, hy, hz),
        "ltb": transform(-hx, hy, hz),
    }
    # 各面を外側から見て反時計回りに並べ、照明用法線を外向きへそろえる。
    mesh.add_face([p["ltb"], p["rtb"], p["rtf"], p["ltf"]], top_material)
    mesh.add_face([p["lbf"], p["rbf"], p["rbb"], p["lbb"]], bottom_material)
    mesh.add_face([p["ltf"], p["rtf"], p["rbf"], p["lbf"]], side_material)
    mesh.add_face([p["rtb"], p["ltb"], p["lbb"], p["rbb"]], side_material)
    mesh.add_face([p["ltb"], p["ltf"], p["lbf"], p["lbb"]], side_material)
    mesh.add_face([p["rtf"], p["rtb"], p["rbb"], p["rbf"]], side_material)


def add_box(
    mesh: Mesh,
    center: Vec3,
    half_size: Vec3,
    top_material: str,
    side_material: str | None = None,
    bottom_material: str | None = None,
) -> None:
    add_oriented_box(mesh, center, half_size, 0.0, top_material, side_material, bottom_material)


def add_polygon_slab(
    mesh: Mesh,
    outline: list[tuple[float, float]],
    top_y: float,
    bottom_y: float,
    top_material: str,
    side_material: str,
    bottom_material: str = "garden_underside",
) -> None:
    center_x = sum(point[0] for point in outline) / len(outline)
    center_z = sum(point[1] for point in outline) / len(outline)
    top_center = (center_x, top_y, center_z)
    bottom_center = (center_x, bottom_y, center_z)
    for index in range(len(outline)):
        next_index = (index + 1) % len(outline)
        x0, z0 = outline[index]
        x1, z1 = outline[next_index]
        mesh.add_face([top_center, (x0, top_y, z0), (x1, top_y, z1)], top_material, (0.0, 1.0, 0.0))
        mesh.add_face(
            [(x1, bottom_y, z1), (x0, bottom_y, z0), bottom_center],
            bottom_material,
            (0.0, -1.0, 0.0),
        )
        mesh.add_face(
            [(x1, top_y, z1), (x0, top_y, z0), (x0, bottom_y, z0), (x1, bottom_y, z1)],
            side_material,
        )


def transform_outline(
    outline: Iterable[tuple[float, float]],
    center: tuple[float, float],
    yaw: float,
) -> list[tuple[float, float]]:
    cosine = math.cos(yaw)
    sine = math.sin(yaw)
    return [
        (
            center[0] + x * cosine + z * sine,
            center[1] - x * sine + z * cosine,
        )
        for x, z in outline
    ]


def add_arrow_slab(
    mesh: Mesh,
    center: tuple[float, float],
    yaw: float,
    length: float,
    width: float,
    top_y: float,
    bottom_y: float,
    material: str,
    side_material: str = "ruin_cap",
) -> None:
    """床面から遠目でも読める、太い矢印レリーフを追加する。"""
    half_length = length * 0.5
    half_width = width * 0.5
    neck_x = length * 0.10
    shaft_half_width = width * 0.20
    shaft = [
        (-half_length, -shaft_half_width),
        (neck_x, -shaft_half_width),
        (neck_x, shaft_half_width),
        (-half_length, shaft_half_width),
    ]
    head = [
        (neck_x, -half_width),
        (half_length, 0.0),
        (neck_x, half_width),
    ]
    add_polygon_slab(
        mesh,
        transform_outline(shaft, center, yaw),
        top_y,
        bottom_y,
        material,
        side_material,
        side_material,
    )
    add_polygon_slab(
        mesh,
        transform_outline(head, center, yaw),
        top_y,
        bottom_y,
        material,
        side_material,
        side_material,
    )


def add_double_arrow_slab(
    mesh: Mesh,
    center: tuple[float, float],
    yaw: float,
    length: float,
    width: float,
    top_y: float,
    bottom_y: float,
    material: str,
    side_material: str = "ruin_cap",
) -> None:
    """往復運動を示す、両端に矢尻を持つレリーフを追加する。"""
    half_length = length * 0.5
    half_width = width * 0.5
    head_length = length * 0.22
    shaft_half_width = width * 0.20
    shaft = [
        (-half_length + head_length, -shaft_half_width),
        (half_length - head_length, -shaft_half_width),
        (half_length - head_length, shaft_half_width),
        (-half_length + head_length, shaft_half_width),
    ]
    left_head = [
        (-half_length, 0.0),
        (-half_length + head_length, -half_width),
        (-half_length + head_length, half_width),
    ]
    right_head = [
        (half_length - head_length, -half_width),
        (half_length, 0.0),
        (half_length - head_length, half_width),
    ]
    for outline in (shaft, left_head, right_head):
        add_polygon_slab(
            mesh,
            transform_outline(outline, center, yaw),
            top_y,
            bottom_y,
            material,
            side_material,
            side_material,
        )


def add_ring_slab(
    mesh: Mesh,
    center: tuple[float, float],
    radius_x: float,
    radius_z: float,
    thickness: float,
    top_y: float,
    bottom_y: float,
    material: str,
    segments: int = 16,
    yaw: float = 0.0,
) -> None:
    """中央が抜けた楕円リングを作り、鎖やスプリングを実形状で表現する。"""
    inner_x = max(0.05, radius_x - thickness)
    inner_z = max(0.05, radius_z - thickness)
    cosine = math.cos(yaw)
    sine = math.sin(yaw)

    def point(rx: float, rz: float, angle: float, y: float) -> Vec3:
        x = math.cos(angle) * rx
        z = math.sin(angle) * rz
        return (
            center[0] + x * cosine + z * sine,
            y,
            center[1] - x * sine + z * cosine,
        )

    for index in range(segments):
        angle0 = math.tau * index / segments
        angle1 = math.tau * (index + 1) / segments
        outer0_top = point(radius_x, radius_z, angle0, top_y)
        outer1_top = point(radius_x, radius_z, angle1, top_y)
        inner0_top = point(inner_x, inner_z, angle0, top_y)
        inner1_top = point(inner_x, inner_z, angle1, top_y)
        outer0_bottom = point(radius_x, radius_z, angle0, bottom_y)
        outer1_bottom = point(radius_x, radius_z, angle1, bottom_y)
        inner0_bottom = point(inner_x, inner_z, angle0, bottom_y)
        inner1_bottom = point(inner_x, inner_z, angle1, bottom_y)
        mesh.add_face([outer0_top, outer1_top, inner1_top, inner0_top], material, (0.0, 1.0, 0.0))
        mesh.add_face([outer1_bottom, outer0_bottom, inner0_bottom, inner1_bottom], material, (0.0, -1.0, 0.0))
        mesh.add_face([outer1_top, outer0_top, outer0_bottom, outer1_bottom], material)
        mesh.add_face([inner0_top, inner1_top, inner1_bottom, inner0_bottom], material)


def add_crack_line(
    mesh: Mesh,
    points: Iterable[tuple[float, float]],
    y: float,
    width: float,
    material: str = "hazard_dark",
) -> None:
    """崩落を予告する太い亀裂線を床面へ追加する。"""
    line = list(points)
    for start, end in zip(line, line[1:]):
        dx = end[0] - start[0]
        dz = end[1] - start[1]
        length = math.hypot(dx, dz)
        if length <= 1.0e-5:
            continue
        add_oriented_box(
            mesh,
            ((start[0] + end[0]) * 0.5, y, (start[1] + end[1]) * 0.5),
            (length * 0.5, 0.025, width * 0.5),
            math.atan2(-dz, dx),
            material,
            material,
            material,
        )


def add_wall(
    mesh: Mesh,
    start: tuple[float, float],
    end: tuple[float, float],
    base_y: float,
    height: float,
    thickness: float = 2.2,
    material: str = "ruin_stone",
) -> None:
    dx = end[0] - start[0]
    dz = end[1] - start[1]
    length = math.hypot(dx, dz)
    yaw = math.atan2(-dz, dx)
    add_oriented_box(
        mesh,
        ((start[0] + end[0]) * 0.5, base_y + height * 0.5, (start[1] + end[1]) * 0.5),
        (length * 0.5, height * 0.5, thickness * 0.5),
        yaw,
        "ruin_cap",
        material,
        material,
    )


def add_arch(
    mesh: Mesh,
    center: tuple[float, float],
    yaw: float,
    base_y: float,
    width: float = 8.0,
    height: float = 8.0,
    depth: float = 2.8,
) -> None:
    cosine = math.cos(yaw)
    sine = math.sin(yaw)

    def offset(side: float, up: float) -> Vec3:
        return (
            center[0] + cosine * side,
            base_y + up,
            center[1] - sine * side,
        )

    pillar_width = 1.8
    add_oriented_box(mesh, offset(-(width - pillar_width) * 0.5, (height - 1.6) * 0.5), (pillar_width * 0.5, (height - 1.6) * 0.5, depth * 0.5), yaw, "ruin_cap", "ruin_stone")
    add_oriented_box(mesh, offset((width - pillar_width) * 0.5, (height - 1.6) * 0.5), (pillar_width * 0.5, (height - 1.6) * 0.5, depth * 0.5), yaw, "ruin_cap", "ruin_stone")
    add_oriented_box(mesh, offset(0.0, height - 0.8), (width * 0.5, 0.8, depth * 0.5), yaw, "ruin_cap", "ruin_stone")


def add_steps(
    mesh: Mesh,
    start: Vec3,
    count: int,
    step_width: float,
    step_depth: float,
    rise: float,
    yaw: float,
) -> None:
    forward = (math.sin(yaw), 0.0, math.cos(yaw))
    for index in range(count):
        height = rise * (index + 1)
        center = (
            start[0] + forward[0] * step_depth * index,
            start[1] + height * 0.5,
            start[2] + forward[2] * step_depth * index,
        )
        add_oriented_box(
            mesh,
            center,
            (step_width * 0.5, height * 0.5, step_depth * 0.54),
            yaw,
            "ruin_path",
            "garden_cliff_warm",
            "garden_underside",
        )


def build_entry_courtyard() -> Mesh:
    mesh = Mesh("star_garden_entry_courtyard")
    outline = [(-45.0, -29.0), (-30.0, -37.0), (19.0, -36.0), (43.0, -22.0), (46.0, 13.0), (29.0, 31.0), (-24.0, 34.0), (-46.0, 17.0)]
    add_polygon_slab(mesh, outline, 0.0, -13.0, "garden_grass", "garden_cliff_warm")
    add_wall(mesh, (-43.0, -28.0), (-43.0, -7.0), 0.0, 5.0)
    add_wall(mesh, (-43.0, 7.0), (-43.0, 17.0), 0.0, 5.0)
    add_wall(mesh, (-36.0, 27.0), (-12.0, 33.0), 0.0, 3.8)
    add_wall(mesh, (10.0, 31.0), (31.0, 25.0), 0.0, 3.8)
    add_wall(mesh, (40.0, -20.0), (43.0, -5.0), 0.0, 4.8)
    add_wall(mesh, (43.0, 8.0), (39.0, 19.0), 0.0, 4.8)
    add_arch(mesh, (-43.0, 0.0), math.pi * 0.5, 0.0, 9.5, 9.0)
    add_arch(mesh, (42.0, 1.0), math.pi * 0.5, 0.0, 9.5, 8.0)
    add_box(mesh, (0.0, 0.18, -2.0), (15.0, 0.18, 10.0), "ruin_path", "ruin_stone")
    return mesh


def build_cliff_pass() -> Mesh:
    mesh = Mesh("star_garden_cliff_pass")
    lower_outline = [(-43.0, -34.0), (5.0, -34.0), (9.0, -8.0), (10.0, 15.0), (-8.0, 44.0), (-31.0, 40.0), (-25.0, 7.0), (-43.0, 3.0)]
    add_polygon_slab(mesh, lower_outline, 0.0, -13.0, "garden_grass_dark", "garden_cliff_rose")
    upper_outline = [(-10.0, 14.0), (46.0, 11.0), (48.0, 45.0), (-10.0, 45.0)]
    add_polygon_slab(mesh, upper_outline, 8.0, -13.0, "garden_grass", "garden_cliff_warm")

    # 低層通路を挟む高い崖壁。南側にはボムで開く洞窟のくぼみを作る。
    add_wall(mesh, (-42.0, -33.0), (-41.0, 2.0), 0.0, 10.0, 3.0, "cliff_moss")
    add_wall(mesh, (6.0, -33.0), (7.0, -12.0), 0.0, 12.0, 3.0, "cliff_moss")
    add_wall(mesh, (8.0, 0.0), (9.0, 13.0), 0.0, 12.0, 3.0, "cliff_moss")
    add_wall(mesh, (-40.0, 3.0), (-27.0, 7.0), 0.0, 8.0, 2.8, "cliff_moss")
    add_wall(mesh, (-24.0, 8.0), (-27.0, 35.0), 0.0, 9.0, 2.8, "cliff_moss")

    # 洞窟は地形の背面に埋め込み、独立した箱に見えないようにする。
    add_box(mesh, (-18.0, -0.55, -48.0), (17.0, 0.55, 11.0), "cave_floor", "garden_cliff_rose")
    add_wall(mesh, (-35.0, -59.0), (0.0, -59.0), 0.0, 8.5, 3.0, "cave_rock")
    add_wall(mesh, (-35.0, -59.0), (-35.0, -38.0), 0.0, 8.5, 3.0, "cave_rock")
    add_wall(mesh, (0.0, -59.0), (0.0, -38.0), 0.0, 8.5, 3.0, "cave_rock")
    # 天井の上にも草地を載せ、空中に置かれた暗い箱へ見えないようにする。
    add_box(mesh, (-18.0, 8.2, -48.0), (18.5, 1.2, 12.5), "garden_grass_dark", "cave_rock")
    # ボム壁は2.55m角のブロックを5列×3段で隙間なく収める。
    # 開口寸法をブロック実寸から決め、固定石枠と洞窟天井を連続させる。
    bomb_block_size = 2.55
    bomb_wall_columns = 5
    bomb_wall_rows = 3
    bomb_opening_width = bomb_block_size * bomb_wall_columns
    bomb_opening_height = bomb_block_size * bomb_wall_rows
    bomb_mouth_x = -18.0
    bomb_mouth_z = -37.7
    jamb_width = 2.5
    frame_depth = 3.6
    lintel_height = 2.4
    frame_half_width = (bomb_opening_width + jamb_width * 2.0) * 0.5

    for side in (-1.0, 1.0):
        jamb_x = bomb_mouth_x + side * (bomb_opening_width + jamb_width) * 0.5
        add_box(
            mesh,
            (jamb_x, bomb_opening_height * 0.5, bomb_mouth_z),
            (jamb_width * 0.5, bomb_opening_height * 0.5, frame_depth * 0.5),
            "ruin_cap",
            "cave_rock",
            "cave_rock",
        )
    add_box(
        mesh,
        (bomb_mouth_x, bomb_opening_height + lintel_height * 0.5, bomb_mouth_z),
        (frame_half_width, lintel_height * 0.5, frame_depth * 0.5),
        "ruin_cap",
        "cave_rock",
        "cave_rock",
    )
    # 洞窟奥のスターは床へ直置きせず、7.2m幅の二段石台座へ載せる。
    star_pedestal_center = (bomb_mouth_x, 0.56, -51.0)
    add_box(
        mesh,
        star_pedestal_center,
        (3.6, 0.56, 2.8),
        "ruin_path",
        "ruin_stone",
        "cave_rock",
    )
    add_box(
        mesh,
        (star_pedestal_center[0], 1.23, star_pedestal_center[2]),
        (2.8, 0.11, 2.1),
        "goal_dais",
        "ruin_cap",
        "ruin_stone",
    )

    add_steps(mesh, (-1.0, 0.0, 5.0), 8, 7.2, 4.7, 1.0, 0.0)
    add_wall(mesh, (12.0, 14.0), (12.0, 42.0), 0.0, 7.4, 2.5, "ruin_stone")
    add_wall(mesh, (45.0, 13.0), (46.0, 31.0), 8.0, 4.2, 2.2, "ruin_stone")
    add_arch(mesh, (46.5, 37.0), math.pi * 0.5, 8.0, 9.0, 7.5)
    return mesh


def build_upper_orchard() -> Mesh:
    mesh = Mesh("star_garden_upper_orchard")
    outline = [(-46.0, -31.0), (36.0, -33.0), (45.0, -13.0), (43.0, 31.0), (24.0, 40.0), (-34.0, 38.0), (-46.0, 19.0)]
    # 高台の下面を水面より下まで延ばし、浮遊島ではなく水辺から続く崖にする。
    add_polygon_slab(mesh, outline, 0.0, -18.0, "garden_grass_light", "garden_cliff_warm")
    add_wall(mesh, (-43.0, -28.0), (-12.0, -31.0), 0.0, 4.2)
    add_wall(mesh, (8.0, -32.0), (34.0, -30.0), 0.0, 4.2)
    add_wall(mesh, (-45.0, -16.0), (-45.0, 0.0), 0.0, 5.2)
    add_wall(mesh, (-45.0, 14.0), (-45.0, 20.0), 0.0, 5.2)
    add_wall(mesh, (43.0, -12.0), (43.0, 2.0), 0.0, 5.2)
    add_wall(mesh, (43.0, 16.0), (42.0, 29.0), 0.0, 5.2)
    add_arch(mesh, (-45.0, 7.0), math.pi * 0.5, 0.0, 9.0, 8.0)
    add_arch(mesh, (43.0, 9.0), math.pi * 0.5, 0.0, 9.0, 8.0)

    # 北壁上の高台は時限出現床を使わないと届かない寄り道。
    add_polygon_slab(mesh, [(-28.0, 25.0), (22.0, 25.0), (25.0, 39.0), (-31.0, 38.0)], 8.0, 0.0, "garden_grass", "ruin_stone")
    add_wall(mesh, (-31.0, 38.0), (25.0, 39.0), 8.0, 4.0, 2.4)
    add_arch(mesh, (-3.0, 38.5), 0.0, 8.0, 10.0, 7.0)
    # 時限床の終点は消えない固定バルコニーにする。北壁の高台へ食い込ませ、
    # 下から見ても到着地点だと読める張り出しと低い欄干を持たせる。
    add_box(mesh, (-4.0, 8.28, 32.0), (9.5, 0.28, 6.0), "ruin_path", "ruin_stone")
    add_wall(mesh, (-13.5, 26.0), (-13.5, 37.8), 8.56, 1.35, 0.8, "ruin_stone")
    add_wall(mesh, (5.5, 26.0), (5.5, 37.8), 8.56, 1.35, 0.8, "ruin_stone")
    for x in (-11.5, 3.5):
        add_box(mesh, (x, 9.48, 37.2), (1.0, 0.92, 1.0), "ruin_cap", "ruin_stone")

    # 高台から本道へ戻る固定帰路。4m前後の二段落差と側壁で、逆走用の
    # 階段ではなく一方通行の降下路であることを明確にする。
    add_box(mesh, (20.0, 4.0, 31.5), (5.0, 4.0, 5.5), "ruin_path", "ruin_stone", "garden_underside")
    add_box(mesh, (29.0, 2.15, 26.0), (5.0, 2.15, 5.0), "ruin_path", "ruin_stone", "garden_underside")
    add_box(mesh, (37.0, 0.22, 18.0), (4.5, 0.22, 5.5), "ruin_path", "ruin_stone", "garden_underside")
    add_wall(mesh, (14.8, 26.0), (24.7, 26.0), 8.0, 1.25, 0.8, "ruin_stone")
    add_wall(mesh, (24.5, 21.0), (33.8, 21.0), 4.3, 1.15, 0.8, "ruin_stone")
    add_wall(mesh, (32.5, 12.5), (41.5, 12.5), 0.44, 1.05, 0.8, "ruin_stone")
    for x, y, z, yaw in (
        (20.0, 8.18, 31.5, -0.52),
        (29.0, 4.48, 26.0, -0.68),
        (37.0, 0.62, 18.0, -0.78),
    ):
        add_oriented_box(mesh, (x, y, z), (2.1, 0.10, 0.32), yaw, "goal_dais", "ruin_cap")
    return mesh


def build_prism_arena() -> Mesh:
    mesh = Mesh("star_garden_prism_arena")
    segments = 24
    outline = []
    for index in range(segments):
        angle = math.tau * index / segments
        radius = 40.0 * (1.0 + 0.025 * math.sin(index * 2.7))
        outline.append((radius * math.cos(angle), radius * 0.86 * math.sin(angle)))
    add_polygon_slab(mesh, outline, 0.0, -23.0, "arena_grass", "arena_cliff")

    # 円形外壁は東西の出入口だけを開け、戦闘空間として読める輪郭にする。
    wall_radius_x = 37.5
    wall_radius_z = 32.0
    for index in range(segments):
        angle0 = math.tau * index / segments
        angle1 = math.tau * (index + 1) / segments
        mid_angle = (angle0 + angle1) * 0.5
        if abs(math.sin(mid_angle)) < 0.24:
            continue
        start = (wall_radius_x * math.cos(angle0), wall_radius_z * math.sin(angle0))
        end = (wall_radius_x * math.cos(angle1), wall_radius_z * math.sin(angle1))
        add_wall(mesh, start, end, 0.0, 7.5, 2.8, "arena_wall")
    add_arch(mesh, (-38.0, 0.0), math.pi * 0.5, 0.0, 10.0, 9.5, 3.2)
    add_arch(mesh, (38.0, 0.0), math.pi * 0.5, 0.0, 10.0, 9.5, 3.2)
    add_polygon_slab(mesh, [(-10.0, -8.0), (10.0, -8.0), (13.0, 8.0), (-13.0, 8.0)], 0.45, 0.0, "arena_dais", "ruin_stone")
    add_box(mesh, (25.0, 1.1, 0.0), (7.0, 1.1, 6.5), "ruin_path", "arena_wall")

    # 崩落床列の中央には水面から立ち上がる固定中継塔を置く。
    # 前後3枚ずつに分けられるため、連鎖崩落が一度切れて休める構成になる。
    relay_outline = [
        (-77.0, 36.0), (-72.0, 31.0), (-63.0, 32.0), (-58.5, 39.0),
        (-61.0, 47.5), (-69.0, 51.0), (-76.5, 46.5),
    ]
    add_polygon_slab(mesh, relay_outline, 6.0, -23.0, "ruin_path", "arena_cliff")
    add_box(mesh, (-68.0, 6.24, 41.0), (6.8, 0.24, 6.0), "arena_dais", "ruin_stone")
    add_wall(mesh, (-75.5, 38.0), (-71.5, 33.2), 6.48, 1.2, 0.8, "arena_wall")
    add_wall(mesh, (-64.0, 49.0), (-59.7, 43.0), 6.48, 1.2, 0.8, "arena_wall")

    # 東門の外側を、封印解除後だけ入れる報酬室として地形に統合する。
    # 西側は封印位置、東側は城壁区画へ抜ける開口として常に空けておく。
    reward_room = [
        (37.0, -9.5), (61.5, -9.5), (66.0, -6.0),
        (66.0, 6.0), (61.5, 9.5), (37.0, 9.5),
    ]
    add_polygon_slab(mesh, reward_room, 0.0, -23.0, "ruin_path", "arena_cliff")
    add_wall(mesh, (39.0, -9.5), (61.5, -9.5), 0.0, 6.2, 2.0, "arena_wall")
    add_wall(mesh, (39.0, 9.5), (61.5, 9.5), 0.0, 6.2, 2.0, "arena_wall")
    add_arch(mesh, (64.0, 0.0), math.pi * 0.5, 0.0, 10.0, 8.5, 2.8)
    add_box(mesh, (50.5, 0.42, 0.0), (5.0, 0.42, 5.0), "arena_dais", "ruin_stone")
    add_box(mesh, (50.5, 0.92, 0.0), (3.6, 0.08, 3.6), "goal_dais", "ruin_cap")
    return mesh


def build_goal_keep() -> Mesh:
    mesh = Mesh("star_garden_goal_keep")
    outline = [(-50.0, -35.0), (39.0, -39.0), (51.0, -20.0), (48.0, 34.0), (22.0, 43.0), (-40.0, 40.0), (-53.0, 20.0)]
    add_polygon_slab(mesh, outline, 0.0, -23.0, "garden_grass", "garden_cliff_rose")
    add_wall(mesh, (-47.0, -33.0), (30.0, -37.0), 0.0, 6.0)
    add_wall(mesh, (40.0, -31.0), (49.0, -17.0), 0.0, 6.0)
    add_wall(mesh, (48.0, -4.0), (47.0, 28.0), 0.0, 6.0)
    add_wall(mesh, (39.0, 35.0), (14.0, 41.0), 0.0, 6.0)
    add_wall(mesh, (-8.0, 41.0), (-39.0, 37.0), 0.0, 6.0)
    add_wall(mesh, (-49.0, 27.0), (-51.0, 10.0), 0.0, 6.0)
    add_arch(mesh, (-50.0, -1.0), math.pi * 0.5, 0.0, 10.0, 9.0)
    # 終盤輸送床の到着点。ゴール地形へ重ねて、着地後に必ず固定床へ乗れる。
    add_box(mesh, (-42.0, 0.32, 7.0), (8.0, 0.32, 7.0), "ruin_path", "ruin_stone", "garden_underside")
    add_wall(mesh, (-49.5, 13.5), (-34.5, 13.5), 0.64, 1.35, 0.85, "ruin_stone")
    add_box(mesh, (-42.0, 0.78, 7.0), (3.5, 0.10, 3.5), "garden_mechanism", "ruin_cap")
    add_polygon_slab(mesh, [(-16.0, -14.0), (17.0, -14.0), (21.0, 15.0), (-19.0, 15.0)], 2.8, 0.0, "goal_dais", "ruin_stone")
    add_steps(mesh, (-26.0, 0.0, 0.0), 4, 13.0, 4.2, 0.7, math.pi * 0.5)
    add_arch(mesh, (1.0, 18.0), 0.0, 2.8, 14.0, 10.0, 3.5)
    return mesh


def build_ruin_bridge() -> Mesh:
    mesh = Mesh("star_garden_ruin_bridge")
    add_box(mesh, (0.0, -0.75, 0.0), (24.0, 0.75, 5.5), "ruin_path", "ruin_stone", "garden_underside")
    # 接続先の地形へ欄干を突き刺さないよう、橋板より両端を短くする。
    add_wall(mesh, (-21.5, -5.5), (21.5, -5.5), 0.0, 1.4, 1.0, "ruin_stone")
    add_wall(mesh, (-21.5, 5.5), (21.5, 5.5), 0.0, 1.4, 1.0, "ruin_stone")
    for x in (-19.0, -9.5, 0.0, 9.5, 19.0):
        add_box(mesh, (x, 0.55, -5.5), (0.65, 1.2, 0.65), "ruin_cap", "ruin_stone")
        add_box(mesh, (x, 0.55, 5.5), (0.65, 1.2, 0.65), "ruin_cap", "ruin_stone")
    return mesh


def build_support_platform() -> Mesh:
    """当たり判定だけになりやすい固定着地点を可視化する汎用石床を生成する。"""
    mesh = Mesh("star_garden_support_platform")
    # ローカル上面をY=0、下面をY=-2に統一する。
    # シーン側ではpositionをコライダー上面、scaleをhalfSizeに合わせれば外形が一致する。
    add_box(mesh, (0.0, -1.0, 0.0), (1.0, 1.0, 1.0), "ruin_path", "ruin_stone", "garden_underside")
    # 上面内に収まる薄い縁取り。歩行面の高さは変えない。
    add_box(mesh, (0.0, -0.08, 0.0), (0.94, 0.08, 0.94), "ruin_cap", "ruin_stone", "ruin_stone")
    return mesh


def build_waterworks() -> Mesh:
    """水路跡と上下二層の攻略空間を持つ庭園区画を生成する。"""
    mesh = Mesh("star_garden_waterworks")
    outline = [
        (-60.0, -34.0), (-32.0, -45.0), (24.0, -43.0), (57.0, -23.0),
        (61.0, 19.0), (38.0, 43.0), (-24.0, 47.0), (-58.0, 28.0),
    ]
    add_polygon_slab(mesh, outline, 0.0, -18.5, "garden_grass_light", "garden_cliff_rose")

    # 低層の水路跡。中央を空けることで、トランポリンの着地点が読みやすくなる。
    add_box(mesh, (-30.0, 0.24, -4.0), (22.0, 0.24, 8.0), "ruin_path", "ruin_stone")
    add_box(mesh, (29.0, 0.24, -4.0), (21.0, 0.24, 8.0), "ruin_path", "ruin_stone")
    add_wall(mesh, (-53.0, -14.0), (-53.0, 24.0), 0.0, 4.8, 2.2)
    add_wall(mesh, (52.0, -15.0), (53.0, 16.0), 0.0, 4.8, 2.2)

    # 上層回廊。下面まで石積みを伸ばして、浮いた板に見えないようにする。
    upper_outline = [(-16.0, 10.0), (38.0, 8.0), (43.0, 36.0), (-20.0, 39.0)]
    add_polygon_slab(mesh, upper_outline, 10.0, 0.0, "garden_grass", "ruin_stone")
    add_wall(mesh, (-20.0, 39.0), (43.0, 36.0), 10.0, 4.2, 2.4)
    add_arch(mesh, (11.0, 37.5), 0.0, 10.0, 11.0, 8.0, 3.0)

    # 水路跡を跨ぐ石造アーチで、背景装飾と攻略床を一体化する。
    for z in (-24.0, 0.0, 24.0):
        add_arch(mesh, (10.0, z), math.pi * 0.5, 0.0, 13.0, 9.5, 3.2)
    return mesh


def build_gimmick_platform() -> Mesh:
    """庭園遺跡に馴染む可動ギミック共通足場を生成する。"""
    mesh = Mesh("star_garden_gimmick_platform")
    add_box(mesh, (0.0, -0.55, 0.0), (5.8, 0.55, 5.0), "ruin_path", "ruin_stone", "garden_underside")
    add_box(mesh, (0.0, 0.13, 0.0), (5.15, 0.13, 4.35), "garden_mechanism", "ruin_cap")
    for x, z in ((-4.8, -4.0), (4.8, -4.0), (-4.8, 4.0), (4.8, 4.0)):
        add_box(mesh, (x, 0.42, z), (0.42, 0.42, 0.42), "goal_dais", "ruin_stone")
    return mesh


def build_moving_platform() -> Mesh:
    """床面いっぱいの両矢印と側面レールで、水平往復する床だと示す。"""
    mesh = Mesh("star_garden_moving_platform")
    add_box(mesh, (0.0, -0.58, 0.0), (6.4, 0.58, 4.8), "ruin_path", "ruin_stone", "garden_underside")
    add_box(mesh, (0.0, 0.24, 0.0), (5.78, 0.24, 4.18), "garden_mechanism", "ruin_cap")
    add_double_arrow_slab(mesh, (0.0, 0.0), 0.0, 9.4, 4.9, 0.505, 0.475, "signal_white")
    for z in (-4.28, 4.28):
        add_box(mesh, (0.0, 0.30, z), (5.65, 0.20, 0.22), "goal_dais", "ruin_stone")
    for x in (-4.9, 4.9):
        add_box(mesh, (x, -0.92, 0.0), (0.55, 0.82, 3.75), "mechanism_dark", "garden_underside")
    return mesh


def build_lift_platform() -> Mesh:
    """中央ピストンと四本のガイドで、上下移動する昇降床だと示す。"""
    mesh = Mesh("star_garden_lift_platform")
    add_box(mesh, (0.0, -0.58, 0.0), (6.4, 0.58, 4.8), "ruin_path", "ruin_stone", "garden_underside")
    add_box(mesh, (0.0, 0.24, 0.0), (5.72, 0.24, 4.12), "garden_mechanism", "ruin_cap")
    add_ring_slab(mesh, (0.0, 0.0), 3.25, 2.55, 0.48, 0.505, 0.465, "signal_white", 20)
    inner = [(math.cos(math.tau * index / 8.0) * 1.7, math.sin(math.tau * index / 8.0) * 1.35) for index in range(8)]
    add_polygon_slab(mesh, inner, 0.51, 0.47, "goal_dais", "ruin_cap")
    for x, z in ((-5.05, -3.55), (5.05, -3.55), (-5.05, 3.55), (5.05, 3.55)):
        add_box(mesh, (x, -1.20, z), (0.48, 1.20, 0.48), "mechanism_dark", "garden_underside")
        add_box(mesh, (x, 0.22, z), (0.72, 0.22, 0.72), "goal_dais", "ruin_stone")
    return mesh


def build_rotating_platform() -> Mesh:
    """大きな円弧配置の矢印と回転軸で、回る床だと示す。"""
    mesh = Mesh("star_garden_rotating_platform")
    outline = [(math.cos(math.tau * index / 8.0) * 6.2, math.sin(math.tau * index / 8.0) * 6.2) for index in range(8)]
    add_polygon_slab(mesh, outline, 0.0, -1.2, "ruin_path", "ruin_stone")
    inset = [(math.cos(math.tau * index / 12.0) * 5.45, math.sin(math.tau * index / 12.0) * 5.45) for index in range(12)]
    add_polygon_slab(mesh, inset, 0.22, 0.0, "garden_mechanism", "ruin_cap")
    add_ring_slab(mesh, (0.0, 0.0), 1.65, 1.65, 0.42, 0.335, 0.22, "goal_dais", 20)
    for angle in (0.0, math.pi * 0.5, math.pi, math.pi * 1.5):
        center = (math.cos(angle) * 3.75, math.sin(angle) * 3.75)
        add_arrow_slab(mesh, center, angle + math.pi * 0.5, 3.2, 2.0, 0.34, 0.22, "signal_white")
    return mesh


def build_seesaw_platform() -> Mesh:
    """太い中央軸と左右の荷重パッドで、傾くシーソーだと示す。"""
    mesh = Mesh("star_garden_seesaw_platform")
    add_box(mesh, (0.0, 0.15, 0.0), (7.2, 0.48, 3.6), "ruin_path", "ruin_stone", "garden_underside")
    add_box(mesh, (0.0, -1.20, 0.0), (1.45, 0.88, 2.95), "mechanism_dark", "garden_underside")
    add_box(mesh, (0.0, 0.70, 0.0), (0.58, 0.14, 3.25), "goal_dais", "ruin_cap")
    for x, yaw in ((-4.65, math.pi), (4.65, 0.0)):
        add_box(mesh, (x, 0.63, 0.0), (1.75, 0.06, 2.85), "garden_mechanism", "ruin_cap")
        add_arrow_slab(mesh, (x, 0.0), yaw, 2.4, 2.0, 0.82, 0.69, "signal_white")
    return mesh


def build_sinking_platform() -> Mesh:
    """中央へ沈み込む段差と四方向の矢印で、沈下床だと示す。"""
    mesh = Mesh("star_garden_sinking_platform")
    add_box(mesh, (0.0, -0.72, 0.0), (5.8, 0.72, 5.0), "ruin_stone", "garden_underside", "garden_underside")
    add_box(mesh, (0.0, 0.26, 0.0), (5.1, 0.26, 4.3), "garden_mechanism", "ruin_cap")
    add_ring_slab(mesh, (0.0, 0.0), 3.95, 3.25, 0.44, 0.52, 0.495, "signal_white", 20)
    center = [(math.cos(math.tau * index / 12.0) * 2.15, math.sin(math.tau * index / 12.0) * 1.75) for index in range(12)]
    add_polygon_slab(mesh, center, 0.515, 0.49, "mechanism_dark", "ruin_cap")
    for x, z, yaw in ((-3.55, 0.0, 0.0), (3.55, 0.0, math.pi), (0.0, -2.85, -math.pi * 0.5), (0.0, 2.85, math.pi * 0.5)):
        add_arrow_slab(mesh, (x, z), yaw, 2.0, 1.45, 0.52, 0.495, "goal_dais")
    for x, z in ((-4.4, -3.6), (4.4, -3.6), (-4.4, 3.6), (4.4, 3.6)):
        add_box(mesh, (x, -0.55, z), (0.46, 0.55, 0.46), "mechanism_dark", "garden_underside")
    return mesh


def build_oneway_platform() -> Mesh:
    """床の大半を占める矢印と後端ストッパーで、一方通行だと示す。"""
    mesh = Mesh("star_garden_oneway_platform")
    add_box(mesh, (0.0, -0.42, 0.0), (5.2, 0.42, 4.4), "ruin_path", "ruin_stone", "garden_underside")
    add_box(mesh, (0.0, 0.14, 0.0), (4.72, 0.14, 3.90), "garden_mechanism", "ruin_cap")
    add_arrow_slab(mesh, (0.35, 0.0), 0.0, 8.25, 5.35, 0.30, 0.27, "signal_white")
    add_box(mesh, (-4.50, 0.24, 0.0), (0.24, 0.06, 3.45), "hazard_dark", "ruin_cap")
    return mesh


def build_trampoline_platform() -> Mesh:
    """多重スプリング環と中央の跳躍パッドで、跳ねる床だと示す。"""
    mesh = Mesh("star_garden_trampoline_platform")
    outer = [(math.cos(math.tau * index / 12.0) * 5.8, math.sin(math.tau * index / 12.0) * 5.8) for index in range(12)]
    add_polygon_slab(mesh, outer, 0.0, -0.9, "ruin_path", "ruin_stone")
    add_ring_slab(mesh, (0.0, 0.0), 4.35, 4.35, 0.48, 0.36, 0.0, "garden_mechanism", 24)
    add_ring_slab(mesh, (0.0, 0.0), 3.55, 3.55, 0.38, 0.50, 0.36, "goal_dais", 24)
    center = [(math.cos(math.tau * index / 12.0) * 2.75, math.sin(math.tau * index / 12.0) * 2.75) for index in range(12)]
    add_polygon_slab(mesh, center, 0.62, 0.50, "garden_mechanism", "ruin_cap")
    add_arrow_slab(mesh, (0.0, 0.0), -math.pi * 0.5, 3.15, 2.65, 0.68, 0.62, "signal_white")
    for x, z in ((-3.25, -3.25), (3.25, -3.25), (-3.25, 3.25), (3.25, 3.25)):
        add_ring_slab(mesh, (x, z), 0.68, 0.68, 0.22, 0.58, 0.36, "goal_dais", 12)
    return mesh


def build_collapse_platform() -> Mesh:
    """分割された三枚の板と太い亀裂で、崩落床だと示す。"""
    mesh = Mesh("star_garden_collapse_platform")
    add_box(mesh, (0.0, -0.52, 0.0), (5.8, 0.52, 5.0), "ruin_path", "ruin_stone", "garden_underside")
    for offset in (-3.75, 0.0, 3.75):
        add_box(mesh, (offset, 0.19, 0.0), (1.72, 0.19, 4.35), "garden_mechanism", "ruin_cap")
    add_crack_line(mesh, [(-5.1, -2.8), (-3.7, -1.6), (-4.2, -0.2), (-2.5, 1.0), (-3.0, 3.0)], 0.375, 0.20)
    add_crack_line(mesh, [(-0.8, -3.6), (0.4, -2.1), (-0.5, -0.7), (1.0, 0.8), (0.2, 3.5)], 0.375, 0.22)
    add_crack_line(mesh, [(2.8, -3.2), (4.0, -1.8), (3.2, -0.3), (4.6, 1.4), (3.5, 3.4)], 0.375, 0.20)
    return mesh


def build_appearing_marker() -> Mesh:
    """非表示の出現床が現れる位置を常時示す軽い台座を生成する。"""
    mesh = Mesh("star_garden_appearing_marker")
    for x, z in ((-4.5, -3.8), (4.5, -3.8), (-4.5, 3.8), (4.5, 3.8)):
        add_box(mesh, (x, 0.36, z), (0.34, 0.36, 0.34), "goal_dais", "ruin_stone")
    # 出現前でも床の位置と向きを読めるよう、細い格子を常時表示する。
    # 足場本体より十分薄くして、実体のある床とは見誤らないシルエットにする。
    for z in (-4.2, -2.1, 0.0, 2.1, 4.2):
        add_box(mesh, (0.0, 0.05, z), (4.7, 0.05, 0.10), "garden_mechanism", "ruin_cap")
    for x in (-5.0, -2.5, 0.0, 2.5, 5.0):
        add_box(mesh, (x, 0.05, 0.0), (0.10, 0.05, 3.8), "garden_mechanism", "ruin_cap")
    return mesh


def build_glass_panel() -> Mesh:
    """屈折マテリアルで描く、薄い破壊可能ガラス板を生成する。"""
    mesh = Mesh("star_garden_glass_panel")
    # ローカル外形を1m角へ統一し、シーン側のscaleで扉枠へ正確に合わせる。
    add_box(mesh, (0.0, 0.0, 0.0), (0.5, 0.5, 0.08), "garden_mechanism", "ruin_cap", "ruin_cap")
    return mesh


def build_spike_marker() -> Mesh:
    """上空から落ちる棘の着地点を常時示す警告台座を生成する。"""
    mesh = Mesh("star_garden_spike_marker")
    outer = [(math.cos(math.tau * index / 12.0) * 3.0, math.sin(math.tau * index / 12.0) * 3.0) for index in range(12)]
    inner = [(math.cos(math.tau * index / 12.0) * 1.5, math.sin(math.tau * index / 12.0) * 1.5) for index in range(12)]
    add_polygon_slab(mesh, outer, 0.08, 0.0, "goal_dais", "ruin_cap")
    add_polygon_slab(mesh, inner, 0.17, 0.08, "garden_mechanism", "ruin_cap")
    return mesh


def build_east_rampart() -> Mesh:
    """ボス後の長い城壁通路と二つの安全中庭を一体化して生成する。"""
    mesh = Mesh("star_garden_east_rampart")
    west = [(-64.0, -34.0), (-12.0, -34.0), (2.0, -20.0), (0.0, 18.0), (-18.0, 33.0), (-63.0, 29.0), (-73.0, 6.0)]
    east = [(8.0, -22.0), (66.0, -26.0), (77.0, -8.0), (73.0, 31.0), (23.0, 34.0), (3.0, 18.0)]
    add_polygon_slab(mesh, west, 0.0, -23.0, "garden_grass_dark", "garden_cliff_rose")
    add_polygon_slab(mesh, east, 3.0, -23.0, "garden_grass", "garden_cliff_warm")
    add_steps(mesh, (1.0, 0.0, -15.0), 3, 9.0, 4.6, 1.0, math.pi * 0.5)
    add_wall(mesh, (-63.0, -31.0), (-18.0, -31.0), 0.0, 5.0)
    add_wall(mesh, (-69.0, 5.0), (-61.0, 27.0), 0.0, 5.0)
    add_wall(mesh, (20.0, 31.0), (70.0, 28.0), 3.0, 5.0)
    add_wall(mesh, (72.0, -6.0), (66.0, -23.0), 3.0, 5.0)
    add_arch(mesh, (-66.0, -2.0), math.pi * 0.5, 0.0, 10.0, 8.0)
    add_arch(mesh, (73.0, 9.0), math.pi * 0.5, 3.0, 10.0, 8.0)

    # 城壁端と可動床列の間に固定の乗り場を設ける。
    ravine_boarding = [(67.0, -20.0), (86.0, -22.0), (95.0, -14.0), (91.0, -4.0), (73.0, 0.0)]
    add_polygon_slab(mesh, ravine_boarding, 3.0, -23.0, "ruin_path", "garden_cliff_warm")
    add_box(mesh, (83.0, 3.24, -11.5), (6.0, 0.24, 5.0), "garden_mechanism", "ruin_stone")
    add_wall(mesh, (69.0, -20.0), (86.0, -21.5), 3.0, 1.25, 0.8, "ruin_stone")

    # 妨害付き輸送床の発進点は独立した浮き床にせず、水面から続く固定塔にする。
    ride_departure = [
        (127.0, -55.0), (133.0, -70.0), (151.0, -72.0),
        (160.0, -61.0), (157.0, -43.0), (137.0, -40.0),
    ]
    add_polygon_slab(mesh, ride_departure, 4.0, -23.0, "ruin_path", "garden_cliff_rose")
    add_box(mesh, (140.0, 4.26, -57.0), (10.0, 0.26, 8.8), "garden_mechanism", "ruin_stone")
    add_wall(mesh, (128.5, -55.0), (134.0, -68.0), 4.0, 1.3, 0.85, "ruin_stone")
    add_wall(mesh, (153.0, -69.0), (158.0, -61.0), 4.0, 1.3, 0.85, "ruin_stone")
    return mesh


def build_ride_platform() -> Mesh:
    """終点で落下する輸送床。広い有効面と進行方向を示す意匠を持つ。"""
    mesh = Mesh("star_garden_ride_platform")
    outline = [
        (-7.5, -4.0), (-6.0, -5.5), (6.0, -5.5), (7.5, -4.0),
        (7.5, 4.0), (6.0, 5.5), (-6.0, 5.5), (-7.5, 4.0),
    ]
    add_polygon_slab(mesh, outline, 0.0, -1.8, "ruin_path", "ruin_stone", "garden_underside")
    add_box(mesh, (0.0, 0.14, 0.0), (6.5, 0.14, 4.25), "garden_mechanism", "ruin_cap")

    # X+側が進行方向だと分かる矢印状の埋め込み意匠。
    for x, half_z in ((-2.8, 1.8), (0.0, 2.35), (2.8, 2.9)):
        add_box(mesh, (x, 0.31, 0.0), (0.72, 0.12, half_z), "goal_dais", "ruin_cap")
    for x, z in ((-6.2, -4.2), (-6.2, 4.2), (6.2, -4.2), (6.2, 4.2)):
        add_box(mesh, (x, 0.42, z), (0.48, 0.42, 0.48), "goal_dais", "ruin_stone")
    return mesh


def build_toggle_switch() -> Mesh:
    """ON/OFF切り替えを色と押し込み面で読める庭園遺跡用スイッチ。"""
    mesh = Mesh("star_garden_toggle_switch")
    add_box(mesh, (0.0, -0.28, 0.0), (2.8, 0.28, 2.8), "ruin_stone", "garden_underside", "garden_underside")
    outer = [(math.cos(math.tau * index / 12.0) * 2.35, math.sin(math.tau * index / 12.0) * 2.35) for index in range(12)]
    inner = [(math.cos(math.tau * index / 12.0) * 1.35, math.sin(math.tau * index / 12.0) * 1.35) for index in range(12)]
    add_polygon_slab(mesh, outer, 0.12, 0.0, "garden_mechanism", "ruin_cap")
    add_polygon_slab(mesh, inner, 0.24, 0.12, "goal_dais", "ruin_cap")
    for yaw in (0.0, math.pi * 0.5):
        add_oriented_box(mesh, (0.0, 0.28, 0.0), (1.8, 0.06, 0.16), yaw, "goal_dais", "ruin_cap")
    return mesh


def build_blink_platform() -> Mesh:
    """二分割セルと発光環で、跳ぶたびに赤青が反転する床だと示す。"""
    mesh = Mesh("star_garden_blink_platform")
    add_box(mesh, (0.0, -0.55, 0.0), (5.8, 0.55, 5.0), "ruin_path", "ruin_stone", "garden_underside")
    add_box(mesh, (-2.62, 0.12, 0.0), (2.48, 0.12, 4.34), "blink_cell_a", "ruin_cap")
    add_box(mesh, (2.62, 0.12, 0.0), (2.48, 0.12, 4.34), "blink_cell_b", "ruin_cap")
    add_box(mesh, (0.0, 0.245, 0.0), (0.17, 0.005, 4.12), "hazard_dark", "hazard_dark")
    add_ring_slab(mesh, (0.0, 0.0), 2.55, 2.15, 0.34, 0.265, 0.245, "signal_white", 20)
    for x, z in ((-4.7, -3.9), (4.7, -3.9), (-4.7, 3.9), (4.7, 3.9)):
        diamond = transform_outline([(0.0, -0.62), (0.62, 0.0), (0.0, 0.62), (-0.62, 0.0)], (x, z), 0.0)
        add_polygon_slab(mesh, diamond, 0.27, 0.245, "goal_dais", "ruin_cap")
    return mesh


def build_linked_platform() -> Mesh:
    """床面いっぱいの実形状チェーンで、ID順に連鎖崩落する床だと示す。"""
    mesh = Mesh("star_garden_linked_platform")
    add_box(mesh, (0.0, -0.58, 0.0), (5.8, 0.58, 5.0), "ruin_path", "ruin_stone", "garden_underside")
    for offset in (-3.70, 0.0, 3.70):
        add_box(mesh, (offset, 0.12, 0.0), (1.72, 0.12, 4.35), "garden_mechanism", "ruin_cap")
    for x in (-1.88, 1.88):
        add_box(mesh, (x, 0.255, 0.0), (0.16, 0.015, 4.25), "hazard_dark", "hazard_dark")
    for x, z, yaw in ((-3.10, -0.72, -0.34), (0.0, 0.72, 0.34), (3.10, -0.72, -0.34)):
        add_ring_slab(mesh, (x, z), 2.18, 1.45, 0.43, 0.405, 0.255, "goal_dais", 20, yaw)
    return mesh


def build_dash_panel() -> Mesh:
    """進行方向へ流れるシェーダー意匠を正しいUVで表示するダッシュパネル。"""
    mesh = Mesh("star_garden_dash_panel")
    outer = [
        (-8.30, -2.55), (8.30, -2.55), (8.85, -2.00), (8.85, 2.00),
        (8.30, 2.55), (-8.30, 2.55), (-8.85, 2.00), (-8.85, -2.00),
    ]
    inner = [
        (-7.72, -2.05), (7.72, -2.05), (8.15, -1.62), (8.15, 1.62),
        (7.72, 2.05), (-7.72, 2.05), (-8.15, 1.62), (-8.15, -1.62),
    ]

    add_polygon_slab(mesh, outer, 0.0, -0.38, "dash_frame", "dash_base", "garden_underside")

    # The animated shader expects U across the lane and V along the launch direction.
    center = (0.0, 0.12, 0.0)
    center_uv = (0.5, 0.5)
    for index, (x0, z0) in enumerate(inner):
        x1, z1 = inner[(index + 1) % len(inner)]
        uv0 = ((x0 + 8.15) / 16.30, (z0 + 2.05) / 4.10)
        uv1 = ((x1 + 8.15) / 16.30, (z1 + 2.05) / 4.10)
        mesh.add_face(
            [center, (x0, 0.12, z0), (x1, 0.12, z1)],
            "dash_surface",
            (0.0, 1.0, 0.0),
            [center_uv, uv0, uv1],
        )

    # Raised rails keep the panel readable when viewed from a shallow camera angle.
    for x in (-8.24, 8.24):
        add_box(mesh, (x, 0.20, 0.0), (0.24, 0.20, 1.78), "dash_rail", "dash_base", "dash_base")
    for x in (-6.25, -2.10, 2.10, 6.25):
        add_box(mesh, (x, 0.205, -2.18), (0.52, 0.10, 0.16), "dash_light", "dash_base", "dash_base")
        add_box(mesh, (x, 0.205, 2.18), (0.52, 0.10, 0.16), "dash_light", "dash_base", "dash_base")
    return mesh


def build_checkpoint() -> Mesh:
    """進行方向から見落とさない発光環と旗を持つチェックポイント。"""
    mesh = Mesh("star_garden_checkpoint")
    add_box(mesh, (0.0, -0.58, 0.0), (1.7, 0.24, 1.7), "ruin_stone", "garden_underside", "garden_underside")
    outer = [(math.cos(math.tau * index / 16.0) * 1.45, math.sin(math.tau * index / 16.0) * 1.45) for index in range(16)]
    inner = [(math.cos(math.tau * index / 16.0) * 0.82, math.sin(math.tau * index / 16.0) * 0.82) for index in range(16)]
    add_polygon_slab(mesh, outer, -0.26, -0.50, "garden_mechanism", "ruin_cap")
    add_polygon_slab(mesh, inner, -0.10, -0.26, "goal_dais", "ruin_cap")
    add_box(mesh, (0.0, 1.15, 0.0), (0.18, 1.25, 0.18), "goal_dais", "ruin_stone")
    add_box(mesh, (0.78, 1.92, 0.0), (0.62, 0.42, 0.10), "garden_mechanism", "ruin_cap")
    add_box(mesh, (0.0, 2.52, 0.0), (0.42, 0.16, 0.42), "goal_dais", "ruin_cap")
    return mesh


def build_giant_gate() -> Mesh:
    """大型スライムの装甲突進で砕く、厚い三連装甲ゲート。"""
    mesh = Mesh("star_garden_giant_gate")
    add_box(mesh, (0.0, 0.0, 0.0), (7.2, 4.2, 1.5), "ruin_stone", "garden_underside", "garden_underside")
    for x in (-4.65, 0.0, 4.65):
        add_box(mesh, (x, 0.0, 1.62), (2.05, 3.45, 0.12), "garden_mechanism", "ruin_cap")
    for x in (-2.35, 2.35):
        add_box(mesh, (x, 0.0, 1.62), (0.18, 3.45, 0.12), "goal_dais", "ruin_cap")
    for y in (-2.55, 0.0, 2.55):
        add_box(mesh, (0.0, y, 1.62), (6.5, 0.16, 0.12), "goal_dais", "ruin_cap")
    return mesh


def build_course_v4() -> Mesh:
    """三つのスター寄り道と、横方向へ分岐する二つの空中攻略区画を持つ長編コース。"""
    mesh = Mesh("star_garden_course_v4", preconvert_engine_space=True)
    bottom_y = -13.0

    def add_land(
        center_x: float,
        center_z: float,
        half_x: float,
        half_z: float,
        top_y: float,
        yaw: float = 0.0,
        top_material: str = "garden_grass",
        side_material: str = "garden_cliff_warm",
    ) -> None:
        half_y = (top_y - bottom_y) * 0.5
        add_oriented_box(
            mesh,
            (center_x, bottom_y + half_y, center_z),
            (half_x, half_y, half_z),
            yaw,
            top_material,
            side_material,
            "garden_underside",
        )

    def add_path(center_x: float, center_z: float, half_x: float, half_z: float, top_y: float, yaw: float = 0.0) -> None:
        add_oriented_box(
            mesh,
            (center_x, top_y + 0.07, center_z),
            (half_x, 0.07, half_z),
            yaw,
            "ruin_path",
            "ruin_stone",
            "ruin_stone",
        )

    arena_entry_yaw = math.atan2(2.0, 35.0)
    chain_yaw = math.atan2(-12.0, 20.0)
    land_specs = (
        (-330.0, -50.0, 30.0, 22.0, 0.0, 0.0, "garden_grass_light", "garden_cliff_warm"),
        (-290.0, -50.0, 12.0, 13.0, 0.0, 0.0, "garden_grass", "garden_cliff_warm"),
        (-178.0, -42.0, 29.0, 22.0, 6.0, 0.0, "arena_grass", "arena_cliff"),
        (-115.0, -42.0, 34.0, 22.0, 6.0, 0.0, "garden_grass_light", "garden_cliff_rose"),
        (-178.0, -72.0, 9.0, 8.0, 6.0, 0.0, "ruin_path", "ruin_stone"),
        (-178.0, -96.0, 16.0, 14.0, 6.0, 0.0, "cave_floor", "cave_rock"),
        (-60.0, -38.0, 21.0, 16.0, 6.0, 0.0, "garden_grass", "garden_cliff_rose"),
        (20.0, -11.0, 9.0, 9.0, 8.0, 0.0, "garden_grass_light", "ruin_stone"),
        (76.0, 18.0, 6.0, 8.0, 10.0, 0.0, "garden_grass", "ruin_stone"),
        (70.0, 91.0, 15.5, 7.0, 12.0, 0.0, "garden_grass_light", "arena_cliff"),
        (111.0, 66.0, 6.5, 6.5, 10.0, 0.0, "garden_grass", "ruin_stone"),
        (142.0, 0.0, 11.0, 10.0, 10.0, 0.0, "garden_grass_light", "ruin_stone"),
        (194.0, -6.0, 41.0, 16.0, 10.0, 0.0, "garden_grass", "garden_cliff_warm"),
        (245.0, -10.0, 23.0, 15.0, 10.0, 0.0, "garden_grass_light", "garden_cliff_warm"),
        (280.0, -12.0, 12.0, 13.0, 10.0, arena_entry_yaw, "ruin_path", "arena_cliff"),
        (315.0, -20.0, 36.0, 30.0, 10.0, 0.0, "arena_grass", "arena_cliff"),
        (352.0, -20.0, 7.0, 13.0, 10.0, 0.0, "ruin_path", "arena_cliff"),
        (380.0, -20.0, 20.0, 11.0, 10.0, 0.0, "garden_grass_light", "garden_cliff_warm"),
        (425.0, -20.0, 13.0, 11.0, 10.0, 0.0, "garden_grass", "garden_cliff_warm"),
        (469.0, -20.0, 16.0, 11.0, 10.0, 0.0, "garden_grass_light", "garden_cliff_rose"),
        (520.0, -20.0, 19.0, 11.0, 10.0, 0.0, "garden_grass", "garden_cliff_rose"),
        (562.0, -20.0, 23.0, 15.0, 10.0, 0.0, "garden_grass_light", "garden_cliff_warm"),
        (605.0, -14.0, 18.0, 13.0, 12.0, chain_yaw, "garden_grass", "garden_cliff_rose"),
        (679.0, 18.0, 11.0, 11.0, 15.0, chain_yaw, "garden_grass_light", "ruin_stone"),
        (785.0, 13.0, 12.0, 11.0, 17.0, -0.20, "garden_grass", "ruin_stone"),
        (814.0, 27.0, 18.0, 14.0, 17.0, -0.20, "garden_grass", "garden_cliff_warm"),
        (850.0, 20.0, 20.0, 20.0, 17.0, 0.0, "garden_grass_light", "garden_cliff_warm"),
    )
    for center_x, center_z, half_x, half_z, top_y, yaw, top_material, side_material in land_specs:
        add_land(center_x, center_z, half_x, half_z, top_y, yaw, top_material, side_material)

    # 各区画の固定地形だけに石畳を敷き、空中ギミックの間は視覚的にも切り離す。
    path_specs = (
        (-330.0, -50.0, 30.0, 4.4, 0.0, 0.0),
        (-290.0, -50.0, 12.0, 4.4, 0.0, 0.0),
        (-178.0, -42.0, 24.0, 5.0, 6.0, 0.0),
        (-115.0, -42.0, 34.0, 4.8, 6.0, 0.0),
        (-178.0, -72.0, 4.2, 8.0, 6.0, math.pi * 0.5),
        (-178.0, -96.0, 10.0, 4.2, 6.0, 0.0),
        (-60.0, -38.0, 21.0, 4.8, 6.0, 0.0),
        (20.0, -11.0, 6.0, 4.8, 8.0, 0.0),
        (76.0, 18.0, 4.2, 4.8, 10.0, 0.0),
        (70.0, 91.0, 10.0, 4.0, 12.0, 0.0),
        (111.0, 66.0, 4.5, 3.8, 10.0, 0.0),
        (142.0, 0.0, 11.0, 4.8, 10.0, 0.0),
        (194.0, -6.0, 41.0, 4.8, 10.0, 0.0),
        (245.0, -10.0, 23.0, 4.8, 10.0, 0.0),
        (280.0, -12.0, 12.0, 4.8, 10.0, arena_entry_yaw),
        (315.0, -20.0, 36.0, 5.4, 10.0, 0.0),
        (352.0, -20.0, 7.0, 4.8, 10.0, 0.0),
        (380.0, -20.0, 20.0, 4.8, 10.0, 0.0),
        (425.0, -20.0, 13.0, 4.8, 10.0, 0.0),
        (469.0, -20.0, 16.0, 4.8, 10.0, 0.0),
        (520.0, -20.0, 19.0, 4.8, 10.0, 0.0),
        (562.0, -20.0, 23.0, 4.8, 10.0, 0.0),
        (605.0, -14.0, 14.0, 4.8, 12.0, chain_yaw),
        (679.0, 18.0, 8.0, 4.8, 15.0, chain_yaw),
        (785.0, 13.0, 8.0, 4.8, 17.0, -0.20),
        (814.0, 27.0, 18.0, 4.8, 17.0, -0.20),
        (850.0, 20.0, 20.0, 5.2, 17.0, 0.0),
    )
    for spec in path_specs:
        add_path(*spec)

    # 収集物の台座は固定地形へ統合し、判定だけが浮く状態を作らない。
    add_box(mesh, (-178.0, 6.65, -98.0), (4.2, 0.65, 3.4), "goal_dais", "ruin_stone")
    add_box(mesh, (850.0, 17.65, 20.0), (10.0, 0.65, 10.0), "goal_dais", "ruin_stone")

    # 節目の門だけで区画を分け、空中ルートの可視性を遮らない。
    add_arch(mesh, (-305.0, -50.0), 0.0, 0.0, 10.0, 7.0, 2.0)
    add_arch(mesh, (-178.0, -80.2), 0.0, 6.0, 18.0, 11.0, 1.8)
    add_arch(mesh, (-79.0, -38.0), 0.0, 6.0, 11.0, 7.5, 2.0)
    add_arch(mesh, (268.0, -12.0), arena_entry_yaw, 10.0, 12.0, 8.0, 2.2)
    add_arch(mesh, (359.0, -20.0), 0.0, 10.0, 12.0, 8.0, 2.2)
    add_arch(mesh, (585.0, -18.0), chain_yaw, 11.0, 12.0, 8.0, 2.2)
    add_arch(mesh, (832.0, 23.5), -0.20, 17.0, 13.0, 8.5, 2.4)
    return mesh


def build_course_v3() -> Mesh:
    """緩いS字と段階的な高低差を持つ、Stage 1の一体型コースを生成する。"""
    mesh = Mesh("star_garden_course_v3", preconvert_engine_space=True)
    bottom_y = -13.0

    def add_land(
        center_x: float,
        center_z: float,
        half_x: float,
        half_z: float,
        top_y: float,
        top_material: str = "garden_grass",
        side_material: str = "garden_cliff_warm",
        yaw: float = 0.0,
    ) -> None:
        half_y = (top_y - bottom_y) * 0.5
        add_oriented_box(
            mesh,
            (center_x, bottom_y + half_y, center_z),
            (half_x, half_y, half_z),
            yaw,
            top_material,
            side_material,
            "garden_underside",
        )

    def add_path(
        center_x: float,
        center_z: float,
        half_x: float,
        half_z: float,
        top_y: float,
        yaw: float = 0.0,
    ) -> None:
        add_oriented_box(
            mesh,
            (center_x, top_y + 0.07, center_z),
            (half_x, 0.07, half_z),
            yaw,
            "ruin_path",
            "ruin_stone",
            "ruin_stone",
        )

    meadow_yaw = math.atan2(-10.0, 40.0)
    stair_yaw = math.atan2(-14.0, 33.0)
    south_bend_yaw = math.atan2(24.0, 68.0)
    landing_yaw = math.atan2(-4.0, 26.0)
    arena_entry_yaw = math.atan2(-5.0, 12.0)
    arena_exit_yaw = math.atan2(1.0, 8.0)
    final_yaw = math.atan2(-8.0, 26.0)
    final_step_yaw = math.atan2(-8.0, 21.0)
    goal_step_yaw = math.atan2(-5.0, 10.0)
    start_and_meadow = (
        (-330.0, -50.0, 30.0, 22.0, 0.0, "garden_grass_light", "garden_cliff_warm", 0.0),
        (-275.0, -50.0, 25.0, 14.0, 0.0, "garden_grass", "garden_cliff_warm", 0.0),
        (-235.0, -40.0, 22.0, 14.0, 0.0, "garden_grass_dark", "garden_cliff_rose", meadow_yaw),
        (-207.0, -32.0, 8.0, 12.0, 0.0, "garden_grass", "garden_cliff_rose", meadow_yaw),
    )
    for spec in start_and_meadow:
        add_land(*spec)

    # 三段で高さを導入し、広い上層庭園へつなぐ。
    for center_x, center_z, top_y in ((-194.0, -28.0, 2.0), (-184.0, -23.0, 4.0), (-174.0, -18.0, 6.0)):
        add_land(center_x, center_z, 5.0, 12.0, top_y, "garden_grass_light", "garden_cliff_rose", stair_yaw)
    add_land(-130.0, -14.0, 39.0, 20.0, 6.0, "garden_grass_light", "garden_cliff_rose")
    add_land(-79.0, -29.0, 15.0, 12.0, 6.0, "garden_grass", "ruin_stone", south_bend_yaw)
    add_land(-62.0, -38.0, 8.0, 8.0, 6.0, "garden_grass_light", "ruin_stone", south_bend_yaw)

    # トランポリン上昇後の水路庭園。可動床で北へ折れ、上下リフトの高台から本道へ戻る。
    add_land(4.0, -52.0, 24.0, 16.0, 15.0, "garden_grass_light", "arena_cliff")
    add_land(30.0, -48.0, 3.0, 8.0, 15.0, "garden_grass", "ruin_stone", landing_yaw)
    add_land(60.0, -14.0, 7.0, 7.0, 15.0, "garden_grass", "ruin_stone")
    add_land(58.0, 10.0, 10.0, 8.0, 23.0, "garden_grass_light", "arena_cliff")
    add_land(72.0, -2.0, 7.0, 10.0, 15.0, "garden_grass_dark", "ruin_stone")
    add_land(68.0, -37.0, 7.0, 10.0, 15.0, "garden_grass_light", "arena_cliff", arena_entry_yaw)
    # リフトの両脇へ高さを示すガイド柱を置き、上下運動の到達先を地上から読ませる。
    for x in (50.8, 65.2):
        add_box(mesh, (x, 19.0, -7.2), (0.34, 4.0, 0.34), "garden_mechanism", "ruin_cap", "ruin_stone")
    add_box(mesh, (58.0, 23.15, -7.2), (7.6, 0.15, 0.34), "goal_dais", "ruin_cap", "ruin_stone")

    arena_outline = [
        (100.0 + 36.0 * math.cos(math.tau * index / 20.0), -32.0 + 28.0 * math.sin(math.tau * index / 20.0))
        for index in range(20)
    ]
    arena_inner = [
        (100.0 + 29.0 * math.cos(math.tau * index / 20.0), -32.0 + 21.0 * math.sin(math.tau * index / 20.0))
        for index in range(20)
    ]
    add_polygon_slab(mesh, arena_outline, 15.0, bottom_y, "arena_grass", "arena_cliff")
    add_polygon_slab(mesh, arena_inner, 15.10, 15.0, "arena_dais", "arena_wall")
    add_land(138.0, -29.0, 4.0, 10.0, 15.0, "garden_grass", "arena_cliff", arena_exit_yaw)
    add_land(146.0, -30.0, 5.0, 10.0, 15.0, "garden_grass_light", "ruin_stone", arena_exit_yaw)

    # 崩落橋の着地点から、緩く北へ戻りながら最終上りへ向かう。
    add_land(214.0, -52.0, 11.0, 16.0, 13.0, "garden_grass_light", "garden_cliff_rose")
    add_land(240.0, -44.0, 18.0, 17.0, 13.0, "garden_grass", "garden_cliff_rose", final_yaw)
    add_land(261.0, -36.0, 5.0, 15.0, 15.0, "garden_grass_light", "garden_cliff_rose", final_step_yaw)
    add_land(271.0, -31.0, 5.0, 15.0, 17.0, "garden_grass", "garden_cliff_rose", goal_step_yaw)
    add_land(292.0, -28.0, 16.0, 20.0, 17.0, "garden_grass_light", "garden_cliff_warm")

    # 本道の石畳は方向転換をそのまま描き、カメラを回さなくても進行を読めるようにする。
    path_specs = (
        (-330.0, -50.0, 30.0, 4.2, 0.0, 0.0),
        (-275.0, -50.0, 25.0, 4.2, 0.0, 0.0),
        (-235.0, -40.0, 22.0, 4.2, 0.0, meadow_yaw),
        (-207.0, -32.0, 8.0, 4.2, 0.0, meadow_yaw),
        (-194.0, -28.0, 5.0, 4.2, 2.0, stair_yaw),
        (-184.0, -23.0, 5.0, 4.2, 4.0, stair_yaw),
        (-174.0, -18.0, 5.0, 4.2, 6.0, stair_yaw),
        (-130.0, -14.0, 39.0, 4.6, 6.0, 0.0),
        (-79.0, -29.0, 15.0, 4.6, 6.0, south_bend_yaw),
        (-62.0, -38.0, 8.0, 4.6, 6.0, south_bend_yaw),
        (4.0, -52.0, 24.0, 4.8, 15.0, 0.0),
        (30.0, -48.0, 3.0, 4.6, 15.0, landing_yaw),
        (60.0, -14.0, 5.0, 4.0, 15.0, 0.0),
        (72.0, -2.0, 4.0, 8.0, 15.0, math.pi * 0.5),
        (58.0, 10.0, 6.5, 3.2, 23.0, math.pi * 0.5),
        (68.0, -37.0, 7.0, 4.6, 15.0, arena_entry_yaw),
        (138.0, -29.0, 4.0, 4.6, 15.0, arena_exit_yaw),
        (146.0, -30.0, 5.0, 4.6, 15.0, arena_exit_yaw),
        (214.0, -52.0, 11.0, 4.6, 13.0, 0.0),
        (240.0, -44.0, 18.0, 4.8, 13.0, final_yaw),
        (261.0, -36.0, 5.0, 4.8, 15.0, final_step_yaw),
        (271.0, -31.0, 5.0, 4.8, 17.0, goal_step_yaw),
        (292.0, -28.0, 16.0, 5.2, 17.0, 0.0),
    )
    for spec in path_specs:
        add_path(*spec)

    # スター1: 最初のカーブから短く南へ逸れる、ガラス越しに報酬が見えるボム保管室。
    add_land(-270.0, -71.0, 7.0, 8.0, 0.0, "garden_grass_dark", "cave_rock")
    add_land(-270.0, -91.0, 17.0, 12.0, 0.0, "cave_floor", "cave_rock")
    add_path(-270.0, -71.0, 3.6, 8.0, 0.0, math.pi * 0.5)
    add_path(-270.0, -91.0, 10.0, 3.6, 0.0)
    add_wall(mesh, (-287.0, -103.0), (-253.0, -103.0), 0.0, 10.0, 1.8, "cave_rock")
    add_wall(mesh, (-287.0, -103.0), (-287.0, -79.0), 0.0, 10.0, 1.8, "cave_rock")
    add_wall(mesh, (-253.0, -103.0), (-253.0, -79.0), 0.0, 10.0, 1.8, "cave_rock")
    add_arch(mesh, (-270.0, -79.4), 0.0, 0.0, 18.0, 10.0, 1.4)
    # 三列二段のガラス境界へ石の桟を重ね、破壊対象の区切りを遠目から読めるようにする。
    for x in (-272.7, -267.3):
        add_box(mesh, (x, 4.2, -79.4), (0.13, 4.2, 0.72), "ruin_cap", "ruin_stone", "ruin_stone")
    add_box(mesh, (-270.0, 4.2, -79.4), (8.1, 0.13, 0.72), "ruin_cap", "ruin_stone", "ruin_stone")
    add_box(mesh, (-270.0, 0.65, -93.0), (4.2, 0.65, 3.5), "goal_dais", "ruin_stone")

    # スター2の台座は水路庭園の上下リフト終点へ統合する。
    add_box(mesh, (58.0, 23.65, 10.0), (4.0, 0.65, 3.2), "goal_dais", "ruin_stone")

    # スター3: 回転・シーソー・沈下床を渡る上級分岐。帰路は高台の真下へ落として逆走を防ぐ。
    add_land(214.0, -69.0, 7.0, 5.0, 15.0, "garden_grass", "ruin_stone")
    add_land(270.0, -99.0, 9.0, 7.0, 20.0, "garden_grass_light", "arena_cliff")
    add_land(270.0, -84.0, 9.0, 8.0, 13.0, "garden_grass", "ruin_stone")
    add_land(257.0, -66.0, 8.0, 12.0, 13.0, "garden_grass_light", "ruin_stone", -0.48)
    add_path(270.0, -99.0, 6.0, 2.8, 20.0)
    add_box(mesh, (270.0, 20.65, -99.0), (4.0, 0.65, 3.2), "goal_dais", "ruin_stone")

    # 門は区間の節目だけに置き、高い壁は洞窟以外に作らない。
    add_arch(mesh, (-315.0, -50.0), 0.0, 0.0, 10.0, 7.0, 2.2)
    add_arch(mesh, (66.0, -38.0), arena_entry_yaw, 15.0, 11.0, 7.5, 2.4)
    add_arch(mesh, (137.0, -29.0), arena_exit_yaw, 15.0, 11.0, 7.5, 2.4)
    add_arch(mesh, (279.0, -28.0), 0.0, 17.0, 12.0, 8.0, 2.5)
    add_box(mesh, (295.0, 17.65, -28.0), (10.0, 0.65, 10.0), "goal_dais", "ruin_stone")
    return mesh


def build_course_v2() -> Mesh:
    """進行方向をX+へ統一した、Stage 1の一体型コース地形を生成する。"""
    mesh = Mesh("star_garden_course_v2")
    bottom_y = -13.0

    def add_land(
        center_x: float,
        center_z: float,
        half_x: float,
        half_z: float,
        top_y: float,
        top_material: str = "garden_grass",
        side_material: str = "garden_cliff_warm",
    ) -> None:
        half_y = (top_y - bottom_y) * 0.5
        add_box(
            mesh,
            (center_x, bottom_y + half_y, center_z),
            (half_x, half_y, half_z),
            top_material,
            side_material,
            "garden_underside",
        )

    def add_path(center_x: float, center_z: float, half_x: float, half_z: float, top_y: float) -> None:
        add_box(
            mesh,
            (center_x, top_y + 0.07, center_z),
            (half_x, 0.07, half_z),
            "ruin_path",
            "ruin_stone",
            "ruin_stone",
        )

    # 本道は低い入口、三段の導入、上層庭園、円形闘技場、ゴールの順に読む。
    main_lands = (
        (-320.0, -50.0, 40.0, 22.0, 0.0, "garden_grass_light"),
        (-245.0, -50.0, 35.0, 13.0, 0.0, "garden_grass"),
        (-192.5, -50.0, 17.5, 15.0, 0.0, "garden_grass_dark"),
        (-168.0, -50.0, 7.0, 15.0, 2.0, "garden_grass"),
        (-154.0, -50.0, 7.0, 15.0, 4.0, "garden_grass_light"),
        (-140.0, -50.0, 7.0, 15.0, 6.0, "garden_grass"),
        (-92.0, -50.0, 41.0, 17.0, 8.0, "garden_grass_light"),
        (-44.0, -50.0, 7.0, 12.0, 8.0, "garden_grass"),
        (40.0, -50.0, 3.0, 10.0, 8.0, "garden_grass"),
        (48.0, -50.0, 5.0, 13.0, 8.0, "garden_grass_light"),
        (122.0, -50.0, 17.0, 17.0, 8.0, "garden_grass_light"),
        (145.0, -50.0, 6.0, 17.0, 10.0, "garden_grass"),
        (157.0, -50.0, 6.0, 17.0, 12.0, "garden_grass_light"),
        (198.0, -50.0, 35.0, 22.0, 12.0, "garden_grass_light"),
    )
    for center_x, center_z, half_x, half_z, top_y, material in main_lands:
        add_land(center_x, center_z, half_x, half_z, top_y, material)

    arena_outline = [
        (38.0 * math.cos(math.tau * index / 20.0), -50.0 + 30.0 * math.sin(math.tau * index / 20.0))
        for index in range(20)
    ]
    add_polygon_slab(mesh, arena_outline, 8.0, bottom_y, "arena_grass", "arena_cliff")
    add_polygon_slab(
        mesh,
        [(31.0 * math.cos(math.tau * index / 20.0), -50.0 + 23.0 * math.sin(math.tau * index / 20.0)) for index in range(20)],
        8.10,
        8.0,
        "arena_dais",
        "arena_wall",
    )

    # 一本の石畳を地形へ埋め込み、遠景からも進行軸を読めるようにする。
    for path_spec in (
        (-320.0, -50.0, 40.0, 4.2, 0.0),
        (-245.0, -50.0, 35.0, 4.2, 0.0),
        (-192.5, -50.0, 17.5, 4.2, 0.0),
        (-168.0, -50.0, 7.0, 4.2, 2.0),
        (-154.0, -50.0, 7.0, 4.2, 4.0),
        (-140.0, -50.0, 7.0, 4.2, 6.0),
        (-92.0, -50.0, 41.0, 4.6, 8.0),
        (-44.0, -50.0, 7.0, 4.6, 8.0),
        (40.0, -50.0, 3.0, 4.6, 8.0),
        (48.0, -50.0, 5.0, 4.6, 8.0),
        (122.0, -50.0, 17.0, 4.6, 8.0),
        (145.0, -50.0, 6.0, 4.6, 10.0),
        (157.0, -50.0, 6.0, 4.6, 12.0),
        (198.0, -50.0, 35.0, 5.0, 12.0),
    ):
        add_path(*path_spec)

    # スター1: 序盤の本道から南へ逸れ、同じ場所へ戻る低いボム洞窟。
    add_land(-240.0, -75.0, 7.0, 12.0, 0.0, "garden_grass_dark")
    add_land(-240.0, -100.0, 18.0, 13.0, 0.0, "cave_floor", "cave_rock")
    add_path(-240.0, -75.0, 3.6, 12.0, 0.0)
    add_path(-240.0, -100.0, 11.0, 3.6, 0.0)
    add_wall(mesh, (-258.0, -113.0), (-222.0, -113.0), 0.0, 3.8, 1.8, "cave_rock")
    add_wall(mesh, (-258.0, -113.0), (-258.0, -87.0), 0.0, 3.8, 1.8, "cave_rock")
    add_wall(mesh, (-222.0, -113.0), (-222.0, -87.0), 0.0, 3.8, 1.8, "cave_rock")
    add_box(mesh, (-240.0, 0.65, -102.0), (4.2, 0.65, 3.5), "goal_dais", "ruin_stone")

    # スター2: 三枚の時限床から見えている高台へ上がり、固定段差で本道へ戻る。
    add_land(-60.0, -18.0, 12.0, 8.0, 14.0, "garden_grass_light", "garden_cliff_rose")
    add_land(-50.0, -31.0, 6.0, 5.0, 11.0, "garden_grass", "garden_cliff_rose")
    add_land(-50.0, -41.0, 6.0, 5.0, 9.0, "garden_grass_light", "garden_cliff_rose")
    add_path(-60.0, -18.0, 8.0, 3.2, 14.0)
    for x, z, top_y in ((-98.0, -29.0, 9.45), (-88.0, -22.0, 11.45), (-76.0, -18.0, 13.45)):
        add_box(mesh, (x, (top_y + bottom_y) * 0.5, z), (1.0, (top_y - bottom_y) * 0.5, 1.0), "garden_mechanism", "ruin_stone")
    add_box(mesh, (-60.0, 14.65, -18.0), (4.0, 0.65, 3.2), "goal_dais", "ruin_stone")

    # スター3: ボス後に見える固定高台。攻略後はゴール広場へ自然に合流する。
    add_land(118.0, -70.0, 6.0, 5.0, 10.5, "garden_grass")
    add_land(130.0, -78.0, 6.0, 5.0, 12.5, "garden_grass_light")
    add_land(145.0, -82.0, 12.0, 8.0, 14.0, "garden_grass_light", "garden_cliff_rose")
    add_land(158.0, -74.0, 6.0, 8.0, 13.0, "garden_grass")
    add_path(145.0, -82.0, 8.0, 3.2, 14.0)
    add_box(mesh, (145.0, 14.65, -82.0), (4.0, 0.65, 3.2), "goal_dais", "ruin_stone")

    # 入口、闘技場、ゴールだけに門を置き、壁でカメラを塞がない。
    add_arch(mesh, (-315.0, -50.0), 0.0, 0.0, 10.0, 7.0, 2.2)
    add_arch(mesh, (-38.0, -50.0), 0.0, 8.0, 11.0, 7.5, 2.4)
    add_arch(mesh, (38.0, -50.0), 0.0, 8.0, 11.0, 7.5, 2.4)
    add_arch(mesh, (183.0, -50.0), 0.0, 12.0, 12.0, 8.0, 2.5)
    add_box(mesh, (215.0, 12.65, -50.0), (14.0, 0.65, 10.0), "goal_dais", "ruin_stone")
    return mesh


def build_backdrop_v2() -> Mesh:
    """本道と誤認しない、水面近くの低い遠景島だけを生成する。"""
    mesh = Mesh("star_garden_backdrop_v2", preconvert_engine_space=True)
    for index, (cx, cz, rx, rz) in enumerate((
        (-300.0, 95.0, 42.0, 20.0),
        (-190.0, -145.0, 48.0, 22.0),
        (-60.0, 120.0, 46.0, 24.0),
        (70.0, -145.0, 52.0, 22.0),
        (190.0, 112.0, 45.0, 22.0),
        (300.0, -128.0, 50.0, 24.0),
    )):
        outer = [
            (cx + math.cos(math.tau * point / 10.0) * rx, cz + math.sin(math.tau * point / 10.0) * rz)
            for point in range(10)
        ]
        inner = [
            (cx + math.cos(math.tau * point / 10.0) * rx * 0.72, cz + math.sin(math.tau * point / 10.0) * rz * 0.68)
            for point in range(10)
        ]
        add_polygon_slab(mesh, outer, -8.3, -18.0, "shore_sand", "backdrop_cliff")
        add_polygon_slab(mesh, inner, -6.7 + 0.25 * (index % 2), -8.3, "backdrop_grass", "backdrop_cliff")
    return mesh


def build_backdrop_ridges() -> Mesh:
    mesh = Mesh("star_garden_backdrop_ridges")
    # 本道へ張り出す巨大な板を廃止し、水面から見える低い島影だけを置く。
    # 外側の砂州と内側の草地を二層にして、遠景にも海岸線を作る。
    island_specs = [
        (-310.0, 112.0, 58.0, 34.0, 0.20),
        (-175.0, -128.0, 72.0, 31.0, -0.10),
        (-60.0, 172.0, 64.0, 38.0, 0.08),
        (92.0, -142.0, 78.0, 34.0, -0.16),
        (238.0, 154.0, 66.0, 32.0, 0.14),
        (382.0, -132.0, 70.0, 36.0, -0.06),
    ]
    for island_index, (cx, cz, rx, rz, skew) in enumerate(island_specs):
        outer = []
        inner = []
        ridge = []
        for index in range(10):
            angle = math.tau * index / 10.0
            wobble = 1.0 + 0.07 * math.sin(index * 2.35 + cx * 0.01)
            outer.append((cx + math.cos(angle) * rx * wobble + math.sin(angle) * rz * skew, cz + math.sin(angle) * rz * wobble))
            inner.append((cx + math.cos(angle) * rx * 0.78 * wobble, cz + math.sin(angle) * rz * 0.72 * wobble))
            ridge_wobble = 1.0 + 0.12 * math.sin(index * 1.75 + island_index * 0.9)
            ridge.append((
                cx + math.cos(angle) * rx * 0.42 * ridge_wobble,
                cz + math.sin(angle) * rz * 0.38 * ridge_wobble,
            ))
        add_polygon_slab(mesh, outer, -8.2, -18.0, "shore_sand", "backdrop_cliff")
        add_polygon_slab(mesh, inner, -5.4, -18.0, "backdrop_grass", "backdrop_cliff")
        ridge_top = -1.8 + 0.7 * (island_index % 3)
        add_polygon_slab(mesh, ridge, ridge_top, -5.4, "garden_grass_dark", "backdrop_cliff")

        # 遠景にも高さの基準を作る。攻略用には見えない小さな遺跡だけを島ごとに変えて置く。
        if island_index % 2 == 0:
            add_arch(mesh, (cx, cz), island_index * 0.41, ridge_top, 8.0, 6.0, 2.1)
        else:
            offset = rx * 0.12
            add_wall(
                mesh,
                (cx - offset, cz - rz * 0.05),
                (cx + offset, cz + rz * 0.05),
                ridge_top,
                4.5,
                1.8,
                "ruin_stone",
            )
    return mesh


MATERIAL_LIBRARY = """newmtl garden_grass
Ka 0.10 0.24 0.05
Kd 0.34 0.70 0.20
Ks 0.05 0.08 0.03
Ns 18
illum 2

newmtl garden_grass_light
Ka 0.14 0.30 0.07
Kd 0.46 0.82 0.25
Ks 0.06 0.09 0.03
Ns 20
illum 2

newmtl garden_grass_dark
Ka 0.07 0.19 0.04
Kd 0.25 0.55 0.16
Ks 0.04 0.06 0.02
Ns 16
illum 2

newmtl garden_cliff_warm
Ka 0.22 0.13 0.07
Kd 0.60 0.36 0.19
Ks 0.04 0.03 0.02
Ns 12
illum 2

newmtl garden_cliff_rose
Ka 0.24 0.13 0.08
Kd 0.66 0.39 0.23
Ks 0.04 0.03 0.02
Ns 12
illum 2

newmtl garden_underside
Ka 0.06 0.06 0.09
Kd 0.18 0.18 0.25
Ks 0.03 0.03 0.04
Ns 10
illum 2

newmtl cliff_moss
Ka 0.12 0.17 0.07
Kd 0.37 0.43 0.20
Ks 0.04 0.04 0.02
Ns 14
illum 2

newmtl ruin_stone
Ka 0.20 0.20 0.25
Kd 0.52 0.52 0.61
Ks 0.08 0.08 0.10
Ns 28
illum 2

newmtl ruin_cap
Ka 0.25 0.24 0.28
Kd 0.68 0.66 0.74
Ks 0.10 0.10 0.13
Ns 34
illum 2

newmtl ruin_path
Ka 0.21 0.20 0.22
Kd 0.60 0.57 0.61
Ks 0.06 0.06 0.07
Ns 20
illum 2

newmtl cave_floor
Ka 0.06 0.07 0.08
Kd 0.18 0.20 0.23
Ks 0.03 0.03 0.04
Ns 10
illum 2

newmtl cave_rock
Ka 0.05 0.06 0.07
Kd 0.16 0.18 0.21
Ks 0.03 0.03 0.04
Ns 12
illum 2

newmtl arena_grass
Ka 0.10 0.25 0.09
Kd 0.31 0.66 0.30
Ks 0.06 0.08 0.05
Ns 20
illum 2

newmtl arena_cliff
Ka 0.18 0.16 0.26
Kd 0.45 0.40 0.62
Ks 0.08 0.07 0.11
Ns 24
illum 2

newmtl arena_wall
Ka 0.23 0.22 0.31
Kd 0.58 0.55 0.72
Ks 0.12 0.11 0.18
Ns 40
illum 2

newmtl arena_dais
Ka 0.26 0.24 0.34
Kd 0.68 0.62 0.82
Ks 0.16 0.14 0.22
Ns 48
illum 2

newmtl goal_dais
Ka 0.27 0.24 0.10
Kd 0.77 0.66 0.24
Ks 0.20 0.17 0.07
Ns 54
illum 2

newmtl garden_mechanism
Ka 0.10 0.24 0.25
Kd 0.30 0.72 0.72
Ks 0.16 0.22 0.22
Ns 46
illum 2

newmtl mechanism_dark
Ka 0.035 0.045 0.060
Kd 0.105 0.135 0.175
Ks 0.16 0.20 0.25
Ns 52
illum 2

newmtl signal_white
Ka 0.32 0.30 0.18
Kd 0.98 0.92 0.48
Ks 0.72 0.68 0.32
Ns 118
illum 2

newmtl hazard_dark
Ka 0.025 0.018 0.022
Kd 0.070 0.045 0.055
Ks 0.10 0.07 0.08
Ns 26
illum 2

newmtl blink_cell_a
Ka 0.045 0.14 0.30
Kd 0.12 0.42 0.92
Ks 0.34 0.62 0.96
Ns 108
illum 2

newmtl blink_cell_b
Ka 0.30 0.045 0.06
Kd 0.92 0.14 0.20
Ks 0.96 0.38 0.42
Ns 108
illum 2

newmtl dash_base
Ka 0.035 0.025 0.030
Kd 0.105 0.075 0.090
Ks 0.28 0.18 0.10
Ns 58
illum 2

newmtl dash_frame
Ka 0.16 0.055 0.012
Kd 0.52 0.17 0.025
Ks 0.48 0.30 0.10
Ns 96
illum 2

newmtl dash_surface
Ka 0.12 0.035 0.006
Kd 0.34 0.075 0.012
Ks 0.42 0.24 0.08
Ns 112
illum 2

newmtl dash_rail
Ka 0.26 0.09 0.012
Kd 0.78 0.27 0.025
Ks 0.60 0.38 0.12
Ns 124
illum 2

newmtl dash_light
Ka 0.42 0.28 0.025
Kd 1.00 0.72 0.08
Ks 0.86 0.68 0.28
Ns 146
illum 2

newmtl shore_sand
Ka 0.22 0.18 0.10
Kd 0.72 0.61 0.34
Ks 0.04 0.04 0.03
Ns 12
illum 2

newmtl backdrop_grass
Ka 0.06 0.15 0.04
Kd 0.20 0.43 0.14
Ks 0.02 0.03 0.01
Ns 8
illum 2

newmtl backdrop_cliff
Ka 0.12 0.09 0.08
Kd 0.34 0.27 0.24
Ks 0.02 0.02 0.02
Ns 8
illum 2
"""


BUILDERS = {
    "star_garden_course_v4": build_course_v4,
    "star_garden_course_v3": build_course_v3,
    "star_garden_course_v2": build_course_v2,
    "star_garden_backdrop_v2": build_backdrop_v2,
    "star_garden_entry_courtyard": build_entry_courtyard,
    "star_garden_cliff_pass": build_cliff_pass,
    "star_garden_upper_orchard": build_upper_orchard,
    "star_garden_prism_arena": build_prism_arena,
    "star_garden_goal_keep": build_goal_keep,
    "star_garden_ruin_bridge": build_ruin_bridge,
    "star_garden_support_platform": build_support_platform,
    "star_garden_waterworks": build_waterworks,
    "star_garden_gimmick_platform": build_gimmick_platform,
    "star_garden_moving_platform": build_moving_platform,
    "star_garden_lift_platform": build_lift_platform,
    "star_garden_rotating_platform": build_rotating_platform,
    "star_garden_seesaw_platform": build_seesaw_platform,
    "star_garden_sinking_platform": build_sinking_platform,
    "star_garden_oneway_platform": build_oneway_platform,
    "star_garden_trampoline_platform": build_trampoline_platform,
    "star_garden_collapse_platform": build_collapse_platform,
    "star_garden_appearing_marker": build_appearing_marker,
    "star_garden_glass_panel": build_glass_panel,
    "star_garden_spike_marker": build_spike_marker,
    "star_garden_east_rampart": build_east_rampart,
    "star_garden_ride_platform": build_ride_platform,
    "star_garden_toggle_switch": build_toggle_switch,
    "star_garden_blink_platform": build_blink_platform,
    "star_garden_linked_platform": build_linked_platform,
    "star_garden_dash_panel": build_dash_panel,
    "star_garden_checkpoint": build_checkpoint,
    "star_garden_giant_gate": build_giant_gate,
    "star_garden_backdrop_ridges": build_backdrop_ridges,
}

DEFAULT_BUILDERS = (
    "star_garden_course_v4",
    "star_garden_backdrop_v2",
    "star_garden_appearing_marker",
    "star_garden_glass_panel",
    "star_garden_gimmick_platform",
    "star_garden_collapse_platform",
    "star_garden_moving_platform",
    "star_garden_lift_platform",
    "star_garden_rotating_platform",
    "star_garden_seesaw_platform",
    "star_garden_sinking_platform",
    "star_garden_oneway_platform",
    "star_garden_trampoline_platform",
    "star_garden_toggle_switch",
    "star_garden_blink_platform",
    "star_garden_linked_platform",
    "star_garden_dash_panel",
    "star_garden_checkpoint",
    "star_garden_giant_gate",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", choices=sorted(BUILDERS), action="append", help="生成対象。省略時は全モデル")
    args = parser.parse_args()

    # 通常実行では現行Stage 1が参照するモデルだけを生成し、廃止した試作地形を戻さない。
    selected = args.model or list(DEFAULT_BUILDERS)
    for name in selected:
        mesh = BUILDERS[name]()
        mesh.write(MODEL_ROOT / name)
        print(f"生成: {name} ({len(mesh.vertices)} vertices / {len(mesh.faces)} faces)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
