"""現在の闘技場OBJの形状を保持し、石板の目地と実寸基準UVを整えます。"""
from pathlib import Path
import argparse
import math
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_stage3_high_crown import ObjBuilder, cross, normalize, sub

def refine(source: Path, output: Path):
    lines = source.read_text(encoding="utf8").splitlines()
    vertices = [list(map(float, line.split()[1:4])) for line in lines if line.startswith("v ")]
    uvs = [list(map(float, line.split()[1:3])) for line in lines if line.startswith("vt ")]
    material = ""
    pending = []
    for line in lines:
        if line.startswith("usemtl "):
            material = line.split()[1]
            pending = []
        if not line.startswith("f "):
            continue
        face = [tuple(int(value) - 1 for value in corner.split("/")) for corner in line.split()[1:]]
        if material not in ("PavingA", "PavingB", "Grass", "GrassLight"):
            continue
        for vertex, uv, _ in face:
            x, _, z = vertices[vertex]
            size = 12.0 if material.startswith("Paving") else 4.0
            uvs[uv] = [x / size, z / size]
        if material.startswith("Paving"):
            pending.append(face)
            if len(pending) == 2:
                ids = sorted({corner[0] for triangle in pending for corner in triangle})
                if len(ids) == 4 and max(vertices[i][1] for i in ids) - min(vertices[i][1] for i in ids) < 0.001:
                    # 大きな坂や動線の面は保ち、小さな石板だけに幅のある継ぎ目を作る。
                    span = max(vertices[i][0] for i in ids) - min(vertices[i][0] for i in ids)
                    if span < 5:
                        cx = sum(vertices[i][0] for i in ids) / 4
                        cz = sum(vertices[i][2] for i in ids) / 4
                        for i in ids:
                            point = vertices[i]
                            length = math.hypot(point[0] - cx, point[2] - cz)
                            factor = max(0, 1 - 0.032 / max(length, 0.001))
                            point[0] = cx + (point[0] - cx) * factor
                            point[2] = cz + (point[2] - cz) * factor
                pending = []
    vi = ti = 0
    for i, line in enumerate(lines):
        if line.startswith("v "):
            lines[i] = "v " + " ".join(f"{value:.7f}" for value in vertices[vi])
            vi += 1
        elif line.startswith("vt "):
            lines[i] = "vt " + " ".join(f"{value:.7f}" for value in uvs[ti])
            ti += 1
    output.write_text("\n".join(lines) + "\n", encoding="utf8")


def beveled_slab(model, points, surface, sides, gap=.065, bevel=.10, depth=.028):
    # 床の高さは維持し、石板の縁を下へ落として目地を見せる。
    cx = sum(point[0] for point in points) / 4
    cz = sum(point[2] for point in points) / 4

    def ring(inset, drop):
        result = []
        for x, y, z in points:
            span = math.hypot(x - cx, z - cz)
            rate = (span - min(inset, span * .45)) / max(span, .001)
            result.append((cx + (x - cx) * rate, y - drop, cz + (z - cz) * rate))
        return result

    lower = ring(gap, depth)
    upper = ring(gap + bevel, 0)
    for index in range(4):
        nxt = (index + 1) % 4
        quad = (lower[index], lower[nxt], upper[nxt], upper[index])
        normal = normalize(cross(sub(quad[1], quad[0]), sub(quad[2], quad[0])))
        if normal[1] < 0:
            quad = tuple(reversed(quad))
            normal = tuple(-value for value in normal)
        model.quad(sides, quad, normal)
    ids = [model.vertex(point, (point[0] / 12, point[2] / 12), (0, 1, 0)) for point in upper]
    model.face(surface, ids[0], ids[1], ids[2], False)
    model.face(surface, ids[0], ids[2], ids[3], False)


