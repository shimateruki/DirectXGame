"""プリズムスライム用の専用OBJモデルを再生成します。

外部DCCがない環境でも同じ形状を再生成できるよう、ボディ、コア、
コア枠を数式から作ります。実行時の色変更はゲーム側で行います。
"""

from __future__ import annotations

import math
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
MODEL_ROOT = PROJECT_ROOT / "Resources" / "3DModel" / "Characters"
EFFECT_MODEL_ROOT = PROJECT_ROOT / "Resources" / "3DModel" / "Effects"


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def _normalize(x: float, y: float, z: float) -> tuple[float, float, float]:
    length = math.sqrt(x * x + y * y + z * z)
    if length <= 1.0e-8:
        return 0.0, 1.0, 0.0
    return x / length, y / length, z / length


def _body_height(x: float, z: float) -> float:
    rx = 0.84
    rz = 0.64
    radial = math.sqrt((x / rx) ** 2 + (z / rz) ** 2)
    radial = min(radial, 1.0)
    dome = 0.10 + 0.72 * max(0.0, 1.0 - radial**1.72) ** 0.68

    # 三つの頂点は別パーツではなく、ボディ表面の連続した盛り上がりです。
    peaks = 0.0
    for center_x, amplitude, width_x in ((-0.43, 0.50, 0.145), (0.0, 0.44, 0.15), (0.43, 0.50, 0.145)):
        dx = (x - center_x) / width_x
        dz = (z + 0.025) / 0.30
        peaks += amplitude * math.exp(-(dx * dx + dz * dz) * 1.18)
    return dome + peaks * max(0.0, 1.0 - radial**2.8)


def _body_normal(x: float, z: float) -> tuple[float, float, float]:
    epsilon = 0.0025
    dhdx = (_body_height(x + epsilon, z) - _body_height(x - epsilon, z)) / (2.0 * epsilon)
    dhdz = (_body_height(x, z + epsilon) - _body_height(x, z - epsilon)) / (2.0 * epsilon)
    return _normalize(-dhdx, 1.0, -dhdz)


def generate_body() -> None:
    segments = 48
    rings = 18
    vertices: list[tuple[float, float, float]] = [(0.0, _body_height(0.0, 0.0), 0.0)]
    uvs: list[tuple[float, float]] = [(0.5, 0.5)]
    normals: list[tuple[float, float, float]] = [_body_normal(0.0, 0.0)]

    for ring in range(1, rings + 1):
        radius = ring / rings
        for segment in range(segments):
            angle = math.tau * segment / segments
            x = math.cos(angle) * 0.84 * radius
            z = math.sin(angle) * 0.64 * radius
            vertices.append((x, _body_height(x, z), z))
            uvs.append((0.5 + x / 1.68, 0.5 - z / 1.28))
            normals.append(_body_normal(x, z))

    faces: list[tuple[int, int, int]] = []
    for segment in range(segments):
        current = 1 + segment
        next_segment = 1 + (segment + 1) % segments
        faces.append((1, current + 1, next_segment + 1))

    for ring in range(1, rings):
        current_start = 1 + (ring - 1) * segments
        next_start = current_start + segments
        for segment in range(segments):
            a = current_start + segment
            b = current_start + (segment + 1) % segments
            c = next_start + segment
            d = next_start + (segment + 1) % segments
            faces.append((a + 1, c + 1, b + 1))
            faces.append((b + 1, c + 1, d + 1))

    # 側面と底面は専用頂点にして、接地面を丸めず安定させます。
    side_top_start = len(vertices)
    side_bottom_start = side_top_start + segments
    for segment in range(segments):
        angle = math.tau * segment / segments
        x = math.cos(angle) * 0.84
        z = math.sin(angle) * 0.64
        radial_normal = _normalize(x / (0.84 * 0.84), 0.14, z / (0.64 * 0.64))
        vertices.append((x, _body_height(x, z), z))
        uvs.append((segment / segments, 0.0))
        normals.append(radial_normal)
    for segment in range(segments):
        angle = math.tau * segment / segments
        x = math.cos(angle) * 0.84
        z = math.sin(angle) * 0.64
        radial_normal = _normalize(x / (0.84 * 0.84), 0.14, z / (0.64 * 0.64))
        vertices.append((x, 0.055, z))
        uvs.append((segment / segments, 1.0))
        normals.append(radial_normal)

    for segment in range(segments):
        next_segment = (segment + 1) % segments
        a = side_top_start + segment
        b = side_top_start + next_segment
        c = side_bottom_start + segment
        d = side_bottom_start + next_segment
        faces.append((a + 1, c + 1, b + 1))
        faces.append((b + 1, c + 1, d + 1))

    bottom_center = len(vertices)
    vertices.append((0.0, 0.055, 0.0))
    uvs.append((0.5, 0.5))
    normals.append((0.0, -1.0, 0.0))
    bottom_ring_start = len(vertices)
    for segment in range(segments):
        angle = math.tau * segment / segments
        x = math.cos(angle) * 0.84
        z = math.sin(angle) * 0.64
        vertices.append((x, 0.055, z))
        uvs.append((0.5 + x / 1.68, 0.5 - z / 1.28))
        normals.append((0.0, -1.0, 0.0))
    for segment in range(segments):
        next_segment = (segment + 1) % segments
        faces.append((bottom_center + 1, bottom_ring_start + next_segment + 1, bottom_ring_start + segment + 1))

    lines = ["# Prism Slime body", "mtllib prism_slime.mtl", "o PrismSlimeBody"]
    lines.extend(f"v {x:.7f} {y:.7f} {z:.7f}" for x, y, z in vertices)
    lines.extend(f"vt {u:.7f} {v:.7f}" for u, v in uvs)
    lines.extend(f"vn {x:.7f} {y:.7f} {z:.7f}" for x, y, z in normals)
    lines.extend(("usemtl PrismGel", "s 1"))
    lines.extend(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}" for a, b, c in faces)

    directory = MODEL_ROOT / "prism_slime"
    _write(directory / "prism_slime.obj", "\n".join(lines) + "\n")
    _write(
        directory / "prism_slime.mtl",
        "newmtl PrismGel\nKa 0.08 0.20 0.24\nKd 0.52 0.94 1.00\nKs 0.72 0.92 1.00\nNs 72.0\nd 1.0\nillum 2\n",
    )


