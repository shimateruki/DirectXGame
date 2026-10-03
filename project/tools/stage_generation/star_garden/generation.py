"""各バージョンのステージ配置を構築します。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Any, Iterable

from .common import (
    ARENA_CENTER,
    BRIDGE_SPECS,
    CLIFF_CENTER,
    EAST_RAMPART_CENTER,
    ENTRY_CENTER,
    GOAL_CENTER,
    MAIN_ROUTE_STRIP_SPECS,
    ORCHARD_CENTER,
    PRISM_BOSS_EVENT_ID,
    PRISM_DEFEAT_EVENT_ID,
    SECRET_STAIR_EVENT_ID,
    WATERWORKS_CENTER,
    WATER_SURFACE_Y,
    add_coin_line,
    add_coin_positions,
    add_model_colliders,
    add_visible_support_models,
    clone_entry,
    configure_star,
    find_enemy,
    first_gimmick,
    first_model,
    make_appearing_floor,
    make_arena_encounter,
    make_blink_floor,
    make_breakable_block,
    make_breakable_glass,
    make_checkpoint,
    make_coin,
    make_enemy,
    make_falling_spike,
    make_ghost_moving_floor,
    make_giant_rush_gate,
    make_gimmick,
    make_hazard_ride_floor,
    make_invisible_box,
    make_mechanical_floor,
    make_prism_barrier,
    make_prism_seal,
    make_route_strip,
    make_stage_entry_assembly,
    make_timed_floor_switch,
    make_visual_anchor_tower,
    make_visual_model,
    make_wall_box,
    make_water,
    object_map,
    set_gameplay_link,
    set_transform,
    stage_v2_floor_specs,
    stage_v3_floor_specs,
    stage_v4_appearing_route_specs,
    stage_v4_blink_floor_specs,
    stage_v4_final_switch_specs,
    stage_v4_floor_specs,
    yaw_from_xz_direction,
)

def revise_stage(stage1: dict[str, Any], stage2: dict[str, Any]) -> None:
    previous = object_map(stage1)
    model_template = previous.get("Stage1_Ruins_EntryCourtyard") or previous.get("Stage1_Course_Westland") or first_model(stage1)
    coin_template = first_gimmick((stage1, stage2), "Coin")
    gate = previous.get("Stage1_EntranceGate")
    goal = previous.get("goal")
    stars = [previous.get(f"Stage1_StarCoin_0{index}") for index in range(1, 4)]
    if gate is None or goal is None or any(star is None for star in stars):
        raise RuntimeError("入口ゲート、ゴール、スターコインのテンプレートが不足しています。")

    gimmick_types = [
        "BreakableBlock",
        "TimedSwitch",
        "EventReceiver",
        "AppearingFloor",
        "MovingFloor",
        "RotatingFloor",
        "SeesawFloor",
        "SinkingFloor",
        "DashPanel",
    ]
    templates = {kind: first_gimmick((stage1, stage2), kind) for kind in gimmick_types}
    enemy_templates = {
        kind: find_enemy(stage1, kind)
        for kind in ("Slime", "WindSlime", "Bomber", "ThunderSlime", "FireSlime", "PrismSlime")
    }

    gate = clone_entry(gate, "Stage1_EntranceGate")
    gate.update({
        "type": "Gimmick",
        "gimmickType": "StageGate",
        "modelName": "Gimmicks/crown_stage_gate",
        "isStatic": False,
    })
    gate.setdefault("param", {}).update({
        "gimmickType": "StageGate",
        "actionMode": 1,
        "targetScene": "SELECT",
        "startActive": True,
        "returnOnOff": True,
    })
    set_transform(gate, [-352.0, 3.01, -50.0], [1.0, 1.0, 1.0], [0.0, math.pi * 0.5, 0.0])

    objects: list[dict[str, Any]] = [
        make_water(model_template, "Stage1_Ocean", [20.0, WATER_SURFACE_Y - 3.5, 20.0], [920.0, 3.5, 620.0]),
        make_visual_model(model_template, "Stage1_Ruins_EntryCourtyard", "Stages/star_garden_entry_courtyard", ENTRY_CENTER.copy()),
        make_visual_model(model_template, "Stage1_Ruins_CliffPass", "Stages/star_garden_cliff_pass", CLIFF_CENTER.copy()),
        make_visual_model(model_template, "Stage1_Ruins_UpperOrchard", "Stages/star_garden_upper_orchard", ORCHARD_CENTER.copy()),
        make_visual_model(model_template, "Stage1_Ruins_Waterworks", "Stages/star_garden_waterworks", WATERWORKS_CENTER.copy()),
        make_visual_model(model_template, "Stage1_Ruins_PrismArena", "Stages/star_garden_prism_arena", ARENA_CENTER.copy()),
        make_visual_model(model_template, "Stage1_Ruins_EastRampart", "Stages/star_garden_east_rampart", EAST_RAMPART_CENTER.copy()),
        make_visual_model(model_template, "Stage1_Ruins_GoalKeep", "Stages/star_garden_goal_keep", GOAL_CENTER.copy()),
        make_visual_model(model_template, "Stage1_Ruins_Backdrop", "Stages/star_garden_backdrop_ridges", [0.0, 0.0, 0.0]),
    ]

    # 連続した地形同士だけを石橋で結び、攻略用の谷はギミック床として残す。
    for label, start, end, y in BRIDGE_SPECS:
        dx = end[0] - start[0]
        dz = end[1] - start[1]
        objects.append(make_visual_model(
            model_template,
            f"Stage1_Ruins_{label}",
            "Stages/star_garden_ruin_bridge",
            [(start[0] + end[0]) * 0.5, y, (start[1] + end[1]) * 0.5],
            [math.hypot(dx, dz) / 48.0, 1.0, 0.92],
            [0.0, yaw_from_xz_direction(dx, dz), 0.0],
        ))
    add_model_colliders(objects, model_template)
    add_visible_support_models(objects, model_template)
    for label, start, end, surface_y, width in MAIN_ROUTE_STRIP_SPECS:
        objects.append(make_route_strip(
            model_template,
            f"Stage1_RouteStrip_{label}",
            start,
            end,
            surface_y,
            width,
        ))

    objects.append(gate)
    objects.extend(make_stage_entry_assembly(model_template, gate))

    # 装飾は安全地形の輪郭づけに使い、通路中央には置かない。
    decoration_specs = [
        ("Tree", "Stages/soft_tree", [-337.0, 0.0, -70.0], 3.6, 0.3),
        ("Tree", "Stages/soft_tree", [-304.0, 0.0, -27.0], 3.2, 1.1),
        ("Tree", "Stages/soft_tree", [-143.0, 8.0, 42.0], 3.5, 0.8),
        ("Tree", "Stages/soft_tree", [-91.0, 8.0, 43.0], 3.0, 1.7),
        ("Tree", "Stages/soft_tree", [-79.0, 8.0, 77.0], 3.7, 2.2),
        ("Tree", "Stages/soft_tree", [32.0, 8.0, 82.0], 3.1, 0.5),
        ("Pillar", "Stages/star_ruin_pillar", [-242.0, 0.0, 9.0], 1.5, 0.2),
        ("Pillar", "Stages/star_ruin_pillar", [-194.0, 8.0, 29.0], 1.25, 0.9),
        ("Pillar", "Stages/star_ruin_pillar", [-145.0, 8.0, 84.0], 1.4, 1.8),
        ("Pillar", "Stages/star_ruin_pillar", [42.0, 8.0, 123.0], 1.5, 2.5),
        ("Pillar", "Stages/star_ruin_pillar", [195.0, 12.0, 66.0], 1.5, 2.5),
        ("Tree", "Stages/soft_tree", [238.0, 12.0, 57.0], 3.2, 0.4),
        ("Tree", "Stages/soft_tree", [330.0, 15.0, 55.0], 3.4, 1.4),
        ("Pillar", "Stages/star_ruin_pillar", [347.0, 15.0, 22.0], 1.6, 0.5),
        ("Pillar", "Stages/star_ruin_pillar", [522.0, 16.0, -36.0], 1.6, 0.5),
    ]
    for index, (label, model_name, position, scale, yaw) in enumerate(decoration_specs, start=1):
        objects.append(make_visual_model(
            model_template,
            f"Stage1_Decor_{label}_{index:02d}",
            model_name,
            position,
            [scale, scale, scale],
            [0.0, yaw, 0.0],
        ))

    # スター1: 本道でボムを確保し、崖壁の奥にある洞窟を開ける。
    objects.append(make_enemy(enemy_templates["Bomber"], "Stage1_BombGrotto_Bomber", [-246.0, 0.82, -16.0], 0.2))
    # 2.55m角のブロックを隙間なく5列×3段に並べ、ボムなしでは通れない壁にする。
    for row, y in enumerate((1.275, 3.825, 6.375), start=1):
        for column, x in enumerate((-243.1, -240.55, -238.0, -235.45, -232.9), start=1):
            objects.append(make_breakable_block(
                templates["BreakableBlock"],
                f"Stage1_BombGrotto_Wall_{row}_{column}",
                [x, y, -42.7],
            ))
    configure_star(stars[0], 0, [-238.0, 3.65, -56.0])
    objects.append(stars[0])
    add_coin_line(objects, coin_template, "Stage1_BombGrotto_Guide", (-247.0, 1.45, -25.0), (-238.0, 1.45, -38.0), 4)

    # スター2: 上層庭園の時限スイッチから、北壁上の隠し高台へ登る。
    timed_switch = make_gimmick(
        templates["TimedSwitch"],
        "Stage1_Secret_TimedSwitch",
        [-137.0, 8.55, 79.0],
        [0.9, 0.45, 0.9],
    )
    timed_switch["targetID"] = SECRET_STAIR_EVENT_ID
    timed_switch["param"].update({"gimmickType": "TimedSwitch", "interval": 15.0, "startActive": False})
    objects.append(timed_switch)
    stair_positions = (
        [-140.0, 10.0, 84.0],
        [-146.0, 12.0, 89.0],
        [-146.0, 14.0, 97.0],
        [-139.0, 16.0, 104.0],
    )
    for index, position in enumerate(stair_positions, start=1):
        marker = make_visual_model(
            model_template,
            f"Stage1_Secret_AppearingMarker_{index:02d}",
            "Stages/star_garden_appearing_marker",
            [position[0], position[1] - 0.18, position[2]],
            [1.12, 1.0, 1.12],
        )
        marker.update({"castShadow": False, "emissive": 1.28, "color": [0.36, 0.92, 1.0, 1.0]})
        objects.append(marker)
        # 出現前にも高さと着地点が読めるよう、海面まで続く細い支持塔を常設する。
        objects.append(make_visual_anchor_tower(
            model_template,
            f"Stage1_Secret_AppearingTower_{index:02d}",
            (position[0], position[2]),
            position[1] - 0.34,
            1.55,
        ))
        floor = make_gimmick(
            templates["AppearingFloor"],
            f"Stage1_Secret_AppearingStep_{index:02d}",
            list(position),
            [1.12, 0.82, 1.12],
        )
        floor["modelName"] = "Stages/star_garden_gimmick_platform"
        floor["myEventID"] = SECRET_STAIR_EVENT_ID
        floor["param"].update({
            "gimmickType": "AppearingFloor",
            "interval": 15.0,
            "startActive": False,
            "returnOnOff": True,
        })
        objects.append(floor)
        objects.append(make_coin(
            coin_template,
            f"Stage1_Secret_Guide_{index:02d}",
            [position[0], position[1] + 2.0, position[2]],
        ))
    # 出現階段の終点に統合した固定バルコニー上へ置く。
    configure_star(stars[1], 1, [-120.0, 19.0, 100.0])
    objects.append(stars[1])

    # 水路遺跡はトランポリンから一方通行床へ着地し、上層回廊へ抜ける。
    trampoline = make_mechanical_floor(
        templates["MovingFloor"], "Stage1_Waterworks_Trampoline", [-18.0, 8.7, 104.0],
        "Trampoline", 0, 1.0,
    )
    trampoline["color"] = [0.42, 0.95, 1.0, 1.0]
    trampoline["emissive"] = 1.18
    trampoline["param"].update({"gimmickType": "Trampoline", "jumpPower": 42.0})
    objects.append(trampoline)
    one_way_positions = (
        [-8.0, 12.0, 112.0],
        [1.0, 14.8, 118.0],
        [10.0, 17.7, 124.0],
    )
    for index, position in enumerate(one_way_positions, start=1):
        one_way = make_mechanical_floor(
            templates["MovingFloor"], f"Stage1_Waterworks_OneWay_{index:02d}", list(position),
            "OneWayFloor", 0, 1.0,
        )
        one_way["color"] = [0.68, 0.92, 0.72, 0.96]
        one_way["param"].update({"gimmickType": "OneWayFloor", "startActive": True})
        objects.append(one_way)
    add_coin_line(objects, coin_template, "Stage1_Waterworks_Guide", (-17.0, 11.0, 106.0), (10.0, 20.0, 124.0), 5)

    # 崩落床は固定中継塔を挟んだ二つの独立列にする。
    # 前半へ乗った時点で後半まで先に落ちると進行不能になるため、イベント列も分離する。
    chain_positions = (
        [61.0, 18.7, 114.0],
        [71.5, 18.4, 106.0],
        [82.0, 18.0, 98.0],
        [92.5, 17.6, 90.0],
        [115.0, 16.8, 72.0],
        [125.6, 15.8, 63.2],
        [136.2, 14.8, 54.4],
    )
    chain_event_groups = ((5101, 5102, 5103, 5104), (5111, 5112, 5113))
    for index, position in enumerate(chain_positions, start=1):
        group_index = 0 if index <= 4 else 1
        index_in_group = index - 1 if group_index == 0 else index - 5
        event_group = chain_event_groups[group_index]
        yaw = yaw_from_xz_direction(10.5, -8.0) if index <= 4 else yaw_from_xz_direction(10.6, -8.8)
        chain = make_mechanical_floor(
            templates["MovingFloor"], f"Stage1_ChainCollapse_{index:02d}", list(position),
            "ChainCollapseFloor", 0, 1.0, yaw=yaw,
        )
        chain["myEventID"] = event_group[index_in_group]
        chain["targetID"] = (
            event_group[index_in_group + 1]
            if index_in_group + 1 < len(event_group)
            else -1
        )
        chain["param"].update({
            "gimmickType": "ChainCollapseFloor",
            "shakeDuration": 0.58,
            "fallDuration": 1.6,
            "interval": 0.24,
            "gravity": 42.0,
            "returnOnOff": True,
        })
        objects.append(chain)
    add_coin_line(objects, coin_template, "Stage1_ChainCollapse_GuideA", (61.0, 21.0, 114.0), (92.5, 19.6, 90.0), 4)
    add_coin_line(objects, coin_template, "Stage1_ChainCollapse_GuideB", (115.0, 19.0, 72.0), (136.2, 17.0, 54.4), 3)

    # スター3: 壁で囲われた広場をクリスタルスライム撃破まで封鎖する。
    prism = make_enemy(
        enemy_templates["PrismSlime"],
        "Stage1_Revision_Prism_RuinArena",
        [170.0, 12.82, 42.0],
        -1.1,
        4.15,
        PRISM_DEFEAT_EVENT_ID,
    )
    prism.update({
        "materialType": 27,
        "color": [0.62, 0.94, 1.0, 1.0],
        "emissive": 1.08,
        "enableEnvMap": True,
        "envIntensity": 1.0,
    })
    objects.append(prism)
    for index, z in enumerate((35.5, 37.7, 39.9, 42.0, 44.1, 46.3, 48.5), start=1):
        objects.append(make_prism_seal(
            templates["EventReceiver"],
            f"Stage1_Revision_PrismSeal_{index:02d}",
            [205.5, 12.2, z],
            2.35 if index % 2 else 2.15,
        ))
    # 撃破後は東口の封鎖が消え、遺跡モデルに統合した報酬室へ入れる。
    configure_star(stars[2], 2, [220.5, 15.5, 42.0])
    objects.append(stars[2])

    # 戦闘後は長い城壁区画で一度呼吸を置き、その先を種類の違う可動床で曲がりながら渡る。
    floor_specs = [
        ("Stage1_Ravine_Seesaw", "SeesawFloor", [368.0, 15.7, 21.5], 0, 8.0, 0.0, yaw_from_xz_direction(12.3, -9.5)),
        # Z移動量を1.2mに抑え、往復の両端でも前後どちらかの床へ安全に跳び移れるようにする。
        ("Stage1_Ravine_Moving", "MovingFloor", [380.3, 16.0, 12.0], 3, 0.42, 1.2, yaw_from_xz_direction(12.4, -9.5)),
        ("Stage1_Ravine_Rotating", "RotatingFloor", [392.7, 15.9, 2.5], 1, 14.0, 0.0, yaw_from_xz_direction(12.3, -9.5)),
        ("Stage1_Ravine_Sinking", "SinkingFloor", [405.0, 15.7, -7.0], 0, 8.0, 0.0, yaw_from_xz_direction(15.0, -15.0)),
    ]
    for name, kind, position, action_mode, speed, move_amount, yaw in floor_specs:
        objects.append(make_mechanical_floor(
            templates[kind], name, position, kind, action_mode, speed, move_amount, yaw,
        ))
    add_coin_line(objects, coin_template, "Stage1_Ravine_Guide", (368.0, 18.0, 21.5), (405.0, 18.0, -7.0), 6)

    ride_start = [420.0, 16.0, -22.0]
    ride_end = [458.0, 16.0, -58.0]
    ride_dx = ride_end[0] - ride_start[0]
    ride_dz = ride_end[2] - ride_start[2]
    ride_yaw = yaw_from_xz_direction(ride_dx, ride_dz)
    ride_distance = math.hypot(ride_dx, ride_dz)
    hazard_event_id = 5300
    hazard_progress = (0.18, 0.38, 0.58, 0.78)
    hazard_lateral = (-2.2, 2.0, -1.6, 2.2)

    objects.append(make_hazard_ride_floor(
        templates["MovingFloor"],
        "Stage1_Ravine_HazardRide",
        ride_start.copy(),
        ride_yaw,
        ride_distance,
        hazard_event_id,
        len(hazard_progress),
    ))

    ride_forward = [ride_dx / ride_distance, 0.0, ride_dz / ride_distance]
    ride_side = [-ride_forward[2], 0.0, ride_forward[0]]
    for index, (progress, lateral) in enumerate(zip(hazard_progress, hazard_lateral), start=1):
        landing_x = ride_start[0] + ride_forward[0] * ride_distance * progress + ride_side[0] * lateral
        landing_z = ride_start[2] + ride_forward[2] * ride_distance * progress + ride_side[2] * lateral
        landing_y = ride_start[1] + 0.2
        drop_distance = 12.6
        objects.append(make_falling_spike(
            templates["MovingFloor"],
            f"Stage1_Ravine_FallingSpike_{index:02d}",
            [landing_x, landing_y + drop_distance, landing_z],
            hazard_event_id + index - 1,
            drop_distance,
        ))
        marker = make_visual_model(
            model_template,
            f"Stage1_Ravine_FallingSpikeMarker_{index:02d}",
            "Stages/star_garden_spike_marker",
            [landing_x, landing_y + 0.04, landing_z],
        )
        marker.update({"castShadow": False, "emissive": 1.35, "color": [1.0, 0.48, 0.16, 1.0]})
        objects.append(marker)

    final_dash_yaw = yaw_from_xz_direction(19.0, -7.0)
    dash = make_gimmick(
        templates["DashPanel"],
        "Stage1_Final_DashPanel",
        [477.0, 16.48, -62.0],
        [1.15, 1.0, 1.0],
        [0.0, final_dash_yaw, 0.0],
    )
    objects.append(dash)

    # 通常敵は広い安全地形に限定し、地形ギミックと同時に処理させない。
    enemy_specs = [
        ("Slime", "Stage1_Enemy_EntrySlime", [-311.0, 0.82, -33.0], 0.2),
        ("WindSlime", "Stage1_Enemy_CliffWind", [-232.0, 0.82, 6.0], -0.8),
        ("FireSlime", "Stage1_Enemy_OrchardFire", [-128.0, 8.82, 49.0], 1.0),
        ("ThunderSlime", "Stage1_Enemy_OrchardThunder", [-90.0, 8.82, 70.0], -1.2),
        ("WindSlime", "Stage1_Enemy_WaterworksWind", [31.0, 8.82, 90.0], -1.0),
        ("Slime", "Stage1_Enemy_RampartSlime", [251.0, 12.82, 27.0], 0.8),
        ("WindSlime", "Stage1_Enemy_RampartWind", [324.0, 15.82, 43.0], -0.9),
        ("FireSlime", "Stage1_Enemy_GoalFire", [486.0, 16.82, -83.0], 0.8),
        ("ThunderSlime", "Stage1_Enemy_GoalThunder", [523.0, 16.82, -49.0], -0.9),
    ]
    for enemy_type, name, position, yaw in enemy_specs:
        objects.append(make_enemy(enemy_templates[enemy_type], name, position, yaw))

    # コインは曲がり角・橋・段差の入口に置き、必ず見える支持床の上を案内する。
    for label, start, end, surface_y in BRIDGE_SPECS:
        dx = end[0] - start[0]
        dz = end[1] - start[1]
        add_coin_line(
            objects,
            coin_template,
            f"Stage1_BridgeGuide_{label}",
            (start[0] + dx * 0.22, surface_y + 1.45, start[1] + dz * 0.22),
            (start[0] + dx * 0.78, surface_y + 1.45, start[1] + dz * 0.78),
            3,
        )
    add_coin_line(objects, coin_template, "Stage1_MainGuide_Entry", (-332.0, 1.45, -47.0), (-292.0, 1.45, -42.0), 5)
    add_coin_line(objects, coin_template, "Stage1_MainGuide_Cliff", (-257.0, 1.45, -21.0), (-224.0, 1.45, -1.5), 5)
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_MainGuide_Stairs",
        (
            (-221.0, 2.45, 0.0),
            (-221.0, 4.45, 9.4),
            (-221.0, 6.45, 18.8),
            (-221.0, 8.45, 28.2),
            (-221.0, 9.45, 32.9),
        ),
    )
    add_coin_line(objects, coin_template, "Stage1_MainGuide_Orchard", (-154.0, 9.45, 61.0), (-74.0, 9.45, 68.0), 6)
    add_coin_line(objects, coin_template, "Stage1_MainGuide_Waterworks", (-58.0, 9.45, 84.0), (40.0, 9.45, 96.0), 6)
    # 城壁の西庭と東庭は中央階段だけで接続される。空中を直進する旧コイン列を廃止する。
    add_coin_line(objects, coin_template, "Stage1_MainGuide_RampartWest", (226.0, 13.45, 42.0), (276.0, 13.45, 22.0), 5)
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_MainGuide_RampartSteps",
        (
            (281.0, 14.45, 20.0),
            (285.6, 15.45, 20.0),
            (290.2, 16.45, 20.0),
        ),
    )
    add_coin_line(objects, coin_template, "Stage1_MainGuide_RampartEast", (296.0, 16.45, 21.0), (347.0, 16.45, 30.0), 5)
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_MainGuide_Final",
        (
            (458.0, 18.35, -58.0),
            (468.0, 17.55, -60.0),
            (478.0, 17.55, -62.0),
            (487.0, 20.35, -64.0),
        ),
    )

    goal = clone_entry(goal, "goal")
    set_transform(goal, [501.0, 20.0, -65.0], [1.0, 1.0, 1.0], [0.0, 0.0, 0.0])
    objects.append(goal)
    stage1["objects"] = objects


def revise_stage_v2(stage1: dict[str, Any], stage2: dict[str, Any]) -> None:
    """旧配置を引き継がず、短く読める一本のStage 1へ全面置換する。"""
    previous = object_map(stage1)
    model_template = previous.get("Stage1_Ruins_EntryCourtyard") or first_model(stage1)
    coin_template = first_gimmick((stage1, stage2), "Coin")
    gate_template = previous.get("Stage1_EntranceGate")
    goal_template = previous.get("goal")
    stars = [previous.get(f"Stage1_StarCoin_0{index}") for index in range(1, 4)]
    if gate_template is None or goal_template is None or any(star is None for star in stars):
        raise RuntimeError("入口ゲート、ゴール、スターコインのテンプレートが不足しています。")

    gimmick_templates = {
        kind: first_gimmick((stage1, stage2), kind)
        for kind in ("BreakableBlock", "TimedSwitch", "AppearingFloor", "MovingFloor", "EventReceiver")
    }
    enemy_templates = {
        kind: find_enemy(stage1, kind)
        for kind in ("Slime", "WindSlime", "Bomber", "FireSlime", "PrismSlime")
    }

    objects: list[dict[str, Any]] = [
        make_water(model_template, "Stage1_Ocean", [-55.0, WATER_SURFACE_Y - 3.5, -50.0], [440.0, 3.5, 290.0]),
        make_visual_model(model_template, "Stage1_Course_V2", "Stages/star_garden_course_v2", [0.0, 0.0, 0.0]),
        make_visual_model(model_template, "Stage1_Backdrop_V2", "Stages/star_garden_backdrop_v2", [0.0, 0.0, 0.0]),
    ]
    for label, position, half_size in stage_v2_floor_specs():
        objects.append(make_invisible_box(
            model_template,
            f"Stage1_Collision_V2_{label}",
            position.copy(),
            half_size.copy(),
        ))

    # 洞窟だけは低い壁に実体を持たせる。本道側にはカメラを塞ぐ壁を置かない。
    for label, start, end in (
        ("BombGrottoBack", (-258.0, -113.0), (-222.0, -113.0)),
        ("BombGrottoWest", (-258.0, -113.0), (-258.0, -87.0)),
        ("BombGrottoEast", (-222.0, -113.0), (-222.0, -87.0)),
    ):
        objects.append(make_wall_box(
            model_template,
            f"Stage1_WallCollision_V2_{label}",
            start,
            end,
            0.0,
            3.8,
            1.8,
        ))

    gate = clone_entry(gate_template, "Stage1_EntranceGate")
    gate.update({
        "type": "Gimmick",
        "gimmickType": "StageGate",
        "modelName": "Gimmicks/crown_stage_gate",
        "isStatic": False,
    })
    gate.setdefault("param", {}).update({
        "gimmickType": "StageGate",
        "actionMode": 1,
        "targetScene": "SELECT",
        "startActive": True,
        "returnOnOff": True,
    })
    set_transform(gate, [-352.0, 3.01, -50.0], [1.0, 1.0, 1.0], [0.0, math.pi * 0.5, 0.0])
    objects.append(gate)
    objects.extend(make_stage_entry_assembly(model_template, gate))

    # 装飾は各区間の外縁だけに置き、進行方向とカメラの視線を空ける。
    decoration_specs = (
        ("Tree_EntryWest", "Stages/soft_tree", [-334.0, 0.0, -66.0], 3.1, 0.3),
        ("Tree_EntryEast", "Stages/soft_tree", [-292.0, 0.0, -34.0], 2.8, 1.1),
        ("Tree_Meadow", "Stages/soft_tree", [-232.0, 0.0, -59.0], 2.5, 2.0),
        ("Pillar_UpperWest", "Stages/star_ruin_pillar", [-119.0, 8.0, -62.0], 1.15, 0.2),
        ("Tree_UpperEast", "Stages/soft_tree", [-70.0, 8.0, -62.0], 2.6, 1.7),
        ("Pillar_ArenaNorth", "Stages/star_ruin_pillar", [0.0, 8.0, -25.0], 1.25, 0.0),
        ("Tree_Landing", "Stages/soft_tree", [129.0, 8.0, -38.0], 2.6, 0.8),
        ("Pillar_Goal", "Stages/star_ruin_pillar", [210.0, 12.0, -67.0], 1.25, 0.4),
    )
    for label, model_name, position, scale, yaw in decoration_specs:
        objects.append(make_visual_model(
            model_template,
            f"Stage1_Decor_V2_{label}",
            model_name,
            position,
            [scale, scale, scale],
            [0.0, yaw, 0.0],
        ))

    # スター1: 本道でボム能力を見せ、低い洞窟の壁を壊して獲得する。
    objects.append(make_enemy(enemy_templates["Bomber"], "Stage1_V2_BombGrotto_Bomber", [-250.0, 0.82, -50.0], 0.0))
    for row, y in enumerate((1.275, 3.825), start=1):
        for column, x in enumerate((-245.10, -242.55, -240.0, -237.45, -234.90), start=1):
            objects.append(make_breakable_block(
                gimmick_templates["BreakableBlock"],
                f"Stage1_V2_BombWall_{row}_{column}",
                [x, y, -87.8],
            ))
    configure_star(stars[0], 0, [-240.0, 3.65, -102.0])
    objects.append(stars[0])
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V2_Star1Guide",
        ((-240.0, 1.45, -66.0), (-240.0, 1.45, -75.0), (-240.0, 1.45, -84.0)),
    )

    # スター2: スイッチを押した時だけ三枚の足場が現れ、固定段差で安全に帰れる。
    timed_switch = make_gimmick(
        gimmick_templates["TimedSwitch"],
        "Stage1_V2_Secret_TimedSwitch",
        [-105.0, 8.55, -38.0],
        [0.9, 0.45, 0.9],
    )
    timed_switch["targetID"] = SECRET_STAIR_EVENT_ID
    timed_switch["param"].update({"gimmickType": "TimedSwitch", "interval": 12.0, "startActive": False})
    objects.append(timed_switch)
    appearing_positions = (
        [-98.0, 9.8, -29.0],
        [-88.0, 11.8, -22.0],
        [-76.0, 13.8, -18.0],
    )
    for index, position in enumerate(appearing_positions, start=1):
        marker = make_visual_model(
            model_template,
            f"Stage1_V2_SecretMarker_{index:02d}",
            "Stages/star_garden_appearing_marker",
            [position[0], position[1] - 0.26, position[2]],
            [1.0, 1.0, 1.0],
        )
        marker.update({"castShadow": False, "emissive": 1.28, "color": [0.36, 0.92, 1.0, 1.0]})
        objects.append(marker)
        floor = make_gimmick(
            gimmick_templates["AppearingFloor"],
            f"Stage1_V2_SecretStep_{index:02d}",
            position.copy(),
            [1.0, 0.82, 1.0],
        )
        floor["modelName"] = "Stages/star_garden_gimmick_platform"
        floor["myEventID"] = SECRET_STAIR_EVENT_ID
        floor["param"].update({
            "gimmickType": "AppearingFloor",
            "interval": 12.0,
            "startActive": False,
            "returnOnOff": True,
        })
        objects.append(floor)
        objects.append(make_coin(
            coin_template,
            f"Stage1_V2_Star2Guide_{index:02d}",
            [position[0], position[1] + 1.8, position[2]],
        ))
    configure_star(stars[1], 1, [-60.0, 17.65, -18.0])
    objects.append(stars[1])

    # ボスは一本道の中央に置き、撃破後だけ東口の結晶列が消える。
    prism = make_enemy(
        enemy_templates["PrismSlime"],
        "Stage1_V2_PrismArena_Boss",
        [0.0, 8.82, -50.0],
        -math.pi * 0.5,
        4.15,
        PRISM_DEFEAT_EVENT_ID,
    )
    prism.update({
        "materialType": 27,
        "color": [0.62, 0.94, 1.0, 1.0],
        "emissive": 1.08,
        "enableEnvMap": True,
        "envIntensity": 1.0,
    })
    objects.append(prism)
    for index, z in enumerate((-59.0, -56.75, -54.5, -52.25, -50.0, -47.75, -45.5, -43.25, -41.0), start=1):
        objects.append(make_prism_seal(
            gimmick_templates["EventReceiver"],
            f"Stage1_V2_PrismSeal_{index:02d}",
            [37.4, 8.2, z],
            2.2,
        ))

    # ボス後は四枚だけの崩落橋。先頭から連鎖し、一定時間後に復帰する。
    collapse_positions = (60.0, 73.0, 86.0, 99.0)
    collapse_events = (5201, 5202, 5203, 5204)
    for index, (x, event_id) in enumerate(zip(collapse_positions, collapse_events), start=1):
        floor = make_mechanical_floor(
            gimmick_templates["MovingFloor"],
            f"Stage1_V2_ChainCollapse_{index:02d}",
            [x, 8.7, -50.0],
            "ChainCollapseFloor",
            0,
            1.0,
        )
        floor["myEventID"] = event_id
        floor["targetID"] = collapse_events[index] if index < len(collapse_events) else -1
        floor["param"].update({
            "gimmickType": "ChainCollapseFloor",
            "shakeDuration": 0.58,
            "fallDuration": 1.6,
            "interval": 0.24,
            "gravity": 42.0,
            "returnOnOff": True,
        })
        objects.append(floor)
        objects.append(make_coin(coin_template, f"Stage1_V2_CollapseGuide_{index:02d}", [x, 10.4, -50.0]))

    # スター3は崩落橋の着地点から見える固定高台に置く。必ずゴール広場へ戻れる。
    configure_star(stars[2], 2, [145.0, 17.65, -82.0])
    objects.append(stars[2])
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V2_Star3Guide",
        (
            (118.0, 12.0, -70.0),
            (130.0, 14.0, -78.0),
            (145.0, 16.0, -82.0),
            (158.0, 15.0, -74.0),
        ),
    )

    # 通常敵は地形ギミックと重ねず、一度に一種類だけ見える間隔へ置く。
    for enemy_type, name, position, yaw in (
        ("Slime", "Stage1_V2_Enemy_EntrySlime", [-286.0, 0.82, -50.0], 0.0),
        ("WindSlime", "Stage1_V2_Enemy_UpperWind", [-118.0, 8.82, -50.0], -0.8),
        ("FireSlime", "Stage1_V2_Enemy_UpperFire", [-72.0, 8.82, -50.0], 0.8),
        ("Slime", "Stage1_V2_Enemy_LandingSlime", [126.0, 8.82, -50.0], math.pi),
        ("WindSlime", "Stage1_V2_Enemy_GoalWind", [180.0, 12.82, -50.0], math.pi),
    ):
        objects.append(make_enemy(enemy_templates[enemy_type], name, position, yaw))

    # 本道のコインは石畳の中央だけを通り、空中や地形外へ逸れない。
    add_coin_line(objects, coin_template, "Stage1_V2_MainGuide_Entry", (-330.0, 1.45, -50.0), (-285.0, 1.45, -50.0), 6)
    add_coin_line(objects, coin_template, "Stage1_V2_MainGuide_Meadow", (-270.0, 1.45, -50.0), (-215.0, 1.45, -50.0), 6)
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V2_MainGuide_Steps",
        ((-192.0, 1.45, -50.0), (-168.0, 3.45, -50.0), (-154.0, 5.45, -50.0), (-140.0, 7.45, -50.0)),
    )
    add_coin_line(objects, coin_template, "Stage1_V2_MainGuide_Upper", (-125.0, 9.45, -50.0), (-47.0, 9.45, -50.0), 7)
    add_coin_line(objects, coin_template, "Stage1_V2_MainGuide_Arena", (-30.0, 9.55, -50.0), (30.0, 9.55, -50.0), 5)
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V2_MainGuide_FinalSteps",
        ((122.0, 9.45, -50.0), (145.0, 11.45, -50.0), (157.0, 13.45, -50.0), (178.0, 13.45, -50.0)),
    )

    goal = clone_entry(goal_template, "goal")
    set_transform(goal, [215.0, 14.3, -50.0], [1.0, 1.0, 1.0], [0.0, 0.0, 0.0])
    objects.append(goal)
    stage1["objects"] = objects


def revise_stage_v3(stage1: dict[str, Any], stage2: dict[str, Any]) -> None:
    """方向変化・高低差・ギミック展開を一続きにしたStage 1へ置換する。"""
    previous = object_map(stage1)
    model_template = previous.get("Stage1_Course_V2") or first_model(stage1)
    coin_template = first_gimmick((stage1, stage2), "Coin")
    gate_template = previous.get("Stage1_EntranceGate")
    goal_template = previous.get("goal")
    stars = [previous.get(f"Stage1_StarCoin_0{index}") for index in range(1, 4)]
    if gate_template is None or goal_template is None or any(star is None for star in stars):
        raise RuntimeError("入口ゲート、ゴール、スターコインのテンプレートが不足しています。")

    templates = {
        kind: first_gimmick((stage1, stage2), kind)
        for kind in (
            "BreakableBlock",
            "MovingFloor",
            "EventReceiver",
            "SeesawFloor",
            "RotatingFloor",
            "SinkingFloor",
            "DashPanel",
        )
    }
    enemy_templates = {
        "Slime": find_enemy(stage1, "Slime"),
        "WindSlime": find_enemy(stage1, "WindSlime"),
        "Bomber": find_enemy(stage1, "Bomber"),
        "FireSlime": find_enemy(stage1, "FireSlime"),
        "PrismSlime": find_enemy(stage1, "PrismSlime"),
        "ThunderSlime": find_enemy(stage2, "ThunderSlime"),
    }

    objects: list[dict[str, Any]] = [
        make_water(model_template, "Stage1_Ocean", [-25.0, WATER_SURFACE_Y - 3.5, -38.0], [470.0, 3.5, 300.0]),
        make_visual_model(model_template, "Stage1_Course_V3", "Stages/star_garden_course_v3", [0.0, 0.0, 0.0]),
        make_visual_model(model_template, "Stage1_Backdrop_V3", "Stages/star_garden_backdrop_v2", [0.0, 0.0, 0.0]),
    ]
    for label, position, half_size, yaw in stage_v3_floor_specs():
        objects.append(make_invisible_box(
            model_template,
            f"Stage1_Collision_V3_{label}",
            position.copy(),
            half_size.copy(),
            yaw,
        ))

    for label, start, end in (
        ("BombGrottoBack", (-287.0, -103.0), (-253.0, -103.0)),
        ("BombGrottoWest", (-287.0, -103.0), (-287.0, -79.0)),
        ("BombGrottoEast", (-253.0, -103.0), (-253.0, -79.0)),
    ):
        objects.append(make_wall_box(
            model_template,
            f"Stage1_WallCollision_V3_{label}",
            start,
            end,
            0.0,
            10.0,
            1.8,
        ))

    gate = clone_entry(gate_template, "Stage1_EntranceGate")
    gate.update({
        "type": "Gimmick",
        "gimmickType": "StageGate",
        "modelName": "Gimmicks/crown_stage_gate",
        "isStatic": False,
    })
    gate.setdefault("param", {}).update({
        "gimmickType": "StageGate",
        "actionMode": 1,
        "targetScene": "SELECT",
        "startActive": True,
        "returnOnOff": True,
    })
    set_transform(gate, [-352.0, 3.01, -50.0], [1.0, 1.0, 1.0], [0.0, math.pi * 0.5, 0.0])
    objects.append(gate)
    objects.extend(make_stage_entry_assembly(model_template, gate))

    # 外縁の装飾でカーブと高低差を縁取り、進行ライン中央は空ける。
    decoration_specs = (
        ("Tree_EntryWest", "Stages/soft_tree", [-338.0, 0.0, -67.0], 3.1, 0.3),
        ("Tree_EntryEast", "Stages/soft_tree", [-307.0, 0.0, -34.0], 2.8, 1.1),
        ("Tree_Meadow", "Stages/soft_tree", [-248.0, 0.0, -27.0], 2.6, 2.0),
        ("Pillar_Stairs", "Stages/star_ruin_pillar", [-201.0, 0.0, -42.0], 1.15, 0.2),
        ("Tree_UpperWest", "Stages/soft_tree", [-153.0, 6.0, -28.0], 2.6, 0.8),
        ("Tree_UpperEast", "Stages/soft_tree", [-107.0, 6.0, -27.0], 2.8, 1.7),
        ("Pillar_Waterworks", "Stages/star_ruin_pillar", [11.0, 15.0, -65.0], 1.35, 0.0),
        ("Pillar_ArenaNorth", "Stages/star_ruin_pillar", [100.0, 15.0, -7.0], 1.3, 0.0),
        ("Tree_Landing", "Stages/soft_tree", [218.0, 13.0, -40.0], 2.5, 0.8),
        ("Tree_Final", "Stages/soft_tree", [245.0, 13.0, -31.0], 2.6, 1.2),
        ("Pillar_GoalSouth", "Stages/star_ruin_pillar", [290.0, 17.0, -44.0], 1.3, 0.4),
        ("Pillar_GoalNorth", "Stages/star_ruin_pillar", [290.0, 17.0, -12.0], 1.3, 2.7),
    )
    for label, model_name, position, scale, yaw in decoration_specs:
        objects.append(make_visual_model(
            model_template,
            f"Stage1_Decor_V3_{label}",
            model_name,
            position,
            [scale, scale, scale],
            [0.0, yaw, 0.0],
        ))

    # スター1: ガラス越しに報酬を先に見せ、ボマーの爆風で入口を割って回収する。
    objects.append(make_enemy(enemy_templates["Bomber"], "Stage1_V3_BombGrotto_Bomber", [-270.0, 0.82, -50.0], 0.0))
    for row, y in enumerate((2.1, 6.3), start=1):
        for column, x in enumerate((-275.4, -270.0, -264.6), start=1):
            objects.append(make_breakable_glass(
                templates["BreakableBlock"],
                f"Stage1_V3_GlassVault_{row}_{column}",
                [x, y, -79.4],
            ))
    configure_star(stars[0], 0, [-270.0, 3.65, -93.0])
    objects.append(stars[0])
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V3_Star1Guide",
        ((-270.0, 1.45, -64.0), (-270.0, 1.45, -71.0), (-270.0, 1.45, -77.0)),
    )

    # 本道の縦移動: トランポリンで役割を見せ、一方通行床三枚で15mの水路庭園へ着地する。
    trampoline = make_mechanical_floor(
        templates["MovingFloor"],
        "Stage1_V3_Trampoline",
        [-62.0, 6.5, -38.0],
        "Trampoline",
        0,
        1.0,
        yaw=math.atan2(6.0, 11.0),
    )
    trampoline.update({"color": [0.42, 0.95, 1.0, 1.0], "emissive": 1.18})
    trampoline["param"].update({"gimmickType": "Trampoline", "jumpPower": 34.0})
    objects.append(trampoline)
    ascent_positions = (
        [-51.0, 8.7, -44.0],
        [-40.0, 11.7, -49.0],
        [-27.0, 14.7, -52.0],
    )
    ascent_yaw = yaw_from_xz_direction(11.0, -5.0)
    for index, position in enumerate(ascent_positions, start=1):
        floor = make_mechanical_floor(
            templates["MovingFloor"],
            f"Stage1_V3_OneWay_{index:02d}",
            position.copy(),
            "OneWayFloor",
            0,
            1.0,
            yaw=ascent_yaw,
        )
        floor.update({"color": [0.68, 0.92, 0.72, 0.96], "emissive": 1.08})
        floor["param"].update({"gimmickType": "OneWayFloor", "startActive": True})
        objects.append(floor)
        objects.append(make_coin(coin_template, f"Stage1_V3_AscentGuide_{index:02d}", [position[0], position[1] + 1.8, position[2]]))

    # 水路庭園はX移動、回転、Z移動で北へ大きく折れ、同じ操作を繰り返させない。
    moving_x = make_mechanical_floor(
        templates["MovingFloor"],
        "Stage1_V3_WaterworksMovingX",
        [42.0, 15.3, -48.0],
        "MovingFloor",
        2,
        1.05,
        4.5,
        yaw=0.0,
    )
    moving_x.update({"color": [0.30, 0.82, 1.0, 1.0], "emissive": 1.14})
    objects.append(moving_x)

    waterworks_rotating = make_mechanical_floor(
        templates["RotatingFloor"],
        "Stage1_V3_WaterworksRotating",
        [54.0, 15.3, -40.0],
        "RotatingFloor",
        2,
        34.0,
        yaw=0.0,
    )
    waterworks_rotating.update({"color": [0.36, 0.92, 0.88, 1.0], "emissive": 1.12})
    waterworks_rotating["param"].update({"gimmickType": "RotatingFloor", "startActive": True, "returnOnOff": True})
    objects.append(waterworks_rotating)

    moving_z = make_mechanical_floor(
        templates["MovingFloor"],
        "Stage1_V3_WaterworksMovingZ",
        [59.0, 15.3, -28.0],
        "MovingFloor",
        3,
        0.95,
        5.5,
        yaw=math.pi * 0.5,
    )
    moving_z.update({"color": [0.32, 0.72, 1.0, 1.0], "emissive": 1.12})
    objects.append(moving_z)

    seesaw = make_mechanical_floor(
        templates["SeesawFloor"],
        "Stage1_V3_WaterworksSeesaw",
        [72.0, 15.3, -25.0],
        "SeesawFloor",
        0,
        1.0,
        yaw=yaw_from_xz_direction(8.0, -12.0),
    )
    objects.append(seesaw)

    # スター2: 多軸可動床区間を越えた先で、上下リフトの上昇タイミングを読む中盤報酬。
    vertical_lift = make_mechanical_floor(
        templates["MovingFloor"],
        "Stage1_V3_ClockworkVerticalLift",
        [58.0, 14.5, -2.0],
        "MovingFloor",
        4,
        1.10,
        8.0,
        yaw=math.pi * 0.5,
    )
    vertical_lift.update({"color": [0.56, 0.94, 1.0, 1.0], "emissive": 1.18})
    objects.append(vertical_lift)
    configure_star(stars[1], 1, [58.0, 26.65, 10.0])
    objects.append(stars[1])

    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V3_WaterworksGuide",
        (
            (29.0, 16.45, -48.0),
            (60.0, 16.45, -14.0),
            (72.0, 16.45, -4.0),
            (68.0, 16.45, -37.0),
        ),
    )

    prism = make_enemy(
        enemy_templates["PrismSlime"],
        "Stage1_V3_PrismArena_Boss",
        [100.0, 15.82, -32.0],
        -math.pi * 0.5,
        4.15,
        PRISM_DEFEAT_EVENT_ID,
    )
    prism.update({
        "materialType": 27,
        "color": [0.62, 0.94, 1.0, 1.0],
        "emissive": 1.08,
        "enableEnvMap": True,
        "envIntensity": 1.0,
    })
    objects.append(prism)
    for index, z in enumerate((-38.0, -35.75, -33.5, -31.25, -29.0, -26.75, -24.5, -22.25, -20.0), start=1):
        objects.append(make_prism_seal(
            templates["EventReceiver"],
            f"Stage1_V3_PrismSeal_{index:02d}",
            [136.8, 15.2, z],
            2.2,
        ))

    # ボス後は四枚の崩落床を曲線上に並べ、同じ方向へ落ち続ける単調さをなくす。
    collapse_specs = (
        ([158.0, 15.5, -32.0], yaw_from_xz_direction(13.0, -7.0)),
        ([171.0, 14.9, -39.0], yaw_from_xz_direction(13.0, -7.0)),
        ([184.0, 14.3, -46.0], yaw_from_xz_direction(13.0, -6.0)),
        ([197.0, 13.7, -52.0], yaw_from_xz_direction(13.0, -6.0)),
    )
    collapse_events = (5201, 5202, 5203, 5204)
    for index, ((position, yaw), event_id) in enumerate(zip(collapse_specs, collapse_events), start=1):
        floor = make_mechanical_floor(
            templates["MovingFloor"],
            f"Stage1_V3_ChainCollapse_{index:02d}",
            position.copy(),
            "ChainCollapseFloor",
            0,
            1.0,
            yaw=yaw,
        )
        floor["myEventID"] = event_id
        floor["targetID"] = collapse_events[index] if index < len(collapse_events) else -1
        floor["param"].update({
            "gimmickType": "ChainCollapseFloor",
            "shakeDuration": 0.58,
            "fallDuration": 1.6,
            "interval": 0.24,
            "gravity": 42.0,
            "returnOnOff": True,
        })
        objects.append(floor)
        objects.append(make_coin(coin_template, f"Stage1_V3_CollapseGuide_{index:02d}", [position[0], position[1] + 1.8, position[2]]))

    # スター3: 回転床、シーソー、二枚の沈下床を連続させたバランスルート。
    rotating = make_mechanical_floor(
        templates["RotatingFloor"],
        "Stage1_V3_HighRouteRotating",
        [220.0, 15.70, -80.0],
        "RotatingFloor",
        1,
        22.0,
        yaw=0.0,
    )
    rotating["param"].update({"gimmickType": "RotatingFloor", "startActive": True, "returnOnOff": True})
    objects.append(rotating)
    high_seesaw = make_mechanical_floor(
        templates["SeesawFloor"],
        "Stage1_V3_HighRouteSeesaw",
        [233.0, 16.40, -87.0],
        "SeesawFloor",
        0,
        1.0,
        yaw=yaw_from_xz_direction(13.0, -7.0),
    )
    high_seesaw.update({"color": [0.48, 0.88, 1.0, 1.0], "emissive": 1.10})
    objects.append(high_seesaw)
    sinking_positions = (
        [246.0, 17.50, -93.0],
        [257.0, 18.40, -98.0],
    )
    for index, position in enumerate(sinking_positions, start=1):
        sinking = make_mechanical_floor(
            templates["SinkingFloor"],
            f"Stage1_V3_HighRouteSinking_{index:02d}",
            position,
            "SinkingFloor",
            0,
            1.0,
            yaw=yaw_from_xz_direction(11.0, -5.0),
        )
        sinking.update({"color": [0.72, 0.86, 1.0, 1.0], "emissive": 1.06})
        objects.append(sinking)
    configure_star(stars[2], 2, [270.0, 23.65, -99.0])
    objects.append(stars[2])
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V3_Star3Guide",
        (
            (214.0, 16.45, -69.0),
            (220.0, 17.8, -80.0),
            (233.0, 19.1, -87.0),
            (246.0, 20.1, -93.0),
            (257.0, 21.0, -98.0),
            (270.0, 22.0, -99.0),
        ),
    )

    dash = make_gimmick(
        templates["DashPanel"],
        "Stage1_V3_FinalDashPanel",
        [249.0, 13.48, -41.0],
        [1.15, 1.0, 1.0],
        [0.0, yaw_from_xz_direction(18.0, 8.0), 0.0],
    )
    objects.append(dash)

    # 敵は各仕掛けの導入前後に一体ずつ置き、ギミックの学習中には重ねない。
    for enemy_type, name, position, yaw in (
        ("Slime", "Stage1_V3_Enemy_EntrySlime", [-292.0, 0.82, -50.0], 0.0),
        ("FireSlime", "Stage1_V3_Enemy_UpperFire", [-148.0, 6.82, -14.0], 0.8),
        ("WindSlime", "Stage1_V3_Enemy_UpperWind", [-108.0, 6.82, -14.0], -0.8),
        ("ThunderSlime", "Stage1_V3_Enemy_WaterworksThunder", [8.0, 15.82, -52.0], math.pi),
        ("Slime", "Stage1_V3_Enemy_LandingSlime", [217.0, 13.82, -49.0], math.pi),
        ("WindSlime", "Stage1_V3_Enemy_FinalWind", [274.0, 17.82, -28.0], math.pi),
    ):
        objects.append(make_enemy(enemy_templates[enemy_type], name, position, yaw))

    # コイン列は実際のカーブと段差をなぞり、一直線の帯を作らない。
    add_coin_line(objects, coin_template, "Stage1_V3_MainGuide_Entry", (-337.0, 1.45, -50.0), (-302.0, 1.45, -50.0), 5)
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V3_MainGuide_MeadowBend",
        (
            (-286.0, 1.45, -50.0),
            (-268.0, 1.45, -49.0),
            (-250.0, 1.45, -44.0),
            (-233.0, 1.45, -39.0),
            (-216.0, 1.45, -33.0),
        ),
    )
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V3_MainGuide_Steps",
        ((-207.0, 1.45, -32.0), (-194.0, 3.45, -28.0), (-184.0, 5.45, -23.0), (-174.0, 7.45, -18.0)),
    )
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V3_MainGuide_UpperBend",
        (
            (-156.0, 7.45, -14.0),
            (-138.0, 7.45, -14.0),
            (-120.0, 7.45, -15.0),
            (-101.0, 7.45, -20.0),
            (-84.0, 7.45, -27.0),
            (-69.0, 7.45, -35.0),
        ),
    )
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V3_MainGuide_Arena",
        ((72.0, 16.45, -36.0), (84.0, 16.55, -34.0), (100.0, 16.55, -32.0), (116.0, 16.55, -30.0), (130.0, 16.45, -29.0)),
    )
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V3_MainGuide_FinalBend",
        (
            (214.0, 14.45, -52.0),
            (230.0, 14.45, -49.0),
            (246.0, 14.45, -42.0),
            (261.0, 16.45, -36.0),
            (271.0, 18.45, -31.0),
            (286.0, 18.45, -28.0),
        ),
    )

    goal = clone_entry(goal_template, "goal")
    set_transform(goal, [295.0, 19.3, -28.0], [1.0, 1.0, 1.0], [0.0, 0.0, 0.0])
    objects.append(goal)
    stage1["objects"] = objects


def revise_stage_v4(
    stage1: dict[str, Any],
    stage2: dict[str, Any],
    stage3_enemy: dict[str, Any],
    sample_objects: dict[str, Any],
) -> None:
    """一つの区画で一つの遊びを学び、休止地形を挟んで応用するStage 1へ置換する。"""
    previous = object_map(stage1)
    model_template = previous.get("Stage1_Course_V4") or previous.get("Stage1_Course_V3") or first_model(stage1)
    coin_template = first_gimmick((stage1, stage2), "Coin")
    gate_template = previous.get("Stage1_EntranceGate")
    goal_template = previous.get("goal")
    stars = [previous.get(f"Stage1_StarCoin_0{index}") for index in range(1, 4)]
    if gate_template is None or goal_template is None or any(star is None for star in stars):
        raise RuntimeError("入口ゲート、ゴール、スターコインのテンプレートが不足しています。")

    templates = {
        kind: first_gimmick((stage1, stage2, sample_objects), kind)
        for kind in (
            "BreakableBlock", "MovingFloor", "EventReceiver", "DashPanel", "BlinkBlock",
            "TimedSwitch", "AppearingFloor",
        )
    }
    enemy_templates = {
        "Slime": find_enemy(stage1, "Slime"),
        "FireSlime": find_enemy(stage1, "FireSlime"),
        "PrismSlime": find_enemy(stage1, "PrismSlime"),
        "GiantSlime": find_enemy(stage1, "Slime"),
    }

    objects: list[dict[str, Any]] = [
        make_water(model_template, "Stage1_Ocean", [240.0, WATER_SURFACE_Y - 3.5, -25.0], [720.0, 3.5, 320.0]),
        make_visual_model(model_template, "Stage1_Course_V4", "Stages/star_garden_course_v4", [0.0, 0.0, 0.0]),
        make_visual_model(model_template, "Stage1_Backdrop_V4", "Stages/star_garden_backdrop_v2", [200.0, 0.0, 0.0], [1.8, 1.0, 1.5]),
    ]
    for label, position, half_size, yaw in stage_v4_floor_specs():
        objects.append(make_invisible_box(
            model_template,
            f"Stage1_Collision_V4_{label}",
            position.copy(),
            half_size.copy(),
            yaw,
        ))

    gate = clone_entry(gate_template, "Stage1_EntranceGate")
    gate.update({
        "type": "Gimmick",
        "gimmickType": "StageGate",
        "modelName": "Gimmicks/crown_stage_gate",
        "isStatic": False,
    })
    gate.setdefault("param", {}).update({
        "gimmickType": "StageGate",
        "actionMode": 1,
        "targetScene": "SELECT",
        "startActive": True,
        "returnOnOff": True,
    })
    set_transform(gate, [-352.0, 3.01, -50.0], [1.0, 1.0, 1.0], [0.0, math.pi * 0.5, 0.0])
    objects.append(gate)
    objects.extend(make_stage_entry_assembly(model_template, gate))

    # 1. 導入はGhost Recorderの三軌道だけに絞り、水平→奥行き→上下の順で学習する。
    moving_specs = (
        ("Stage1_V4_MovingIntro_A", [-267.0, 0.70, -50.0], "stage1_v4_moving_intro_a", "Stages/star_garden_moving_platform", 0.0),
        ("Stage1_V4_MovingIntro_B", [-239.0, 2.40, -50.0], "stage1_v4_moving_intro_b", "Stages/star_garden_moving_platform", math.pi * 0.5),
        ("Stage1_V4_MovingIntro_C", [-222.0, 4.50, -42.0], "stage1_v4_moving_intro_c", "Stages/star_garden_lift_platform", 0.0),
    )
    for name, position, path_name, model_name, yaw in moving_specs:
        floor = make_ghost_moving_floor(templates["MovingFloor"], name, position, path_name, yaw)
        floor["modelName"] = model_name
        objects.append(floor)
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V4_MovingGuide",
        (
            (-278.0, 1.6, -50.0),
            (-258.0, 2.2, -50.0),
            (-239.0, 4.0, -50.0),
            (-222.0, 6.2, -42.0),
            (-202.0, 7.5, -42.0),
        ),
    )

    # 2. 大型スライムを吸収して装甲突進を得た時だけ、寄り道の能力ゲートが開く。
    giant_ability_boss = make_enemy(
        enemy_templates["GiantSlime"],
        "Stage1_V4_GiantSlime_AbilityBoss",
        [-178.0, 7.82, -42.0],
        math.pi * 0.5,
        1.25,
    )
    giant_ability_boss["enemyType"] = "GiantSlime"
    giant_ability_boss.setdefault("param", {})["enemyType"] = "GiantSlime"
    objects.append(giant_ability_boss)
    objects.append(make_giant_rush_gate(
        templates["BreakableBlock"],
        "Stage1_V4_GiantRushGate",
        [-178.0, 10.2, -80.2],
        0.0,
    ))
    configure_star(stars[0], 0, [-178.0, 9.65, -98.0])
    objects.append(stars[0])
    add_coin_positions(
        objects,
        coin_template,
        "Stage1_V4_Star1Guide",
        ((-178.0, 7.5, -62.0), (-178.0, 7.5, -70.0), (-178.0, 7.5, -76.0)),
    )

    # 能力区画とスイッチ区画の間は、安全な庭園を長く取り、同時処理を要求しない。
    for enemy_type, name, position, yaw in (
        ("Slime", "Stage1_V4_RestGardenSlime", [-122.0, 6.82, -32.0], 0.7),
        ("FireSlime", "Stage1_V4_RestGardenFire", [-91.0, 6.82, -50.0], -0.8),
    ):
        objects.append(make_enemy(enemy_templates[enemy_type], name, position, yaw))
    add_coin_line(objects, coin_template, "Stage1_V4_RestGuide", (-145.0, 7.5, -42.0), (-76.0, 7.5, -38.0), 7)

    # 3. ジャンプ回数で赤青が反転する床。疎な面配置の中に、両色から始められる保証経路を通す。
    blink_coin_floors = {
        "FieldA_02", "FieldA_05", "FieldB_03", "FieldB_07",
        "FieldC_02", "FieldC_05", "StarField_01", "StarField_02",
        "StarField_05", "ReturnA_01", "ReturnB_01", "ReturnB_02",
    }
    for floor_name, position, color_type in stage_v4_blink_floor_specs():
        floor = make_blink_floor(
            templates["BlinkBlock"],
            f"Stage1_V4_BlinkFloor_{floor_name}",
            list(position),
            color_type,
            0.0,
        )
        objects.append(floor)
        if floor_name in blink_coin_floors:
            objects.append(make_coin(
                coin_template,
                f"Stage1_V4_BlinkGuide_{floor_name}",
                [position[0], position[1] + 1.8, position[2]],
            ))

    # スターは安全な台座ではなく、寄り道外周の赤床そのものに直接置く。
    # 床コライダー上面11.77mに、他のスターと同じ2.35mの浮遊間隔を取る。
    configure_star(stars[1], 1, [66.0, 14.12, 78.0])
    objects.append(stars[1])

    # 4. 安全地形から闘技場へ入ると四辺が閉じ、中ボスが演出付きで出現する。
    objects.append(make_checkpoint(goal_template, "Stage1_V4_Checkpoint_PreBoss", [245.0, 10.82, -10.0]))
    objects.append(make_arena_encounter(
        templates["EventReceiver"],
        "Stage1_V4_PrismArena_Encounter",
        [315.0, 10.0, -20.0],
    ))
    prism = make_enemy(
        enemy_templates["PrismSlime"],
        "Stage1_V4_PrismArena_Boss",
        [315.0, 9.77, -20.0],
        -math.pi * 0.5,
        4.15,
        PRISM_DEFEAT_EVENT_ID,
    )
    prism.update({
        "materialType": 27,
        "color": [0.62, 0.94, 1.0, 1.0],
        "emissive": 1.08,
        "enableEnvMap": True,
        "envIntensity": 1.0,
        "collider": {
            "center": [0.0, 0.83, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [0.773809552192688, 0.773809552192688, 0.773809552192688],
            "type": 1,
        },
    })
    prism.setdefault("param", {}).update({
        "enemyType": "PrismSlime",
        "actionMode": 1,
        "shakeDuration": 1.08,
        "startActive": False,
    })
    set_gameplay_link(prism, PRISM_BOSS_EVENT_ID, PRISM_DEFEAT_EVENT_ID)
    objects.append(prism)
    barrier_specs = (
        ("West", [284.0, 16.4, -20.0], [30.0, 6.4, 1.0], math.pi * 0.5),
        ("East", [351.0, 16.4, -20.0], [30.0, 6.4, 1.0], math.pi * 0.5),
        ("North", [317.5, 16.4, -50.0], [33.5, 6.4, 1.0], 0.0),
        ("South", [317.5, 16.4, 10.0], [33.5, 6.4, 1.0], 0.0),
    )
    for index, (side, position, scale, yaw) in enumerate(barrier_specs, start=1):
        objects.append(make_prism_barrier(
            templates["EventReceiver"],
            f"Stage1_V4_PrismBarrier_{side}",
            position,
            scale,
            yaw,
            PRISM_BOSS_EVENT_ID + index,
        ))

    # 5. 一本道のダッシュ区画は三つの落とし穴を段階的に広げ、各穴の直前にだけパネルを置く。
    for index, x in enumerate((392.0, 435.0, 478.0), start=1):
        dash = make_gimmick(
            templates["DashPanel"],
            f"Stage1_V4_DashPanel_{index:02d}",
            [x, 10.02, -20.0],
            [1.0, 1.0, 1.0],
            [0.0, math.pi * 0.5, 0.0],
        )
        dash.update({
            "modelName": "Stages/star_garden_dash_panel",
            "castShadow": True,
            "materialType": 24,
            "color": [1.0, 0.38, 0.04, 1.0],
            "emissive": 1.2,
            "metallic": 0.28,
            "roughness": 0.42,
            "textureTiling": [1.0, 1.0],
            "autoTextureTiling": False,
            "collisionAttribute": 4,
            "collisionMask": 255,
            "collider": {
                "center": [0.0, -0.08, 0.0],
                "rotation": [0.0, 0.0, 0.0],
                "size": [8.35, 0.20, 2.52],
                "type": 3,
            },
        })
        objects.append(dash)
    for index, (start_x, end_x) in enumerate(((396.0, 416.0), (438.0, 457.0), (485.0, 507.0)), start=1):
        add_coin_positions(
            objects,
            coin_template,
            f"Stage1_V4_DashGapGuide_{index:02d}",
            tuple(
                (
                    start_x + (end_x - start_x) * step / 4.0,
                    12.2 + math.sin(math.pi * step / 4.0) * 2.6,
                    -20.0,
                )
                for step in range(5)
            ),
        )

    # 6. ダッシュ後は、番号のない時限スイッチで予告格子上へ出現床を展開する。
    # 本道は高低差のあるS字、スター側は高所から一方向に落下して前方へ復帰する別ルートにする。
    objects.append(make_checkpoint(goal_template, "Stage1_V4_Checkpoint_PostDash", [562.0, 10.82, -20.0]))
    for switch_name, position, target_id in stage_v4_final_switch_specs():
        objects.append(make_timed_floor_switch(
            templates["TimedSwitch"],
            f"Stage1_V4_AppearingSwitch_{switch_name}",
            list(position),
            target_id,
        ))

    marker_colors = {
        "Approach": [0.30, 0.92, 1.0, 1.0],
        "Main": [0.42, 0.96, 0.76, 1.0],
        "StarOut": [1.0, 0.78, 0.24, 1.0],
        "StarReturn": [0.58, 0.78, 1.0, 1.0],
    }
    coin_floors = {
        ("Approach", 1), ("Approach", 2), ("Approach", 3),
        ("Main", 2), ("Main", 4), ("Main", 5),
        ("StarOut", 1), ("StarOut", 3), ("StarOut", 5),
        ("StarReturn", 2), ("StarReturn", 3),
    }
    for route_name, event_id, floor_specs in stage_v4_appearing_route_specs():
        positions = tuple(spec[0] for spec in floor_specs)
        for zero_index, (position, duration) in enumerate(floor_specs):
            if zero_index + 1 < len(positions):
                direction_start = position
                direction_end = positions[zero_index + 1]
            elif zero_index > 0:
                direction_start = positions[zero_index - 1]
                direction_end = position
            else:
                direction_start = position
                direction_end = (position[0] + 1.0, position[1], position[2])
            yaw = yaw_from_xz_direction(
                direction_end[0] - direction_start[0],
                direction_end[2] - direction_start[2],
            )
            index = zero_index + 1
            floor_name = f"Stage1_V4_AppearingFloor_{route_name}_{index:02d}"
            objects.append(make_appearing_floor(
                templates["AppearingFloor"],
                floor_name,
                list(position),
                event_id,
                duration,
                yaw,
            ))
            marker = make_visual_model(
                model_template,
                f"Stage1_V4_AppearingMarker_{route_name}_{index:02d}",
                "Stages/star_garden_appearing_marker",
                [position[0], position[1] - 0.38, position[2]],
                [1.0, 1.0, 1.0],
                [0.0, yaw, 0.0],
            )
            marker.update({
                "castShadow": False,
                "color": marker_colors[route_name],
                "emissive": 1.26,
                "metallic": 0.08,
                "roughness": 0.36,
            })
            objects.append(marker)
            if (route_name, index) in coin_floors:
                objects.append(make_coin(
                    coin_template,
                    f"Stage1_V4_AppearingGuide_{route_name}_{index:02d}",
                    [position[0], position[1] + 1.9, position[2]],
                ))

    # 高所往路から16m落ちた床へ直接スターを置く。着地スイッチが前方復帰ルートだけを開く。
    configure_star(stars[2], 2, [757.0, 15.35, 96.0])
    objects.append(stars[2])

    # 区画の外縁だけを装飾し、進行床と操作対象のシルエットを隠さない。
    for index, (model_name, position, scale, yaw) in enumerate((
        ("Stages/soft_tree", [-332.0, 0.0, -68.0], 2.8, 0.4),
        ("Stages/star_ruin_pillar", [-205.0, 6.0, -58.0], 1.3, 0.2),
        ("Stages/soft_tree", [-112.0, 6.0, -60.0], 2.7, 1.5),
        ("Stages/star_ruin_pillar", [-79.0, 6.0, -22.0], 1.2, 0.0),
        ("Stages/star_ruin_pillar", [225.0, 10.0, -26.0], 1.35, 0.4),
        ("Stages/star_ruin_pillar", [579.0, 10.0, -34.0], 1.35, 0.0),
        ("Stages/soft_tree", [820.0, 17.0, 48.0], 2.8, 1.2),
        ("Stages/star_ruin_pillar", [858.0, 17.0, 4.0], 1.4, 0.3),
    ), start=1):
        objects.append(make_visual_model(
            model_template,
            f"Stage1_Decor_V4_{index:02d}",
            model_name,
            position,
            [scale, scale, scale],
            [0.0, yaw, 0.0],
        ))

    add_coin_line(objects, coin_template, "Stage1_V4_MainGuide_Start", (-338.0, 1.5, -50.0), (-304.0, 1.5, -50.0), 5)
    add_coin_line(objects, coin_template, "Stage1_V4_MainGuide_PreBoss", (210.0, 11.5, 0.0), (270.0, 11.5, -12.0), 6)
    add_coin_line(objects, coin_template, "Stage1_V4_MainGuide_PostDash", (545.0, 11.5, -20.0), (620.0, 13.5, -14.0), 7)
    add_coin_line(objects, coin_template, "Stage1_V4_MainGuide_Goal", (812.0, 18.5, 27.0), (842.0, 18.5, 20.0), 5)

    goal = clone_entry(goal_template, "goal")
    set_transform(goal, [850.0, 19.3, 20.0], [1.0, 1.0, 1.0], [0.0, 0.0, 0.0])
    objects.append(goal)
    stage1["objects"] = objects
