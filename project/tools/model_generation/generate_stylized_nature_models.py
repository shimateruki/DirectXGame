from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
STAGE_DIR = ROOT / "Resources" / "3DModel" / "Stages"


class ObjBuilder:
    def __init__(self, mtl_name: str, object_name: str) -> None:
        self.mtl_name = mtl_name
        self.object_name = object_name
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

    def vertex(self, position: tuple[float, float, float], uv: tuple[float, float], normal: tuple[float, float, float]) -> int:
        self.vertices.append(position)
        self.uvs.append(uv)
        self.normals.append(self.normalize(normal))
        return len(self.vertices)

    def face(self, material: str, a: int, b: int, c: int) -> None:
        self.faces.setdefault(material, []).append((a, b, c))

    def quad(self, material: str, points: list[tuple[float, float, float]], uv_rect: tuple[float, float, float, float]) -> None:
        u0, v0, u1, v1 = uv_rect
        normal = self.normalize(self.cross(self.sub(points[1], points[0]), self.sub(points[2], points[0])))
        ids = [
            self.vertex(points[0], (u0, v1), normal),
            self.vertex(points[1], (u1, v1), normal),
            self.vertex(points[2], (u1, v0), normal),
            self.vertex(points[3], (u0, v0), normal),
        ]
        self.face(material, ids[0], ids[1], ids[2])
        self.face(material, ids[0], ids[2], ids[3])
        self.face(material, ids[2], ids[1], ids[0])
        self.face(material, ids[3], ids[2], ids[0])

    def blade(self, material: str, center: tuple[float, float, float], angle: float, width: float, height: float, bend: float) -> None:
        right = (math.cos(angle) * width * 0.5, 0.0, math.sin(angle) * width * 0.5)
        forward = (-math.sin(angle), 0.0, math.cos(angle))
        base_l = (center[0] - right[0], center[1], center[2] - right[2])
        base_r = (center[0] + right[0], center[1], center[2] + right[2])
        mid = (
            center[0] + forward[0] * bend * 0.45,
            center[1] + height * 0.55,
            center[2] + forward[2] * bend * 0.45,
        )
        tip = (
            center[0] + forward[0] * bend,
            center[1] + height,
            center[2] + forward[2] * bend,
        )
        mid_l = (mid[0] - right[0] * 0.45, mid[1], mid[2] - right[2] * 0.45)
        mid_r = (mid[0] + right[0] * 0.45, mid[1], mid[2] + right[2] * 0.45)
        normal = self.normalize((forward[0] * 0.25, 0.32, forward[2] * 0.25))
        ids = [
            self.vertex(base_l, (0.0, 1.0), normal),
            self.vertex(base_r, (1.0, 1.0), normal),
            self.vertex(mid_r, (0.84, 0.45), normal),
            self.vertex(tip, (0.5, 0.0), normal),
            self.vertex(mid_l, (0.16, 0.45), normal),
        ]
        for tri in ((0, 1, 2), (0, 2, 4), (4, 2, 3)):
            self.face(material, ids[tri[0]], ids[tri[1]], ids[tri[2]])
            self.face(material, ids[tri[2]], ids[tri[1]], ids[tri[0]])

    def uv_sphere(self, material: str, center: tuple[float, float, float], scale: tuple[float, float, float], segments: int, rings: int) -> None:
        grid: list[list[int]] = []
        for i in range(rings + 1):
            theta = math.pi * i / rings
            row: list[int] = []
            for j in range(segments + 1):
                phi = math.tau * j / segments
                local = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
                pos = (
                    center[0] + local[0] * scale[0],
                    center[1] + local[1] * scale[1],
                    center[2] + local[2] * scale[2],
                )
                normal = (local[0] / scale[0], local[1] / scale[1], local[2] / scale[2])
                row.append(self.vertex(pos, (j / segments, i / rings), normal))
            grid.append(row)
        for i in range(rings):
            for j in range(segments):
                a, b, c, d = grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]
                self.face(material, a, b, c)
                self.face(material, a, c, d)

    def cylinder(self, material: str, radius_base: float, radius_top: float, height: float, segments: int) -> None:
        bottom: list[int] = []
        top: list[int] = []
        for i in range(segments + 1):
            angle = math.tau * i / segments
            n = self.normalize((math.cos(angle), 0.18, math.sin(angle)))
            bottom.append(self.vertex((math.cos(angle) * radius_base, 0.0, math.sin(angle) * radius_base), (i / segments, 1.0), n))
            top.append(self.vertex((math.cos(angle) * radius_top, height, math.sin(angle) * radius_top), (i / segments, 0.0), n))
        for i in range(segments):
            self.face(material, bottom[i], bottom[i + 1], top[i + 1])
            self.face(material, bottom[i], top[i + 1], top[i])

    def write(self, path: Path) -> None:
        with path.open("w", encoding="utf-8", newline="\n") as f:
            f.write(f"mtllib {self.mtl_name}\n")
            f.write(f"o {self.object_name}\n")
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


