"""風スライムの共有モデルへ、風属性を示す立体装飾を追加します。"""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODEL_DIR = ROOT / "Resources" / "3DModel" / "Characters" / "slime_wind"
GLTF_PATH = MODEL_DIR / "slime_wind.gltf"
BIN_PATH = MODEL_DIR / "slime_wind.bin"
MARKER_NAME = "cg2WindSlimeDecoration"


def subtract(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return a[0] - b[0], a[1] - b[1], a[2] - b[2]


def cross(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def normalize(value: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(value[0] ** 2 + value[1] ** 2 + value[2] ** 2)
    if length <= 1.0e-8:
        return 0.0, 1.0, 0.0
    return value[0] / length, value[1] / length, value[2] / length


class TriangleMesh:
    def __init__(self) -> None:
        self.positions: list[tuple[float, float, float]] = []
        self.normals: list[tuple[float, float, float]] = []
        self.uvs: list[tuple[float, float]] = []

    def triangle(
        self,
        a: tuple[float, float, float],
        b: tuple[float, float, float],
        c: tuple[float, float, float],
    ) -> None:
        normal = normalize(cross(subtract(b, a), subtract(c, a)))
        self.positions.extend((a, b, c))
        self.normals.extend((normal, normal, normal))
        self.uvs.extend(((0.0, 0.0), (1.0, 0.0), (0.5, 1.0)))

    def quad(
        self,
        a: tuple[float, float, float],
        b: tuple[float, float, float],
        c: tuple[float, float, float],
        d: tuple[float, float, float],
    ) -> None:
        self.triangle(a, b, c)
        self.triangle(a, c, d)


def add_leaf_prism(
    mesh: TriangleMesh,
    polygon: list[tuple[float, float]],
    center_z: float,
    half_depth: float,
) -> None:
    front = [(x, y, center_z - half_depth) for x, y in polygon]
    back = [(x, y, center_z + half_depth) for x, y in polygon]

    for index in range(1, len(polygon) - 1):
        mesh.triangle(front[0], front[index + 1], front[index])
        mesh.triangle(back[0], back[index], back[index + 1])

    for index in range(len(polygon)):
        next_index = (index + 1) % len(polygon)
        mesh.quad(front[index], front[next_index], back[next_index], back[index])


def add_mirrored_wind_fins(mesh: TriangleMesh) -> None:
    right_fins = [
        [(0.42, 0.43), (0.48, 0.52), (0.70, 0.65), (0.66, 0.53), (0.52, 0.40)],
        [(0.44, 0.35), (0.52, 0.43), (0.77, 0.50), (0.68, 0.39), (0.51, 0.31)],
        [(0.42, 0.28), (0.52, 0.33), (0.69, 0.34), (0.62, 0.27), (0.49, 0.23)],
    ]
    for polygon in right_fins:
        add_leaf_prism(mesh, polygon, 0.02, 0.038)
        mirrored = [(-x, y) for x, y in reversed(polygon)]
        add_leaf_prism(mesh, mirrored, 0.02, 0.038)


def add_tube_arc(
    mesh: TriangleMesh,
    radius: float,
    height: float,
    start_angle: float,
    end_angle: float,
    tube_radius: float,
    segment_count: int,
    side_count: int = 6,
) -> None:
    rings: list[list[tuple[float, float, float]]] = []
    for segment in range(segment_count + 1):
        rate = segment / segment_count
        angle = start_angle + (end_angle - start_angle) * rate
        ring: list[tuple[float, float, float]] = []
        for side in range(side_count):
            phase = math.tau * side / side_count
            horizontal_radius = radius + math.cos(phase) * tube_radius
            ring.append((
                math.cos(angle) * horizontal_radius,
                height + math.sin(phase) * tube_radius,
                math.sin(angle) * horizontal_radius,
            ))
        rings.append(ring)

    for segment in range(segment_count):
        for side in range(side_count):
            next_side = (side + 1) % side_count
            mesh.quad(
                rings[segment][side],
                rings[segment + 1][side],
                rings[segment + 1][next_side],
                rings[segment][next_side],
            )

    for ring, reverse in ((rings[0], True), (rings[-1], False)):
        center = tuple(sum(vertex[axis] for vertex in ring) / side_count for axis in range(3))
        for side in range(side_count):
            next_side = (side + 1) % side_count
            if reverse:
                mesh.triangle(center, ring[next_side], ring[side])
            else:
                mesh.triangle(center, ring[side], ring[next_side])


def pack_vectors(values: list[tuple[float, ...]]) -> bytes:
    if not values:
        return b""
    component_count = len(values[0])
    return b"".join(struct.pack("<" + "f" * component_count, *value) for value in values)


def append_buffer_data(blob: bytearray, data: bytes) -> tuple[int, int]:
    while len(blob) % 4:
        blob.append(0)
    offset = len(blob)
    blob.extend(data)
    return offset, len(data)


def append_mesh_primitive(
    gltf: dict,
    blob: bytearray,
    mesh: TriangleMesh,
    material_index: int,
) -> dict:
    attributes: dict[str, int] = {}
    for semantic, values, component_count, accessor_type in (
        ("POSITION", mesh.positions, 3, "VEC3"),
        ("NORMAL", mesh.normals, 3, "VEC3"),
        ("TEXCOORD_0", mesh.uvs, 2, "VEC2"),
    ):
        offset, byte_length = append_buffer_data(blob, pack_vectors(values))
        view_index = len(gltf["bufferViews"])
        gltf["bufferViews"].append({
            "buffer": 0,
            "byteLength": byte_length,
            "byteOffset": offset,
            "target": 34962,
        })
        accessor: dict = {
            "bufferView": view_index,
            "componentType": 5126,
            "count": len(values),
            "type": accessor_type,
        }
        if semantic == "POSITION":
            accessor["min"] = [min(value[axis] for value in values) for axis in range(component_count)]
            accessor["max"] = [max(value[axis] for value in values) for axis in range(component_count)]
        accessor_index = len(gltf["accessors"])
        gltf["accessors"].append(accessor)
        attributes[semantic] = accessor_index

    return {"attributes": attributes, "material": material_index, "mode": 4}


def remove_previous_decoration(gltf: dict, blob: bytearray) -> bytearray:
    marker = gltf.get("extras", {}).get(MARKER_NAME)
    if not marker:
        return blob

    for collection_name in ("accessors", "bufferViews", "materials", "meshes", "nodes"):
        gltf[collection_name] = gltf[collection_name][: marker[collection_name]]
    for scene in gltf.get("scenes", []):
        scene["nodes"] = [index for index in scene.get("nodes", []) if index < marker["nodes"]]
    gltf.get("extras", {}).pop(MARKER_NAME, None)
    return blob[: marker["byteLength"]]


def main() -> None:
    gltf = json.loads(GLTF_PATH.read_text(encoding="utf-8"))
    blob = bytearray(BIN_PATH.read_bytes())
    blob = remove_previous_decoration(gltf, blob)

    marker = {
        "byteLength": len(blob),
        "accessors": len(gltf["accessors"]),
        "bufferViews": len(gltf["bufferViews"]),
        "materials": len(gltf["materials"]),
        "meshes": len(gltf["meshes"]),
        "nodes": len(gltf["nodes"]),
    }

    fin_material = len(gltf["materials"])
    gltf["materials"].append({
        "doubleSided": True,
        "emissiveFactor": [0.04, 0.18, 0.12],
        "name": "WindSlimeFins",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.32, 0.96, 0.73, 1.0],
            "metallicFactor": 0.04,
            "roughnessFactor": 0.30,
        },
    })
    swirl_material = len(gltf["materials"])
    gltf["materials"].append({
        "doubleSided": True,
        "emissiveFactor": [0.10, 0.32, 0.24],
        "name": "WindSlimeSwirl",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.72, 1.0, 0.90, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.22,
        },
    })

    fins = TriangleMesh()
    add_mirrored_wind_fins(fins)
    swirls = TriangleMesh()
    add_tube_arc(swirls, 0.575, 0.24, math.radians(18.0), math.radians(284.0), 0.021, 30)
    add_tube_arc(swirls, 0.515, 0.48, math.radians(196.0), math.radians(358.0), 0.017, 20)

    decoration_mesh_index = len(gltf["meshes"])
    gltf["meshes"].append({
        "name": "WindSlimeDecorationMesh",
        "primitives": [
            append_mesh_primitive(gltf, blob, fins, fin_material),
            append_mesh_primitive(gltf, blob, swirls, swirl_material),
        ],
    })
    decoration_node_index = len(gltf["nodes"])
    gltf["nodes"].append({"mesh": decoration_mesh_index, "name": "WindSlimeDecoration"})
    gltf["scenes"][gltf.get("scene", 0)].setdefault("nodes", []).append(decoration_node_index)

    gltf.setdefault("extras", {})[MARKER_NAME] = marker
    gltf["asset"]["generator"] = "CG2 Wind Slime decorated shared-mesh model"
    gltf["buffers"][0]["byteLength"] = len(blob)
    BIN_PATH.write_bytes(blob)
    GLTF_PATH.write_text(json.dumps(gltf, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(f"Updated: {GLTF_PATH}")
    print(f"Decoration vertices: fins={len(fins.positions)}, swirls={len(swirls.positions)}")


if __name__ == "__main__":
    main()
