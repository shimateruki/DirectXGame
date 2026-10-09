"""ルートとステージ全体の整合性検証。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Any, Iterable

from .common import (
    ANIMATION_ROOT,
    FINAL_APPROACH_EVENT_ID,
    FINAL_CHOICE_EVENT_ID,
    FINAL_RETURN_EVENT_ID,
    MODEL_ROOT,
    PLAYER_START,
    PRISM_BARRIER_COUNT,
    PRISM_BOSS_EVENT_ID,
    PRISM_DEFEAT_EVENT_ID,
    SECRET_STAIR_EVENT_ID,
    WATER_SURFACE_Y,
    load_json,
    object_map,
    stage_v2_floor_specs,
    stage_v3_floor_specs,
    stage_v4_appearing_route_specs,
    stage_v4_blink_floor_specs,
    stage_v4_blink_paths,
    stage_v4_final_switch_specs,
    stage_v4_floor_specs,
)
from .geometry import (
    collider_world_box,
    load_obj_top_faces_in_engine_space,
    oriented_box_edge_gap,
    oriented_boxes_overlap,
    point_inside_polygon,
    point_inside_terrain_collider,
    point_inside_world_box,
    validate_bomb_grotto_wall,
    validate_bridge_alignment,
    validate_floor_link,
    validate_integrated_collider_visual_contract,
    validate_main_route_strips,
    validate_mechanical_collider_tops,
    validate_model_reference,
    validate_star_supports,
    validate_supported_guide_coins,
    validate_v3_course_mesh_alignment,
    validate_v3_glass_vault,
    validate_visible_support_models,
)

def validate_route_support(objects: list[dict[str, Any]]) -> list[str]:
    """主要地点の床コライダーを標本検査する。ルート全域の連続性までは保証しない。"""
    errors: list[str] = []
    terrain_colliders = [
        entry
        for entry in objects
        if entry.get("type") == "InvisibleBox" and entry.get("name", "").startswith("Stage1_Collision_")
    ]
    samples = [
        ("開始地点", -344.0, -50.0),
        ("入口中庭", -320.0, -42.0),
        ("入口石橋", -271.0, -32.0),
        ("崖道入口", -255.0, -22.0),
        ("ボム洞窟", -238.0, -56.0),
        ("崖道階段", -221.0, 20.0),
        ("上層連絡橋", -166.0, 48.0),
        ("上層庭園", -112.0, 62.0),
        ("水路連絡橋", -65.0, 76.0),
        ("水路遺跡", -25.0, 102.0),
        ("水路上層", 10.0, 128.0),
        ("上層石橋", 45.0, 123.0),
        ("連鎖崩落橋", 102.0, 83.0),
        ("クリスタル戦", 170.0, 42.0),
        ("クリスタル報酬室", 220.5, 42.0),
        ("東城壁西中庭", 250.0, 35.0),
        ("東城壁東中庭", 323.0, 39.0),
        ("可動床列", 397.0, -1.0),
        ("輸送床乗り場", 425.0, -26.0),
        ("輸送床終点", 458.0, -58.0),
        ("ゴール", 501.0, -65.0),
    ]
    mechanical_types = {
        "Trampoline",
        "OneWayFloor",
        "ChainCollapseFloor",
        "SeesawFloor",
        "MovingFloor",
        "RotatingFloor",
        "SinkingFloor",
        "HazardRideFloor",
    }
    mechanical = [
        entry for entry in objects
        if entry.get("gimmickType") in mechanical_types
        and int(entry.get("collisionAttribute", 0)) != 0
    ]
    for label, x, z in samples:
        supported = any(point_inside_terrain_collider(entry, x, z) for entry in terrain_colliders)
        supported = supported or any(point_inside_terrain_collider(entry, x, z) for entry in mechanical)
        if not supported:
            errors.append(f"{label}({x:.1f}, {z:.1f})に地形当たり判定がありません。")

    by_name = {entry.get("name", ""): entry for entry in objects}
    static_route_groups = [
        (
            "入口から水路までの固定本道",
            [
                "Stage1_Collision_EntryWest",
                "Stage1_Collision_EntryEast",
                "Stage1_Collision_EntryBridgeStartAbutment",
                "Stage1_Collision_EntryBridge",
                "Stage1_Collision_EntryBridgeEndAbutment",
                "Stage1_Collision_CliffLowerA",
                "Stage1_Collision_CliffLowerB_South",
                "Stage1_Collision_CliffLowerB_Mid",
                "Stage1_Collision_CliffLowerB_Inner",
                *[f"Stage1_Collision_CliffStep_{index:02d}" for index in range(1, 9)],
                "Stage1_Collision_CliffUpper",
                "Stage1_Collision_UpperBridgeStartAbutment",
                "Stage1_Collision_UpperBridge",
                "Stage1_Collision_UpperBridgeEndAbutment",
                "Stage1_Collision_OrchardMain",
                "Stage1_Collision_OrchardBridgeStartAbutment",
                "Stage1_Collision_OrchardBridge",
                "Stage1_Collision_OrchardBridgeEndAbutment",
                "Stage1_Collision_WaterworksWest",
            ],
            0.75,
            1.15,
        ),
        (
            "水路低層の固定本道",
            [
                "Stage1_Collision_WaterworksWest",
                "Stage1_Collision_WaterworksCenterLink",
                "Stage1_Collision_WaterworksEast_Core",
            ],
            0.50,
            0.20,
        ),
        (
            "水路上層から崩落床までの固定本道",
            [
                "Stage1_Collision_WaterworksUpper",
                "Stage1_Collision_WaterworksUpperBridgeStartAbutment",
                "Stage1_Collision_WaterworksUpperBridge",
                "Stage1_Collision_WaterworksUpperBridgeEndAbutment",
            ],
            0.50,
            0.20,
        ),
        (
            "スター2取得後の固定帰路",
            [
                "Stage1_Collision_OrchardSecretArrival",
                "Stage1_Collision_OrchardSecretReturnBridge",
                "Stage1_Collision_OrchardSecretReturnHigh",
                "Stage1_Collision_OrchardSecretReturnMiddle",
                "Stage1_Collision_OrchardSecretReturnLow",
                "Stage1_Collision_OrchardMain",
            ],
            0.75,
            4.10,
        ),
        (
            "中ボス後の城壁本道",
            [
                "Stage1_Collision_PrismArenaCenter",
                "Stage1_Collision_PrismArenaEastThreshold",
                "Stage1_Collision_PrismRewardRoom",
                "Stage1_Collision_EastRampartWest_Core",
                *[f"Stage1_Collision_EastRampartStep_{index:02d}" for index in range(1, 4)],
                "Stage1_Collision_EastRampartEast",
                "Stage1_Collision_RavineBoardingBase",
                "Stage1_Collision_RavineBoardingPad",
            ],
            0.75,
            1.15,
        ),
        (
            "ゴール着地後の固定本道",
            [
                "Stage1_Collision_RideLandingPad",
                "Stage1_Collision_GoalKeepEntry",
                "Stage1_Collision_GoalKeepCenter",
                "Stage1_Collision_GoalDais",
            ],
            0.75,
            2.90,
        ),
    ]
    for group_label, ordered_names, maximum_gap, maximum_height_delta in static_route_groups:
        for index, (start_name, end_name) in enumerate(zip(ordered_names, ordered_names[1:]), start=1):
            errors.extend(validate_floor_link(
                f"{group_label}{index}～{index + 1}",
                by_name.get(start_name),
                by_name.get(end_name),
                maximum_gap,
                maximum_height_delta,
            ))

    route_groups = [
        (
            "スター2時限出現床",
            [f"Stage1_Secret_AppearingStep_{index:02d}" for index in range(1, 5)],
            3.5,
            2.2,
        ),
        (
            "水路一方通行床",
            [f"Stage1_Waterworks_OneWay_{index:02d}" for index in range(1, 4)],
            2.0,
            3.3,
        ),
        (
            "連鎖崩落床前半",
            [f"Stage1_ChainCollapse_{index:02d}" for index in range(1, 5)],
            2.5,
            1.6,
        ),
        (
            "連鎖崩落床後半",
            [f"Stage1_ChainCollapse_{index:02d}" for index in range(5, 8)],
            2.5,
            1.6,
        ),
        (
            "終盤可動床列",
            [
                "Stage1_Ravine_Seesaw",
                "Stage1_Ravine_Moving",
                "Stage1_Ravine_Rotating",
                "Stage1_Ravine_Sinking",
            ],
            4.2,
            1.8,
        ),
    ]
    for group_label, ordered_names, maximum_gap, maximum_height_delta in route_groups:
        for index, (start_name, end_name) in enumerate(zip(ordered_names, ordered_names[1:]), start=1):
            errors.extend(validate_floor_link(
                f"{group_label}{index}～{index + 1}",
                by_name.get(start_name),
                by_name.get(end_name),
                maximum_gap,
                maximum_height_delta,
            ))

    transitions = [
        ("庭園本道→スター2時限床", "Stage1_Collision_OrchardMain", "Stage1_Secret_AppearingStep_01", 0.75, 2.50),
        ("スター2時限床→固定到着台", "Stage1_Secret_AppearingStep_04", "Stage1_Collision_OrchardSecretArrival", 2.50, 0.25),
        ("水路西岸→トランポリン", "Stage1_Collision_WaterworksWest", "Stage1_Waterworks_Trampoline", 0.75, 1.40),
        ("トランポリン→一方通行床", "Stage1_Waterworks_Trampoline", "Stage1_Waterworks_OneWay_01", 1.00, 3.10),
        ("一方通行床→水路上層", "Stage1_Waterworks_OneWay_03", "Stage1_Collision_WaterworksUpper", 0.75, 0.25),
        ("上層石橋→崩落床前半", "Stage1_Collision_WaterworksUpperBridge", "Stage1_ChainCollapse_01", 2.5, 2.0),
        ("崩落床前半→固定中継塔", "Stage1_ChainCollapse_04", "Stage1_Collision_ChainRelayPad", 2.5, 2.0),
        ("固定中継塔→崩落床後半", "Stage1_Collision_ChainRelayPad", "Stage1_ChainCollapse_05", 2.5, 2.0),
        ("崩落床後半→クリスタル広場", "Stage1_ChainCollapse_07", "Stage1_Collision_PrismArenaCenter", 2.5, 3.5),
        ("東城壁→可動床列", "Stage1_Collision_RavineBoardingPad", "Stage1_Ravine_Seesaw", 2.5, 2.0),
        ("可動床列→輸送床乗り場", "Stage1_Ravine_Sinking", "Stage1_Collision_RideDeparturePad", 2.5, 2.0),
        ("輸送床乗り場→輸送床", "Stage1_Collision_RideDeparturePad", "Stage1_Ravine_HazardRide", 1.0, 1.0),
        ("輸送床終点→ゴール入口", "Stage1_Collision_RideLandingPad", "Stage1_Collision_GoalKeepEntry", 1.0, 1.0),
    ]
    for label, start_name, end_name, maximum_gap, maximum_height_delta in transitions:
        errors.extend(validate_floor_link(
            label,
            by_name.get(start_name),
            by_name.get(end_name),
            maximum_gap,
            maximum_height_delta,
        ))

    moving = by_name.get("Stage1_Ravine_Moving")
    if moving is not None:
        action_mode = int(moving.get("param", {}).get("actionMode", 0))
        move_amount = abs(float(moving.get("param", {}).get("moveAmount", 0.0)))
        if action_mode != 3 or abs(move_amount - 1.2) > 0.01:
            errors.append("終盤移動床の移動方向または移動量が安全設計値と一致していません。")
        else:
            for suffix, offset in (("手前端", -move_amount), ("奥端", move_amount)):
                endpoint = copy.deepcopy(moving)
                endpoint["position"][2] = float(endpoint["position"][2]) + offset
                errors.extend(validate_floor_link(
                    f"終盤移動床{suffix}→シーソー床",
                    endpoint,
                    by_name.get("Stage1_Ravine_Seesaw"),
                    4.2,
                    1.8,
                ))
                errors.extend(validate_floor_link(
                    f"終盤移動床{suffix}→回転床",
                    endpoint,
                    by_name.get("Stage1_Ravine_Rotating"),
                    4.2,
                    1.8,
                ))

    ride = by_name.get("Stage1_Ravine_HazardRide")
    landing = by_name.get("Stage1_Collision_RideLandingPad")
    if ride is not None and landing is not None:
        ride_end = copy.deepcopy(ride)
        ride_position = [float(value) for value in ride.get("position", [0.0, 0.0, 0.0])]
        ride_yaw = float(ride.get("rotation", [0.0, 0.0, 0.0])[1])
        travel_distance = float(ride.get("param", {}).get("moveAmount", 0.0))
        ride_end["position"] = [
            ride_position[0] + math.cos(ride_yaw) * travel_distance,
            ride_position[1],
            ride_position[2] - math.sin(ride_yaw) * travel_distance,
        ]
        errors.extend(validate_floor_link("輸送床の実移動終点→固定着地点", ride_end, landing, 0.25, 0.25))

    errors.extend(validate_mechanical_collider_tops(objects))
    return errors


def validate_stage(data: dict[str, Any], player_data: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    objects = data.get("objects", [])
    names = [entry.get("name", "") for entry in objects]
    if len(names) != len(set(names)):
        errors.append("オブジェクト名が重複しています。")
    guids = [entry.get("guid", "") for entry in objects if entry.get("guid")]
    if len(guids) != len(set(guids)):
        errors.append("GUIDが重複しています。")

    missing_models = sorted({
        entry.get("modelName", "")
        for entry in objects
        if entry.get("modelName") and not validate_model_reference(entry["modelName"])
    })
    if missing_models:
        errors.append("モデル参照が不足しています: " + ", ".join(missing_models))

    by_name = object_map(data)
    stars = [by_name.get(f"Stage1_StarCoin_0{index}") for index in range(1, 4)]
    if any(star is None for star in stars) or [star.get("targetID") for star in stars if star] != [0, 1, 2]:
        errors.append("スターコイン3枚の番号または配置が不正です。")
    errors.extend(validate_star_supports(by_name))
    errors.extend(validate_visible_support_models(by_name))
    errors.extend(validate_integrated_collider_visual_contract(by_name))
    errors.extend(validate_main_route_strips(by_name))
    errors.extend(validate_bridge_alignment(by_name))
    errors.extend(validate_bomb_grotto_wall(objects))
    errors.extend(validate_supported_guide_coins(objects))

    required_gimmicks = {
        "BreakableBlock",
        "TimedSwitch",
        "AppearingFloor",
        "Trampoline",
        "OneWayFloor",
        "ChainCollapseFloor",
        "MovingFloor",
        "RotatingFloor",
        "SeesawFloor",
        "SinkingFloor",
        "DashPanel",
        "HazardRideFloor",
        "FallingSpike",
    }
    active_gimmicks = {entry.get("gimmickType") for entry in objects}
    missing_gimmicks = sorted(required_gimmicks - active_gimmicks)
    if missing_gimmicks:
        errors.append("使用予定ギミックが不足しています: " + ", ".join(missing_gimmicks))

    obsolete_models = {
        "Stages/star_garden_westland",
        "Stages/star_garden_eastland",
        "Stages/star_garden_side_terrace",
        "Stages/star_garden_valley",
        "Stages/star_garden_island",
    }
    if any(entry.get("modelName") in obsolete_models for entry in objects):
        errors.append("旧来の平面庭園または浮遊島モデルが残っています。")
    if any(entry.get("gimmickType") == "LaunchStar" for entry in objects):
        errors.append("旧来の発射スター移動が残っています。")

    water = by_name.get("Stage1_Ocean")
    if not water or water.get("materialType") != 8:
        errors.append("Stage 1下部の水面シェーダーが設定されていません。")
    elif abs(float(water["position"][1]) + float(water["scale"][1]) - WATER_SURFACE_Y) > 0.01:
        errors.append("Stage 1水面の高さが設計値と一致していません。")
    elif float(water["scale"][0]) < 800.0 or float(water["scale"][2]) < 500.0:
        errors.append("Stage 1水面が背景地形を覆うには狭すぎます。")

    if any(
        any(abs(float(axis)) > 0.01 for axis in star.get("rotation", [0.0, 0.0, 0.0]))
        for star in stars
        if star
    ):
        errors.append("スターコインの正面角度が統一されていません。")

    route_objects = [
        entry for entry in objects
        if entry.get("name") not in {"Stage1_Ocean", "Stage1_Ruins_Backdrop"}
        and not entry.get("name", "").startswith(("Stage1_Collision_", "Stage1_WallCollision_"))
    ]
    if route_objects:
        route_x = [float(entry.get("position", [0.0, 0.0, 0.0])[0]) for entry in route_objects]
        if max(route_x) - min(route_x) < 820.0:
            errors.append("Stage 1の主導線が短すぎます。")

    expected_model_by_type = {
        "AppearingFloor": "Stages/star_garden_gimmick_platform",
        "MovingFloor": "Stages/star_garden_moving_platform",
        "RotatingFloor": "Stages/star_garden_rotating_platform",
        "SeesawFloor": "Stages/star_garden_seesaw_platform",
        "SinkingFloor": "Stages/star_garden_sinking_platform",
        "Trampoline": "Stages/star_garden_trampoline_platform",
        "OneWayFloor": "Stages/star_garden_oneway_platform",
        "ChainCollapseFloor": "Stages/star_garden_collapse_platform",
    }
    for gimmick_type, expected_model in expected_model_by_type.items():
        mismatched = [
            entry.get("name", "") for entry in objects
            if entry.get("gimmickType") == gimmick_type and entry.get("modelName") != expected_model
        ]
        if mismatched:
            errors.append(f"{gimmick_type}に判別用モデルが設定されていません: {', '.join(mismatched)}")

    prism = by_name.get("Stage1_Revision_Prism_RuinArena")
    seals = [entry for entry in objects if entry.get("name", "").startswith("Stage1_Revision_PrismSeal_")]
    if not prism or prism.get("targetID") != PRISM_DEFEAT_EVENT_ID:
        errors.append("クリスタルスライム撃破イベントが設定されていません。")
    if len(seals) != 7 or any(entry.get("myEventID") != PRISM_DEFEAT_EVENT_ID for entry in seals):
        errors.append("クリスタルスライム戦の封鎖設定が不正です。")

    switch = by_name.get("Stage1_Secret_TimedSwitch")
    appearing = [entry for entry in objects if entry.get("name", "").startswith("Stage1_Secret_AppearingStep_")]
    if not switch or switch.get("targetID") != SECRET_STAIR_EVENT_ID:
        errors.append("時限スイッチのイベント設定が不正です。")
    if len(appearing) != 4 or any(entry.get("myEventID") != SECRET_STAIR_EVENT_ID for entry in appearing):
        errors.append("出現階段のイベント設定が不正です。")
    appearing_markers = [entry for entry in objects if entry.get("name", "").startswith("Stage1_Secret_AppearingMarker_")]
    if len(appearing_markers) != len(appearing):
        errors.append("非表示になる出現床の常設予告台座が不足しています。")

    chain_floors = [by_name.get(f"Stage1_ChainCollapse_{index:02d}") for index in range(1, 8)]
    if any(entry is None for entry in chain_floors):
        errors.append("連鎖崩落床が前半4枚・後半3枚そろっていません。")
    else:
        expected_groups = ((5101, 5102, 5103, 5104), (5111, 5112, 5113))
        group_offsets = (0, 4)
        for group_index, (event_group, group_offset) in enumerate(zip(expected_groups, group_offsets)):
            group = chain_floors[group_offset:group_offset + len(event_group)]
            for index_in_group, (entry, expected_event) in enumerate(zip(group, event_group)):
                expected_target = event_group[index_in_group + 1] if index_in_group + 1 < len(event_group) else -1
                if entry.get("myEventID") != expected_event or entry.get("targetID") != expected_target:
                    errors.append(f"連鎖崩落床の第{group_index + 1}群イベント接続が不正です。")
                    break

    ride = by_name.get("Stage1_Ravine_HazardRide")
    falling_spikes = [
        by_name.get(f"Stage1_Ravine_FallingSpike_{index:02d}")
        for index in range(1, 5)
    ]
    if not ride or ride.get("gimmickType") != "HazardRideFloor" or ride.get("targetID") != 5300:
        errors.append("妨害付き輸送床の起動設定が不正です。")
    if any(spike is None for spike in falling_spikes):
        errors.append("輸送床に連動する落下棘が4本そろっていません。")
    elif [spike.get("myEventID") for spike in falling_spikes] != [5300, 5301, 5302, 5303]:
        errors.append("輸送床と落下棘のイベント接続が不正です。")
    else:
        for spike in falling_spikes:
            collider = spike.get("collider", {})
            center = collider.get("center", [])
            size = collider.get("size", [])
            expected_center = [0.0, 1.375, 0.0]
            expected_size = [1.1, 1.375, 1.1]
            center_matches = len(center) == 3 and all(
                abs(float(actual) - expected) <= 0.001
                for actual, expected in zip(center, expected_center)
            )
            size_matches = len(size) == 3 and all(
                abs(float(actual) - expected) <= 0.001
                for actual, expected in zip(size, expected_size)
            )
            if not center_matches or not size_matches:
                errors.append("落下棘の見た目と被弾判定の高さが一致していません。")
                break
    spike_markers = [entry for entry in objects if entry.get("name", "").startswith("Stage1_Ravine_FallingSpikeMarker_")]
    if len(spike_markers) != 4:
        errors.append("待機中に非表示になる落下棘の着地点表示が不足しています。")

    if "Stage1_Ruins_FinalBridge" in by_name or "Stage1_Collision_FinalBridge" in by_name:
        errors.append("輸送床と競合する旧最終橋が残っています。")

    gate = by_name.get("Stage1_EntranceGate")
    gate_param = gate.get("param", {}) if gate else {}
    if (
        not gate
        or gate.get("gimmickType") != "StageGate"
        or gate_param.get("actionMode") != 1
        or gate_param.get("targetScene") != "SELECT"
    ):
        errors.append("Stage 1入口ゲートの設定が不正です。")
    for name in (
        "Stage1_EntryDecor_Pad",
        "Stage1_EntryDecor_Frame",
        "Stage1_EntryDecor_Brazier_1",
        "Stage1_EntryDecor_Brazier_2",
        "Stage1_EntryDecor_Flame_1",
        "Stage1_EntryDecor_Flame_2",
    ):
        if name not in by_name:
            errors.append(f"{name}が不足しています。")

    players = [entry for entry in player_data.get("objects", []) if entry.get("type") == "Player"]
    player_position = players[0].get("position", []) if len(players) == 1 else []
    player_matches = len(player_position) == 3 and all(
        abs(float(actual) - expected) <= 0.001
        for actual, expected in zip(player_position, PLAYER_START)
    )
    if not player_matches:
        errors.append("Playerの開始位置が不正です。")
    errors.extend(validate_route_support(objects))
    return errors


def validate_stage_v2(data: dict[str, Any], player_data: dict[str, Any]) -> list[str]:
    """全面置換後のStage 1だけを対象に、旧配置の混入と進行不能を検出する。"""
    errors: list[str] = []
    objects = data.get("objects", [])
    names = [str(entry.get("name", "")) for entry in objects]
    guids = [str(entry.get("guid", "")) for entry in objects]
    by_name = {str(entry.get("name", "")): entry for entry in objects}

    if len(objects) > 150:
        errors.append(f"Stage 1のオブジェクト数が多すぎます: {len(objects)} / 150")
    if len(names) != len(set(names)) or "" in names:
        errors.append("Stage 1に空名または重複名があります。")
    if len(guids) != len(set(guids)) or "" in guids:
        errors.append("Stage 1に空GUIDまたは重複GUIDがあります。")

    required_names = {
        "Stage1_Ocean",
        "Stage1_Course_V2",
        "Stage1_Backdrop_V2",
        "Stage1_EntranceGate",
        "Stage1_V2_PrismArena_Boss",
        "Stage1_V2_Secret_TimedSwitch",
        "Stage1_StarCoin_01",
        "Stage1_StarCoin_02",
        "Stage1_StarCoin_03",
        "goal",
    }
    required_names.update(f"Stage1_Collision_V2_{label}" for label, _position, _half_size in stage_v2_floor_specs())
    required_names.update(f"Stage1_V2_SecretStep_{index:02d}" for index in range(1, 4))
    required_names.update(f"Stage1_V2_ChainCollapse_{index:02d}" for index in range(1, 5))
    missing = sorted(required_names.difference(by_name))
    if missing:
        errors.append("Stage 1の必須要素が不足しています: " + ", ".join(missing))

    legacy_model_fragments = (
        "star_garden_entry_courtyard",
        "star_garden_cliff_pass",
        "star_garden_upper_orchard",
        "star_garden_waterworks",
        "star_garden_prism_arena",
        "star_garden_east_rampart",
        "star_garden_goal_keep",
        "star_garden_backdrop_ridges",
        "star_garden_ride_platform",
    )
    for entry in objects:
        model_name = str(entry.get("modelName", ""))
        if any(fragment in model_name for fragment in legacy_model_fragments):
            errors.append(f"旧Stage 1モデルが残っています: {entry.get('name')} -> {model_name}")
        if model_name and not validate_model_reference(model_name):
            errors.append(f"モデル参照を解決できません: {entry.get('name')} -> {model_name}")

    course = by_name.get("Stage1_Course_V2", {})
    if course.get("modelName") != "Stages/star_garden_course_v2" or course.get("scale") != [1.0, 1.0, 1.0]:
        errors.append("新Stage 1の一体地形モデル設定が不正です。")
    backdrop = by_name.get("Stage1_Backdrop_V2", {})
    if backdrop.get("modelName") != "Stages/star_garden_backdrop_v2":
        errors.append("新Stage 1の遠景モデル設定が不正です。")

    allowed_gimmick_types = {
        "Coin",
        "StageGate",
        "BreakableBlock",
        "TimedSwitch",
        "AppearingFloor",
        "EventReceiver",
        "ChainCollapseFloor",
    }
    actual_gimmick_types = {
        str(entry.get("gimmickType", ""))
        for entry in objects
        if entry.get("type") == "Gimmick"
    }
    unexpected_gimmicks = sorted(actual_gimmick_types.difference(allowed_gimmick_types))
    if unexpected_gimmicks:
        errors.append("新Stage 1へ不要なギミックが混入しています: " + ", ".join(unexpected_gimmicks))

    expected_floor_positions = {
        f"Stage1_Collision_V2_{label}": (position, half_size)
        for label, position, half_size in stage_v2_floor_specs()
    }
    for name, (expected_position, expected_half_size) in expected_floor_positions.items():
        entry = by_name.get(name)
        if entry is None:
            continue
        actual_position = [float(value) for value in entry.get("position", [])]
        actual_half_size = [float(value) for value in entry.get("collider", {}).get("size", [])]
        position_matches = len(actual_position) == 3 and all(
            abs(actual - expected) <= 0.02
            for actual, expected in zip(actual_position, expected_position)
        )
        size_matches = len(actual_half_size) == 3 and all(
            abs(actual - expected) <= 0.02
            for actual, expected in zip(actual_half_size, expected_half_size)
        )
        if entry.get("type") != "InvisibleBox" or not position_matches or not size_matches:
            errors.append(f"固定床の見た目と判定が一致していません: {name}")

    # 本道の固定地形は各区間のX端が接する。崩落橋だけを意図したジャンプ区間として除外する。
    main_route_labels = (
        "StartCourtyard",
        "MeadowLane",
        "StairApproach",
        "MainStep01",
        "MainStep02",
        "MainStep03",
        "UpperGarden",
        "ArenaWestConnector",
        "ArenaWestThreshold",
        "ArenaCenter",
        "ArenaEastInnerThreshold",
        "ArenaEastThreshold",
        "PostBossDeck",
    )
    floor_spec_map = {label: (position, half_size) for label, position, half_size in stage_v2_floor_specs()}
    for left_label, right_label in zip(main_route_labels, main_route_labels[1:]):
        left_position, left_half = floor_spec_map[left_label]
        right_position, right_half = floor_spec_map[right_label]
        left_edge = left_position[0] + left_half[0]
        right_edge = right_position[0] - right_half[0]
        if right_edge - left_edge > 0.05:
            errors.append(f"本道の固定床が途切れています: {left_label} -> {right_label}")
        left_top = left_position[1] + left_half[1]
        right_top = right_position[1] + right_half[1]
        if abs(right_top - left_top) > 2.05:
            errors.append(f"本道の段差が高すぎます: {left_label} -> {right_label}")

    for index, x in enumerate((60.0, 73.0, 86.0, 99.0), start=1):
        floor = by_name.get(f"Stage1_V2_ChainCollapse_{index:02d}", {})
        position = floor.get("position", [])
        expected_target = 5200 + index + 1 if index < 4 else -1
        if (
            floor.get("gimmickType") != "ChainCollapseFloor"
            or len(position) != 3
            or abs(float(position[0]) - x) > 0.02
            or floor.get("targetID") != expected_target
        ):
            errors.append(f"崩落橋{index}の連鎖設定が不正です。")

    star_entries = [
        by_name.get("Stage1_StarCoin_01", {}),
        by_name.get("Stage1_StarCoin_02", {}),
        by_name.get("Stage1_StarCoin_03", {}),
    ]
    if {entry.get("targetID") for entry in star_entries} != {0, 1, 2}:
        errors.append("3枚のスターインデックスが不正です。")

    gate = by_name.get("Stage1_EntranceGate", {})
    gate_param = gate.get("param", {})
    if gate.get("position") != [-352.0, 3.01, -50.0] or gate_param.get("targetScene") != "SELECT":
        errors.append("入口演出用ゲートの位置または戻り先が不正です。")
    for name in (
        "Stage1_EntryDecor_Pad",
        "Stage1_EntryDecor_Frame",
        "Stage1_EntryDecor_Brazier_1",
        "Stage1_EntryDecor_Brazier_2",
        "Stage1_EntryDecor_Flame_1",
        "Stage1_EntryDecor_Flame_2",
    ):
        if name not in by_name:
            errors.append(f"入口演出用オブジェクトが不足しています: {name}")

    players = [entry for entry in player_data.get("objects", []) if entry.get("type") == "Player"]
    player_position = players[0].get("position", []) if len(players) == 1 else []
    if len(player_position) != 3 or any(
        abs(float(actual) - expected) > 0.001
        for actual, expected in zip(player_position, PLAYER_START)
    ):
        errors.append("Playerの開始位置が不正です。")
    return errors


def validate_stage_v3(data: dict[str, Any], player_data: dict[str, Any]) -> list[str]:
    """V3の方向変化・高低差・ギミック展開と、旧配置の不在を検証する。"""
    errors: list[str] = []
    objects = data.get("objects", [])
    names = [str(entry.get("name", "")) for entry in objects]
    guids = [str(entry.get("guid", "")) for entry in objects]
    by_name = {str(entry.get("name", "")): entry for entry in objects}

    if not 145 <= len(objects) <= 215:
        errors.append(f"Stage 1 V3の密度が想定外です: {len(objects)} objects (想定 145～215)")
    if len(names) != len(set(names)) or "" in names:
        errors.append("Stage 1 V3に空名または重複名があります。")
    if len(guids) != len(set(guids)) or "" in guids:
        errors.append("Stage 1 V3に空GUIDまたは重複GUIDがあります。")

    required_names = {
        "Stage1_Ocean",
        "Stage1_Course_V3",
        "Stage1_Backdrop_V3",
        "Stage1_EntranceGate",
        "Stage1_V3_Trampoline",
        "Stage1_V3_WaterworksMovingX",
        "Stage1_V3_WaterworksRotating",
        "Stage1_V3_WaterworksMovingZ",
        "Stage1_V3_WaterworksSeesaw",
        "Stage1_V3_ClockworkVerticalLift",
        "Stage1_V3_PrismArena_Boss",
        "Stage1_V3_HighRouteRotating",
        "Stage1_V3_HighRouteSeesaw",
        "Stage1_V3_HighRouteSinking_01",
        "Stage1_V3_HighRouteSinking_02",
        "Stage1_V3_FinalDashPanel",
        "Stage1_StarCoin_01",
        "Stage1_StarCoin_02",
        "Stage1_StarCoin_03",
        "goal",
    }
    required_names.update(f"Stage1_Collision_V3_{label}" for label, _position, _half_size, _yaw in stage_v3_floor_specs())
    required_names.update(f"Stage1_V3_OneWay_{index:02d}" for index in range(1, 4))
    required_names.update(f"Stage1_V3_GlassVault_{row}_{column}" for row in range(1, 3) for column in range(1, 4))
    required_names.update(f"Stage1_V3_ChainCollapse_{index:02d}" for index in range(1, 5))
    missing = sorted(required_names.difference(by_name))
    if missing:
        errors.append("Stage 1 V3の必須要素が不足しています: " + ", ".join(missing))

    for entry in objects:
        name = str(entry.get("name", ""))
        model_name = str(entry.get("modelName", ""))
        if name.startswith("Stage1_Collision_V2_") or name in {"Stage1_Course_V2", "Stage1_Backdrop_V2"}:
            errors.append(f"旧V2配置が残っています: {name}")
        if model_name == "Stages/star_garden_course_v2":
            errors.append(f"旧V2地形モデルが残っています: {name}")
        if model_name and not validate_model_reference(model_name):
            errors.append(f"モデル参照を解決できません: {name} -> {model_name}")

    course = by_name.get("Stage1_Course_V3", {})
    if (
        course.get("modelName") != "Stages/star_garden_course_v3"
        or course.get("position") != [0.0, 0.0, 0.0]
        or course.get("rotation") != [0.0, 0.0, 0.0]
        or course.get("scale") != [1.0, 1.0, 1.0]
    ):
        errors.append("Stage 1 V3の一体地形モデル設定が不正です。")

    allowed_gimmick_types = {
        "Coin",
        "StageGate",
        "BreakableBlock",
        "TimedSwitch",
        "AppearingFloor",
        "Trampoline",
        "OneWayFloor",
        "MovingFloor",
        "SeesawFloor",
        "EventReceiver",
        "ChainCollapseFloor",
        "RotatingFloor",
        "SinkingFloor",
        "DashPanel",
    }
    actual_gimmick_types = {
        str(entry.get("gimmickType", ""))
        for entry in objects
        if entry.get("type") == "Gimmick"
    }
    unexpected = sorted(actual_gimmick_types.difference(allowed_gimmick_types))
    if unexpected:
        errors.append("Stage 1 V3へ不要なギミックが混入しています: " + ", ".join(unexpected))
    for required_type in ("Trampoline", "OneWayFloor", "MovingFloor", "SeesawFloor", "ChainCollapseFloor", "RotatingFloor", "SinkingFloor", "DashPanel"):
        if required_type not in actual_gimmick_types:
            errors.append(f"Stage 1 V3のギミック展開に{required_type}がありません。")

    for label, expected_position, expected_half_size, expected_yaw in stage_v3_floor_specs():
        name = f"Stage1_Collision_V3_{label}"
        entry = by_name.get(name)
        if entry is None:
            continue
        actual_position = [float(value) for value in entry.get("position", [])]
        actual_half_size = [float(value) for value in entry.get("collider", {}).get("size", [])]
        actual_rotation = [float(value) for value in entry.get("rotation", [])]
        if (
            entry.get("type") != "InvisibleBox"
            or len(actual_position) != 3
            or len(actual_half_size) != 3
            or len(actual_rotation) != 3
            or any(abs(actual - expected) > 0.02 for actual, expected in zip(actual_position, expected_position))
            or any(abs(actual - expected) > 0.02 for actual, expected in zip(actual_half_size, expected_half_size))
            or abs(actual_rotation[1] - expected_yaw) > 0.02
        ):
            errors.append(f"V3固定床の見た目と判定が一致していません: {name}")

    errors.extend(validate_v3_course_mesh_alignment(by_name))

    # 固定床区間は連続し、段差は2m以内。ギミック区間は別途接続を確認する。
    fixed_links = (
        ("StartCourtyard", "MeadowWest", 0.2, 0.2),
        ("MeadowWest", "MeadowEast", 2.5, 0.2),
        ("MeadowEast", "StairApproach", 2.5, 0.2),
        ("StairApproach", "MainStep01", 2.5, 2.1),
        ("MainStep01", "MainStep02", 2.5, 2.1),
        ("MainStep02", "MainStep03", 2.5, 2.1),
        ("MainStep03", "UpperGarden", 1.5, 0.2),
        ("UpperGarden", "UpperBend", 2.5, 0.2),
        ("UpperBend", "TrampolineBoarding", 2.5, 0.2),
        ("WaterworksLanding", "MovingFloorBoarding", 1.0, 0.2),
        ("ArenaWestConnector", "ArenaCenter", 1.5, 0.2),
        ("ArenaCenter", "ArenaEastThreshold", 2.0, 0.2),
        ("ArenaEastThreshold", "PostBossDeck", 1.0, 0.2),
        ("CollapseLanding", "FinalGarden", 2.5, 0.2),
        ("FinalGarden", "FinalStep01", 2.5, 2.1),
        ("FinalStep01", "FinalStep02", 2.5, 2.1),
        ("FinalStep02", "GoalPlaza", 2.5, 0.2),
        ("GoalPlaza", "GoalDais", 0.2, 1.6),
    )
    for left_label, right_label, maximum_gap, maximum_height in fixed_links:
        errors.extend(validate_floor_link(
            f"V3本道 {left_label}→{right_label}",
            by_name.get(f"Stage1_Collision_V3_{left_label}"),
            by_name.get(f"Stage1_Collision_V3_{right_label}"),
            maximum_gap,
            maximum_height,
        ))

    dynamic_links = (
        ("上昇開始", "Stage1_Collision_V3_TrampolineBoarding", "Stage1_V3_Trampoline", 1.0, 1.2),
        ("トランポリン→一方通行床1", "Stage1_V3_Trampoline", "Stage1_V3_OneWay_01", 3.0, 2.2),
        ("一方通行床1→2", "Stage1_V3_OneWay_01", "Stage1_V3_OneWay_02", 3.0, 3.2),
        ("一方通行床2→3", "Stage1_V3_OneWay_02", "Stage1_V3_OneWay_03", 3.5, 3.2),
        ("一方通行床3→水路庭園", "Stage1_V3_OneWay_03", "Stage1_Collision_V3_WaterworksLanding", 3.0, 0.2),
        ("移動床乗り場→X移動床", "Stage1_Collision_V3_MovingFloorBoarding", "Stage1_V3_WaterworksMovingX", 3.0, 1.0),
        ("X移動床→回転床", "Stage1_V3_WaterworksMovingX", "Stage1_V3_WaterworksRotating", 2.0, 0.3),
        ("回転床→Z移動床", "Stage1_V3_WaterworksRotating", "Stage1_V3_WaterworksMovingZ", 2.0, 0.3),
        ("Z移動床→北側固定島", "Stage1_V3_WaterworksMovingZ", "Stage1_Collision_V3_ClockworkNorthLanding", 2.3, 1.0),
        ("北側固定島→シーソー", "Stage1_Collision_V3_ClockworkNorthLanding", "Stage1_V3_WaterworksSeesaw", 1.5, 1.2),
        ("シーソー→闘技場", "Stage1_V3_WaterworksSeesaw", "Stage1_Collision_V3_ArenaWestConnector", 3.0, 1.2),
        ("北側固定島→上下リフト", "Stage1_Collision_V3_ClockworkNorthLanding", "Stage1_V3_ClockworkVerticalLift", 0.5, 0.2),
        ("上下リフト→スター2高台", "Stage1_V3_ClockworkVerticalLift", "Stage1_Collision_V3_ClockworkStarBalcony", 0.2, 8.1),
        ("スター2高台→落下帰路", "Stage1_Collision_V3_ClockworkStarBalcony", "Stage1_Collision_V3_ClockworkReturnDrop", 0.2, 8.1),
        ("スター2帰路→北側固定島", "Stage1_Collision_V3_ClockworkReturnDrop", "Stage1_Collision_V3_ClockworkNorthLanding", 0.2, 0.2),
        ("ボス後床→崩落床1", "Stage1_Collision_V3_PostBossDeck", "Stage1_V3_ChainCollapse_01", 3.0, 1.2),
        ("崩落床1→2", "Stage1_V3_ChainCollapse_01", "Stage1_V3_ChainCollapse_02", 4.0, 1.0),
        ("崩落床2→3", "Stage1_V3_ChainCollapse_02", "Stage1_V3_ChainCollapse_03", 4.0, 1.0),
        ("崩落床3→4", "Stage1_V3_ChainCollapse_03", "Stage1_V3_ChainCollapse_04", 4.0, 1.0),
        ("崩落床4→着地点", "Stage1_V3_ChainCollapse_04", "Stage1_Collision_V3_CollapseLanding", 3.0, 1.2),
        ("高台入口→回転床", "Stage1_Collision_V3_HighRouteEntry", "Stage1_V3_HighRouteRotating", 3.0, 1.2),
        ("回転床→シーソー", "Stage1_V3_HighRouteRotating", "Stage1_V3_HighRouteSeesaw", 3.0, 1.3),
        ("シーソー→沈む床1", "Stage1_V3_HighRouteSeesaw", "Stage1_V3_HighRouteSinking_01", 3.0, 1.0),
        ("沈む床1→2", "Stage1_V3_HighRouteSinking_01", "Stage1_V3_HighRouteSinking_02", 3.0, 1.0),
        ("沈む床2→スター高台", "Stage1_V3_HighRouteSinking_02", "Stage1_Collision_V3_HighRouteBalcony", 3.0, 1.2),
        ("スター高台→落下帰路", "Stage1_Collision_V3_HighRouteBalcony", "Stage1_Collision_V3_HighRouteDropLanding", 0.2, 7.1),
        ("落下帰路→下段帰路", "Stage1_Collision_V3_HighRouteDropLanding", "Stage1_Collision_V3_HighRouteReturnLow", 1.0, 0.2),
        ("下段帰路→最終庭園", "Stage1_Collision_V3_HighRouteReturnLow", "Stage1_Collision_V3_FinalGarden", 3.0, 0.2),
    )
    for label, start_name, end_name, maximum_gap, maximum_height in dynamic_links:
        errors.extend(validate_floor_link(
            f"V3ギミック区間 {label}",
            by_name.get(start_name),
            by_name.get(end_name),
            maximum_gap,
            maximum_height,
        ))

    expected_counts = {
        "AppearingFloor": 0,
        "OneWayFloor": 3,
        "MovingFloor": 3,
        "SeesawFloor": 2,
        "ChainCollapseFloor": 4,
        "RotatingFloor": 2,
        "SinkingFloor": 2,
        "Trampoline": 1,
        "DashPanel": 1,
    }
    for gimmick_type, expected_count in expected_counts.items():
        count = sum(entry.get("gimmickType") == gimmick_type for entry in objects)
        if count != expected_count:
            errors.append(f"{gimmick_type}の配置数が不正です: {count} / {expected_count}")

    movement_specs = {
        "Stage1_V3_WaterworksMovingX": (2, 4.5, 1.05),
        "Stage1_V3_WaterworksMovingZ": (3, 5.5, 0.95),
        "Stage1_V3_ClockworkVerticalLift": (4, 8.0, 1.10),
    }
    for name, (expected_mode, expected_amount, minimum_speed) in movement_specs.items():
        param = by_name.get(name, {}).get("param", {})
        if (
            param.get("actionMode") != expected_mode
            or abs(float(param.get("moveAmount", 0.0)) - expected_amount) > 0.02
            or float(param.get("speed", 0.0)) < minimum_speed - 0.02
        ):
            errors.append(f"{name}の移動軸、移動量、または周期が不正です。")

    waterworks_rotating = by_name.get("Stage1_V3_WaterworksRotating", {}).get("param", {})
    if (
        waterworks_rotating.get("actionMode") != 2
        or float(waterworks_rotating.get("speed", 0.0)) < 33.98
    ):
        errors.append("水路庭園の回転床が横倒し回転になっていません。")

    lift_box = collider_world_box(by_name.get("Stage1_V3_ClockworkVerticalLift", {}))
    balcony_box = collider_world_box(by_name.get("Stage1_Collision_V3_ClockworkStarBalcony", {}))
    if lift_box is None or balcony_box is None:
        errors.append("スター2の上下リフトまたは到着高台に有効な判定がありません。")
    else:
        lift_amount = float(by_name["Stage1_V3_ClockworkVerticalLift"].get("param", {}).get("moveAmount", 0.0))
        if abs(float(lift_box["top"]) + lift_amount - float(balcony_box["top"])) > 0.05:
            errors.append("スター2の上下リフト最高点と高台の歩行面が一致していません。")

    legacy_timed_names = [name for name in names if name.startswith("Stage1_V3_Timed")]
    if legacy_timed_names:
        errors.append("旧スター2時限足場が残っています: " + ", ".join(sorted(legacy_timed_names)))

    collapse_events = (5201, 5202, 5203, 5204)
    for index, event_id in enumerate(collapse_events, start=1):
        floor = by_name.get(f"Stage1_V3_ChainCollapse_{index:02d}", {})
        expected_target = collapse_events[index] if index < len(collapse_events) else -1
        if floor.get("myEventID") != event_id or floor.get("targetID") != expected_target:
            errors.append(f"V3崩落橋{index}のイベント連鎖が不正です。")

    errors.extend(validate_mechanical_collider_tops(objects))
    errors.extend(validate_v3_glass_vault(objects))
    star_entries = [by_name.get(f"Stage1_StarCoin_0{index}", {}) for index in range(1, 4)]
    if {entry.get("targetID") for entry in star_entries} != {0, 1, 2}:
        errors.append("V3の3枚のスターインデックスが不正です。")
    first_position = [float(value) for value in star_entries[0].get("position", [0.0, 0.0, 0.0])]
    second_position = [float(value) for value in star_entries[1].get("position", [0.0, 0.0, 0.0])]
    if math.dist(first_position, second_position) < 300.0:
        errors.append("スター1とスター2の空間的な間隔が不足しています。")
    if second_position != [58.0, 26.65, 10.0]:
        errors.append("スター2が水路庭園の上下リフト終点にありません。")

    gate = by_name.get("Stage1_EntranceGate", {})
    if gate.get("position") != [-352.0, 3.01, -50.0] or gate.get("param", {}).get("targetScene") != "SELECT":
        errors.append("V3入口ゲートの位置または戻り先が不正です。")
    players = [entry for entry in player_data.get("objects", []) if entry.get("type") == "Player"]
    player_position = players[0].get("position", []) if len(players) == 1 else []
    if len(player_position) != 3 or any(abs(float(actual) - expected) > 0.001 for actual, expected in zip(player_position, PLAYER_START)):
        errors.append("Playerの開始位置が不正です。")
    return errors


def validate_v4_course_mesh_alignment(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """V4の一体地形上面が、全固定床コライダーを実際に覆うことを確認する。"""
    errors: list[str] = []
    top_faces = load_obj_top_faces_in_engine_space("Stages/star_garden_course_v4")
    if not top_faces:
        return ["Stage 1 V4地形OBJの上面を読み取れません。"]

    for label, _position, _half_size, _yaw in stage_v4_floor_specs():
        name = f"Stage1_Collision_V4_{label}"
        box = collider_world_box(by_name.get(name, {}))
        if box is None:
            continue
        center_x, _center_y, center_z = box["center"]
        half_x, _half_y, half_z = box["half"]
        axis_x, axis_z = box["axes"]
        samples = [(center_x, center_z)]
        for sign_x, sign_z in ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)):
            samples.append((
                center_x + axis_x[0] * half_x * sign_x * 0.55 + axis_z[0] * half_z * sign_z * 0.55,
                center_z + axis_x[1] * half_x * sign_x * 0.55 + axis_z[1] * half_z * sign_z * 0.55,
            ))
        surface_y = float(box["top"])
        for sample_x, sample_z in samples:
            if not any(
                abs(face_y - surface_y) <= 0.03 and point_inside_polygon((sample_x, sample_z), polygon)
                for face_y, polygon in top_faces
            ):
                errors.append(
                    f"V4地形OBJと固定床判定がずれています: {name} "
                    f"sample=({sample_x:.2f}, {surface_y:.2f}, {sample_z:.2f})"
                )
                break
    return errors


def validate_stage_v4(data: dict[str, Any], player_data: dict[str, Any]) -> list[str]:
    """区画ごとの主題、休止地形、スター導線、チェックポイントをまとめて検証する。"""
    errors: list[str] = []
    objects = data.get("objects", [])
    names = [str(entry.get("name", "")) for entry in objects]
    guids = [str(entry.get("guid", "")) for entry in objects]
    by_name = {str(entry.get("name", "")): entry for entry in objects}

    if not 120 <= len(objects) <= 260:
        errors.append(f"Stage 1 V4の密度が想定外です: {len(objects)} objects (想定 120～260)")
    if "" in names or len(names) != len(set(names)):
        errors.append("Stage 1 V4に空名または重複名があります。")
    if "" in guids or len(guids) != len(set(guids)):
        errors.append("Stage 1 V4に空GUIDまたは重複GUIDがあります。")

    required_names = {
        "Stage1_Ocean",
        "Stage1_Course_V4",
        "Stage1_Backdrop_V4",
        "Stage1_EntranceGate",
        "Stage1_V4_MovingIntro_A",
        "Stage1_V4_MovingIntro_B",
        "Stage1_V4_MovingIntro_C",
        "Stage1_V4_GiantSlime_AbilityBoss",
        "Stage1_V4_GiantRushGate",
        "Stage1_V4_Checkpoint_PreBoss",
        "Stage1_V4_PrismArena_Encounter",
        "Stage1_V4_PrismArena_Boss",
        "Stage1_V4_Checkpoint_PostDash",
        "Stage1_StarCoin_01",
        "Stage1_StarCoin_02",
        "Stage1_StarCoin_03",
        "goal",
    }
    required_names.update(
        f"Stage1_V4_PrismBarrier_{side}"
        for side in ("West", "East", "North", "South")
    )
    required_names.update(
        f"Stage1_Collision_V4_{label}"
        for label, _position, _half_size, _yaw in stage_v4_floor_specs()
    )
    required_names.update(
        f"Stage1_V4_BlinkFloor_{floor_name}"
        for floor_name, _position, _color_type in stage_v4_blink_floor_specs()
    )
    required_names.update(f"Stage1_V4_DashPanel_{index:02d}" for index in range(1, 4))
    required_names.update(
        f"Stage1_V4_AppearingSwitch_{switch_name}"
        for switch_name, _position, _target_id in stage_v4_final_switch_specs()
    )
    required_names.update(
        f"Stage1_V4_{kind}_{route_name}_{index:02d}"
        for route_name, _event_id, floor_specs in stage_v4_appearing_route_specs()
        for index in range(1, len(floor_specs) + 1)
        for kind in ("AppearingFloor", "AppearingMarker")
    )
    missing = sorted(required_names.difference(by_name))
    if missing:
        errors.append("Stage 1 V4の必須要素が不足しています: " + ", ".join(missing))

    legacy_prism_seals = [name for name in names if name.startswith("Stage1_V4_PrismSeal_")]
    if legacy_prism_seals:
        errors.append("旧式の出口だけを塞ぐプリズム棘が残っています: " + ", ".join(legacy_prism_seals))

    prism = by_name.get("Stage1_V4_PrismArena_Boss", {})
    prism_param = prism.get("param", {})
    prism_link = prism.get("components", {}).get("GameplayLink", {})
    if (
        prism.get("enemyType") != "PrismSlime"
        or prism.get("myEventID") != PRISM_BOSS_EVENT_ID
        or prism.get("targetID") != PRISM_DEFEAT_EVENT_ID
        or prism_link.get("eventId") != PRISM_BOSS_EVENT_ID
        or prism_link.get("targetId") != PRISM_DEFEAT_EVENT_ID
        or prism_param.get("actionMode") != 1
        or prism_param.get("startActive") is not False
    ):
        errors.append("V4中ボスが遭遇前待機・撃破通知のイベント構成になっていません。")

    encounter = by_name.get("Stage1_V4_PrismArena_Encounter", {})
    encounter_param = encounter.get("param", {})
    encounter_link = encounter.get("components", {}).get("GameplayLink", {})
    if (
        encounter.get("gimmickType") != "ArenaEncounter"
        or encounter.get("collisionAttribute") != 16
        or encounter.get("collisionMask") != 1
        or encounter.get("myEventID") != PRISM_DEFEAT_EVENT_ID
        or encounter.get("targetID") != PRISM_BOSS_EVENT_ID
        or encounter_link.get("eventId") != PRISM_DEFEAT_EVENT_ID
        or encounter_link.get("targetId") != PRISM_BOSS_EVENT_ID
        or encounter_param.get("maxCount") != PRISM_BARRIER_COUNT
        or encounter_param.get("startActive") is not True
    ):
        errors.append("V4中ボス遭遇管理の進入・封鎖・撃破解除リンクが不正です。")

    expected_barriers = {
        "West": ([284.0, 16.4, -20.0], [30.0, 6.4, 1.0], math.pi * 0.5),
        "East": ([351.0, 16.4, -20.0], [30.0, 6.4, 1.0], math.pi * 0.5),
        "North": ([317.5, 16.4, -50.0], [33.5, 6.4, 1.0], 0.0),
        "South": ([317.5, 16.4, 10.0], [33.5, 6.4, 1.0], 0.0),
    }
    for index, (side, (expected_position, expected_scale, expected_yaw)) in enumerate(expected_barriers.items(), start=1):
        barrier = by_name.get(f"Stage1_V4_PrismBarrier_{side}", {})
        barrier_param = barrier.get("param", {})
        barrier_link = barrier.get("components", {}).get("GameplayLink", {})
        actual_position = [float(value) for value in barrier.get("position", [])]
        actual_scale = [float(value) for value in barrier.get("scale", [])]
        actual_rotation = [float(value) for value in barrier.get("rotation", [])]
        if (
            barrier.get("gimmickType") != "PrismBarrier"
            or barrier.get("modelName") != "Gimmicks/portal_surface"
            or barrier.get("materialType") != 22
            or barrier.get("myEventID") != PRISM_BOSS_EVENT_ID + index
            or barrier_link.get("eventId") != PRISM_BOSS_EVENT_ID + index
            or barrier_param.get("startActive") is not False
            or barrier_param.get("returnOnOff") is not True
            or len(actual_position) != 3
            or len(actual_scale) != 3
            or len(actual_rotation) != 3
            or any(abs(actual - expected) > 0.02 for actual, expected in zip(actual_position, expected_position))
            or any(abs(actual - expected) > 0.02 for actual, expected in zip(actual_scale, expected_scale))
            or abs(actual_rotation[1] - expected_yaw) > 0.02
        ):
            errors.append(f"V4闘技場の{side}プリズム障壁が外周と一致していません。")

    for entry in objects:
        name = str(entry.get("name", ""))
        model_name = str(entry.get("modelName", ""))
        if "_V3_" in name or name in {"Stage1_Course_V3", "Stage1_Backdrop_V3"}:
            errors.append(f"旧V3配置が残っています: {name}")
        if model_name == "Stages/star_garden_course_v3":
            errors.append(f"旧V3地形モデルが残っています: {name}")
        if model_name and not validate_model_reference(model_name):
            errors.append(f"モデル参照を解決できません: {name} -> {model_name}")

    course = by_name.get("Stage1_Course_V4", {})
    if (
        course.get("modelName") != "Stages/star_garden_course_v4"
        or course.get("position") != [0.0, 0.0, 0.0]
        or course.get("rotation") != [0.0, 0.0, 0.0]
        or course.get("scale") != [1.0, 1.0, 1.0]
    ):
        errors.append("Stage 1 V4の一体地形モデル設定が不正です。")

    for label, expected_position, expected_half_size, expected_yaw in stage_v4_floor_specs():
        name = f"Stage1_Collision_V4_{label}"
        entry = by_name.get(name)
        if entry is None:
            continue
        actual_position = [float(value) for value in entry.get("position", [])]
        actual_half_size = [float(value) for value in entry.get("collider", {}).get("size", [])]
        actual_rotation = [float(value) for value in entry.get("rotation", [])]
        if (
            entry.get("type") != "InvisibleBox"
            or len(actual_position) != 3
            or len(actual_half_size) != 3
            or len(actual_rotation) != 3
            or any(abs(actual - expected) > 0.02 for actual, expected in zip(actual_position, expected_position))
            or any(abs(actual - expected) > 0.02 for actual, expected in zip(actual_half_size, expected_half_size))
            or abs(actual_rotation[1] - expected_yaw) > 0.02
        ):
            errors.append(f"V4固定床の見た目と判定が一致していません: {name}")
    errors.extend(validate_v4_course_mesh_alignment(by_name))

    # 同じ遊びの中は到達可能にし、区画同士は安全地形でつなぐ。
    fixed_links = (
        ("StartCourtyard", "MovingIntro", 0.2, 0.2),
        ("GiantArena", "AbilityRestGarden", 0.2, 0.2),
        ("GiantArena", "StarVaultCorridor", 0.2, 0.2),
        ("StarVaultCorridor", "StarVaultRoom", 2.2, 0.2),
        ("AbilityRestGarden", "BlinkIntro", 0.2, 0.2),
        ("BlinkMainExit", "BossApproachGarden", 0.2, 0.2),
        ("BossApproachGarden", "PreBossCheckpoint", 0.2, 0.2),
        ("PreBossCheckpoint", "ArenaEntry", 1.0, 0.2),
        ("ArenaEntry", "ArenaCenter", 2.5, 0.2),
        ("ArenaCenter", "ArenaExit", 0.2, 0.2),
        ("ArenaExit", "DashStart", 1.0, 0.2),
        ("DashRunC", "PostDashCheckpoint", 3.0, 0.2),
        ("PostDashCheckpoint", "AppearingStart", 3.0, 2.2),
        ("GoalApproach", "GoalPlaza", 3.0, 0.2),
        ("GoalPlaza", "GoalDais", 0.2, 1.6),
    )
    for left, right, maximum_gap, maximum_height in fixed_links:
        errors.extend(validate_floor_link(
            f"V4固定区間 {left}→{right}",
            by_name.get(f"Stage1_Collision_V4_{left}"),
            by_name.get(f"Stage1_Collision_V4_{right}"),
            maximum_gap,
            maximum_height,
        ))

    # ダッシュ路の三つの穴は、通常移動では無視できない幅を保つ。
    for left, right, minimum_gap, maximum_gap in (
        ("DashStart", "DashRunA", 10.0, 14.5),
        ("DashRunA", "DashRunB", 14.0, 18.0),
        ("DashRunB", "DashRunC", 15.0, 19.0),
    ):
        left_box = collider_world_box(by_name.get(f"Stage1_Collision_V4_{left}", {}))
        right_box = collider_world_box(by_name.get(f"Stage1_Collision_V4_{right}", {}))
        if left_box is None or right_box is None:
            continue
        gap = oriented_box_edge_gap(left_box, right_box)
        if not minimum_gap - 0.02 <= gap <= maximum_gap + 0.02:
            errors.append(f"V4ダッシュ区間 {left}→{right} の穴幅が不正です: {gap:.2f}m")

    # 見た目は不規則でも、赤青床と終盤出現床の保証経路はすべて実測する。
    blink_paths = stage_v4_blink_paths()
    appearing_paths = (
        ("出現床導入", (
            "Stage1_Collision_V4_AppearingStart",
            "Stage1_V4_AppearingFloor_Approach_01", "Stage1_V4_AppearingFloor_Approach_02",
            "Stage1_V4_AppearingFloor_Approach_03", "Stage1_Collision_V4_AppearingRelay",
        )),
        ("出現床本道", (
            "Stage1_Collision_V4_AppearingRelay",
            "Stage1_V4_AppearingFloor_Main_01", "Stage1_V4_AppearingFloor_Main_02",
            "Stage1_V4_AppearingFloor_Main_03", "Stage1_V4_AppearingFloor_Main_04",
            "Stage1_V4_AppearingFloor_Main_05", "Stage1_Collision_V4_AppearingMainLanding",
            "Stage1_Collision_V4_GoalApproach",
        )),
        ("スター3上昇往路", (
            "Stage1_Collision_V4_AppearingRelay",
            "Stage1_V4_AppearingFloor_StarOut_01", "Stage1_V4_AppearingFloor_StarOut_02",
            "Stage1_V4_AppearingFloor_StarOut_03", "Stage1_V4_AppearingFloor_StarOut_04",
            "Stage1_V4_AppearingFloor_StarOut_05",
        )),
        ("スター3前方復帰", (
            "Stage1_V4_AppearingFloor_StarOut_06",
            "Stage1_V4_AppearingFloor_StarReturn_01", "Stage1_V4_AppearingFloor_StarReturn_02",
            "Stage1_V4_AppearingFloor_StarReturn_03", "Stage1_Collision_V4_GoalApproach",
        )),
    )
    for route_label, route in (*blink_paths, *appearing_paths):
        for start_name, end_name in zip(route, route[1:]):
            errors.extend(validate_floor_link(
                f"V4 {route_label} {start_name}→{end_name}",
                by_name.get(start_name),
                by_name.get(end_name),
                10.0,
                3.1,
            ))

    # スター直前だけは高所から低所への一方向落下。通常ジャンプで往路へ戻れる高さを許可しない。
    high_floor = by_name.get("Stage1_V4_AppearingFloor_StarOut_05")
    star_floor = by_name.get("Stage1_V4_AppearingFloor_StarOut_06")
    errors.extend(validate_floor_link(
        "V4 スター3一方向落下",
        high_floor,
        star_floor,
        5.0,
        17.0,
    ))
    high_box = collider_world_box(high_floor or {})
    star_box = collider_world_box(star_floor or {})
    if high_box is not None and star_box is not None:
        drop_height = float(high_box["top"]) - float(star_box["top"])
        if not 15.5 <= drop_height <= 16.5:
            errors.append(f"スター3の一方向落差が不正です: {drop_height:.2f}m")

    # 保証経路上の着地床は必ず赤青交互にし、反転後に次の床が消える詰みを防ぐ。
    for route_label, route in blink_paths:
        route_colors = [
            int(by_name.get(name, {}).get("param", {}).get("colorType", -1))
            for name in route
            if name.startswith("Stage1_V4_BlinkFloor_")
        ]
        if any(left == right for left, right in zip(route_colors, route_colors[1:])):
            errors.append(f"V4 {route_label} の赤青床が交互になっていません。")

    expected_counts = {
        "MovingFloor": 3,
        "BlinkBlock": 35,
        "TimedSwitch": 3,
        "Switch": 0,
        "EventReceiver": 0,
        "ArenaEncounter": 1,
        "PrismBarrier": 4,
        "DashPanel": 3,
        "AppearingFloor": 17,
        "ChainCollapseFloor": 0,
        "BreakableBlock": 1,
    }
    for gimmick_type, expected_count in expected_counts.items():
        count = sum(entry.get("gimmickType") == gimmick_type for entry in objects)
        if count != expected_count:
            errors.append(f"{gimmick_type}の配置数が不正です: {count} / {expected_count}")

    for index, expected_x in enumerate((392.0, 435.0, 478.0), start=1):
        panel = by_name.get(f"Stage1_V4_DashPanel_{index:02d}", {})
        collider = panel.get("collider", {})
        if (
            panel.get("modelName") != "Stages/star_garden_dash_panel"
            or panel.get("materialType") != 24
            or panel.get("position") != [expected_x, 10.02, -20.0]
            or panel.get("rotation") != [0.0, math.pi * 0.5, 0.0]
            or panel.get("scale") != [1.0, 1.0, 1.0]
            or collider.get("size") != [8.35, 0.20, 2.52]
            or collider.get("center") != [0.0, -0.08, 0.0]
        ):
            errors.append(f"V4ダッシュパネル{index}の専用モデル、向き、または判定が不正です。")

    dash_model_path = MODEL_ROOT / "Stages/star_garden_dash_panel/star_garden_dash_panel.obj"
    if dash_model_path.is_file():
        dash_model_lines = dash_model_path.read_text(encoding="utf-8").splitlines()
        if not any(line.startswith("vt ") for line in dash_model_lines):
            errors.append("V4ダッシュパネルモデルにアニメーション用UVがありません。")

    semantic_model_specs = {
        "star_garden_moving_platform": (0.505, {"signal_white", "mechanism_dark"}),
        "star_garden_lift_platform": (0.510, {"signal_white", "mechanism_dark"}),
        "star_garden_rotating_platform": (0.340, {"signal_white", "goal_dais"}),
        "star_garden_seesaw_platform": (0.840, {"signal_white", "mechanism_dark"}),
        "star_garden_sinking_platform": (0.520, {"goal_dais", "mechanism_dark"}),
        "star_garden_oneway_platform": (0.300, {"signal_white", "hazard_dark"}),
        "star_garden_trampoline_platform": (0.680, {"signal_white", "goal_dais"}),
        "star_garden_collapse_platform": (0.400, {"hazard_dark"}),
        "star_garden_blink_platform": (0.270, {"blink_cell_a", "blink_cell_b", "signal_white"}),
        "star_garden_linked_platform": (0.405, {"goal_dais", "hazard_dark"}),
    }
    for model_name, (expected_top, required_materials) in semantic_model_specs.items():
        model_path = MODEL_ROOT / f"Stages/{model_name}/{model_name}.obj"
        if not model_path.is_file():
            errors.append(f"意味を形で示すギミックモデルがありません: {model_name}")
            continue
        model_lines = model_path.read_text(encoding="utf-8").splitlines()
        used_materials = {
            parts[1]
            for line in model_lines
            if (parts := line.split()) and parts[0] == "usemtl" and len(parts) >= 2
        }
        missing_materials = sorted(required_materials.difference(used_materials))
        if missing_materials:
            errors.append(f"{model_name}の識別意匠が不足しています: {', '.join(missing_materials)}")
        vertex_y = [
            float(parts[2])
            for line in model_lines
            if (parts := line.split()) and parts[0] == "v" and len(parts) >= 4
        ]
        if not vertex_y or abs(max(vertex_y) - expected_top) > 0.011:
            actual_top = max(vertex_y) if vertex_y else float("nan")
            errors.append(f"{model_name}の見た目上面が判定面と一致していません: {actual_top:.3f} / {expected_top:.3f}")

    blink_colors: set[int] = set()
    for floor_name, expected_position, expected_color in stage_v4_blink_floor_specs():
        floor = by_name.get(f"Stage1_V4_BlinkFloor_{floor_name}", {})
        param = floor.get("param", {})
        box = collider_world_box(floor)
        expected_top = float(floor.get("position", [0.0, 0.0, 0.0])[1]) + 0.27
        color_type = int(param.get("colorType", -1))
        blink_colors.add(color_type)
        if (
            floor.get("gimmickType") != "BlinkBlock"
            or floor.get("modelName") != "Stages/star_garden_blink_platform"
            or floor.get("position") != list(expected_position)
            or color_type != expected_color
            or box is None
            or abs(float(box["top"]) - expected_top) > 0.03
        ):
            errors.append(f"V4ジャンプON/OFF床{floor_name}の設定が不正です。")
    if blink_colors != {0, 1}:
        errors.append("V4ジャンプON/OFF床に赤青の両状態がありません。")

    # 赤青床が固定地形へ埋まると、色が切り替わったことも着地面も読めなくなる。
    blink_floors = [
        entry
        for entry in objects
        if entry.get("name", "").startswith("Stage1_V4_BlinkFloor_")
    ]
    blink_terrain = [
        entry
        for entry in objects
        if entry.get("name", "").startswith("Stage1_Collision_V4_Blink")
    ]
    for floor in blink_floors:
        floor_box = collider_world_box(floor)
        if floor_box is None:
            continue
        for terrain in blink_terrain:
            terrain_box = collider_world_box(terrain)
            if terrain_box is None:
                continue
            vertical_overlap = min(float(floor_box["top"]), float(terrain_box["top"])) - max(
                float(floor_box["bottom"]),
                float(terrain_box["bottom"]),
            )
            if vertical_overlap > 0.01 and oriented_boxes_overlap(floor_box, terrain_box):
                errors.append(
                    f"赤青床が固定地形へ埋まっています: {floor.get('name', '')} / {terrain.get('name', '')}"
                )

    # 出現床は固定島へ埋めず、同時に現れる床同士も3Dで重ねない。
    appearing_floors = [
        entry for entry in objects
        if str(entry.get("name", "")).startswith("Stage1_V4_AppearingFloor_")
    ]
    fixed_terrain = [
        entry for entry in objects
        if str(entry.get("name", "")).startswith("Stage1_Collision_V4_")
    ]
    for index, floor in enumerate(appearing_floors):
        floor_box = collider_world_box(floor)
        if floor_box is None:
            continue
        candidates = (*fixed_terrain, *appearing_floors[index + 1:])
        for other in candidates:
            other_box = collider_world_box(other)
            if other_box is None:
                continue
            vertical_overlap = min(float(floor_box["top"]), float(other_box["top"])) - max(
                float(floor_box["bottom"]), float(other_box["bottom"]),
            )
            if vertical_overlap > 0.01 and oriented_boxes_overlap(floor_box, other_box):
                errors.append(
                    f"出現床が別の床または固定地形へ埋まっています: "
                    f"{floor.get('name', '')} / {other.get('name', '')}"
                )

    ghost_specs = {
        "Stage1_V4_MovingIntro_A": ("stage1_v4_moving_intro_a", "Stages/star_garden_moving_platform", 0.0),
        "Stage1_V4_MovingIntro_B": ("stage1_v4_moving_intro_b", "Stages/star_garden_moving_platform", math.pi * 0.5),
        "Stage1_V4_MovingIntro_C": ("stage1_v4_moving_intro_c", "Stages/star_garden_lift_platform", 0.0),
    }
    for object_name, (path_name, model_name, yaw) in ghost_specs.items():
        moving_floor = by_name.get(object_name, {})
        recorder = moving_floor.get("recorder", {})
        if (
            recorder.get("recordPathName") != path_name
            or recorder.get("isRecordLoop") is not True
            or recorder.get("isRecordRelative") is not True
            or moving_floor.get("modelName") != model_name
            or moving_floor.get("rotation") != [0.0, yaw, 0.0]
        ):
            errors.append(f"{object_name}のGhost Recorder、軸記号、または専用モデル設定が不正です。")
        path = ANIMATION_ROOT / f"{path_name}.json"
        if not path.is_file():
            errors.append(f"Ghost Recorder Pathがありません: {path_name}")
        else:
            path_data = load_json(path)
            if len(path_data.get("frames", [])) < 121:
                errors.append(f"Ghost Recorder Pathのフレーム数が不足しています: {path_name}")

    gate = by_name.get("Stage1_V4_GiantRushGate", {})
    if gate.get("param", {}).get("actionMode") != 6:
        errors.append("スター1の能力ゲートが大型スライム突進専用になっていません。")
    if by_name.get("Stage1_V4_GiantSlime_AbilityBoss", {}).get("enemyType") != "GiantSlime":
        errors.append("スター1前に能力取得用の大型スライムがいません。")

    checkpoints = [entry for entry in objects if entry.get("eventID") == 4]
    if {entry.get("name") for entry in checkpoints} != {
        "Stage1_V4_Checkpoint_PreBoss",
        "Stage1_V4_Checkpoint_PostDash",
    }:
        errors.append("中ボス前とダッシュ後のチェックポイント配置が不正です。")
    for checkpoint in checkpoints:
        if (
            checkpoint.get("modelName") != "Stages/star_garden_checkpoint"
            or checkpoint.get("targetID") != -1
            or checkpoint.get("meshEffect1")
            or checkpoint.get("meshEffect2")
            or checkpoint.get("gpuParticleName")
            or checkpoint.get("particleName")
            or "components" in checkpoint
            or "lod" in checkpoint
        ):
            errors.append(f"チェックポイントにゴール王冠の設定が残っています: {checkpoint.get('name', '')}")

    expected_group_counts = {
        FINAL_APPROACH_EVENT_ID: 3,
        FINAL_CHOICE_EVENT_ID: 11,
        FINAL_RETURN_EVENT_ID: 3,
    }
    actual_group_counts: dict[int, int] = {}
    for route_name, event_id, floor_specs in stage_v4_appearing_route_specs():
        for index, (expected_position, expected_duration) in enumerate(floor_specs, start=1):
            floor = by_name.get(f"Stage1_V4_AppearingFloor_{route_name}_{index:02d}", {})
            marker = by_name.get(f"Stage1_V4_AppearingMarker_{route_name}_{index:02d}", {})
            actual_group_counts[event_id] = actual_group_counts.get(event_id, 0) + 1
            if (
                floor.get("gimmickType") != "AppearingFloor"
                or floor.get("modelName") != "Stages/star_garden_gimmick_platform"
                or floor.get("position") != list(expected_position)
                or floor.get("myEventID") != event_id
                or floor.get("targetID") != -1
                or abs(float(floor.get("param", {}).get("interval", 0.0)) - expected_duration) > 0.02
                or floor.get("param", {}).get("startActive") is not False
            ):
                errors.append(f"V4出現床{route_name}-{index}の位置、時間、またはイベントIDが不正です。")
            expected_marker_position = [expected_position[0], expected_position[1] - 0.38, expected_position[2]]
            if (
                marker.get("type") != "Model"
                or marker.get("modelName") != "Stages/star_garden_appearing_marker"
                or marker.get("position") != expected_marker_position
                or marker.get("collider", {}).get("type") != 0
            ):
                errors.append(f"V4出現床{route_name}-{index}の予告格子が不正です。")
    if actual_group_counts != expected_group_counts:
        errors.append(f"V4出現床のイベントグループ数が不正です: {actual_group_counts}")

    for switch_name, expected_position, target_id in stage_v4_final_switch_specs():
        switch = by_name.get(f"Stage1_V4_AppearingSwitch_{switch_name}", {})
        collider = switch.get("collider", {})
        if (
            switch.get("gimmickType") != "TimedSwitch"
            or switch.get("modelName") != "Stages/star_garden_toggle_switch"
            or switch.get("position") != list(expected_position)
            or switch.get("targetID") != target_id
            or switch.get("texturePath", "") != ""
            or collider.get("center") != [0.0, -0.11, 0.0]
            or collider.get("size") != [2.8, 0.45, 2.8]
        ):
            errors.append(f"V4終盤スイッチ{switch_name}のモデル、位置、または起動先が不正です。")

    legacy_linked = [name for name in names if name.startswith("Stage1_V4_Linked")]
    if legacy_linked:
        errors.append("旧崩落連鎖床が残っています: " + ", ".join(sorted(legacy_linked)))

    errors.extend(validate_mechanical_collider_tops(objects))
    stars = [by_name.get(f"Stage1_StarCoin_0{index}", {}) for index in range(1, 4)]
    expected_star_positions = (
        [-178.0, 9.65, -98.0],
        [66.0, 14.12, 78.0],
        [757.0, 15.35, 96.0],
    )
    if {entry.get("targetID") for entry in stars} != {0, 1, 2}:
        errors.append("V4の3枚のスターインデックスが不正です。")
    for index, (star, expected) in enumerate(zip(stars, expected_star_positions), start=1):
        if star.get("position") != expected:
            errors.append(f"スター{index}の配置が区画終端からずれています。")
        rotation = [float(value) for value in star.get("rotation", [])]
        if (
            star.get("modelName") != "Gimmicks/star"
            or star.get("eventID") != 7
            or len(rotation) != 3
            or abs(rotation[0]) > 0.001
            or abs(rotation[1]) > 0.001
            or abs(rotation[2]) > 0.001
        ):
            errors.append(f"スター{index}のモデル、収集ID、または向きが不正です。")

    star_floor = by_name.get("Stage1_V4_BlinkFloor_StarField_03", {})
    star_floor_box = collider_world_box(star_floor)
    star2_position = [float(value) for value in stars[1].get("position", [0.0, 0.0, 0.0])]
    star_floor_position = [float(value) for value in star_floor.get("position", [0.0, 0.0, 0.0])]
    if (
        star_floor.get("param", {}).get("colorType") != 1
        or star_floor_box is None
        or star2_position[0] != star_floor_position[0]
        or star2_position[2] != star_floor_position[2]
        or abs(star2_position[1] - float(star_floor_box["top"]) - 2.35) > 0.02
        or "Stage1_V4_BlinkGuide_StarField_03" in by_name
    ):
        errors.append("スター2が案内コインなしで赤青床へ直接配置されていません。")

    star3_floor = by_name.get("Stage1_V4_AppearingFloor_StarOut_06", {})
    star3_floor_box = collider_world_box(star3_floor)
    star3_position = [float(value) for value in stars[2].get("position", [0.0, 0.0, 0.0])]
    star_exit_switch = by_name.get("Stage1_V4_AppearingSwitch_StarExit", {})
    switch_position = [float(value) for value in star_exit_switch.get("position", [0.0, 0.0, 0.0])]
    if (
        star3_floor_box is None
        or not point_inside_world_box(star3_floor_box, star3_position[0], star3_position[2])
        or abs(star3_position[1] - float(star3_floor_box["top"]) - 2.35) > 0.02
        or not point_inside_world_box(star3_floor_box, switch_position[0], switch_position[2])
        or abs((switch_position[1] - 0.56) - float(star3_floor_box["top"])) > 0.02
    ):
        errors.append("スター3または前方復帰スイッチが一方向落下床へ直接配置されていません。")
    if math.dist(expected_star_positions[0], expected_star_positions[1]) < 300.0:
        errors.append("スター1とスター2の区画間隔が不足しています。")
    if math.dist(expected_star_positions[1], expected_star_positions[2]) < 500.0:
        errors.append("スター2とスター3の区画間隔が不足しています。")

    entrance = by_name.get("Stage1_EntranceGate", {})
    if entrance.get("position") != [-352.0, 3.01, -50.0] or entrance.get("param", {}).get("targetScene") != "SELECT":
        errors.append("V4入口ゲートの位置または戻り先が不正です。")
    # スター2と3は本道の最大Zから50m以上外し、通過だけでは触れない寄り道にする。
    if expected_star_positions[1][2] - 28.0 < 50.0:
        errors.append("スター2がジャンプON/OFF本道から十分に分岐していません。")
    if expected_star_positions[2][2] - 25.0 < 50.0:
        errors.append("スター3がID連動床の本道から十分に分岐していません。")
    if by_name.get("goal", {}).get("position") != [850.0, 19.3, 20.0]:
        errors.append("V4ゴールが最終広場にありません。")
    players = [entry for entry in player_data.get("objects", []) if entry.get("type") == "Player"]
    player_position = players[0].get("position", []) if len(players) == 1 else []
    if len(player_position) != 3 or any(
        abs(float(actual) - expected) > 0.001
        for actual, expected in zip(player_position, PLAYER_START)
    ):
        errors.append("Playerの開始位置が不正です。")
    return errors
