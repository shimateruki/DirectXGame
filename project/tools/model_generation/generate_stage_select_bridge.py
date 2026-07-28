from __future__ import annotations

import math
from pathlib import Path

from PIL import Image


PROJECT_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = PROJECT_ROOT / "Resources" / "3DModel" / "Stages" / "stage_select_bridge"

Vec2 = tuple[float, float]
Vec3 = tuple[float, float, float]


def add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(v: Vec3, scalar: float) -> Vec3:
    return (v[0] * scalar, v[1] * scalar, v[2] * scalar)


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def normalize(v: Vec3) -> Vec3:
    length = math.sqrt(dot(v, v))
    if length <= 1.0e-8:
        return (0.0, 1.0, 0.0)
    return (v[0] / length, v[1] / length, v[2] / length)


def rotate_euler(point: Vec3, rotation: Vec3) -> Vec3:
    x, y, z = point
    sx, cx = math.sin(rotation[0]), math.cos(rotation[0])
    sy, cy = math.sin(rotation[1]), math.cos(rotation[1])
    sz, cz = math.sin(rotation[2]), math.cos(rotation[2])

    y, z = y * cx - z * sx, y * sx + z * cx
    x, z = x * cy + z * sy, -x * sy + z * cy
    x, y = x * cz - y * sz, x * sz + y * cz
    return (x, y, z)


class ObjBuilder:
    def __init__(self) -> None:
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

    def quad(self, material: str, points: list[Vec3], uvs: list[Vec2]) -> None:
        normal = normalize(cross(sub(points[1], points[0]), sub(points[2], points[0])))
        indices = [self.vertex(point, uv, normal) for point, uv in zip(points, uvs)]
        self.triangle(material, indices[0], indices[1], indices[2])
        self.triangle(material, indices[0], indices[2], indices[3])

    def box(self, material: str, center: Vec3, size: Vec3, rotation: Vec3 = (0.0, 0.0, 0.0)) -> None:
        hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5

        def transform(point: Vec3) -> Vec3:
            return add(rotate_euler(point, rotation), center)

        p = {
            "lbf": transform((-hx, -hy, hz)),
            "rbf": transform((hx, -hy, hz)),
            "ltf": transform((-hx, hy, hz)),
            "rtf": transform((hx, hy, hz)),
            "lbb": transform((-hx, -hy, -hz)),
            "rbb": transform((hx, -hy, -hz)),
            "ltb": transform((-hx, hy, -hz)),
            "rtb": transform((hx, hy, -hz)),
        }
        uv = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
        self.quad(material, [p["lbf"], p["rbf"], p["rtf"], p["ltf"]], uv)
        self.quad(material, [p["rbb"], p["lbb"], p["ltb"], p["rtb"]], uv)
        self.quad(material, [p["rbf"], p["rbb"], p["rtb"], p["rtf"]], uv)
        self.quad(material, [p["lbb"], p["lbf"], p["ltf"], p["ltb"]], uv)
        self.quad(material, [p["ltf"], p["rtf"], p["rtb"], p["ltb"]], uv)
        self.quad(material, [p["lbb"], p["rbb"], p["rbf"], p["lbf"]], uv)

    def tube(self, material: str, start: Vec3, end: Vec3, radius: float, segments: int = 8) -> None:
        axis = normalize(sub(end, start))
        reference = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.92 else (1.0, 0.0, 0.0)
        tangent = normalize(cross(axis, reference))
        bitangent = normalize(cross(axis, tangent))
        start_ring: list[int] = []
        end_ring: list[int] = []

        for index in range(segments + 1):
            angle = math.tau * index / segments
            radial = add(mul(tangent, math.cos(angle)), mul(bitangent, math.sin(angle)))
            uv_x = index / segments
            start_ring.append(self.vertex(add(start, mul(radial, radius)), (uv_x, 1.0), radial))
            end_ring.append(self.vertex(add(end, mul(radial, radius)), (uv_x, 0.0), radial))

        for index in range(segments):
            a, b = start_ring[index], start_ring[index + 1]
            c, d = end_ring[index + 1], end_ring[index]
            self.triangle(material, a, b, c)
            self.triangle(material, a, c, d)

    def torus(
        self,
        material: str,
        center: Vec3,
        major_radius: float,
        minor_radius: float,
        major_segments: int = 12,
        minor_segments: int = 6,
    ) -> None:
        grid: list[list[int]] = []
        for ring_index in range(major_segments + 1):
            ring_angle = math.tau * ring_index / major_segments
            radial = (math.cos(ring_angle), 0.0, math.sin(ring_angle))
            row: list[int] = []
            for side_index in range(minor_segments + 1):
                side_angle = math.tau * side_index / minor_segments
                normal = normalize(
                    (
                        radial[0] * math.cos(side_angle),
                        math.sin(side_angle),
                        radial[2] * math.cos(side_angle),
                    )
                )
                position = add(
                    center,
                    (
                        radial[0] * (major_radius + minor_radius * math.cos(side_angle)),
                        minor_radius * math.sin(side_angle),
                        radial[2] * (major_radius + minor_radius * math.cos(side_angle)),
                    ),
                )
                row.append(
                    self.vertex(
                        position,
                        (ring_index / major_segments, side_index / minor_segments),
                        normal,
                    )
                )
            grid.append(row)

        for ring_index in range(major_segments):
            for side_index in range(minor_segments):
                a = grid[ring_index][side_index]
                b = grid[ring_index + 1][side_index]
                c = grid[ring_index + 1][side_index + 1]
                d = grid[ring_index][side_index + 1]
                self.triangle(material, a, b, c)
                self.triangle(material, a, c, d)

    def write(self, path: Path) -> None:
        with path.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write("mtllib stage_select_bridge.mtl\n")
            stream.write("o stage_select_bridge\n")
            for vertex in self.vertices:
                stream.write(f"v {vertex[0]:.6f} {vertex[1]:.6f} {vertex[2]:.6f}\n")
            for uv in self.uvs:
                stream.write(f"vt {uv[0]:.6f} {uv[1]:.6f}\n")
            for normal in self.normals:
                stream.write(f"vn {normal[0]:.6f} {normal[1]:.6f} {normal[2]:.6f}\n")
            stream.write("s 1\n")
            for material, faces in self.faces.items():
                stream.write(f"usemtl {material}\n")
                for a, b, c in faces:
                    stream.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