def generate_core() -> None:
    vertices = [
        (0.0, 0.62, 0.0),
        (0.54, 0.0, 0.0),
        (0.0, 0.0, 0.34),
        (-0.54, 0.0, 0.0),
        (0.0, 0.0, -0.34),
        (0.0, -0.62, 0.0),
    ]
    faces = [
        (1, 2, 3), (1, 3, 4), (1, 4, 5), (1, 5, 2),
        (6, 3, 2), (6, 4, 3), (6, 5, 4), (6, 2, 5),
    ]
    lines = ["# Prism Slime faceted core", "mtllib prism_slime_core.mtl", "o PrismSlimeCore"]
    lines.extend(f"v {x:.7f} {y:.7f} {z:.7f}" for x, y, z in vertices)
    lines.extend(("usemtl PrismCore", "s off"))
    lines.extend(f"f {a} {b} {c}" for a, b, c in faces)
    directory = MODEL_ROOT / "prism_slime_core"
    _write(directory / "prism_slime_core.obj", "\n".join(lines) + "\n")
    _write(
        directory / "prism_slime_core.mtl",
        "newmtl PrismCore\nKa 0.18 0.34 0.42\nKd 0.70 0.98 1.00\nKs 1.00 1.00 1.00\nNs 110.0\nd 1.0\nillum 2\n",
    )


