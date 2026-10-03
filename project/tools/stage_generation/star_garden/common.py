"""共通設定とオブジェクト構築。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[3]
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
