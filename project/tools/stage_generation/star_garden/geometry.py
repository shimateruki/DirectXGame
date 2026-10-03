"""地形・足場の幾何検証。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Any, Iterable

from .common import (
    BRIDGE_SPECS,
    INTEGRATED_COLLIDER_VISUAL_GROUPS,
    MAIN_ROUTE_STRIP_SPECS,
    MODEL_ROOT,
    critical_visible_support_colliders,
    stage_v3_floor_specs,
    visible_support_name,
    yaw_from_xz_direction,
)

def validate_model_reference(model_name: str) -> bool:
    if not model_name:
        return True
    relative = Path(*model_name.split("/"))
    directory = MODEL_ROOT / relative
    if not directory.is_dir():
        return False
    candidates = [
        path for path in directory.iterdir()
        if path.is_file() and path.suffix.lower() in {".obj", ".gltf", ".glb", ".fbx"}
    ]
    for path in candidates:
        if path.suffix.lower() != ".obj":
            return path.stat().st_size > 64
        vertices: list[tuple[float, float, float]] = []
        face_count = 0
        with path.open("r", encoding="utf-8", errors="ignore") as source:
            for line in source:
                if line.startswith("v "):
                    values = line.split()
                    if len(values) >= 4:
                        vertices.append((float(values[1]), float(values[2]), float(values[3])))
                elif line.startswith("f "):
                    face_count += 1
        if len(vertices) < 3 or face_count == 0:
            continue
        spans = [max(axis) - min(axis) for axis in zip(*vertices)]
        if max(spans) > 0.001:
            return True
    return False


def load_obj_top_faces_in_engine_space(model_name: str) -> list[tuple[float, list[tuple[float, float]]]]:
    """Assimpの左手系変換後にゲーム内で見えるOBJ上面を読み取る。"""
    relative = Path(*model_name.split("/"))
    directory = MODEL_ROOT / relative
    candidates = sorted(directory.glob("*.obj"))
    if not candidates:
        return []

    vertices: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    faces: list[tuple[float, list[tuple[float, float]]]] = []
    with candidates[0].open("r", encoding="utf-8", errors="ignore") as source:
        for line in source:
            if line.startswith("v "):
                values = line.split()
                if len(values) >= 4:
                    # Model.Loader.cppのaiProcess_ConvertToLeftHandedと同じZ反転を適用する。
                    vertices.append((float(values[1]), float(values[2]), -float(values[3])))
            elif line.startswith("vn "):
                values = line.split()
                if len(values) >= 4:
                    normals.append((float(values[1]), float(values[2]), -float(values[3])))
            elif line.startswith("f "):
                tokens = line.split()[1:]
                if len(tokens) < 3:
                    continue
                try:
                    vertex_indices = [int(token.split("/")[0]) - 1 for token in tokens]
                    normal_token = tokens[0].split("/")
                    normal_index = int(normal_token[2]) - 1 if len(normal_token) >= 3 and normal_token[2] else -1
                    points = [vertices[index] for index in vertex_indices]
                except (IndexError, ValueError):
                    continue
                if normal_index < 0 or normal_index >= len(normals) or normals[normal_index][1] <= 0.5:
                    continue
                top_y = sum(point[1] for point in points) / len(points)
                if max(abs(point[1] - top_y) for point in points) > 0.02:
                    continue
                faces.append((top_y, [(point[0], point[2]) for point in points]))
    return faces


def collider_world_box(entry: dict[str, Any]) -> dict[str, Any] | None:
    """ローカルコライダーを、XZ平面のワールドOBBと上下端へ変換する。"""
    collider = entry.get("collider", {})
    if int(collider.get("type", 0)) == 0:
        return None

    position = [float(value) for value in entry.get("position", [0.0, 0.0, 0.0])]
    scale = [float(value) for value in entry.get("scale", [1.0, 1.0, 1.0])]
    rotation = [float(value) for value in entry.get("rotation", [0.0, 0.0, 0.0])]
    local_center = [float(value) for value in collider.get("center", [0.0, 0.0, 0.0])]
    local_half = [float(value) for value in collider.get("size", [0.0, 0.0, 0.0])]
    collider_rotation = [float(value) for value in collider.get("rotation", [0.0, 0.0, 0.0])]

    yaw = rotation[1] + collider_rotation[1]
    cosine = math.cos(rotation[1])
    sine = math.sin(rotation[1])
    scaled_center_x = local_center[0] * scale[0]
    scaled_center_z = local_center[2] * scale[2]
    center_x = position[0] + scaled_center_x * cosine + scaled_center_z * sine
    center_z = position[2] - scaled_center_x * sine + scaled_center_z * cosine
    center_y = position[1] + local_center[1] * scale[1]
    half_x = abs(local_half[0] * scale[0])
    half_y = abs(local_half[1] * scale[1])
    half_z = abs(local_half[2] * scale[2])

    axis_x = (math.cos(yaw), -math.sin(yaw))
    axis_z = (math.sin(yaw), math.cos(yaw))
    vertices = [
        (
            center_x + axis_x[0] * half_x * sign_x + axis_z[0] * half_z * sign_z,
            center_z + axis_x[1] * half_x * sign_x + axis_z[1] * half_z * sign_z,
        )
        for sign_x, sign_z in ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0))
    ]
    return {
        "center": (center_x, center_y, center_z),
        "half": (half_x, half_y, half_z),
        "axes": (axis_x, axis_z),
        "vertices": vertices,
        "bottom": center_y - half_y,
        "top": center_y + half_y,
    }


def point_inside_world_box(box: dict[str, Any], x: float, z: float, margin: float = 0.0) -> bool:
    center_x, _, center_z = box["center"]
    half_x, _, half_z = box["half"]
    axis_x, axis_z = box["axes"]
    dx = x - center_x
    dz = z - center_z
    local_x = dx * axis_x[0] + dz * axis_x[1]
    local_z = dx * axis_z[0] + dz * axis_z[1]
    return abs(local_x) <= half_x + margin and abs(local_z) <= half_z + margin


def point_inside_terrain_collider(entry: dict[str, Any], x: float, z: float) -> bool:
    box = collider_world_box(entry)
    return bool(box and point_inside_world_box(box, x, z))


def projected_interval(vertices: list[tuple[float, float]], axis: tuple[float, float]) -> tuple[float, float]:
    values = [vertex[0] * axis[0] + vertex[1] * axis[1] for vertex in vertices]
    return min(values), max(values)


def oriented_boxes_overlap(first: dict[str, Any], second: dict[str, Any], tolerance: float = 1.0e-6) -> bool:
    axes = (*first["axes"], *second["axes"])
    for axis in axes:
        first_min, first_max = projected_interval(first["vertices"], axis)
        second_min, second_max = projected_interval(second["vertices"], axis)
        if first_max < second_min - tolerance or second_max < first_min - tolerance:
            return False
    return True


def point_segment_distance(
    point: tuple[float, float],
    start: tuple[float, float],
    end: tuple[float, float],
) -> float:
    edge_x = end[0] - start[0]
    edge_z = end[1] - start[1]
    edge_length_squared = edge_x * edge_x + edge_z * edge_z
    if edge_length_squared <= 1.0e-12:
        return math.hypot(point[0] - start[0], point[1] - start[1])
    projection = ((point[0] - start[0]) * edge_x + (point[1] - start[1]) * edge_z) / edge_length_squared
    projection = max(0.0, min(1.0, projection))
    nearest_x = start[0] + edge_x * projection
    nearest_z = start[1] + edge_z * projection
    return math.hypot(point[0] - nearest_x, point[1] - nearest_z)


def point_inside_polygon(point: tuple[float, float], polygon: list[tuple[float, float]]) -> bool:
    """境界を含めてXZ平面上の多角形内判定を行う。"""
    if len(polygon) < 3:
        return False
    edges = list(zip(polygon, polygon[1:] + polygon[:1]))
    if any(point_segment_distance(point, start, end) <= 0.02 for start, end in edges):
        return True

    inside = False
    x, z = point
    for start, end in edges:
        if (start[1] > z) == (end[1] > z):
            continue
        crossing_x = start[0] + (z - start[1]) * (end[0] - start[0]) / (end[1] - start[1])
        if x < crossing_x:
            inside = not inside
    return inside


def validate_v3_course_mesh_alignment(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """実際のOBJ読込後の地形上面と固定床コライダーを直接照合する。"""
    errors: list[str] = []
    top_faces = load_obj_top_faces_in_engine_space("Stages/star_garden_course_v3")
    if not top_faces:
        return ["Stage 1 V3地形OBJの上面を読み取れません。"]

    for label, _position, _half_size, _yaw in stage_v3_floor_specs():
        name = f"Stage1_Collision_V3_{label}"
        box = collider_world_box(by_name.get(name, {}))
        if box is None:
            continue
        center_x, _center_y, center_z = box["center"]
        half_x, _half_y, half_z = box["half"]
        axis_x, axis_z = box["axes"]
        samples = [(center_x, center_z)]
        for sign_x, sign_z in ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)):
            samples.append((
                center_x + axis_x[0] * half_x * sign_x * 0.55 + axis_z[0] * half_z * sign_z * 0.55,
                center_z + axis_x[1] * half_x * sign_x * 0.55 + axis_z[1] * half_z * sign_z * 0.55,
            ))
        surface_y = float(box["top"])
        for sample_x, sample_z in samples:
            supported = any(
                abs(face_y - surface_y) <= 0.03 and point_inside_polygon((sample_x, sample_z), polygon)
                for face_y, polygon in top_faces
            )
            if not supported:
                errors.append(
                    f"V3地形OBJと固定床判定がずれています: {name} "
                    f"sample=({sample_x:.2f}, {surface_y:.2f}, {sample_z:.2f})"
                )
                break
    return errors


def oriented_box_edge_gap(first: dict[str, Any], second: dict[str, Any]) -> float:
    """二つの床OBBが重なる場合は0、離れている場合は床端同士の最短距離を返す。"""
    if oriented_boxes_overlap(first, second):
        return 0.0
    minimum = math.inf
    for source, target in ((first, second), (second, first)):
        target_vertices = target["vertices"]
        target_edges = list(zip(target_vertices, target_vertices[1:] + target_vertices[:1]))
        for vertex in source["vertices"]:
            for edge_start, edge_end in target_edges:
                minimum = min(minimum, point_segment_distance(vertex, edge_start, edge_end))
    return minimum


def validate_floor_link(
    label: str,
    start: dict[str, Any] | None,
    end: dict[str, Any] | None,
    maximum_gap: float,
    maximum_height_delta: float,
) -> list[str]:
    errors: list[str] = []
    if start is None or end is None:
        errors.append(f"{label}の床または当たり判定が不足しています。")
        return errors
    start_box = collider_world_box(start)
    end_box = collider_world_box(end)
    if start_box is None or end_box is None:
        errors.append(f"{label}に有効な床コライダーがありません。")
        return errors

    gap = oriented_box_edge_gap(start_box, end_box)
    height_delta = abs(float(start_box["top"]) - float(end_box["top"]))
    if gap > maximum_gap + 0.01:
        errors.append(f"{label}の床端間隔が広すぎます: {gap:.2f}m (許容 {maximum_gap:.2f}m)")
    if height_delta > maximum_height_delta + 0.01:
        errors.append(
            f"{label}の歩行面高低差が大きすぎます: {height_delta:.2f}m "
            f"(許容 {maximum_height_delta:.2f}m)"
        )
    return errors


def validate_mechanical_collider_tops(objects: list[dict[str, Any]]) -> list[str]:
    """専用床モデルの見た目上面とコライダー上面が一致することを確認する。"""
    errors: list[str] = []
    model_top_by_type = {
        "MovingFloor": 0.51,
        "RotatingFloor": 0.34,
        "SeesawFloor": 0.84,
        "SinkingFloor": 0.52,
        "Trampoline": 0.68,
        "OneWayFloor": 0.30,
        "ChainCollapseFloor": 0.40,
        "HazardRideFloor": 0.84,
        "AppearingFloor": 0.42,
    }
    for entry in objects:
        gimmick_type = entry.get("gimmickType")
        if gimmick_type not in model_top_by_type:
            continue
        box = collider_world_box(entry)
        if box is None:
            errors.append(f"{entry.get('name', gimmick_type)}に床コライダーがありません。")
            continue
        position_y = float(entry.get("position", [0.0, 0.0, 0.0])[1])
        scale_y = abs(float(entry.get("scale", [1.0, 1.0, 1.0])[1]))
        expected_top = position_y + model_top_by_type[gimmick_type] * scale_y
        if abs(float(box["top"]) - expected_top) > 0.03:
            errors.append(
                f"{entry.get('name', gimmick_type)}の見た目上面とコライダー上面が一致しません: "
                f"model={expected_top:.2f}, collider={float(box['top']):.2f}"
            )
    return errors


def validate_v3_glass_vault(objects: list[dict[str, Any]]) -> list[str]:
    """スター1のガラス入口が、見た目と判定の両方で回り込み不能な開口を塞ぐことを確認する。"""
    errors: list[str] = []
    panels = {
        str(entry.get("name", "")): entry
        for entry in objects
        if str(entry.get("name", "")).startswith("Stage1_V3_GlassVault_")
    }
    expected = {
        f"Stage1_V3_GlassVault_{row}_{column}"
        for row in range(1, 3)
        for column in range(1, 4)
    }
    if set(panels) != expected:
        errors.append("スター1のガラス入口が3列×2段でそろっていません。")
        return errors

    bounds_x: list[tuple[float, float]] = []
    bounds_y: list[tuple[float, float]] = []
    for name, panel in panels.items():
        box = collider_world_box(panel)
        if (
            panel.get("modelName") != "Stages/star_garden_glass_panel"
            or panel.get("materialType") != 10
            or box is None
        ):
            errors.append(f"{name}のガラスモデル、屈折マテリアル、または当たり判定が不正です。")
            continue
        center_x, center_y, _center_z = box["center"]
        half_x, half_y, _half_z = box["half"]
        bounds_x.append((center_x - half_x, center_x + half_x))
        bounds_y.append((center_y - half_y, center_y + half_y))

    if bounds_x and (min(bound[0] for bound in bounds_x) > -278.08 or max(bound[1] for bound in bounds_x) < -261.92):
        errors.append("スター1のガラス入口に左右から通れる隙間があります。")
    if bounds_y and (min(bound[0] for bound in bounds_y) > 0.02 or max(bound[1] for bound in bounds_y) < 8.38):
        errors.append("スター1のガラス入口に上下から通れる隙間があります。")

    wall_names = (
        "Stage1_WallCollision_V3_BombGrottoBack",
        "Stage1_WallCollision_V3_BombGrottoWest",
        "Stage1_WallCollision_V3_BombGrottoEast",
    )
    for wall_name in wall_names:
        box = collider_world_box(next((entry for entry in objects if entry.get("name") == wall_name), {}))
        if box is None or float(box["top"]) < 9.98:
            errors.append(f"{wall_name}が低く、スター1へ回り込めます。")
    return errors


def validate_bomb_grotto_wall(objects: list[dict[str, Any]]) -> list[str]:
    """ボム洞窟の入口が5列×3段のブロックで隙間なく塞がれていることを確認する。"""
    errors: list[str] = []
    blocks = [entry for entry in objects if entry.get("name", "").startswith("Stage1_BombGrotto_Wall_")]
    indexed: dict[tuple[int, int], dict[str, Any]] = {}
    for entry in blocks:
        suffix = entry.get("name", "").removeprefix("Stage1_BombGrotto_Wall_")
        parts = suffix.split("_")
        if len(parts) != 2 or not all(part.isdigit() for part in parts):
            errors.append(f"ボム洞窟壁の名前形式が不正です: {entry.get('name', '')}")
            continue
        indexed[(int(parts[0]), int(parts[1]))] = entry

    expected_indices = {(row, column) for row in range(1, 4) for column in range(1, 6)}
    if len(blocks) != 15 or set(indexed) != expected_indices:
        errors.append("ボム洞窟の壊れる壁が5列×3段でそろっていません。")
        return errors

    opening_min_x = -244.375
    opening_max_x = -231.625
    opening_bottom = 0.0
    opening_top = 7.65
    row_bounds: list[tuple[float, float]] = []
    for row in range(1, 4):
        horizontal_bounds: list[tuple[float, float]] = []
        vertical_bounds: list[tuple[float, float]] = []
        for column in range(1, 6):
            box = collider_world_box(indexed[(row, column)])
            if box is None:
                errors.append(f"ボム洞窟壁 {row}-{column} にコライダーがありません。")
                continue
            minimum_x = min(vertex[0] for vertex in box["vertices"])
            maximum_x = max(vertex[0] for vertex in box["vertices"])
            minimum_z = min(vertex[1] for vertex in box["vertices"])
            maximum_z = max(vertex[1] for vertex in box["vertices"])
            if not minimum_z - 0.01 <= -42.7 <= maximum_z + 0.01:
                errors.append(f"ボム洞窟壁 {row}-{column} が入口面から外れています。")
            horizontal_bounds.append((minimum_x, maximum_x))
            vertical_bounds.append((float(box["bottom"]), float(box["top"])))
        if len(horizontal_bounds) != 5:
            continue
        horizontal_bounds.sort()
        for column, (previous, current) in enumerate(zip(horizontal_bounds, horizontal_bounds[1:]), start=1):
            gap = current[0] - previous[1]
            if gap > 0.02:
                errors.append(f"ボム洞窟壁の第{row}段 {column}～{column + 1}列に{gap:.3f}mの隙間があります。")
        if horizontal_bounds[0][0] > opening_min_x + 0.01 or horizontal_bounds[-1][1] < opening_max_x - 0.01:
            errors.append(f"ボム洞窟壁の第{row}段が入口幅を完全に覆っていません。")
        row_bounds.append((min(bound[0] for bound in vertical_bounds), max(bound[1] for bound in vertical_bounds)))

    row_bounds.sort()
    for row, (previous, current) in enumerate(zip(row_bounds, row_bounds[1:]), start=1):
        gap = current[0] - previous[1]
        if gap > 0.02:
            errors.append(f"ボム洞窟壁の第{row}段と第{row + 1}段に{gap:.3f}mの隙間があります。")
    if row_bounds and (row_bounds[0][0] > opening_bottom + 0.01 or row_bounds[-1][1] < opening_top - 0.01):
        errors.append("ボム洞窟壁が入口の上下を完全に封鎖していません。")
    return errors


def validate_star_supports(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """各スターが攻略先の専用支持台上に置かれていることを確認する。"""
    errors: list[str] = []
    support_by_star = {
        "Stage1_StarCoin_01": "Stage1_Collision_CaveStarPedestalCap",
        "Stage1_StarCoin_02": "Stage1_Collision_OrchardSecretArrival",
        "Stage1_StarCoin_03": "Stage1_Collision_PrismRewardPedestalCap",
    }
    for star_name, support_name in support_by_star.items():
        star = by_name.get(star_name)
        support = by_name.get(support_name)
        if star is None or support is None:
            errors.append(f"{star_name}または専用支持台{support_name}が不足しています。")
            continue
        support_box = collider_world_box(support)
        if support_box is None:
            errors.append(f"{support_name}に有効なコライダーがありません。")
            continue
        star_position = [float(value) for value in star.get("position", [0.0, 0.0, 0.0])]
        if not point_inside_world_box(support_box, star_position[0], star_position[2], margin=-0.05):
            errors.append(f"{star_name}が専用支持台{support_name}の上にありません。")
        height = star_position[1] - float(support_box["top"])
        if height < 1.8 or height > 3.2:
            errors.append(
                f"{star_name}と専用支持台の高さ関係が不正です: {height:.2f}m "
                "(許容 1.80～3.20m)"
            )
    return errors


def validate_visible_support_models(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """重要な固定判定と専用の可視足場が一対一で一致することを確認する。"""
    errors: list[str] = []
    expected_model = "Stages/star_garden_support_platform"
    for collider_name in critical_visible_support_colliders():
        visual_name = visible_support_name(collider_name)
        collider = by_name.get(collider_name)
        visual = by_name.get(visual_name)
        if collider is None:
            errors.append(f"攻略用の固定判定{collider_name}が不足しています。")
            continue
        if visual is None:
            errors.append(f"{collider_name}と対になる可視足場{visual_name}が不足しています。")
            continue
        if visual.get("modelName") != expected_model:
            errors.append(f"{visual_name}に専用の固定足場モデルが設定されていません。")

        collider_data = collider.get("collider", {})
        collider_position = [float(value) for value in collider.get("position", [0.0, 0.0, 0.0])]
        collider_rotation = [float(value) for value in collider.get("rotation", [0.0, 0.0, 0.0])]
        half_size = [float(value) for value in collider_data.get("size", [0.0, 0.0, 0.0])]
        visual_position = [float(value) for value in visual.get("position", [0.0, 0.0, 0.0])]
        visual_rotation = [float(value) for value in visual.get("rotation", [0.0, 0.0, 0.0])]
        visual_scale = [float(value) for value in visual.get("scale", [0.0, 0.0, 0.0])]
        expected_position = [
            collider_position[0],
            collider_position[1] + half_size[1],
            collider_position[2],
        ]

        if any(abs(actual - expected) > 0.01 for actual, expected in zip(visual_position, expected_position)):
            errors.append(f"{visual_name}の位置が{collider_name}の歩行面と一致していません。")
        if any(abs(actual - expected) > 0.01 for actual, expected in zip(visual_scale, half_size)):
            errors.append(f"{visual_name}の外形が{collider_name}の当たり判定と一致していません。")
        rotation_delta = max(
            abs(math.remainder(actual - expected, math.tau))
            for actual, expected in zip(visual_rotation, collider_rotation)
        )
        if rotation_delta > 0.001:
            errors.append(f"{visual_name}の向きが{collider_name}の当たり判定と一致していません。")
    return errors


def validate_integrated_collider_visual_contract(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """固定床が、統合地形・専用足場・橋・階段のいずれかへ必ず紐づくことを確認する。"""
    errors: list[str] = []
    accounted = set(critical_visible_support_colliders())

    for visual_name, expected_model, collider_names in INTEGRATED_COLLIDER_VISUAL_GROUPS:
        visual = by_name.get(visual_name)
        if visual is None or visual.get("modelName") != expected_model:
            errors.append(f"統合地形{visual_name}のモデル参照が不正です。")
        for collider_name in collider_names:
            accounted.add(collider_name)
            if collider_name not in by_name:
                errors.append(f"統合地形{visual_name}に必要な{collider_name}が不足しています。")

    for label, _start, _end, _surface_y in BRIDGE_SPECS:
        accounted.add(f"Stage1_Collision_{label}")

    for index in range(1, 9):
        name = f"Stage1_Collision_CliffStep_{index:02d}"
        accounted.add(name)
        if name not in by_name:
            errors.append(f"崖道の見える階段に対応する{name}が不足しています。")
    for index in range(1, 4):
        name = f"Stage1_Collision_EastRampartStep_{index:02d}"
        accounted.add(name)
        if name not in by_name:
            errors.append(f"城壁の見える階段に対応する{name}が不足しています。")

    fixed_floor_names = {
        name
        for name, entry in by_name.items()
        if entry.get("type") == "InvisibleBox" and name.startswith("Stage1_Collision_")
    }
    unaccounted = sorted(fixed_floor_names - accounted)
    if unaccounted:
        errors.append(
            "可視モデルとの対応が宣言されていない固定床があります: "
            + ", ".join(unaccounted)
        )
    return errors


def validate_main_route_strips(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """広い地形上の本道を示す石帯が、設計座標と一致することを確認する。"""
    errors: list[str] = []
    for label, start, end, surface_y, width in MAIN_ROUTE_STRIP_SPECS:
        name = f"Stage1_RouteStrip_{label}"
        strip = by_name.get(name)
        if strip is None:
            errors.append(f"本道の見える石帯{name}が不足しています。")
            continue
        if strip.get("modelName") != "Stages/star_garden_support_platform":
            errors.append(f"{name}に石床モデルが設定されていません。")
            continue

        dx = end[0] - start[0]
        dz = end[1] - start[1]
        expected_position = [
            (start[0] + end[0]) * 0.5,
            surface_y + 0.035,
            (start[1] + end[1]) * 0.5,
        ]
        expected_scale = [math.hypot(dx, dz) * 0.5, 0.055, width * 0.5]
        expected_yaw = yaw_from_xz_direction(dx, dz)
        position = [float(value) for value in strip.get("position", [0.0, 0.0, 0.0])]
        scale = [float(value) for value in strip.get("scale", [0.0, 0.0, 0.0])]
        yaw = float(strip.get("rotation", [0.0, 0.0, 0.0])[1])
        if any(abs(actual - expected) > 0.01 for actual, expected in zip(position, expected_position)):
            errors.append(f"{name}の位置が本道設計からずれています。")
        if any(abs(actual - expected) > 0.01 for actual, expected in zip(scale, expected_scale)):
            errors.append(f"{name}の幅または長さが本道設計からずれています。")
        if abs(math.remainder(yaw - expected_yaw, math.tau)) > 0.001:
            errors.append(f"{name}の向きが本道設計からずれています。")
    return errors


def validate_supported_guide_coins(objects: list[dict[str, Any]]) -> list[str]:
    """地上の案内コインが、透明な隙間や床面の下へ置かれていないことを確認する。"""
    errors: list[str] = []
    required_prefixes = (
        "Stage1_MainGuide_",
        "Stage1_BombGrotto_Guide_",
        "Stage1_BridgeGuide_",
        "Stage1_Secret_Guide_",
    )
    support_gimmicks = {
        "AppearingFloor",
        "Trampoline",
        "OneWayFloor",
        "ChainCollapseFloor",
        "SeesawFloor",
        "MovingFloor",
        "RotatingFloor",
        "SinkingFloor",
        "HazardRideFloor",
    }
    supports = [
        entry
        for entry in objects
        if (
            entry.get("type") == "InvisibleBox"
            and entry.get("name", "").startswith("Stage1_Collision_")
        )
        or entry.get("gimmickType") in support_gimmicks
    ]
    support_boxes = [
        (entry.get("name", ""), collider_world_box(entry))
        for entry in supports
    ]

    for coin in objects:
        name = coin.get("name", "")
        if coin.get("gimmickType") != "Coin" or not name.startswith(required_prefixes):
            continue
        position = [float(value) for value in coin.get("position", [0.0, 0.0, 0.0])]
        candidates = [
            (support_name, box)
            for support_name, box in support_boxes
            if box is not None and point_inside_world_box(box, position[0], position[2], margin=0.30)
        ]
        if not candidates:
            errors.append(f"案内コイン{name}の真下に歩行床がありません。")
            continue
        support_name, support_box = min(
            candidates,
            key=lambda item: abs(position[1] - float(item[1]["top"])),
        )
        height = position[1] - float(support_box["top"])
        if height < 0.30 or height > 4.20:
            errors.append(
                f"案内コイン{name}と{support_name}の高さ関係が不正です: {height:.2f}m"
            )
    return errors


def validate_bridge_alignment(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """石橋の見た目・透明コライダー・両端の固定着地点を確認する。"""
    errors: list[str] = []
    for label, start, end, surface_y in BRIDGE_SPECS:
        visual_name = f"Stage1_Ruins_{label}"
        collider_name = f"Stage1_Collision_{label}"
        visual = by_name.get(visual_name)
        collider = by_name.get(collider_name)
        if visual is None or collider is None:
            errors.append(f"石橋{label}の見た目または当たり判定が不足しています。")
            continue
        if visual.get("modelName") != "Stages/star_garden_ruin_bridge":
            errors.append(f"{visual_name}に石橋専用モデルが設定されていません。")

        visual_position = [float(value) for value in visual.get("position", [0.0, 0.0, 0.0])]
        collider_position = [float(value) for value in collider.get("position", [0.0, 0.0, 0.0])]
        visual_rotation = [float(value) for value in visual.get("rotation", [0.0, 0.0, 0.0])]
        collider_rotation = [float(value) for value in collider.get("rotation", [0.0, 0.0, 0.0])]
        collider_size = [float(value) for value in collider.get("collider", {}).get("size", [0.0, 0.0, 0.0])]
        visual_scale = [float(value) for value in visual.get("scale", [1.0, 1.0, 1.0])]

        horizontal_offset = math.hypot(
            visual_position[0] - collider_position[0],
            visual_position[2] - collider_position[2],
        )
        yaw_delta = abs(math.remainder(visual_rotation[1] - collider_rotation[1], math.tau))
        visual_length = 48.0 * visual_scale[0]
        collider_length = 2.0 * collider_size[0]
        collider_top = collider_position[1] + collider_size[1]
        if horizontal_offset > 0.01 or yaw_delta > 0.001:
            errors.append(f"{label}の見た目と当たり判定の中心または向きが一致していません。")
        if abs(visual_length - collider_length) > 0.05:
            errors.append(f"{label}の見た目と当たり判定の長さが一致していません。")
        if abs(visual_position[1] - collider_top) > 0.05:
            errors.append(f"{label}の歩行面と当たり判定の高さが一致していません。")

        for suffix, endpoint in (("Start", start), ("End", end)):
            abutment_name = f"Stage1_Collision_{label}{suffix}Abutment"
            abutment = by_name.get(abutment_name)
            if abutment is None:
                errors.append(f"{label}の{suffix}側に固定着地点がありません。")
                continue
            position = [float(value) for value in abutment.get("position", [0.0, 0.0, 0.0])]
            rotation = [float(value) for value in abutment.get("rotation", [0.0, 0.0, 0.0])]
            size = [float(value) for value in abutment.get("collider", {}).get("size", [0.0, 0.0, 0.0])]
            endpoint_offset = math.hypot(position[0] - endpoint[0], position[2] - endpoint[1])
            top = position[1] + size[1]
            if endpoint_offset > 0.01:
                errors.append(f"{label}の{suffix}側固定着地点が橋端からずれています。")
            if abs(top - surface_y) > 0.01:
                errors.append(f"{label}の{suffix}側固定着地点の高さが橋面と一致していません。")
            if abs(math.remainder(rotation[1] - collider_rotation[1], math.tau)) > 0.001:
                errors.append(f"{label}の{suffix}側固定着地点の向きが橋と一致していません。")
    return errors
