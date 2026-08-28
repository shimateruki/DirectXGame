"""ステージ2中ボス用のマグマスライムモデルを生成します。

外部DCCに依存せず、OBJ/MTL、テクスチャ、DDSキャッシュ、Asset Database用の
metaを同じ結果で再生成できます。ゲーム処理や当たり判定はこのスクリプトの
対象外です。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


PROJECT_ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = PROJECT_ROOT / "Resources" / "3DModel" / "Characters" / "magma_slime"
HUD_OUT_DIR = PROJECT_ROOT / "Resources" / "sprite" / "ui" / "hud" / "boss"
TEXCONV_PATH = PROJECT_ROOT / "Resources" / "tools" / "Texconv.exe"


Vec2 = tuple[float, float]
Vec3 = tuple[float, float, float]


def add(a: Vec3, b: Vec3) -> Vec3:
    return a[0] + b[0], a[1] + b[1], a[2] + b[2]


def sub(a: Vec3, b: Vec3) -> Vec3:
    return a[0] - b[0], a[1] - b[1], a[2] - b[2]


def mul(value: Vec3, scale: float) -> Vec3:
    return value[0] * scale, value[1] * scale, value[2] * scale


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def normalize(value: Vec3) -> Vec3:
    length = math.sqrt(dot(value, value))
    if length <= 1.0e-8:
        return 0.0, 1.0, 0.0
    return value[0] / length, value[1] / length, value[2] / length


class ObjBuilder:
    def __init__(self) -> None:
        self.vertices: list[Vec3] = []
        self.uvs: list[Vec2] = []
        self.normals: list[Vec3] = []
        self.faces: dict[str, list[tuple[int, int, int]]] = {}
        self.smooth_materials: set[str] = set()

    def add_vertex(self, position: Vec3, uv: Vec2, normal: Vec3) -> int:
        self.vertices.append(position)
        self.uvs.append(uv)
        self.normals.append(normalize(normal))
        return len(self.vertices)

    def add_face(self, material: str, a: int, b: int, c: int, smooth: bool = True) -> None:
        pa = self.vertices[a - 1]
        pb = self.vertices[b - 1]
        pc = self.vertices[c - 1]
        if dot(cross(sub(pb, pa), sub(pc, pa)), cross(sub(pb, pa), sub(pc, pa))) <= 1.0e-12:
            return
        self.faces.setdefault(material, []).append((a, b, c))
        if smooth:
            self.smooth_materials.add(material)

    def add_flat_triangle(self, material: str, a: Vec3, b: Vec3, c: Vec3) -> None:
        normal = normalize(cross(sub(b, a), sub(c, a)))
        first = len(self.vertices) + 1
        self.add_vertex(a, (0.0, 0.0), normal)
        self.add_vertex(b, (1.0, 0.0), normal)
        self.add_vertex(c, (0.5, 1.0), normal)
        self.add_face(material, first, first + 1, first + 2, smooth=False)

    def add_flat_quad(self, material: str, a: Vec3, b: Vec3, c: Vec3, d: Vec3) -> None:
        self.add_flat_triangle(material, a, b, c)
        self.add_flat_triangle(material, a, c, d)

    @staticmethod
    def _body_point(radius: float, angle: float) -> Vec3:
        radius = max(0.0, min(1.0, radius))
        wobble = 1.0 + 0.028 * math.sin(angle * 3.0 + 0.35) + 0.018 * math.sin(angle * 7.0 - 0.6)
        x = math.cos(angle) * 1.72 * radius * wobble
        z = math.sin(angle) * 1.36 * radius * (1.0 + 0.022 * math.cos(angle * 5.0))
        dome = 1.66 * max(0.0, 1.0 - radius**1.72) ** 0.62
        crown_bias = 0.10 * math.exp(-((radius / 0.34) ** 2))
        y = 0.065 + dome + crown_bias
        return x, y, z

    @classmethod
    def _body_normal(cls, radius: float, angle: float) -> Vec3:
        if radius <= 1.0e-5:
            return 0.0, 1.0, 0.0
        epsilon_r = 0.0015
        epsilon_a = 0.0015
        radial = sub(cls._body_point(min(1.0, radius + epsilon_r), angle), cls._body_point(max(0.0, radius - epsilon_r), angle))
        tangent = sub(cls._body_point(radius, angle + epsilon_a), cls._body_point(radius, angle - epsilon_a))
        return normalize(cross(tangent, radial))

    def add_organic_body(self) -> None:
        material = "MagmaBody"
        segments = 72
        rings = 28
        center = self.add_vertex(self._body_point(0.0, 0.0), (0.5, 0.5), (0.0, 1.0, 0.0))
        rows: list[list[int]] = []
        for ring in range(1, rings + 1):
            radius = ring / rings
            row: list[int] = []
            for segment in range(segments):
                angle = math.tau * segment / segments
                position = self._body_point(radius, angle)
                uv = (0.5 + position[0] / 3.5, 0.5 - position[2] / 2.8)
                row.append(self.add_vertex(position, uv, self._body_normal(radius, angle)))
            rows.append(row)

        for segment in range(segments):
            next_segment = (segment + 1) % segments
            self.add_face(material, center, rows[0][segment], rows[0][next_segment])
        for ring in range(rings - 1):
            for segment in range(segments):
                next_segment = (segment + 1) % segments
                a = rows[ring][segment]
                b = rows[ring + 1][segment]
                c = rows[ring + 1][next_segment]
                d = rows[ring][next_segment]
                self.add_face(material, a, b, c)
                self.add_face(material, a, c, d)

        # 側面と底面を閉じ、配置時に地面から光が漏れない形にします。
        bottom_ring: list[int] = []
        for segment in range(segments):
            angle = math.tau * segment / segments
            top_position = self._body_point(1.0, angle)
            radial_normal = normalize((top_position[0] / (1.72 * 1.72), 0.08, top_position[2] / (1.36 * 1.36)))
            bottom_ring.append(self.add_vertex((top_position[0], 0.035, top_position[2]), (segment / segments, 1.0), radial_normal))
            top_index = rows[-1][segment]
            next_top = rows[-1][(segment + 1) % segments]
            bottom_index = bottom_ring[-1]
            # 次の底面頂点は全頂点生成後に接続します。
            if segment > 0:
                previous_top = rows[-1][segment - 1]
                previous_bottom = bottom_ring[segment - 1]
                self.add_face(material, previous_top, previous_bottom, top_index)
                self.add_face(material, top_index, previous_bottom, bottom_index)
        self.add_face(material, rows[-1][-1], bottom_ring[-1], rows[-1][0])
        self.add_face(material, rows[-1][0], bottom_ring[-1], bottom_ring[0])

        bottom_center = self.add_vertex((0.0, 0.035, 0.0), (0.5, 0.5), (0.0, -1.0, 0.0))
        for segment in range(segments):
            next_segment = (segment + 1) % segments
            self.add_face(material, bottom_center, bottom_ring[next_segment], bottom_ring[segment])

    def add_uv_sphere(
        self,
        material: str,
        center: Vec3,
        scale: Vec3,
        segments: int = 24,
        rings: int = 12,
    ) -> None:
        grid: list[list[int]] = []
        for ring in range(rings + 1):
            theta = math.pi * ring / rings
            row: list[int] = []
            for segment in range(segments + 1):
                phi = math.tau * segment / segments
                local = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
                position = (
                    center[0] + local[0] * scale[0],
                    center[1] + local[1] * scale[1],
                    center[2] + local[2] * scale[2],
                )
                normal = (
                    local[0] / max(scale[0], 1.0e-6),
                    local[1] / max(scale[1], 1.0e-6),
                    local[2] / max(scale[2], 1.0e-6),
                )
                row.append(self.add_vertex(position, (segment / segments, ring / rings), normal))
            grid.append(row)
        for ring in range(rings):
            for segment in range(segments):
                a = grid[ring][segment]
                b = grid[ring + 1][segment]
                c = grid[ring + 1][segment + 1]
                d = grid[ring][segment + 1]
                self.add_face(material, a, b, c)
                self.add_face(material, a, c, d)

    def add_faceted_rock(self, material: str, center: Vec3, scale: Vec3, seed: int) -> None:
        rng = random.Random(seed)
        segments = 10
        rings = 5
        positions: list[list[Vec3]] = []
        for ring in range(rings + 1):
            theta = math.pi * ring / rings
            row: list[Vec3] = []
            for segment in range(segments):
                phi = math.tau * segment / segments
                jitter = 0.88 + rng.random() * 0.22
                local = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
                row.append((
                    center[0] + local[0] * scale[0] * jitter,
                    center[1] + local[1] * scale[1] * jitter,
                    center[2] + local[2] * scale[2] * jitter,
                ))
            positions.append(row)
        for ring in range(rings):
            for segment in range(segments):
                next_segment = (segment + 1) % segments
                a = positions[ring][segment]
                b = positions[ring + 1][segment]
                c = positions[ring + 1][next_segment]
                d = positions[ring][next_segment]
                self.add_flat_triangle(material, a, b, c)
                self.add_flat_triangle(material, a, c, d)

    def add_cone_shard(
        self,
        material: str,
        base_center: Vec3,
        radius: float,
        tip: Vec3,
        sides: int,
        seed: int,
    ) -> None:
        rng = random.Random(seed)
        ring: list[Vec3] = []
        for side in range(sides):
            angle = math.tau * side / sides + 0.18
            local_radius = radius * (0.82 + rng.random() * 0.28)
            ring.append((
                base_center[0] + math.cos(angle) * local_radius,
                base_center[1] + (rng.random() - 0.5) * 0.08,
                base_center[2] + math.sin(angle) * local_radius,
            ))
        for side in range(sides):
            self.add_flat_triangle(material, ring[side], tip, ring[(side + 1) % sides])
        for side in range(1, sides - 1):
            self.add_flat_triangle(material, ring[0], ring[side + 1], ring[side])

    def add_horizontal_torus(
        self,
        material: str,
        center: Vec3,
        radius_x: float,
        radius_z: float,
        tube_radius: float,
        segments: int = 36,
        tube_segments: int = 8,
    ) -> None:
        grid: list[list[int]] = []
        for segment in range(segments + 1):
            theta = math.tau * segment / segments
            outward = normalize((math.cos(theta) / radius_x, 0.0, math.sin(theta) / radius_z))
            ring_center = (
                center[0] + math.cos(theta) * radius_x,
                center[1],
                center[2] + math.sin(theta) * radius_z,
            )
            row: list[int] = []
            for side in range(tube_segments + 1):
                phi = math.tau * side / tube_segments
                normal = normalize(add(mul(outward, math.cos(phi)), (0.0, math.sin(phi), 0.0)))
                position = add(ring_center, mul(normal, tube_radius))
                row.append(self.add_vertex(position, (segment / segments, side / tube_segments), normal))
            grid.append(row)
        for segment in range(segments):
            for side in range(tube_segments):
                a = grid[segment][side]
                b = grid[segment + 1][side]
                c = grid[segment + 1][side + 1]
                d = grid[segment][side + 1]
                self.add_face(material, a, b, c)
                self.add_face(material, a, c, d)

    def add_ribbon(self, material: str, points: list[Vec3], width: float, surface_normal: Vec3) -> None:
        for start, end in zip(points, points[1:]):
            tangent = normalize(sub(end, start))
            side = mul(normalize(cross(surface_normal, tangent)), width * 0.5)
            self.add_flat_quad(material, sub(start, side), add(start, side), add(end, side), sub(end, side))

    def write_obj(self, path: Path) -> None:
        with path.open("w", encoding="utf-8", newline="\n") as file:
            file.write("# CG2 Stage 2 mid-boss Magma Slime\n")
            file.write("mtllib magma_slime.mtl\n")
            file.write("o MagmaSlime\n")
            for vertex in self.vertices:
                file.write(f"v {vertex[0]:.7f} {vertex[1]:.7f} {vertex[2]:.7f}\n")
            for uv in self.uvs:
                file.write(f"vt {uv[0]:.7f} {uv[1]:.7f}\n")
            for normal in self.normals:
                file.write(f"vn {normal[0]:.7f} {normal[1]:.7f} {normal[2]:.7f}\n")
            for material, faces in self.faces.items():
                file.write(f"usemtl {material}\n")
                file.write("s 1\n" if material in self.smooth_materials else "s off\n")
                for a, b, c in faces:
                    file.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


MATERIALS = {
    "MagmaBody": {
        "texture": "magma_slime_body.png",
        "ka": (0.42, 0.030, 0.002),
        "kd": (1.00, 0.165, 0.012),
        "ks": (0.72, 0.22, 0.035),
        "ke": (0.20, 0.018, 0.001),
        "ns": 92.0,
        "preview": (232, 54, 9),
    },
    "HotLava": {
        "texture": "magma_slime_hot.png",
        "ka": (0.92, 0.30, 0.008),
        "kd": (1.00, 0.68, 0.035),
        "ks": (1.00, 0.78, 0.18),
        "ke": (0.88, 0.22, 0.004),
        "ns": 128.0,
        "preview": (255, 182, 25),
    },
    "ObsidianCrust": {
        "texture": "magma_slime_obsidian.png",
        "ka": (0.008, 0.003, 0.006),
        "kd": (0.045, 0.012, 0.020),
        "ks": (0.48, 0.16, 0.20),
        "ke": (0.0, 0.0, 0.0),
        "ns": 138.0,
        "preview": (35, 12, 19),
    },
    "EyeSocket": {
        "texture": "magma_slime_eye.png",
        "ka": (0.002, 0.001, 0.002),
        "kd": (0.008, 0.003, 0.005),
        "ks": (0.15, 0.04, 0.06),
        "ke": (0.0, 0.0, 0.0),
        "ns": 72.0,
        "preview": (9, 3, 5),
    },
    "EyeGlow": {
        "texture": "magma_slime_eye_glow.png",
        "ka": (0.95, 0.54, 0.02),
        "kd": (1.00, 0.91, 0.24),
        "ks": (1.00, 0.92, 0.46),
        "ke": (1.00, 0.44, 0.015),
        "ns": 180.0,
        "preview": (255, 242, 92),
    },
    "Ember": {
        "texture": "magma_slime_ember.png",
        "ka": (0.68, 0.035, 0.001),
        "kd": (1.00, 0.12, 0.004),
        "ks": (0.82, 0.24, 0.02),
        "ke": (0.62, 0.025, 0.001),
        "ns": 110.0,
        "preview": (255, 71, 8),
    },
}


def build_model() -> ObjBuilder:
    model = ObjBuilder()
    model.add_organic_body()

    # 黒曜石の殻は顔を隠さず、上面から側面へ割れて残った配置にします。
    crust_rocks = [
        ((-0.78, 1.31, -0.30), (0.66, 0.24, 0.58), 101),
        ((0.82, 1.28, -0.25), (0.69, 0.25, 0.55), 102),
        ((-1.31, 0.62, 0.08), (0.48, 0.36, 0.52), 103),
        ((1.33, 0.60, 0.04), (0.50, 0.34, 0.50), 104),
        ((-1.02, 0.57, -0.80), (0.55, 0.35, 0.45), 105),
        ((1.04, 0.58, -0.78), (0.58, 0.37, 0.46), 106),
        ((0.02, 1.48, -0.73), (0.62, 0.22, 0.46), 107),
    ]
    for center, scale, seed in crust_rocks:
        model.add_faceted_rock("ObsidianCrust", center, scale, seed)

    # 中央火口と三本角で、中ボスとして遠距離から判別できる輪郭を作ります。
    model.add_uv_sphere("HotLava", (0.0, 1.70, -0.12), (0.34, 0.075, 0.27), 28, 8)
    model.add_horizontal_torus("ObsidianCrust", (0.0, 1.78, -0.12), 0.43, 0.34, 0.105)
    left_tip = (-0.82, 2.31, -0.16)
    right_tip = (0.84, 2.29, -0.18)
    center_tip = (0.05, 2.59, -0.64)
    model.add_cone_shard("ObsidianCrust", (-0.54, 1.50, -0.18), 0.31, left_tip, 7, 201)
    model.add_cone_shard("ObsidianCrust", (0.55, 1.49, -0.19), 0.31, right_tip, 7, 202)
    model.add_cone_shard("ObsidianCrust", (0.00, 1.58, -0.57), 0.37, center_tip, 8, 203)
    model.add_cone_shard("Ember", (-0.79, 2.15, -0.16), 0.075, left_tip, 6, 211)
    model.add_cone_shard("Ember", (0.81, 2.13, -0.18), 0.075, right_tip, 6, 212)
    model.add_cone_shard("HotLava", (0.04, 2.40, -0.63), 0.085, center_tip, 6, 213)

    # 正面の発光亀裂。枝分かれさせ、単なる赤いスライムとの差を明確にします。
    front_normal = (0.0, 0.0, 1.0)
    crack_paths = [
        [(-0.02, 1.50, 0.73), (-0.10, 1.28, 1.00), (0.02, 1.08, 1.14), (-0.08, 0.84, 1.27)],
        [(-0.08, 1.24, 1.03), (-0.40, 1.12, 1.08), (-0.58, 0.93, 1.18)],
        [(0.01, 1.08, 1.15), (0.31, 0.96, 1.18), (0.49, 0.75, 1.26)],
        [(-0.95, 0.96, 0.89), (-1.10, 0.76, 0.98), (-1.02, 0.54, 1.08)],
        [(0.96, 0.94, 0.87), (1.10, 0.72, 0.97), (1.04, 0.50, 1.04)],
    ]
    for index, points in enumerate(crack_paths):
        model.add_ribbon("HotLava", points, 0.060 if index == 0 else 0.043, front_normal)

    # 暗い眼窩と縦長の高温瞳で、攻撃的だがスライムらしい表情を残します。
    model.add_uv_sphere("EyeSocket", (-0.48, 0.92, 1.12), (0.24, 0.29, 0.070), 24, 12)
    model.add_uv_sphere("EyeSocket", (0.48, 0.92, 1.12), (0.24, 0.29, 0.070), 24, 12)
    model.add_uv_sphere("EyeGlow", (-0.47, 0.91, 1.185), (0.060, 0.145, 0.024), 16, 10)
    model.add_uv_sphere("EyeGlow", (0.47, 0.91, 1.185), (0.060, 0.145, 0.024), 16, 10)
    model.add_ribbon("ObsidianCrust", [(-0.73, 1.19, 1.12), (-0.43, 1.12, 1.20), (-0.24, 1.16, 1.15)], 0.105, front_normal)
    model.add_ribbon("ObsidianCrust", [(0.73, 1.19, 1.12), (0.43, 1.12, 1.20), (0.24, 1.16, 1.15)], 0.105, front_normal)
    model.add_ribbon("EyeSocket", [(-0.34, 0.55, 1.22), (-0.16, 0.45, 1.29), (0.00, 0.42, 1.31), (0.16, 0.45, 1.29), (0.34, 0.55, 1.22)], 0.075, front_normal)

    # 裾の溶岩滴は本体につなげ、浮遊パーツに見えない位置へ配置します。
    drip_data = [
        ((-1.46, 0.18, 0.47), (0.27, 0.18, 0.27)),
        ((-0.95, 0.13, 1.03), (0.24, 0.13, 0.22)),
        ((0.00, 0.12, 1.29), (0.31, 0.12, 0.19)),
        ((0.93, 0.13, 1.04), (0.24, 0.13, 0.22)),
        ((1.47, 0.18, 0.45), (0.27, 0.18, 0.27)),
    ]
    for center, scale in drip_data:
        model.add_uv_sphere("HotLava", center, scale, 20, 8)

    return model


def clamp_byte(value: float) -> int:
    return max(0, min(255, int(round(value))))


def create_textures() -> list[Path]:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    generated: list[Path] = []

    size = 256
    body = Image.new("RGBA", (size, size))
    pixels = body.load()
    for y in range(size):
        v = y / (size - 1)
        for x in range(size):
            u = x / (size - 1)
            flow = 0.5 + 0.5 * math.sin(u * 19.0 + math.sin(v * 11.0) * 2.4)
            cells = 0.5 + 0.5 * math.sin(u * 37.0 + v * 21.0) * math.sin(v * 31.0 - u * 13.0)
            heat = 0.62 * flow + 0.38 * cells
            pixels[x, y] = (
                clamp_byte(206 + heat * 49),
                clamp_byte(28 + heat * 58 + (1.0 - v) * 15),
                clamp_byte(4 + heat * 9),
                255,
            )
    body_path = OUT_DIR / "magma_slime_body.png"
    body.save(body_path)
    generated.append(body_path)

    hot = Image.new("RGBA", (128, 128))
    pixels = hot.load()
    for y in range(128):
        for x in range(128):
            u = x / 127.0
            v = y / 127.0
            wave = 0.5 + 0.5 * math.sin(u * 15.0 + math.sin(v * 9.0) * 1.8)
            pixels[x, y] = (255, clamp_byte(142 + wave * 93), clamp_byte(12 + wave * 42), 255)
    hot_path = OUT_DIR / "magma_slime_hot.png"
    hot.save(hot_path)
    generated.append(hot_path)

    rng = random.Random(4128)
    obsidian = Image.new("RGBA", (128, 128))
    pixels = obsidian.load()
    for y in range(128):
        for x in range(128):
            ridge = 0.5 + 0.5 * math.sin(x * 0.19 + math.sin(y * 0.13) * 2.0)
            noise = rng.random()
            value = 8.0 + ridge * 10.0 + noise * 7.0
            pixels[x, y] = (clamp_byte(value * 1.38), clamp_byte(value * 0.48), clamp_byte(value * 0.72), 255)
    obsidian_path = OUT_DIR / "magma_slime_obsidian.png"
    obsidian.save(obsidian_path)
    generated.append(obsidian_path)

    solids = {
        "magma_slime_eye.png": (6, 2, 4, 255),
        "magma_slime_eye_glow.png": (255, 236, 74, 255),
        "magma_slime_ember.png": (255, 59, 5, 255),
    }
    for name, color in solids.items():
        path = OUT_DIR / name
        Image.new("RGBA", (32, 32), color).save(path)
        generated.append(path)
    return generated


def write_materials() -> Path:
    path = OUT_DIR / "magma_slime.mtl"
    with path.open("w", encoding="utf-8", newline="\n") as file:
        file.write("# CG2 Magma Slime material set\n\n")
        for name, material in MATERIALS.items():
            file.write(f"newmtl {name}\n")
            file.write(f"Ka {material['ka'][0]:.6f} {material['ka'][1]:.6f} {material['ka'][2]:.6f}\n")
            file.write(f"Kd {material['kd'][0]:.6f} {material['kd'][1]:.6f} {material['kd'][2]:.6f}\n")
            file.write(f"Ks {material['ks'][0]:.6f} {material['ks'][1]:.6f} {material['ks'][2]:.6f}\n")
            file.write(f"Ke {material['ke'][0]:.6f} {material['ke'][1]:.6f} {material['ke'][2]:.6f}\n")
            file.write(f"Ns {material['ns']:.6f}\n")
            file.write("Ni 1.450000\n")
            file.write("d 1.000000\n")
            file.write("illum 2\n")
            file.write(f"map_Kd {material['texture']}\n\n")
    return path


def convert_textures_to_dds(png_paths: list[Path], output_dir: Path = OUT_DIR) -> list[Path]:
    if not TEXCONV_PATH.is_file():
        print(f"Warning: Texconv.exe was not found: {TEXCONV_PATH}")
        return []
    dds_paths: list[Path] = []
    for png_path in png_paths:
        completed = subprocess.run(
            [
                str(TEXCONV_PATH),
                "-f",
                "BC7_UNORM_SRGB",
                "-y",
                "-m",
                "0",
                "-o",
                str(output_dir),
                str(png_path),
            ],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if completed.returncode != 0:
            raise RuntimeError(f"DDS conversion failed: {png_path.name}\n{completed.stdout}\n{completed.stderr}")
        dds_path = png_path.with_suffix(".dds")
        if not dds_path.is_file():
            raise RuntimeError(f"DDS was not generated: {dds_path}")
        dds_paths.append(dds_path)
    return dds_paths


def create_boss_name_sprite() -> Path:
    """既存ボスHUDと同じ寸法で、透明背景のマグマ用ボス名を生成します。"""

    HUD_OUT_DIR.mkdir(parents=True, exist_ok=True)
    output = HUD_OUT_DIR / "magma_slime_name.png"
    image = Image.new("RGBA", (800, 168), (0, 0, 0, 0))
    font_candidates = (
        Path("C:/Windows/Fonts/MEIRYOBD.TTC"),
        Path("C:/Windows/Fonts/meiryob.ttc"),
        Path("C:/Windows/Fonts/YuGothB.ttc"),
    )
    font_path = next((candidate for candidate in font_candidates if candidate.is_file()), None)
    if font_path is None:
        raise RuntimeError("日本語ボス名に使用するフォントが見つかりません。")

    font = ImageFont.truetype(str(font_path), 84)
    text = "マグマスライム"
    measure = ImageDraw.Draw(image, "RGBA")
    bounds = measure.textbbox((0, 0), text, font=font, stroke_width=0)
    text_width = bounds[2] - bounds[0]
    text_height = bounds[3] - bounds[1]
    origin = ((800 - text_width) * 0.5, (168 - text_height) * 0.5 - bounds[1] - 3)

    # 黒曜石色の外縁と赤熱した内縁を重ね、細い文字でも背景へ埋もれないようにします。
    draw = ImageDraw.Draw(image, "RGBA")
    draw.text(
        origin, text, font=font, fill=(255, 112, 18, 255),
        stroke_width=13, stroke_fill=(48, 10, 12, 246),
    )
    draw.text(
        origin, text, font=font, fill=(255, 146, 22, 255),
        stroke_width=7, stroke_fill=(156, 24, 10, 255),
    )

    text_mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(text_mask).text(origin, text, font=font, fill=255)
    gradient = Image.new("RGBA", image.size, (0, 0, 0, 0))
    gradient_pixels = gradient.load()
    top = max(0, int(origin[1] + bounds[1]))
    bottom = min(image.height - 1, int(origin[1] + bounds[3]))
    height = max(1, bottom - top)
    for y in range(top, bottom + 1):
        rate = (y - top) / height
        red = 255
        green = int(244 - rate * 154)
        blue = int(118 - rate * 104)
        for x in range(image.width):
            gradient_pixels[x, y] = (red, green, blue, 255)
    image.alpha_composite(Image.composite(gradient, Image.new("RGBA", image.size), text_mask))

    # 左右の火種は名称の輪郭と干渉しない位置へ置き、マグマHUDだと一目で判別させます。
    for center_x, mirror in ((54, 1.0), (746, -1.0)):
        flame = [
            (center_x, 38),
            (center_x + int(18 * mirror), 72),
            (center_x + int(10 * mirror), 121),
            (center_x, 140),
            (center_x - int(15 * mirror), 105),
            (center_x - int(12 * mirror), 67),
        ]
        draw.polygon(flame, fill=(255, 58, 10, 248))
        draw.polygon(
            [(center_x, 66), (center_x + int(8 * mirror), 95), (center_x, 124), (center_x - int(6 * mirror), 98)],
            fill=(255, 226, 72, 245),
        )
        draw.ellipse((center_x - 4, 88, center_x + 4, 109), fill=(255, 255, 210, 225))

    image.save(output)
    return output


def stable_guid(relative_path: str) -> str:
    return hashlib.md5(f"cg2:magma-slime:{relative_path}".encode("utf-8")).hexdigest()


def write_meta(asset_path: Path, asset_type: str, importer: str, import_settings: dict) -> Path:
    relative_path = asset_path.relative_to(PROJECT_ROOT).as_posix()
    meta = {
        "assetType": asset_type,
        "guid": stable_guid(relative_path),
        "importSettings": import_settings,
        "importer": importer,
        "source": relative_path,
        "version": 1,
    }
    meta_path = Path(str(asset_path) + ".meta")
    meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return meta_path


def write_all_meta(obj_path: Path, mtl_path: Path, textures: list[Path]) -> None:
    write_meta(obj_path, "Model", "ModelImporter", {"generateTangents": True, "scale": 1.0})
    write_meta(mtl_path, "Binary", "BinaryImporter", {})
    for texture in textures:
        write_meta(texture, "Texture", "TextureImporter", {"colorSpace": "Auto", "generateMipmaps": True})


def validate_assets(
    model: ObjBuilder,
    obj_path: Path,
    material_path: Path,
    png_paths: list[Path],
    dds_paths: list[Path],
) -> tuple[Vec3, Vec3]:
    if not obj_path.is_file() or obj_path.stat().st_size == 0:
        raise RuntimeError("OBJが生成されていません。")
    if not material_path.is_file() or material_path.stat().st_size == 0:
        raise RuntimeError("MTLが生成されていません。")
    if set(model.faces) != set(MATERIALS):
        missing = sorted(set(MATERIALS) - set(model.faces))
        extra = sorted(set(model.faces) - set(MATERIALS))
        raise RuntimeError(f"マテリアル構成が不正です。missing={missing}, extra={extra}")

    vertex_count = len(model.vertices)
    for material, faces in model.faces.items():
        if not faces:
            raise RuntimeError(f"面がないマテリアルがあります: {material}")
        for face in faces:
            if any(index < 1 or index > vertex_count for index in face):
                raise RuntimeError(f"OBJの面Indexが範囲外です: {material} {face}")
            a, b, c = (model.vertices[index - 1] for index in face)
            area_normal = cross(sub(b, a), sub(c, a))
            if dot(area_normal, area_normal) <= 1.0e-12:
                raise RuntimeError(f"面積0の三角形があります: {material} {face}")

    texture_names = {path.name for path in png_paths}
    referenced_names = {str(material["texture"]) for material in MATERIALS.values()}
    if texture_names != referenced_names:
        raise RuntimeError(f"MTLのテクスチャ参照が不正です: generated={texture_names}, referenced={referenced_names}")
    for texture in png_paths:
        with Image.open(texture) as image:
            image.verify()
    for dds_path in dds_paths:
        if dds_path.read_bytes()[:4] != b"DDS ":
            raise RuntimeError(f"DDSヘッダーが不正です: {dds_path}")

    bounds_min = tuple(min(vertex[axis] for vertex in model.vertices) for axis in range(3))
    bounds_max = tuple(max(vertex[axis] for vertex in model.vertices) for axis in range(3))
    if bounds_min[1] < -1.0e-4 or bounds_min[1] > 0.06:
        raise RuntimeError(f"接地位置が不正です: minY={bounds_min[1]:.6f}")
    if bounds_max[1] < 2.4:
        raise RuntimeError(f"中ボス用の上端シルエットが不足しています: maxY={bounds_max[1]:.6f}")
    return bounds_min, bounds_max


def render_preview(model: ObjBuilder, output_path: Path) -> None:
    scale = 2
    width = 760 * scale
    height = 620 * scale
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image, "RGBA")

    camera = (4.65, 3.25, 6.20)
    target = (0.0, 1.10, 0.0)
    forward = normalize(sub(target, camera))
    right = normalize(cross(forward, (0.0, 1.0, 0.0)))
    camera_up = normalize(cross(right, forward))
    light = normalize((-0.35, 0.82, 0.46))
    focal = 790.0 * scale

    projected: list[tuple[float, float, float]] = []
    for vertex in model.vertices:
        relative = sub(vertex, camera)
        depth = dot(relative, forward)
        view_x = dot(relative, right)
        view_y = dot(relative, camera_up)
        factor = focal / max(depth, 0.2)
        projected.append((width * 0.5 + view_x * factor, height * 0.58 - view_y * factor, depth))

    draw.ellipse((width * 0.17, height * 0.78, width * 0.83, height * 0.91), fill=(10, 4, 6, 76))

    triangles: list[tuple[float, str, tuple[int, int, int], float]] = []
    emissive = {"HotLava", "EyeGlow", "Ember"}
    for material, faces in model.faces.items():
        for face in faces:
            normal = normalize((
                sum(model.normals[index - 1][0] for index in face),
                sum(model.normals[index - 1][1] for index in face),
                sum(model.normals[index - 1][2] for index in face),
            ))
            brightness = 1.0 if material in emissive else 0.44 + 0.56 * max(0.0, dot(normal, light))
            depth = sum(projected[index - 1][2] for index in face) / 3.0
            triangles.append((depth, material, face, brightness))
    triangles.sort(key=lambda item: item[0], reverse=True)

    for _, material, face, brightness in triangles:
        base = MATERIALS[material]["preview"]
        color = tuple(clamp_byte(channel * brightness) for channel in base) + (255,)
        polygon = [(projected[index - 1][0], projected[index - 1][1]) for index in face]
        outline = (70, 22, 24, 120) if material == "ObsidianCrust" else None
        draw.polygon(polygon, fill=color, outline=outline)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.resize((width // scale, height // scale), Image.Resampling.LANCZOS).save(output_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="マグマスライムのモデルアセットを生成します。")
    parser.add_argument("--preview", type=Path, help="確認用PNGの出力先。プロジェクト外を推奨します。")
    parser.add_argument("--hud-only", action="store_true", help="ボス名HUDだけを生成します。")
    args = parser.parse_args()

    boss_name_png = create_boss_name_sprite()
    boss_name_dds = convert_textures_to_dds([boss_name_png], HUD_OUT_DIR)
    write_meta(boss_name_png, "Texture", "TextureImporter", {"colorSpace": "Auto", "generateMipmaps": False})
    for dds_path in boss_name_dds:
        write_meta(dds_path, "Texture", "TextureImporter", {"colorSpace": "Auto", "generateMipmaps": False})
    if args.hud_only:
        print(f"Generated HUD: {boss_name_png}")
        return

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    png_paths = create_textures()
    material_path = write_materials()
    model = build_model()
    obj_path = OUT_DIR / "magma_slime.obj"
    model.write_obj(obj_path)
    dds_paths = convert_textures_to_dds(png_paths)
    write_all_meta(obj_path, material_path, png_paths + dds_paths)
    bounds_min, bounds_max = validate_assets(model, obj_path, material_path, png_paths, dds_paths)
    if args.preview:
        render_preview(model, args.preview.resolve())

    face_count = sum(len(faces) for faces in model.faces.values())
    print(f"Generated: {obj_path}")
    print(f"Vertices: {len(model.vertices)}")
    print(f"Triangles: {face_count}")
    print(f"Materials: {len(model.faces)}")
    print(
        "Bounds: "
        f"min=({bounds_min[0]:.3f}, {bounds_min[1]:.3f}, {bounds_min[2]:.3f}) "
        f"max=({bounds_max[0]:.3f}, {bounds_max[1]:.3f}, {bounds_max[2]:.3f})"
    )
    if args.preview:
        print(f"Preview: {args.preview.resolve()}")


if __name__ == "__main__":
    main()