def make_gradient(path: Path, top: tuple[int, int, int], bottom: tuple[int, int, int], spots: bool = False) -> None:
    size = 96
    img = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(img, "RGBA")
    for y in range(size):
        t = y / (size - 1)
        color = tuple(int(top[i] * (1.0 - t) + bottom[i] * t) for i in range(3))
        draw.line((0, y, size, y), fill=(*color, 255))
    if spots:
        for i in range(18):
            x = (i * 37) % size
            y = (i * 53) % size
            r = 4 + (i % 4)
            draw.ellipse((x - r, y - r, x + r, y + r), fill=(255, 255, 185, 36))
    img.save(path)


def write_mtl(path: Path, entries: dict[str, tuple[str, tuple[float, float, float], float]]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as f:
        for name, (texture, kd, ns) in entries.items():
            f.write(f"newmtl {name}\n")
            f.write(f"Ns {ns:.3f}\n")
            f.write("Ka 1.000 1.000 1.000\n")
            f.write(f"Kd {kd[0]:.3f} {kd[1]:.3f} {kd[2]:.3f}\n")
            f.write("Ks 0.040 0.050 0.030\n")
            f.write("d 1.000\n")
            f.write("illum 2\n")
            f.write(f"map_Kd {texture}\n\n")


def build_soft_grass() -> None:
    out = STAGE_DIR / "soft_grass"
    out.mkdir(parents=True, exist_ok=True)
    make_gradient(out / "soft_grass_leaf.png", (160, 232, 88), (54, 150, 48), True)
    write_mtl(out / "soft_grass.mtl", {"SoftGrass": ("soft_grass_leaf.png", (0.42, 0.78, 0.22), 12.0)})
    obj = ObjBuilder("soft_grass.mtl", "soft_grass")
    for i in range(22):
        angle = math.tau * i / 22.0
        ring = 0.10 + 0.45 * ((i * 7) % 11) / 10.0
        center = (math.cos(angle) * ring, 0.0, math.sin(angle) * ring)
        height = 0.42 + 0.40 * ((i * 5) % 9) / 8.0
        width = 0.10 + 0.08 * ((i * 3) % 5) / 4.0
        bend = 0.08 + 0.22 * ((i * 11) % 7) / 6.0
        obj.blade("SoftGrass", center, angle + 0.4 * math.sin(i), width, height, bend)
    obj.write(out / "soft_grass.obj")


def build_soft_tree() -> None:
    out = STAGE_DIR / "soft_tree"
    out.mkdir(parents=True, exist_ok=True)
    make_gradient(out / "soft_tree_leaf.png", (145, 220, 82), (64, 136, 48), True)
    make_gradient(out / "soft_tree_trunk.png", (166, 112, 62), (92, 55, 32), False)
    write_mtl(out / "soft_tree.mtl", {
        "SoftTrunk": ("soft_tree_trunk.png", (0.60, 0.38, 0.20), 12.0),
        "SoftLeaf": ("soft_tree_leaf.png", (0.36, 0.72, 0.25), 16.0),
    })
    obj = ObjBuilder("soft_tree.mtl", "soft_tree")
    obj.cylinder("SoftTrunk", 0.24, 0.18, 1.18, 12)
    obj.uv_sphere("SoftLeaf", (0.0, 1.55, 0.0), (0.90, 0.58, 0.84), 24, 12)
    obj.uv_sphere("SoftLeaf", (-0.45, 1.34, 0.08), (0.56, 0.38, 0.52), 18, 10)
    obj.uv_sphere("SoftLeaf", (0.47, 1.36, -0.06), (0.58, 0.40, 0.54), 18, 10)
    obj.uv_sphere("SoftLeaf", (0.10, 1.88, -0.18), (0.62, 0.42, 0.58), 18, 10)
    obj.write(out / "soft_tree.obj")


def build_grass_patch() -> None:
    out = STAGE_DIR / "soft_grass_patch"
    out.mkdir(parents=True, exist_ok=True)
    make_gradient(out / "soft_grass_patch.png", (172, 232, 98), (82, 166, 58), True)
    write_mtl(out / "soft_grass_patch.mtl", {"SoftGrassPatch": ("soft_grass_patch.png", (0.45, 0.78, 0.26), 10.0)})
    obj = ObjBuilder("soft_grass_patch.mtl", "soft_grass_patch")
    y = 0.02
    size_x = 1.5
    size_z = 1.0
    obj.quad("SoftGrassPatch", [(-size_x, y, -size_z), (size_x, y, -size_z), (size_x, y, size_z), (-size_x, y, size_z)], (0.0, 0.0, 1.0, 1.0))
    for i in range(18):
        angle = math.tau * i / 18.0
        center = (math.cos(angle) * 0.60, 0.02, math.sin(angle) * 0.40)
        obj.blade("SoftGrassPatch", center, angle, 0.08, 0.25 + (i % 4) * 0.05, 0.08)
    obj.write(out / "soft_grass_patch.obj")


def main() -> None:
    build_soft_grass()
    build_soft_tree()
    build_grass_patch()
    print("generated stylized nature models")


if __name__ == "__main__":
    main()
