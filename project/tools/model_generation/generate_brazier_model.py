from __future__ import annotations

from dataclasses import dataclass
from math import cos, pi, sin, sqrt
from pathlib import Path
from typing import Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = PROJECT_ROOT / "Resources" / "3DModel" / "Gimmicks" / "brazier"
OBJ_PATH = OUTPUT_DIR / "brazier.obj"
MTL_PATH = OUTPUT_DIR / "brazier.mtl"


@dataclass(frozen=True)
class Vertex:
    position: tuple[float, float, float]
    uv: tuple[float, float]
    normal: tuple[float, float, float]


vertices: list[Vertex] = []
faces: list[tuple[str, int, int, int]] = []


def normalize(value: tuple[float, float, float]) -> tuple[float, float, float]:
    length = sqrt(value[0] ** 2 + value[1] ** 2 + value[2] ** 2)
    if length <= 1.0e-8:
        return (0.0, 1.0, 0.0)
    return (value[0] / length, value[1] / length, value[2] / length)


def add_vertex(
    position: tuple[float, float, float],
    uv: tuple[float, float],
    normal: tuple[float, float, float],
) -> int:
    vertices.append(Vertex(position, uv, normalize(normal)))
    return len(vertices)


def add_face(material: str, a: int, b: int, c: int) -> None:
    faces.append((material, a, b, c))


def add_lathe(
    profile: Iterable[tuple[float, float]],
    segments: int,
    material: str,
    *,
    invert: bool = False,
) -> None:
    rings = list(profile)
    ring_indices: list[list[int]] = []

    for ring_index, (radius, height) in enumerate(rings):
        if ring_index == 0:
            next_radius, next_height = rings[1]
            delta_radius = next_radius - radius
            delta_height = next_height - height
        elif ring_index == len(rings) - 1:
            previous_radius, previous_height = rings[ring_index - 1]
            delta_radius = radius - previous_radius
            delta_height = height - previous_height
        else:
            previous_radius, previous_height = rings[ring_index - 1]
            next_radius, next_height = rings[ring_index + 1]
            delta_radius = next_radius - previous_radius
            delta_height = next_height - previous_height

        indices: list[int] = []
        for segment in range(segments):
            angle = segment / segments * 2.0 * pi
            radial_x = cos(angle)
            radial_z = sin(angle)
            normal = (delta_height * radial_x, -delta_radius, delta_height * radial_z)
            if invert:
                normal = (-normal[0], -normal[1], -normal[2])
            indices.append(add_vertex(
                (radius * radial_x, height, radius * radial_z),
                (segment / segments, ring_index / max(1, len(rings) - 1)),
                normal,
            ))
        ring_indices.append(indices)

    for ring_index in range(len(ring_indices) - 1):
        lower = ring_indices[ring_index]
        upper = ring_indices[ring_index + 1]
        for segment in range(segments):
            next_segment = (segment + 1) % segments
            a = lower[segment]
            b = lower[next_segment]
            c = upper[next_segment]
            d = upper[segment]
            if invert:
                add_face(material, a, b, c)
                add_face(material, a, c, d)
            else:
                add_face(material, a, c, b)
                add_face(material, a, d, c)


def add_disc(radius: float, height: float, segments: int, material: str, *, upward: bool) -> None:
    normal = (0.0, 1.0 if upward else -1.0, 0.0)
    center = add_vertex((0.0, height, 0.0), (0.5, 0.5), normal)
    ring: list[int] = []
    for segment in range(segments):
        angle = segment / segments * 2.0 * pi
        x = radius * cos(angle)
        z = radius * sin(angle)
        ring.append(add_vertex(
            (x, height, z),
            (0.5 + x / (radius * 2.0), 0.5 + z / (radius * 2.0)),
            normal,
        ))

    for segment in range(segments):
        next_segment = (segment + 1) % segments
        if upward:
            add_face(material, center, ring[segment], ring[next_segment])
        else:
            add_face(material, center, ring[next_segment], ring[segment])


def add_torus(
    major_radius: float,
    minor_radius: float,
    height: float,
    major_segments: int,
    minor_segments: int,
    material: str,
) -> None:
    grid: list[list[int]] = []
    for major_index in range(major_segments):
        major_angle = major_index / major_segments * 2.0 * pi
        major_cos = cos(major_angle)
        major_sin = sin(major_angle)
        row: list[int] = []
        for minor_index in range(minor_segments):
            minor_angle = minor_index / minor_segments * 2.0 * pi
            minor_cos = cos(minor_angle)
            minor_sin = sin(minor_angle)
            radius = major_radius + minor_radius * minor_cos
            row.append(add_vertex(
                (radius * major_cos, height + minor_radius * minor_sin, radius * major_sin),
                (major_index / major_segments, minor_index / minor_segments),
                (major_cos * minor_cos, minor_sin, major_sin * minor_cos),
            ))
        grid.append(row)

    for major_index in range(major_segments):
        next_major = (major_index + 1) % major_segments
        for minor_index in range(minor_segments):
            next_minor = (minor_index + 1) % minor_segments
            a = grid[major_index][minor_index]
            b = grid[next_major][minor_index]
            c = grid[next_major][next_minor]
            d = grid[major_index][next_minor]
            add_face(material, a, c, b)
            add_face(material, a, d, c)


