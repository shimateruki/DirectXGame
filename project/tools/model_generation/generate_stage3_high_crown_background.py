"""ステージ3「High Crown」の背景モデルと配置を生成します。

既存の闘技場、ボス、ゲート、報酬には触れず、王城基壇、雲海、遠景城郭を
背景専用Objectとして追加します。背景Objectは当たり判定を持ちません。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import uuid
from copy import deepcopy
from pathlib import Path

from PIL import Image, ImageDraw

from generate_ring_burner_model import ObjBuilder, cross, dot, normalize, sub


PROJECT_ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = PROJECT_ROOT / "Resources" / "3DModel" / "Stages" / "high_crown_background"
SCENE_PATH = PROJECT_ROOT / "Resources" / "json" / "3Dobject" / "stage3_object.json"
LIGHT_PATH = PROJECT_ROOT / "Resources" / "json" / "light" / "stage3_high_crown.json"
SKYBOX_PATH = "Resources/skybox/stage3_high_crown.dds"
TEXCONV_PATH = PROJECT_ROOT / "Resources" / "tools" / "Texconv.exe"
STAGE_NORMAL_MAP = "Resources/3DModel/Stages/high_crown/high_crown_surface_normal.dds"
STAGE_ORM_MAP = "Resources/3DModel/Stages/high_crown/high_crown_surface_orm.dds"
GUID_NAMESPACE = uuid.UUID("f57217dc-c5d0-4bdf-85bf-99fbbde30131")

Vec3 = tuple[float, float, float]

MATERIALS = {
    "BackdropStoneDark": ((108, 122, 154), 0.72, 0.02, 0.0),
    "BackdropStone": ((168, 181, 207), 0.68, 0.01, 0.0),
    "BackdropStoneLight": ((225, 230, 240), 0.62, 0.01, 0.0),
    "BackdropGold": ((244, 190, 72), 0.30, 0.72, 0.03),
    "BackdropRune": ((174, 84, 220), 0.24, 0.04, 0.22),
    "BackdropBanner": ((112, 58, 152), 0.76, 0.0, 0.0),
    "CloudLight": ((238, 247, 253), 0.96, 0.0, 0.04),
    "CloudShade": ((194, 220, 240), 0.98, 0.0, 0.015),
}


def stable_guid(name: str) -> str:
    return str(uuid.uuid5(GUID_NAMESPACE, f"stage3-high-crown-background:{name}"))


def asset_guid(path: Path) -> str:
    relative = path.relative_to(PROJECT_ROOT).as_posix()
    return hashlib.md5(f"cg2:stage3-high-crown-background:{relative}".encode("utf-8")).hexdigest()


def add_elliptic_cylinder(
    model: ObjBuilder,
    material_side: str,
    material_top: str,
    center: Vec3,
    radius_x: float,
    radius_z: float,
    height: float,
    segments: int,
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
        cosine = math.cos(angle)
        sine = math.sin(angle)
        radial = normalize((cosine / radius_x, 0.0, sine / radius_z))
        x = center[0] + cosine * radius_x
        z = center[2] + sine * radius_z
        side_bottom.append(model.vertex((x, bottom_y, z), (index / segments, 1.0), radial))
        side_top.append(model.vertex((x, top_y, z), (index / segments, 0.0), radial))
        cap_top.append(model.vertex((x, top_y, z), (0.5 + cosine * 0.5, 0.5 - sine * 0.5), (0.0, 1.0, 0.0)))
        cap_bottom.append(model.vertex((x, bottom_y, z), (0.5 + cosine * 0.5, 0.5 + sine * 0.5), (0.0, -1.0, 0.0)))
    for index in range(segments):
        nxt = (index + 1) % segments
        model.face(material_side, side_bottom[index], side_bottom[nxt], side_top[nxt])
        model.face(material_side, side_bottom[index], side_top[nxt], side_top[index])
        model.face(material_top, top_center, cap_top[index], cap_top[nxt], False)
        model.face(material_side, bottom_center, cap_bottom[nxt], cap_bottom[index], False)


def add_elliptic_frustum(
    model: ObjBuilder,
    material_side: str,
    material_top: str,
    center: Vec3,
    top_radius_x: float,
    top_radius_z: float,
    bottom_radius_x: float,
    bottom_radius_z: float,
    height: float,
    segments: int,
) -> None:
    bottom_y = center[1] - height * 0.5
    top_y = center[1] + height * 0.5
    top_center = model.vertex((center[0], top_y, center[2]), (0.5, 0.5), (0.0, 1.0, 0.0))
    bottom_center = model.vertex((center[0], bottom_y, center[2]), (0.5, 0.5), (0.0, -1.0, 0.0))
    side_bottom: list[int] = []
    side_top: list[int] = []
    cap_top: list[int] = []
    cap_bottom: list[int] = []
    slope = (max(top_radius_x, top_radius_z) - max(bottom_radius_x, bottom_radius_z)) / max(height, 0.001)
    for index in range(segments):
        angle = math.tau * index / segments
        cosine = math.cos(angle)
        sine = math.sin(angle)
        normal = normalize((cosine / max(top_radius_x, 0.001), slope, sine / max(top_radius_z, 0.001)))
        top = (center[0] + cosine * top_radius_x, top_y, center[2] + sine * top_radius_z)
        bottom = (center[0] + cosine * bottom_radius_x, bottom_y, center[2] + sine * bottom_radius_z)
        side_bottom.append(model.vertex(bottom, (index / segments, 1.0), normal))
        side_top.append(model.vertex(top, (index / segments, 0.0), normal))
        cap_top.append(model.vertex(top, (0.5 + cosine * 0.5, 0.5 - sine * 0.5), (0.0, 1.0, 0.0)))
        cap_bottom.append(model.vertex(bottom, (0.5 + cosine * 0.5, 0.5 + sine * 0.5), (0.0, -1.0, 0.0)))
    for index in range(segments):
        nxt = (index + 1) % segments
        model.face(material_side, side_bottom[index], side_bottom[nxt], side_top[nxt])
        model.face(material_side, side_bottom[index], side_top[nxt], side_top[index])
        model.face(material_top, top_center, cap_top[index], cap_top[nxt], False)
        model.face(material_side, bottom_center, cap_bottom[nxt], cap_bottom[index], False)


def add_vertical_crystal(model: ObjBuilder, material: str, center: Vec3, radius: float, height: float) -> None:
    lower = model.vertex((center[0], center[1] - height * 0.42, center[2]), (0.5, 1.0), (0.0, -1.0, 0.0))
    upper = model.vertex((center[0], center[1] + height * 0.58, center[2]), (0.5, 0.0), (0.0, 1.0, 0.0))
    ring: list[int] = []
    for index in range(5):
        angle = math.tau * index / 5
        radial = (math.cos(angle), 0.0, math.sin(angle))
        ring.append(model.vertex(
            (center[0] + radial[0] * radius, center[1], center[2] + radial[2] * radius),
            (index / 5, 0.5),
            radial,
        ))
    for index in range(5):
        nxt = (index + 1) % 5
        model.face(material, ring[index], upper, ring[nxt], False)
        model.face(material, ring[nxt], lower, ring[index], False)


def add_cloud_sea_surface(model: ObjBuilder, radius_x: float, radius_z: float) -> None:
    """上面が一枚板に見えない、緩やかな起伏を持つ楕円形の雲海を作ります。"""
    ring_count = 18
    segment_count = 96
    rings: list[list[int]] = []

    for ring_index in range(ring_count + 1):
        radius_rate = ring_index / ring_count
        ring: list[int] = []
        for segment_index in range(segment_count):
            angle = math.tau * segment_index / segment_count
            edge_noise = 1.0 + radius_rate * (
                0.018 * math.sin(angle * 5.0 + 0.7)
                + 0.012 * math.sin(angle * 11.0 - 0.4)
            )
            x = math.cos(angle) * radius_x * radius_rate * edge_noise
            z = math.sin(angle) * radius_z * radius_rate * edge_noise
            broad_wave = (
                math.sin(x * 0.031 + z * 0.018) * 0.90
                + math.cos(z * 0.027 - x * 0.014) * 0.68
                + math.sin((x + z) * 0.015 + 1.2) * 0.42
            )
            edge_sink = max(0.0, radius_rate - 0.82) * 5.5
            y = -46.2 + broad_wave - edge_sink

            slope_x = 0.031 * 0.90 * math.cos(x * 0.031 + z * 0.018)
            slope_x += 0.014 * 0.68 * math.sin(z * 0.027 - x * 0.014)
            slope_z = 0.018 * 0.90 * math.cos(x * 0.031 + z * 0.018)
            slope_z -= 0.027 * 0.68 * math.sin(z * 0.027 - x * 0.014)
            normal = normalize((-slope_x, 1.0, -slope_z))
            uv = (0.5 + x / (radius_x * 2.0), 0.5 - z / (radius_z * 2.0))
            ring.append(model.vertex((x, y, z), uv, normal))
        rings.append(ring)

    for ring_index in range(ring_count):
        inner = rings[ring_index]
        outer = rings[ring_index + 1]
        for segment_index in range(segment_count):
            nxt = (segment_index + 1) % segment_count
            model.face("CloudLight", inner[segment_index], outer[segment_index], outer[nxt], False)
            model.face("CloudLight", inner[segment_index], outer[nxt], inner[nxt], False)

def build_foundation() -> ObjBuilder:
    model = ObjBuilder()
    # 闘技場の下面へ食い込ませた三段城塞で、円盤ではなく王城の頂上として見せます。
    add_elliptic_frustum(model, "BackdropStone", "BackdropStoneLight", (0.0, -12.0, 0.0), 41.2, 35.2, 35.0, 29.6, 12.0, 80)
    add_elliptic_frustum(model, "BackdropStoneDark", "BackdropStone", (0.0, -23.0, 0.0), 35.0, 29.6, 27.0, 22.2, 10.0, 72)
    add_elliptic_frustum(model, "BackdropStone", "BackdropStoneDark", (0.0, -36.0, 0.0), 27.0, 22.2, 15.5, 12.5, 16.0, 64)
    add_elliptic_frustum(model, "BackdropStoneDark", "BackdropStoneDark", (0.0, -48.0, 0.0), 15.5, 12.5, 8.0, 6.5, 8.0, 48)
    add_elliptic_cylinder(model, "BackdropGold", "BackdropGold", (0.0, -8.90, 0.0), 41.35, 35.35, 0.48, 80)
    add_elliptic_cylinder(model, "BackdropStoneLight", "BackdropStoneLight", (0.0, -17.18, 0.0), 35.35, 29.90, 0.64, 72)
    add_elliptic_cylinder(model, "BackdropGold", "BackdropGold", (0.0, -28.18, 0.0), 27.28, 22.45, 0.42, 64)

    # 階段の下へ橋台を伸ばし、開始地点まで同じ建築物としてつなげます。
    model.box("BackdropStoneDark", (0.0, -10.2, 49.5), (8.20, 7.70, 18.0))
    model.box("BackdropStone", (0.0, -2.76, 49.5), (7.55, 0.34, 18.1))
    model.box("BackdropStoneLight", (0.0, -17.70, 49.5), (8.55, 0.28, 18.25))
    for z in (35.0, 43.0, 51.0, 59.0, 67.0):
        for x in (-7.68, 7.68):
            model.box("BackdropStone", (x, -9.6, z), (0.44, 6.95, 1.05))
            model.box("BackdropGold", (x * 1.06, -5.1, z), (0.12, 1.55, 0.62))

    # 放射状の控え壁は上部を太くし、雲海まで荷重が流れる輪郭にします。
    for index in range(16):
        angle = math.tau * index / 16
        radius_x = 35.8 if index % 2 == 0 else 33.8
        radius_z = 30.2 if index % 2 == 0 else 28.5
        x = math.cos(angle) * radius_x
        z = math.sin(angle) * radius_z
        add_vertical_crystal(
            model,
            "BackdropStoneLight" if index % 2 == 0 else "BackdropStoneDark",
            (x, -18.5 - (index % 3) * 1.1, z),
            1.45 if index % 2 == 0 else 1.15,
            17.0 if index % 2 == 0 else 13.0,
        )
        if index % 4 == 0:
            add_vertical_crystal(model, "BackdropRune", (x, -10.1, z), 0.38, 1.55)

    # 雲海まで届く橋脚で階段下の空白を埋め、導入路を城塞の一部として支えます。
    for z in (36.5, 47.0, 57.5, 67.0):
        for x in (-6.25, 6.25):
            model.box("BackdropStoneDark", (x, -27.0, z), (1.85, 24.5, 2.10))
            model.box("BackdropStone", (x, -5.35, z), (2.25, 0.42, 2.50))
            model.box("BackdropStoneLight", (x, -28.0, z), (0.24, 18.0, 2.24))
            model.box("BackdropGold", (x * 1.04, -15.5, z + 2.18), (0.14, 2.45, 0.18))
        model.box("BackdropStone", (0.0, -13.0, z), (6.20, 0.48, 1.35))
        model.box("BackdropStoneDark", (0.0, -34.5, z), (6.20, 0.42, 1.35))
    return model


def build_cloud_sea() -> ObjBuilder:
    model = ObjBuilder()
    add_cloud_sea_surface(model, 220.0, 188.0)

    # 扁平な円模様に見えないよう、少数の大きな雲丘を雲海へ半分沈めて厚みを作ります。
    cloud_rings = ((70.0, 58.0, 10, 18.0), (122.0, 102.0, 12, 24.0), (174.0, 148.0, 14, 30.0))
    for ring_index, (radius_x, radius_z, count, puff_size) in enumerate(cloud_rings):
        for index in range(count):
            angle = math.tau * index / count + ring_index * 0.23
            radial_noise = 1.0 + 0.07 * math.sin(index * 2.37 + ring_index)
            x = math.cos(angle) * radius_x * radial_noise
            z = math.sin(angle) * radius_z * radial_noise
            height = 7.5 + (index % 4) * 1.15 + ring_index * 0.65
            y = -46.0 + math.sin(index * 1.71 + ring_index * 0.8) * 1.20
            width = puff_size * (0.84 + 0.17 * math.sin(index * 1.13 + 0.4))
            depth = puff_size * (0.66 + 0.13 * math.cos(index * 0.93))
            material = "CloudShade" if (index + ring_index * 2) % 9 == 0 else "CloudLight"
            model.sphere(material, (x, y, z), (width, height, depth), 16, 9)
    return model


def build_cloud_anchor() -> ObjBuilder:
    # Cloudシェーダーは描画時に専用板ポリへ差し替えるため、読込確認用の最小モデルだけを持たせます。
    model = ObjBuilder()
    model.box("CloudLight", (0.0, 0.0, 0.0), (0.5, 0.5, 0.05))
    return model


def build_horizon_citadel() -> ObjBuilder:
    model = ObjBuilder()
    cloud_base_y = -47.0
    peak_layout = (
        (-118.0, -108.0, 28.0, 22.0, 45.0, 0),
        (0.0, -150.0, 43.0, 31.0, 56.0, 2),
        (118.0, -104.0, 31.0, 23.0, 43.0, 1),
        (-154.0, 22.0, 37.0, 28.0, 40.0, 1),
        (158.0, 30.0, 35.0, 26.0, 42.0, 0),
        (-92.0, 128.0, 29.0, 22.0, 36.0, 0),
        (96.0, 132.0, 26.0, 20.0, 34.0, 1),
    )
    for peak_index, (x, z, radius_x, radius_z, height, tower_level) in enumerate(peak_layout):
        top_y = cloud_base_y + height
        add_elliptic_frustum(
            model,
            "BackdropStoneDark",
            "BackdropStone",
            (x, cloud_base_y + height * 0.50, z),
            radius_x * 0.24,
            radius_z * 0.24,
            radius_x,
            radius_z,
            height,
            12,
        )

        # 単純な円錐に見えないよう、左右非対称の岩肩を重ねて自然な城山の輪郭を作ります。
        shoulder_layout = (
            (-0.34, 0.08, 0.70, 0.54, 0.72),
            (0.31, -0.11, 0.62, 0.60, 0.64),
            (0.02, 0.34, 0.54, 0.48, 0.57),
        )
        for shoulder_index, (offset_x_rate, offset_z_rate, width_rate, depth_rate, height_rate) in enumerate(shoulder_layout):
            shoulder_height = height * height_rate
            add_elliptic_frustum(
                model,
                "BackdropStone" if shoulder_index % 2 == 0 else "BackdropStoneDark",
                "BackdropStoneLight" if shoulder_index == 2 else "BackdropStone",
                (
                    x + radius_x * offset_x_rate,
                    cloud_base_y + shoulder_height * 0.50,
                    z + radius_z * offset_z_rate,
                ),
                radius_x * width_rate * 0.16,
                radius_z * depth_rate * 0.16,
                radius_x * width_rate,
                radius_z * depth_rate,
                shoulder_height,
                9,
            )
        add_elliptic_frustum(model, "BackdropStone", "BackdropStoneLight", (x, top_y - 2.0, z), radius_x * 0.28, radius_z * 0.28, radius_x * 0.42, radius_z * 0.42, 4.0, 12)
        add_elliptic_cylinder(model, "BackdropStoneDark", "BackdropStoneLight", (x, top_y + 0.25, z), radius_x * 0.29, radius_z * 0.29, 0.50, 16)
        add_elliptic_cylinder(model, "BackdropGold", "BackdropGold", (x, top_y + 0.58, z), radius_x * 0.30, radius_z * 0.30, 0.18, 16)

        # 遠景では塔を少数の太いシルエットにし、城郭だと一目で分かる形を優先します。
        tower_count = 1 + tower_level * 2
        for tower_index in range(tower_count):
            offset_x = 0.0 if tower_count == 1 else (tower_index - (tower_count - 1) * 0.5) * min(9.0, radius_x * 0.34)
            is_center = tower_index == tower_count // 2
            half_width = 2.8 if tower_level < 2 else (3.3 if is_center else 2.6)
            half_height = 5.0 + tower_level * 1.8 + (2.4 if tower_level == 2 and is_center else 0.0)
            tower_y = top_y + 0.84 + half_height
            model.box("BackdropStoneDark", (x + offset_x, tower_y, z), (half_width, half_height, half_width))
            model.box("BackdropStoneLight", (x + offset_x, tower_y + half_height + 0.28, z), (half_width + 0.55, 0.28, half_width + 0.55))
            model.box("BackdropGold", (x + offset_x, tower_y + half_height - 1.25, z + half_width + 0.08), (half_width * 0.72, 0.20, 0.12))
            add_vertical_crystal(
                model,
                "BackdropRune" if tower_index % 2 == 0 else "BackdropStoneLight",
                (x + offset_x, tower_y + half_height + 2.25, z),
                half_width * 0.46,
                3.8 + tower_level * 0.8,
            )

        # 複数塔は低い城壁でつなぎ、孤立した箱ではなく遠景の砦として読ませます。
        if tower_count > 1:
            wall_half_width = min(11.0, radius_x * 0.42)
            wall_y = top_y + 2.55
            model.box(
                "BackdropStone",
                (x, wall_y, z),
                (wall_half_width, 1.65, 1.05),
            )
            model.box(
                "BackdropStoneLight",
                (x, wall_y + 1.82, z),
                (wall_half_width + 0.55, 0.17, 1.18),
            )
            for merlon_x in (-wall_half_width * 0.72, 0.0, wall_half_width * 0.72):
                model.box(
                    "BackdropGold",
                    (x + merlon_x, wall_y + 2.42, z + 1.12),
                    (0.42, 0.42, 0.16),
                )

        if peak_index in (0, 1, 2):
            model.box("BackdropBanner", (x, top_y + 4.4, z + radius_z * 0.30), (2.6 + tower_level, 2.8, 0.12))
    return model


def build_entrance_citadel() -> ObjBuilder:
    model = ObjBuilder()
    # 開始地点を城門前庭として造形し、階段が空中で途切れて見えない終点を作ります。
    add_elliptic_frustum(
        model,
        "BackdropStoneDark",
        "BackdropStone",
        (0.0, -23.0, 75.0),
        19.0,
        16.0,
        12.0,
        10.0,
        38.0,
        48,
    )
    model.box("BackdropStoneDark", (0.0, -4.10, 74.0), (18.5, 2.10, 16.5))
    model.box("BackdropStoneLight", (0.0, -1.82, 74.0), (18.9, 0.18, 16.9))
    model.box("BackdropGold", (0.0, -1.42, 57.35), (19.0, 0.22, 0.20))
    model.box("BackdropGold", (-18.72, -1.42, 74.0), (0.20, 0.22, 16.9))
    model.box("BackdropGold", (18.72, -1.42, 74.0), (0.20, 0.22, 16.9))

    # 正面の双塔と門扉で、ここが雲上王城の入口だと画角内で読めるシルエットにします。
    for side in (-1.0, 1.0):
        tower_x = side * 11.4
        model.box("BackdropStoneDark", (tower_x, 5.8, 84.2), (4.2, 7.8, 4.4))
        model.box("BackdropStone", (tower_x, 13.92, 84.2), (4.75, 0.32, 4.95))
        model.box("BackdropGold", (tower_x, 14.42, 84.2), (4.95, 0.18, 5.15))
        for merlon_x in (-2.8, 0.0, 2.8):
            model.box("BackdropStoneLight", (tower_x + merlon_x, 15.52, 84.2), (0.62, 0.92, 0.82))
        add_vertical_crystal(model, "BackdropRune", (tower_x, 18.15, 84.2), 0.62, 3.4)
        model.box("BackdropBanner", (tower_x, 6.2, 79.72), (1.45, 3.2, 0.14))

    model.box("BackdropStoneDark", (0.0, 4.9, 87.3), (5.15, 6.8, 0.62))
    model.box("BackdropStone", (-6.25, 5.4, 87.2), (1.15, 7.35, 1.15))
    model.box("BackdropStone", (6.25, 5.4, 87.2), (1.15, 7.35, 1.15))
    model.box("BackdropStoneLight", (0.0, 12.65, 87.2), (7.45, 0.55, 1.35))
    model.box("BackdropGold", (0.0, 13.48, 87.0), (7.75, 0.24, 1.52))
    add_vertical_crystal(model, "BackdropRune", (0.0, 17.4, 87.0), 1.05, 5.2)

    # 前庭の縁と下面にも連続した柱を置き、薄い板ではなく重量のある建築に見せます。
    for side in (-1.0, 1.0):
        model.box("BackdropStone", (side * 17.25, 0.35, 71.5), (0.72, 2.0, 13.6))
        for z in (61.0, 71.0, 81.0):
            model.box("BackdropStoneDark", (side * 15.6, -22.0, z), (2.15, 18.0, 2.15))
            model.box("BackdropStoneLight", (side * 15.6, -5.0, z), (2.55, 0.45, 2.55))
            model.box("BackdropGold", (side * 15.6, -15.0, z + 2.23), (1.28, 0.22, 0.14))
    return model

def create_texture(path: Path, material: str, color: tuple[int, int, int], seed: int) -> None:
    size = 256
    image = Image.new("RGBA", (size, size), (*color, 255))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            hashed = ((x * 73856093) ^ (y * 19349663) ^ (seed * 83492791)) & 255
            grain = hashed / 255.0 - 0.5
            broad = math.sin(x * 0.031 + y * 0.027 + seed * 0.71)
            rate = 0.90 + grain * 0.12 + broad * 0.05
            if "Stone" in material:
                row = y // 64
                shifted_x = (x + (32 if row % 2 else 0)) % 64
                if y % 64 < 4 or shifted_x < 4:
                    rate *= 0.73
                if ((x * 5 + y * 11 + seed * 17) % 127) < 2:
                    rate *= 0.82
            elif "Cloud" in material:
                swirl = math.sin(x * 0.022 + math.sin(y * 0.018 + seed) * 1.8)
                soft = math.sin((x + y) * 0.011 + seed * 0.4)
                rate = 0.94 + swirl * 0.055 + soft * 0.035 + grain * 0.025
            elif "Gold" in material:
                rate += 0.10 if (x + seed * 13) % 47 < 2 else 0.0
            rate = max(0.55, min(1.10, rate))
            pixels[x, y] = tuple(min(255, int(channel * rate)) for channel in color) + (255,)
    image.save(path)


def write_mtl(path: Path) -> list[Path]:
    textures: list[Path] = []
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("# CG2 Stage 3 High Crown background materials\n\n")
        for index, (name, (color, roughness, metallic, emission)) in enumerate(MATERIALS.items()):
            texture = OUT_DIR / f"high_crown_background_{name.lower()}.png"
            create_texture(texture, name, color, index + 1)
            textures.append(texture)
            diffuse = tuple(channel / 255.0 for channel in color)
            stream.write(f"newmtl {name}\n")
            stream.write(f"Ka {diffuse[0] * 0.34:.6f} {diffuse[1] * 0.34:.6f} {diffuse[2] * 0.34:.6f}\n")
            stream.write(f"Kd {diffuse[0]:.6f} {diffuse[1]:.6f} {diffuse[2]:.6f}\n")
            stream.write("Ks 0.720000 0.760000 0.840000\n")
            stream.write(f"Ke {diffuse[0] * emission:.6f} {diffuse[1] * emission:.6f} {diffuse[2] * emission:.6f}\n")
            stream.write(f"Ns {max(1.0, (1.0 - roughness) * 128.0):.6f}\n")
            stream.write(f"Pr {roughness:.6f}\nPm {metallic:.6f}\nNi 1.450000\nd 1.000000\nillum 2\n")
            stream.write(f"map_Kd {texture.with_suffix('.dds').name}\n")
            if "Cloud" not in name:
                stream.write("map_Bump ../high_crown/high_crown_surface_normal.dds\n")
                stream.write("map_Pm ../high_crown/high_crown_surface_orm.dds\n")
            stream.write("\n")
    return textures


def convert_dds(paths: list[Path]) -> list[Path]:
    if not TEXCONV_PATH.is_file():
        raise RuntimeError(f"Texconv.exeが見つかりません: {TEXCONV_PATH}")
    output: list[Path] = []
    for path in paths:
        completed = subprocess.run(
            [str(TEXCONV_PATH), "-f", "BC7_UNORM_SRGB", "-y", "-m", "0", "-o", str(path.parent), str(path)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if completed.returncode != 0:
            raise RuntimeError(f"DDS変換に失敗しました: {path.name}\n{completed.stdout}\n{completed.stderr}")
        dds = path.with_suffix(".dds")
        if not dds.is_file() or dds.read_bytes()[:4] != b"DDS ":
            raise RuntimeError(f"DDS出力が不正です: {dds}")
        output.append(dds)
    return output


def write_obj(path: Path, name: str, model: ObjBuilder) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(f"# CG2 Stage 3 High Crown background\nmtllib high_crown_background.mtl\no {name}\n")
        for value in model.vertices:
            stream.write(f"v {value[0]:.7f} {value[1]:.7f} {value[2]:.7f}\n")
        for value in model.uvs:
            stream.write(f"vt {value[0]:.7f} {value[1]:.7f}\n")
        for value in model.normals:
            stream.write(f"vn {value[0]:.7f} {value[1]:.7f} {value[2]:.7f}\n")
        for material, faces in model.faces.items():
            stream.write(f"usemtl {material}\n")
            stream.write("s 1\n" if material in model.smooth_materials else "s off\n")
            for a, b, c in faces:
                stream.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


def write_meta(path: Path, asset_type: str, importer: str) -> None:
    relative = path.relative_to(PROJECT_ROOT).as_posix()
    payload = {
        "assetType": asset_type,
        "guid": asset_guid(path),
        "importSettings": {"generateTangents": True, "scale": 1} if asset_type == "Model" else {},
        "importer": importer,
        "source": relative,
        "version": 1,
    }
    Path(str(path) + ".meta").write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def model_bounds(model: ObjBuilder) -> tuple[Vec3, Vec3]:
    return (
        tuple(min(vertex[axis] for vertex in model.vertices) for axis in range(3)),
        tuple(max(vertex[axis] for vertex in model.vertices) for axis in range(3)),
    )


def make_background_object(template: dict, name: str, model_name: str, *, clouds: bool) -> dict:
    item = deepcopy(template)
    item.update({
        "name": name,
        "guid": stable_guid(name),
        "modelName": model_name,
        "position": [0.0, 0.0, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "quaternion": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0],
        "collider": {"center": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "size": [1.0, 1.0, 1.0], "type": 0},
        "collisionAttribute": 0,
        "collisionMask": 0,
        "castShadow": not clouds,
        "isStatic": True,
        "materialType": 0,
        "normalMapPath": "" if clouds else STAGE_NORMAL_MAP,
        "ormMapPath": "" if clouds else STAGE_ORM_MAP,
        "enableNormalMap": not clouds,
        "enableEnvMap": not clouds,
        "envIntensity": 0.12 if clouds else 0.18,
        "metallic": 0.0 if clouds else 0.03,
        "roughness": 0.96 if clouds else 0.72,
        "emissive": 1.04 if clouds else 1.0,
        "textureTiling": [1.0, 1.0],
        "autoTextureTiling": False,
        "parentGuid": "",
        "parentName": "",
    })
    return item


def cloud_shader_specs() -> list[dict]:
    """闘技場の外周を包み、遠景まで連続して見えるCloudシェーダー層を返します。"""
    specs: list[dict] = []
    rings = (
        (68.0, 56.0, 8, 19.0, -38.5, 0),
        (124.0, 104.0, 10, 28.0, -41.0, -1),
        (180.0, 152.0, 8, 40.0, -43.0, 1),
    )
    for ring_index, (radius_x, radius_z, count, base_size, base_y, fixed_mode) in enumerate(rings):
        for index in range(count):
            angle = math.tau * index / count + ring_index * 0.27
            radial_noise = 1.0 + 0.055 * math.sin(index * 2.31 + ring_index * 0.8)
            size = base_size * (0.88 + 0.15 * math.sin(index * 1.37 + ring_index))
            mode = fixed_mode if fixed_mode >= 0 else (0 if index % 3 else 1)
            prefix = "CloudPuff" if mode == 0 else "CloudDrift"
            alpha = 0.90 - ring_index * 0.08
            specs.append({
                "name": f"{prefix}_{ring_index + 1}_{index + 1:02d}",
                "position": [
                    round(math.cos(angle) * radius_x * radial_noise, 4),
                    round(base_y + math.sin(index * 1.61 + ring_index) * 1.6, 4),
                    round(math.sin(angle) * radius_z * radial_noise, 4),
                ],
                "size": round(size, 4),
                "mode": mode,
                "color": [0.90 - ring_index * 0.035, 0.955, 1.0, alpha],
                "speed": 0.14 + ((index + ring_index * 2) % 5) * 0.025,
                "density": 2.75 - ring_index * 0.18,
                "detail": 5.0 + ((index + ring_index) % 4) * 0.65,
                "softness": 0.72 + ring_index * 0.05,
                "intensity": 1.18 - ring_index * 0.08,
            })
    return specs


def make_cloud_shader_object(template: dict, spec: dict) -> dict:
    """既存Cloudシェーダーを使う、Editor調整可能な雲塊Objectを作ります。"""
    item = deepcopy(template)
    size = spec["size"]
    mode = spec["mode"]
    name = f"Stage3_HighCrown_Background_{spec['name']}"
    item.update({
        "name": name,
        "guid": stable_guid(name),
        "modelName": "Stages/high_crown_background/high_crown_cloud_anchor.obj",
        "position": spec["position"],
        "rotation": [0.0, 0.0, 0.0],
        "quaternion": [0.0, 0.0, 0.0, 1.0],
        "scale": [size, size, size],
        "collider": {"center": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "size": [1.0, 1.0, 1.0], "type": 0},
        "collisionAttribute": 0,
        "collisionMask": 0,
        "castShadow": False,
        "isStatic": True,
        "blendMode": 1,
        "materialType": 21,
        "color": spec["color"],
        "enableLighting": False,
        "enableNormalMap": False,
        "enableEnvMap": False,
        "normalMapPath": "",
        "ormMapPath": "",
        "envIntensity": 0.0,
        "metallic": 0.0,
        "roughness": 1.0,
        "emissive": 1.0,
        "texturePath": "",
        "textureTiling": [1.0, 1.0],
        "autoTextureTiling": False,
        "parentGuid": "",
        "parentName": "",
        "waterParam": {
            "waveSpeed": spec["speed"],
            "waveHeight": spec["density"],
            "waveFrequency": spec["detail"],
            "flowSpeedX": 0.10 + (0.04 if mode == 1 else 0.0),
            "flowSpeedY": -0.035,
            "effectType": float(mode),
            "effectScale": 1.0,
            "effectScaleX": 1.25 if mode == 1 else 1.0,
            "effectScaleY": 0.68 if mode == 1 else 1.0,
            "effectScaleZ": 1.0,
            "effectSoftness": spec["softness"],
            "effectIntensity": spec["intensity"],
            "billboardScale": 1.0,
        },
    })
    return item

def update_scene() -> list[dict]:
    data = json.loads(SCENE_PATH.read_text(encoding="utf-8"))
    objects = data.get("objects", [])
    template = next((item for item in objects if item.get("name") == "Stage3_HighCrown_ArenaVisual"), None)
    if template is None:
        raise RuntimeError("ステージ3の闘技場Objectが見つかりません。")
    prefix = "Stage3_HighCrown_Background_"
    objects = [item for item in objects if not item.get("name", "").startswith(prefix)]
    backgrounds = [
        make_background_object(template, f"{prefix}Foundation", "Stages/high_crown_background/high_crown_foundation.obj", clouds=False),
        make_background_object(template, f"{prefix}CloudSea", "Stages/high_crown_background/high_crown_cloud_sea.obj", clouds=True),
        make_background_object(template, f"{prefix}HorizonCitadel", "Stages/high_crown_background/high_crown_horizon_citadel.obj", clouds=False),
        make_background_object(template, f"{prefix}EntranceCitadel", "Stages/high_crown_background/high_crown_entrance_citadel.obj", clouds=False),
        *(make_cloud_shader_object(template, spec) for spec in cloud_shader_specs()),
    ]
    arena_index = next(index for index, item in enumerate(objects) if item.get("name") == "Stage3_HighCrown_ArenaVisual")
    objects[arena_index + 1:arena_index + 1] = backgrounds
    data["objects"] = objects
    SCENE_PATH.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8", newline="\n")
    return backgrounds


def update_light() -> None:
    data = json.loads(LIGHT_PATH.read_text(encoding="utf-8"))
    data["clearColor"] = [0.38, 0.61, 0.84, 1.0]
    data["skybox"] = {"enabled": True, "texture": SKYBOX_PATH}
    data.setdefault("shadow", {})["areaSize"] = 150.0
    directional = data.setdefault("directionalLight", {})
    directional.update({
        "ambientColor": [0.40, 0.45, 0.56],
        "enableFog": 1,
        "fogColor": [0.58, 0.76, 0.95],
        "fogStart": 105.0,
        "fogEnd": 330.0,
        "fogHeightMin": -62.0,
        "fogHeightMax": 48.0,
        "volumetricIntensity": 0.055,
        "volumetricSteps": 24,
    })
    LIGHT_PATH.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8", newline="\n")


def validate(models: dict[str, ObjBuilder], backgrounds: list[dict]) -> None:
    expected = {
        "foundation": {"BackdropStoneDark", "BackdropStone", "BackdropStoneLight", "BackdropGold", "BackdropRune"},
        "cloud_sea": {"CloudLight", "CloudShade"},
        "cloud_anchor": {"CloudLight"},
        "horizon_citadel": {"BackdropStoneDark", "BackdropStone", "BackdropStoneLight", "BackdropGold", "BackdropRune", "BackdropBanner"},
        "entrance_citadel": {"BackdropStoneDark", "BackdropStone", "BackdropStoneLight", "BackdropGold", "BackdropRune", "BackdropBanner"},
    }
    for name, model in models.items():
        missing = expected[name] - set(model.faces)
        if missing or not model.vertices or not any(model.faces.values()):
            raise RuntimeError(f"{name}のモデル構成が不正です: missing={sorted(missing)}")
    foundation_min, foundation_max = model_bounds(models["foundation"])
    cloud_min, cloud_max = model_bounds(models["cloud_sea"])
    horizon_min, horizon_max = model_bounds(models["horizon_citadel"])
    if not (-3.2 <= foundation_max[1] <= -2.3) or foundation_min[1] > -50.0:
        raise RuntimeError(f"基壇が闘技場へ接続していません: {foundation_min} - {foundation_max}")
    if max(abs(cloud_min[0]), abs(cloud_max[0])) < 200.0 or cloud_max[1] > -31.0:
        raise RuntimeError(f"雲海の範囲または高さが不正です: {cloud_min} - {cloud_max}")
    if cloud_max[1] - cloud_min[1] < 16.0:
        raise RuntimeError(f"雲海モデルに十分な厚みがありません: {cloud_min} - {cloud_max}")
    if max(abs(horizon_min[0]), abs(horizon_max[0]), abs(horizon_min[2]), abs(horizon_max[2])) < 140.0 or horizon_max[1] < 20.0:
        raise RuntimeError(f"遠景城郭の範囲が不正です: {horizon_min} - {horizon_max}")

    cloud_specs = cloud_shader_specs()
    cloud_layers = [item for item in backgrounds if item.get("materialType") == 21]
    if len(backgrounds) != 4 + len(cloud_specs):
        raise RuntimeError(f"背景Object数が不正です: {len(backgrounds)}")
    if len(cloud_layers) != len(cloud_specs) or not 18 <= len(cloud_layers) <= 28:
        raise RuntimeError(f"Cloudシェーダー層の数が不正です: {len(cloud_layers)}")
    if {item.get("waterParam", {}).get("effectType") for item in cloud_layers} != {0.0, 1.0}:
        raise RuntimeError("雲塊と流雲の両方が配置されていません。")
    if any(item.get("modelName") != "Stages/high_crown_background/high_crown_cloud_anchor.obj" for item in cloud_layers):
        raise RuntimeError("Cloudシェーダー層の代理モデルが統一されていません。")
    if any(item.get("waterParam", {}).get("waveHeight", 0.0) < 2.0 for item in cloud_layers):
        raise RuntimeError("Cloudシェーダー層の密度が不足しています。")
    if any(item["collider"]["type"] != 0 or item["collisionAttribute"] != 0 for item in backgrounds):
        raise RuntimeError("背景Objectに不要な当たり判定があります。")
    if len({item["guid"] for item in backgrounds}) != len(backgrounds):
        raise RuntimeError("背景ObjectのGUIDが重複しています。")
    scene_names = {item.get("name") for item in json.loads(SCENE_PATH.read_text(encoding="utf-8")).get("objects", [])}
    required = {"Stage3_HighCrown_ArenaVisual", "Stage3_HighCrown_Encounter", "Stage3_HighCrown_BossGate", "Stage3_HighCrown_VictoryCrown"}
    if not required.issubset(scene_names):
        raise RuntimeError("背景更新で既存のボス戦Objectが欠落しました。")


def render_preview(models: dict[str, ObjBuilder], output: Path, camera: Vec3, target: Vec3, focal: float) -> None:
    width, height = 1200, 800
    image = Image.new("RGBA", (width, height), (102, 157, 210, 255))
    draw = ImageDraw.Draw(image, "RGBA")
    for y in range(height):
        t = y / max(height - 1, 1)
        draw.line((0, y, width, y), fill=(int(111 + 89 * t), int(169 + 55 * t), int(220 + 27 * t), 255))
    forward = normalize(sub(target, camera))
    right = normalize(cross(forward, (0.0, 1.0, 0.0)))
    up = normalize(cross(right, forward))
    light = normalize((-0.45, 0.82, 0.38))
    triangles: list[tuple[float, tuple[tuple[float, float], ...], tuple[int, int, int, int]]] = []
    for model_name, model in models.items():
        if model_name == "cloud_anchor":
            continue
        projected: list[tuple[float, float, float]] = []
        for vertex in model.vertices:
            relative = sub(vertex, camera)
            depth = dot(relative, forward)
            factor = focal / max(depth, 0.2)
            projected.append((width * 0.5 + dot(relative, right) * factor, height * 0.55 - dot(relative, up) * factor, depth))
        for material, faces in model.faces.items():
            for face in faces:
                if any(projected[index - 1][2] <= 0.2 for index in face):
                    continue
                normal = normalize(tuple(sum(model.normals[index - 1][axis] for index in face) for axis in range(3)))
                brightness = 0.50 + 0.50 * max(0.0, dot(normal, light))
                if "Rune" in material:
                    brightness = 1.0
                if "Cloud" in material:
                    brightness = max(brightness, 0.88)
                base = MATERIALS[material][0]
                color = tuple(max(0, min(255, int(channel * brightness))) for channel in base) + (255,)
                depth = sum(projected[index - 1][2] for index in face) / 3.0
                polygon = tuple((projected[index - 1][0], projected[index - 1][1]) for index in face)
                triangles.append((depth, polygon, color))
    triangles.sort(key=lambda item: item[0], reverse=True)
    for _, polygon, color in triangles:
        draw.polygon(polygon, fill=color)
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(description="ステージ3 High Crownの背景を生成します。")
    parser.add_argument("--preview-dir", type=Path)
    args = parser.parse_args()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    models = {"foundation": build_foundation(), "cloud_sea": build_cloud_sea(), "cloud_anchor": build_cloud_anchor(), "horizon_citadel": build_horizon_citadel(), "entrance_citadel": build_entrance_citadel()}
    model_files: list[Path] = []
    for name, model in models.items():
        path = OUT_DIR / f"high_crown_{name}.obj"
        write_obj(path, f"high_crown_{name}", model)
        model_files.append(path)
    mtl = OUT_DIR / "high_crown_background.mtl"
    textures = write_mtl(mtl)
    dds_textures = convert_dds(textures)
    for asset in [*model_files, mtl, *textures, *dds_textures]:
        suffix = asset.suffix.lower()
        write_meta(asset, "Model" if suffix == ".obj" else ("Material" if suffix == ".mtl" else "Texture"), "ModelImporter" if suffix == ".obj" else ("MaterialImporter" if suffix == ".mtl" else "TextureImporter"))
    backgrounds = update_scene()
    update_light()
    validate(models, backgrounds)
    if args.preview_dir:
        render_preview(models, args.preview_dir / "stage3_high_crown_background_overview.png", (170.0, 112.0, 190.0), (0.0, -22.0, 0.0), 710.0)
        render_preview(models, args.preview_dir / "stage3_high_crown_background_gameplay.png", (48.0, 28.0, 86.0), (0.0, -9.0, -24.0), 760.0)
    print("Generated Stage 3 background: foundation / cloud sea / horizon citadel / entrance citadel")
    for name, model in models.items():
        minimum, maximum = model_bounds(model)
        triangles = sum(len(faces) for faces in model.faces.values())
        print(f"  {name}: vertices={len(model.vertices)} triangles={triangles} bounds={minimum}..{maximum}")
    print(f"Scene backgrounds: {len(backgrounds)} (collision disabled)")


if __name__ == "__main__":
    main()