def make_wood_texture(path: Path, base: tuple[int, int, int], phase: float) -> None:
    size = 128
    image = Image.new("RGBA", (size, size))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            wave = math.sin(y * 0.115 + math.sin(x * 0.072 + phase) * 2.6 + phase)
            fine = math.sin(y * 0.42 + x * 0.035 + phase * 2.0) * 0.35
            edge = -7.0 if y < 5 or y >= size - 5 else 0.0
            shade = wave * 9.0 + fine * 5.0 + edge
            pixels[x, y] = (
                max(0, min(255, int(base[0] + shade))),
                max(0, min(255, int(base[1] + shade * 0.72))),
                max(0, min(255, int(base[2] + shade * 0.38))),
                255,
            )
    image.save(path)


def make_rope_texture(path: Path) -> None:
    size = 128
    image = Image.new("RGBA", (size, size))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            strand = math.sin((x + y * 1.65) * 0.22)
            ridge = math.sin((x + y * 1.65) * 0.44) * 0.35
            shade = strand * 13.0 + ridge * 6.0
            pixels[x, y] = (
                max(0, min(255, int(224 + shade))),
                max(0, min(255, int(190 + shade * 0.72))),
                max(0, min(255, int(122 + shade * 0.38))),
                255,
            )
    image.save(path)


def make_metal_texture(path: Path) -> None:
    size = 64
    image = Image.new("RGBA", (size, size))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            highlight = max(0.0, 1.0 - math.sqrt((x - 21.0) ** 2 + (y - 18.0) ** 2) / 48.0)
            pixels[x, y] = (
                int(126 + 48 * highlight),
                int(91 + 40 * highlight),
                int(45 + 24 * highlight),
                255,
            )
    image.save(path)


def write_materials(path: Path) -> None:
    entries = [
        ("WoodLight", "bridge_wood_light.png", 18.0),
        ("WoodWarm", "bridge_wood_warm.png", 16.0),
        ("WoodDeep", "bridge_wood_deep.png", 14.0),
        ("Rope", "bridge_rope.png", 12.0),
        ("Peg", "bridge_peg.png", 34.0),
    ]
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for name, texture, shininess in entries:
            stream.write(f"newmtl {name}\n")
            stream.write(f"Ns {shininess:.3f}\n")
            stream.write("Ka 0.120 0.120 0.120\n")
            stream.write("Kd 1.000 1.000 1.000\n")
            stream.write("Ks 0.055 0.045 0.030\n")
            stream.write("d 1.000\n")
            stream.write("illum 2\n")
            stream.write(f"map_Kd {texture}\n\n")


