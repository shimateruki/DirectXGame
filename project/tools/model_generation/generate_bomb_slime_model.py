from __future__ import annotations

import math
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "Resources" / "3DModel" / "Characters" / "bomb_slime"


class ObjBuilder:
    def __init__(self) -> None:
        self.vertices: list[tuple[float, float, float]] = []
        self.uvs: list[tuple[float, float]] = []
        self.normals: list[tuple[float, float, float]] = []
        self.faces: dict[str, list[tuple[int, int, int]]] = {}

    @staticmethod
    def normalize(v: tuple[float, float, float]) -> tuple[float, float, float]:
        length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
        if length <= 1e-6:
            return (0.0, 1.0, 0.0)
        return (v[0] / length, v[1] / length, v[2] / length)

    @staticmethod
    def cross(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
        return (
            a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0],
        )

    @staticmethod
    def sub(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
        return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

    @staticmethod
    def add(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
        return (a[0] + b[0], a[1] + b[1], a[2] + b[2])

    @staticmethod
    def mul(a: tuple[float, float, float], s: float) -> tuple[float, float, float]:
        return (a[0] * s, a[1] * s, a[2] * s)

    def add_vertex(
        self,
        position: tuple[float, float, float],
        uv: tuple[float, float],
        normal: tuple[float, float, float],
    ) -> int:
        self.vertices.append(position)
        self.uvs.append(uv)
        self.normals.append(self.normalize(normal))
        return len(self.vertices)

    def add_face(self, material: str, a: int, b: int, c: int) -> None:
        self.faces.setdefault(material, []).append((a, b, c))

    def add_uv_sphere(
        self,
        material: str,
        center: tuple[float, float, float],
        scale: tuple[float, float, float],
        segments: int,
        rings: int,
        theta_min: float = 0.0,
        theta_max: float = math.pi,
    ) -> None:
        grid: list[list[int]] = []
        for i in range(rings + 1):
            theta = theta_min + (theta_max - theta_min) * (i / rings)
            row: list[int] = []
            sin_t = math.sin(theta)
            cos_t = math.cos(theta)
            for j in range(segments + 1):
                phi = 2.0 * math.pi * (j / segments)
                cos_p = math.cos(phi)
                sin_p = math.sin(phi)
                local = (sin_t * cos_p, cos_t, sin_t * sin_p)
                position = (
                    center[0] + local[0] * scale[0],
                    center[1] + local[1] * scale[1],
                    center[2] + local[2] * scale[2],
                )
                normal = (
                    local[0] / max(scale[0], 1e-6),
                    local[1] / max(scale[1], 1e-6),
                    local[2] / max(scale[2], 1e-6),
                )
                row.append(self.add_vertex(position, (j / segments, i / rings), normal))
            grid.append(row)

        for i in range(rings):
            for j in range(segments):
                a = grid[i][j]
                b = grid[i + 1][j]
                c = grid[i + 1][j + 1]
                d = grid[i][j + 1]
                self.add_face(material, a, b, c)
                self.add_face(material, a, c, d)

    def add_cylinder_between(
        self,
        material: str,
        start: tuple[float, float, float],
        end: tuple[float, float, float],
        radius: float,
        segments: int,
    ) -> None:
        axis = self.normalize(self.sub(end, start))
        helper = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.92 else (1.0, 0.0, 0.0)
        right = self.normalize(self.cross(axis, helper))
        up = self.normalize(self.cross(right, axis))

        start_ring: list[int] = []
        end_ring: list[int] = []
        for i in range(segments + 1):
            angle = 2.0 * math.pi * (i / segments)
            radial = self.normalize(self.add(self.mul(right, math.cos(angle)), self.mul(up, math.sin(angle))))
            offset = self.mul(radial, radius)
            uvx = i / segments
            start_ring.append(self.add_vertex(self.add(start, offset), (uvx, 1.0), radial))
            end_ring.append(self.add_vertex(self.add(end, offset), (uvx, 0.0), radial))

        for i in range(segments):
            a = start_ring[i]
            b = end_ring[i]
            c = end_ring[i + 1]
            d = start_ring[i + 1]
            self.add_face(material, a, b, c)
            self.add_face(material, a, c, d)

    def add_elliptic_torus(
        self,
        material: str,
        center: tuple[float, float, float],
        radius_x: float,
        radius_z: float,
        tube_radius: float,
        segments: int,
        tube_segments: int,
    ) -> None:
        grid: list[list[int]] = []
        for i in range(segments + 1):
            theta = 2.0 * math.pi * (i / segments)
            cx = radius_x * math.cos(theta)
            cz = radius_z * math.sin(theta)
            ring_center = (center[0] + cx, center[1], center[2] + cz)
            outward = self.normalize((math.cos(theta) / max(radius_x, 1e-6), 0.0, math.sin(theta) / max(radius_z, 1e-6)))
            vertical = (0.0, 1.0, 0.0)
            row: list[int] = []
            for j in range(tube_segments + 1):
                phi = 2.0 * math.pi * (j / tube_segments)
                normal = self.normalize(self.add(self.mul(outward, math.cos(phi)), self.mul(vertical, math.sin(phi))))
                pos = self.add(ring_center, self.mul(normal, tube_radius))
                row.append(self.add_vertex(pos, (i / segments, j / tube_segments), normal))
            grid.append(row)

        for i in range(segments):
            for j in range(tube_segments):
                a = grid[i][j]
                b = grid[i + 1][j]
                c = grid[i + 1][j + 1]
                d = grid[i][j + 1]
                self.add_face(material, a, b, c)
                self.add_face(material, a, c, d)

    def add_front_triangle(
        self,
        material: str,
        center: tuple[float, float, float],
        width: float,
        height: float,
    ) -> None:
        normal = (0.0, 0.12, 1.0)
        p0 = (center[0], center[1] + height * 0.55, center[2])
        p1 = (center[0] - width * 0.50, center[1] - height * 0.45, center[2])
        p2 = (center[0] + width * 0.50, center[1] - height * 0.45, center[2])
        a = self.add_vertex(p0, (0.5, 0.0), normal)
        b = self.add_vertex(p1, (0.0, 1.0), normal)
        c = self.add_vertex(p2, (1.0, 1.0), normal)
        self.add_face(material, a, b, c)

    def add_spark(self, material: str, center: tuple[float, float, float], radius: float) -> None:
        normal = (0.0, 0.2, 1.0)
        for i in range(8):
            a0 = 2.0 * math.pi * (i / 8)
            a1 = 2.0 * math.pi * ((i + 0.5) / 8)
            inner = radius * 0.35
            p0 = center
            p1 = (center[0] + math.cos(a0) * inner, center[1] + math.sin(a0) * inner, center[2])
            p2 = (center[0] + math.cos(a1) * radius, center[1] + math.sin(a1) * radius, center[2] + 0.015)
            a = self.add_vertex(p0, (0.5, 0.5), normal)
            b = self.add_vertex(p1, (0.0, 1.0), normal)
            c = self.add_vertex(p2, (1.0, 0.0), normal)
            self.add_face(material, a, b, c)

    def write_obj(self, path: Path) -> None:
        with path.open("w", encoding="utf-8", newline="\n") as f:
            f.write("mtllib bomb_slime.mtl\n")
            f.write("o BombSlime\n")
            for v in self.vertices:
                f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
            for uv in self.uvs:
                f.write(f"vt {uv[0]:.6f} {uv[1]:.6f}\n")
            for n in self.normals:
                f.write(f"vn {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}\n")
            f.write("s 1\n")
            for material, faces in self.faces.items():
                f.write(f"usemtl {material}\n")
                for a, b, c in faces:
                    f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


def make_texture(name: str, top: tuple[int, int, int], bottom: tuple[int, int, int], size: int = 64) -> None:
    img = Image.new("RGBA", (size, size))
    pix = img.load()
    for y in range(size):
        t = y / max(size - 1, 1)
        for x in range(size):
            vignette = 1.0 - 0.15 * math.hypot((x / (size - 1)) - 0.5, (y / (size - 1)) - 0.35)
            color = tuple(
                max(0, min(255, int((top[i] * (1.0 - t) + bottom[i] * t) * vignette)))
                for i in range(3)
            )
            pix[x, y] = (*color, 255)
    img.save(OUT_DIR / name)


def make_solid_texture(name: str, color: tuple[int, int, int, int], size: int = 16) -> None:
    Image.new("RGBA", (size, size), color).save(OUT_DIR / name)


def write_materials() -> None:
    materials = {
        "Body": ("bomb_slime_body.png", (0.0, 0.75, 0.95), 180.0),
        "Eye": ("bomb_slime_eye.png", (0.02, 0.03, 0.18), 220.0),
        "EyeHighlight": ("bomb_slime_eye_highlight.png", (1.0, 1.0, 1.0), 260.0),
        "Bomb": ("bomb_slime_bomb.png", (0.02, 0.02, 0.025), 90.0),
        "BombCap": ("bomb_slime_metal.png", (0.42, 0.43, 0.45), 120.0),
        "Fuse": ("bomb_slime_fuse.png", (0.70, 0.48, 0.23), 40.0),
        "Spark": ("bomb_slime_spark.png", (1.0, 0.68, 0.05), 80.0),
        "Strap": ("bomb_slime_strap.png", (0.95, 0.24, 0.12), 70.0),
        "Glove": ("bomb_slime_glove.png", (1.0, 0.62, 0.16), 120.0),
        "Warning": ("bomb_slime_warning.png", (1.0, 0.86, 0.12), 70.0),
        "Blush": ("bomb_slime_blush.png", (1.0, 0.35, 0.45), 60.0),
    }
    with (OUT_DIR / "bomb_slime.mtl").open("w", encoding="utf-8", newline="\n") as f:
        for material, (texture, kd, ns) in materials.items():
            f.write(f"newmtl {material}\n")
            f.write(f"Ns {ns:.6f}\n")
            f.write("Ka 1.000000 1.000000 1.000000\n")
            f.write(f"Kd {kd[0]:.6f} {kd[1]:.6f} {kd[2]:.6f}\n")
            f.write("Ks 0.250000 0.250000 0.250000\n")
            f.write("Ke 0.000000 0.000000 0.000000\n")
            f.write("Ni 1.450000\n")
            f.write("d 1.000000\n")
            f.write("illum 2\n")
            f.write(f"map_Kd {texture}\n\n")


def build_model() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    make_texture("bomb_slime_body.png", (24, 202, 236), (0, 58, 136), 96)
    make_texture("bomb_slime_bomb.png", (44, 45, 52), (5, 5, 8), 64)
    make_texture("bomb_slime_eye.png", (15, 42, 116), (1, 4, 38), 32)
    make_solid_texture("bomb_slime_eye_highlight.png", (255, 255, 255, 255))
    make_solid_texture("bomb_slime_metal.png", (125, 127, 132, 255))
    make_solid_texture("bomb_slime_fuse.png", (165, 108, 44, 255))
    make_solid_texture("bomb_slime_spark.png", (255, 179, 24, 255))
    make_solid_texture("bomb_slime_strap.png", (224, 59, 32, 255))
    make_solid_texture("bomb_slime_glove.png", (255, 149, 38, 255))
    make_solid_texture("bomb_slime_warning.png", (255, 219, 35, 255))
    make_solid_texture("bomb_slime_blush.png", (255, 94, 118, 255))
    write_materials()

    obj = ObjBuilder()

    obj.add_uv_sphere("Body", (0.0, 0.58, 0.0), (1.15, 0.56, 0.88), 56, 24)
    obj.add_uv_sphere("Bomb", (0.0, 0.95, -0.04), (0.82, 0.30, 0.56), 48, 14, 0.0, math.pi * 0.62)
    obj.add_elliptic_torus("BombCap", (0.0, 0.87, -0.04), 0.84, 0.57, 0.035, 56, 8)
    obj.add_elliptic_torus("Strap", (0.0, 0.50, 0.0), 1.07, 0.81, 0.050, 64, 8)
    obj.add_elliptic_torus("Strap", (0.0, 0.36, 0.0), 0.82, 0.66, 0.032, 56, 8)

    obj.add_uv_sphere("Eye", (-0.34, 0.66, 0.80), (0.105, 0.120, 0.045), 18, 10)
    obj.add_uv_sphere("Eye", (0.34, 0.66, 0.80), (0.105, 0.120, 0.045), 18, 10)
    obj.add_uv_sphere("EyeHighlight", (-0.375, 0.71, 0.835), (0.026, 0.030, 0.012), 12, 6)
    obj.add_uv_sphere("EyeHighlight", (0.305, 0.71, 0.835), (0.026, 0.030, 0.012), 12, 6)
    obj.add_uv_sphere("Blush", (-0.55, 0.50, 0.80), (0.085, 0.035, 0.018), 12, 6)
    obj.add_uv_sphere("Blush", (0.55, 0.50, 0.80), (0.085, 0.035, 0.018), 12, 6)
    obj.add_front_triangle("Warning", (0.0, 0.36, 0.898), 0.34, 0.29)

    obj.add_uv_sphere("Glove", (-0.88, 0.47, 0.38), (0.18, 0.13, 0.15), 18, 10)
    obj.add_uv_sphere("Glove", (0.88, 0.47, 0.38), (0.18, 0.13, 0.15), 18, 10)

    obj.add_cylinder_between("BombCap", (0.0, 1.19, 0.00), (0.0, 1.30, 0.03), 0.078, 16)
    fuse_points = [
        (0.0, 1.29, 0.03),
        (0.07, 1.39, 0.09),
        (0.18, 1.45, 0.07),
        (0.26, 1.52, 0.13),
    ]
    for start, end in zip(fuse_points, fuse_points[1:]):
        obj.add_cylinder_between("Fuse", start, end, 0.026, 10)
    obj.add_spark("Spark", (0.30, 1.56, 0.16), 0.13)

    obj.add_uv_sphere("EyeHighlight", (-0.28, 1.03, 0.36), (0.12, 0.032, 0.018), 14, 6)
    obj.add_uv_sphere("EyeHighlight", (0.28, 1.03, 0.36), (0.12, 0.032, 0.018), 14, 6)
    obj.write_obj(OUT_DIR / "bomb_slime.obj")


if __name__ == "__main__":
    build_model()
