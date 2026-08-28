"""リングバーナー用の専用モデルアセットを生成します。

固定砲台として一目で読める四脚、上に乗れる平らな天板、発射元になる
発光コアをOBJ/MTLとテクスチャへ決定的に出力します。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw


PROJECT_ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = PROJECT_ROOT / "Resources" / "3DModel" / "Characters" / "ring_burner"
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

    def vertex(self, position: Vec3, uv: Vec2, normal: Vec3) -> int:
        self.vertices.append(position)
        self.uvs.append(uv)
        self.normals.append(normalize(normal))
        return len(self.vertices)

    def face(self, material: str, a: int, b: int, c: int, smooth: bool = True) -> None:
        pa, pb, pc = self.vertices[a - 1], self.vertices[b - 1], self.vertices[c - 1]
        area = cross(sub(pb, pa), sub(pc, pa))
        if dot(area, area) <= 1.0e-12:
            return
        self.faces.setdefault(material, []).append((a, b, c))
        if smooth:
            self.smooth_materials.add(material)

    def quad(self, material: str, points: tuple[Vec3, Vec3, Vec3, Vec3], normal: Vec3) -> None:
        indices = [
            self.vertex(points[0], (0.0, 1.0), normal),
            self.vertex(points[1], (1.0, 1.0), normal),
            self.vertex(points[2], (1.0, 0.0), normal),
            self.vertex(points[3], (0.0, 0.0), normal),
        ]
        self.face(material, indices[0], indices[1], indices[2], False)
        self.face(material, indices[0], indices[2], indices[3], False)

    def box(self, material: str, center: Vec3, half: Vec3) -> None:
        x0, x1 = center[0] - half[0], center[0] + half[0]
        y0, y1 = center[1] - half[1], center[1] + half[1]
        z0, z1 = center[2] - half[2], center[2] + half[2]
        self.quad(material, ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)), (0.0, 0.0, 1.0))
        self.quad(material, ((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)), (0.0, 0.0, -1.0))
        self.quad(material, ((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)), (1.0, 0.0, 0.0))
        self.quad(material, ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)), (-1.0, 0.0, 0.0))
        self.quad(material, ((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)), (0.0, 1.0, 0.0))
        self.quad(material, ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)), (0.0, -1.0, 0.0))

    def cylinder(self, material: str, center: Vec3, radius: float, height: float, segments: int = 40) -> None:
        bottom_y = center[1] - height * 0.5
        top_y = center[1] + height * 0.5
        bottom_center = self.vertex((center[0], bottom_y, center[2]), (0.5, 0.5), (0.0, -1.0, 0.0))
        top_center = self.vertex((center[0], top_y, center[2]), (0.5, 0.5), (0.0, 1.0, 0.0))
        side_bottom: list[int] = []
        side_top: list[int] = []
        cap_bottom: list[int] = []
        cap_top: list[int] = []
        for segment in range(segments):
            angle = math.tau * segment / segments
            radial = (math.cos(angle), 0.0, math.sin(angle))
            x = center[0] + radial[0] * radius
            z = center[2] + radial[2] * radius
            side_bottom.append(self.vertex((x, bottom_y, z), (segment / segments, 1.0), radial))
            side_top.append(self.vertex((x, top_y, z), (segment / segments, 0.0), radial))
            cap_bottom.append(self.vertex((x, bottom_y, z), (0.5 + radial[0] * 0.5, 0.5 + radial[2] * 0.5), (0.0, -1.0, 0.0)))
            cap_top.append(self.vertex((x, top_y, z), (0.5 + radial[0] * 0.5, 0.5 - radial[2] * 0.5), (0.0, 1.0, 0.0)))
        for segment in range(segments):
            nxt = (segment + 1) % segments
            self.face(material, side_bottom[segment], side_bottom[nxt], side_top[nxt])
            self.face(material, side_bottom[segment], side_top[nxt], side_top[segment])
            self.face(material, bottom_center, cap_bottom[nxt], cap_bottom[segment], False)
            self.face(material, top_center, cap_top[segment], cap_top[nxt], False)

    def torus(self, material: str, center: Vec3, major: float, minor: float, segments: int = 48, sides: int = 10) -> None:
        grid: list[list[int]] = []
        for segment in range(segments + 1):
            theta = math.tau * segment / segments
            radial = (math.cos(theta), 0.0, math.sin(theta))
            row: list[int] = []
            for side in range(sides + 1):
                phi = math.tau * side / sides
                normal = normalize((radial[0] * math.cos(phi), math.sin(phi), radial[2] * math.cos(phi)))
                ring_center = (center[0] + radial[0] * major, center[1], center[2] + radial[2] * major)
                position = add(ring_center, mul(normal, minor))
                row.append(self.vertex(position, (segment / segments, side / sides), normal))
            grid.append(row)
        for segment in range(segments):
            for side in range(sides):
                a, b = grid[segment][side], grid[segment + 1][side]
                c, d = grid[segment + 1][side + 1], grid[segment][side + 1]
                self.face(material, a, b, c)
                self.face(material, a, c, d)

    def sphere(self, material: str, center: Vec3, scale: Vec3, segments: int = 28, rings: int = 14) -> None:
        grid: list[list[int]] = []
        for ring in range(rings + 1):
            theta = math.pi * ring / rings
            row: list[int] = []
            for segment in range(segments + 1):
                phi = math.tau * segment / segments
                local = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
                position = (center[0] + local[0] * scale[0], center[1] + local[1] * scale[1], center[2] + local[2] * scale[2])
                normal = normalize((local[0] / scale[0], local[1] / scale[1], local[2] / scale[2]))
                row.append(self.vertex(position, (segment / segments, ring / rings), normal))
            grid.append(row)
        for ring in range(rings):
            for segment in range(segments):
                a, b = grid[ring][segment], grid[ring + 1][segment]
                c, d = grid[ring + 1][segment + 1], grid[ring][segment + 1]
                self.face(material, a, b, c)
                self.face(material, a, c, d)

    def write(self, path: Path) -> None:
        with path.open("w", encoding="utf-8", newline="\n") as file:
            file.write("# CG2 Ring Burner\nmtllib ring_burner.mtl\no RingBurner\n")
            for value in self.vertices:
                file.write(f"v {value[0]:.7f} {value[1]:.7f} {value[2]:.7f}\n")
            for value in self.uvs:
                file.write(f"vt {value[0]:.7f} {value[1]:.7f}\n")
            for value in self.normals:
                file.write(f"vn {value[0]:.7f} {value[1]:.7f} {value[2]:.7f}\n")
            for material, faces in self.faces.items():
                file.write(f"usemtl {material}\n")
                file.write("s 1\n" if material in self.smooth_materials else "s off\n")
                for a, b, c in faces:
                    file.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


MATERIALS = {
    "BurnerMetal": ((0.055, 0.072, 0.105), (0.13, 0.18, 0.25), (0.72, 0.82, 0.96), (0.0, 0.0, 0.0), 150.0, "ring_burner_metal.png", (31, 46, 65)),
    "HeatGuard": ((0.22, 0.045, 0.012), (0.86, 0.16, 0.025), (1.0, 0.44, 0.08), (0.18, 0.025, 0.0), 120.0, "ring_burner_guard.png", (225, 56, 17)),
    "SafeTop": ((0.06, 0.16, 0.20), (0.12, 0.45, 0.52), (0.68, 0.98, 1.0), (0.01, 0.08, 0.10), 175.0, "ring_burner_top.png", (48, 168, 184)),
    "PlasmaCore": ((0.72, 0.12, 0.006), (1.0, 0.52, 0.03), (1.0, 0.88, 0.28), (1.0, 0.23, 0.005), 220.0, "ring_burner_core.png", (255, 139, 22)),
}


def build_model() -> ObjBuilder:
    model = ObjBuilder()
    # 四脚と低い台座により、向きに依存しない固定砲台として読ませます。
    for x in (-0.92, 0.92):
        for z in (-0.92, 0.92):
            model.box("BurnerMetal", (x, 0.25, z), (0.31, 0.25, 0.31))
            model.box("HeatGuard", (x * 0.90, 0.50, z * 0.90), (0.25, 0.13, 0.25))
    model.cylinder("BurnerMetal", (0.0, 0.55, 0.0), 1.24, 0.70)
    model.torus("HeatGuard", (0.0, 0.88, 0.0), 1.06, 0.16)
    model.cylinder("SafeTop", (0.0, 1.02, 0.0), 0.96, 0.20)
    model.torus("SafeTop", (0.0, 1.14, 0.0), 0.78, 0.09)
    model.sphere("PlasmaCore", (0.0, 1.16, 0.0), (0.46, 0.30, 0.46))
    # コア周囲の四つの発光ノッチは矢印ではなく、全方向発射を伝えます。
    model.box("PlasmaCore", (0.0, 0.88, 1.16), (0.18, 0.10, 0.12))
    model.box("PlasmaCore", (0.0, 0.88, -1.16), (0.18, 0.10, 0.12))
    model.box("PlasmaCore", (1.16, 0.88, 0.0), (0.12, 0.10, 0.18))
    model.box("PlasmaCore", (-1.16, 0.88, 0.0), (0.12, 0.10, 0.18))
    return model


def create_textures() -> list[Path]:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    paths: list[Path] = []
    palettes = {
        "ring_burner_metal.png": ((22, 31, 44), (58, 78, 103)),
        "ring_burner_guard.png": ((152, 31, 10), (255, 104, 20)),
        "ring_burner_top.png": ((25, 103, 119), (90, 218, 221)),
        "ring_burner_core.png": ((255, 78, 3), (255, 227, 69)),
    }
    size = 128
    for name, (dark, light) in palettes.items():
        image = Image.new("RGBA", (size, size), (*dark, 255))
        pixels = image.load()
        for y in range(size):
            for x in range(size):
                wave = 0.5 + 0.5 * math.sin(x * 0.18 + math.sin(y * 0.11) * 1.8)
                grid = 0.12 if (x % 32 < 2 or y % 32 < 2) else 0.0
                mix = min(1.0, 0.18 + wave * 0.55 + grid)
                pixels[x, y] = tuple(int(dark[i] + (light[i] - dark[i]) * mix) for i in range(3)) + (255,)
        path = OUT_DIR / name
        image.save(path)
        paths.append(path)
    return paths


def write_materials() -> Path:
    path = OUT_DIR / "ring_burner.mtl"
    with path.open("w", encoding="utf-8", newline="\n") as file:
        file.write("# CG2 Ring Burner material set\n\n")
        for name, (ka, kd, ks, ke, ns, texture, _) in MATERIALS.items():
            file.write(f"newmtl {name}\n")
            file.write(f"Ka {ka[0]:.6f} {ka[1]:.6f} {ka[2]:.6f}\n")
            file.write(f"Kd {kd[0]:.6f} {kd[1]:.6f} {kd[2]:.6f}\n")
            file.write(f"Ks {ks[0]:.6f} {ks[1]:.6f} {ks[2]:.6f}\n")
            file.write(f"Ke {ke[0]:.6f} {ke[1]:.6f} {ke[2]:.6f}\n")
            file.write(f"Ns {ns:.6f}\nNi 1.450000\nd 1.000000\nillum 2\nmap_Kd {texture}\n\n")
    return path


def convert_dds(png_paths: list[Path]) -> list[Path]:
    if not TEXCONV_PATH.is_file():
        print(f"Warning: Texconv.exe was not found: {TEXCONV_PATH}")
        return []
    result: list[Path] = []
    for png_path in png_paths:
        completed = subprocess.run(
            [str(TEXCONV_PATH), "-f", "BC7_UNORM_SRGB", "-y", "-m", "0", "-o", str(OUT_DIR), str(png_path)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if completed.returncode != 0:
            raise RuntimeError(f"DDS変換に失敗しました: {png_path.name}\n{completed.stdout}\n{completed.stderr}")
        dds_path = png_path.with_suffix(".dds")
        if not dds_path.is_file():
            raise RuntimeError(f"DDSが生成されていません: {dds_path}")
        result.append(dds_path)
    return result


def stable_guid(relative_path: str) -> str:
    return hashlib.md5(f"cg2:ring-burner:{relative_path}".encode("utf-8")).hexdigest()


def write_meta(asset: Path, asset_type: str, importer: str, settings: dict) -> None:
    relative = asset.relative_to(PROJECT_ROOT).as_posix()
    data = {
        "assetType": asset_type,
        "guid": stable_guid(relative),
        "importSettings": settings,
        "importer": importer,
        "source": relative,
        "version": 1,
    }
    Path(str(asset) + ".meta").write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def validate(model: ObjBuilder, obj_path: Path, textures: list[Path]) -> tuple[Vec3, Vec3]:
    if set(model.faces) != set(MATERIALS):
        raise RuntimeError(f"マテリアル構成が不正です: {set(model.faces)}")
    for path in textures:
        if path.suffix.lower() == ".dds" and path.read_bytes()[:4] != b"DDS ":
            raise RuntimeError(f"DDSヘッダーが不正です: {path}")
    bounds_min = tuple(min(value[axis] for value in model.vertices) for axis in range(3))
    bounds_max = tuple(max(value[axis] for value in model.vertices) for axis in range(3))
    if bounds_min[1] < -1.0e-4 or bounds_max[1] < 1.4:
        raise RuntimeError(f"モデル境界が不正です: min={bounds_min}, max={bounds_max}")
    if not obj_path.is_file() or obj_path.stat().st_size == 0:
        raise RuntimeError("OBJが生成されていません。")
    return bounds_min, bounds_max


def render_preview(model: ObjBuilder, output: Path) -> None:
    width, height = 760, 600
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image, "RGBA")
    camera, target = (4.2, 3.0, 5.2), (0.0, 0.75, 0.0)
    forward = normalize(sub(target, camera))
    right = normalize(cross(forward, (0.0, 1.0, 0.0)))
    up = normalize(cross(right, forward))
    light = normalize((-0.45, 0.82, 0.38))
    projected: list[tuple[float, float, float]] = []
    for vertex in model.vertices:
        relative = sub(vertex, camera)
        depth = dot(relative, forward)
        factor = 660.0 / max(depth, 0.2)
        projected.append((width * 0.5 + dot(relative, right) * factor, height * 0.57 - dot(relative, up) * factor, depth))
    draw.ellipse((125, 450, 635, 548), fill=(6, 15, 24, 70))
    triangles: list[tuple[float, str, tuple[int, int, int], float]] = []
    for material, faces in model.faces.items():
        for face in faces:
            normal = normalize(tuple(sum(model.normals[index - 1][axis] for index in face) for axis in range(3)))
            brightness = 1.0 if material == "PlasmaCore" else 0.42 + 0.58 * max(0.0, dot(normal, light))
            depth = sum(projected[index - 1][2] for index in face) / 3.0
            triangles.append((depth, material, face, brightness))
    triangles.sort(key=lambda item: item[0], reverse=True)
    for _, material, face, brightness in triangles:
        base = MATERIALS[material][6]
        color = tuple(max(0, min(255, int(channel * brightness))) for channel in base) + (255,)
        draw.polygon([(projected[index - 1][0], projected[index - 1][1]) for index in face], fill=color)
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(description="リングバーナーのモデルアセットを生成します。")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    png_paths = create_textures()
    material_path = write_materials()
    model = build_model()
    obj_path = OUT_DIR / "ring_burner.obj"
    model.write(obj_path)
    dds_paths = convert_dds(png_paths)
    write_meta(obj_path, "Model", "ModelImporter", {"generateTangents": True, "scale": 1.0})
    write_meta(material_path, "Binary", "BinaryImporter", {})
    for texture in png_paths + dds_paths:
        write_meta(texture, "Texture", "TextureImporter", {"colorSpace": "Auto", "generateMipmaps": True})
    bounds_min, bounds_max = validate(model, obj_path, dds_paths)
    if args.preview:
        render_preview(model, args.preview.resolve())
    print(f"Generated: {obj_path}")
    print(f"Vertices: {len(model.vertices)}")
    print(f"Triangles: {sum(len(faces) for faces in model.faces.values())}")
    print(f"Bounds: min={bounds_min}, max={bounds_max}")
    if args.preview:
        print(f"Preview: {args.preview.resolve()}")


if __name__ == "__main__":
    main()