def generate_frame() -> None:
    major_segments = 40
    minor_segments = 8
    vertices: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    faces: list[tuple[int, int, int]] = []
    major_x = 0.66
    major_y = 0.82
    tube = 0.065

    # XY平面の楕円リング。+Zがモデル正面です。
    for major in range(major_segments):
        angle = math.tau * major / major_segments
        cx = math.cos(angle) * major_x
        cy = math.sin(angle) * major_y
        outward = _normalize(math.cos(angle) / major_x, math.sin(angle) / major_y, 0.0)
        for minor in range(minor_segments):
            tube_angle = math.tau * minor / minor_segments
            nx = outward[0] * math.cos(tube_angle)
            ny = outward[1] * math.cos(tube_angle)
            nz = math.sin(tube_angle)
            normal = _normalize(nx, ny, nz)
            vertices.append((cx + normal[0] * tube, cy + normal[1] * tube, normal[2] * tube))
            normals.append(normal)

    for major in range(major_segments):
        next_major = (major + 1) % major_segments
        for minor in range(minor_segments):
            next_minor = (minor + 1) % minor_segments
            a = major * minor_segments + minor + 1
            b = next_major * minor_segments + minor + 1
            c = major * minor_segments + next_minor + 1
            d = next_major * minor_segments + next_minor + 1
            faces.append((a, b, c))
            faces.append((c, b, d))

    lines = ["# Prism Slime core frame", "mtllib prism_slime_frame.mtl", "o PrismSlimeFrame"]
    lines.extend(f"v {x:.7f} {y:.7f} {z:.7f}" for x, y, z in vertices)
    lines.extend(f"vn {x:.7f} {y:.7f} {z:.7f}" for x, y, z in normals)
    lines.extend(("usemtl PrismFrame", "s 1"))
    lines.extend(f"f {a}//{a} {b}//{b} {c}//{c}" for a, b, c in faces)
    directory = MODEL_ROOT / "prism_slime_frame"
    _write(directory / "prism_slime_frame.obj", "\n".join(lines) + "\n")
    _write(
        directory / "prism_slime_frame.mtl",
        "newmtl PrismFrame\nKa 0.12 0.28 0.36\nKd 0.72 0.98 1.00\nKs 1.00 1.00 1.00\nNs 96.0\nd 1.0\nillum 2\n",
    )


def generate_attack_spike() -> None:
    vertices: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    faces: list[tuple[int, int, int]] = []

    def add_triangle(
        a: tuple[float, float, float],
        b: tuple[float, float, float],
        c: tuple[float, float, float],
    ) -> None:
        ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        normal = _normalize(
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        )
        first = len(vertices) + 1
        vertices.extend((a, b, c))
        normals.extend((normal, normal, normal))
        faces.append((first, first + 1, first + 2))

    def add_shard(
        center_x: float,
        center_z: float,
        radius: float,
        height: float,
        tip_x: float,
        tip_z: float,
        sides: int,
        yaw: float,
    ) -> None:
        ring: list[tuple[float, float, float]] = []
        for index in range(sides):
            angle = yaw + math.tau * index / sides
            ring.append((
                center_x + math.cos(angle) * radius,
                0.0,
                center_z + math.sin(angle) * radius,
            ))
        tip = (center_x + tip_x, height, center_z + tip_z)
        for index in range(sides):
            # Keep the side winding outward so the runtime back-face culling
            # does not remove the crystal body.
            add_triangle(ring[index], tip, ring[(index + 1) % sides])
        base_center = (center_x, 0.0, center_z)
        for index in range(1, sides - 1):
            add_triangle(base_center, ring[index], ring[index + 1])

    add_shard(0.0, 0.0, 0.38, 2.75, -0.06, 0.03, 6, 0.18)
    add_shard(-0.42, 0.10, 0.25, 1.82, -0.22, 0.10, 5, -0.14)
    add_shard(0.40, -0.12, 0.22, 1.52, 0.20, -0.08, 5, 0.42)

    lines = [
        "# Reusable faceted prism spike cluster",
        "mtllib prism_crystal_spike.mtl",
        "o PrismCrystalSpike",
    ]
    lines.extend(f"v {x:.7f} {y:.7f} {z:.7f}" for x, y, z in vertices)
    lines.extend(f"vn {x:.7f} {y:.7f} {z:.7f}" for x, y, z in normals)
    lines.extend(("usemtl PrismCrystal", "s off"))
    lines.extend(f"f {a}//{a} {b}//{b} {c}//{c}" for a, b, c in faces)

    directory = EFFECT_MODEL_ROOT / "prism_crystal_spike"
    _write(directory / "prism_crystal_spike.obj", "\n".join(lines) + "\n")
    _write(
        directory / "prism_crystal_spike.mtl",
        "newmtl PrismCrystal\nKa 0.12 0.24 0.34\nKd 0.44 0.84 1.00\nKs 1.00 1.00 1.00\nNs 128.0\nd 1.0\nillum 2\n",
    )


def main() -> None:
    generate_body()
    generate_core()
    generate_frame()
    generate_attack_spike()
    print("Prism Slime model assets generated.")


if __name__ == "__main__":
    main()