def add_grass_borders(path: Path, add_ornaments=None):
    model = ObjBuilder()
    # 暖色と寒色の大きな石板を使い、隣接する列の目地をずらす。
    for inner_x, inner_z, outer_x, outer_z, height, rows, tiles in (
        (12.05, 9.05, 29.52, 24.42, .755, 5, 46),
        (33.23, 28.03, 38.65, 32.55, .721, 2, 56),
    ):
        for row in range(rows):
            rx0 = inner_x + (outer_x - inner_x) * row / rows
            rz0 = inner_z + (outer_z - inner_z) * row / rows
            rx1 = inner_x + (outer_x - inner_x) * (row + 1) / rows
            rz1 = inner_z + (outer_z - inner_z) * (row + 1) / rows
            for tile in range(tiles):
                angles = [math.tau * (index + (row % 2) * .5) / tiles for index in (tile, tile + 1)]
                a, b = angles
                points = [(math.cos(angle) * rx, height, math.sin(angle) * rz)
                    for angle, rx, rz in ((a, rx0, rz0), (b, rx0, rz0), (b, rx1, rz1), (a, rx1, rz1))]
                material = "ArenaSlabCool" if (tile + row * 3) % 7 in (0, 2) else "ArenaSlabIvory"
                beveled_slab(model, points, material, "ArenaSlabEdge")
    # 芝生の端は小さな石を連ね、数枚ごとに濃淡を付ける。
    for rx, rz, y, segments in ((11.85, 8.85, .94, 48), (29.70, 24.60, .85, 88), (33.08, 27.88, .83, 96)):
        for index in range(segments):
            a = math.tau * index / segments
            b = math.tau * (index + 1) / segments
            points = [(math.cos(angle) * (rx + width), y, math.sin(angle) * (rz + width))
                for angle, width in ((a, -.19), (b, -.19), (b, .19), (a, .19))]
            beveled_slab(model, points, "ArenaSlabIvory", "ArenaSlabEdge", .012, .045, .10)
    # 縁に寄せた草の束。両面に面を持たせ、カメラの向きで消えないようにする。
    for rx, rz, count in ((11.42, 8.42, 160), (29.99, 24.89, 200), (32.68, 27.48, 220)):
        for index in range(count):
            angle = math.tau * index / count
            spread = .10 * math.sin(index * 2.31)
            x, z = math.cos(angle) * (rx + spread), math.sin(angle) * (rz + spread)
            # 芝帯を横切る通路には草を置かない。
            if rx > 20 and (abs(x) < 5.35 or abs(z) < 3.85):
                continue
            for heading in (angle, angle + 1.5):
                dx, dz = math.cos(heading) * .10, math.sin(heading) * .10
                h = .94 + (index % 5) * .035
                points = ((x-dx,.78,z-dz),(x+dx,.78,z+dz),(x+dx*.30,h,z+dz*.30),(x-dx*.30,h,z-dz*.30))
                model.quad("ArenaGrassTip", points, (0, 1, 0))
                model.quad("ArenaGrassTip", tuple(reversed(points)), (0, 1, 0))
    if add_ornaments is not None:
        add_ornaments(model)
    text = path.read_text(encoding="utf8")
    nv = sum(line.startswith("v ") for line in text.splitlines())
    nt = sum(line.startswith("vt ") for line in text.splitlines())
    nn = sum(line.startswith("vn ") for line in text.splitlines())
    lines = ["v " + " ".join(f"{v:.7f}" for v in point) for point in model.vertices]
    lines += ["vt " + " ".join(f"{v:.7f}" for v in uv) for uv in model.uvs]
    lines += ["vn " + " ".join(f"{v:.7f}" for v in normal) for normal in model.normals]
    for material, faces in model.faces.items():
        lines.append("usemtl " + material)
        for face in faces:
            lines.append("f " + " ".join(f"{i+nv}/{i+nt}/{i+nn}" for i in face))
    path.write_text(text + "\n".join(lines) + "\n", encoding="utf8")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    refine(args.source, args.output)
    add_grass_borders(args.output)
