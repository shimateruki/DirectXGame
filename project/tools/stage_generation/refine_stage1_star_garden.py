#!/usr/bin/env python3
"""Stage 1 を「安全な本道＋意味のある寄り道＋中ボス戦」へ再構成する。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
STAGE1_PATH = ROOT / "Resources/json/3Dobject/stage1_object.json"
STAGE2_PATH = ROOT / "Resources/json/3Dobject/stage2_object.json"
GUID_NAMESPACE = uuid.UUID("ed9d8843-2923-4e66-a82e-968d5fb41e26")
PRISM_DEFEAT_EVENT_ID = 4201


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def write_json(path: Path, data: dict[str, Any]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as destination:
        json.dump(data, destination, ensure_ascii=False, indent=4)
        destination.write("\n")


def object_map(data: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {entry["name"]: entry for entry in data["objects"]}


def first_gimmick(data: dict[str, Any], gimmick_type: str) -> dict[str, Any]:
    for entry in data["objects"]:
        if entry.get("gimmickType") == gimmick_type:
            return entry
    raise RuntimeError(f"{gimmick_type} のテンプレートが見つかりません。")


def stable_guid(name: str) -> str:
    return str(uuid.uuid5(GUID_NAMESPACE, name))


def clone_entry(template: dict[str, Any], name: str) -> dict[str, Any]:
    entry = copy.deepcopy(template)
    entry["name"] = name
    entry["guid"] = stable_guid(name)
    entry["parentGuid"] = ""
    entry["parentName"] = ""
    return entry


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


def set_launch_destination(
    entry: dict[str, Any],
    start: list[float],
    destination: list[float],
    speed: float,
    jump_power: float,
) -> None:
    dx = destination[0] - start[0]
    dy = destination[1] - start[1]
    dz = destination[2] - start[2]
    distance = math.sqrt(dx * dx + dy * dy + dz * dz)
    horizontal_yaw = math.atan2(dx, dz)
    pitch = -math.asin(max(-1.0, min(1.0, dy / distance)))
    set_transform(entry, start, rotation=[pitch, horizontal_yaw, 0.0])
    entry["param"]["moveAmount"] = round(distance, 4)
    entry["param"]["speed"] = speed
    entry["param"]["jumpPower"] = jump_power


def make_model(
    template: dict[str, Any],
    name: str,
    model_name: str,
    position: list[float],
    scale: list[float],
    rotation: list[float] | None = None,
) -> dict[str, Any]:
    entry = clone_entry(template, name)
    entry["type"] = "Model"
    entry["gimmickType"] = ""
    entry["enemyType"] = ""
    entry["modelName"] = model_name
    entry["isStatic"] = True
    entry["collisionAttribute"] = 4
    entry["collisionMask"] = 4294967295
    set_transform(entry, position, scale, rotation or [0.0, 0.0, 0.0])
    return entry


def make_bridge(
    template: dict[str, Any],
    name: str,
    position: list[float],
    scale: list[float],
    yaw: float,
) -> dict[str, Any]:
    entry = make_model(template, name, "Stages/star_garden_bridge", position, scale, [0.0, yaw, 0.0])
    entry["collider"] = {
        "center": [0.0, 0.0, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [3.5, 0.32, 1.45],
        "type": 3,
    }
    return entry


def make_coin(template: dict[str, Any], name: str, position: list[float]) -> dict[str, Any]:
    entry = clone_entry(template, name)
    set_transform(entry, position, [0.065, 0.065, 0.065], [0.0, 0.0, 0.0])
    return entry


def make_stage_entry_assembly(
    template: dict[str, Any], gate: dict[str, Any]
) -> list[dict[str, Any]]:
    """ステージセレクトと同じ入口ゲートの外装一式を生成する。"""
    gate_position = [float(value) for value in gate["position"]]
    gate_rotation = gate.get("rotation", [0.0, 0.0, 0.0])
    gate_yaw = float(gate_rotation[1])
    side = [math.cos(gate_yaw), 0.0, -math.sin(gate_yaw)]

    def make_visual(
        name: str,
        model_name: str,
        position: list[float],
        scale: list[float],
        rotation: list[float],
    ) -> dict[str, Any]:
        entry = clone_entry(template, name)
        entry.update({
            "type": "Model",
            "gimmickType": "",
            "enemyType": "",
            "itemType": "",
            "modelName": model_name,
            "isStatic": True,
            "collisionAttribute": 0,
            "collisionMask": 0,
            "collider": {
                "center": [0.0, 0.0, 0.0],
                "rotation": [0.0, 0.0, 0.0],
                "size": [1.0, 1.0, 1.0],
                "type": 0,
            },
        })
        entry.pop("param", None)
        set_transform(entry, position, scale, rotation)
        return entry

    pad = make_visual(
        "Stage1_EntryDecor_Pad",
        "Stages/stage_select_gate_pad",
        [gate_position[0], gate_position[1] - 2.27, gate_position[2]],
        [1.0, 1.0, 1.0],
        [0.0, 0.0, 0.0],
    )
    pad.update({
        "blendMode": 1,
        "castShadow": False,
        "color": [0.22, 0.24, 0.30, 1.0],
        "emissive": 0.18,
        "enableEnvMap": False,
        "roughness": 0.42,
    })

    frame = make_visual(
        "Stage1_EntryDecor_Frame",
        "Stages/gate",
        [gate_position[0], gate_position[1] + 0.82, gate_position[2]],
        [1.0, 1.0, 1.0],
        [0.0, gate_yaw - math.pi * 0.5, math.pi * 0.5],
    )
    frame.update({
        "blendMode": 1,
        "castShadow": True,
        "color": [1.0, 1.0, 1.0, 1.0],
        "emissive": 1.0,
        "enableEnvMap": False,
        "roughness": 0.62,
    })

    assembly = [pad, frame]
    for side_index, side_sign in enumerate((-1.0, 1.0), start=1):
        offset = [component * 4.16 * side_sign for component in side]
        brazier_position = [
            gate_position[0] + offset[0],
            gate_position[1] - 2.28,
            gate_position[2] + offset[2],
        ]
        brazier = make_visual(
            f"Stage1_EntryDecor_Brazier_{side_index}",
            "Gimmicks/brazier",
            brazier_position,
            [1.55, 1.55, 1.55],
            [0.0, gate_yaw, 0.0],
        )
        brazier.update({
            "castShadow": True,
            "collisionAttribute": 4,
            "collisionMask": 4294967295,
            "collider": {
                "center": [0.0, 0.52, 0.0],
                "rotation": [0.0, 0.0, 0.0],
                "size": [0.72, 0.55, 0.72],
                "type": 2,
            },
            "emissive": 0.10,
            "enableEnvMap": True,
            "envIntensity": 0.18,
            "roughness": 0.72,
        })
        flame = make_visual(
            f"Stage1_EntryDecor_Flame_{side_index}",
            "Effects/flame",
            [brazier_position[0], gate_position[1] - 0.03, brazier_position[2]],
            [1.0, 1.0, 1.0],
            [0.0, 0.0, 0.0],
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
            "enableEnvMap": False,
            "roughness": 0.72,
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


def make_moving_floor(
    template: dict[str, Any],
    name: str,
    position: list[float],
    action_mode: int,
    move_amount: float,
    speed: float,
    scale: list[float],
    color: list[float],
) -> dict[str, Any]:
    entry = clone_entry(template, name)
    entry["type"] = "Gimmick"
    entry["gimmickType"] = "MovingFloor"
    entry["modelName"] = "Stages/galaxy_orbit_platform"
    entry["color"] = color
    entry["emissive"] = 1.25
    entry["metallic"] = 0.22
    entry["roughness"] = 0.34
    entry["castShadow"] = True
    entry["collider"] = {
        "center": [0.0, 0.1, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [2.55, 0.52, 2.55],
        "type": 3,
    }
    entry["param"]["gimmickType"] = "MovingFloor"
    entry["param"]["actionMode"] = action_mode
    entry["param"]["moveAmount"] = move_amount
    entry["param"]["speed"] = speed
    entry["param"]["startActive"] = True
    set_transform(entry, position, scale, [0.0, 0.0, 0.0])
    return entry


def make_breakable_block(
    template: dict[str, Any], name: str, position: list[float]
) -> dict[str, Any]:
    entry = clone_entry(template, name)
    entry["castShadow"] = True
    entry["color"] = [0.94, 0.72, 0.88, 1.0]
    entry["emissive"] = 1.15
    set_transform(entry, position, [2.8, 2.8, 2.8], [0.0, 0.0, 0.0])
    return entry


def make_prism_seal(
    template: dict[str, Any], name: str, position: list[float], scale: float
) -> dict[str, Any]:
    entry = clone_entry(template, name)
    entry["type"] = "Gimmick"
    entry["gimmickType"] = "EventReceiver"
    entry["modelName"] = "Effects/prism_crystal_spike"
    entry["myEventID"] = PRISM_DEFEAT_EVENT_ID
    entry["materialType"] = 27
    entry["color"] = [0.48, 0.78, 1.0, 1.0]
    entry["emissive"] = 1.45
    entry["metallic"] = 0.72
    entry["roughness"] = 0.14
    entry["enableEnvMap"] = True
    entry["envIntensity"] = 1.2
    entry["castShadow"] = True
    entry["collider"] = {
        "center": [0.0, 1.35, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [0.65, 1.35, 0.65],
        "type": 3,
    }
    entry["param"]["gimmickType"] = "EventReceiver"
    entry["param"]["actionMode"] = 5
    entry["param"]["startActive"] = False
    entry["param"]["returnOnOff"] = False
    set_transform(entry, position, [scale, scale, scale], [0.0, 0.0, 0.0])
    return entry


def make_prism_enemy(template: dict[str, Any]) -> dict[str, Any]:
    entry = clone_entry(template, "Stage1_Revision_Prism_RuinArena")
    entry["type"] = "Enemy"
    entry["enemyType"] = "PrismSlime"
    entry["param"] = {"enemyType": "PrismSlime"}
    entry["materialType"] = 27
    entry["color"] = [0.62, 0.94, 1.0, 1.0]
    entry["emissive"] = 1.08
    entry["enableEnvMap"] = True
    entry["envIntensity"] = 1.0
    entry["collisionAttribute"] = 2
    entry["collisionMask"] = 77
    entry["collider"] = {
        "center": [0.0, 0.36, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "size": [0.78, 0.78, 0.78],
        "type": 1,
    }
    entry["targetID"] = PRISM_DEFEAT_EVENT_ID
    entry["isStatic"] = False
    set_transform(entry, [160.0, 20.82, 43.0], [4.2, 4.2, 4.2], [0.0, -1.15, 0.0])
    return entry


def configure_star(entry: dict[str, Any], index: int, position: list[float]) -> None:
    entry["eventID"] = 7
    entry["targetID"] = index
    entry["modelName"] = "Gimmicks/star"
    entry["color"] = [1.0, 0.74, 0.08, 1.0]
    entry["emissive"] = 1.65
    entry["castShadow"] = False
    set_transform(entry, position, [0.05, 0.05, 0.05], [0.0, 0.0, 0.0])


def revise_stage(stage1: dict[str, Any], stage2: dict[str, Any]) -> None:
    objects = stage1["objects"]
    by_name = object_map(stage1)

    required_names = [
        "Stage1_Planetoid_CometMeadowB",
        "Stage1_Planetoid_SecretMoon",
        "Stage1_Bomber_MeadowB",
        "Stage1_Launch_WindToRuins",
        "Stage1_RuinTimedSwitch",
        "Stage1_RelayRotator_A",
        "Stage1_RelayPhase_B",
        "Stage1_RelayRotator_C",
        "Stage1_StarCoin_01",
        "Stage1_StarCoin_02",
        "Stage1_StarCoin_03",
    ]
    missing = [name for name in required_names if name not in by_name]
    if missing:
        raise RuntimeError("Stage 1 の必須オブジェクトが不足しています: " + ", ".join(missing))

    entrance_gate = by_name.get("Stage1_EntranceGate")
    if not entrance_gate:
        raise RuntimeError("Stage 1 の入口ゲートが見つかりません。")
    entrance_gate["type"] = "Gimmick"
    entrance_gate["gimmickType"] = "StageGate"
    entrance_gate["modelName"] = "Gimmicks/crown_stage_gate"
    entrance_param = entrance_gate.setdefault("param", {})
    entrance_param.update({
        "gimmickType": "StageGate",
        "actionMode": 1,
        "targetScene": "SELECT",
        "startActive": True,
        "returnOnOff": True,
    })

    model_template = by_name["Stage1_Planetoid_SecretMoon"]
    coin_template = by_name.get("Stage1_SecretMoonCoin_01") or by_name["Stage1_CometBridgeCoin_01"]
    enemy_template = by_name["Stage1_Bomber_MeadowB"]
    moving_template = first_gimmick(stage2, "MovingFloor")
    breakable_template = first_gimmick(stage2, "BreakableBlock")
    receiver_template = first_gimmick(stage2, "EventReceiver")

    obsolete_exact = {
        "Stage1_WindSpiralTower",
        "Stage1_WindTowerSpring",
        "Stage1_WindHook_Low",
        "Stage1_WindHook_Mid",
        "Stage1_WindHook_High",
        "Stage1_WindLedge_Low",
        "Stage1_WindLedge_Mid",
        "Stage1_WindLedge_High",
        "Stage1_WindSummit",
        "Stage1_Wind_TowerGuard",
        "Stage1_SecretMoonHook",
        "Stage1_Fire_RuinEntry",
    }
    obsolete_prefixes = (
        "Stage1_WindClimbCoin_",
        "Stage1_SecretMoonCoin_",
        "Stage1_Revision_",
        "Stage1_EntryDecor_",
    )
    stage1["objects"] = [
        entry
        for entry in objects
        if entry.get("name") not in obsolete_exact
        and not entry.get("name", "").startswith(obsolete_prefixes)
    ]

    # ボム能力を見せてから、本道から一段だけ外れた小島のスターへ誘導する。
    bomber = by_name["Stage1_Bomber_MeadowB"]
    set_transform(bomber, [-5.0, 15.82, -10.0], [1.0, 1.0, 1.0], [0.0, -1.3, 0.0])

    additions: list[dict[str, Any]] = []
    additions.extend(make_stage_entry_assembly(model_template, entrance_gate))
    additions.append(
        make_model(
            model_template,
            "Stage1_Revision_BombGardenIsland",
            "Stages/star_garden_island",
            [-14.0, 16.0, -39.0],
            [10.0, 3.2, 8.0],
        )
    )
    additions.append(
        make_bridge(
            model_template,
            "Stage1_Revision_BombGardenBridge",
            [-14.0, 15.7, -30.0],
            [1.5, 1.0, 1.6],
            math.pi * 0.5,
        )
    )
    for layer, y in enumerate((16.8, 19.6), start=1):
        for column, x in enumerate((-17.0, -14.0, -11.0), start=1):
            additions.append(
                make_breakable_block(
                    breakable_template,
                    f"Stage1_Revision_BombWall_{layer}_{column}",
                    [x, y, -31.5],
                )
            )
    for index, position in enumerate(
        ([-9.5, 17.0, -19.0], [-11.0, 17.0, -23.0], [-12.5, 17.0, -27.0]),
        start=1,
    ):
        additions.append(make_coin(coin_template, f"Stage1_Revision_BombRouteCoin_{index:02d}", position))

    # 必須だった高いフック塔を撤去し、本道は島の地表から発射スターへ直結させる。
    launch_to_ruins = by_name["Stage1_Launch_WindToRuins"]
    set_launch_destination(
        launch_to_ruins,
        [91.0, 25.65, -17.0],
        [143.0, 21.2, 36.0],
        speed=44.0,
        jump_power=18.0,
    )
    for index, position in enumerate(
        ([48.0, 26.4, -26.0], [58.0, 26.4, -23.0], [68.0, 26.4, -21.0], [78.0, 26.4, -19.0]),
        start=1,
    ):
        additions.append(make_coin(coin_template, f"Stage1_Revision_WindMainCoin_{index:02d}", position))

    secret_moon = by_name["Stage1_Planetoid_SecretMoon"]
    set_transform(secret_moon, [72.0, 31.0, -78.0], [13.0, 7.5, 11.0], [0.0, 0.0, 0.0])
    secret_beacon = by_name.get("Stage1_SecretMoonBeacon")
    if secret_beacon:
        set_transform(secret_beacon, [72.0, 31.1, -78.0], [1.0, 1.0, 1.0], [0.0, 0.0, 0.0])
    additions.append(
        make_moving_floor(
            moving_template,
            "Stage1_Revision_SecretMovingFloor_A",
            [63.0, 26.9, -60.5],
            3,
            3.6,
            0.65,
            [1.5, 0.86, 1.5],
            [0.54, 0.96, 1.0, 1.0],
        )
    )
    additions.append(
        make_moving_floor(
            moving_template,
            "Stage1_Revision_SecretMovingFloor_B",
            [68.0, 28.9, -68.5],
            3,
            2.8,
            0.82,
            [1.5, 0.86, 1.5],
            [0.72, 0.88, 1.0, 1.0],
        )
    )
    for index, position in enumerate(
        ([63.0, 28.4, -60.5], [68.0, 30.4, -68.5], [72.0, 32.7, -73.0]),
        start=1,
    ):
        additions.append(make_coin(coin_template, f"Stage1_Revision_SecretRouteCoin_{index:02d}", position))

    # クリスタルスライムを倒すまで、遺跡出口を結晶の列で封鎖する。
    additions.append(make_prism_enemy(enemy_template))
    # 島の外周まで塞ぎ、戦闘を避けて横を抜けられない結晶門にする。
    for index, offset in enumerate(range(-21, 22, 3), start=1):
        additions.append(
            make_prism_seal(
                receiver_template,
                f"Stage1_Revision_PrismSeal_{index:02d}",
                [176.0, 20.2, 42.0 + float(offset)],
                2.25 if index % 2 else 2.05,
            )
        )

    ruin_switch = by_name["Stage1_RuinTimedSwitch"]
    set_transform(ruin_switch, [182.0, 20.55, 30.5], [0.9, 0.45, 0.9], [0.0, -0.5, 0.0])

    # 中継区間は消える床を使わず、振幅の小さい移動床で必ず復帰できる構成にする。
    relay_settings = (
        ("Stage1_RelayRotator_A", [245.0, 24.96, 15.0], 2, 1.6, 0.62),
        ("Stage1_RelayPhase_B", [258.0, 26.5, 21.0], 4, 1.25, 0.74),
        ("Stage1_RelayRotator_C", [271.0, 28.2, 26.0], 3, 1.5, 0.68),
    )
    for name, position, action_mode, move_amount, speed in relay_settings:
        entry = by_name[name]
        entry["gimmickType"] = "MovingFloor"
        entry["param"]["gimmickType"] = "MovingFloor"
        entry["param"]["actionMode"] = action_mode
        entry["param"]["moveAmount"] = move_amount
        entry["param"]["speed"] = speed
        entry["param"]["startActive"] = True
        entry["modelName"] = "Stages/galaxy_orbit_platform"
        entry["collider"] = {
            "center": [0.0, 0.1, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "size": [2.55, 0.52, 2.55],
            "type": 3,
        }
        set_transform(entry, position, [1.55, 0.9, 1.55], [0.0, 0.0, 0.0])

    configure_star(by_name["Stage1_StarCoin_01"], 0, [-14.0, 18.4, -39.0])
    configure_star(by_name["Stage1_StarCoin_02"], 1, [72.0, 33.0, -78.0])
    configure_star(by_name["Stage1_StarCoin_03"], 2, [180.0, 22.8, 32.0])

    stage1["objects"].extend(additions)


def validate_stage(data: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    objects = data.get("objects", [])
    names = [entry.get("name", "") for entry in objects]
    if len(names) != len(set(names)):
        errors.append("オブジェクト名が重複しています。")
    guids = [entry.get("guid", "") for entry in objects if entry.get("guid")]
    if len(guids) != len(set(guids)):
        errors.append("GUIDが重複しています。")

    stars = [entry for entry in objects if entry.get("name", "").startswith("Stage1_StarCoin_")]
    if sorted(entry.get("targetID") for entry in stars) != [0, 1, 2]:
        errors.append("スターコインの番号が0～2で一意になっていません。")

    prism = next((entry for entry in objects if entry.get("name") == "Stage1_Revision_Prism_RuinArena"), None)
    seals = [entry for entry in objects if entry.get("name", "").startswith("Stage1_Revision_PrismSeal_")]
    if not prism or prism.get("targetID") != PRISM_DEFEAT_EVENT_ID:
        errors.append("クリスタルスライムの撃破イベントが設定されていません。")
    if len(seals) != 15 or any(entry.get("myEventID") != PRISM_DEFEAT_EVENT_ID for entry in seals):
        errors.append("クリスタル封鎖のイベント接続が不正です。")

    forbidden = (
        "Stage1_WindSpiralTower",
        "Stage1_WindHook_",
        "Stage1_WindLedge_",
        "Stage1_WindClimbCoin_",
    )
    if any(any(name.startswith(prefix) for prefix in forbidden) for name in names):
        errors.append("旧縦塔ルートが残っています。")

    moving_names = {
        "Stage1_Revision_SecretMovingFloor_A",
        "Stage1_Revision_SecretMovingFloor_B",
        "Stage1_RelayRotator_A",
        "Stage1_RelayPhase_B",
        "Stage1_RelayRotator_C",
    }
    by_name = object_map(data)
    for name in moving_names:
        if by_name.get(name, {}).get("gimmickType") != "MovingFloor":
            errors.append(f"{name} が移動床になっていません。")

    entrance_gate = by_name.get("Stage1_EntranceGate")
    entrance_param = entrance_gate.get("param", {}) if entrance_gate else {}
    if (
        not entrance_gate
        or entrance_gate.get("gimmickType") != "StageGate"
        or entrance_gate.get("modelName") != "Gimmicks/crown_stage_gate"
        or entrance_param.get("actionMode") != 1
        or entrance_param.get("targetScene") != "SELECT"
    ):
        errors.append("Stage 1 の入口ゲート設定が不正です。")

    expected_entry_models = {
        "Stage1_EntryDecor_Pad": "Stages/stage_select_gate_pad",
        "Stage1_EntryDecor_Frame": "Stages/gate",
        "Stage1_EntryDecor_Brazier_1": "Gimmicks/brazier",
        "Stage1_EntryDecor_Brazier_2": "Gimmicks/brazier",
        "Stage1_EntryDecor_Flame_1": "Effects/flame",
        "Stage1_EntryDecor_Flame_2": "Effects/flame",
    }
    for name, model_name in expected_entry_models.items():
        if by_name.get(name, {}).get("modelName") != model_name:
            errors.append(f"{name} の入口ゲート外装が不足または不正です。")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validate-only", action="store_true", help="現在のJSONを検証するだけで保存しない")
    args = parser.parse_args()

    stage1 = load_json(STAGE1_PATH)
    if not args.validate_only:
        stage2 = load_json(STAGE2_PATH)
        revise_stage(stage1, stage2)

    errors = validate_stage(stage1)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    if not args.validate_only:
        write_json(STAGE1_PATH, stage1)
        print(f"Stage 1 を再構成しました: {len(stage1['objects'])} objects")
    else:
        print(f"Stage 1 検証OK: {len(stage1['objects'])} objects")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