def deck_height(z: float) -> float:
    normalized = min(1.0, abs(z) / 0.95)
    return 0.987 + 0.028 * (1.0 - normalized * normalized)


def add_rope_span(
    builder: ObjBuilder,
    x: float,
    z0: float,
    z1: float,
    y: float,
    sag: float,
    radius: float,
) -> None:
    divisions = 6
    previous = (x, y, z0)
    for index in range(1, divisions + 1):
        t = index / divisions
        z = z0 + (z1 - z0) * t
        sag_offset = sag * 4.0 * t * (1.0 - t)
        current = (x, y - sag_offset, z)
        builder.tube("Rope", previous, current, radius, 8)
        previous = current


def build_bridge() -> ObjBuilder:
    builder = ObjBuilder()
    plank_count = 13
    z_step = 1.90 / plank_count
    wood_materials = ["WoodLight", "WoodWarm", "WoodDeep"]

    # 既存ブロックのローカル上面 y=1 を歩行面として維持する。
    for index in range(plank_count):
        z = -0.95 + z_step * (index + 0.5)
        top = deck_height(z) + 0.006 * math.sin(index * 2.37)
        thickness = 0.105 + 0.008 * ((index * 5) % 4)
        width = 1.82 + 0.055 * math.sin(index * 1.71)
        yaw = 0.009 * math.sin(index * 1.93)
        roll = 0.008 * math.sin(index * 2.61)
        pitch = -0.018 * z
        builder.box(
            wood_materials[index % len(wood_materials)],
            (0.018 * math.sin(index * 1.37), top - thickness * 0.5, z),
            (width, thickness, z_step * 0.82),
            (pitch, yaw, roll),
        )

        for side in (-1.0, 1.0):
            builder.box(
                "Peg",
                (side * 0.72, top + 0.012, z),
                (0.036, 0.020, 0.014),
                (0.0, yaw, roll),
            )

    # 長手の梁と横桁で、横から見ても橋として成立する厚みを作る。
    for side in (-1.0, 1.0):
        builder.box("WoodDeep", (side * 0.73, 0.846, 0.0), (0.15, 0.17, 1.86))
    for z in (-0.73, -0.25, 0.25, 0.73):
        builder.box("WoodDeep", (0.0, 0.805, z), (1.72, 0.095, 0.058))

    post_z = (-0.86, -0.29, 0.29, 0.86)
    for side in (-1.0, 1.0):
        x = side * 0.87
        for index, z in enumerate(post_z):
            post_top = 1.39 + 0.018 * math.sin(index * 1.7 + side)
            post_bottom = 0.955
            builder.box(
                "WoodDeep",
                (x, (post_top + post_bottom) * 0.5, z),
                (0.080, post_top - post_bottom, 0.038),
                (0.0, 0.0, side * 0.012 * math.sin(index + 0.5)),
            )
            builder.torus("Rope", (x, 1.305, z), 0.052, 0.012)
            builder.torus("Rope", (x, 1.205, z), 0.052, 0.011)

        for z0, z1 in zip(post_z[:-1], post_z[1:]):
            add_rope_span(builder, x, z0, z1, 1.355, 0.095, 0.021)
            add_rope_span(builder, x, z0, z1, 1.205, 0.058, 0.016)

    return builder


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    make_wood_texture(OUTPUT_DIR / "bridge_wood_light.png", (191, 124, 59), 0.2)
    make_wood_texture(OUTPUT_DIR / "bridge_wood_warm.png", (171, 101, 47), 1.7)
    make_wood_texture(OUTPUT_DIR / "bridge_wood_deep.png", (116, 68, 39), 3.1)
    make_rope_texture(OUTPUT_DIR / "bridge_rope.png")
    make_metal_texture(OUTPUT_DIR / "bridge_peg.png")
    write_materials(OUTPUT_DIR / "stage_select_bridge.mtl")
    builder = build_bridge()
    builder.write(OUTPUT_DIR / "stage_select_bridge.obj")
    polygon_count = sum(len(faces) for faces in builder.faces.values())
    print(f"generated stage select bridge: {len(builder.vertices)} vertices, {polygon_count} triangles")


if __name__ == "__main__":
    main()
