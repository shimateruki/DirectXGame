"""ステージ3の通路や玉座、外周の装飾を生成します。"""
from pathlib import Path
import json
import math
import re

from generate_stage3_high_crown import ObjBuilder, STAGE_DIR, add_vertical_crystal, write_meta, write_obj
from refine_stage3_stone_surface import refine, add_grass_borders, beveled_slab

STONE = "ArenaSlabIvory"
EDGE = "ArenaSlabEdge"
WALL = "ArenaCastleBlue"
GOLD = "ArenaGold"
CLOTH = "ArenaBanner"
ROOT = Path(__file__).resolve().parents[2]


def build_throne():
    model = ObjBuilder()
    # 玉座全体を既存の台座の占有範囲内に収める。
    model.box(WALL, (0, .35, 0), (5.6, .35, 2.75))
    model.box(EDGE, (0, .78, 0), (5.48, .09, 2.64))
    model.box(STONE, (0, 1.01, -.25), (5.05, .17, 2.39))
    model.box(STONE, (0, 1.25, -.72), (4.55, .14, 1.88))
    # 石柱の間に紫の背板を収め、金は紋章と柱頭に使う。
    model.box(STONE, (0, 2.03, .55), (2.14, .49, 1.28))
    model.box(CLOTH, (0, 2.48, .49), (1.82, .13, 1.12))
    model.box(EDGE, (0, 4.32, -.48), (1.98, 1.76, .34))
    model.box(CLOTH, (0, 4.25, -.10), (1.53, 1.42, .12))
    model.box(GOLD, (0, 5.83, -.04), (1.60, .065, .12))
    for x in (-1.97, 1.97):
        model.box(STONE, (x, 4.18, -.37), (.31, 1.83, .56))
        model.box(EDGE, (x, 2.78, .69), (.41, .19, 1.08))
        model.box(STONE, (x, 6.02, -.38), (.43, .16, .59))
        add_vertical_crystal(model, GOLD, (x, 6.50, -.37), .20, .76, 5)
    model.box(GOLD, (0, 3.97, .05), (.59, .12, .10))
    for x, height in ((-.48, .26), (0, .47), (.48, .26)):
        model.box(GOLD, (x, 4.12 + height * .5, .05), (.11, height * .5, .10))
    return model


def build_arch(opening):
    model = ObjBuilder()
    # 開口と半円の石輪を同じ奥行きで作り、手前から空が見える構造にする。
    if opening:
        model.box(WALL, (-2.65, 3.8, 0), (.49, 3.8, .70))
        model.box(WALL, (2.65, 3.8, 0), (.49, 3.8, .70))
        inner, outer, spring = 2.16, 2.57, 3.05
        for i in range(16):
            a, b = (math.pi * k / 16 for k in (i, i + 1))
            def point(angle, radius, z):
                return (radius * math.cos(angle), spring + radius * math.sin(angle), z)
            for z in (-.74, .74):
                quad = (point(a, inner, z), point(b, inner, z),
                        point(b, outer, z), point(a, outer, z))
                model.quad(EDGE if i % 3 else STONE, quad if z < 0 else tuple(reversed(quad)), (0, 0, math.copysign(1, z)))
                fill = (point(a, outer, z), point(b, outer, z),
                        (point(b, outer, z)[0], 7.65, z), (point(a, outer, z)[0], 7.65, z))
                model.quad(WALL, fill if z < 0 else tuple(reversed(fill)), (0, 0, math.copysign(1, z)))
            model.quad(EDGE, (point(b, inner, -.74), point(a, inner, -.74),
                point(a, inner, .74), point(b, inner, .74)), (0, -1, 0))
    else:
        model.box(WALL, (0, 3.85, 0), (3.14, 3.85, .70))
        for z in (-.75, .75):
            model.box(GOLD, (0, 7.05, z), (1.01, .07, .09))
            model.box(CLOTH, (0, 5.47, z), (.74, 1.54, .048))
            sign = math.copysign(1, z)
            model.box(GOLD, (0, 5.32, z + sign * .06), (.31, .10, .026))
            for x, y in ((-.26, 5.56), (0, 5.68), (.26, 5.56)):
                model.box(GOLD, (x, y, z + sign * .06), (.065, .21, .026))
    # 下端の石帯を各区画でつなぎ、アーチの開口を城壁の窓として見せる。
    model.box(WALL, (0, .41, 0), (3.14, .41, .72))
    model.box(EDGE, (0, .85, 0), (3.14, .10, .79))
    model.box(WALL, (0, 6.80, 0), (3.14, .07, .745))
    model.box(EDGE, (0, 7.71, 0), (3.14, .14, .80))
    model.box(STONE, (0, 8.02, 0), (3.14, .18, .84))
    for x in (-2.52, -1.26, 0, 1.26, 2.52):
        model.box(STONE, (x, 8.55, 0), (.35, .36, .80))
    return model


