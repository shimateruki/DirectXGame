from __future__ import annotations

import argparse
import json
import math
import uuid
from dataclasses import dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
MODEL_ROOT = PROJECT_ROOT / "Resources" / "3DModel" / "Stages"
SCENE_PATH = PROJECT_ROOT / "Resources" / "json" / "3Dobject" / "stageSelect_object.json"
PLAYER_PATH = PROJECT_ROOT / "Resources" / "json" / "3Dobject" / "stageSelect_player.json"

ASSET_NAMESPACE = uuid.UUID("b28be6ad-a834-46f2-baa0-080c50773a1d")
OBJECT_NAMESPACE = uuid.UUID("0c8e91f3-8081-44b5-932a-b61aaf07c34a")

Vec2 = tuple[float, float]
Vec3 = tuple[float, float, float]


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def normalize(value: Vec3) -> Vec3:
    length = math.sqrt(value[0] ** 2 + value[1] ** 2 + value[2] ** 2)
    if length <= 1.0e-8:
        return (0.0, 1.0, 0.0)
    return (value[0] / length, value[1] / length, value[2] / length)


def rotate_y(point: Vec3, yaw: float) -> Vec3:
    sin_yaw = math.sin(yaw)
    cos_yaw = math.cos(yaw)
    return (
        point[0] * cos_yaw + point[2] * sin_yaw,
        point[1],
        -point[0] * sin_yaw + point[2] * cos_yaw,
    )


class ObjBuilder:
    def __init__(self, object_name: str) -> None:
        self.object_name = object_name
        self.vertices: list[Vec3] = []
        self.uvs: list[Vec2] = []
        self.normals: list[Vec3] = []
        self.faces: dict[str, list[tuple[int, int, int]]] = {}

    def vertex(self, position: Vec3, uv: Vec2, normal: Vec3) -> int:
        self.vertices.append(position)
        self.uvs.append(uv)
        self.normals.append(normalize(normal))
        return len(self.vertices)

    def triangle(self, material: str, a: int, b: int, c: int) -> None:
        self.faces.setdefault(material, []).append((a, b, c))

    def face(self, material: str, points: list[Vec3], uvs: list[Vec2]) -> None:
        normal = normalize(cross(sub(points[1], points[0]), sub(points[2], points[0])))
        indices = [self.vertex(point, uv, normal) for point, uv in zip(points, uvs)]
        self.triangle(material, indices[0], indices[1], indices[2])
        if len(indices) == 4:
            self.triangle(material, indices[0], indices[2], indices[3])

    def box(
        self,
        material: str,
        center: Vec3,
        size: Vec3,
        yaw: float = 0.0,
    ) -> None:
        half_x = size[0] * 0.5
        half_y = size[1] * 0.5
        half_z = size[2] * 0.5

        def transform(point: Vec3) -> Vec3:
            rotated = rotate_y(point, yaw)
            return (
                rotated[0] + center[0],
                rotated[1] + center[1],
                rotated[2] + center[2],
            )

        p = {
            "lbf": transform((-half_x, -half_y, half_z)),
            "rbf": transform((half_x, -half_y, half_z)),
            "ltf": transform((-half_x, half_y, half_z)),
            "rtf": transform((half_x, half_y, half_z)),
            "lbb": transform((-half_x, -half_y, -half_z)),
            "rbb": transform((half_x, -half_y, -half_z)),
            "ltb": transform((-half_x, half_y, -half_z)),
            "rtb": transform((half_x, half_y, -half_z)),
        }
        uv = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
        self.face(material, [p["lbf"], p["rbf"], p["rtf"], p["ltf"]], uv)
        self.face(material, [p["rbb"], p["lbb"], p["ltb"], p["rtb"]], uv)
        self.face(material, [p["rbf"], p["rbb"], p["rtb"], p["rtf"]], uv)
        self.face(material, [p["lbb"], p["lbf"], p["ltf"], p["ltb"]], uv)
        self.face(material, [p["ltf"], p["rtf"], p["rtb"], p["ltb"]], uv)
        self.face(material, [p["lbb"], p["rbb"], p["rbf"], p["lbf"]], uv)

    def write(self, output_path: Path) -> None:
        with output_path.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(f"mtllib {self.object_name}.mtl\n")
            stream.write(f"o {self.object_name}\n")
            for vertex in self.vertices:
                stream.write(f"v {vertex[0]:.6f} {vertex[1]:.6f} {vertex[2]:.6f}\n")
            for uv in self.uvs:
                stream.write(f"vt {uv[0]:.6f} {uv[1]:.6f}\n")
            for normal in self.normals:
                stream.write(f"vn {normal[0]:.6f} {normal[1]:.6f} {normal[2]:.6f}\n")
            stream.write("s off\n")
            for material, faces in self.faces.items():
                stream.write(f"usemtl {material}\n")
                for a, b, c in faces:
                    stream.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


def irregular_radius(angle: float, phase: float) -> float:
    return 1.0 + math.sin(angle * 3.0 + phase) * 0.025 + math.sin(angle * 7.0 - phase * 0.7) * 0.014


def ring_position(angle: float, radius_x: float, radius_z: float, y: float, phase: float) -> Vec3:
    radius = irregular_radius(angle, phase)
    return (math.cos(angle) * radius_x * radius, y, math.sin(angle) * radius_z * radius)