def add_uv_sphere(
    center: tuple[float, float, float],
    radius: tuple[float, float, float],
    segments: int,
    rings: int,
    material: str,
    phase: float,
) -> None:
    grid: list[list[int]] = []
    for ring_index in range(1, rings):
        latitude = -pi * 0.5 + ring_index / rings * pi
        row: list[int] = []
        for segment in range(segments):
            longitude = segment / segments * 2.0 * pi + phase
            nx = cos(latitude) * cos(longitude)
            ny = sin(latitude)
            nz = cos(latitude) * sin(longitude)
            row.append(add_vertex(
                (
                    center[0] + nx * radius[0],
                    center[1] + ny * radius[1],
                    center[2] + nz * radius[2],
                ),
                (segment / segments, ring_index / rings),
                (nx / max(radius[0], 1.0e-4), ny / max(radius[1], 1.0e-4), nz / max(radius[2], 1.0e-4)),
            ))
        grid.append(row)

    bottom = add_vertex(
        (center[0], center[1] - radius[1], center[2]),
        (0.5, 0.0),
        (0.0, -1.0, 0.0),
    )
    top = add_vertex(
        (center[0], center[1] + radius[1], center[2]),
        (0.5, 1.0),
        (0.0, 1.0, 0.0),
    )

    for segment in range(segments):
        next_segment = (segment + 1) % segments
        add_face(material, bottom, grid[0][next_segment], grid[0][segment])
        add_face(material, top, grid[-1][segment], grid[-1][next_segment])

    for ring_index in range(len(grid) - 1):
        for segment in range(segments):
            next_segment = (segment + 1) % segments
            a = grid[ring_index][segment]
            b = grid[ring_index][next_segment]
            c = grid[ring_index + 1][next_segment]
            d = grid[ring_index + 1][segment]
            add_face(material, a, c, b)
            add_face(material, a, d, c)


def build_model() -> None:
    segments = 32

    # Rounded stone pedestal with a short, sturdy neck.
    add_lathe([
        (0.42, 0.00),
        (0.50, 0.045),
        (0.50, 0.13),
        (0.43, 0.19),
        (0.36, 0.235),
        (0.32, 0.31),
        (0.30, 0.43),
        (0.25, 0.50),
    ], segments, "stone")
    add_disc(0.42, 0.0, segments, "stone", upward=False)
    add_torus(0.405, 0.032, 0.18, segments, 8, "gold")

    # Deep outer bowl and a separate inner surface keep the rim readable.
    add_lathe([
        (0.20, 0.46),
        (0.25, 0.52),
        (0.36, 0.61),
        (0.50, 0.75),
        (0.62, 0.90),
        (0.67, 0.99),
    ], segments, "metal")
    add_lathe([
        (0.18, 0.72),
        (0.28, 0.78),
        (0.43, 0.88),
        (0.59, 0.975),
    ], segments, "metal", invert=True)
    add_torus(0.635, 0.068, 1.005, segments, 10, "gold")

    # A recessed coal bed gives the flame a believable source point.
    add_lathe([(0.34, 0.80), (0.36, 0.84)], 20, "coal")
    add_disc(0.36, 0.84, 20, "coal", upward=True)
    coal_lumps = [
        ((-0.15, 0.88, -0.06), (0.16, 0.09, 0.13), 0.15),
        ((0.13, 0.89, -0.09), (0.17, 0.10, 0.12), 0.70),
        ((-0.05, 0.90, 0.15), (0.18, 0.10, 0.14), 1.10),
        ((0.20, 0.88, 0.12), (0.12, 0.08, 0.11), 0.35),
    ]
    for center, radius, phase in coal_lumps:
        add_uv_sphere(center, radius, 10, 5, "coal", phase)


def write_obj() -> None:
    lines = ["mtllib brazier.mtl", "o stylized_gate_brazier"]
    for vertex in vertices:
        lines.append("v {:.6f} {:.6f} {:.6f}".format(*vertex.position))
    for vertex in vertices:
        lines.append("vt {:.6f} {:.6f}".format(*vertex.uv))
    for vertex in vertices:
        lines.append("vn {:.6f} {:.6f} {:.6f}".format(*vertex.normal))

    active_material = ""
    for material, a, b, c in faces:
        if material != active_material:
            lines.append(f"usemtl {material}")
            active_material = material
        lines.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}")

    OBJ_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_mtl() -> None:
    MTL_PATH.write_text(
        """newmtl stone
Ka 0.28 0.25 0.22
Kd 0.67 0.63 0.55
Ks 0.07 0.06 0.05
Ns 18
illum 2
map_Kd brazier_stone.png

newmtl metal
Ka 0.18 0.17 0.20
Kd 0.58 0.54 0.62
Ks 0.46 0.43 0.50
Ns 72
illum 2
map_Kd brazier_metal.png

newmtl gold
Ka 0.42 0.24 0.06
Kd 0.96 0.62 0.16
Ks 0.58 0.42 0.18
Ns 96
illum 2
map_Kd brazier_gold.png

newmtl coal
Ka 0.03 0.02 0.015
Kd 0.09 0.055 0.035
Ks 0.03 0.02 0.015
Ns 8
illum 2
map_Kd brazier_metal.png
""",
        encoding="utf-8",
    )


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    build_model()
    write_obj()
    write_mtl()
    print(f"Generated {OBJ_PATH}")
    print(f"Vertices: {len(vertices)}, triangles: {len(faces)}")


if __name__ == "__main__":
    main()
