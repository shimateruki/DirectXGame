"""火山ステージのモデル構築。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Iterable, Sequence

from .common import (
    ObjBuilder,
    Vec3,
    add_annulus,
    add_bottom_fan,
    add_box,
    add_cylinder,
    add_cylinder_x,
    add_oriented_box,
    add_surface_ribbon,
    add_tapered_fortress_base,
    add_top_fan,
    connect_rings,
    ring_points,
)

def build_magma_fortress_foundation() -> ObjBuilder:
    """広い平坦面と、マグマへ沈む厚い側壁を持つ城塞基礎。"""

    builder = ObjBuilder("magma_fortress_foundation")
    add_tapered_fortress_base(
        builder,
        width=32.0,
        depth=24.0,
        top_y=0.0,
        bottom_y=-4.8,
        chamfer=1.35,
    )

    # 壁際の低い縁石。中央の歩行面は広く空ける。
    add_box(builder, (0.0, 0.22, -11.35), (27.5, 0.44, 0.52), "fortress_trim")
    add_box(builder, (0.0, 0.22, 11.35), (27.5, 0.44, 0.52), "fortress_trim")
    return builder


def build_basalt_causeway() -> ObjBuilder:
    """城塞区画をつなぐ、長く幅広い玄武岩の土手道。"""

    builder = ObjBuilder("basalt_causeway")
    add_tapered_fortress_base(
        builder,
        width=28.0,
        depth=8.0,
        top_y=0.0,
        bottom_y=-3.2,
        chamfer=0.65,
    )
    # 橋端の金属補強。床面よりわずかに低くして移動を妨げない。
    for x in (-12.0, -6.0, 0.0, 6.0, 12.0):
        add_box(builder, (x, -0.03, 0.0), (0.26, 0.12, 7.25), "iron_hot")
    return builder


def build_metal_grate_platform() -> ObjBuilder:
    """隙間からマグマが見える、閉メッシュ部材のみの金属グレート。"""

    builder = ObjBuilder("metal_grate_platform")
    # 外周フレームと下側の梁。
    # 梁同士の境界辺を完全一致させず、各部材を独立した閉メッシュに保つ。
    add_box(builder, (0.0, -0.38, -4.65), (11.6, 0.76, 0.70), "fortress_trim")
    add_box(builder, (0.0, -0.38, 4.65), (11.6, 0.76, 0.70), "fortress_trim")
    add_box(builder, (-5.65, -0.38, 0.0), (0.70, 0.76, 8.6), "fortress_trim")
    add_box(builder, (5.65, -0.38, 0.0), (0.70, 0.76, 8.6), "fortress_trim")

    for z in (-3.0, 0.0, 3.0):
        add_box(builder, (0.0, -0.52, z), (11.0, 0.36, 0.30), "iron_hot")

    # 独立した細い桟はすべて箱として閉じる。上端はY=0で統一する。
    for index in range(13):
        x = -4.8 + index * 0.8
        add_box(builder, (x, -0.09, 0.0), (0.34, 0.18, 8.7), "iron_grate")
    return builder


def build_fortress_plaza() -> ObjBuilder:
    """段差付きの砦広場。外周床と中央壇を別の閉メッシュで構成する。"""

    builder = ObjBuilder("fortress_plaza")
    add_tapered_fortress_base(
        builder,
        width=26.0,
        depth=22.0,
        top_y=0.0,
        bottom_y=-3.8,
        chamfer=1.15,
    )

    # 中央壇と正面階段。1段15cm相当でスライムも登りやすい寸法にする。
    add_box(builder, (0.0, 0.30, 1.8), (14.0, 0.60, 10.0), "fortress_floor")
    step_depth = 1.35
    for index in range(3):
        height = 0.15 * (index + 1)
        z = -4.55 - index * step_depth
        add_box(builder, (0.0, height * 0.5, z), (14.0, height, step_depth + 0.08), "fortress_trim")

    for x in (-11.9, 11.9):
        add_box(builder, (x, 0.42, 0.0), (0.65, 0.84, 17.2), "fortress_trim")
    return builder


def build_fortress_gate() -> ObjBuilder:
    """広い開口を保った火山城塞の門。"""

    builder = ObjBuilder("fortress_gate")
    for x in (-5.4, 5.4):
        add_box(builder, (x, 4.0, 0.0), (3.0, 8.0, 3.4), "fortress_cliff")
        add_box(builder, (x, 0.40, 0.0), (4.0, 0.80, 4.2), "fortress_trim")
        add_box(builder, (x, 6.05, -1.76), (1.0, 3.1, 0.18), "iron_hot")
    add_box(builder, (0.0, 7.35, 0.0), (13.8, 2.2, 3.4), "fortress_cliff")
    add_box(builder, (0.0, 8.62, 0.0), (14.8, 0.38, 3.8), "fortress_trim")
    for x in (-6.4, -3.2, 0.0, 3.2, 6.4):
        add_box(builder, (x, 9.28, 0.0), (1.5, 1.32, 3.4), "fortress_trim")
    return builder


def build_fortress_pillar() -> ObjBuilder:
    """門・土手道・砦広場に共用できる城塞支柱。"""

    builder = ObjBuilder("fortress_pillar")
    add_box(builder, (0.0, 0.40, 0.0), (4.2, 0.80, 4.2), "fortress_trim")
    add_box(builder, (0.0, 4.55, 0.0), (2.45, 7.50, 2.45), "fortress_cliff")
    add_box(builder, (0.0, 8.55, 0.0), (3.55, 0.70, 3.55), "fortress_trim")
    for y in (2.0, 5.0, 7.6):
        add_box(builder, (0.0, y, -1.27), (2.75, 0.28, 0.16), "iron_hot")
    return builder


def build_fortress_ramp() -> ObjBuilder:
    """Scene側のZ回転で坂にする、ローカル上面が水平な城塞橋。"""

    builder = ObjBuilder("fortress_ramp")
    add_tapered_fortress_base(
        builder,
        width=30.0,
        depth=11.2,
        top_y=0.0,
        bottom_y=-3.4,
        chamfer=0.72,
    )
    # 端部の補強帯も水平面内に置き、OBBの歩行面と干渉させない。
    for x in (-12.0, -6.0, 0.0, 6.0, 12.0):
        add_box(builder, (x, -0.04, 0.0), (0.28, 0.14, 10.15), "iron_hot")
    return builder


def build_fortress_grate_drum() -> ObjBuilder:
    """回転床用の円筒金網。外周の桟はすべて独立した閉メッシュ。"""

    builder = ObjBuilder("fortress_grate_drum")
    length = 18.0
    radius = 3.05
    beam_width = 0.34
    radial_segments = 18

    # 回転軸と両端ハブ。
    add_cylinder_x(builder, (0.0, 0.0, 0.0), 0.48, 19.2, "iron_hot", 16)
    add_cylinder_x(builder, (-8.65, 0.0, 0.0), 0.86, 0.70, "fortress_trim", 16)
    add_cylinder_x(builder, (8.65, 0.0, 0.0), 0.86, 0.70, "fortress_trim", 16)

    # X方向の縦桟。外周上端が連続して見える密度にする。
    for index in range(radial_segments):
        angle = math.tau * index / radial_segments
        radial = (0.0, math.cos(angle), math.sin(angle))
        tangent = (0.0, -math.sin(angle), math.cos(angle))
        center = (0.0, radial[1] * radius, radial[2] * radius)
        add_oriented_box(
            builder,
            center,
            (length, beam_width, beam_width),
            "iron_grate",
            (1.0, 0.0, 0.0),
            radial,
            tangent,
        )

    # 周方向のリング桟。短い閉じた箱を重ねて円形を構成する。
    chord_length = math.tau * radius / radial_segments * 1.08
    for x in (-8.3, -5.5, -2.75, 0.0, 2.75, 5.5, 8.3):
        for index in range(radial_segments):
            angle = math.tau * (index + 0.5) / radial_segments
            radial = (0.0, math.cos(angle), math.sin(angle))
            tangent = (0.0, -math.sin(angle), math.cos(angle))
            center = (x, radial[1] * radius, radial[2] * radius)
            add_oriented_box(
                builder,
                center,
                (chord_length, beam_width, beam_width),
                "fortress_trim",
                tangent,
                radial,
                (1.0, 0.0, 0.0),
            )
    return builder


def build_volcanic_island() -> ObjBuilder:
    builder = ObjBuilder("volcanic_island")
    segments = 24
    rings = [
        ring_points(segments, 8.45, 0.0, x_scale=1.04, z_scale=0.96, noise=0.045),
        ring_points(segments, 9.05, -1.35, x_scale=1.03, z_scale=0.97, noise=0.055),
        ring_points(segments, 8.25, -4.2, x_scale=1.02, z_scale=0.98, noise=0.060),
        ring_points(segments, 6.75, -8.2, x_scale=1.00, z_scale=0.96, noise=0.065),
        ring_points(segments, 4.55, -12.2, x_scale=0.98, z_scale=1.02, noise=0.060),
        ring_points(segments, 2.10, -16.0, x_scale=1.02, z_scale=0.98, noise=0.050),
    ]
    add_top_fan(builder, rings[0], 0.0, "basalt_top")
    for band, (upper, lower) in enumerate(zip(rings, rings[1:])):
        material = "basalt_side" if band < 3 else "obsidian"
        alternate = "obsidian" if band < 2 else ("ember" if band == 4 else None)
        connect_rings(builder, upper, lower, material, alternate_material=alternate)
    add_bottom_fan(builder, rings[-1], (0.0, -18.0, 0.0), "magma")

    cracks = [
        [(-7.8, 0.055, -1.8), (-5.2, 0.06, -1.0), (-3.4, 0.065, -1.8), (-1.2, 0.07, -0.8)],
        [(6.9, 0.055, 2.1), (4.7, 0.06, 1.0), (3.2, 0.065, 1.7), (1.1, 0.07, 0.7)],
        [(-1.8, 0.055, 7.0), (-1.2, 0.06, 4.8), (-2.0, 0.065, 3.1)],
        [(2.8, 0.055, -7.1), (2.1, 0.06, -4.9), (3.0, 0.065, -3.0)],
    ]
    for index, crack in enumerate(cracks):
        add_surface_ribbon(builder, crack, 0.07 + index * 0.012, "ember")
    return builder


def build_obsidian_raft() -> ObjBuilder:
    builder = ObjBuilder("obsidian_raft")
    segments = 12
    top = ring_points(segments, 3.25, 0.38, x_scale=1.05, z_scale=0.95, phase=0.12, noise=0.075)
    edge = ring_points(segments, 3.55, -0.10, x_scale=1.02, z_scale=0.98, phase=0.12, noise=0.085)
    lower = ring_points(segments, 2.55, -1.75, x_scale=0.96, z_scale=1.04, phase=0.12, noise=0.080)
    tip = ring_points(segments, 0.75, -3.35, x_scale=1.10, z_scale=0.90, phase=0.12, noise=0.040)
    add_top_fan(builder, top, 0.38, "obsidian")
    connect_rings(builder, top, edge, "obsidian", alternate_material="basalt_top")
    connect_rings(builder, edge, lower, "basalt_side", alternate_material="obsidian")
    connect_rings(builder, lower, tip, "basalt_side", alternate_material="ember")
    add_bottom_fan(builder, tip, (0.0, -3.75, 0.0), "obsidian")
    add_surface_ribbon(builder, [(-2.7, 0.42, -0.7), (-0.8, 0.43, -0.2), (0.9, 0.43, -0.8)], 0.055, "ember")
    add_surface_ribbon(builder, [(0.2, 0.425, 2.5), (0.5, 0.43, 1.2), (1.4, 0.43, 0.4)], 0.05, "ember")
    return builder


def build_volcanic_spire() -> ObjBuilder:
    builder = ObjBuilder("volcanic_spire")
    segments = 20
    outer = [
        ring_points(segments, 7.8, 0.0, noise=0.055),
        ring_points(segments, 6.7, 4.2, phase=0.05, noise=0.060),
        ring_points(segments, 5.3, 8.7, phase=0.10, noise=0.055),
        ring_points(segments, 4.1, 12.6, phase=0.15, noise=0.045),
    ]
    for band, (lower, upper) in enumerate(zip(outer, outer[1:])):
        connect_rings(builder, upper, lower, "basalt_side", alternate_material="ember" if band == 1 else "obsidian")
    inner = ring_points(segments, 2.25, 11.7, phase=0.15, noise=0.025)
    for index in range(segments):
        next_index = (index + 1) % segments
        builder.add_face(
            [outer[-1][index], outer[-1][next_index], inner[next_index], inner[index]],
            "basalt_top",
            (0.0, 1.0, 0.0),
        )
    crater = ring_points(segments, 1.15, 10.4, phase=0.15)
    connect_rings(builder, inner, crater, "obsidian", inward=True, alternate_material="ember")
    add_top_fan(builder, crater, 10.42, "magma")
    add_bottom_fan(builder, outer[0], (0.0, -0.25, 0.0), "basalt_side")
    return builder


def build_volcanic_backdrop_ridge() -> ObjBuilder:
    """遠景用の層状火山岩稜線を1メッシュへまとめる。

    単純な円錐ではなく、張り出した岩棚と侵食された段丘を交互に積み、
    シルエットだけでも火山盆地の岩壁に見える形へする。発光面はスカイ
    ボックス側の細い溶岩脈へ任せ、山肌へ大きな発光ポリゴンを置かない。
    """

    builder = ObjBuilder("volcanic_backdrop_ridge")
    massif_specs = (
        (-48.0, 1.0, 20.0, 10.0, 24.0, 0.10, -2.2),
        (-29.0, -2.5, 25.0, 12.5, 39.0, 0.31, 3.4),
        (0.0, 2.0, 32.0, 15.0, 49.0, -0.14, -4.0),
        (31.0, -2.0, 27.0, 12.0, 36.0, 0.22, 3.0),
        (52.0, 2.5, 21.0, 10.5, 26.0, -0.27, -2.0),
    )
    tier_specs = (
        # height, x radius, z radius, x drift, z drift, phase, material
        (0.00, 1.08, 1.08, 0.00, 0.00, 0.00, "backdrop_near"),
        (0.08, 1.12, 1.11, -0.02, -0.01, 0.02, "backdrop_near"),
        (0.23, 0.92, 0.88, 0.04, 0.01, -0.03, "backdrop_mid"),
        (0.30, 0.98, 0.93, 0.02, 0.00, 0.04, "backdrop_mid"),
        (0.49, 0.71, 0.68, 0.10, -0.02, -0.02, "backdrop_near"),
        (0.56, 0.77, 0.73, 0.07, -0.01, 0.03, "backdrop_mid"),
        (0.73, 0.51, 0.50, 0.16, 0.02, -0.04, "backdrop_near"),
        (0.80, 0.57, 0.56, 0.12, 0.01, 0.02, "backdrop_far"),
        (0.94, 0.33, 0.34, 0.22, -0.01, -0.02, "backdrop_mid"),
        (0.98, 0.38, 0.37, 0.17, 0.00, 0.03, "backdrop_far"),
    )

    for center_x, center_z, radius_x, radius_z, height, phase, lean in massif_specs:
        rings: list[list[Vec3]] = []
        materials: list[str] = []
        for tier_index, (
            height_ratio,
            x_ratio,
            z_ratio,
            x_drift,
            z_drift,
            ring_phase,
            material,
        ) in enumerate(tier_specs):
            y = height * height_ratio
            local_center_x = center_x + radius_x * x_drift + lean * height_ratio
            local_center_z = center_z + math.sin(phase * 4.0 + tier_index * 0.61) * radius_z * z_drift
            ring = ring_points(
                16,
                1.0,
                y,
                x_scale=radius_x * x_ratio,
                z_scale=radius_z * z_ratio,
                phase=phase + ring_phase,
                noise=0.090 if height_ratio < 0.6 else 0.060,
            )
            edge_roughness = 0.0 if tier_index == len(tier_specs) - 1 else (
                0.28 + height * (0.007 if height_ratio < 0.65 else 0.004)
            )
            rings.append([
                (
                    x + local_center_x,
                    point_y + edge_roughness * (
                        math.sin(vertex_index * 1.47 + phase * 5.0)
                        + math.sin(vertex_index * 0.61 - phase * 3.0) * 0.42
                    ),
                    z + local_center_z,
                )
                for vertex_index, (x, point_y, z) in enumerate(ring)
            ])
            materials.append(material)

        for band, (lower, upper) in enumerate(zip(rings, rings[1:])):
            connect_rings(
                builder,
                upper,
                lower,
                materials[band + 1],
                center_xz=(center_x, center_z),
            )

        crown = rings[-1]
        crown_center = (
            sum(point[0] for point in crown) / len(crown),
            height * 0.982,
            sum(point[2] for point in crown) / len(crown),
        )
        for index in range(len(crown)):
            next_index = (index + 1) % len(crown)
            builder.add_face(
                [crown_center, crown[index], crown[next_index]],
                "backdrop_far",
                (0.0, 1.0, 0.0),
            )
        add_bottom_fan(
            builder,
            rings[0],
            (center_x, -1.4, center_z),
            "backdrop_near",
        )

    return builder


def build_magma_horizon_island() -> ObjBuilder:
    """マグマ海の中景に置く、低く横へ広がった火口島。"""

    builder = ObjBuilder("magma_horizon_island")
    segments = 28
    outer_base = ring_points(segments, 25.0, -7.5, x_scale=1.18, z_scale=0.86, phase=0.08, noise=0.075)
    lower_shelf = ring_points(segments, 23.0, -2.8, x_scale=1.16, z_scale=0.88, phase=0.04, noise=0.070)
    upper_shelf = ring_points(segments, 19.0, 1.2, x_scale=1.12, z_scale=0.92, phase=-0.03, noise=0.060)
    crater_rim = ring_points(segments, 15.2, 4.0, x_scale=1.08, z_scale=0.94, phase=0.06, noise=0.052)
    crater_inner = ring_points(segments, 7.2, 3.35, x_scale=1.16, z_scale=0.86, phase=0.06, noise=0.035)
    magma_pool = ring_points(segments, 5.0, 1.45, x_scale=1.12, z_scale=0.90, phase=0.06, noise=0.020)

    connect_rings(builder, lower_shelf, outer_base, "backdrop_near", alternate_material="obsidian")
    connect_rings(builder, upper_shelf, lower_shelf, "backdrop_mid", alternate_material="backdrop_near")
    connect_rings(builder, crater_rim, upper_shelf, "backdrop_near", alternate_material="backdrop_mid")
    connect_rings(builder, crater_rim, crater_inner, "backdrop_far", center_xz=(0.0, 0.0))
    connect_rings(builder, crater_inner, magma_pool, "obsidian", inward=True, alternate_material="ember")
    add_top_fan(builder, magma_pool, 1.47, "backdrop_lava")
    add_bottom_fan(builder, outer_base, (0.0, -8.4, 0.0), "backdrop_near")

    # 大きさと高さの異なる不規則な岩塔を足し、同一モデルの反復感を弱める。
    rock_specs = (
        (-15.0, -2.0, 2.4, 8.4, -0.20),
        (13.5, 3.4, 2.1, 9.6, 0.27),
        (5.5, -9.0, 1.7, 6.8, -0.08),
    )
    for x, z, radius, height, phase in rock_specs:
        def shifted_rock_ring(ring_radius: float, y: float, ring_phase: float, noise: float) -> list[Vec3]:
            return [
                (point_x + x, point_y, point_z + z)
                for point_x, point_y, point_z in ring_points(
                    11,
                    ring_radius,
                    y,
                    x_scale=0.86,
                    z_scale=1.12,
                    phase=ring_phase,
                    noise=noise,
                )
            ]

        rock_base = shifted_rock_ring(radius, 2.6, phase, 0.11)
        rock_mid = shifted_rock_ring(radius * 0.66, 2.6 + height * 0.58, phase + 0.09, 0.09)
        rock_tip = shifted_rock_ring(radius * 0.18, 2.6 + height, phase - 0.07, 0.04)
        connect_rings(builder, rock_mid, rock_base, "backdrop_mid", alternate_material="backdrop_near")
        connect_rings(builder, rock_tip, rock_mid, "backdrop_far", alternate_material="obsidian")
        add_top_fan(builder, rock_tip, 2.6 + height + 0.08, "backdrop_far")
    return builder


def build_magma_horizon_spire() -> ObjBuilder:
    """遠景の高さ方向へ変化を作る、複数火口を持つ火山塔。"""

    builder = ObjBuilder("magma_horizon_spire")
    segments = 20
    cone_specs = (
        (0.0, 0.0, 10.5, 30.0, 0.04),
        (-12.0, 4.0, 6.4, 20.0, -0.11),
        (10.5, 6.0, 7.2, 23.0, 0.17),
    )
    for center_x, center_z, radius, height, phase in cone_specs:
        def shifted_ring(ring_radius: float, y: float, ring_phase: float, noise: float) -> list[Vec3]:
            return [
                (x + center_x, point_y, z + center_z)
                for x, point_y, z in ring_points(
                    segments,
                    ring_radius,
                    y,
                    x_scale=1.0,
                    z_scale=0.92,
                    phase=ring_phase,
                    noise=noise,
                )
            ]

        base = shifted_ring(radius, 0.0, phase, 0.070)
        shoulder = shifted_ring(radius * 0.72, height * 0.46, phase + 0.05, 0.060)
        upper = shifted_ring(radius * 0.46, height * 0.76, phase - 0.04, 0.050)
        rim = shifted_ring(radius * 0.34, height * 0.90, phase + 0.03, 0.040)
        inner = shifted_ring(radius * 0.17, height * 0.86, phase + 0.03, 0.020)

        connect_rings(builder, shoulder, base, "backdrop_near", alternate_material="obsidian")
        connect_rings(builder, upper, shoulder, "backdrop_mid", alternate_material="backdrop_near")
        connect_rings(builder, rim, upper, "backdrop_far", alternate_material="backdrop_mid")
        connect_rings(builder, rim, inner, "obsidian", center_xz=(center_x, center_z))
        add_top_fan(builder, inner, height * 0.865, "backdrop_lava")
        add_bottom_fan(builder, base, (center_x, -0.8, center_z), "backdrop_near")
    return builder


def build_magma_vent() -> ObjBuilder:
    builder = ObjBuilder("magma_vent")
    add_annulus(builder, 3.4, 2.05, 0.0, 0.72, "basalt_side", 16)
    add_cylinder(builder, (0.0, 0.36, 0.0), 2.02, 0.28, "magma", 16, "magma")
    for index in range(8):
        angle = math.tau * index / 8.0 + 0.2
        add_box(
            builder,
            (math.cos(angle) * 2.75, 0.72, math.sin(angle) * 2.75),
            (1.35, 0.85 + (index % 3) * 0.14, 1.0),
            "obsidian" if index % 2 else "basalt_top",
            -angle,
        )
    return builder


def build_caldera_arch() -> ObjBuilder:
    builder = ObjBuilder("caldera_arch")
    add_box(builder, (-5.1, 3.0, 0.0), (2.2, 6.0, 2.5), "basalt_side", -0.08)
    add_box(builder, (5.1, 3.0, 0.0), (2.2, 6.0, 2.5), "basalt_side", 0.08)
    for index in range(7):
        angle = math.pi * index / 6.0
        center = (math.cos(angle) * 5.0, 5.4 + math.sin(angle) * 5.0, 0.0)
        add_box(builder, center, (2.15, 1.55, 2.5), "obsidian" if index % 2 else "basalt_top", angle - math.pi * 0.5)
    add_box(builder, (0.0, 10.7, 0.0), (1.0, 0.35, 3.0), "ember", 0.0)
    return builder


def build_forge_temple() -> ObjBuilder:
    builder = ObjBuilder("forge_temple")
    add_cylinder(builder, (0.0, 0.65, 0.0), 7.2, 1.3, "basalt_side", 16, "basalt_top")
    add_cylinder(builder, (0.0, 1.55, 0.0), 5.9, 0.5, "obsidian", 16, "obsidian")
    for x in (-4.4, 4.4):
        for z in (-4.0, 4.0):
            add_cylinder(builder, (x, 4.8, z), 0.72, 6.4, "metal", 10, "ember")
    add_box(builder, (0.0, 8.25, 0.0), (11.5, 0.8, 10.5), "basalt_side")
    add_box(builder, (0.0, 9.0, 0.0), (8.8, 0.7, 8.0), "obsidian", 0.08)
    add_cylinder(builder, (0.0, 11.1, 0.0), 2.3, 4.0, "basalt_side", 12, "basalt_top")
    add_cylinder(builder, (0.0, 13.35, 0.0), 1.3, 0.5, "ember", 12, "magma")
    add_box(builder, (0.0, 4.0, 5.35), (4.2, 5.0, 0.65), "ember")
    return builder


MODEL_BUILDERS = {
    "volcanic_island": build_volcanic_island,
    "obsidian_raft": build_obsidian_raft,
    "volcanic_spire": build_volcanic_spire,
    "volcanic_backdrop_ridge": build_volcanic_backdrop_ridge,
    "magma_horizon_island": build_magma_horizon_island,
    "magma_horizon_spire": build_magma_horizon_spire,
    "magma_vent": build_magma_vent,
    "caldera_arch": build_caldera_arch,
    "forge_temple": build_forge_temple,
    "magma_fortress_foundation": build_magma_fortress_foundation,
    "basalt_causeway": build_basalt_causeway,
    "metal_grate_platform": build_metal_grate_platform,
    "fortress_plaza": build_fortress_plaza,
    "fortress_gate": build_fortress_gate,
    "fortress_pillar": build_fortress_pillar,
    "fortress_ramp": build_fortress_ramp,
    "fortress_grate_drum": build_fortress_grate_drum,
}