def build_island(name: str, radius_x: float, radius_z: float, phase: float) -> ObjBuilder:
    builder = ObjBuilder(name)
    segments = 32
    rings = [
        (0.0, radius_x * 0.96, radius_z * 0.96, "GrassTop"),
        (-0.32, radius_x, radius_z, "GrassEdge"),
        (-1.05, radius_x * 1.02, radius_z * 1.02, "SandRim"),
        (-3.10, radius_x * 0.98, radius_z * 0.98, "CliffWarm"),
        (-5.55, radius_x * 0.91, radius_z * 0.91, "CliffDeep"),
        (-7.20, radius_x * 0.84, radius_z * 0.84, "UnderwaterStone"),
    ]

    center = (0.0, 0.0, 0.0)
    for index in range(segments):
        angle0 = math.tau * index / segments
        angle1 = math.tau * (index + 1) / segments
        point0 = ring_position(angle0, rings[0][1], rings[0][2], 0.0, phase)
        point1 = ring_position(angle1, rings[0][1], rings[0][2], 0.0, phase)
        builder.face(
            "GrassTop",
            [center, point1, point0],
            [(0.5, 0.5), ((point1[0] / radius_x + 1.0) * 0.5, (point1[2] / radius_z + 1.0) * 0.5), ((point0[0] / radius_x + 1.0) * 0.5, (point0[2] / radius_z + 1.0) * 0.5)],
        )

    for ring_index in range(len(rings) - 1):
        upper = rings[ring_index]
        lower = rings[ring_index + 1]
        material = lower[3]
        for index in range(segments):
            angle0 = math.tau * index / segments
            angle1 = math.tau * (index + 1) / segments
            upper0 = ring_position(angle0, upper[1], upper[2], upper[0], phase)
            upper1 = ring_position(angle1, upper[1], upper[2], upper[0], phase)
            lower0 = ring_position(angle0, lower[1], lower[2], lower[0], phase)
            lower1 = ring_position(angle1, lower[1], lower[2], lower[0], phase)
            builder.face(
                material,
                [upper0, upper1, lower1, lower0],
                [(index / segments, 0.0), ((index + 1) / segments, 0.0), ((index + 1) / segments, 1.0), (index / segments, 1.0)],
            )

    bottom_y, bottom_x, bottom_z, bottom_material = rings[-1]
    bottom_center = (0.0, bottom_y, 0.0)
    for index in range(segments):
        angle0 = math.tau * index / segments
        angle1 = math.tau * (index + 1) / segments
        point0 = ring_position(angle0, bottom_x, bottom_z, bottom_y, phase)
        point1 = ring_position(angle1, bottom_x, bottom_z, bottom_y, phase)
        builder.face(
            bottom_material,
            [bottom_center, point0, point1],
            [(0.5, 0.5), ((point0[0] / bottom_x + 1.0) * 0.5, (point0[2] / bottom_z + 1.0) * 0.5), ((point1[0] / bottom_x + 1.0) * 0.5, (point1[2] / bottom_z + 1.0) * 0.5)],
        )
    return builder


def add_cylinder(
    builder: ObjBuilder,
    material: str,
    radius: float,
    y_min: float,
    y_max: float,
    segments: int = 32,
) -> None:
    for index in range(segments):
        angle0 = math.tau * index / segments
        angle1 = math.tau * (index + 1) / segments
        top0 = (math.cos(angle0) * radius, y_max, math.sin(angle0) * radius)
        top1 = (math.cos(angle1) * radius, y_max, math.sin(angle1) * radius)
        bottom0 = (top0[0], y_min, top0[2])
        bottom1 = (top1[0], y_min, top1[2])
        builder.face(material, [(0.0, y_max, 0.0), top1, top0], [(0.5, 0.5), (1.0, 1.0), (0.0, 1.0)])
        builder.face(material, [top0, top1, bottom1, bottom0], [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])
        builder.face(material, [(0.0, y_min, 0.0), bottom0, bottom1], [(0.5, 0.5), (0.0, 0.0), (1.0, 0.0)])


def build_hub_plaza() -> ObjBuilder:
    builder = ObjBuilder("stage_select_hub_plaza")
    add_cylinder(builder, "PlazaStone", 7.4, 0.0, 0.24)
    add_cylinder(builder, "PlazaGold", 8.1, -0.04, 0.10)

    branch_targets = [(-48.0, 28.0), (50.0, 24.0), (6.0, -54.0)]
    for target_x, target_z in branch_targets:
        length = math.sqrt(target_x * target_x + target_z * target_z)
        direction_x = target_x / length
        direction_z = target_z / length
        yaw = math.atan2(direction_x, direction_z)
        for step in range(6):
            distance = 9.0 + step * 2.65
            center = (direction_x * distance, 0.19, direction_z * distance)
            builder.box("PathStone", center, (3.25, 0.22, 2.05), yaw)

    for step in range(2):
        builder.box("PathStone", (0.0, 0.19, 9.6 + step * 2.65), (3.25, 0.22, 2.05), 0.0)
    return builder


def build_gate_pad() -> ObjBuilder:
    builder = ObjBuilder("stage_select_gate_pad")
    add_cylinder(builder, "PadGold", 6.1, 0.0, 0.12)
    add_cylinder(builder, "PadStone", 5.55, 0.10, 0.28)
    add_cylinder(builder, "PadInlay", 3.65, 0.27, 0.34)
    return builder


MATERIALS = {
    "GrassTop": ((0.34, 0.72, 0.18), 22.0),
    "GrassEdge": ((0.24, 0.55, 0.12), 14.0),
    "SandRim": ((0.92, 0.72, 0.36), 10.0),
    "CliffWarm": ((0.52, 0.35, 0.21), 8.0),
    "CliffDeep": ((0.36, 0.27, 0.22), 6.0),
    "UnderwaterStone": ((0.28, 0.34, 0.34), 18.0),
    "PlazaStone": ((0.74, 0.80, 0.76), 34.0),
    "PlazaGold": ((0.90, 0.63, 0.18), 44.0),
    "PathStone": ((0.68, 0.74, 0.70), 28.0),
    "PadStone": ((0.70, 0.76, 0.74), 32.0),
    "PadGold": ((0.88, 0.61, 0.17), 42.0),
    "PadInlay": ((0.22, 0.66, 0.78), 48.0),
}


