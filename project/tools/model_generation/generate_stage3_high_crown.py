"""ステージ3「High Crown」のボスと闘技場モデルを決定的に生成します。

攻撃処理は含めず、偽王スライム、楕円闘技場、黒格子ゲート、外周壁、
導入階段、王座背景をOBJ/MTL/テクスチャとして出力します。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from generate_ring_burner_model import ObjBuilder, cross, dot, normalize, sub


PROJECT_ROOT = Path(__file__).resolve().parents[2]
BOSS_DIR = PROJECT_ROOT / "Resources" / "3DModel" / "Characters" / "false_king_slime"
STAGE_DIR = PROJECT_ROOT / "Resources" / "3DModel" / "Stages" / "high_crown"
TEXCONV_PATH = PROJECT_ROOT / "Resources" / "tools" / "Texconv.exe"
HUD_BOSS_DIR = PROJECT_ROOT / "Resources" / "sprite" / "ui" / "hud" / "boss"

Vec3 = tuple[float, float, float]


BOSS_COLORS = {
    "SlimeBody": (210, 226, 242),
    "SlimeHighlight": (255, 252, 255),
    "SlimeShadow": (183, 176, 219),
    "Face": (27, 17, 52),
    "CrownBlack": (54, 55, 72),
    "CrownStone": (82, 81, 98),
    "CrownGold": (236, 188, 76),
    "CoreGlow": (255, 166, 225),
    "GemRed": (235, 68, 76),
    "GemBlue": (53, 129, 235),
    "GemGreen": (45, 187, 133),
    "GemViolet": (175, 74, 225),
    "GemOrange": (246, 154, 46),
    "GemCyan": (51, 207, 202),
}

STAGE_COLORS = {
    "StoneDark": (116, 124, 145),
    "Stone": (166, 168, 180),
    "StoneLight": (224, 220, 211),
    "PavingA": (199, 190, 178),
    "PavingB": (184, 177, 168),
    "Grass": (76, 133, 69),
    "GrassLight": (106, 157, 82),
    "Gold": (210, 158, 52),
    "GateBlack": (25, 27, 39),
    "GateGlow": (157, 79, 194),
    "PurpleRune": (176, 80, 200),
    "Banner": (91, 38, 119),
    "FlameGlow": (255, 144, 45),
}


# OBJ/MTLにも物理材質値を持たせ、Object単位のPBR値だけに依存しないようにします。
BOSS_PBR = {
    "SlimeBody": (0.20, 0.02),
    "SlimeHighlight": (0.14, 0.01),
    "SlimeShadow": (0.30, 0.01),
    "Face": (0.18, 0.04),
    "CrownBlack": (0.52, 0.12),
    "CrownStone": (0.60, 0.05),
    "CrownGold": (0.26, 0.78),
    "CoreGlow": (0.20, 0.10),
    "GemRed": (0.16, 0.56),
    "GemBlue": (0.16, 0.56),
    "GemGreen": (0.16, 0.56),
    "GemViolet": (0.16, 0.56),
    "GemOrange": (0.16, 0.56),
    "GemCyan": (0.16, 0.56),
}

STAGE_PBR = {
    "StoneDark": (0.66, 0.02),
    "Stone": (0.62, 0.01),
    "StoneLight": (0.58, 0.01),
    "PavingA": (0.60, 0.01),
    "PavingB": (0.66, 0.01),
    "Grass": (0.88, 0.0),
    "GrassLight": (0.84, 0.0),
    "Gold": (0.28, 0.76),
    "GateBlack": (0.36, 0.72),
    "GateGlow": (0.18, 0.10),
    "PurpleRune": (0.24, 0.06),
    "Banner": (0.72, 0.0),
    "FlameGlow": (0.20, 0.06),
}


def add_elliptic_cylinder(
    model: ObjBuilder,
    material_side: str,
    material_top: str,
    center: Vec3,
    radius_x: float,
    radius_z: float,
    height: float,
    segments: int = 40,
) -> None:
    bottom_y = center[1] - height * 0.5
    top_y = center[1] + height * 0.5
    top_center = model.vertex((center[0], top_y, center[2]), (0.5, 0.5), (0.0, 1.0, 0.0))
    bottom_center = model.vertex((center[0], bottom_y, center[2]), (0.5, 0.5), (0.0, -1.0, 0.0))
    side_bottom: list[int] = []
    side_top: list[int] = []
    cap_top: list[int] = []
    cap_bottom: list[int] = []
    for index in range(segments):
        angle = math.tau * index / segments
        radial = normalize((math.cos(angle) / radius_x, 0.0, math.sin(angle) / radius_z))
        x = center[0] + math.cos(angle) * radius_x
        z = center[2] + math.sin(angle) * radius_z
        side_bottom.append(model.vertex((x, bottom_y, z), (index / segments, 1.0), radial))
        side_top.append(model.vertex((x, top_y, z), (index / segments, 0.0), radial))
        cap_top.append(model.vertex((x, top_y, z), (0.5 + math.cos(angle) * 0.5, 0.5 - math.sin(angle) * 0.5), (0.0, 1.0, 0.0)))
        cap_bottom.append(model.vertex((x, bottom_y, z), (0.5 + math.cos(angle) * 0.5, 0.5 + math.sin(angle) * 0.5), (0.0, -1.0, 0.0)))
    for index in range(segments):
        nxt = (index + 1) % segments
        model.face(material_side, side_bottom[index], side_bottom[nxt], side_top[nxt], False)
        model.face(material_side, side_bottom[index], side_top[nxt], side_top[index], False)
        model.face(material_top, top_center, cap_top[index], cap_top[nxt], False)
        model.face(material_side, bottom_center, cap_bottom[nxt], cap_bottom[index], False)


def add_elliptic_annulus_tiles(
    model: ObjBuilder,
    materials: tuple[str, ...],
    inner_radius_x: float,
    inner_radius_z: float,
    outer_radius_x: float,
    outer_radius_z: float,
    y: float,
    segments: int = 48,
    rows: int = 1,
) -> None:
    """楕円環を分割した石板として作り、巨大な一枚テクスチャに見えないようにします。"""
    for row in range(rows):
        row_inner = row / rows
        row_outer = (row + 1) / rows
        inner_x = inner_radius_x + (outer_radius_x - inner_radius_x) * row_inner
        inner_z = inner_radius_z + (outer_radius_z - inner_radius_z) * row_inner
        outer_x = inner_radius_x + (outer_radius_x - inner_radius_x) * row_outer
        outer_z = inner_radius_z + (outer_radius_z - inner_radius_z) * row_outer
        for segment in range(segments):
            a0 = math.tau * segment / segments
            a1 = math.tau * (segment + 1) / segments
            lift = ((segment + row) % 3) * 0.002
            points = (
                (math.cos(a0) * inner_x, y + lift, math.sin(a0) * inner_z),
                (math.cos(a1) * inner_x, y + lift, math.sin(a1) * inner_z),
                (math.cos(a1) * outer_x, y + lift, math.sin(a1) * outer_z),
                (math.cos(a0) * outer_x, y + lift, math.sin(a0) * outer_z),
            )
            model.quad(materials[(segment + row) % len(materials)], points, (0.0, 1.0, 0.0))


def add_flat_diamond(
    model: ObjBuilder,
    material: str,
    center: Vec3,
    half_x: float,
    half_z: float,
) -> None:
    x, y, z = center
    model.quad(
        material,
        ((x - half_x, y, z), (x, y, z + half_z), (x + half_x, y, z), (x, y, z - half_z)),
        (0.0, 1.0, 0.0),
    )


def add_vertical_crystal(
    model: ObjBuilder,
    material: str,
    center: Vec3,
    radius: float,
    height: float,
    segments: int = 6,
) -> None:
    """上下に尖った低ポリゴン結晶。王冠片と浮遊宝石の共通形状です。"""
    ring_y = center[1]
    lower = model.vertex((center[0], ring_y - height * 0.42, center[2]), (0.5, 1.0), (0.0, -1.0, 0.0))
    upper = model.vertex((center[0], ring_y + height * 0.58, center[2]), (0.5, 0.0), (0.0, 1.0, 0.0))
    ring: list[int] = []
    for index in range(segments):
        angle = math.tau * index / segments
        radial = (math.cos(angle), 0.0, math.sin(angle))
        ring.append(model.vertex(
            (center[0] + radial[0] * radius, ring_y, center[2] + radial[2] * radius),
            (index / segments, 0.5),
            radial,
        ))
    for index in range(segments):
        nxt = (index + 1) % segments
        model.face(material, ring[index], upper, ring[nxt], False)
        model.face(material, ring[nxt], lower, ring[index], False)


def add_vertical_cone(
    model: ObjBuilder,
    material: str,
    center: Vec3,
    radius: float,
    height: float,
    segments: int = 10,
) -> None:
    base_y = center[1]
    tip = model.vertex((center[0], base_y + height, center[2]), (0.5, 0.0), (0.0, 1.0, 0.0))
    base_center = model.vertex((center[0], base_y, center[2]), (0.5, 0.5), (0.0, -1.0, 0.0))
    side: list[int] = []
    cap: list[int] = []
    for index in range(segments):
        angle = math.tau * index / segments
        radial = (math.cos(angle), 0.0, math.sin(angle))
        position = (center[0] + radial[0] * radius, base_y, center[2] + radial[2] * radius)
        side_normal = normalize((radial[0], radius / max(height, 0.001), radial[2]))
        side.append(model.vertex(position, (index / segments, 1.0), side_normal))
        cap.append(model.vertex(position, (0.5 + radial[0] * 0.5, 0.5 + radial[2] * 0.5), (0.0, -1.0, 0.0)))
    for index in range(segments):
        nxt = (index + 1) % segments
        model.face(material, side[index], tip, side[nxt], False)
        model.face(material, base_center, cap[nxt], cap[index], False)


def add_crown_plate(
    model: ObjBuilder,
    material: str,
    center: Vec3,
    angle: float,
    width: float,
    height: float,
    thickness: float = 0.20,
) -> None:
    """暗い割れ石の前後面と、金色の厚みを持つ不規則な王冠板を追加します。"""
    radial = (math.sin(angle), 0.0, math.cos(angle))
    tangent = (math.cos(angle), 0.0, -math.sin(angle))
    outline = [
        (-width * 0.50, 0.0),
        (width * 0.50, 0.0),
        (width * 0.42, height * 0.58),
        (width * 0.10, height),
        (-width * 0.38, height * 0.72),
    ]

    def point(local_x: float, local_y: float, depth: float) -> Vec3:
        return (
            center[0] + tangent[0] * local_x + radial[0] * depth,
            center[1] + local_y,
            center[2] + tangent[2] * local_x + radial[2] * depth,
        )

    front = [model.vertex(point(x, y, thickness * 0.5), (x / width + 0.5, 1.0 - y / height), radial) for x, y in outline]
    back_normal = (-radial[0], 0.0, -radial[2])
    back = [model.vertex(point(x, y, -thickness * 0.5), (x / width + 0.5, 1.0 - y / height), back_normal) for x, y in reversed(outline)]
    for index in range(1, len(front) - 1):
        model.face(material, front[0], front[index], front[index + 1], False)
        model.face(material, back[0], back[index], back[index + 1], False)

    for index in range(len(outline)):
        nxt = (index + 1) % len(outline)
        x0, y0 = outline[index]
        x1, y1 = outline[nxt]
        p0 = point(x0, y0, thickness * 0.5)
        p1 = point(x1, y1, thickness * 0.5)
        p2 = point(x1, y1, -thickness * 0.5)
        p3 = point(x0, y0, -thickness * 0.5)
        side_normal = normalize(cross(sub(p1, p0), sub(p3, p0)))
        model.quad("CrownGold", (p0, p1, p2, p3), side_normal)


def add_false_king_crown(model: ObjBuilder, base_y: float, include_floating_gems: bool = True) -> None:
    """参考ラフの黒い割れ石、金縁、中央宝石を一体の王冠として追加します。"""
    model.torus("CrownGold", (0.0, base_y, 0.0), 1.78, 0.22, 48, 12)
    model.torus("CrownStone", (0.0, base_y + 0.18, 0.0), 1.50, 0.18, 44, 10)

    # 黒い針の集合ではなく、金縁を持つ幅広の割れ石9枚で王冠の塊を作ります。
    shard_angles = [-1.28, -0.82, -0.35, 0.12, 0.59, 1.06, 2.22, 3.14, 4.10]
    shard_heights = [0.86, 1.16, 1.00, 1.40, 1.00, 1.16, 0.94, 1.22, 0.92]
    for index, (angle, height) in enumerate(zip(shard_angles, shard_heights)):
        radius = 1.48 if index % 2 == 0 else 1.58
        center = (
            math.sin(angle) * radius,
            base_y + 0.58 + height * 0.20,
            math.cos(angle) * radius,
        )
        # 暗い石板の面を残しつつ、押し出し側面だけを金にして参考ラフの金縁へ寄せます。
        plate_center = (center[0], base_y + 0.30, center[2])
        add_crown_plate(
            model,
            "CrownStone" if index % 3 == 0 else "CrownBlack",
            plate_center,
            angle,
            0.88 if index % 2 == 0 else 0.78,
            height,
        )
        model.box("CrownGold", (center[0], base_y + 0.34, center[2]), (0.28, 0.13, 0.28))

    # 正面中央は金枠とピンク結晶を二層にし、ラフの王冠の顔になる宝石を再現します。
    add_vertical_crystal(model, "CrownGold", (0.0, base_y + 0.58, 1.76), 0.52, 1.10, 4)
    add_vertical_crystal(model, "CoreGlow", (0.0, base_y + 0.60, 2.00), 0.34, 0.76, 4)

    if not include_floating_gems:
        return

    gem_layout = [
        ("GemRed", -0.92, base_y + 2.02, 0.30, 0.34, 1.02),
        ("GemOrange", 1.02, base_y + 1.78, 0.32, 0.31, 0.92),
        ("GemGreen", -2.45, base_y + 0.86, 0.32, 0.34, 1.02),
        ("GemBlue", 2.55, base_y + 0.78, 0.28, 0.31, 0.96),
        ("GemViolet", -1.88, base_y + 1.36, -0.62, 0.24, 0.76),
        ("GemCyan", 1.86, base_y + 1.19, -0.68, 0.24, 0.76),
    ]
    for material, x, y, z, radius, height in gem_layout:
        add_vertical_crystal(model, material, (x, y + 0.01, z), radius, height, 6)
        model.box("CrownGold", (x, y - height * 0.39, z), (radius * 0.48, 0.055, radius * 0.48))
        model.box("CrownGold", (x, y + height * 0.53, z), (radius * 0.34, 0.045, radius * 0.34))


def build_false_king() -> ObjBuilder:
    model = ObjBuilder()
    # 参考ラフの低く横へ広がった真珠色ボディ。単純な球体に見えないよう裾を分けます。
    model.sphere("SlimeBody", (0.0, 1.48, 0.0), (2.82, 1.46, 2.28), 52, 24)
    model.sphere("SlimeShadow", (-2.06, 0.54, 0.03), (0.92, 0.52, 1.38), 28, 14)
    model.sphere("SlimeShadow", (2.06, 0.54, 0.03), (0.92, 0.52, 1.38), 28, 14)
    model.sphere("SlimeBody", (-0.96, 0.34, 1.42), (1.12, 0.34, 0.72), 26, 12)
    model.sphere("SlimeBody", (0.98, 0.34, 1.40), (1.08, 0.34, 0.72), 26, 12)
    model.sphere("SlimeHighlight", (-0.78, 2.22, 1.58), (0.62, 0.24, 0.15), 22, 10)
    model.sphere("CoreGlow", (0.0, 2.60, 0.75), (0.74, 0.24, 0.56), 24, 10)

    # 正面は+Z。口を付けず、縦長の目だけで威圧感を出します。
    for x in (-0.78, 0.78):
        model.sphere("Face", (x, 1.64, 2.39), (0.31, 0.51, 0.13), 24, 14)
        model.sphere("SlimeHighlight", (x - 0.07, 1.84, 2.51), (0.07, 0.10, 0.028), 12, 7)

    add_false_king_crown(model, 2.86, True)
    return model


def build_false_king_crown() -> ObjBuilder:
    model = ObjBuilder()
    add_false_king_crown(model, 0.30, True)
    return model


def build_arena() -> ObjBuilder:
    model = ObjBuilder()
    # 大型ボスの正面と左右へ回避距離を残す、横80m・奥行68mの戦闘床です。
    add_elliptic_cylinder(model, "StoneDark", "Stone", (0.0, -2.05, 0.0), 40.0, 34.0, 4.7, 96)
    add_elliptic_cylinder(model, "Stone", "PavingB", (0.0, 0.38, 0.0), 39.3, 33.3, 0.54, 96)

    # 明暗を交互に並べず、淡いコンクリート板へ少量の色差だけを混ぜます。
    paving_pattern = ("PavingA", "PavingA", "PavingA", "PavingA", "PavingB")
    add_elliptic_annulus_tiles(model, paving_pattern, 33.2, 28.0, 38.65, 32.55, 0.67, 104, 2)
    add_elliptic_annulus_tiles(model, ("Grass", "Grass", "GrassLight"), 29.8, 24.7, 32.95, 27.75, 0.685, 96, 1)
    add_elliptic_annulus_tiles(model, paving_pattern, 12.0, 9.0, 29.55, 24.45, 0.705, 104, 8)
    add_elliptic_cylinder(model, "Grass", "GrassLight", (0.0, 0.73, 0.0), 11.7, 8.7, 0.12, 80)

    # 入口、玉座、左右へ石の動線を伸ばし、芝一枚に見えない戦闘床へします。
    model.box("PavingA", (0.0, 0.79, 22.0), (5.2, 0.06, 11.2))
    model.box("PavingB", (0.0, 0.795, -22.8), (5.2, 0.06, 11.3))
    model.box("PavingA", (-24.8, 0.79, 0.0), (12.2, 0.06, 3.65))
    model.box("PavingB", (24.8, 0.795, 0.0), (12.2, 0.06, 3.65))

    # 床の王冠紋章と三つの紫ルーンは、戦闘位置を読み取るためのランドマークです。
    model.box("Gold", (0.0, 0.86, 0.35), (2.15, 0.045, 0.38))
    add_flat_diamond(model, "Gold", (-1.52, 0.91, -0.18), 0.72, 1.45)
    add_flat_diamond(model, "Gold", (0.0, 0.91, -0.42), 0.84, 1.85)
    add_flat_diamond(model, "Gold", (1.52, 0.91, -0.18), 0.72, 1.45)
    for x, z in ((-24.0, -7.0), (24.0, -7.0), (0.0, 20.5)):
        model.cylinder("StoneLight", (x, 0.82, z), 2.65, 0.12, 8)
        add_flat_diamond(model, "PurpleRune", (x, 0.91, z), 1.58, 1.98)

    # 外壁下の崩れた支持石。規則正しい円盤だけに見えない輪郭を作ります。
    for index in range(36):
        angle = math.tau * index / 36
        x = math.cos(angle) * 39.2
        z = math.sin(angle) * 33.2
        drop = 2.8 + (index % 4) * 0.48
        model.box("StoneDark", (x, -4.05 - drop * 0.20, z), (0.82, drop, 0.82))
    return model


def build_wall() -> ObjBuilder:
    model = ObjBuilder()
    model.box("StoneDark", (0.0, 1.46, 0.0), (3.0, 1.46, 0.70))
    model.box("Stone", (0.0, 2.34, 0.74), (3.02, 0.46, 0.12))
    model.box("StoneLight", (0.0, 3.02, 0.0), (3.10, 0.16, 0.78))
    # 石組みの横目地を立体化し、単色の板に見えないようにします。
    for y in (0.62, 1.45, 2.24):
        model.box("Stone", (0.0, y, 0.72), (2.86, 0.055, 0.08))
    for x in (-2.52, -1.26, 0.0, 1.26, 2.52):
        model.box("StoneLight", (x, 3.54, 0.0), (0.38, 0.50, 0.80))
    return model


def build_tower() -> ObjBuilder:
    model = ObjBuilder()
    # 反復壁から装飾を分離し、四つの塔だけを王宮テーマのランドマークにします。
    model.box("StoneDark", (0.0, 1.75, 0.0), (1.72, 1.75, 1.48))
    model.box("Stone", (0.0, 3.58, 0.0), (1.92, 0.18, 1.66))
    model.box("StoneLight", (0.0, 3.92, 0.0), (1.58, 0.16, 1.38))
    model.box("Banner", (0.0, 2.12, 1.50), (0.72, 1.02, 0.05))
    model.box("Gold", (0.0, 3.15, 1.55), (0.82, 0.07, 0.09))
    for x in (-0.90, 0.0, 0.90):
        add_vertical_crystal(model, "StoneLight", (x, 4.42 + (0.28 if abs(x) < 0.1 else 0.0), 0.0), 0.34, 0.92, 5)
    add_vertical_crystal(model, "GateGlow", (0.0, 4.96, 0.0), 0.34, 0.98, 5)
    model.box("Gold", (1.36, 1.80, 1.58), (0.10, 0.54, 0.10))
    model.sphere("FlameGlow", (1.36, 2.48, 1.58), (0.21, 0.38, 0.21), 14, 8)
    return model


def build_gate_frame() -> ObjBuilder:
    model = ObjBuilder()
    # 門柱、側翼、梁を淡い石造ブロックで一体化し、格子が開いていても門楼を残します。
    for x in (-7.25, 7.25):
        model.box("Stone", (x, 3.65, 0.0), (1.36, 3.65, 1.52))
        # 正面へ石の段差を重ね、単純な直方体の柱に見えない輪郭を作ります。
        model.box("StoneLight", (x, 0.32, 0.24), (1.62, 0.32, 1.72))
        model.box("StoneDark", (x, 2.15, 1.42), (1.08, 0.12, 0.12))
        model.box("StoneDark", (x, 4.45, 1.42), (1.08, 0.12, 0.12))
        model.box("Stone", (x, 7.38, 0.0), (1.66, 0.22, 1.72))
        model.box("StoneLight", (x, 7.82, 0.0), (1.26, 0.20, 1.30))
        model.box("Banner", (x, 4.10, 1.42), (0.62, 1.28, 0.05))
        model.box("Gold", (x, 5.42, 1.47), (0.72, 0.07, 0.10))
        add_vertical_crystal(model, "GateGlow", (x, 8.56, 0.0), 0.52, 1.38, 5)
        wing_sign = -1.0 if x < 0.0 else 1.0
        model.box("Stone", (x + wing_sign * 2.65, 2.20, 0.28), (1.38, 2.20, 1.20))
        model.box("StoneLight", (x + wing_sign * 2.65, 4.48, 0.28), (1.56, 0.15, 1.32))
    model.box("Stone", (0.0, 7.55, 0.0), (6.18, 0.46, 1.00))
    model.box("StoneDark", (0.0, 7.48, 1.02), (6.02, 0.16, 0.10))
    model.box("StoneLight", (0.0, 8.08, 0.0), (6.48, 0.12, 1.08))
    model.box("StoneLight", (0.0, 8.38, 0.0), (6.70, 0.15, 1.14))
    for x in (-5.25, -2.62, 0.0, 2.62, 5.25):
        add_vertical_crystal(model, "GateGlow", (x, 8.75, 0.0), 0.24, 0.70, 4)
    return model


def build_gate() -> ObjBuilder:
    model = ObjBuilder()
    # 黒格子だけを可動部に分離し、常設の石造外枠と独立して開閉させます。

    for x in (-5.85, -4.39, -2.93, -1.46, 0.0, 1.46, 2.93, 4.39, 5.85):
        model.box("GateBlack", (x, 3.58, 0.0), (0.22, 3.58, 0.38))
        # 棒の先端を王冠形にし、参考画像の黒網シルエットへ寄せます。
        add_vertical_crystal(model, "GateBlack", (x, 7.45 + (0.40 if abs(x) < 0.2 else 0.0), 0.0), 0.38, 1.02, 4)
    for y in (0.52, 1.92, 3.32, 4.72, 6.12):
        model.box("GateBlack", (0.0, y, 0.0), (6.25, 0.18, 0.42))
    # 紫の継ぎ目は面ではなく小さな結晶に限定し、地形の前後関係を保ちます。
    for x in (-4.39, -1.46, 1.46, 4.39):
        for y in (1.92, 4.72):
            add_vertical_crystal(model, "GateGlow", (x, y, 0.42), 0.16, 0.38, 4)
    return model


def build_step() -> ObjBuilder:
    model = ObjBuilder()
    step_count = 9
    total_depth = 27.0
    tread_depth = total_depth / step_count
    base_bottom = -3.15

    # カメラが最下段の前面へ潜り込まないよう、階段の手前に十分な奥行きの到着床を設けます。
    # Stage3上ではこのモデルをZ=37へ置くため、到着床はZ=49～59の範囲になります。
    landing_center_z = total_depth * 0.5 + 5.0
    model.box("StoneDark", (0.0, (base_bottom - 2.40) * 0.5, landing_center_z), (6.20, 0.42, 5.0))
    model.box("PavingA", (0.0, -2.365, landing_center_z), (6.20, 0.035, 4.96))
    model.box("Grass", (0.0, -2.315, landing_center_z), (4.95, 0.025, 4.42))
    for x in (-5.72, 5.72):
        model.box("StoneLight", (x, -2.04, landing_center_z), (0.48, 0.36, 4.94))

    for index in range(step_count):
        # 入口側を2.4m下げ、闘技場側まで一段0.4mずつ上る九段の実階段にします。
        top_y = -2.40 + index * 0.40
        center_y = (base_bottom + top_y) * 0.5
        half_y = (top_y - base_bottom) * 0.5
        center_z = total_depth * 0.5 - tread_depth * (index + 0.5)
        model.box("StoneDark", (0.0, center_y, center_z), (6.20, half_y, tread_depth * 0.5))
        model.box("PavingA", (0.0, top_y + 0.035, center_z), (6.20, 0.035, tread_depth * 0.5 - 0.04))
        model.box("Grass", (0.0, top_y + 0.085, center_z + 0.26), (4.95, 0.025, tread_depth * 0.5 - 0.54))
        model.box("StoneLight", (0.0, top_y - 0.20, center_z - tread_depth * 0.5 + 0.08), (6.20, 0.20, 0.08))
        # 連続した金色レールではなく、段差に沿う厚い石造縁石と小さな王宮金具にします。
        for x in (-5.72, 5.72):
            model.box("StoneLight", (x, top_y + 0.34, center_z), (0.48, 0.34, tread_depth * 0.5 - 0.06))
            if index in (0, 4, 8):
                model.box("Gold", (x, top_y + 0.72, center_z), (0.30, 0.08, 0.34))
    return model


def build_throne() -> ObjBuilder:
    model = ObjBuilder()
    model.box("StoneDark", (0.0, 0.62, 0.0), (5.6, 0.62, 2.75))
    model.box("PavingB", (0.0, 1.29, 0.0), (5.45, 0.08, 2.58))
    model.box("Stone", (0.0, 2.08, 0.68), (2.35, 0.72, 1.28))
    model.box("Gold", (0.0, 2.84, 0.68), (2.48, 0.08, 1.36))
    # 玉座背面は一枚壁ではなく、崩れた王冠状の石柱と紫結晶で構成します。
    shard_layout = [
        (-4.65, 3.42, 1.92, 0.76, 4.65),
        (-3.30, 4.25, 2.10, 0.92, 6.00),
        (-1.75, 4.92, 2.18, 1.05, 7.10),
        (0.0, 5.35, 2.26, 1.18, 7.90),
        (1.72, 4.78, 2.18, 1.02, 6.85),
        (3.22, 4.10, 2.08, 0.88, 5.70),
        (4.58, 3.35, 1.90, 0.72, 4.45),
    ]
    for index, (x, y, z, radius, height) in enumerate(shard_layout):
        add_vertical_crystal(model, "StoneLight" if index % 2 else "StoneDark", (x, y, z), radius, height, 5)
    for x, y, z, radius, height in ((-2.45, 5.15, 2.62, 0.38, 1.55), (0.0, 6.62, 2.72, 0.52, 2.10), (2.42, 5.08, 2.62, 0.38, 1.48)):
        add_vertical_crystal(model, "GateGlow", (x, y, z), radius, height, 6)
    # 水平トーラスは正面から浮いた輪に見えるため使わず、王冠の割れ目を金の留め具でつなぎます。
    for x in (-3.85, -2.55, -1.15, 0.0, 1.15, 2.55, 3.85):
        model.box("Gold", (x, 2.92 + (1.0 - abs(x) / 4.0) * 0.48, 2.40), (0.20, 0.18, 0.16))
    return model


def write_obj(model: ObjBuilder, path: Path, mtl_name: str, object_name: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as file:
        file.write(f"# CG2 Stage 3 High Crown\nmtllib {mtl_name}\no {object_name}\n")
        for value in model.vertices:
            file.write(f"v {value[0]:.7f} {value[1]:.7f} {value[2]:.7f}\n")
        for value in model.uvs:
            file.write(f"vt {value[0]:.7f} {value[1]:.7f}\n")
        for value in model.normals:
            file.write(f"vn {value[0]:.7f} {value[1]:.7f} {value[2]:.7f}\n")
        for material, faces in model.faces.items():
            file.write(f"usemtl {material}\n")
            file.write("s 1\n" if material in model.smooth_materials else "s off\n")
            for a, b, c in faces:
                file.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


def create_texture(
    path: Path,
    material_name: str,
    dark: tuple[int, int, int],
    light: tuple[int, int, int],
    seed: float,
) -> None:
    size = 256
    image = Image.new("RGBA", (size, size), (*dark, 255))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            value = (x * 73856093) ^ (y * 19349663) ^ int(seed * 83492791)
            grain = (value & 255) / 255.0
            broad = 0.5 + 0.5 * math.sin(x * 0.038 + y * 0.031 + seed)
            rate = 0.34 + grain * 0.18 + broad * 0.22
            if "Grass" in material_name:
                blade = 0.15 if ((x * 3 + y * 7 + int(seed * 11)) % 37) < 2 else 0.0
                rate += blade
            elif material_name in ("StoneDark", "Stone", "StoneLight"):
                # 横長の石組みをアルベドにも薄く刻み、門柱と外壁を一枚板に見せません。
                brick_size = 64
                row = y // brick_size
                shifted_x = (x + (brick_size // 2 if row % 2 else 0)) % brick_size
                mortar = (y % brick_size) < 4 or shifted_x < 4
                rate += -0.26 if mortar else 0.025
                chip = -0.18 if ((x * 5 + y * 11 + int(seed * 17)) % 113) < 2 else 0.0
                rate += chip
            elif "Paving" in material_name:
                # 各床ポリゴンの外周を目地にし、放射状の巨大な三角形ではなく石板として読み取らせます。
                edge = min(x, y, size - 1 - x, size - 1 - y)
                rate += -0.22 if edge < 5 else 0.02
                chip = -0.12 if ((x * 7 + y * 13 + int(seed * 17)) % 181) < 2 else 0.0
                rate += chip
            elif "Gold" in material_name:
                rate += 0.10 if (x + int(seed * 19)) % 47 < 2 else 0.0
            rate = max(0.08, min(0.92, rate))
            color = [int(dark[i] + (light[i] - dark[i]) * rate) for i in range(3)]
            if material_name == "SlimeBody":
                # UV上で三色の真珠反射を緩く混ぜ、ゲーム内照明でも白飛びしない色差を残します。
                cyan = 0.5 + 0.5 * math.sin(x * 0.021 + y * 0.010 + 0.7)
                pink = 0.5 + 0.5 * math.sin(x * 0.014 - y * 0.024 + 2.2)
                warm = 0.5 + 0.5 * math.sin(x * 0.030 + y * 0.018 + 4.1)
                color[0] = min(255, int(color[0] + pink * 12.0 + warm * 7.0))
                color[1] = min(255, int(color[1] + cyan * 9.0 + warm * 4.0))
                color[2] = min(255, int(color[2] + cyan * 14.0 + pink * 8.0))
            pixels[x, y] = tuple(color) + (255,)
    image.save(path)


def create_boss_name_sprite() -> Path:
    HUD_BOSS_DIR.mkdir(parents=True, exist_ok=True)
    output = HUD_BOSS_DIR / "false_king_slime_name.png"
    image = Image.new("RGBA", (800, 168), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image, "RGBA")
    font_candidates = [
        Path("C:/Windows/Fonts/MEIRYOBD.TTC"),
        Path("C:/Windows/Fonts/meiryob.ttc"),
        Path("C:/Windows/Fonts/YuGothB.ttc"),
    ]
    font_path = next((candidate for candidate in font_candidates if candidate.is_file()), None)
    if font_path is None:
        raise RuntimeError("日本語ボス名に使用するフォントが見つかりません。")
    font = ImageFont.truetype(str(font_path), 86)
    text = "偽王スライム"
    bounds = draw.textbbox((0, 0), text, font=font, stroke_width=0)
    text_width = bounds[2] - bounds[0]
    text_height = bounds[3] - bounds[1]
    origin = ((800 - text_width) * 0.5, (168 - text_height) * 0.5 - bounds[1] - 4)
    # 紫の外縁、黒い輪郭、金の本文を重ねて王冠の意匠へ寄せます。
    draw.text(origin, text, font=font, fill=(255, 213, 70, 255), stroke_width=12, stroke_fill=(92, 35, 134, 235))
    draw.text(origin, text, font=font, fill=(255, 218, 82, 255), stroke_width=6, stroke_fill=(25, 18, 39, 255))
    highlight_origin = (origin[0], origin[1] - 2)
    draw.text(highlight_origin, text, font=font, fill=(255, 239, 151, 150), stroke_width=0)
    for x, color in ((72, (255, 66, 96, 255)), (728, (64, 220, 255, 255))):
        draw.polygon([(x, 54), (x + 18, 84), (x, 114), (x - 18, 84)], fill=color)
        draw.polygon([(x, 63), (x + 9, 84), (x, 94), (x - 9, 84)], fill=(255, 255, 255, 165))
    image.save(output)
    return output


def create_surface_maps(directory: Path, prefix: str, style: str) -> tuple[Path, Path]:
    """法線とORMを生成し、アルベドだけの平坦な見え方を避けます。"""
    size = 512
    heights: list[list[float]] = [[0.0] * size for _ in range(size)]
    for y in range(size):
        v = y / size
        for x in range(size):
            u = x / size
            if style == "boss":
                height = (
                    math.sin(u * math.tau * 3.0 + math.sin(v * math.tau * 2.0) * 0.55) * 0.34
                    + math.sin(v * math.tau * 4.0 + u * math.tau * 0.7) * 0.22
                    + math.sin((u + v) * math.tau * 9.0) * 0.07
                )
            elif style == "gate":
                # Brushed metal keeps the lattice dark while catching a thin moving highlight.
                height = (
                    math.sin(v * math.tau * 48.0 + u * 3.0) * 0.08
                    + math.sin(v * math.tau * 121.0 - u * 5.0) * 0.025
                )
            else:
                # Clean concrete uses shallow aggregate instead of a repeated deep grid.
                hash_value = ((x * 73856093) ^ (y * 19349663) ^ 0x5F3759DF) & 255
                aggregate = hash_value / 255.0 - 0.5
                height = (
                    aggregate * 0.060
                    + math.sin(u * math.tau * 7.0) * 0.018
                    + math.sin(v * math.tau * 9.0) * 0.014
                )
            heights[y][x] = height

    strength = 2.2 if style == "boss" else (3.2 if style == "gate" else 1.8)
    normal = Image.new("RGB", (size, size))
    orm = Image.new("RGB", (size, size))
    normal_pixels = normal.load()
    orm_pixels = orm.load()
    for y in range(size):
        for x in range(size):
            dx = heights[y][(x + 1) % size] - heights[y][(x - 1) % size]
            dy = heights[(y + 1) % size][x] - heights[(y - 1) % size][x]
            nx, ny, nz = normalize((-dx * strength, -dy * strength, 1.0))
            normal_pixels[x, y] = (
                int((nx * 0.5 + 0.5) * 255.0),
                int((ny * 0.5 + 0.5) * 255.0),
                int((nz * 0.5 + 0.5) * 255.0),
            )
            variation = math.sin(x * 0.073 + y * 0.031) * 0.5 + 0.5
            ao = 245 if style == "boss" else int(226 + variation * 24)
            roughness = int((0.30 if style == "boss" else (0.38 if style == "gate" else 0.72)) * 255.0)
            # Bは各MTLのmetallic値を維持するため1.0とし、Object側の係数で調整します。
            orm_pixels[x, y] = (ao, roughness, 255)

    normal_path = directory / f"{prefix}_surface_normal.png"
    orm_path = directory / f"{prefix}_surface_orm.png"
    normal.save(normal_path)
    orm.save(orm_path)
    return normal_path, orm_path


def write_mtl(
    path: Path,
    colors: dict[str, tuple[int, int, int]],
    prefix: str,
    pbr: dict[str, tuple[float, float]],
    normal_name: str,
    orm_name: str,
) -> list[Path]:
    textures: list[Path] = []
    with path.open("w", encoding="utf-8", newline="\n") as file:
        file.write("# CG2 High Crown material set\n\n")
        for index, (name, color) in enumerate(colors.items()):
            texture_name = f"{prefix}_{name.lower()}.png"
            texture_path = path.parent / texture_name
            dark_rate = 0.82 if any(token in name for token in ("Stone", "Paving")) else 0.68
            dark = tuple(max(0, int(channel * dark_rate)) for channel in color)
            light = tuple(min(255, int(channel * 1.08 + 6)) for channel in color)
            create_texture(texture_path, name, dark, light, index * 0.73)
            textures.append(texture_path)
            kd = tuple(channel / 255.0 for channel in color)
            emission = 0.26 if any(token in name for token in ("Glow", "Gem", "Rune", "Core")) else (0.08 if name == "CrownGold" else 0.0)
            roughness, metallic = pbr[name]
            shininess = max(1.0, (1.0 - roughness) * 128.0)
            file.write(f"newmtl {name}\n")
            file.write(f"Ka {kd[0] * 0.35:.6f} {kd[1] * 0.35:.6f} {kd[2] * 0.35:.6f}\n")
            file.write(f"Kd {kd[0]:.6f} {kd[1]:.6f} {kd[2]:.6f}\n")
            file.write("Ks 0.780000 0.780000 0.860000\n")
            file.write(f"Ke {kd[0] * emission:.6f} {kd[1] * emission:.6f} {kd[2] * emission:.6f}\n")
            file.write(f"Ns {shininess:.6f}\n")
            file.write(f"Pr {roughness:.6f}\nPm {metallic:.6f}\n")
            file.write("Ni 1.450000\nd 1.000000\nillum 2\n")
            file.write(f"map_Kd {texture_name}\n")
            file.write(f"map_Bump {normal_name}\n")
            file.write(f"map_Pm {orm_name}\n\n")
    return textures


def convert_dds(paths: list[Path], *, srgb: bool) -> list[Path]:
    if not TEXCONV_PATH.is_file():
        raise RuntimeError(f"Texconv.exeが見つかりません: {TEXCONV_PATH}")
    dds_paths: list[Path] = []
    pixel_format = "BC7_UNORM_SRGB" if srgb else "BC7_UNORM"
    for path in paths:
        completed = subprocess.run(
            [str(TEXCONV_PATH), "-f", pixel_format, "-y", "-m", "0", "-o", str(path.parent), str(path)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if completed.returncode != 0:
            raise RuntimeError(f"DDS変換に失敗しました: {path.name}\n{completed.stdout}\n{completed.stderr}")
        dds_path = path.with_suffix(".dds")
        if not dds_path.is_file() or dds_path.read_bytes()[:4] != b"DDS ":
            raise RuntimeError(f"DDS出力が不正です: {dds_path}")
        dds_paths.append(dds_path)
    return dds_paths


def stable_guid(relative: str) -> str:
    return hashlib.md5(f"cg2:stage3-high-crown:{relative}".encode("utf-8")).hexdigest()


def write_meta(path: Path, asset_type: str, importer: str) -> None:
    relative = path.relative_to(PROJECT_ROOT).as_posix()
    data = {
        "assetType": asset_type,
        "guid": stable_guid(relative),
        "importSettings": {},
        "importer": importer,
        "source": relative,
        "version": 1,
    }
    Path(str(path) + ".meta").write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def render_preview(
    model: ObjBuilder,
    colors: dict[str, tuple[int, int, int]],
    output: Path,
    camera: Vec3,
    target: Vec3,
    focal: float,
) -> None:
    width, height = 920, 680
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image, "RGBA")
    forward = normalize(sub(target, camera))
    right = normalize(cross(forward, (0.0, 1.0, 0.0)))
    up = normalize(cross(right, forward))
    projected: list[tuple[float, float, float]] = []
    for vertex in model.vertices:
        relative = sub(vertex, camera)
        depth = dot(relative, forward)
        factor = focal / max(depth, 0.2)
        projected.append((width * 0.5 + dot(relative, right) * factor, height * 0.56 - dot(relative, up) * factor, depth))
    light = normalize((-0.45, 0.82, 0.38))
    triangles: list[tuple[float, str, tuple[int, int, int], float]] = []
    for material, faces in model.faces.items():
        for face in faces:
            normal = normalize(tuple(sum(model.normals[index - 1][axis] for index in face) for axis in range(3)))
            brightness = 0.48 + 0.52 * max(0.0, dot(normal, light))
            if "Gem" in material or "Glow" in material:
                brightness = 1.0
            depth = sum(projected[index - 1][2] for index in face) / 3.0
            triangles.append((depth, material, face, brightness))
    triangles.sort(key=lambda item: item[0], reverse=True)
    for _, material, face, brightness in triangles:
        base = colors[material]
        color = tuple(max(0, min(255, int(channel * brightness))) for channel in base) + (255,)
        draw.polygon([(projected[index - 1][0], projected[index - 1][1]) for index in face], fill=color)
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def validate_model(name: str, model: ObjBuilder, expected_materials: set[str]) -> None:
    missing = expected_materials - set(model.faces)
    if missing:
        raise RuntimeError(f"{name}のマテリアルが不足しています: {sorted(missing)}")
    if not model.vertices or not any(model.faces.values()):
        raise RuntimeError(f"{name}のジオメトリが空です。")


def main() -> None:
    parser = argparse.ArgumentParser(description="ステージ3 High Crownのモデルを生成します。")
    parser.add_argument("--preview-dir", type=Path)
    args = parser.parse_args()

    BOSS_DIR.mkdir(parents=True, exist_ok=True)
    STAGE_DIR.mkdir(parents=True, exist_ok=True)

    boss = build_false_king()
    crown = build_false_king_crown()
    arena = build_arena()
    wall = build_wall()
    tower = build_tower()
    gate = build_gate()
    gate_frame = build_gate_frame()
    step = build_step()
    throne = build_throne()
    validate_model("FalseKingSlime", boss, set(BOSS_COLORS))
    validate_model("FalseKingCrown", crown, {
        "CrownBlack", "CrownStone", "CrownGold", "CoreGlow",
        "GemRed", "GemBlue", "GemGreen", "GemViolet", "GemOrange", "GemCyan",
    })
    validate_model("Arena", arena, {
        "StoneDark", "Stone", "StoneLight", "PavingA", "PavingB",
        "Grass", "GrassLight", "Gold", "PurpleRune",
    })
    validate_model("Wall", wall, {
        "StoneDark", "Stone", "StoneLight",
    })
    validate_model("Tower", tower, {
        "StoneDark", "Stone", "StoneLight", "Banner", "Gold", "GateGlow", "FlameGlow",
    })
    validate_model("Gate", gate, {
        "GateBlack", "GateGlow",
    })
    validate_model("GateFrame", gate_frame, {
        "StoneDark", "Stone", "StoneLight", "Banner", "Gold", "GateGlow",
    })
    validate_model("Step", step, {"StoneDark", "StoneLight", "PavingA", "Grass", "Gold"})
    validate_model("Throne", throne, {
        "StoneDark", "Stone", "StoneLight", "PavingB", "Gold", "GateGlow",
    })

    boss_obj = BOSS_DIR / "false_king_slime.obj"
    crown_obj = BOSS_DIR / "false_king_crown.obj"
    stage_models = {
        "high_crown_arena": arena,
        "high_crown_wall": wall,
        "high_crown_tower": tower,
        "high_crown_gate": gate,
        "high_crown_gate_frame": gate_frame,
        "high_crown_step": step,
        "high_crown_throne": throne,
    }
    write_obj(boss, boss_obj, "false_king_slime.mtl", "FalseKingSlime")
    write_obj(crown, crown_obj, "false_king_slime.mtl", "FalseKingCrown")
    for name, model in stage_models.items():
        write_obj(model, STAGE_DIR / f"{name}.obj", "high_crown.mtl", name)

    boss_mtl = BOSS_DIR / "false_king_slime.mtl"
    stage_mtl = STAGE_DIR / "high_crown.mtl"
    boss_normal_png, boss_orm_png = create_surface_maps(BOSS_DIR, "false_king", "boss")
    stage_normal_png, stage_orm_png = create_surface_maps(STAGE_DIR, "high_crown", "stone")
    gate_normal_png, gate_orm_png = create_surface_maps(STAGE_DIR, "high_crown_gate", "gate")
    boss_pngs = write_mtl(
        boss_mtl, BOSS_COLORS, "false_king", BOSS_PBR,
        boss_normal_png.with_suffix(".dds").name,
        boss_orm_png.with_suffix(".dds").name,
    )
    stage_pngs = write_mtl(
        stage_mtl, STAGE_COLORS, "high_crown", STAGE_PBR,
        stage_normal_png.with_suffix(".dds").name,
        stage_orm_png.with_suffix(".dds").name,
    )
    boss_name_png = create_boss_name_sprite()
    color_dds_paths = convert_dds(boss_pngs + stage_pngs + [boss_name_png], srgb=True)
    support_pngs = [
        boss_normal_png, boss_orm_png,
        stage_normal_png, stage_orm_png,
        gate_normal_png, gate_orm_png,
    ]
    support_dds_paths = convert_dds(support_pngs, srgb=False)

    boss_dds_end = len(boss_pngs)
    stage_dds_end = boss_dds_end + len(stage_pngs)
    assets = [
        boss_obj, crown_obj, boss_mtl, *boss_pngs, *color_dds_paths[:boss_dds_end],
        stage_mtl, *stage_pngs, *color_dds_paths[boss_dds_end:stage_dds_end],
        boss_name_png, color_dds_paths[-1], *support_pngs, *support_dds_paths,
    ]
    assets.extend(STAGE_DIR / f"{name}.obj" for name in stage_models)
    for asset in assets:
        suffix = asset.suffix.lower()
        if suffix == ".obj":
            write_meta(asset, "Model", "ModelImporter")
        elif suffix == ".mtl":
            write_meta(asset, "Material", "MaterialImporter")
        else:
            write_meta(asset, "Texture", "TextureImporter")

    if args.preview_dir:
        render_preview(boss, BOSS_COLORS, args.preview_dir / "false_king_slime_preview.png", (8.0, 6.0, 10.5), (0.0, 2.2, 0.0), 720.0)
        render_preview(crown, BOSS_COLORS, args.preview_dir / "false_king_crown_preview.png", (7.0, 5.0, 9.5), (0.0, 1.2, 0.0), 720.0)
        render_preview(arena, STAGE_COLORS, args.preview_dir / "high_crown_arena_preview.png", (37.0, 31.0, 44.0), (0.0, 0.0, 0.0), 720.0)
        render_preview(wall, STAGE_COLORS, args.preview_dir / "high_crown_wall_preview.png", (8.5, 5.8, 10.5), (0.0, 1.8, 0.0), 720.0)
        render_preview(tower, STAGE_COLORS, args.preview_dir / "high_crown_tower_preview.png", (7.0, 6.8, 10.5), (0.0, 2.4, 0.0), 720.0)
        render_preview(gate, STAGE_COLORS, args.preview_dir / "high_crown_gate_preview.png", (12.0, 8.5, 17.0), (0.0, 3.6, 0.0), 720.0)
        render_preview(gate_frame, STAGE_COLORS, args.preview_dir / "high_crown_gate_frame_preview.png", (12.0, 8.5, 17.0), (0.0, 3.6, 0.0), 720.0)
        render_preview(step, STAGE_COLORS, args.preview_dir / "high_crown_step_preview.png", (15.0, 8.5, 24.0), (0.0, 0.1, 0.0), 720.0)

    print(f"Generated False King: {boss_obj.relative_to(PROJECT_ROOT)}")
    print(f"Generated High Crown models: {len(stage_models)}")
    print(
        f"Generated textures: {len(boss_pngs) + len(stage_pngs) + 1 + len(support_pngs)} PNG / "
        f"{len(color_dds_paths) + len(support_dds_paths)} DDS"
    )


if __name__ == "__main__":
    main()
