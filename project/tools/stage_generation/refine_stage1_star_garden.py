#!/usr/bin/env python3
"""Stage 1を立体的な庭園遺跡コースへ再構成する。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[2]
STAGE1_PATH = ROOT / "Resources/json/3Dobject/stage1_object.json"
STAGE1_PLAYER_PATH = ROOT / "Resources/json/3Dobject/stage1_player.json"
STAGE2_PATH = ROOT / "Resources/json/3Dobject/stage2_object.json"
STAGE3_ENEMY_PATH = ROOT / "Resources/json/3Dobject/stage3_enemy.json"
SAMPLE_OBJECT_PATH = ROOT / "Resources/json/3Dobject/sample_object.json"
ANIMATION_ROOT = ROOT / "Resources/json/animation"
MODEL_ROOT = ROOT / "Resources/3DModel"
GUID_NAMESPACE = uuid.UUID("ed9d8843-2923-4e66-a82e-968d5fb41e26")
PRISM_DEFEAT_EVENT_ID = 4201
PRISM_BOSS_EVENT_ID = 4202
PRISM_BARRIER_COUNT = 4
SECRET_STAIR_EVENT_ID = 4100
FINAL_APPROACH_EVENT_ID = 7401
FINAL_CHOICE_EVENT_ID = 7402
FINAL_RETURN_EVENT_ID = 7403

PLAYER_START = [-344.0, 0.82, -50.0]
ENTRY_CENTER = [-320.0, 0.0, -50.0]
CLIFF_CENTER = [-220.0, 0.0, -5.0]
ORCHARD_CENTER = [-112.0, 8.0, 62.0]
WATERWORKS_CENTER = [0.0, 8.0, 105.0]
ARENA_CENTER = [170.0, 12.0, 42.0]
EAST_RAMPART_CENTER = [280.0, 12.0, 35.0]
GOAL_CENTER = [500.0, 16.0, -65.0]
WATER_SURFACE_Y = -9.5

BRIDGE_SPECS = (
    ("EntryBridge", (-279.0, -39.0), (-263.0, -24.0), 0.18),
    ("UpperBridge", (-174.0, 35.0), (-158.0, 60.0), 8.18),
    ("OrchardBridge", (-70.0, 70.0), (-60.0, 82.0), 8.18),
    ("WaterworksUpperBridge", (36.0, 128.0), (54.0, 118.0), 18.18),
)

# 大きな統合地形の内部にあるだけでは、局所的な足場が本当に描画されているかを
# 保証できない。攻略に必須の固定判定は、同寸法の専用モデルを必ず1対1で持たせる。
CRITICAL_VISIBLE_SUPPORT_COLLIDERS = (
    "Stage1_Collision_CaveMouth",
    "Stage1_Collision_CaveStarPedestalBase",
    "Stage1_Collision_CaveStarPedestalCap",
    "Stage1_Collision_WaterworksCenterLink",
    "Stage1_Collision_OrchardSecretArrival",
    "Stage1_Collision_OrchardSecretReturnBridge",
    "Stage1_Collision_OrchardSecretReturnHigh",
    "Stage1_Collision_OrchardSecretReturnMiddle",
    "Stage1_Collision_OrchardSecretReturnLow",
    "Stage1_Collision_ChainRelayBase",
    "Stage1_Collision_ChainRelayPad",
    "Stage1_Collision_PrismArenaEastThreshold",
    "Stage1_Collision_PrismRewardRoom",
    "Stage1_Collision_PrismRewardPedestalBase",
    "Stage1_Collision_PrismRewardPedestalCap",
    "Stage1_Collision_RavineBoardingBase",
    "Stage1_Collision_RavineBoardingPad",
    "Stage1_Collision_RideDepartureBase",
    "Stage1_Collision_RideDeparturePad",
    "Stage1_Collision_RideLandingBase",
    "Stage1_Collision_RideLandingPad",
    "Stage1_Collision_GoalKeepEntry",
)

# 広い地形の上では、石の帯を本道の輪郭として薄く重ねる。
# 判定を持たないため、この座標はEditorで見た目だけを安全に調整できる。
MAIN_ROUTE_STRIP_SPECS = (
    ("EntryGate", (-344.0, -50.0), (-316.0, -47.0), 0.0, 4.6),
    ("EntryBridgeApproach", (-316.0, -47.0), (-283.0, -40.0), 0.0, 4.6),
    ("CliffApproach", (-259.0, -23.0), (-239.0, -14.0), 0.0, 4.4),
    ("CliffStairApproach", (-239.0, -14.0), (-224.0, -1.0), 0.0, 4.4),
    ("CliffUpper", (-221.0, 34.0), (-178.0, 35.0), 8.0, 4.6),
    ("OrchardWest", (-154.0, 60.0), (-116.0, 60.0), 8.0, 4.8),
    ("OrchardEast", (-116.0, 60.0), (-75.0, 69.0), 8.0, 4.8),
    ("WaterworksWest", (-56.0, 84.0), (-28.0, 99.0), 8.0, 4.6),
    ("WaterworksTrampoline", (-28.0, 99.0), (-18.0, 104.0), 8.0, 4.6),
    ("WaterworksUpper", (10.0, 128.0), (34.0, 128.0), 18.0, 4.6),
    ("ArenaEntry", (141.0, 51.0), (170.0, 42.0), 12.0, 5.2),
    ("ArenaExit", (170.0, 42.0), (204.0, 42.0), 12.0, 5.2),
    ("RampartWest", (224.0, 42.0), (260.0, 39.0), 12.0, 4.8),
    ("RampartSteps", (260.0, 39.0), (278.0, 21.0), 12.0, 4.6),
    ("RampartEast", (295.0, 20.0), (323.0, 39.0), 15.0, 4.8),
    ("RavineApproach", (323.0, 39.0), (352.0, 25.0), 15.0, 4.8),
    ("GoalApproach", (461.0, -58.0), (479.0, -62.0), 16.0, 4.8),
    ("GoalDais", (486.0, -64.0), (501.0, -65.0), 18.9, 5.2),
)

# 統合地形モデルの内側に収めた固定床。当たり判定を追加した際にここへ登録しないと
# 検証で止まるため、「判定だけ追加して見た目を忘れる」状態を作らない。
INTEGRATED_COLLIDER_VISUAL_GROUPS = (
    (
        "Stage1_Ruins_EntryCourtyard",
        "Stages/star_garden_entry_courtyard",
        (
            "Stage1_Collision_EntryWest",
            "Stage1_Collision_EntryEast",
        ),
    ),
    (
        "Stage1_Ruins_CliffPass",
        "Stages/star_garden_cliff_pass",
        (
            "Stage1_Collision_CliffLowerA",
            "Stage1_Collision_CliffLowerB_South",
            "Stage1_Collision_CliffLowerB_Mid",
            "Stage1_Collision_CliffLowerB_Inner",
            "Stage1_Collision_CliffLowerB_North",
            "Stage1_Collision_CaveRoom",
            "Stage1_Collision_CliffUpper",
        ),
    ),
    (
        "Stage1_Ruins_UpperOrchard",
        "Stages/star_garden_upper_orchard",
        (
            "Stage1_Collision_OrchardMain",
            "Stage1_Collision_OrchardNorth",
        ),
    ),
    (
        "Stage1_Ruins_Waterworks",
        "Stages/star_garden_waterworks",
        (
            "Stage1_Collision_WaterworksWest",
            "Stage1_Collision_WaterworksEast_South",
            "Stage1_Collision_WaterworksEast_Core",
            "Stage1_Collision_WaterworksUpper",
        ),
    ),
    (
        "Stage1_Ruins_PrismArena",
        "Stages/star_garden_prism_arena",
        (
            "Stage1_Collision_PrismArenaCenter",
            "Stage1_Collision_PrismArenaNorth",
            "Stage1_Collision_PrismArenaSouth",
        ),
    ),
    (
        "Stage1_Ruins_EastRampart",
        "Stages/star_garden_east_rampart",
        (
            "Stage1_Collision_EastRampartWest_South",
            "Stage1_Collision_EastRampartWest_Core",
            "Stage1_Collision_EastRampartWest_North",
            "Stage1_Collision_EastRampartWest_Cap",
            "Stage1_Collision_EastRampartEast",
            "Stage1_Collision_EastRampartEastNorth",
        ),
    ),
    (
        "Stage1_Ruins_GoalKeep",
        "Stages/star_garden_goal_keep",
        (
            "Stage1_Collision_GoalKeepCenter",
            "Stage1_Collision_GoalKeepNorth",
            "Stage1_Collision_GoalKeepSouth",
            "Stage1_Collision_GoalDais",
        ),
    ),
)


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def write_json(path: Path, data: dict[str, Any]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as destination:
        json.dump(data, destination, ensure_ascii=False, indent=4)
        destination.write("\n")


def stable_guid(name: str) -> str:
    return str(uuid.uuid5(GUID_NAMESPACE, name))


def object_map(data: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {entry["name"]: entry for entry in data.get("objects", [])}


def clone_entry(template: dict[str, Any], name: str) -> dict[str, Any]:
    entry = copy.deepcopy(template)
    entry["name"] = name
    entry["guid"] = stable_guid(name)
    entry["parentGuid"] = ""
    entry["parentName"] = ""
    return entry


def first_model(data: dict[str, Any]) -> dict[str, Any]:
    for entry in data.get("objects", []):
        if entry.get("type") == "Model" and entry.get("modelName"):
            return entry
    raise RuntimeError("Modelテンプレートが見つかりません。")


def first_gimmick(data_sets: Iterable[dict[str, Any]], gimmick_type: str) -> dict[str, Any]:
    for data in data_sets:
        for entry in data.get("objects", []):
            if entry.get("gimmickType") == gimmick_type:
                return entry
    raise RuntimeError(f"{gimmick_type}のテンプレートが見つかりません。")


def find_enemy(data: dict[str, Any], enemy_type: str) -> dict[str, Any]:
    for entry in data.get("objects", []):
        if entry.get("enemyType") == enemy_type:
            return entry
    raise RuntimeError(f"{enemy_type}のテンプレートが見つかりません。")


def quaternion_from_euler(rotation: list[float]) -> list[float]:
    x, y, z = rotation
    sx, cx = math.sin(x * 0.5), math.cos(x * 0.5)
    sy, cy = math.sin(y * 0.5), math.cos(y * 0.5)
    sz, cz = math.sin(z * 0.5), math.cos(z * 0.5)
    return [
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    ]


def set_transform(
    entry: dict[str, Any],
    position: list[float],
    scale: list[float] | None = None,
    rotation: list[float] | None = None,
) -> None:
    entry["position"] = position
    if scale is not None:
        entry["scale"] = scale
    if rotation is not None:
        entry["rotation"] = rotation
        entry["quaternion"] = quaternion_from_euler(rotation)


def set_gameplay_link(entry: dict[str, Any], event_id: int, target_id: int) -> None:
    """旧フィールドとGameplayLinkを同じIDへ揃え、ロード後の上書き差を防ぐ。"""
    entry["myEventID"] = event_id
    entry["targetID"] = target_id
    components = entry.setdefault("components", {})
    link = components.setdefault("GameplayLink", {})
    link.update({
        "_editorPresent": True,
        "eventId": event_id,
        "targetId": target_id,
        "version": 1,
    })


def set_no_collider(entry: dict[str, Any]) -> None:
    entry["collisionAttribute"] = 0
    entry["collisionMask"] = 0
    entry["collider"] = {
        "center": [0.0, 0.0, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [1.0, 1.0, 1.0],
        "type": 0,
    }


def yaw_from_xz_direction(dx: float, dz: float) -> float:
    """ローカル+X軸をXZ平面上の進行方向へ合わせるY回転角を返す。"""
    if math.hypot(dx, dz) <= 1.0e-6:
        return 0.0
    # エンジンは行ベクトルで、ローカル+Xはワールド(cos(yaw), -sin(yaw))へ向く。
    return math.atan2(-dz, dx)


def make_visual_model(
    template: dict[str, Any],
    name: str,
    model_name: str,
    position: list[float],
    scale: list[float] | None = None,
    rotation: list[float] | None = None,
) -> dict[str, Any]:
    entry = clone_entry(template, name)
    entry.update({
        "type": "Model",
        "gimmickType": "",
        "enemyType": "",
        "itemType": "",
        "modelName": model_name,
        "isStatic": True,
        "castShadow": True,
        "color": [1.0, 1.0, 1.0, 1.0],
        "emissive": 1.0,
        "materialType": 0,
        "texturePath": "",
        "normalMapPath": "",
        "ormMapPath": "",
    })
    entry.pop("param", None)
    set_no_collider(entry)
    set_transform(entry, position, scale or [1.0, 1.0, 1.0], rotation or [0.0, 0.0, 0.0])
    return entry


def make_route_strip(
    template: dict[str, Any],
    name: str,
    start: tuple[float, float],
    end: tuple[float, float],
    surface_y: float,
    width: float,
) -> dict[str, Any]:
    """広い固定床の上へ、当たり判定を持たない薄い石の本道を重ねる。"""
    dx = end[0] - start[0]
    dz = end[1] - start[1]
    length = math.hypot(dx, dz)
    strip = make_visual_model(
        template,
        name,
        "Stages/star_garden_support_platform",
        [(start[0] + end[0]) * 0.5, surface_y + 0.035, (start[1] + end[1]) * 0.5],
        [length * 0.5, 0.055, width * 0.5],
        [0.0, yaw_from_xz_direction(dx, dz), 0.0],
    )
    strip.update({
        "castShadow": False,
        "color": [0.94, 0.90, 0.76, 1.0],
        "emissive": 1.03,
        "roughness": 0.72,
    })
    return strip


def make_visual_anchor_tower(
    template: dict[str, Any],
    name: str,
    position_xz: tuple[float, float],
    top_y: float,
    half_width: float,
) -> dict[str, Any]:
    """空中ギミックの位置を水面から読める、判定なしの細い支持塔を作る。"""
    bottom_y = WATER_SURFACE_Y - 0.35
    half_height = max(0.5, (top_y - bottom_y) * 0.5)
    tower = make_visual_model(
        template,
        name,
        "Stages/star_garden_support_platform",
        [position_xz[0], top_y, position_xz[1]],
        [half_width, half_height, half_width],
    )
    tower.update({
        "color": [0.66, 0.70, 0.78, 1.0],
        "emissive": 0.96,
        "roughness": 0.82,
    })
    return tower


def make_invisible_box(
    template: dict[str, Any],
    name: str,
    position: list[float],
    half_size: list[float],
    yaw: float = 0.0,
) -> dict[str, Any]:
    entry = clone_entry(template, name)
    entry.update({
        "type": "InvisibleBox",
        "gimmickType": "",
        "enemyType": "",
        "itemType": "",
        "modelName": "",
        "isStatic": True,
        "castShadow": False,
        "collisionAttribute": 4,
        "collisionMask": 4294967295,
        "collider": {
            "center": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": half_size,
            "type": 3,
        },
    })
    entry.pop("param", None)
    set_transform(entry, position, [1.0, 1.0, 1.0], [0.0, yaw, 0.0])
    return entry


def make_wall_box(
    template: dict[str, Any],
    name: str,
    start: tuple[float, float],
    end: tuple[float, float],
    base_y: float,
    height: float,
    thickness: float,
) -> dict[str, Any]:
    dx = end[0] - start[0]
    dz = end[1] - start[1]
    length = math.hypot(dx, dz)
    return make_invisible_box(
        template,
        name,
        [(start[0] + end[0]) * 0.5, base_y + height * 0.5, (start[1] + end[1]) * 0.5],
        [length * 0.5, height * 0.5, thickness * 0.5],
        yaw_from_xz_direction(dx, dz),
    )


def make_coin(template: dict[str, Any], name: str, position: list[float]) -> dict[str, Any]:
    entry = clone_entry(template, name)
    entry["type"] = "Gimmick"
    entry["gimmickType"] = "Coin"
    entry.setdefault("param", {})["gimmickType"] = "Coin"
    entry["modelName"] = "Gimmicks/koin"
    set_transform(entry, position, [0.055, 0.055, 0.055], [0.0, 0.0, 0.0])
    return entry


def add_coin_line(
    objects: list[dict[str, Any]],
    template: dict[str, Any],
    prefix: str,
    start: tuple[float, float, float],
    end: tuple[float, float, float],
    count: int,
) -> None:
    for index in range(count):
        t = index / max(1, count - 1)
        position = [
            start[0] + (end[0] - start[0]) * t,
            start[1] + (end[1] - start[1]) * t,
            start[2] + (end[2] - start[2]) * t,
        ]
        objects.append(make_coin(template, f"{prefix}_{index + 1:02d}", position))


def add_coin_positions(
    objects: list[dict[str, Any]],
    template: dict[str, Any],
    prefix: str,
    positions: Iterable[tuple[float, float, float]],
) -> None:
    """段差や曲がり角へ、補間ではなく支持床に合わせたコインを置く。"""
    for index, position in enumerate(positions, start=1):
        objects.append(make_coin(template, f"{prefix}_{index:02d}", list(position)))


def make_gimmick(
    template: dict[str, Any],
    name: str,
    position: list[float],
    scale: list[float],
    rotation: list[float] | None = None,
) -> dict[str, Any]:
    entry = clone_entry(template, name)
    entry["type"] = "Gimmick"
    gimmick_type = entry.get("gimmickType", "")
    entry.setdefault("param", {})["gimmickType"] = gimmick_type
    entry["isStatic"] = False
    set_transform(entry, position, scale, rotation or [0.0, 0.0, 0.0])
    return entry


def make_mechanical_floor(
    template: dict[str, Any],
    name: str,
    position: list[float],
    gimmick_type: str,
    action_mode: int,
    speed: float,
    move_amount: float = 0.0,
    yaw: float = 0.0,
) -> dict[str, Any]:
    entry = make_gimmick(template, name, position, [1.0, 1.0, 1.0], [0.0, yaw, 0.0])
    model_by_type = {
        "MovingFloor": "Stages/star_garden_moving_platform",
        "RotatingFloor": "Stages/star_garden_rotating_platform",
        "SeesawFloor": "Stages/star_garden_seesaw_platform",
        "SinkingFloor": "Stages/star_garden_sinking_platform",
        "Trampoline": "Stages/star_garden_trampoline_platform",
        "OneWayFloor": "Stages/star_garden_oneway_platform",
        "ChainCollapseFloor": "Stages/star_garden_collapse_platform",
    }
    if gimmick_type == "MovingFloor" and action_mode == 4:
        model_by_type["MovingFloor"] = "Stages/star_garden_lift_platform"
    collider_by_type = {
        "MovingFloor": [6.4, 0.60, 4.8],
        "RotatingFloor": [6.2, 0.68, 6.2],
        "SeesawFloor": [7.2, 0.62, 3.6],
        "SinkingFloor": [5.8, 0.74, 5.0],
        "Trampoline": [5.8, 0.52, 5.8],
        "OneWayFloor": [5.2, 0.48, 4.4],
        "ChainCollapseFloor": [5.8, 0.58, 5.0],
    }
    # 専用モデルの歩行面とコライダー上面を一致させる。
    collider_center_y_by_type = {
        "MovingFloor": -0.09,
        "RotatingFloor": -0.34,
        "SeesawFloor": 0.22,
        "SinkingFloor": -0.22,
        "Trampoline": 0.16,
        "OneWayFloor": -0.18,
        "ChainCollapseFloor": -0.18,
    }
    entry.update({
        "gimmickType": gimmick_type,
        "modelName": model_by_type.get(gimmick_type, "Stages/star_garden_gimmick_platform"),
        "castShadow": True,
        "color": [1.0, 1.0, 1.0, 1.0],
        "emissive": 1.0,
        "metallic": 0.08,
        "roughness": 0.55,
        "collisionAttribute": 4,
        "collisionMask": 255,
        "collider": {
            "center": [0.0, collider_center_y_by_type.get(gimmick_type, 0.0), 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": collider_by_type.get(gimmick_type, [5.8, 0.56, 5.0]),
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": gimmick_type,
        "actionMode": action_mode,
        "speed": speed,
        "moveAmount": move_amount,
        "startActive": True,
    })
    return entry


def make_ghost_moving_floor(
    template: dict[str, Any],
    name: str,
    position: list[float],
    path_name: str,
    yaw: float = 0.0,
) -> dict[str, Any]:
    """Ghost Recorderの編集可能Pathだけで動く導入用移動床を生成する。"""
    entry = make_mechanical_floor(template, name, position, "MovingFloor", 0, 0.0, 0.0, yaw)
    entry.setdefault("recorder", {}).update({
        "recordPathName": path_name,
        "isRecordLoop": True,
        "isRecordRelative": True,
    })
    entry.update({
        "color": [0.30, 0.88, 1.0, 1.0],
        "emissive": 1.18,
        "metallic": 0.22,
        "roughness": 0.36,
    })
    return entry


def make_timed_floor_switch(
    template: dict[str, Any],
    name: str,
    position: list[float],
    target_id: int,
) -> dict[str, Any]:
    """数字や文字を使わず、着地で同じIDの出現床群を起動するスイッチを生成する。"""
    entry = make_gimmick(template, name, position, [1.0, 1.0, 1.0])
    entry.update({
        "gimmickType": "TimedSwitch",
        "modelName": "Stages/star_garden_toggle_switch",
        "targetID": target_id,
        "collisionAttribute": 4,
        "collisionMask": 255,
        "color": [0.22, 0.86, 1.0, 1.0],
        "emissive": 1.22,
        "metallic": 0.18,
        "roughness": 0.38,
        "collider": {
            "center": [0.0, -0.11, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [2.8, 0.45, 2.8],
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": "TimedSwitch",
        "interval": 0.8,
        "switchMode": 0,
        "startActive": False,
        "returnOnOff": True,
    })
    return entry


def make_appearing_floor(
    template: dict[str, Any],
    name: str,
    position: list[float],
    event_id: int,
    duration: float,
    yaw: float,
) -> dict[str, Any]:
    """スイッチ入力中だけ現れ、専用の予告格子と上面が一致する攻略床を生成する。"""
    entry = make_gimmick(template, name, position, [1.0, 1.0, 1.0], [0.0, yaw, 0.0])
    entry.update({
        "gimmickType": "AppearingFloor",
        "modelName": "Stages/star_garden_gimmick_platform",
        "myEventID": event_id,
        "targetID": -1,
        "castShadow": True,
        "color": [0.34, 0.94, 1.0, 1.0],
        "emissive": 1.16,
        "metallic": 0.16,
        "roughness": 0.40,
        "collisionAttribute": 4,
        "collisionMask": 255,
        "collider": {
            "center": [0.0, -0.08, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [5.8, 0.50, 5.0],
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": "AppearingFloor",
        "interval": duration,
        "startActive": False,
        "returnOnOff": True,
    })
    return entry


def make_switch_floor(
    template: dict[str, Any],
    name: str,
    position: list[float],
    event_id: int,
    yaw: float,
) -> dict[str, Any]:
    """スイッチON中だけ実体化する、発光輪郭付きの攻略床を生成する。"""
    entry = make_gimmick(template, name, position, [1.0, 1.0, 1.0], [0.0, yaw, 0.0])
    entry.update({
        "gimmickType": "EventReceiver",
        "modelName": "Stages/star_garden_gimmick_platform",
        "myEventID": event_id,
        "collisionAttribute": 4,
        "collisionMask": 255,
        "color": [0.36, 0.92, 1.0, 0.96],
        "emissive": 1.16,
        "metallic": 0.14,
        "roughness": 0.42,
        "collider": {
            "center": [0.0, -0.28, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [5.8, 0.55, 5.0],
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": "EventReceiver",
        "actionMode": 4,
        "startActive": False,
        "returnOnOff": True,
        "moveSpeed": 10.0,
    })
    return entry


def make_blink_floor(
    template: dict[str, Any],
    name: str,
    position: list[float],
    color_type: int,
    yaw: float,
) -> dict[str, Any]:
    """プレイヤーがジャンプするたび、赤と青が交互に実体化する床を生成する。"""
    entry = make_gimmick(template, name, position, [1.0, 1.0, 1.0], [0.0, yaw, 0.0])
    entry.update({
        "gimmickType": "BlinkBlock",
        "modelName": "Stages/star_garden_blink_platform",
        "castShadow": True,
        "emissive": 1.10,
        "metallic": 0.12,
        "roughness": 0.44,
        "collisionAttribute": 4,
        "collisionMask": 255,
        "collider": {
            "center": [0.0, -0.28, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [5.8, 0.55, 5.0],
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": "BlinkBlock",
        "colorType": color_type,
    })
    return entry


def make_checkpoint(
    template: dict[str, Any],
    name: str,
    position: list[float],
) -> dict[str, Any]:
    """接触地点をリスポーン座標として保存する、視認可能な中間地点を生成する。"""
    entry = clone_entry(template, name)
    entry.update({
        "type": "Model",
        "gimmickType": "",
        "enemyType": "",
        "itemType": "",
        "eventID": 4,
        "myEventID": -1,
        "targetID": -1,
        "modelName": "Stages/star_garden_checkpoint",
        "isStatic": False,
        "collisionAttribute": 16,
        "collisionMask": 1,
        "color": [0.30, 0.95, 1.0, 1.0],
        "emissive": 1.35,
        "metallic": 0.28,
        "roughness": 0.34,
        "castShadow": True,
        "collider": {
            "center": [0.0, 1.45, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [1.35, 1.45, 1.35],
            "type": 3,
        },
    })
    entry.pop("param", None)
    entry.pop("eventType", None)
    entry.pop("components", None)
    entry.pop("lod", None)
    entry["meshEffect1"] = ""
    entry["meshEffect2"] = ""
    entry["gpuParticleName"] = ""
    entry["particleName"] = ""
    set_transform(entry, position, [1.0, 1.0, 1.0], [0.0, 0.0, 0.0])
    return entry


def make_giant_rush_gate(
    template: dict[str, Any],
    name: str,
    position: list[float],
    yaw: float,
) -> dict[str, Any]:
    """大型スライムの装甲突進だけで壊せる能力ゲートを生成する。"""
    entry = make_gimmick(template, name, position, [1.0, 1.0, 1.0], [0.0, yaw, 0.0])
    entry.update({
        "gimmickType": "BreakableBlock",
        "modelName": "Stages/star_garden_giant_gate",
        "color": [0.92, 0.42, 0.78, 1.0],
        "emissive": 1.10,
        "metallic": 0.10,
        "roughness": 0.60,
        "collisionAttribute": 4,
        "collisionMask": 255,
        "collider": {
            "center": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [7.2, 4.2, 1.5],
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": "BreakableBlock",
        "actionMode": 6,
    })
    return entry


def build_loop_path_frames(
    axis: int,
    minimum: float,
    maximum: float,
    duration: float,
    phase: float = 0.0,
) -> list[dict[str, Any]]:
    """Ghost Recorderで再編集できる60fpsの滑らかな往復Pathを生成する。"""
    frame_count = max(121, int(round(duration * 60.0)))
    center = (minimum + maximum) * 0.5
    amplitude = (maximum - minimum) * 0.5
    frames: list[dict[str, Any]] = []
    for index in range(frame_count):
        angle = math.tau * index / frame_count + phase
        offset = center + math.sin(angle) * amplitude
        position = [0.0, 0.0, 0.0]
        position[axis] = offset
        frames.append({
            "pos": position,
            "rot": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0],
            "eventID": 0,
        })
    return frames


def write_stage1_v4_ghost_paths() -> None:
    """導入区画の三つの移動床PathとAssetメタ情報を同時に保存する。"""
    path_specs = {
        "stage1_v4_moving_intro_a": build_loop_path_frames(0, -9.0, 9.0, 4.2, -math.pi * 0.5),
        "stage1_v4_moving_intro_b": build_loop_path_frames(2, -8.5, 8.5, 4.8, 0.0),
        "stage1_v4_moving_intro_c": build_loop_path_frames(1, 0.0, 4.0, 4.0, -math.pi * 0.5),
    }
    for name, frames in path_specs.items():
        path = ANIMATION_ROOT / f"{name}.json"
        write_json(path, {
            "anchorName": "",
            "frames": frames,
            "genParams": {
                "startPos": frames[0]["pos"],
                "startRot": [0.0, 0.0, 0.0],
                "startScale": [1.0, 1.0, 1.0],
                "startEventID": 0,
                "startWaitTime": 0.0,
                "endPos": frames[-1]["pos"],
                "endRot": [0.0, 0.0, 0.0],
                "endScale": [1.0, 1.0, 1.0],
                "endEventID": 0,
                "endWaitTime": 0.0,
                "anchorOffsetPos": [0.0, 0.0, 0.0],
                "anchorOffsetRot": [0.0, 0.0, 0.0],
                "startDurationToNext": 1.0,
                "startEasingToNext": 3,
                "waypoints": [],
                "useSpline": True,
                "generateRelative": True,
            },
        })
        write_json(path.with_suffix(path.suffix + ".meta"), {
            "assetType": "JSON",
            "guid": uuid.uuid5(GUID_NAMESPACE, f"animation/{name}").hex,
            "importSettings": {},
            "importer": "JsonImporter",
            "source": f"Resources/json/animation/{name}.json",
            "version": 1,
        })


def make_hazard_ride_floor(
    template: dict[str, Any],
    name: str,
    position: list[float],
    yaw: float,
    travel_distance: float,
    first_event_id: int,
    hazard_count: int,
) -> dict[str, Any]:
    """乗ると発進し、道中の妨害を順に起動して終点で落下する輸送床を生成する。"""
    entry = make_gimmick(template, name, position, [1.0, 1.0, 1.0], [0.0, yaw, 0.0])
    entry.update({
        "gimmickType": "HazardRideFloor",
        "modelName": "Stages/star_garden_ride_platform",
        "castShadow": True,
        "color": [0.36, 0.84, 1.0, 1.0],
        "emissive": 1.12,
        "metallic": 0.18,
        "roughness": 0.42,
        "collisionAttribute": 4,
        "collisionMask": 255,
        "targetID": first_event_id,
        "collider": {
            "center": [0.0, -0.06, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [7.5, 0.9, 5.5],
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": "HazardRideFloor",
        "actionMode": 0,
        "speed": 7.2,
        "moveAmount": travel_distance,
        "shakeDuration": 0.62,
        "interval": 0.82,
        "fallDuration": 2.0,
        "gravity": 38.0,
        "maxCount": hazard_count,
        "startActive": True,
        "returnOnOff": True,
    })
    return entry


def make_falling_spike(
    template: dict[str, Any],
    name: str,
    start_position: list[float],
    event_id: int,
    drop_distance: float,
) -> dict[str, Any]:
    """輸送床の進行に連動し、上空から着地点へ落下する棘を生成する。"""
    entry = make_gimmick(
        template,
        name,
        start_position,
        [1.35, 2.4, 1.35],
        [0.0, 0.0, math.pi],
    )
    entry.update({
        "gimmickType": "FallingSpike",
        "modelName": "Effects/prism_crystal_spike",
        "myEventID": event_id,
        "targetID": -1,
        "castShadow": True,
        "color": [0.42, 0.90, 1.0, 1.0],
        "emissive": 1.25,
        "metallic": 0.18,
        "roughness": 0.26,
        "collisionAttribute": 0,
        "collisionMask": 0,
        "collider": {
            # 棘モデルのローカルY=0～2.75とZ軸180度回転に合わせ、
            # 上空の出現座標ではなく見えている棘全体へ被弾判定を重ねる。
            "center": [0.0, 1.375, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [1.1, 1.375, 1.1],
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": "FallingSpike",
        "shakeDuration": 0.52,
        "moveAmount": drop_distance,
        "gravity": 62.0,
        "speed": 8.0,
        "jumpPower": 9.0,
        "moveSpeed": 7.0,
        "startActive": False,
        "returnOnOff": True,
    })
    return entry


def make_water(
    template: dict[str, Any],
    name: str,
    position: list[float],
    scale: list[float],
) -> dict[str, Any]:
    """地形より下へ広がる、当たり判定を持たない海面を生成する。"""
    entry = make_visual_model(template, name, "Stages/block", position, scale)
    entry.update({
        "materialType": 8,
        "blendMode": 1,
        "castShadow": False,
        "enableLighting": True,
        "isStatic": True,
        "color": [0.18, 0.62, 0.92, 0.92],
        "emissive": 0.92,
        "waterParam": {
            "billboardScale": 0.5,
            "effectIntensity": 1.05,
            "effectScale": 0.78,
            "effectScaleX": 7.0,
            "effectScaleY": 0.34,
            "effectScaleZ": 1.12,
            "effectSoftness": 0.48,
            "effectType": 0.0,
            "flowSpeedX": 0.025,
            "flowSpeedY": 0.014,
            "waveFrequency": 3.8,
            "waveHeight": 0.34,
            "waveSpeed": 0.9,
        },
    })
    return entry


def make_enemy(
    template: dict[str, Any],
    name: str,
    position: list[float],
    yaw: float = 0.0,
    scale: float = 1.0,
    target_id: int = -1,
) -> dict[str, Any]:
    entry = clone_entry(template, name)
    enemy_type = template.get("enemyType", "")
    entry["type"] = "Enemy"
    entry["enemyType"] = enemy_type
    entry.setdefault("param", {})["enemyType"] = enemy_type
    entry["isStatic"] = False
    entry["targetID"] = target_id
    set_transform(entry, position, [scale, scale, scale], [0.0, yaw, 0.0])
    return entry


def configure_star(entry: dict[str, Any], index: int, position: list[float]) -> None:
    entry["type"] = "Model"
    entry["gimmickType"] = ""
    entry.pop("param", None)
    entry["eventID"] = 7
    entry["targetID"] = index
    entry["modelName"] = "Gimmicks/star"
    entry["color"] = [1.0, 0.74, 0.08, 1.0]
    entry["emissive"] = 1.65
    entry["castShadow"] = False
    entry["collider"] = {
        "center": [0.0, 0.0, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [22.0, 22.0, 22.0],
        "type": 1,
    }
    set_transform(
        entry,
        position,
        [0.05, 0.05, 0.05],
        [0.0, 0.0, 0.0],
    )


def make_breakable_block(template: dict[str, Any], name: str, position: list[float]) -> dict[str, Any]:
    entry = make_gimmick(template, name, position, [2.55, 2.55, 2.55])
    entry.update({
        "gimmickType": "BreakableBlock",
        "modelName": "Stages/bomb_break_block",
        "castShadow": True,
        "color": [0.78, 0.64, 0.71, 1.0],
        "emissive": 1.02,
    })
    entry["param"]["gimmickType"] = "BreakableBlock"
    return entry


def make_breakable_glass(template: dict[str, Any], name: str, position: list[float]) -> dict[str, Any]:
    """ボムで割れ、向こう側の報酬を屈折越しに見せるガラス板を生成する。"""
    entry = make_gimmick(template, name, position, [5.4, 4.2, 1.0])
    entry.update({
        "gimmickType": "BreakableBlock",
        "modelName": "Stages/star_garden_glass_panel",
        "materialType": 10,
        "blendMode": 1,
        "castShadow": False,
        "enableLighting": True,
        "color": [0.34, 0.88, 1.0, 0.20],
        "emissive": 1.08,
        "metallic": 0.05,
        "roughness": 0.12,
        "enableEnvMap": True,
        "envIntensity": 1.0,
        "collisionAttribute": 4,
        "collisionMask": 255,
        "collider": {
            "center": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [0.5, 0.5, 0.12],
            "type": 3,
        },
        "waterParam": {
            "billboardScale": 0.5,
            "effectIntensity": 0.72,
            "effectScale": 0.48,
            "effectScaleX": 1.0,
            "effectScaleY": 1.0,
            "effectScaleZ": 1.0,
            "effectSoftness": 0.58,
            "effectType": 0.0,
            "flowSpeedX": 0.0,
            "flowSpeedY": 0.0,
            "waveFrequency": 2.0,
            "waveHeight": 0.06,
            "waveSpeed": 0.0,
        },
    })
    entry["param"]["gimmickType"] = "BreakableBlock"
    return entry


def make_prism_seal(
    template: dict[str, Any],
    name: str,
    position: list[float],
    scale: float,
) -> dict[str, Any]:
    entry = make_gimmick(template, name, position, [scale, scale, scale])
    entry.update({
        "gimmickType": "EventReceiver",
        "modelName": "Effects/prism_crystal_spike",
        "myEventID": PRISM_DEFEAT_EVENT_ID,
        "materialType": 27,
        "color": [0.48, 0.78, 1.0, 1.0],
        "emissive": 1.45,
        "metallic": 0.72,
        "roughness": 0.14,
        "enableEnvMap": True,
        "envIntensity": 1.2,
        "castShadow": True,
        "collider": {
            "center": [0.0, 1.35, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [0.65, 1.35, 0.65],
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": "EventReceiver",
        "actionMode": 5,
        "startActive": False,
        "returnOnOff": False,
    })
    return entry


def make_arena_encounter(
    template: dict[str, Any],
    name: str,
    position: list[float],
) -> dict[str, Any]:
    """進入、四辺封鎖、中ボス出現、撃破解除を一つの状態として管理する。"""
    entry = make_gimmick(template, name, position, [1.0, 1.0, 1.0])
    entry.update({
        "gimmickType": "ArenaEncounter",
        "modelName": "",
        "castShadow": False,
        "isVisible": True,
        "collisionAttribute": 16,
        "collisionMask": 1,
        "collider": {
            "center": [-18.0, 2.4, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [3.0, 3.2, 22.5],
            "type": 3,
        },
    })
    entry["param"].update({
        "gimmickType": "ArenaEncounter",
        "maxCount": PRISM_BARRIER_COUNT,
        "shakeDuration": 0.72,
        "startActive": True,
        "returnOnOff": False,
    })
    set_gameplay_link(entry, PRISM_DEFEAT_EVENT_ID, PRISM_BOSS_EVENT_ID)
    return entry


def make_prism_barrier(
    template: dict[str, Any],
    name: str,
    position: list[float],
    scale: list[float],
    yaw: float,
    event_id: int,
) -> dict[str, Any]:
    """闘技場の外周を塞ぐ、下から展開する当たり判定付きプリズム障壁を生成する。"""
    entry = make_gimmick(template, name, position, scale, [0.0, yaw, 0.0])
    entry.update({
        "gimmickType": "PrismBarrier",
        "modelName": "Gimmicks/portal_surface",
        "materialType": 22,
        "blendMode": 1,
        "castShadow": False,
        "enableLighting": True,
        "enableEnvMap": False,
        "color": [0.30, 0.86, 1.0, 0.84],
        "emissive": 2.4,
        "metallic": 0.0,
        "roughness": 0.22,
        "collisionAttribute": 4,
        "collisionMask": 255,
        "collider": {
            "center": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [1.0, 1.0, 0.45],
            "type": 3,
        },
        "waterParam": {
            "billboardScale": 1.0,
            "effectIntensity": 1.78,
            "effectScale": 1.0,
            "effectScaleX": 1.0,
            "effectScaleY": 1.0,
            "effectScaleZ": 0.08,
            "effectSoftness": 0.48,
            "effectType": 3.0,
            "flowSpeedX": 0.0,
            "flowSpeedY": 0.0,
            "uvOffsetX": 0.0,
            "uvOffsetY": 1.0,
            "waveFrequency": 18.0,
            "waveHeight": 0.72,
            "waveSpeed": 1.25,
        },
    })
    entry["param"].update({
        "gimmickType": "PrismBarrier",
        "moveSpeed": 0.42,
        "interval": 2.4,
        "startActive": False,
        "returnOnOff": True,
    })
    set_gameplay_link(entry, event_id, -1)
    return entry


def make_stage_entry_assembly(
    template: dict[str, Any],
    gate: dict[str, Any],
) -> list[dict[str, Any]]:
    """開始演出が参照する名前を保ったまま入口外装を生成する。"""
    gate_position = [float(value) for value in gate["position"]]
    gate_yaw = float(gate.get("rotation", [0.0, 0.0, 0.0])[1])
    side = [math.cos(gate_yaw), 0.0, -math.sin(gate_yaw)]

    pad = make_visual_model(
        template,
        "Stage1_EntryDecor_Pad",
        "Stages/stage_select_gate_pad",
        [gate_position[0], gate_position[1] - 2.27, gate_position[2]],
    )
    pad.update({"castShadow": False, "color": [0.22, 0.24, 0.30, 1.0], "emissive": 0.18})
    frame = make_visual_model(
        template,
        "Stage1_EntryDecor_Frame",
        "Stages/gate",
        [gate_position[0], gate_position[1] + 0.82, gate_position[2]],
        rotation=[0.0, gate_yaw - math.pi * 0.5, math.pi * 0.5],
    )
    frame.update({"color": [1.0, 1.0, 1.0, 1.0], "emissive": 1.0, "roughness": 0.62})

    assembly = [pad, frame]
    for side_index, side_sign in enumerate((-1.0, 1.0), start=1):
        offset = [component * 4.16 * side_sign for component in side]
        brazier_position = [
            gate_position[0] + offset[0],
            gate_position[1] - 2.28,
            gate_position[2] + offset[2],
        ]
        brazier = make_visual_model(
            template,
            f"Stage1_EntryDecor_Brazier_{side_index}",
            "Gimmicks/brazier",
            brazier_position,
            [1.55, 1.55, 1.55],
            [0.0, gate_yaw, 0.0],
        )
        brazier.update({
            "collisionAttribute": 4,
            "collisionMask": 4294967295,
            "collider": {
                "center": [0.0, 0.52, 0.0],
                "rotation": [0.0, 0.0, 0.0],
                "size": [0.72, 0.55, 0.72],
                "type": 2,
            },
            "enableEnvMap": True,
            "envIntensity": 0.18,
            "roughness": 0.72,
        })
        flame = make_visual_model(
            template,
            f"Stage1_EntryDecor_Flame_{side_index}",
            "Effects/flame",
            [brazier_position[0], gate_position[1] - 0.03, brazier_position[2]],
        )
        flame.update({
            "type": "Effect",
            "isStatic": False,
            "materialType": 11,
            "blendMode": 1,
            "castShadow": False,
            "color": [1.0, 0.20, 0.02, 0.84],
            "emissive": 2.15,
            "enableLighting": False,
            "waterParam": {
                "billboardScale": 0.74,
                "effectIntensity": 0.88,
                "effectScale": 0.96,
                "effectScaleX": 0.90,
                "effectScaleY": 1.15,
                "effectScaleZ": 0.82,
                "effectSoftness": 0.36,
                "effectType": 4.0,
                "flowSpeedX": 0.03 * side_sign,
                "flowSpeedY": 0.48,
                "waveFrequency": 2.2,
                "waveHeight": 0.5,
                "waveSpeed": 1.55,
            },
        })
        assembly.extend((brazier, flame))
    return assembly


def add_model_colliders(objects: list[dict[str, Any]], template: dict[str, Any]) -> None:
    # 見た目用の不規則な輪郭を一枚の矩形で覆わず、実際に歩く面だけを分割する。
    floor_specs = [
        ("Stage1_Collision_EntryWest", [-329.0, -0.72, -51.0], [31.0, 0.82, 25.0], 0.0),
        ("Stage1_Collision_EntryEast", [-294.0, -0.72, -43.0], [14.0, 0.82, 14.0], 0.0),
        ("Stage1_Collision_CliffLowerA", [-242.0, -0.72, -17.0], [20.0, 0.82, 16.0], 0.0),
        ("Stage1_Collision_CliffLowerB_South", [-225.0, -0.82, -15.0], [13.0, 0.82, 2.0], 0.0),
        ("Stage1_Collision_CliffLowerB_Mid", [-224.5, -0.82, -6.5], [13.5, 0.82, 6.5], 0.0),
        ("Stage1_Collision_CliffLowerB_Inner", [-227.5, -0.82, 5.0], [16.5, 0.82, 5.0], 0.0),
        ("Stage1_Collision_CliffLowerB_North", [-231.0, -0.82, 14.5], [15.0, 0.82, 4.5], 0.0),
        # 洞窟床と崖道の2mの空白を、見える石の敷居と同寸法の判定で接続する。
        ("Stage1_Collision_CaveMouth", [-238.0, -0.72, -41.0], [11.0, 0.82, 8.0], 0.0),
        ("Stage1_Collision_CaveRoom", [-238.0, -0.72, -56.0], [16.5, 0.82, 9.0], 0.0),
        ("Stage1_Collision_CaveStarPedestalBase", [-238.0, 0.56, -56.0], [3.6, 0.56, 2.8], 0.0),
        ("Stage1_Collision_CaveStarPedestalCap", [-238.0, 1.23, -56.0], [2.8, 0.11, 2.1], 0.0),
        ("Stage1_Collision_CliffUpper", [-202.0, 7.25, 25.5], [27.0, 0.85, 14.5], 0.0),
        ("Stage1_Collision_OrchardMain", [-112.0, 7.25, 62.0], [40.0, 0.85, 30.0], 0.0),
        ("Stage1_Collision_OrchardNorth", [-116.0, 15.25, 94.0], [27.0, 0.85, 7.5], 0.0),
        ("Stage1_Collision_OrchardSecretArrival", [-124.0, 16.28, 100.0], [12.0, 0.28, 6.0], 0.0),
        ("Stage1_Collision_OrchardSecretReturnBridge", [-104.5, 14.25, 96.0], [7.5, 2.25, 4.5], 0.0),
        ("Stage1_Collision_OrchardSecretReturnHigh", [-92.0, 12.0, 93.5], [5.0, 4.0, 5.5], 0.0),
        ("Stage1_Collision_OrchardSecretReturnMiddle", [-83.0, 10.15, 88.0], [5.0, 2.15, 5.0], 0.0),
        ("Stage1_Collision_OrchardSecretReturnLow", [-75.0, 8.22, 80.0], [4.5, 0.22, 5.5], 0.0),
        ("Stage1_Collision_WaterworksWest", [-28.0, 7.25, 101.0], [27.0, 0.85, 30.0], 0.0),
        ("Stage1_Collision_WaterworksEast_South", [22.5, 7.15, 77.0], [17.5, 0.85, 5.0], 0.0),
        ("Stage1_Collision_WaterworksEast_Core", [30.0, 7.15, 106.0], [25.0, 0.85, 24.0], 0.0),
        # 西岸と東岸の間に残っていた6mの透明な割れ目を、石床として明示して塞ぐ。
        ("Stage1_Collision_WaterworksCenterLink", [2.0, 7.15, 91.0], [4.0, 0.85, 9.0], 0.0),
        ("Stage1_Collision_WaterworksUpper", [11.0, 17.25, 128.0], [26.0, 0.85, 12.5], 0.0),
        # 円形広場と不規則なゴール地形は、外接矩形ではなく内側の矩形群で近似する。
        ("Stage1_Collision_PrismArenaCenter", [170.0, 11.2, 42.0], [34.0, 0.9, 14.0], 0.0),
        ("Stage1_Collision_PrismArenaNorth", [170.0, 11.2, 63.0], [23.0, 0.9, 7.0], 0.0),
        ("Stage1_Collision_PrismArenaSouth", [170.0, 11.2, 21.0], [23.0, 0.9, 7.0], 0.0),
        ("Stage1_Collision_ChainRelayBase", [102.25, 17.1, 83.0], [8.2, 0.9, 8.2], 0.0),
        ("Stage1_Collision_ChainRelayPad", [102.0, 18.24, 83.0], [6.8, 0.24, 6.0], 0.0),
        ("Stage1_Collision_PrismArenaEastThreshold", [205.75, 11.1, 42.0], [2.25, 0.9, 8.0], 0.0),
        ("Stage1_Collision_PrismRewardRoom", [221.5, 11.1, 42.0], [14.0, 0.9, 8.8], 0.0),
        ("Stage1_Collision_PrismRewardPedestalBase", [220.5, 12.42, 42.0], [5.0, 0.42, 5.0], 0.0),
        ("Stage1_Collision_PrismRewardPedestalCap", [220.5, 12.92, 42.0], [3.6, 0.08, 3.6], 0.0),
        # ボス後の城壁は二つの中庭に分け、中央階段で3m上がる。
        ("Stage1_Collision_EastRampartWest_South", [242.0, 11.1, 8.5], [26.0, 0.9, 6.5], 0.0),
        ("Stage1_Collision_EastRampartWest_Core", [246.375, 11.1, 34.0], [33.375, 0.9, 19.0], 0.0),
        ("Stage1_Collision_EastRampartWest_North", [243.75, 11.1, 56.5], [27.75, 0.9, 3.5], 0.0),
        ("Stage1_Collision_EastRampartWest_Cap", [241.875, 11.1, 62.0], [24.625, 0.9, 2.0], 0.0),
        ("Stage1_Collision_EastRampartEast", [323.0, 14.2, 39.0], [33.5, 0.9, 24.0], 0.0),
        ("Stage1_Collision_EastRampartEastNorth", [320.0, 14.2, 61.5], [23.0, 0.9, 7.5], 0.0),
        ("Stage1_Collision_RavineBoardingBase", [361.0, 14.1, 24.0], [11.5, 0.9, 9.0], 0.0),
        ("Stage1_Collision_RavineBoardingPad", [363.0, 15.24, 23.5], [6.0, 0.24, 5.0], 0.0),
        ("Stage1_Collision_RideDepartureBase", [423.5, 15.1, -21.0], [15.0, 0.9, 14.0], 0.0),
        ("Stage1_Collision_RideDeparturePad", [420.0, 16.26, -22.0], [10.0, 0.26, 8.8], 0.0),
        ("Stage1_Collision_GoalKeepCenter", [500.0, 15.2, -65.0], [45.0, 0.9, 20.0], 0.0),
        ("Stage1_Collision_GoalKeepNorth", [502.0, 15.2, -40.0], [34.0, 0.9, 5.0], 0.0),
        ("Stage1_Collision_GoalKeepSouth", [498.0, 15.2, -92.0], [38.0, 0.9, 7.0], 0.0),
        ("Stage1_Collision_GoalKeepEntry", [454.0, 15.2, -66.0], [7.0, 0.9, 10.0], 0.0),
        ("Stage1_Collision_RideLandingBase", [458.0, 16.32, -58.0], [8.0, 0.32, 7.0], 0.0),
        ("Stage1_Collision_RideLandingPad", [458.0, 16.78, -58.0], [3.5, 0.1, 3.5], 0.0),
        ("Stage1_Collision_GoalDais", [501.0, 18.05, -65.0], [18.0, 0.85, 14.0], 0.0),
    ]
    for name, position, half_size, yaw in floor_specs:
        objects.append(make_invisible_box(template, name, position, half_size, yaw))

    def add_bridge_collider(label: str, start: tuple[float, float], end: tuple[float, float], surface_y: float) -> None:
        dx = end[0] - start[0]
        dz = end[1] - start[1]
        yaw = yaw_from_xz_direction(dx, dz)
        objects.append(make_invisible_box(
            template,
            f"Stage1_Collision_{label}",
            [(start[0] + end[0]) * 0.5, surface_y - 0.82, (start[1] + end[1]) * 0.5],
            [math.hypot(dx, dz) * 0.5, 0.82, 5.0],
            yaw,
        ))
        # 橋の両端に見える固定着地点を置き、地形の縁との小さな隙間をなくす。
        for suffix, point in (("Start", start), ("End", end)):
            objects.append(make_invisible_box(
                template,
                f"Stage1_Collision_{label}{suffix}Abutment",
                [point[0], surface_y - 0.82, point[1]],
                [3.2, 0.82, 5.2],
                yaw,
            ))

    for label, start, end, surface_y in BRIDGE_SPECS:
        add_bridge_collider(label, start, end, surface_y)

    # 城壁中央の見た目階段と同じ三段で当たり判定を作る。
    for index in range(3):
        height = float(index + 1)
        objects.append(make_invisible_box(
            template,
            f"Stage1_Collision_EastRampartStep_{index + 1:02d}",
            [281.0 + 4.6 * index, 12.0 + height * 0.5 - 0.05, 20.0],
            [2.5, height * 0.5 + 0.05, 4.5],
        ))

    # 崖道の階段は見た目と同じ8段で当たり判定を作る。
    for index in range(8):
        height = float(index + 1)
        objects.append(make_invisible_box(
            template,
            f"Stage1_Collision_CliffStep_{index + 1:02d}",
            [-221.0, height * 0.5 - 0.05, 0.0 + 4.7 * index],
            [3.5, height * 0.5 + 0.05, 2.5],
        ))

    wall_specs = [
        ("CaveBack", (-255.0, -65.0), (-221.0, -65.0), 0.0, 8.5, 2.8),
        ("CaveLeft", (-255.0, -65.0), (-255.0, -42.0), 0.0, 8.5, 2.8),
        ("CaveRight", (-221.0, -65.0), (-221.0, -42.0), 0.0, 8.5, 2.8),
        ("WaterworksWest", (-55.0, 86.0), (-55.0, 125.0), 8.0, 4.8, 2.2),
        ("WaterworksEast", (55.0, 86.0), (55.0, 118.0), 8.0, 4.8, 2.2),
        ("GoalNorth", (453.0, -98.0), (530.0, -102.0), 16.0, 6.0, 2.4),
        ("GoalEast", (540.0, -96.0), (549.0, -82.0), 16.0, 6.0, 2.4),
        ("GoalSouth", (548.0, -69.0), (547.0, -37.0), 16.0, 6.0, 2.4),
        ("GoalWestA", (451.0, -92.0), (449.0, -73.0), 16.0, 6.0, 2.4),
        ("GoalWestB", (449.0, -59.0), (447.0, -55.0), 16.0, 6.0, 2.4),
    ]
    for name, start, end, base_y, height, thickness in wall_specs:
        objects.append(make_wall_box(template, f"Stage1_WallCollision_{name}", start, end, base_y, height, thickness))

    # 円形戦闘広場は東西の門だけを開ける。
    segments = 20
    for index in range(segments):
        angle0 = math.tau * index / segments
        angle1 = math.tau * (index + 1) / segments
        mid_angle = (angle0 + angle1) * 0.5
        if abs(math.sin(mid_angle)) < 0.30:
            continue
        start = (170.0 + 37.5 * math.cos(angle0), 42.0 + 32.0 * math.sin(angle0))
        end = (170.0 + 37.5 * math.cos(angle1), 42.0 + 32.0 * math.sin(angle1))
        objects.append(make_wall_box(
            template,
            f"Stage1_WallCollision_Arena_{index + 1:02d}",
            start,
            end,
            12.0,
            7.5,
            2.8,
        ))


def visible_support_name(collider_name: str) -> str:
    """重要コライダーと一対一になる可視足場名を返す。"""
    return collider_name.replace("Stage1_Collision_", "Stage1_Visible_", 1)


def critical_visible_support_colliders() -> tuple[str, ...]:
    """見た目の欠落を許可しない固定足場コライダーを列挙する。"""
    bridge_abutments = tuple(
        f"Stage1_Collision_{label}{suffix}Abutment"
        for label, _start, _end, _surface_y in BRIDGE_SPECS
        for suffix in ("Start", "End")
    )
    return CRITICAL_VISIBLE_SUPPORT_COLLIDERS + bridge_abutments


def add_visible_support_models(objects: list[dict[str, Any]], template: dict[str, Any]) -> None:
    """攻略上重要な透明判定へ、同じ外形の石床モデルを重ねる。"""
    by_name = {entry.get("name", ""): entry for entry in objects}
    for collider_name in critical_visible_support_colliders():
        collider = by_name.get(collider_name)
        if collider is None:
            raise ValueError(f"可視足場の元になるコライダーがありません: {collider_name}")

        collider_data = collider.get("collider", {})
        half_size = [float(value) for value in collider_data.get("size", [0.0, 0.0, 0.0])]
        position = [float(value) for value in collider.get("position", [0.0, 0.0, 0.0])]
        rotation = [float(value) for value in collider.get("rotation", [0.0, 0.0, 0.0])]
        if min(half_size) <= 0.0:
            raise ValueError(f"可視足場のコライダー寸法が不正です: {collider_name}")

        objects.append(make_visual_model(
            template,
            visible_support_name(collider_name),
            "Stages/star_garden_support_platform",
            [position[0], position[1] + half_size[1], position[2]],
            half_size.copy(),
            rotation,
        ))


def revise_player(player_data: dict[str, Any]) -> None:
    players = [entry for entry in player_data.get("objects", []) if entry.get("type") == "Player"]
    if len(players) != 1:
        raise RuntimeError("Stage 1のPlayerが一意ではありません。")
    set_transform(players[0], PLAYER_START.copy(), [1.0, 1.0, 1.0], [0.0, math.pi * 0.5, 0.0])


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


def stage_v2_floor_specs() -> tuple[tuple[str, list[float], list[float]], ...]:
    """新コースの見えている固定床と一致するコライダー定義を返す。"""
    return (
        ("StartCourtyard", [-320.0, -0.8, -50.0], [40.0, 0.8, 22.0]),
        ("MeadowLane", [-245.0, -0.8, -50.0], [35.0, 0.8, 13.0]),
        ("StairApproach", [-192.5, -0.8, -50.0], [17.5, 0.8, 15.0]),
        ("MainStep01", [-168.0, 1.2, -50.0], [7.0, 0.8, 15.0]),
        ("MainStep02", [-154.0, 3.2, -50.0], [7.0, 0.8, 15.0]),
        ("MainStep03", [-140.0, 5.2, -50.0], [7.0, 0.8, 15.0]),
        ("UpperGarden", [-92.0, 7.2, -50.0], [41.0, 0.8, 17.0]),
        ("ArenaWestConnector", [-44.0, 7.2, -50.0], [7.0, 0.8, 12.0]),
        ("ArenaWestThreshold", [-35.5, 7.2, -50.0], [2.5, 0.8, 6.0]),
        ("ArenaCenter", [0.0, 7.2, -50.0], [34.0, 0.8, 17.0]),
        ("ArenaNorth", [0.0, 7.2, -29.0], [22.0, 0.8, 4.0]),
        ("ArenaSouth", [0.0, 7.2, -71.0], [22.0, 0.8, 4.0]),
        ("ArenaEastInnerThreshold", [35.5, 7.2, -50.0], [2.5, 0.8, 6.0]),
        ("ArenaEastThreshold", [40.0, 7.2, -50.0], [3.0, 0.8, 10.0]),
        ("PostBossDeck", [48.0, 7.2, -50.0], [5.0, 0.8, 13.0]),
        ("LandingGarden", [122.0, 7.2, -50.0], [17.0, 0.8, 17.0]),
        ("FinalStep01", [145.0, 9.2, -50.0], [6.0, 0.8, 17.0]),
        ("FinalStep02", [157.0, 11.2, -50.0], [6.0, 0.8, 17.0]),
        ("GoalPlaza", [198.0, 11.2, -50.0], [35.0, 0.8, 22.0]),
        ("GoalDais", [215.0, 12.65, -50.0], [14.0, 0.65, 10.0]),
        ("BombGrottoCorridor", [-240.0, -0.8, -75.0], [7.0, 0.8, 12.0]),
        ("BombGrottoRoom", [-240.0, -0.8, -100.0], [18.0, 0.8, 13.0]),
        ("BombGrottoPedestal", [-240.0, 0.65, -102.0], [4.2, 0.65, 3.5]),
        ("SecretBalcony", [-60.0, 13.2, -18.0], [12.0, 0.8, 8.0]),
        ("SecretReturnHigh", [-50.0, 10.2, -31.0], [6.0, 0.8, 5.0]),
        ("SecretReturnLow", [-50.0, 8.2, -41.0], [6.0, 0.8, 5.0]),
        ("SecretPedestal", [-60.0, 14.65, -18.0], [4.0, 0.65, 3.2]),
        ("FinalSecretStep01", [118.0, 9.7, -70.0], [6.0, 0.8, 5.0]),
        ("FinalSecretStep02", [130.0, 11.7, -78.0], [6.0, 0.8, 5.0]),
        ("FinalSecretBalcony", [145.0, 13.2, -82.0], [12.0, 0.8, 8.0]),
        ("FinalSecretReturn", [158.0, 12.2, -74.0], [6.0, 0.8, 8.0]),
        ("FinalSecretPedestal", [145.0, 14.65, -82.0], [4.0, 0.65, 3.2]),
    )


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


def stage_v3_floor_specs() -> tuple[tuple[str, list[float], list[float], float], ...]:
    """V3コースの固定床と、見た目に合わせたY回転を返す。"""
    meadow_yaw = yaw_from_xz_direction(40.0, 10.0)
    stair_yaw = yaw_from_xz_direction(33.0, 14.0)
    south_bend_yaw = yaw_from_xz_direction(68.0, -24.0)
    landing_yaw = yaw_from_xz_direction(26.0, 4.0)
    arena_entry_yaw = yaw_from_xz_direction(12.0, 5.0)
    arena_exit_yaw = yaw_from_xz_direction(8.0, -1.0)
    final_yaw = yaw_from_xz_direction(26.0, 8.0)
    final_step_yaw = yaw_from_xz_direction(21.0, 8.0)
    goal_step_yaw = yaw_from_xz_direction(10.0, 5.0)
    return (
        ("StartCourtyard", [-330.0, -0.8, -50.0], [30.0, 0.8, 22.0], 0.0),
        ("MeadowWest", [-275.0, -0.8, -50.0], [25.0, 0.8, 14.0], 0.0),
        ("MeadowEast", [-235.0, -0.8, -40.0], [22.0, 0.8, 14.0], meadow_yaw),
        ("StairApproach", [-207.0, -0.8, -32.0], [8.0, 0.8, 12.0], meadow_yaw),
        ("MainStep01", [-194.0, 1.2, -28.0], [5.0, 0.8, 12.0], stair_yaw),
        ("MainStep02", [-184.0, 3.2, -23.0], [5.0, 0.8, 12.0], stair_yaw),
        ("MainStep03", [-174.0, 5.2, -18.0], [5.0, 0.8, 12.0], stair_yaw),
        ("UpperGarden", [-130.0, 5.2, -14.0], [39.0, 0.8, 20.0], 0.0),
        ("UpperBend", [-79.0, 5.2, -29.0], [15.0, 0.8, 12.0], south_bend_yaw),
        ("TrampolineBoarding", [-62.0, 5.2, -38.0], [8.0, 0.8, 8.0], south_bend_yaw),
        ("WaterworksLanding", [4.0, 14.2, -52.0], [24.0, 0.8, 16.0], 0.0),
        ("MovingFloorBoarding", [30.0, 14.2, -48.0], [3.0, 0.8, 8.0], landing_yaw),
        ("ArenaWestConnector", [68.0, 14.2, -37.0], [7.0, 0.8, 10.0], arena_entry_yaw),
        ("ArenaCenter", [100.0, 14.2, -32.0], [32.0, 0.8, 16.0], 0.0),
        ("ArenaNorth", [100.0, 14.2, -12.0], [21.0, 0.8, 4.0], 0.0),
        ("ArenaSouth", [100.0, 14.2, -52.0], [21.0, 0.8, 4.0], 0.0),
        ("ArenaEastThreshold", [138.0, 14.2, -29.0], [4.0, 0.8, 10.0], arena_exit_yaw),
        ("PostBossDeck", [146.0, 14.2, -30.0], [5.0, 0.8, 10.0], arena_exit_yaw),
        ("CollapseLanding", [214.0, 12.2, -52.0], [11.0, 0.8, 16.0], 0.0),
        ("FinalGarden", [240.0, 12.2, -44.0], [18.0, 0.8, 17.0], final_yaw),
        ("FinalStep01", [261.0, 14.2, -36.0], [5.0, 0.8, 15.0], final_step_yaw),
        ("FinalStep02", [271.0, 16.2, -31.0], [5.0, 0.8, 15.0], goal_step_yaw),
        ("GoalPlaza", [292.0, 16.2, -28.0], [16.0, 0.8, 20.0], 0.0),
        ("GoalDais", [295.0, 17.65, -28.0], [10.0, 0.65, 10.0], 0.0),
        ("BombGrottoCorridor", [-270.0, -0.8, -71.0], [7.0, 0.8, 8.0], 0.0),
        ("BombGrottoRoom", [-270.0, -0.8, -91.0], [17.0, 0.8, 12.0], 0.0),
        ("BombGrottoPedestal", [-270.0, 0.65, -93.0], [4.2, 0.65, 3.5], 0.0),
        ("ClockworkNorthLanding", [60.0, 14.2, -14.0], [7.0, 0.8, 7.0], 0.0),
        ("ClockworkStarBalcony", [58.0, 22.2, 10.0], [10.0, 0.8, 8.0], 0.0),
        ("ClockworkReturnDrop", [72.0, 14.2, -2.0], [7.0, 0.8, 10.0], 0.0),
        ("ClockworkPedestal", [58.0, 23.65, 10.0], [4.0, 0.65, 3.2], 0.0),
        ("HighRouteEntry", [214.0, 14.2, -69.0], [7.0, 0.8, 5.0], 0.0),
        ("HighRouteBalcony", [270.0, 19.2, -99.0], [9.0, 0.8, 7.0], 0.0),
        ("HighRouteDropLanding", [270.0, 12.2, -84.0], [9.0, 0.8, 8.0], 0.0),
        ("HighRouteReturnLow", [257.0, 12.2, -66.0], [8.0, 0.8, 12.0], -0.48),
        ("HighRoutePedestal", [270.0, 20.65, -99.0], [4.0, 0.65, 3.2], 0.0),
    )


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


def stage_v4_appearing_route_specs() -> tuple[
    tuple[str, int, tuple[tuple[tuple[float, float, float], float], ...]], ...
]:
    """終盤の出現床を、導入・本道・スター往路・前方復帰の四群に分けて返す。"""
    return (
        ("Approach", FINAL_APPROACH_EVENT_ID, (
            ((632.0, 12.28, -10.0), 10.0),
            ((649.0, 13.08, -1.0), 10.0),
            ((663.0, 13.78, 10.0), 10.0),
        )),
        ("Main", FINAL_CHOICE_EVENT_ID, (
            ((700.0, 15.28, 10.0), 13.0),
            ((716.0, 16.78, 1.0), 13.0),
            ((733.0, 15.98, 13.0), 13.0),
            ((750.0, 17.58, 4.0), 13.0),
            ((762.0, 16.58, 17.0), 13.0),
        )),
        ("StarOut", FINAL_CHOICE_EVENT_ID, (
            ((695.0, 17.08, 34.0), 18.0),
            ((712.0, 20.08, 44.0), 18.0),
            ((706.0, 23.08, 60.0), 18.0),
            ((726.0, 26.08, 70.0), 18.0),
            ((742.0, 28.58, 82.0), 18.0),
            # 高所の往路へ戻れない高さまで落下し、スターと帰路スイッチを同じ床で受ける。
            ((754.0, 12.58, 93.0), 28.0),
        )),
        ("StarReturn", FINAL_RETURN_EVENT_ID, (
            ((772.0, 13.58, 82.0), 14.0),
            ((786.0, 14.78, 66.0), 14.0),
            ((799.0, 15.98, 49.0), 14.0),
        )),
    )


def stage_v4_final_switch_specs() -> tuple[tuple[str, tuple[float, float, float], int], ...]:
    """見た目に番号を持たない終盤スイッチの位置と起動先を返す。"""
    return (
        ("Approach", (606.0, 12.56, -14.0), FINAL_APPROACH_EVENT_ID),
        ("Choice", (679.0, 15.56, 18.0), FINAL_CHOICE_EVENT_ID),
        ("StarExit", (751.5, 13.56, 90.5), FINAL_RETURN_EVENT_ID),
    )


def stage_v4_floor_specs() -> tuple[tuple[str, list[float], list[float], float], ...]:
    """本道と三つの寄り道を横方向へ分岐させたV4コースの固定床を返す。"""
    arena_entry_yaw = yaw_from_xz_direction(35.0, -2.0)
    chain_yaw = yaw_from_xz_direction(20.0, 12.0)
    return (
        ("StartCourtyard", [-330.0, -0.8, -50.0], [30.0, 0.8, 22.0], 0.0),
        ("MovingIntro", [-290.0, -0.8, -50.0], [12.0, 0.8, 13.0], 0.0),
        ("GiantArena", [-178.0, 5.2, -42.0], [29.0, 0.8, 22.0], 0.0),
        ("AbilityRestGarden", [-115.0, 5.2, -42.0], [34.0, 0.8, 22.0], 0.0),
        ("StarVaultCorridor", [-178.0, 5.2, -72.0], [9.0, 0.8, 8.0], 0.0),
        ("StarVaultRoom", [-178.0, 5.2, -96.0], [16.0, 0.8, 14.0], 0.0),
        ("StarVaultPedestal", [-178.0, 6.65, -98.0], [4.2, 0.65, 3.4], 0.0),
        ("BlinkIntro", [-60.0, 5.2, -38.0], [21.0, 0.8, 16.0], 0.0),
        # 固定地形は赤青床へ張り出さない大きさに絞り、床端との間に着地用の隙間を残す。
        ("BlinkRelay", [20.0, 7.2, -11.0], [9.0, 0.8, 9.0], 0.0),
        ("BlinkJunction", [76.0, 9.2, 18.0], [6.0, 0.8, 8.0], 0.0),
        ("BlinkStarBalcony", [70.0, 11.2, 91.0], [15.5, 0.8, 7.0], 0.0),
        ("BlinkReturn", [111.0, 9.2, 66.0], [6.5, 0.8, 6.5], 0.0),
        ("BlinkMainExit", [142.0, 9.2, 0.0], [11.0, 0.8, 10.0], 0.0),
        ("BossApproachGarden", [194.0, 9.2, -6.0], [41.0, 0.8, 16.0], 0.0),
        ("PreBossCheckpoint", [245.0, 9.2, -10.0], [23.0, 0.8, 15.0], 0.0),
        ("ArenaEntry", [280.0, 9.2, -12.0], [12.0, 0.8, 13.0], arena_entry_yaw),
        ("ArenaCenter", [315.0, 9.2, -20.0], [36.0, 0.8, 30.0], 0.0),
        ("ArenaExit", [352.0, 9.2, -20.0], [7.0, 0.8, 13.0], 0.0),
        ("DashStart", [380.0, 9.2, -20.0], [20.0, 0.8, 11.0], 0.0),
        ("DashRunA", [425.0, 9.2, -20.0], [13.0, 0.8, 11.0], 0.0),
        ("DashRunB", [469.0, 9.2, -20.0], [16.0, 0.8, 11.0], 0.0),
        ("DashRunC", [520.0, 9.2, -20.0], [19.0, 0.8, 11.0], 0.0),
        ("PostDashCheckpoint", [562.0, 9.2, -20.0], [23.0, 0.8, 15.0], 0.0),
        ("AppearingStart", [605.0, 11.2, -14.0], [18.0, 0.8, 13.0], chain_yaw),
        ("AppearingRelay", [679.0, 14.2, 18.0], [11.0, 0.8, 11.0], chain_yaw),
        ("AppearingMainLanding", [785.0, 16.2, 13.0], [12.0, 0.8, 11.0], -0.20),
        ("GoalApproach", [814.0, 16.2, 27.0], [18.0, 0.8, 14.0], -0.20),
        ("GoalPlaza", [850.0, 16.2, 20.0], [20.0, 0.8, 20.0], 0.0),
        ("GoalDais", [850.0, 17.65, 20.0], [10.0, 0.65, 10.0], 0.0),
    )


def stage_v4_blink_floor_specs() -> tuple[tuple[str, tuple[float, float, float], int], ...]:
    """不規則な面配置の赤青床を返す。各安全地形には両色から入れる経路を用意する。"""
    return (
        # 導入盤面。二列を少しずらし、累計ジャンプ数がどちらの偶奇でも開始できる。
        ("FieldA_01", (-27.0, 6.60, -47.0), 1),
        ("FieldA_02", (-11.0, 7.00, -42.0), 0),
        ("FieldA_03", (4.0, 7.50, -34.0), 1),
        ("FieldA_04", (-28.0, 6.60, -29.0), 0),
        ("FieldA_05", (-13.0, 7.00, -23.0), 1),
        ("FieldA_06", (3.0, 7.50, -17.0), 0),

        # 中央盤面。直線を避け、三つの曲がった選択肢を疎に配置する。
        ("FieldB_01", (38.0, 8.50, -16.0), 1),
        ("FieldB_02", (52.0, 9.00, -8.0), 0),
        ("FieldB_03", (65.0, 9.50, 2.0), 1),
        ("FieldB_04", (37.0, 8.50, 3.0), 0),
        ("FieldB_05", (50.0, 9.00, 12.0), 1),
        ("FieldB_06", (63.0, 9.50, 22.0), 0),
        ("FieldB_07", (55.0, 9.20, 34.0), 0),
        ("FieldB_08", (36.0, 8.70, 21.0), 1),

        # 本道盤面。上下二列をずらし、出口へ向けて緩く収束させる。
        ("FieldC_01", (89.0, 9.70, 10.0), 1),
        ("FieldC_02", (105.0, 9.60, 4.0), 0),
        ("FieldC_03", (121.0, 9.40, -3.0), 1),
        ("FieldC_04", (90.0, 9.80, 28.0), 0),
        ("FieldC_05", (106.0, 9.70, 22.0), 1),
        ("FieldC_06", (122.0, 9.50, 14.0), 0),

        # スター寄り道。中央で横移動でき、外周側の赤床にスターを載せる。
        ("StarField_01", (62.0, 10.50, 40.0), 1),
        ("StarField_02", (52.0, 11.00, 59.0), 0),
        ("StarField_03", (66.0, 11.50, 78.0), 1),
        ("StarField_04", (82.0, 10.50, 39.0), 0),
        ("StarField_05", (94.0, 10.80, 56.0), 1),
        ("StarField_06", (88.0, 11.20, 75.0), 0),
        ("StarField_07", (72.0, 10.90, 59.0), 0),

        # スター回収後の復路。安全地形を挟んで二色の選択肢へ戻す。
        ("ReturnA_01", (94.0, 11.30, 91.0), 1),
        ("ReturnA_02", (108.0, 10.70, 78.0), 0),
        ("ReturnA_03", (91.0, 11.00, 73.0), 0),
        ("ReturnB_01", (112.0, 10.20, 48.0), 1),
        ("ReturnB_02", (124.0, 9.80, 31.0), 0),
        ("ReturnB_03", (123.0, 9.50, 13.0), 1),
        ("ReturnB_04", (96.0, 10.10, 47.0), 0),
        ("ReturnB_05", (108.0, 9.80, 31.0), 1),
    )


def stage_v4_blink_paths() -> tuple[tuple[str, tuple[str, ...]], ...]:
    """赤開始と青開始の双方で成立する保証経路を返す。"""
    collision = "Stage1_Collision_V4_{}".format
    floor = "Stage1_V4_BlinkFloor_{}".format
    return (
        ("導入・赤開始", (collision("BlinkIntro"), floor("FieldA_01"), floor("FieldA_02"), floor("FieldA_03"), collision("BlinkRelay"))),
        ("導入・青開始", (collision("BlinkIntro"), floor("FieldA_04"), floor("FieldA_05"), floor("FieldA_06"), collision("BlinkRelay"))),
        ("中央・赤開始", (collision("BlinkRelay"), floor("FieldB_01"), floor("FieldB_02"), floor("FieldB_03"), collision("BlinkJunction"))),
        ("中央・青開始", (collision("BlinkRelay"), floor("FieldB_04"), floor("FieldB_05"), floor("FieldB_06"), collision("BlinkJunction"))),
        ("中央・横道", (collision("BlinkRelay"), floor("FieldB_04"), floor("FieldB_08"), floor("FieldB_07"), collision("BlinkJunction"))),
        ("本道・赤開始", (collision("BlinkJunction"), floor("FieldC_01"), floor("FieldC_02"), floor("FieldC_03"), collision("BlinkMainExit"))),
        ("本道・青開始", (collision("BlinkJunction"), floor("FieldC_04"), floor("FieldC_05"), floor("FieldC_06"), collision("BlinkMainExit"))),
        ("スター往路・赤開始", (collision("BlinkJunction"), floor("StarField_01"), floor("StarField_02"), floor("StarField_03"), collision("BlinkStarBalcony"))),
        ("スター往路・青開始", (collision("BlinkJunction"), floor("StarField_04"), floor("StarField_05"), floor("StarField_06"), collision("BlinkStarBalcony"))),
        ("スター往路・横断", (collision("BlinkJunction"), floor("StarField_01"), floor("StarField_07"), floor("StarField_03"), collision("BlinkStarBalcony"))),
        ("スター復路・赤開始", (collision("BlinkStarBalcony"), floor("ReturnA_01"), floor("ReturnA_02"), collision("BlinkReturn"))),
        ("スター復路・青開始", (collision("BlinkStarBalcony"), floor("ReturnA_03"), collision("BlinkReturn"))),
        ("復帰・赤開始", (collision("BlinkReturn"), floor("ReturnB_01"), floor("ReturnB_02"), floor("ReturnB_03"), collision("BlinkMainExit"))),
        ("復帰・青開始", (collision("BlinkReturn"), floor("ReturnB_04"), floor("ReturnB_05"), floor("FieldC_06"), collision("BlinkMainExit"))),
    )


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


def validate_model_reference(model_name: str) -> bool:
    if not model_name:
        return True
    relative = Path(*model_name.split("/"))
    directory = MODEL_ROOT / relative
    if not directory.is_dir():
        return False
    candidates = [
        path for path in directory.iterdir()
        if path.is_file() and path.suffix.lower() in {".obj", ".gltf", ".glb", ".fbx"}
    ]
    for path in candidates:
        if path.suffix.lower() != ".obj":
            return path.stat().st_size > 64
        vertices: list[tuple[float, float, float]] = []
        face_count = 0
        with path.open("r", encoding="utf-8", errors="ignore") as source:
            for line in source:
                if line.startswith("v "):
                    values = line.split()
                    if len(values) >= 4:
                        vertices.append((float(values[1]), float(values[2]), float(values[3])))
                elif line.startswith("f "):
                    face_count += 1
        if len(vertices) < 3 or face_count == 0:
            continue
        spans = [max(axis) - min(axis) for axis in zip(*vertices)]
        if max(spans) > 0.001:
            return True
    return False


def load_obj_top_faces_in_engine_space(model_name: str) -> list[tuple[float, list[tuple[float, float]]]]:
    """Assimpの左手系変換後にゲーム内で見えるOBJ上面を読み取る。"""
    relative = Path(*model_name.split("/"))
    directory = MODEL_ROOT / relative
    candidates = sorted(directory.glob("*.obj"))
    if not candidates:
        return []

    vertices: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    faces: list[tuple[float, list[tuple[float, float]]]] = []
    with candidates[0].open("r", encoding="utf-8", errors="ignore") as source:
        for line in source:
            if line.startswith("v "):
                values = line.split()
                if len(values) >= 4:
                    # Model.Loader.cppのaiProcess_ConvertToLeftHandedと同じZ反転を適用する。
                    vertices.append((float(values[1]), float(values[2]), -float(values[3])))
            elif line.startswith("vn "):
                values = line.split()
                if len(values) >= 4:
                    normals.append((float(values[1]), float(values[2]), -float(values[3])))
            elif line.startswith("f "):
                tokens = line.split()[1:]
                if len(tokens) < 3:
                    continue
                try:
                    vertex_indices = [int(token.split("/")[0]) - 1 for token in tokens]
                    normal_token = tokens[0].split("/")
                    normal_index = int(normal_token[2]) - 1 if len(normal_token) >= 3 and normal_token[2] else -1
                    points = [vertices[index] for index in vertex_indices]
                except (IndexError, ValueError):
                    continue
                if normal_index < 0 or normal_index >= len(normals) or normals[normal_index][1] <= 0.5:
                    continue
                top_y = sum(point[1] for point in points) / len(points)
                if max(abs(point[1] - top_y) for point in points) > 0.02:
                    continue
                faces.append((top_y, [(point[0], point[2]) for point in points]))
    return faces


def collider_world_box(entry: dict[str, Any]) -> dict[str, Any] | None:
    """ローカルコライダーを、XZ平面のワールドOBBと上下端へ変換する。"""
    collider = entry.get("collider", {})
    if int(collider.get("type", 0)) == 0:
        return None

    position = [float(value) for value in entry.get("position", [0.0, 0.0, 0.0])]
    scale = [float(value) for value in entry.get("scale", [1.0, 1.0, 1.0])]
    rotation = [float(value) for value in entry.get("rotation", [0.0, 0.0, 0.0])]
    local_center = [float(value) for value in collider.get("center", [0.0, 0.0, 0.0])]
    local_half = [float(value) for value in collider.get("size", [0.0, 0.0, 0.0])]
    collider_rotation = [float(value) for value in collider.get("rotation", [0.0, 0.0, 0.0])]

    yaw = rotation[1] + collider_rotation[1]
    cosine = math.cos(rotation[1])
    sine = math.sin(rotation[1])
    scaled_center_x = local_center[0] * scale[0]
    scaled_center_z = local_center[2] * scale[2]
    center_x = position[0] + scaled_center_x * cosine + scaled_center_z * sine
    center_z = position[2] - scaled_center_x * sine + scaled_center_z * cosine
    center_y = position[1] + local_center[1] * scale[1]
    half_x = abs(local_half[0] * scale[0])
    half_y = abs(local_half[1] * scale[1])
    half_z = abs(local_half[2] * scale[2])

    axis_x = (math.cos(yaw), -math.sin(yaw))
    axis_z = (math.sin(yaw), math.cos(yaw))
    vertices = [
        (
            center_x + axis_x[0] * half_x * sign_x + axis_z[0] * half_z * sign_z,
            center_z + axis_x[1] * half_x * sign_x + axis_z[1] * half_z * sign_z,
        )
        for sign_x, sign_z in ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0))
    ]
    return {
        "center": (center_x, center_y, center_z),
        "half": (half_x, half_y, half_z),
        "axes": (axis_x, axis_z),
        "vertices": vertices,
        "bottom": center_y - half_y,
        "top": center_y + half_y,
    }


def point_inside_world_box(box: dict[str, Any], x: float, z: float, margin: float = 0.0) -> bool:
    center_x, _, center_z = box["center"]
    half_x, _, half_z = box["half"]
    axis_x, axis_z = box["axes"]
    dx = x - center_x
    dz = z - center_z
    local_x = dx * axis_x[0] + dz * axis_x[1]
    local_z = dx * axis_z[0] + dz * axis_z[1]
    return abs(local_x) <= half_x + margin and abs(local_z) <= half_z + margin


def point_inside_terrain_collider(entry: dict[str, Any], x: float, z: float) -> bool:
    box = collider_world_box(entry)
    return bool(box and point_inside_world_box(box, x, z))


def projected_interval(vertices: list[tuple[float, float]], axis: tuple[float, float]) -> tuple[float, float]:
    values = [vertex[0] * axis[0] + vertex[1] * axis[1] for vertex in vertices]
    return min(values), max(values)


def oriented_boxes_overlap(first: dict[str, Any], second: dict[str, Any], tolerance: float = 1.0e-6) -> bool:
    axes = (*first["axes"], *second["axes"])
    for axis in axes:
        first_min, first_max = projected_interval(first["vertices"], axis)
        second_min, second_max = projected_interval(second["vertices"], axis)
        if first_max < second_min - tolerance or second_max < first_min - tolerance:
            return False
    return True


def point_segment_distance(
    point: tuple[float, float],
    start: tuple[float, float],
    end: tuple[float, float],
) -> float:
    edge_x = end[0] - start[0]
    edge_z = end[1] - start[1]
    edge_length_squared = edge_x * edge_x + edge_z * edge_z
    if edge_length_squared <= 1.0e-12:
        return math.hypot(point[0] - start[0], point[1] - start[1])
    projection = ((point[0] - start[0]) * edge_x + (point[1] - start[1]) * edge_z) / edge_length_squared
    projection = max(0.0, min(1.0, projection))
    nearest_x = start[0] + edge_x * projection
    nearest_z = start[1] + edge_z * projection
    return math.hypot(point[0] - nearest_x, point[1] - nearest_z)


def point_inside_polygon(point: tuple[float, float], polygon: list[tuple[float, float]]) -> bool:
    """境界を含めてXZ平面上の多角形内判定を行う。"""
    if len(polygon) < 3:
        return False
    edges = list(zip(polygon, polygon[1:] + polygon[:1]))
    if any(point_segment_distance(point, start, end) <= 0.02 for start, end in edges):
        return True

    inside = False
    x, z = point
    for start, end in edges:
        if (start[1] > z) == (end[1] > z):
            continue
        crossing_x = start[0] + (z - start[1]) * (end[0] - start[0]) / (end[1] - start[1])
        if x < crossing_x:
            inside = not inside
    return inside


def validate_v3_course_mesh_alignment(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """実際のOBJ読込後の地形上面と固定床コライダーを直接照合する。"""
    errors: list[str] = []
    top_faces = load_obj_top_faces_in_engine_space("Stages/star_garden_course_v3")
    if not top_faces:
        return ["Stage 1 V3地形OBJの上面を読み取れません。"]

    for label, _position, _half_size, _yaw in stage_v3_floor_specs():
        name = f"Stage1_Collision_V3_{label}"
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
            supported = any(
                abs(face_y - surface_y) <= 0.03 and point_inside_polygon((sample_x, sample_z), polygon)
                for face_y, polygon in top_faces
            )
            if not supported:
                errors.append(
                    f"V3地形OBJと固定床判定がずれています: {name} "
                    f"sample=({sample_x:.2f}, {surface_y:.2f}, {sample_z:.2f})"
                )
                break
    return errors


def oriented_box_edge_gap(first: dict[str, Any], second: dict[str, Any]) -> float:
    """二つの床OBBが重なる場合は0、離れている場合は床端同士の最短距離を返す。"""
    if oriented_boxes_overlap(first, second):
        return 0.0
    minimum = math.inf
    for source, target in ((first, second), (second, first)):
        target_vertices = target["vertices"]
        target_edges = list(zip(target_vertices, target_vertices[1:] + target_vertices[:1]))
        for vertex in source["vertices"]:
            for edge_start, edge_end in target_edges:
                minimum = min(minimum, point_segment_distance(vertex, edge_start, edge_end))
    return minimum


def validate_floor_link(
    label: str,
    start: dict[str, Any] | None,
    end: dict[str, Any] | None,
    maximum_gap: float,
    maximum_height_delta: float,
) -> list[str]:
    errors: list[str] = []
    if start is None or end is None:
        errors.append(f"{label}の床または当たり判定が不足しています。")
        return errors
    start_box = collider_world_box(start)
    end_box = collider_world_box(end)
    if start_box is None or end_box is None:
        errors.append(f"{label}に有効な床コライダーがありません。")
        return errors

    gap = oriented_box_edge_gap(start_box, end_box)
    height_delta = abs(float(start_box["top"]) - float(end_box["top"]))
    if gap > maximum_gap + 0.01:
        errors.append(f"{label}の床端間隔が広すぎます: {gap:.2f}m (許容 {maximum_gap:.2f}m)")
    if height_delta > maximum_height_delta + 0.01:
        errors.append(
            f"{label}の歩行面高低差が大きすぎます: {height_delta:.2f}m "
            f"(許容 {maximum_height_delta:.2f}m)"
        )
    return errors


def validate_mechanical_collider_tops(objects: list[dict[str, Any]]) -> list[str]:
    """専用床モデルの見た目上面とコライダー上面が一致することを確認する。"""
    errors: list[str] = []
    model_top_by_type = {
        "MovingFloor": 0.51,
        "RotatingFloor": 0.34,
        "SeesawFloor": 0.84,
        "SinkingFloor": 0.52,
        "Trampoline": 0.68,
        "OneWayFloor": 0.30,
        "ChainCollapseFloor": 0.40,
        "HazardRideFloor": 0.84,
        "AppearingFloor": 0.42,
    }
    for entry in objects:
        gimmick_type = entry.get("gimmickType")
        if gimmick_type not in model_top_by_type:
            continue
        box = collider_world_box(entry)
        if box is None:
            errors.append(f"{entry.get('name', gimmick_type)}に床コライダーがありません。")
            continue
        position_y = float(entry.get("position", [0.0, 0.0, 0.0])[1])
        scale_y = abs(float(entry.get("scale", [1.0, 1.0, 1.0])[1]))
        expected_top = position_y + model_top_by_type[gimmick_type] * scale_y
        if abs(float(box["top"]) - expected_top) > 0.03:
            errors.append(
                f"{entry.get('name', gimmick_type)}の見た目上面とコライダー上面が一致しません: "
                f"model={expected_top:.2f}, collider={float(box['top']):.2f}"
            )
    return errors


def validate_v3_glass_vault(objects: list[dict[str, Any]]) -> list[str]:
    """スター1のガラス入口が、見た目と判定の両方で回り込み不能な開口を塞ぐことを確認する。"""
    errors: list[str] = []
    panels = {
        str(entry.get("name", "")): entry
        for entry in objects
        if str(entry.get("name", "")).startswith("Stage1_V3_GlassVault_")
    }
    expected = {
        f"Stage1_V3_GlassVault_{row}_{column}"
        for row in range(1, 3)
        for column in range(1, 4)
    }
    if set(panels) != expected:
        errors.append("スター1のガラス入口が3列×2段でそろっていません。")
        return errors

    bounds_x: list[tuple[float, float]] = []
    bounds_y: list[tuple[float, float]] = []
    for name, panel in panels.items():
        box = collider_world_box(panel)
        if (
            panel.get("modelName") != "Stages/star_garden_glass_panel"
            or panel.get("materialType") != 10
            or box is None
        ):
            errors.append(f"{name}のガラスモデル、屈折マテリアル、または当たり判定が不正です。")
            continue
        center_x, center_y, _center_z = box["center"]
        half_x, half_y, _half_z = box["half"]
        bounds_x.append((center_x - half_x, center_x + half_x))
        bounds_y.append((center_y - half_y, center_y + half_y))

    if bounds_x and (min(bound[0] for bound in bounds_x) > -278.08 or max(bound[1] for bound in bounds_x) < -261.92):
        errors.append("スター1のガラス入口に左右から通れる隙間があります。")
    if bounds_y and (min(bound[0] for bound in bounds_y) > 0.02 or max(bound[1] for bound in bounds_y) < 8.38):
        errors.append("スター1のガラス入口に上下から通れる隙間があります。")

    wall_names = (
        "Stage1_WallCollision_V3_BombGrottoBack",
        "Stage1_WallCollision_V3_BombGrottoWest",
        "Stage1_WallCollision_V3_BombGrottoEast",
    )
    for wall_name in wall_names:
        box = collider_world_box(next((entry for entry in objects if entry.get("name") == wall_name), {}))
        if box is None or float(box["top"]) < 9.98:
            errors.append(f"{wall_name}が低く、スター1へ回り込めます。")
    return errors


def validate_bomb_grotto_wall(objects: list[dict[str, Any]]) -> list[str]:
    """ボム洞窟の入口が5列×3段のブロックで隙間なく塞がれていることを確認する。"""
    errors: list[str] = []
    blocks = [entry for entry in objects if entry.get("name", "").startswith("Stage1_BombGrotto_Wall_")]
    indexed: dict[tuple[int, int], dict[str, Any]] = {}
    for entry in blocks:
        suffix = entry.get("name", "").removeprefix("Stage1_BombGrotto_Wall_")
        parts = suffix.split("_")
        if len(parts) != 2 or not all(part.isdigit() for part in parts):
            errors.append(f"ボム洞窟壁の名前形式が不正です: {entry.get('name', '')}")
            continue
        indexed[(int(parts[0]), int(parts[1]))] = entry

    expected_indices = {(row, column) for row in range(1, 4) for column in range(1, 6)}
    if len(blocks) != 15 or set(indexed) != expected_indices:
        errors.append("ボム洞窟の壊れる壁が5列×3段でそろっていません。")
        return errors

    opening_min_x = -244.375
    opening_max_x = -231.625
    opening_bottom = 0.0
    opening_top = 7.65
    row_bounds: list[tuple[float, float]] = []
    for row in range(1, 4):
        horizontal_bounds: list[tuple[float, float]] = []
        vertical_bounds: list[tuple[float, float]] = []
        for column in range(1, 6):
            box = collider_world_box(indexed[(row, column)])
            if box is None:
                errors.append(f"ボム洞窟壁 {row}-{column} にコライダーがありません。")
                continue
            minimum_x = min(vertex[0] for vertex in box["vertices"])
            maximum_x = max(vertex[0] for vertex in box["vertices"])
            minimum_z = min(vertex[1] for vertex in box["vertices"])
            maximum_z = max(vertex[1] for vertex in box["vertices"])
            if not minimum_z - 0.01 <= -42.7 <= maximum_z + 0.01:
                errors.append(f"ボム洞窟壁 {row}-{column} が入口面から外れています。")
            horizontal_bounds.append((minimum_x, maximum_x))
            vertical_bounds.append((float(box["bottom"]), float(box["top"])))
        if len(horizontal_bounds) != 5:
            continue
        horizontal_bounds.sort()
        for column, (previous, current) in enumerate(zip(horizontal_bounds, horizontal_bounds[1:]), start=1):
            gap = current[0] - previous[1]
            if gap > 0.02:
                errors.append(f"ボム洞窟壁の第{row}段 {column}～{column + 1}列に{gap:.3f}mの隙間があります。")
        if horizontal_bounds[0][0] > opening_min_x + 0.01 or horizontal_bounds[-1][1] < opening_max_x - 0.01:
            errors.append(f"ボム洞窟壁の第{row}段が入口幅を完全に覆っていません。")
        row_bounds.append((min(bound[0] for bound in vertical_bounds), max(bound[1] for bound in vertical_bounds)))

    row_bounds.sort()
    for row, (previous, current) in enumerate(zip(row_bounds, row_bounds[1:]), start=1):
        gap = current[0] - previous[1]
        if gap > 0.02:
            errors.append(f"ボム洞窟壁の第{row}段と第{row + 1}段に{gap:.3f}mの隙間があります。")
    if row_bounds and (row_bounds[0][0] > opening_bottom + 0.01 or row_bounds[-1][1] < opening_top - 0.01):
        errors.append("ボム洞窟壁が入口の上下を完全に封鎖していません。")
    return errors


def validate_star_supports(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """各スターが攻略先の専用支持台上に置かれていることを確認する。"""
    errors: list[str] = []
    support_by_star = {
        "Stage1_StarCoin_01": "Stage1_Collision_CaveStarPedestalCap",
        "Stage1_StarCoin_02": "Stage1_Collision_OrchardSecretArrival",
        "Stage1_StarCoin_03": "Stage1_Collision_PrismRewardPedestalCap",
    }
    for star_name, support_name in support_by_star.items():
        star = by_name.get(star_name)
        support = by_name.get(support_name)
        if star is None or support is None:
            errors.append(f"{star_name}または専用支持台{support_name}が不足しています。")
            continue
        support_box = collider_world_box(support)
        if support_box is None:
            errors.append(f"{support_name}に有効なコライダーがありません。")
            continue
        star_position = [float(value) for value in star.get("position", [0.0, 0.0, 0.0])]
        if not point_inside_world_box(support_box, star_position[0], star_position[2], margin=-0.05):
            errors.append(f"{star_name}が専用支持台{support_name}の上にありません。")
        height = star_position[1] - float(support_box["top"])
        if height < 1.8 or height > 3.2:
            errors.append(
                f"{star_name}と専用支持台の高さ関係が不正です: {height:.2f}m "
                "(許容 1.80～3.20m)"
            )
    return errors


def validate_visible_support_models(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """重要な固定判定と専用の可視足場が一対一で一致することを確認する。"""
    errors: list[str] = []
    expected_model = "Stages/star_garden_support_platform"
    for collider_name in critical_visible_support_colliders():
        visual_name = visible_support_name(collider_name)
        collider = by_name.get(collider_name)
        visual = by_name.get(visual_name)
        if collider is None:
            errors.append(f"攻略用の固定判定{collider_name}が不足しています。")
            continue
        if visual is None:
            errors.append(f"{collider_name}と対になる可視足場{visual_name}が不足しています。")
            continue
        if visual.get("modelName") != expected_model:
            errors.append(f"{visual_name}に専用の固定足場モデルが設定されていません。")

        collider_data = collider.get("collider", {})
        collider_position = [float(value) for value in collider.get("position", [0.0, 0.0, 0.0])]
        collider_rotation = [float(value) for value in collider.get("rotation", [0.0, 0.0, 0.0])]
        half_size = [float(value) for value in collider_data.get("size", [0.0, 0.0, 0.0])]
        visual_position = [float(value) for value in visual.get("position", [0.0, 0.0, 0.0])]
        visual_rotation = [float(value) for value in visual.get("rotation", [0.0, 0.0, 0.0])]
        visual_scale = [float(value) for value in visual.get("scale", [0.0, 0.0, 0.0])]
        expected_position = [
            collider_position[0],
            collider_position[1] + half_size[1],
            collider_position[2],
        ]

        if any(abs(actual - expected) > 0.01 for actual, expected in zip(visual_position, expected_position)):
            errors.append(f"{visual_name}の位置が{collider_name}の歩行面と一致していません。")
        if any(abs(actual - expected) > 0.01 for actual, expected in zip(visual_scale, half_size)):
            errors.append(f"{visual_name}の外形が{collider_name}の当たり判定と一致していません。")
        rotation_delta = max(
            abs(math.remainder(actual - expected, math.tau))
            for actual, expected in zip(visual_rotation, collider_rotation)
        )
        if rotation_delta > 0.001:
            errors.append(f"{visual_name}の向きが{collider_name}の当たり判定と一致していません。")
    return errors


def validate_integrated_collider_visual_contract(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """固定床が、統合地形・専用足場・橋・階段のいずれかへ必ず紐づくことを確認する。"""
    errors: list[str] = []
    accounted = set(critical_visible_support_colliders())

    for visual_name, expected_model, collider_names in INTEGRATED_COLLIDER_VISUAL_GROUPS:
        visual = by_name.get(visual_name)
        if visual is None or visual.get("modelName") != expected_model:
            errors.append(f"統合地形{visual_name}のモデル参照が不正です。")
        for collider_name in collider_names:
            accounted.add(collider_name)
            if collider_name not in by_name:
                errors.append(f"統合地形{visual_name}に必要な{collider_name}が不足しています。")

    for label, _start, _end, _surface_y in BRIDGE_SPECS:
        accounted.add(f"Stage1_Collision_{label}")

    for index in range(1, 9):
        name = f"Stage1_Collision_CliffStep_{index:02d}"
        accounted.add(name)
        if name not in by_name:
            errors.append(f"崖道の見える階段に対応する{name}が不足しています。")
    for index in range(1, 4):
        name = f"Stage1_Collision_EastRampartStep_{index:02d}"
        accounted.add(name)
        if name not in by_name:
            errors.append(f"城壁の見える階段に対応する{name}が不足しています。")

    fixed_floor_names = {
        name
        for name, entry in by_name.items()
        if entry.get("type") == "InvisibleBox" and name.startswith("Stage1_Collision_")
    }
    unaccounted = sorted(fixed_floor_names - accounted)
    if unaccounted:
        errors.append(
            "可視モデルとの対応が宣言されていない固定床があります: "
            + ", ".join(unaccounted)
        )
    return errors


def validate_main_route_strips(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """広い地形上の本道を示す石帯が、設計座標と一致することを確認する。"""
    errors: list[str] = []
    for label, start, end, surface_y, width in MAIN_ROUTE_STRIP_SPECS:
        name = f"Stage1_RouteStrip_{label}"
        strip = by_name.get(name)
        if strip is None:
            errors.append(f"本道の見える石帯{name}が不足しています。")
            continue
        if strip.get("modelName") != "Stages/star_garden_support_platform":
            errors.append(f"{name}に石床モデルが設定されていません。")
            continue

        dx = end[0] - start[0]
        dz = end[1] - start[1]
        expected_position = [
            (start[0] + end[0]) * 0.5,
            surface_y + 0.035,
            (start[1] + end[1]) * 0.5,
        ]
        expected_scale = [math.hypot(dx, dz) * 0.5, 0.055, width * 0.5]
        expected_yaw = yaw_from_xz_direction(dx, dz)
        position = [float(value) for value in strip.get("position", [0.0, 0.0, 0.0])]
        scale = [float(value) for value in strip.get("scale", [0.0, 0.0, 0.0])]
        yaw = float(strip.get("rotation", [0.0, 0.0, 0.0])[1])
        if any(abs(actual - expected) > 0.01 for actual, expected in zip(position, expected_position)):
            errors.append(f"{name}の位置が本道設計からずれています。")
        if any(abs(actual - expected) > 0.01 for actual, expected in zip(scale, expected_scale)):
            errors.append(f"{name}の幅または長さが本道設計からずれています。")
        if abs(math.remainder(yaw - expected_yaw, math.tau)) > 0.001:
            errors.append(f"{name}の向きが本道設計からずれています。")
    return errors


def validate_supported_guide_coins(objects: list[dict[str, Any]]) -> list[str]:
    """地上の案内コインが、透明な隙間や床面の下へ置かれていないことを確認する。"""
    errors: list[str] = []
    required_prefixes = (
        "Stage1_MainGuide_",
        "Stage1_BombGrotto_Guide_",
        "Stage1_BridgeGuide_",
        "Stage1_Secret_Guide_",
    )
    support_gimmicks = {
        "AppearingFloor",
        "Trampoline",
        "OneWayFloor",
        "ChainCollapseFloor",
        "SeesawFloor",
        "MovingFloor",
        "RotatingFloor",
        "SinkingFloor",
        "HazardRideFloor",
    }
    supports = [
        entry
        for entry in objects
        if (
            entry.get("type") == "InvisibleBox"
            and entry.get("name", "").startswith("Stage1_Collision_")
        )
        or entry.get("gimmickType") in support_gimmicks
    ]
    support_boxes = [
        (entry.get("name", ""), collider_world_box(entry))
        for entry in supports
    ]

    for coin in objects:
        name = coin.get("name", "")
        if coin.get("gimmickType") != "Coin" or not name.startswith(required_prefixes):
            continue
        position = [float(value) for value in coin.get("position", [0.0, 0.0, 0.0])]
        candidates = [
            (support_name, box)
            for support_name, box in support_boxes
            if box is not None and point_inside_world_box(box, position[0], position[2], margin=0.30)
        ]
        if not candidates:
            errors.append(f"案内コイン{name}の真下に歩行床がありません。")
            continue
        support_name, support_box = min(
            candidates,
            key=lambda item: abs(position[1] - float(item[1]["top"])),
        )
        height = position[1] - float(support_box["top"])
        if height < 0.30 or height > 4.20:
            errors.append(
                f"案内コイン{name}と{support_name}の高さ関係が不正です: {height:.2f}m"
            )
    return errors


def validate_bridge_alignment(by_name: dict[str, dict[str, Any]]) -> list[str]:
    """石橋の見た目・透明コライダー・両端の固定着地点を確認する。"""
    errors: list[str] = []
    for label, start, end, surface_y in BRIDGE_SPECS:
        visual_name = f"Stage1_Ruins_{label}"
        collider_name = f"Stage1_Collision_{label}"
        visual = by_name.get(visual_name)
        collider = by_name.get(collider_name)
        if visual is None or collider is None:
            errors.append(f"石橋{label}の見た目または当たり判定が不足しています。")
            continue
        if visual.get("modelName") != "Stages/star_garden_ruin_bridge":
            errors.append(f"{visual_name}に石橋専用モデルが設定されていません。")

        visual_position = [float(value) for value in visual.get("position", [0.0, 0.0, 0.0])]
        collider_position = [float(value) for value in collider.get("position", [0.0, 0.0, 0.0])]
        visual_rotation = [float(value) for value in visual.get("rotation", [0.0, 0.0, 0.0])]
        collider_rotation = [float(value) for value in collider.get("rotation", [0.0, 0.0, 0.0])]
        collider_size = [float(value) for value in collider.get("collider", {}).get("size", [0.0, 0.0, 0.0])]
        visual_scale = [float(value) for value in visual.get("scale", [1.0, 1.0, 1.0])]

        horizontal_offset = math.hypot(
            visual_position[0] - collider_position[0],
            visual_position[2] - collider_position[2],
        )
        yaw_delta = abs(math.remainder(visual_rotation[1] - collider_rotation[1], math.tau))
        visual_length = 48.0 * visual_scale[0]
        collider_length = 2.0 * collider_size[0]
        collider_top = collider_position[1] + collider_size[1]
        if horizontal_offset > 0.01 or yaw_delta > 0.001:
            errors.append(f"{label}の見た目と当たり判定の中心または向きが一致していません。")
        if abs(visual_length - collider_length) > 0.05:
            errors.append(f"{label}の見た目と当たり判定の長さが一致していません。")
        if abs(visual_position[1] - collider_top) > 0.05:
            errors.append(f"{label}の歩行面と当たり判定の高さが一致していません。")

        for suffix, endpoint in (("Start", start), ("End", end)):
            abutment_name = f"Stage1_Collision_{label}{suffix}Abutment"
            abutment = by_name.get(abutment_name)
            if abutment is None:
                errors.append(f"{label}の{suffix}側に固定着地点がありません。")
                continue
            position = [float(value) for value in abutment.get("position", [0.0, 0.0, 0.0])]
            rotation = [float(value) for value in abutment.get("rotation", [0.0, 0.0, 0.0])]
            size = [float(value) for value in abutment.get("collider", {}).get("size", [0.0, 0.0, 0.0])]
            endpoint_offset = math.hypot(position[0] - endpoint[0], position[2] - endpoint[1])
            top = position[1] + size[1]
            if endpoint_offset > 0.01:
                errors.append(f"{label}の{suffix}側固定着地点が橋端からずれています。")
            if abs(top - surface_y) > 0.01:
                errors.append(f"{label}の{suffix}側固定着地点の高さが橋面と一致していません。")
            if abs(math.remainder(rotation[1] - collider_rotation[1], math.tau)) > 0.001:
                errors.append(f"{label}の{suffix}側固定着地点の向きが橋と一致していません。")
    return errors


def validate_route_support(objects: list[dict[str, Any]]) -> list[str]:
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validate-only", action="store_true", help="現在のJSONを検証するだけで保存しない")
    args = parser.parse_args()

    stage1 = load_json(STAGE1_PATH)
    player_data = load_json(STAGE1_PLAYER_PATH)
    if not args.validate_only:
        stage2 = load_json(STAGE2_PATH)
        stage3_enemy = load_json(STAGE3_ENEMY_PATH)
        sample_objects = load_json(SAMPLE_OBJECT_PATH)
        revise_stage_v4(stage1, stage2, stage3_enemy, sample_objects)
        revise_player(player_data)
        write_stage1_v4_ghost_paths()

    errors = validate_stage_v4(stage1, player_data)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    if not args.validate_only:
        write_json(STAGE1_PATH, stage1)
        write_json(STAGE1_PLAYER_PATH, player_data)
        print(f"Stage 1 V4を再構成しました: {len(stage1['objects'])} objects")
    else:
        print(f"Stage 1 V4検証OK: {len(stage1['objects'])} objects")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