def write_mtl(path: Path, material_names: list[str]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for name in material_names:
            color, shininess = MATERIALS[name]
            stream.write(f"newmtl {name}\n")
            stream.write(f"Ns {shininess:.3f}\n")
            stream.write("Ka 0.080 0.080 0.080\n")
            stream.write(f"Kd {color[0]:.3f} {color[1]:.3f} {color[2]:.3f}\n")
            stream.write("Ks 0.045 0.045 0.045\n")
            stream.write("d 1.000\n")
            stream.write("illum 2\n\n")


def stable_guid(path: Path) -> str:
    relative = path.relative_to(PROJECT_ROOT).as_posix()
    return uuid.uuid5(ASSET_NAMESPACE, relative).hex


def write_meta(path: Path, asset_type: str) -> None:
    relative = path.relative_to(PROJECT_ROOT).as_posix()
    if asset_type == "Model":
        data = {
            "assetType": "Model",
            "guid": stable_guid(path),
            "importSettings": {"generateTangents": True, "scale": 1.0},
            "importer": "ModelImporter",
            "source": relative,
            "version": 1,
        }
    else:
        data = {
            "assetType": "Binary",
            "guid": stable_guid(path),
            "importSettings": {},
            "importer": "BinaryImporter",
            "source": relative,
            "version": 1,
        }
    path.with_name(path.name + ".meta").write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def export_model(builder: ObjBuilder) -> None:
    output_dir = MODEL_ROOT / builder.object_name
    output_dir.mkdir(parents=True, exist_ok=True)
    obj_path = output_dir / f"{builder.object_name}.obj"
    mtl_path = output_dir / f"{builder.object_name}.mtl"
    builder.write(obj_path)
    write_mtl(mtl_path, list(builder.faces.keys()))
    write_meta(obj_path, "Model")
    write_meta(mtl_path, "Binary")


def object_guid(name: str) -> str:
    return str(uuid.uuid5(OBJECT_NAMESPACE, name))


def quaternion_from_yaw(yaw: float) -> list[float]:
    return [0.0, math.sin(yaw * 0.5), 0.0, math.cos(yaw * 0.5)]


def multiply_quaternion(left: Sequence[float], right: Sequence[float]) -> list[float]:
    lx, ly, lz, lw = left
    rx, ry, rz, rw = right
    return [
        lw * rx + lx * rw + ly * rz - lz * ry,
        lw * ry - lx * rz + ly * rw + lz * rx,
        lw * rz + lx * ry - ly * rx + lz * rw,
        lw * rw - lx * rx - ly * ry - lz * rz,
    ]


def quaternion_from_euler(rotation: Sequence[float]) -> list[float]:
    pitch, yaw, roll = rotation
    qx = [math.sin(pitch * 0.5), 0.0, 0.0, math.cos(pitch * 0.5)]
    qy = [0.0, math.sin(yaw * 0.5), 0.0, math.cos(yaw * 0.5)]
    qz = [0.0, 0.0, math.sin(roll * 0.5), math.cos(roll * 0.5)]
    return multiply_quaternion(multiply_quaternion(qy, qx), qz)


def quaternion_from_yaw_and_roll(yaw: float, roll: float) -> list[float]:
    """エンジンの Z -> X -> Y 回転順に合わせ、Y回転とZ回転を合成します。"""
    sin_yaw = math.sin(yaw * 0.5)
    cos_yaw = math.cos(yaw * 0.5)
    sin_roll = math.sin(roll * 0.5)
    cos_roll = math.cos(roll * 0.5)
    return [
        sin_yaw * sin_roll,
        sin_yaw * cos_roll,
        cos_yaw * sin_roll,
        cos_yaw * cos_roll,
    ]


def default_local_fog() -> dict:
    return {
        "color": [0.2, 0.8, 0.5, 1.0],
        "density": 0.5,
        "edgeFade": 0.0,
        "noiseScale": 0.0,
        "noiseSpeed": 0.0,
        "scatteringG": 0.0,
        "scatteringIntensity": 0.0,
    }


def base_object(
    name: str,
    object_type: str,
    model_name: str,
    position: Vec3,
    scale: Vec3 = (1.0, 1.0, 1.0),
    yaw: float = 0.0,
) -> dict:
    return {
        "animation": {"animName": "", "isAnimLoop": True},
        "autoTextureTiling": False,
        "blendMode": 1,
        "castShadow": True,
        "collider": {
            "center": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [1.0, 1.0, 1.0],
            "type": 0,
        },
        "collisionAttribute": 0,
        "collisionMask": 0,
        "color": [1.0, 1.0, 1.0, 1.0],
        "emissive": 1.0,
        "enableEnvMap": False,
        "enableLighting": True,
        "enableNormalMap": False,
        "enemyType": "",
        "envIntensity": 0.15,
        "eventID": 0,
        "gimmickType": "",
        "gpuParticleName": "",
        "guid": object_guid(name),
        "isStatic": True,
        "isVisible": True,
        "itemType": "",
        "localFog": default_local_fog(),
        "materialType": 0,
        "meshDrawIndex": -1,
        "meshEffect1": "",
        "meshEffect2": "",
        "metallic": 0.0,
        "modelName": model_name,
        "myEventID": -1,
        "name": name,
        "normalMapPath": "",
        "ormMapPath": "",
        "parentGuid": "",
        "parentName": "",
        "particleName": "",
        "position": list(position),
        "quaternion": quaternion_from_yaw(yaw),
        "recorder": {"isRecordLoop": False, "isRecordRelative": False, "recordPathName": ""},
        "rotation": [0.0, yaw, 0.0],
        "roughness": 0.72,
        "saveCategory": "Object",
        "scale": list(scale),
        "targetID": -1,
        "texturePath": "",
        "textureTiling": [1.0, 1.0],
        "type": object_type,
    }


def make_water() -> dict:
    water = base_object(
        "StageSelect_Ocean",
        "Model",
        "Stages/block",
        (0.0, -7.84, 0.0),
        (92.0, 4.97, 82.0),
    )
    water.update(
        {
            "castShadow": False,
            "collisionAttribute": 0,
            "collisionMask": 0,
            "envIntensity": 1.0,
            "materialType": 8,
            "roughness": 0.5,
            "waterParam": {
                "billboardScale": 0.5,
                "effectIntensity": 1.15,
                "effectScale": 0.82,
                "effectScaleX": 8.0,
                "effectScaleY": 0.35,
                "effectScaleZ": 1.15,
                "effectSoftness": 0.48,
                "effectType": 0.0,
                "flowSpeedX": 0.035,
                "flowSpeedY": 0.018,
                "waveFrequency": 4.2,
                "waveHeight": 0.42,
                "waveSpeed": 1.05,
            },
        }
    )
    return water


def make_collision_box(name: str, position: Vec3, size: Vec3, yaw: float = 0.0) -> dict:
    result = base_object(name, "InvisibleBox", "", position, yaw=yaw)
    result.update(
        {
            "castShadow": False,
            "collisionAttribute": 4,
            "collisionMask": 4294967295,
            "enableLighting": False,
            "isVisible": False,
        }
    )
    result["collider"] = {
        "center": [0.0, 0.0, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": list(size),
        "type": 3 if abs(yaw) > 1.0e-5 else 2,
    }
    return result


def make_ellipse_collision_strips(
    name_prefix: str,
    center: Vec2,
    radii: Vec2,
    collision_y: float,
    strip_count: int = 15,
) -> list[dict]:
    """楕円島の外へはみ出さない短冊状Colliderで歩行面を近似します。"""
    if strip_count < 3 or strip_count % 2 == 0:
        raise ValueError("楕円Colliderの分割数には3以上の奇数を指定してください。")

    radius_x, radius_z = radii
    covered_radius_z = radius_z * 0.985
    half_depth = covered_radius_z / float(strip_count)
    strips: list[dict] = []
    for index in range(strip_count):
        center_z = -covered_radius_z + half_depth + index * half_depth * 2.0
        # 境界側だけを基準にすると短冊の接合部に穴ができるため、半分内側で幅を求めます。
        sample_z = min(radius_z, abs(center_z) + half_depth * 0.55)
        normalized_z = sample_z / radius_z
        half_width = radius_x * math.sqrt(max(0.0, 1.0 - normalized_z * normalized_z)) * 1.01
        strips.append(
            make_collision_box(
                f"{name_prefix}_{index}",
                (center[0], collision_y, center[1] + center_z),
                (half_width, 3.65, half_depth * 1.12),
            )
        )
    return strips


def angle_distance(a: float, b: float) -> float:
    return abs((a - b + math.pi) % math.tau - math.pi)


def make_ellipse_boundary_walls(
    name_prefix: str,
    center: Vec2,
    radii: Vec2,
    wall_center_y: float,
    opening_angles: Sequence[float],
    opening_half_angle: float,
    segment_count: int,
) -> list[dict]:
    """橋の接続部だけを空け、島の外周に落下防止用の透明壁を配置します。"""
    walls: list[dict] = []
    for index in range(segment_count):
        start_angle = math.tau * float(index) / float(segment_count)
        end_angle = math.tau * float(index + 1) / float(segment_count)
        middle_angle = (start_angle + end_angle) * 0.5
        if any(angle_distance(middle_angle, opening) < opening_half_angle for opening in opening_angles):
            continue

        start_x = center[0] + math.cos(start_angle) * radii[0]
        start_z = center[1] + math.sin(start_angle) * radii[1]
        end_x = center[0] + math.cos(end_angle) * radii[0]
        end_z = center[1] + math.sin(end_angle) * radii[1]
        delta_x = end_x - start_x
        delta_z = end_z - start_z
        segment_length = math.sqrt(delta_x * delta_x + delta_z * delta_z)
        yaw = math.atan2(-delta_z, delta_x)
        walls.append(
            make_collision_box(
                f"{name_prefix}_{index}",
                ((start_x + end_x) * 0.5, wall_center_y, (start_z + end_z) * 0.5),
                (segment_length * 0.5 + 0.12, 7.0, 0.42),
                yaw,
            )
        )
    return walls


def make_bridge_boundary_walls(index: int, target: Vec2, target_radii: Vec2, wall_center_y: float) -> list[dict]:
    distance = math.sqrt(target[0] ** 2 + target[1] ** 2)
    direction_x = target[0] / distance
    direction_z = target[1] / distance
    right_x = direction_z
    right_z = -direction_x
    hub_radius = 1.0 / math.sqrt((direction_x / 27.0) ** 2 + (direction_z / 23.0) ** 2)
    island_radius = 1.0 / math.sqrt((direction_x / target_radii[0]) ** 2 + (direction_z / target_radii[1]) ** 2)
    gap = max(5.0, distance - hub_radius - island_radius)
    bridge_length = gap + 4.0
    center_distance = hub_radius + gap * 0.5
    center_x = direction_x * center_distance
    center_z = direction_z * center_distance
    yaw = math.atan2(direction_x, direction_z)
    walls: list[dict] = []
    for side_index, side in enumerate((-1.0, 1.0)):
        walls.append(
            make_collision_box(
                f"StageSelect_BridgeBoundary_{index}_{side_index}",
                (center_x + right_x * side * 2.62, wall_center_y, center_z + right_z * side * 2.62),
                (0.42, 7.0, bridge_length * 0.5 + 0.35),
                yaw,
            )
        )
    return walls


def make_bridge(index: int, target: Vec2, target_radii: Vec2) -> dict:
    distance = math.sqrt(target[0] ** 2 + target[1] ** 2)
    direction_x = target[0] / distance
    direction_z = target[1] / distance
    hub_radius = 1.0 / math.sqrt((direction_x / 27.0) ** 2 + (direction_z / 23.0) ** 2)
    island_radius = 1.0 / math.sqrt((direction_x / target_radii[0]) ** 2 + (direction_z / target_radii[1]) ** 2)
    gap = max(5.0, distance - hub_radius - island_radius)
    bridge_length = gap + 4.0
    center_distance = hub_radius + gap * 0.5
    position = (direction_x * center_distance, -2.15, direction_z * center_distance)
    yaw = math.atan2(direction_x, direction_z)
    bridge = base_object(
        f"StageSelect_Bridge_{index}",
        "Model",
        "Stages/stage_select_bridge",
        position,
        (2.35, 1.0, bridge_length / 1.883096),
        yaw,
    )
    bridge.update(
        {
            "collisionAttribute": 4,
            "collisionMask": 4294967295,
            "envIntensity": 0.24,
            "roughness": 0.78,
        }
    )
    bridge["collider"] = {
        "center": [0.0, 1.02, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [0.94, 0.16, 0.93],
        "type": 3,
    }
    return bridge


def gate_water_param() -> dict:
    return {
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


def make_gate(name: str, position: Vec3, yaw: float, target_id: int, mode: int, target_scene: str) -> dict:
    gate = base_object(
        name,
        "Gimmick",
        "Gimmicks/crown_stage_gate",
        position,
        (2.05, 2.05, 0.94),
        yaw,
    )
    gate.update(
        {
            "castShadow": False,
            "collisionAttribute": 16,
            "collisionMask": 1,
            "color": [0.35, 0.75, 1.0, 1.0],
            "emissive": 2.0,
            "enableEnvMap": False,
            "envIntensity": 0.8,
            "gimmickType": "StageGate",
            "isStatic": False,
            "materialType": 22,
            "roughness": 0.5,
            "targetID": target_id,
            "waterParam": gate_water_param(),
            "param": {
                "actionMode": mode,
                "gimmickType": "StageGate",
                "targetScene": target_scene,
                "startActive": True,
                "returnOnOff": True,
                "moveSpeed": 6.0,
                "speed": 1.0,
                "hp": 100.0,
                "maxHp": 100.0,
            },
        }
    )
    gate["collider"] = {
        "center": [0.0, 0.15, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [0.72, 0.78, 0.20],
        "type": 3,
    }
    return gate


def make_gate_frame(name: str, position: Vec3, facing_yaw: float) -> dict:
    # このモデルはZ回転で起こした後のローカル+Xが正面になるため、
    # ローカル+Zが正面のポータルよりY回転を90度補正します。
    frame_yaw = facing_yaw - math.pi * 0.5
    frame = base_object(name, "Model", "Stages/gate", position, (1.0, 1.0, 1.0), frame_yaw)
    frame.update({"envIntensity": 0.28, "roughness": 0.62})
    frame["rotation"] = [0.0, frame_yaw, math.pi * 0.5]
    frame["quaternion"] = quaternion_from_yaw_and_roll(frame_yaw, math.pi * 0.5)
    frame["collider"] = {
        "center": [0.0, 0.0, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [1.0, 1.0, 1.0],
        "type": 0,
    }
    return frame


def make_brazier(name: str, position: Vec3, yaw: float) -> dict:
    brazier = base_object(
        name,
        "Model",
        "Gimmicks/brazier",
        position,
        (1.55, 1.55, 1.55),
        yaw,
    )
    brazier.update(
        {
            "collisionAttribute": 4,
            "collisionMask": 4294967295,
            "emissive": 0.1,
            "enableEnvMap": True,
            "envIntensity": 0.18,
            "roughness": 0.72,
        }
    )
    brazier["collider"] = {
        "center": [0.0, 0.52, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [0.72, 0.55, 0.72],
        "type": 2,
    }
    return brazier


def make_brazier_flame(name: str, position: Vec3, flow_x: float) -> dict:
    flame = base_object(name, "Effect", "Effects/flame", position)
    flame.update(
        {
            "blendMode": 1,
            "castShadow": False,
            "color": [1.0, 0.2, 0.02, 0.84],
            "emissive": 2.15,
            "enableLighting": False,
            "isStatic": False,
            "materialType": 11,
            "waterParam": {
                "billboardScale": 0.74,
                "effectIntensity": 0.88,
                "effectScale": 0.96,
                "effectScaleX": 0.9,
                "effectScaleY": 1.15,
                "effectScaleZ": 0.82,
                "effectSoftness": 0.36,
                "effectType": 4.0,
                "flowSpeedX": flow_x,
                "flowSpeedY": 0.48,
                "waveFrequency": 2.2,
                "waveHeight": 0.5,
                "waveSpeed": 1.55,
            },
        }
    )
    return flame


def make_stage_sign(name: str, position: Vec3, yaw: float) -> dict:
    sign = base_object(
        name,
        "Model",
        "Stages/signboard",
        position,
        (0.0145, 0.0145, 0.0145),
        yaw,
    )
    sign.update(
        {
            "collisionAttribute": 4,
            "collisionMask": 4294967295,
            "enableEnvMap": True,
            "envIntensity": 0.16,
            "roughness": 0.82,
        }
    )
    sign["collider"] = {
        "center": [0.0, 81.19140625, 10.390625],
        "rotation": [0.0, 0.0, 0.0],
        "size": [77.3984375, 98.95703125, 17.6015625],
        "type": 3,
    }
    return sign


def make_gate_lock(stage_index: int, position: Vec3, yaw: float) -> dict:
    lock = base_object(
        f"StageSelect_LockSeal_{stage_index}",
        "Model",
        "Gimmicks/stage_gate_lock",
        position,
        (1.55, 1.55, 0.72),
        yaw,
    )
    lock.update(
        {
            "castShadow": True,
            "collisionAttribute": 0,
            "collisionMask": 0,
            "color": [0.72, 0.58, 0.32, 1.0],
            "emissive": 0.22,
            "envIntensity": 0.48,
            "isStatic": False,
            "roughness": 0.34,
            "targetID": stage_index,
        }
    )
    return lock


def make_crown_gate_key(stage_index: int, position: Vec3, yaw: float) -> dict:
    """解放ムービーで使用する王冠キーを、ゲートごとに編集可能なオブジェクトとして配置します。"""
    key = base_object(
        f"StageSelect_CrownKey_{stage_index}",
        "Model",
        "Gimmicks/crown_gate_key",
        position,
        (0.82, 0.82, 0.82),
        yaw,
    )
    key.update(
        {
            "castShadow": True,
            "collisionAttribute": 0,
            "collisionMask": 0,
            "color": [1.0, 0.82, 0.24, 1.0],
            "emissive": 3.0,
            "enableEnvMap": True,
            "envIntensity": 0.68,
            "isStatic": False,
            "isVisible": False,
            "roughness": 0.24,
            "targetID": stage_index,
        }
    )
    return key


@dataclass(frozen=True)
class StageIslandLayout:
    index: int
    center: Vec2
    radii: Vec2


STAGE_ISLANDS = [
    StageIslandLayout(0, (-48.0, 28.0), (15.0, 12.5)),
    StageIslandLayout(1, (50.0, 24.0), (15.0, 12.5)),
    StageIslandLayout(2, (6.0, -54.0), (16.0, 13.0)),
]


def set_parent(item: dict, parent_name: str) -> None:
    item["parentName"] = parent_name
    item["parentGuid"] = object_guid(parent_name) if parent_name else ""


def make_group_root(name: str, parent_name: str = "") -> dict:
    group = base_object(name, "Model", "", (0.0, 0.0, 0.0))
    group.update(
        {
            "castShadow": False,
            "enableLighting": False,
            "isVisible": False,
        }
    )
    set_parent(group, parent_name)
    return group


def organize_scene_hierarchy(objects: list[dict]) -> list[dict]:
    world_group = "[Group] World"
    hub_group = "[Group] Central Hub"
    hub_terrain_group = "[Group] Central Terrain"
    tutorial_gate_group = "[Group] Tutorial Gate"
    hub_decoration_group = "[Group] Central Decoration"

    groups = [
        make_group_root(world_group),
        make_group_root(hub_group),
        make_group_root(hub_terrain_group, hub_group),
        make_group_root(tutorial_gate_group, hub_group),
        make_group_root(hub_decoration_group, hub_group),
    ]

    stage_groups: dict[int, dict[str, str]] = {}
    for stage_index in range(3):
        stage_label = stage_index + 1
        stage_root = f"[Group] Stage {stage_label} Island"
        terrain_root = f"[Group] Stage {stage_label} Terrain"
        bridge_root = f"[Group] Stage {stage_label} Bridge"
        gate_root = f"[Group] Stage {stage_label} Gate"
        stage_groups[stage_index] = {
            "root": stage_root,
            "terrain": terrain_root,
            "bridge": bridge_root,
            "gate": gate_root,
        }
        groups.extend(
            [
                make_group_root(stage_root),
                make_group_root(terrain_root, stage_root),
                make_group_root(bridge_root, stage_root),
                make_group_root(gate_root, stage_root),
            ]
        )

    for item in objects:
        name = item["name"]
        parent_name = ""

        if name == "StageSelect_Ocean":
            parent_name = world_group
        elif name.startswith("StageSelect_CentralCollision_") or name.startswith("StageSelect_CentralBoundary_"):
            parent_name = hub_terrain_group
        elif name in {
            "StageSelect_CentralIsland",
            "StageSelect_HubPlaza",
            "StageSelect_CrownCore",
        }:
            parent_name = hub_terrain_group
        elif name in {
            "StageSelect_TutorialGateFrame",
            "StageSelect_TutorialPad",
            "TutorialArrivalGate",
        }:
            parent_name = tutorial_gate_group
        elif name.startswith("StageSelect_Tree_"):
            parent_name = hub_decoration_group
        else:
            for stage_index, stage_group in stage_groups.items():
                if (
                    name == f"StageIsland_{stage_index}"
                    or name.startswith(f"StageIslandCollision_{stage_index}_")
                    or name.startswith(f"StageSelect_IslandBoundary_{stage_index}_")
                ):
                    parent_name = stage_group["terrain"]
                    break
                if (
                    name == f"StageSelect_Bridge_{stage_index}"
                    or name.startswith(f"StageSelect_BridgeBoundary_{stage_index}_")
                ):
                    parent_name = stage_group["bridge"]
                    break
                if (
                    name == f"StageMarkerPad_{stage_index}"
                    or name == f"StageSelect_StageGateFrame_{stage_index}"
                    or name == f"StageGate_{stage_index}"
                    or name == f"StageSelect_LockSeal_{stage_index}"
                    or name == f"StageSelect_CrownKey_{stage_index}"
                    or name == f"StageSelect_GateSign_{stage_index}"
                    or name.startswith(f"StageSelect_GateBrazier_{stage_index}_")
                    or name.startswith(f"StageSelect_GateFlame_{stage_index}_")
                    or name.startswith(f"StageCoin_{stage_index}_")
                ):
                    parent_name = stage_group["gate"]
                    break

        set_parent(item, parent_name)

    return groups + objects


def build_scene() -> dict:
    objects: list[dict] = [make_water()]
    island_top = -1.15
    collision_y = island_top - 3.5

    central_island = base_object(
        "StageSelect_CentralIsland",
        "Model",
        "Stages/stage_select_hub_island",
        (0.0, island_top, 0.0),
    )
    central_island.update(
        {
            "collisionAttribute": 4,
            "collisionMask": 4294967295,
        }
    )
    central_island["collider"] = {
        "center": [0.0, -3.55, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [23.25, 3.65, 23.25],
        "type": 4,
    }
    objects.append(central_island)
    objects.extend(make_ellipse_collision_strips(
        "StageSelect_CentralCollision",
        (0.0, 0.0),
        (28.0, 24.0),
        collision_y,
    ))
    central_openings = [math.atan2(layout.center[1], layout.center[0]) for layout in STAGE_ISLANDS]
    objects.extend(make_ellipse_boundary_walls(
        "StageSelect_CentralBoundary",
        (0.0, 0.0),
        (27.65, 23.65),
        island_top + 5.75,
        central_openings,
        0.20,
        20,
    ))
    objects.append(base_object("StageSelect_HubPlaza", "Model", "Stages/stage_select_hub_plaza", (0.0, island_top + 0.03, 0.0)))

    crown = base_object("StageSelect_CrownCore", "Model", "Stages/crown", (0.0, island_top + 1.05, 0.0), (1.55, 1.55, 1.55))
    crown.update({"castShadow": False, "emissive": 1.4, "envIntensity": 0.55, "roughness": 0.28})
    objects.append(crown)

    tutorial_position = (0.0, island_top + 2.30, 15.0)
    tutorial_frame = make_gate_frame(
        "StageSelect_TutorialGateFrame",
        (0.0, island_top + 3.12, 15.0),
        math.pi,
    )
    objects.append(tutorial_frame)
    tutorial_pad = base_object(
        "StageSelect_TutorialPad",
        "Model",
        "Stages/stage_select_gate_pad",
        (0.0, island_top + 0.03, 15.0),
    )
    tutorial_pad.update(
        {
            "castShadow": False,
            "color": [0.62, 0.90, 1.0, 1.0],
            "emissive": 1.05,
            "envIntensity": 0.35,
            "roughness": 0.42,
        }
    )
    objects.append(tutorial_pad)
    tutorial_gate = make_gate("TutorialArrivalGate", tutorial_position, math.pi, -1, 1, "TUTORIAL")
    tutorial_gate["color"] = [0.38, 0.88, 1.0, 1.0]
    objects.append(tutorial_gate)

    for layout in STAGE_ISLANDS:
        center_x, center_z = layout.center
        island = base_object(
            f"StageIsland_{layout.index}",
            "Model",
            "Stages/stage_select_gate_island",
            (center_x, island_top, center_z),
            (layout.radii[0] / 15.0, 1.0, layout.radii[1] / 12.5),
        )
        island.update(
            {
                "collisionAttribute": 4,
                "collisionMask": 4294967295,
                "envIntensity": 0.18,
                "roughness": 0.76,
            }
        )
        island["collider"] = {
            "center": [0.0, -3.55, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [min(layout.radii) * 0.93, 3.65, min(layout.radii) * 0.93],
            "type": 4,
        }
        objects.append(island)

        objects.extend(make_ellipse_collision_strips(
            f"StageIslandCollision_{layout.index}",
            layout.center,
            layout.radii,
            collision_y,
        ))
        length = math.sqrt(center_x * center_x + center_z * center_z)
        inward_x = -center_x / length
        inward_z = -center_z / length
        objects.append(make_bridge(layout.index, layout.center, layout.radii))
        objects.extend(make_bridge_boundary_walls(
            layout.index,
            layout.center,
            layout.radii,
            island_top + 5.75,
        ))
        objects.extend(make_ellipse_boundary_walls(
            f"StageSelect_IslandBoundary_{layout.index}",
            layout.center,
            (layout.radii[0] * 0.975, layout.radii[1] * 0.975),
            island_top + 5.75,
            [math.atan2(inward_z, inward_x)],
            0.25,
            16,
        ))

        outward_x = center_x / length
        outward_z = center_z / length
        inward_x = -outward_x
        inward_z = -outward_z
        gate_x = center_x + outward_x * 3.6
        gate_z = center_z + outward_z * 3.6
        # 中央島から近づいたときに開口部と南京錠の正面が見えるよう、
        # ゲートのローカル +Z を中央島へ向けます。
        gate_yaw = math.atan2(inward_x, inward_z)

        # 島の内側から近づいたときに、ゲート正面が中央島側を向くようにします。
        gate_yaw = math.atan2(inward_x, inward_z)

        pad = base_object(
            f"StageMarkerPad_{layout.index}",
            "Model",
            "Stages/stage_select_gate_pad",
            (gate_x, island_top + 0.03, gate_z),
        )
        pad.update({"castShadow": False, "emissive": 0.85, "envIntensity": 0.35, "roughness": 0.42})
        objects.append(pad)
        objects.append(make_gate_frame(
            f"StageSelect_StageGateFrame_{layout.index}",
            (gate_x, island_top + 3.12, gate_z),
            gate_yaw,
        ))
        objects.append(
            make_gate(
                f"StageGate_{layout.index}",
                (gate_x, island_top + 2.30, gate_z),
                gate_yaw,
                layout.index,
                0,
                "SELECT",
            )
        )
        if layout.index in (1, 2):
            objects.append(
                make_gate_lock(
                    layout.index,
                    (gate_x, island_top + 2.55, gate_z),
                    gate_yaw + math.pi,
                )
            )
            objects.append(
                make_crown_gate_key(
                    layout.index,
                    (gate_x, island_top + 2.55, gate_z),
                    gate_yaw,
                )
            )

        right_x = inward_z
        right_z = -inward_x
        for side_index, side in enumerate((-1.0, 1.0)):
            brazier_x = gate_x + inward_x * 0.35 + right_x * side * 4.15
            brazier_z = gate_z + inward_z * 0.35 + right_z * side * 4.15
            objects.append(
                make_brazier(
                    f"StageSelect_GateBrazier_{layout.index}_{side_index}",
                    (brazier_x, island_top + 0.02, brazier_z),
                    gate_yaw,
                )
            )
            objects.append(
                make_brazier_flame(
                    f"StageSelect_GateFlame_{layout.index}_{side_index}",
                    (brazier_x, island_top + 2.27, brazier_z),
                    0.03 if side < 0.0 else -0.025,
                )
            )

        sign_x = gate_x + inward_x * 3.2 + right_x * 5.25
        sign_z = gate_z + inward_z * 3.2 + right_z * 5.25
        sign_yaw = gate_yaw + math.pi
        objects.append(
            make_stage_sign(
                f"StageSelect_GateSign_{layout.index}",
                (sign_x, island_top + 0.27, sign_z),
                sign_yaw,
            )
        )

        # スターは看板正面へ固定し、ステージごとの収集状況を一目で確認できるようにします。
        for coin_index in range(3):
            side = float(coin_index - 1) * 0.68
            coin_x = sign_x + inward_x * 0.34 + right_x * side
            coin_z = sign_z + inward_z * 0.34 + right_z * side
            coin = base_object(
                f"StageCoin_{layout.index}_{coin_index}",
                "Model",
                "Gimmicks/star",
                (coin_x, island_top + 1.73, coin_z),
                (0.011, 0.011, 0.011),
                sign_yaw,
            )
            coin_rotation = [-math.pi * 0.5, sign_yaw, 0.0]
            coin["rotation"] = coin_rotation
            coin["quaternion"] = quaternion_from_euler(coin_rotation)
            coin.update({"castShadow": False, "emissive": 0.45, "envIntensity": 0.28, "roughness": 0.38})
            objects.append(coin)

    tree_positions = [
        (-15.0, -1.08, -5.0, 2.20),
        (16.0, -1.08, -4.0, 1.95),
        (11.5, -1.08, 12.0, 1.75),
    ]
    for index, (x, y, z, scale) in enumerate(tree_positions):
        tree = base_object(f"StageSelect_Tree_{index}", "Model", "Stages/tree1", (x, y, z), (scale, scale, scale))
        tree.update({"envIntensity": 0.16, "roughness": 0.86})
        objects.append(tree)

    return {"objects": organize_scene_hierarchy(objects)}


def build_player_scene() -> dict:
    player = base_object("Player", "Player", "Characters/slime", (0.0, -0.42, 5.5), (2.0, 2.0, 2.0), math.pi)
    player.update(
        {
            "castShadow": True,
            "collisionAttribute": 1,
            "collisionMask": 4294967295,
            "envIntensity": 0.0,
            "isStatic": False,
            "lod": {
                "enabled": True,
                "levels": [
                    {"distance": 35.0, "level": 1, "modelName": "Characters/slime/slime_lod1.gltf"},
                    {"distance": 70.0, "level": 2, "modelName": "Characters/slime/slime_lod2.gltf"},
                ],
            },
            "param": {
                "actionMode": 0,
                "attackPower": 1.0,
                "colorType": 0,
                "detectionRange": 20.0,
                "enemyType": "",
                "fallDuration": 2.0,
                "gimmickType": "",
                "gravity": 50.0,
                "healAmount": 1.0,
                "hp": 6.0,
                "interval": 3.0,
                "itemType": "",
                "jumpPower": 24.0,
                "maxCount": 5,
                "maxFallSpeed": 60.0,
                "maxHp": 6.0,
                "moveAmount": 10.0,
                "moveSpeed": 6.0,
                "returnOnOff": True,
                "shakeDuration": 1.0,
                "speed": 25.0,
                "startActive": False,
                "switchMode": 0,
                "targetScene": "SELECT",
            },
            "roughness": 0.6,
            "saveCategory": "Player",
        }
    )
    player["collider"] = {
        "center": [0.0, 0.35, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [0.55, 0.42, 0.55],
        "type": 3,
    }
    return {"objects": [player]}


def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def validate_scene(scene: dict) -> None:
    objects = scene["objects"]
    names = [item["name"] for item in objects]
    if len(names) != len(set(names)):
        raise RuntimeError("ステージセレクトのオブジェクト名が重複しています。")

    object_by_name = {item["name"]: item for item in objects}
    top_level_groups = {
        "[Group] World",
        "[Group] Central Hub",
        "[Group] Stage 1 Island",
        "[Group] Stage 2 Island",
        "[Group] Stage 3 Island",
    }
    group_names = {name for name in names if name.startswith("[Group] ")}
    expected_groups = top_level_groups | {
        "[Group] Central Terrain",
        "[Group] Tutorial Gate",
        "[Group] Central Decoration",
        "[Group] Stage 1 Terrain",
        "[Group] Stage 1 Bridge",
        "[Group] Stage 1 Gate",
        "[Group] Stage 2 Terrain",
        "[Group] Stage 2 Bridge",
        "[Group] Stage 2 Gate",
        "[Group] Stage 3 Terrain",
        "[Group] Stage 3 Bridge",
        "[Group] Stage 3 Gate",
    }
    missing_groups = expected_groups.difference(group_names)
    if missing_groups:
        raise RuntimeError(f"整理用の親オブジェクトがありません: {sorted(missing_groups)}")

    for item in objects:
        name = item["name"]
        parent_name = item.get("parentName", "")
        parent_guid = item.get("parentGuid", "")
        if name in top_level_groups:
            if parent_name or parent_guid:
                raise RuntimeError(f"最上位グループに親が設定されています: {name}")
            continue
        if not parent_name:
            raise RuntimeError(f"親が設定されていないオブジェクトがあります: {name}")
        parent = object_by_name.get(parent_name)
        if parent is None:
            raise RuntimeError(f"存在しない親を参照しています: {name} -> {parent_name}")
        if parent_guid != parent.get("guid", ""):
            raise RuntimeError(f"親GUIDが一致しません: {name} -> {parent_name}")

        visited = {name}
        cursor = parent
        while cursor is not None:
            cursor_name = cursor["name"]
            if cursor_name in visited:
                raise RuntimeError(f"親子関係が循環しています: {name}")
            visited.add(cursor_name)
            next_parent_name = cursor.get("parentName", "")
            cursor = object_by_name.get(next_parent_name) if next_parent_name else None

    for index in range(3):
        required = {
            f"StageIsland_{index}",
            f"StageMarkerPad_{index}",
            f"StageSelect_StageGateFrame_{index}",
            f"StageGate_{index}",
            f"StageSelect_Bridge_{index}",
        }
        missing = required.difference(names)
        if missing:
            raise RuntimeError(f"Stage {index + 1} の必須オブジェクトがありません: {sorted(missing)}")
    for index in (1, 2):
        if f"StageSelect_LockSeal_{index}" not in names:
            raise RuntimeError(f"Stage {index + 1} の封鎖モデルがありません。")
        if f"StageSelect_CrownKey_{index}" not in names:
            raise RuntimeError(f"Stage {index + 1} の王冠鍵モデルがありません。")
    if "TutorialArrivalGate" not in names:
        raise RuntimeError("チュートリアル到着ゲートがありません。")


def main() -> None:
    parser = argparse.ArgumentParser(description="ステージセレクトの中央島と3つの孤島を生成します。")
    parser.add_argument("--models-only", action="store_true", help="モデルのみ生成してScene JSONは変更しません。")
    args = parser.parse_args()

    export_model(build_island("stage_select_hub_island", 28.0, 24.0, 0.35))
    export_model(build_island("stage_select_gate_island", 15.0, 12.5, 1.15))
    export_model(build_hub_plaza())
    export_model(build_gate_pad())

    if not args.models_only:
        scene = build_scene()
        validate_scene(scene)
        player_scene = build_player_scene()
        if player_scene["objects"][0].get("isStatic", True):
            raise RuntimeError("ステージセレクトのプレイヤーが静的オブジェクトになっています。")
        write_json(SCENE_PATH, scene)
        write_json(PLAYER_PATH, player_scene)

    print("ステージセレクト用モデルと配置データを生成しました。")


if __name__ == "__main__":
    main()