def build_parapet():
    model = ObjBuilder()
    model.box(WALL, (0, .44, 0), (3.0, .44, .70))
    model.box(EDGE, (0, .13, 0), (3.10, .13, .76))
    for x in (-2.60, 2.60):
        model.box(EDGE, (x, .52, 0), (.25, .44, .78))
    # 笠石の上面までを従来の接触高さに合わせる。
    beveled_slab(model, [(-3.14, 1.11, -.82), (-3.14, 1.11, .82),
        (3.14, 1.11, .82), (3.14, 1.11, -.82)], STONE, EDGE, 0, .12, .22)
    return model


def build_ornaments(model):
    # 既存の通路ごとの上面に浅い面取りを載せ、床の衝突は既存のまま使う。
    for cx, cz, hx, hz, columns, rows in (
        (0, 22.0, 5.2, 11.2, 3, 6), (0, -22.8, 5.2, 11.3, 3, 6),
        (-24.8, 0, 12.2, 3.65, 6, 3), (24.8, 0, 12.2, 3.65, 6, 3),
    ):
        for row in range(rows):
            for col in range(columns):
                x = cx - hx + col * 2 * hx / columns
                z = cz - hz + row * 2 * hz / rows
                x1, z1 = x + 2 * hx / columns, z + 2 * hz / rows
                pts = [(x,.87,z), (x,.87,z1), (x1,.87,z1), (x1,.87,z)]
                beveled_slab(model, pts, STONE if (row + col) % 5 else "ArenaSlabCool", EDGE, .045, .09, .035)
    # 地面のルーンを、石台に収まる低い紫水晶として見せる。
    for x, z in ((-24, -7), (24, -7), (0, 20.5)):
        model.cylinder(EDGE, (x, .87, z), 2.29, .10, 8)
        model.cylinder(STONE, (x, .96, z), 2.15, .09, 8)
        add_vertical_crystal(model, "ArenaGemViolet", (x, 1.05, z), 1.38, .55, 4)
        add_vertical_crystal(model, "ArenaGemLight", (x, 1.17, z), .68, .35, 4)


def update_scene():
    path = ROOT / "Resources/json/3Dobject/stage3_object.json"
    raw = path.read_bytes()
    text = raw.decode("utf8").replace("\r\n", "\n")
    for item in json.loads(text)["objects"]:
        name = item["name"]
        match = re.fullmatch(r"Stage3_HighCrown_BackgroundWall_(\d+)", name)
        if not match:
            continue
        model = "high_crown_banner_wall.obj" if int(match[1]) % 4 == 0 else "high_crown_arch_wall.obj"
        marker = text.index('"name": "' + name + '"')
        start = text.rfind("\n        {", 0, marker)
        end = text.index("\n        }", marker) + 10
        block = text[start:end]
        block = block.replace("high_crown_wall.obj", model)
        text = text[:start] + block + text[end:]
    json.loads(text)
    path.write_bytes(text.encode().replace(b"\n", b"\r\n" if b"\r\n" in raw else b"\n"))


def generate():
    for name, model in (
        ("high_crown_throne", build_throne()), ("high_crown_parapet", build_parapet()),
        ("high_crown_arch_wall", build_arch(True)), ("high_crown_banner_wall", build_arch(False)),
    ):
        path = STAGE_DIR / (name + ".obj")
        write_obj(model, path, "high_crown.mtl", name)
        if not path.with_suffix(".obj.meta").exists():
            write_meta(path, "Model", "ModelImporter")
    path = STAGE_DIR / "high_crown_arena_refined.obj"
    refine(STAGE_DIR / "high_crown_arena.obj", path)
    add_grass_borders(path, build_ornaments)
    update_scene()


if __name__ == "__main__":
    generate()
