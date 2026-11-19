"""ステージ3の門と塔を、橋・前庭・外周の支持構造へつなぎます。"""
from pathlib import Path
from math import cos, hypot, pi, sin, tau
import json
import re

from generate_stage3_high_crown import ObjBuilder, STAGE_DIR, cross, normalize, sub, write_obj, write_meta
from generate_stage3_high_crown_background import add_elliptic_frustum

ROOT = Path(__file__).resolve().parents[2]
STONE = "ArenaSlabIvory"
EDGE = "ArenaSlabEdge"
BLUE = "ArenaCastleBlue"
GOLD = "ArenaGold"
FLAG = "ArenaBanner"


def tower(model, x, y, z, radius, height):
    # 石積みの円筒に、帯と胸壁の出っ張りを作る。
    model.cylinder(EDGE, (x, y + .45, z), radius * 1.11, .9, 16)
    model.cylinder(BLUE, (x, y + height / 2, z), radius, height, 16)
    for rate in (.20, .59, .88):
        model.cylinder(EDGE, (x, y + height * rate, z), radius * 1.035, .28, 16)
    model.cylinder(STONE, (x, y + height, z), radius * 1.14, .55, 16)
    for index in range(12):
        angle = tau * index / 12
        model.box(STONE, (x + cos(angle) * radius, y + height + .68, z + sin(angle) * radius),
            (radius * .14, .46, radius * .14))
    # 窓と旗の厚みを外壁より少し手前に置く。
    for direction in (-1, 1):
        model.box(FLAG, (x, y + height * .64, z + direction * radius), (radius * .24, height * .13, .09))
        model.box(GOLD, (x, y + height * .77, z + direction * radius), (radius * .30, .10, .14))


def stone_beam(model, start, end, half_width, half_height, material=BLUE):
    """水平な石梁を始点と終点の間に置きます。端点のYは共通です。"""
    ax, y, az = start
    bx, _, bz = end
    length = hypot(bx - ax, bz - az)
    dx = -(bz - az) / length * half_width
    dz = (bx - ax) / length * half_width
    points = ((ax + dx, az + dz), (bx + dx, bz + dz),
              (bx - dx, bz - dz), (ax - dx, az - dz))
    bottom = [(x, y - half_height, z) for x, z in points]
    top = [(x, y + half_height, z) for x, z in points]
    model.quad(material, tuple(top), (0, 1, 0))
    model.quad(material, tuple(reversed(bottom)), (0, -1, 0))
    for index in range(4):
        nxt = (index + 1) % 4
        quad = (bottom[index], bottom[nxt], top[nxt], top[index])
        normal = normalize(cross(sub(quad[1], quad[0]), sub(quad[2], quad[0])))
        model.quad(material, quad, normal)


def arch_ring(model, center_z, spring, inner_x, inner_y, thickness, half_depth):
    # 門柱に載る迫石をひとつずつ作り、上から見ても奥行きのあるアーチにする。
    def point(angle, offset, z):
        return ((inner_x + offset) * cos(angle),
                spring + (inner_y + offset) * sin(angle), z)

    front = center_z + half_depth
    back = center_z - half_depth
    for index in range(13):
        lo = pi * (index + .025) / 13
        hi = pi * (index + .975) / 13
        for z, facing in ((front, 1), (back, -1)):
            face = (point(lo, 0, z), point(hi, 0, z),
                    point(hi, thickness, z), point(lo, thickness, z))
            model.quad(STONE if index % 4 else EDGE,
                       tuple(reversed(face)) if facing > 0 else face, (0, 0, facing))
        for offset, sign in ((0, -1), (thickness, 1)):
            face = (point(lo, offset, front), point(hi, offset, front),
                    point(hi, offset, back), point(lo, offset, back))
            mid = (lo + hi) / 2
            normal = (cos(mid) * sign, sin(mid) * sign, 0)
            model.quad(EDGE, face if sign > 0 else tuple(reversed(face)), normal)



def entrance():
    model = ObjBuilder()
    # 描画床の上端を既存の当たり判定と同じローカルY=-1.64に置く。
    add_elliptic_frustum(model, BLUE, EDGE, (0, -21.8, 75), 18.1, 16, 12, 10, 36, 36)
    model.box(BLUE, (0, -4.1, 74), (18.72, 2.1, 16.65))
    model.box(STONE, (0, -1.84, 74), (18.9, .20, 16.8))
    # 前庭から続く側壁で双塔と門柱を結び、橋の側面から門まで石が続くようにする。
    for sign in (-1, 1):
        x = sign * 10.9
        tower(model, x, -1.64, 86.0, 3.35, 10.8)
        model.box(BLUE, (x, -4.94, 86.1), (3.8, 3.2, 3.58))
        # 高さと張り出しの異なる帯で、塔の下へ向かって厚みを増す。
        for y, width, depth in ((-7.45, 4.6, 4.2), (-5.85, 4.18, 3.92), (-3.0, 3.9, 3.70)):
            model.box(EDGE, (x, y, 86.1), (width, .75, depth))
        model.box(BLUE, (sign * 15.2, .02, 76), (.74, 1.66, 12.2))
        model.box(STONE, (sign * 15.2, 1.76, 76), (.85, .13, 12.2))
        model.box(BLUE, (sign * 8.15, 1.65, 87.3), (2.95, 3.30, 1.53))
        model.box(EDGE, (sign * 5.77, 1.30, 87.3), (.65, 2.94, 1.62))
        model.box(STONE, (sign * 5.77, 4.22, 87.3), (.80, .23, 1.65))
        model.box(GOLD, (sign * 7.36, 1.95, 88.91), (.32, .38, .05))
    arch_ring(model, 87.3, 4.30, 5.10, 3.65, .78, 1.59)
    model.box(GOLD, (0, 8.39, 88.92), (.41, .44, .10))
    return model


