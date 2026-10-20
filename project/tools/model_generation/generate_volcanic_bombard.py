"""ステージ2向けの大型迫撃砲モデルを OBJ/MTL で生成する。

原点は台座の底、+Z は発射方向。砲口の中心は既存の
GimmickFireCannon の発射位置 (Y=2.73, Z=2.55) に合わせる。
"""

from __future__ import annotations

import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Resources/3DModel/Gimmicks/volcanic_bombard"
NAME = "volcanic_bombard"
TAU = math.tau


class Mesh:
    def __init__(self) -> None:
        self.vertices: list[tuple[float, float, float]] = []
        self.normals: list[tuple[float, float, float]] = []
        self.faces: list[tuple[str, tuple[int, int, int]]] = []

    def triangle(self, material: str, a, b, c) -> None:
        # 面ごとの法線で石と鋳物の輪郭を保ち、細かな装飾だけを丸く見せる。
        ux, uy, uz = (b[i] - a[i] for i in range(3))
        vx, vy, vz = (c[i] - a[i] for i in range(3))
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        if length < 1e-8:
            return
        normal = (nx / length, ny / length, nz / length)
        indexes = []
        for point in (a, b, c):
            self.vertices.append(tuple(point))
            self.normals.append(normal)
            indexes.append(len(self.vertices))
        self.faces.append((material, tuple(indexes)))

    def polygon(self, material: str, *points) -> None:
        for i in range(1, len(points) - 1):
            self.triangle(material, points[0], points[i], points[i + 1])

    def ring_y(self, profile, material: str, sides=16, cx=0.0, cz=0.0) -> None:
        # 台座や縦軸のリベットには Y 軸回転の断面を使う。
        for (ra, ya), (rb, yb) in zip(profile, profile[1:]):
            for i in range(sides):
                a = TAU * i / sides
                b = TAU * (i + 1) / sides
                pa = (cx + ra * math.cos(a), ya, cz + ra * math.sin(a))
                pb = (cx + ra * math.cos(b), ya, cz + ra * math.sin(b))
                qa = (cx + rb * math.cos(a), yb, cz + rb * math.sin(a))
                qb = (cx + rb * math.cos(b), yb, cz + rb * math.sin(b))
                self.polygon(material, pa, qa, qb, pb)

    def ring_z(self, profile, material: str, sides=24, cy=2.73) -> None:
        # 砲身を +Z に向け、正面に実際の空洞を残す。
        for (ra, za), (rb, zb) in zip(profile, profile[1:]):
            for i in range(sides):
                a = TAU * i / sides
                b = TAU * (i + 1) / sides
                pa = (ra * math.cos(a), cy + ra * math.sin(a), za)
                pb = (ra * math.cos(b), cy + ra * math.sin(b), za)
                qa = (rb * math.cos(a), cy + rb * math.sin(a), zb)
                qb = (rb * math.cos(b), cy + rb * math.sin(b), zb)
                self.polygon(material, pa, pb, qb, qa)

    def ring_x(self, profile, material: str, sides=16, cy=2.05, cz=-0.12) -> None:
        # 左右の軸受は砲身から独立した実体のある円盤にする。
        for (ra, xa), (rb, xb) in zip(profile, profile[1:]):
            for i in range(sides):
                a = TAU * i / sides
                b = TAU * (i + 1) / sides
                pa = (xa, cy + ra * math.cos(a), cz + ra * math.sin(a))
                pb = (xa, cy + ra * math.cos(b), cz + ra * math.sin(b))
                qa = (xb, cy + rb * math.cos(a), cz + rb * math.sin(a))
                qb = (xb, cy + rb * math.cos(b), cz + rb * math.sin(b))
                self.polygon(material, pa, pb, qb, qa)

    def box(self, center, size, material: str) -> None:
        cx, cy, cz = center
        hx, hy, hz = (v * 0.5 for v in size)
        p = [(cx + sx * hx, cy + sy * hy, cz + sz * hz)
             for sx, sy, sz in ((-1, -1, -1), (1, -1, -1), (1, 1, -1),
                                (-1, 1, -1), (-1, -1, 1), (1, -1, 1),
                                (1, 1, 1), (-1, 1, 1))]
        for ids in ((3, 2, 1, 0), (4, 5, 6, 7), (0, 1, 5, 4),
                    (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
            self.polygon(material, *(p[i] for i in ids))

    def beam(self, start, end, width, depth, material: str) -> None:
        # YZ 平面の斜材。砲身の重量を受ける支柱として左右に配置する。
        dx, dy, dz = (end[i] - start[i] for i in range(3))
        length = math.sqrt(dx * dx + dy * dy + dz * dz)
        ay, az = -dz / length * depth * 0.5, dy / length * depth * 0.5
        x0, x1 = start[0] - width * 0.5, start[0] + width * 0.5
        near = [(x0, start[1] + ay, start[2] + az),
                (x1, start[1] + ay, start[2] + az),
                (x1, start[1] - ay, start[2] - az),
                (x0, start[1] - ay, start[2] - az)]
        far = [(x, y + dy, z + dz) for x, y, z in near]
        self.polygon(material, *reversed(near))
        self.polygon(material, *far)
        for i in range(4):
            j = (i + 1) % 4
            self.polygon(material, near[i], near[j], far[j], far[i])

    def diamond(self, x, y, z, radius, material) -> None:
        # 小さな鋲は八面体にして暗い砲身でも陰影が残るようにする。
        v = ((x, y + radius, z), (x + radius, y, z), (x, y, z + radius),
             (x - radius, y, z), (x, y, z - radius), (x, y - radius, z))
        for ring in ((1, 2), (2, 3), (3, 4), (4, 1)):
            self.triangle(material, v[0], v[ring[0]], v[ring[1]])
            self.triangle(material, v[5], v[ring[1]], v[ring[0]])

    def save(self) -> None:
        OUT.mkdir(parents=True, exist_ok=True)
        with (OUT / f"{NAME}.obj").open("w", encoding="ascii", newline="\n") as stream:
            stream.write(f"mtllib {NAME}.mtl\no {NAME}\n")
            for x, y, z in self.vertices:
                stream.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
            for x, y, z in self.normals:
                stream.write(f"vn {x:.6f} {y:.6f} {z:.6f}\n")
            current = None
            for material, face in self.faces:
                if material != current:
                    stream.write(f"usemtl {material}\n")
                    current = material
                stream.write("f " + " ".join(f"{v}//{v}" for v in face) + "\n")
        (OUT / f"{NAME}.mtl").write_text(MATERIALS, encoding="ascii", newline="\n")


MATERIALS = """newmtl basalt
Ka 0.035 0.025 0.035
Kd 0.180 0.145 0.180
Ks 0.120 0.105 0.150
Ns 34
illum 2

newmtl basalt_edge
Ka 0.080 0.050 0.060
Kd 0.305 0.215 0.220
Ks 0.205 0.155 0.155
Ns 50
illum 2

newmtl forged_iron
Ka 0.045 0.045 0.055
Kd 0.250 0.270 0.310
Ks 0.580 0.600 0.650
Ns 118
illum 2

newmtl dark_iron
Ka 0.020 0.018 0.024
Kd 0.085 0.076 0.105
Ks 0.360 0.310 0.400
Ns 86
illum 2

newmtl copper
Ka 0.180 0.085 0.035
Kd 0.680 0.345 0.135
Ks 0.820 0.520 0.300
Ns 128
illum 2

newmtl copper_highlight
Ka 0.260 0.135 0.045
Kd 0.920 0.570 0.225
Ks 0.950 0.780 0.400
Ns 156
illum 2

newmtl furnace
Ka 0.060 0.018 0.010
Kd 0.155 0.055 0.035
Ks 0.100 0.055 0.045
Ns 20
illum 2

newmtl ember
Ka 0.760 0.190 0.015
Kd 1.000 0.420 0.055
Ks 0.880 0.570 0.120
Ns 110
illum 2
"""


def build() -> None:
    mesh = Mesh()

    # 八角形の石基礎と鋳鉄の回転台は、設置面のシルエットを広く取る。
    mesh.ring_y([(0.0, 0.0), (1.66, 0.0), (1.91, 0.13),
                 (1.82, 0.45), (1.53, 0.63)], "basalt", 12, cz=-0.35)
    mesh.ring_y([(1.85, 0.16), (1.92, 0.19), (1.87, 0.27)],
                "basalt_edge", 12, cz=-0.35)
    mesh.ring_y([(1.47, 0.55), (1.57, 0.64), (1.53, 0.77),
                 (1.28, 0.87)], "forged_iron", 16, cz=-0.35)
    mesh.ring_y([(1.52, 0.75), (1.55, 0.81), (1.48, 0.86)],
                "copper", 16, cz=-0.35)
    mesh.ring_y([(1.22, 0.82), (1.14, 1.01), (1.04, 1.30),
                 (0.0, 1.36)], "dark_iron", 16, cz=-0.35)

    # 両側の支柱、軸受、可動軸を独立したメッシュにして砲身を支える構造を見せる。
    for sign in (-1, 1):
        x = sign * 1.36
        mesh.box((x, 0.98, -0.36), (0.54, 0.32, 2.25), "forged_iron")
        mesh.beam((x, 1.02, -1.13), (x, 2.06, -0.22), 0.36, 0.40, "dark_iron")
        mesh.beam((x, 1.02, 0.49), (x, 2.06, -0.22), 0.36, 0.40, "dark_iron")
        if sign > 0:
            mesh.ring_x([(0.0, 1.18), (0.65, 1.18), (0.71, 1.33),
                         (0.61, 1.58), (0.0, 1.60)], "forged_iron")
            mesh.ring_x([(0.0, 1.57), (0.49, 1.57), (0.54, 1.62),
                         (0.0, 1.66)], "copper")
            mesh.ring_x([(0.0, 1.65), (0.25, 1.65), (0.0, 1.71)],
                        "dark_iron")
        else:
            mesh.ring_x([(0.0, -1.71), (0.25, -1.71), (0.0, -1.65)],
                        "dark_iron")
            mesh.ring_x([(0.0, -1.66), (0.54, -1.66), (0.49, -1.57),
                         (0.0, -1.57)], "copper")
            mesh.ring_x([(0.0, -1.60), (0.61, -1.60), (0.71, -1.33),
                         (0.65, -1.18), (0.0, -1.18)], "forged_iron")
        for z in (-1.28, 0.52):
            mesh.diamond(x, 1.18, z, 0.10, "copper_highlight")

    # 丸い後部から広い砲口へつながる独自の玄武岩製砲身。
    mesh.ring_z([(0.0, -1.83), (0.45, -1.83), (0.85, -1.68),
                 (1.07, -1.36), (1.11, -0.85), (1.06, 0.10),
                 (1.09, 1.43), (1.14, 2.08), (1.18, 2.21)],
                "basalt", 24)
    mesh.ring_z([(1.09, -1.42), (1.16, -1.27), (1.17, -1.17),
                 (1.12, -1.02)], "copper", 24)
    for start in (-0.42, 0.22, 1.24):
        mesh.ring_z([(1.07, start), (1.15, start + 0.07),
                     (1.16, start + 0.20), (1.08, start + 0.28)],
                    "forged_iron", 24)
        mesh.ring_z([(1.16, start + 0.09), (1.18, start + 0.14),
                     (1.16, start + 0.19)], "copper", 24)

    # 砲口を厚い二重金属輪と深い空洞で作り、正面からも大砲と読めるようにする。
    mesh.ring_z([(1.16, 2.07), (1.34, 2.18), (1.43, 2.30),
                 (1.43, 2.43), (1.34, 2.53), (1.06, 2.53)],
                "copper", 32)
    mesh.ring_z([(1.34, 2.52), (1.29, 2.57), (1.12, 2.57)],
                "copper_highlight", 32)
    # 内壁の順序を逆にし、前から穴の奥が見える向きに法線を置く。
    mesh.ring_z([(1.12, 2.57), (1.03, 2.38), (0.92, 1.86),
                 (0.83, 1.22), (0.72, 0.67)], "furnace", 32)
    mesh.ring_z([(0.72, 0.68), (0.64, 0.65), (0.50, 0.62),
                 (0.0, 0.62)], "ember", 32)
    mesh.ring_z([(1.00, 1.97), (0.96, 1.86), (0.94, 1.82)],
                "ember", 32)
    mesh.ring_z([(1.41, 2.31), (1.45, 2.34), (1.43, 2.39)],
                "copper_highlight", 32)

    # 砲口の留め具と背面の放熱フィンは遊ぶ距離でも面で見える大きさにする。
    for i in range(12):
        angle = TAU * i / 12
        mesh.diamond(1.37 * math.cos(angle),
                     2.73 + 1.37 * math.sin(angle), 2.49,
                     0.10, "dark_iron")
    for x in (-0.68, -0.23, 0.23, 0.68):
        mesh.box((x, 3.72, -1.00), (0.15, 0.21, 0.50), "copper")

    # 左右の大きな三角紋章で向きと危険性を知らせる。
    for sign in (-1, 1):
        x = sign * 1.69
        mesh.polygon("ember", (x, 2.16, -0.38),
                     (x, 1.91, -0.54), (x, 1.91, -0.22))

    mesh.save()
    print(f"{OUT / (NAME + '.obj')}  triangles={len(mesh.faces)}")


if __name__ == "__main__":
    build()