def horizon():
    model = ObjBuilder()
    for x, z, radius, top in (
        (-118, -108, 16, -9), (0, -150, 22, -4), (118, -104, 16, -10),
        (-154, 22, 19, -13), (158, 30, 18, -11), (-92, 128, 15, -17), (96, 132, 15, -18),
    ):
        # 雲へ沈む土台の上に小さな砦を置き、裾が広がった一枚の斜面を解消する。
        add_elliptic_frustum(model, BLUE, EDGE, (x, top - 18, z), radius, radius * .82, radius * .72, radius * .63, 36, 20)
        model.cylinder(EDGE, (x, top + .10, z), radius * 1.02, .80, 20)
        model.cylinder(BLUE, (x, top + 2.3, z), radius * .84, 4.0, 20)
        model.cylinder(STONE, (x, top + 4.35, z), radius * .88, .45, 20)
        for offset, height in ((-.42, 9), (0, 13), (.42, 9)):
            tower(model, x + radius * offset, top + 2.8, z, radius * .175, height)
    return model


def outer_support(objects):
    model = ObjBuilder()
    for item in objects:
        if not re.fullmatch(r"Stage3_HighCrown_DecorTower_[1-4]", item["name"]):
            continue
        x, _, z = item["position"]
        # 外周塔の真下に支持柱を置き、闘技場の床下から放射状に受ける。
        radius = hypot(x, z)
        ux, uz = x / radius, z / radius
        tower(model, x, .78, z, 3.6, 11.4)
        model.cylinder(BLUE, (x, -11.9, z), 3.9, 25.2, 16)
        model.cylinder(EDGE, (x, -2.3, z), 4.45, .9, 16)
        # 継ぎ目のない石梁を床下へ差し込み、外周塔の基部と一体にする。
        start = (ux * (radius - 26), -6.25, uz * (radius - 26))
        end = (x, -6.25, z)
        stone_beam(model, start, end, 2.09, 2.48)
        stone_beam(model, (start[0], -3.61, start[2]), (x, -3.61, z), 2.20, .24, EDGE)
        for depth, width in ((-20, 4.6), (-15.5, 4.35), (-10.5, 4.15)):
            model.cylinder(EDGE, (x, depth, z), width, .50, 16)
    # 格子門から外周アーチへ袖壁を延ばす。橋の通路には張り出さない。
    for sign in (-1, 1):
        x = sign * 7.25
        model.box(BLUE, (x, -5.1, 43.2), (1.67, 5.75, 2.08))
        model.box(EDGE, (x, -1.0, 43.2), (1.82, .32, 2.24))
        stone_beam(model, (sign * 8.6, -.90, 43.6), (sign * 16.4, -.90, 55.4), 1.68, 1.38)
        stone_beam(model, (sign * 8.6, .57, 43.6), (sign * 16.4, .57, 55.4), 1.80, .18, STONE)
    model.box(BLUE, (0, -1.12, 43.2), (7.28, 1.02, 2.14))
    return model


def generate():
    scene = ROOT / "Resources/json/3Dobject/stage3_object.json"
    raw = scene.read_bytes()
    text = raw.decode("utf8").replace("\r\n", "\n")
    objects = json.loads(text)["objects"]
    names = {item["name"]: item for item in objects}
    # 既存のシーン参照と衝突を保持し、背景の描画モデルだけを差し替える。
    for name, model in (
        ("high_crown_entrance_structure", entrance()),
        ("high_crown_horizon_towers", horizon()),
        ("high_crown_outer_support", outer_support(objects)),
    ):
        output = STAGE_DIR / (name + ".obj")
        # エンジンのOBJ読み込みはZ軸を反転する。シーン座標に合わせ、面の表裏も保つ。
        model.vertices = [(x, y, -z) for x, y, z in model.vertices]
        model.normals = [(x, y, -z) for x, y, z in model.normals]
        for material, faces in model.faces.items():
            model.faces[material] = [(a, c, b) for a, b, c in faces]
        write_obj(model, output, "high_crown.mtl", name)
        write_meta(output, "Model", "ModelImporter")
    replacements = {
        "Stage3_HighCrown_Background_EntranceCitadel": "high_crown_entrance_structure",
        "Stage3_HighCrown_Background_HorizonCitadel": "high_crown_horizon_towers",
        "Stage3_HighCrown_OuterWallGroup": "high_crown_outer_support",
    }
    for name, asset in replacements.items():
        item = names[name]
        assert not item.get("collisionAttribute") and not item.get("collisionMask"), name
        marker = text.index('"name": "' + name + '"')
        start = text.rfind("\n        {", 0, marker)
        end = text.index("\n        }", marker) + 10
        block = text[start:end]
        reference = f'"modelName": "Stages/high_crown/{asset}.obj"'
        if '"modelName":' in block:
            block = re.sub(r'"modelName": "[^"]*"', reference, block)
        else:
            block = block.replace('"name":', reference + ',\n            "name":', 1)
        text = text[:start] + block + text[end:]
    # 新しい支持柱・塔と同じ位置の旧装飾塔を描画対象から外す。
    for item in objects:
        if not re.fullmatch(r"Stage3_HighCrown_DecorTower_[1-4]", item["name"]):
            continue
        marker = text.index('"name": "' + item["name"] + '"')
        start = text.rfind("\n        {", 0, marker)
        end = text.index("\n        }", marker) + 10
        block = re.sub(r' *"modelName": "[^"]*",\n', '', text[start:end], count=1)
        text = text[:start] + block + text[end:]
    json.loads(text)
    scene.write_bytes(text.encode().replace(b"\n", b"\r\n" if b"\r\n" in raw else b"\n"))


if __name__ == "__main__":
    generate()
