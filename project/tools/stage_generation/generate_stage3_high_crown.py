"""ステージ3「High Crown」の闘技場シーンデータを生成します。

短い導入路、封鎖ゲート、偽王スライム、撃破後に出現する王冠までを
エディタで個別調整できるObjectとして出力します。攻撃は敵クラスと攻撃プロファイルが担当します。
"""

from __future__ import annotations

import json
import math
import uuid
from copy import deepcopy
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
OBJECT_DIR = PROJECT_ROOT / "Resources" / "json" / "3Dobject"
STAGE_SELECT = PROJECT_ROOT / "Resources" / "json" / "stage_select" / "stages.json"
LIGHT_PATH = PROJECT_ROOT / "Resources" / "json" / "light" / "stage3_high_crown.json"
SKYBOX_PATH = "Resources/skybox/stage3_high_crown.dds"
GUID_NAMESPACE = uuid.UUID("7cc41bbd-8186-48d4-945a-9c631409e9fd")
STAGE_NORMAL_MAP = "Resources/3DModel/Stages/high_crown/high_crown_surface_normal.dds"
STAGE_ORM_MAP = "Resources/3DModel/Stages/high_crown/high_crown_surface_orm.dds"
GATE_NORMAL_MAP = "Resources/3DModel/Stages/high_crown/high_crown_gate_surface_normal.dds"
GATE_ORM_MAP = "Resources/3DModel/Stages/high_crown/high_crown_gate_surface_orm.dds"
BOSS_NORMAL_MAP = "Resources/3DModel/Characters/false_king_slime/false_king_surface_normal.dds"
BOSS_ORM_MAP = "Resources/3DModel/Characters/false_king_slime/false_king_surface_orm.dds"


def stable_guid(name: str) -> str:
    return str(uuid.uuid5(GUID_NAMESPACE, f"stage3-high-crown:{name}"))


def quaternion_from_yaw(yaw: float) -> list[float]:
    return [0.0, math.sin(yaw * 0.5), 0.0, math.cos(yaw * 0.5)]


def default_param() -> dict:
    return {
        "actionMode": 0,
        "attackPower": 1.0,
        "colorType": 0,
        "detectionRange": 20.0,
        "enemyType": "",
        "fallDuration": 2.0,
        "gimmickType": "",
        "gravity": 50.0,
        "healAmount": 1.0,
        "hp": 100.0,
        "interval": 0.8,
        "itemType": "",
        "jumpPower": 24.0,
        "maxCount": 5,
        "maxFallSpeed": 60.0,
        "maxHp": 100.0,
        "moveAmount": 10.0,
        "moveSpeed": 6.0,
        "returnOnOff": True,
        "shakeDuration": 1.0,
        "speed": 12.0,
        "startActive": False,
        "switchMode": 0,
        "targetScene": "SELECT",
    }


def object_data(
    name: str,
    object_type: str,
    model: str,
    position: list[float],
    scale: list[float] | None = None,
    rotation: list[float] | None = None,
    collider_type: int = 0,
    collider_center: list[float] | None = None,
    collider_size: list[float] | None = None,
    collision_attribute: int = 0,
    collision_mask: int = 0,
    save_category: str = "Object",
    gimmick_type: str = "",
    enemy_type: str = "",
    event_type: int = 0,
    my_event_id: int = -1,
    target_id: int = -1,
    param: dict | None = None,
    material_type: int = 0,
    color: list[float] | None = None,
    emissive: float = 1.0,
    metallic: float = 0.0,
    roughness: float = 0.52,
    env_map: bool = False,
    env_intensity: float = 1.0,
    cast_shadow: bool = True,
    is_static: bool = False,
    mesh_effect1: str = "",
    mesh_effect2: str = "",
    normal_map: str = "",
    orm_map: str = "",
    texture_tiling: list[float] | None = None,
    auto_texture_tiling: bool = False,
) -> dict:
    scale = scale or [1.0, 1.0, 1.0]
    rotation = rotation or [0.0, 0.0, 0.0]
    collider_center = collider_center or [0.0, 0.0, 0.0]
    collider_size = collider_size or [1.0, 1.0, 1.0]
    color = color or [1.0, 1.0, 1.0, 1.0]
    texture_tiling = texture_tiling or [1.0, 1.0]
    data = {
        "animation": {"animName": "", "animatorController": "", "isAnimLoop": True},
        "autoTextureTiling": auto_texture_tiling,
        "blendMode": 1,
        "castShadow": cast_shadow,
        "collider": {
            "center": collider_center,
            "rotation": [0.0, 0.0, 0.0],
            "size": collider_size,
            "type": collider_type,
        },
        "collisionAttribute": collision_attribute,
        "collisionMask": collision_mask,
        "color": color,
        "emissive": emissive,
        "enableEnvMap": env_map,
        "enableLighting": True,
        "enableNormalMap": bool(normal_map),
        "enemyType": enemy_type,
        "envIntensity": env_intensity,
        "eventID": event_type,
        "gimmickType": gimmick_type,
        "gpuParticleName": "",
        "guid": stable_guid(name),
        "isStatic": is_static,
        "itemType": "",
        "layer": "Default",
        "localFog": {
            "color": [0.22, 0.14, 0.36, 1.0],
            "density": 0.0,
            "edgeFade": 0.0,
            "noiseScale": 0.0,
            "noiseSpeed": 0.0,
            "scatteringG": 0.0,
            "scatteringIntensity": 0.0,
        },
        "materialType": material_type,
        "meshDrawIndex": -1,
        "meshEffect1": mesh_effect1,
        "meshEffect2": mesh_effect2,
        "metallic": metallic,
        "modelName": model,
        "myEventID": my_event_id,
        "name": name,
        "normalMapPath": normal_map,
        "ormMapPath": orm_map,
        "parentGuid": "",
        "parentName": "",
        "particleName": "",
        "position": position,
        "quaternion": quaternion_from_yaw(rotation[1]),
        "recorder": {"isRecordLoop": False, "isRecordRelative": False, "recordPathName": ""},
        "rotation": rotation,
        "roughness": roughness,
        "saveCategory": save_category,
        "scale": scale,
        "tag": "",
        "targetID": target_id,
        "texturePath": "",
        "textureTiling": texture_tiling,
        "type": object_type,
    }
    if param is not None:
        data["param"] = param
    if my_event_id > 0 or target_id > 0:
        data["components"] = {
            "GameplayLink": {
                "_editorPresent": True,
                "eventId": my_event_id,
                "targetId": target_id,
                "version": 1,
            }
        }
    return data


def build_static_models() -> list[dict]:
    objects: list[dict] = []
    stage_pbr_single = {
        "normal_map": STAGE_NORMAL_MAP,
        "orm_map": STAGE_ORM_MAP,
        "texture_tiling": [1.0, 1.0],
    }
    stage_pbr_tiled = {
        "normal_map": STAGE_NORMAL_MAP,
        "orm_map": STAGE_ORM_MAP,
        "texture_tiling": [2.0, 2.0],
    }
    objects.append(object_data(
        "Stage3_HighCrown_ArenaVisual", "Model", "Stages/high_crown/high_crown_arena.obj",
        [0.0, 0.0, 0.0], collider_type=0, material_type=0,
        metallic=0.04, roughness=0.72, env_map=True, env_intensity=0.20, is_static=True,
        **stage_pbr_single,
    ))
    objects.append(object_data(
        "Stage3_HighCrown_Throne", "Model", "Stages/high_crown/high_crown_throne.obj",
        [0.0, 0.72, -22.0], collider_type=3, collider_center=[0.0, 0.5, 0.0],
        collider_size=[5.2, 0.65, 2.5], collision_attribute=4, collision_mask=0xFFFFFFFF,
        material_type=0, metallic=0.06, roughness=0.68, env_map=True, env_intensity=0.24, is_static=True,
        **stage_pbr_tiled,
    ))

    # 楕円形と判定を一致させるため、見えない床をZ方向の短い帯へ分割します。
    floor_strips = [
        (0.0, 6.0, 39.20),
        (-9.0, 3.0, 37.70), (9.0, 3.0, 37.70),
        (-15.0, 3.0, 35.40), (15.0, 3.0, 35.40),
        (-21.0, 3.0, 31.20), (21.0, 3.0, 31.20),
        (-27.0, 3.0, 24.00), (27.0, 3.0, 24.00),
        (-31.5, 1.5, 14.00), (31.5, 1.5, 14.00),
        (-33.2, 0.8, 7.50), (33.2, 0.8, 7.50),
    ]
    for index, (z, half_z, half_x) in enumerate(floor_strips):
        objects.append(object_data(
            f"[Collision] Stage3_HighCrown_ArenaFloor_{index:02d}", "InvisibleBox", "",
            [0.0, 0.0, z], collider_type=3, collider_center=[0.0, 0.10, 0.0],
            collider_size=[half_x, 0.70, half_z], collision_attribute=4,
            collision_mask=0xFFFFFFFF, cast_shadow=False, is_static=True,
        ))

    # 入口だけを空けた外周壁。各Objectをエディタから動かせる粒度で保持します。
    radius_x, radius_z = 39.2, 33.2
    wall_count = 44
    for index in range(wall_count):
        angle = math.tau * index / wall_count
        # +Z側中央を門柱込み約17m幅のゲート開口として残します。
        delta_south = math.atan2(math.sin(angle - math.pi * 0.5), math.cos(angle - math.pi * 0.5))
        if abs(delta_south) < 0.23:
            continue
        x = math.cos(angle) * radius_x
        z = math.sin(angle) * radius_z
        tangent_x = -radius_x * math.sin(angle)
        tangent_z = radius_z * math.cos(angle)
        yaw = math.atan2(-tangent_z, tangent_x)
        objects.append(object_data(
            f"Stage3_HighCrown_Wall_{index:02d}", "Model", "Stages/high_crown/high_crown_wall.obj",
            [round(x, 5), 0.80, round(z, 5)], scale=[1.0, 1.0, 1.0],
            rotation=[0.0, yaw, 0.0], collider_type=3,
            collider_center=[0.0, 2.02, 0.0], collider_size=[3.05, 2.02, 0.78],
            collision_attribute=4, collision_mask=0xFFFFFFFF, material_type=0,
            metallic=0.06, roughness=0.70, env_map=True, env_intensity=0.20, is_static=True,
            **stage_pbr_tiled,
        ))

    # 反復壁の装飾過多を避け、四隅の塔だけに旗・炎・結晶を集約します。
    for index, (x, z, yaw) in enumerate((
        (-31.5, -20.0, 0.52), (31.5, -20.0, -0.52),
        (-31.5, 19.0, 2.62), (31.5, 19.0, -2.62),
    )):
        objects.append(object_data(
            f"Stage3_HighCrown_DecorTower_{index + 1}", "Model", "Stages/high_crown/high_crown_tower.obj",
            [x, 0.80, z], rotation=[0.0, yaw, 0.0], collider_type=0,
            material_type=0, metallic=0.08, roughness=0.70,
            env_map=True, env_intensity=0.22, is_static=True,
            **stage_pbr_tiled,
        ))

    # 27mを一枚の坂にせず、入口側から闘技場へ3.2m上がる九段階段にします。
    objects.append(object_data(
        "Stage3_HighCrown_ApproachStairs", "Model", "Stages/high_crown/high_crown_step.obj",
        # OBJ読込時のZ反転をY軸180度回転で相殺し、入口側の到着床と判定を一致させます。
        [0.0, 0.0, 45.0], rotation=[0.0, math.pi, 0.0], collider_type=0,
        material_type=0, metallic=0.04, roughness=0.80,
        env_map=True, env_intensity=0.20, is_static=True,
        **stage_pbr_single,
    ))
    objects.append(object_data(
        "[Collision] Stage3_HighCrown_ApproachLanding", "InvisibleBox", "",
        [0.0, 0.0, 63.5], collider_type=3,
        collider_center=[0.0, -5.16, 0.0], collider_size=[6.20, 2.74, 5.0],
        collision_attribute=4, collision_mask=0xFFFFFFFF,
        cast_shadow=False, is_static=True,
    ))
    collision_bottom = -8.0
    for index in range(9):
        top_y = -2.40 + index * 0.40
        center_z = 45.0 + 13.5 - 3.0 * (index + 0.5)
        # 高速落下時も段の下へ抜けないよう、見た目より下側だけ判定を厚くします。
        half_y = (top_y - collision_bottom) * 0.5
        center_y = (collision_bottom + top_y) * 0.5
        objects.append(object_data(
            f"[Collision] Stage3_HighCrown_ApproachStair_{index + 1:02d}", "InvisibleBox", "",
            [0.0, 0.0, center_z], collider_type=3,
            collider_center=[0.0, center_y, 0.0], collider_size=[6.20, half_y, 1.50],
            collision_attribute=4, collision_mask=0xFFFFFFFF,
            cast_shadow=False, is_static=True,
        ))

    # 格子と分離した石造外枠。開門中も消えず、入口と封鎖位置を常に示します。
    # 戦闘封鎖用の門だけを常設します。開始地点の背後に同型の門を置くと、
    # 三人称カメラと階段の間へ門柱が入り込むため、到着側には配置しません。
    for frame_name, frame_z, frame_yaw in (
        ("BossGateFrame", 33.00, 0.0),
    ):
        frame_y = 0.72
        objects.append(object_data(
            f"Stage3_HighCrown_{frame_name}", "Model", "Stages/high_crown/high_crown_gate_frame.obj",
            [0.0, frame_y, frame_z], rotation=[0.0, frame_yaw, 0.0], collider_type=0,
            material_type=0, metallic=0.08, roughness=0.66,
            env_map=True, env_intensity=0.22, is_static=True,
            **stage_pbr_tiled,
        ))
        for side_index, x in enumerate((-7.25, 7.25)):
            objects.append(object_data(
                f"[Collision] Stage3_HighCrown_{frame_name}_Pillar_{side_index + 1}",
                "InvisibleBox", "", [x, frame_y, frame_z], collider_type=3,
                collider_center=[0.0, 3.65, 0.0], collider_size=[1.36, 3.65, 1.52],
                collision_attribute=4, collision_mask=0xFFFFFFFF,
                cast_shadow=False, is_static=True,
            ))
    return objects


def build_encounter_objects() -> list[dict]:
    objects: list[dict] = []
    entry_param = default_param()
    entry_param.update({"gimmickType": "StageGate", "actionMode": 1, "startActive": True, "speed": 1.0})
    objects.append(object_data(
        "Stage3_HighCrown_EntranceGate", "Gimmick", "",
        [0.0, -0.82, 68.10], rotation=[0.0, math.pi, 0.0],
        collider_type=3, collider_center=[0.0, 0.15, 0.0], collider_size=[0.72, 0.78, 0.20],
        collision_attribute=16, collision_mask=1, gimmick_type="StageGate", param=entry_param,
        cast_shadow=False,
    ))

    encounter_param = default_param()
    encounter_param.update({
        "gimmickType": "ArenaEncounter",
        "actionMode": 2,
        "maxCount": 1,
        "shakeDuration": 1.10,
        "startActive": True,
        "returnOnOff": False,
    })
    objects.append(object_data(
        "Stage3_HighCrown_Encounter", "Gimmick", "",
        [0.0, 0.76, 8.00], collider_type=3, collider_center=[0.0, 1.65, 0.0],
        collider_size=[10.5, 2.60, 2.20], collision_attribute=16, collision_mask=1,
        gimmick_type="ArenaEncounter", my_event_id=6101, target_id=6102,
        param=encounter_param, cast_shadow=False,
    ))

    gate_param = default_param()
    gate_param.update({
        "gimmickType": "BossGate",
        "moveSpeed": 0.58,
        "moveAmount": 8.0,
        "startActive": False,
        "returnOnOff": False,
    })
    objects.append(object_data(
        "Stage3_HighCrown_BossGate", "Gimmick", "Stages/high_crown/high_crown_gate.obj",
        [0.0, 0.72, 33.00], collider_type=3, collider_center=[0.0, 3.58, 0.0],
        collider_size=[6.25, 3.58, 0.55], collision_attribute=4, collision_mask=0xFFFFFFFF,
        gimmick_type="BossGate", my_event_id=6103, param=gate_param,
        material_type=0, metallic=0.34, roughness=0.38, env_map=True, env_intensity=0.38,
        normal_map=GATE_NORMAL_MAP, orm_map=GATE_ORM_MAP, texture_tiling=[1.5, 1.5],
    ))

    reward_param = default_param()
    reward_param.update({
        "gimmickType": "EventReceiver",
        "actionMode": 6,
        "moveAmount": 9.00,
        "moveSpeed": 5.20,
        "jumpPower": 1.85,
        "fallDuration": 1.55,
        "startActive": False,
        "returnOnOff": False,
    })
    reward = object_data(
        "Stage3_HighCrown_VictoryCrown", "Gimmick", "Characters/false_king_slime/false_king_crown",
        [0.0, 0.82, -2.00], scale=[0.92, 0.92, 0.92],
        collider_type=3, collider_center=[0.0, 1.20, 0.0], collider_size=[2.65, 1.25, 2.35],
        collision_attribute=16, collision_mask=1, gimmick_type="EventReceiver", event_type=5,
        my_event_id=6104, param=reward_param, color=[1.0, 1.0, 1.0, 1.0],
        material_type=0, emissive=1.18, metallic=0.38, roughness=0.30,
        env_map=True, env_intensity=0.48, mesh_effect1="Resources/json/effect/effect_crown_idle_shell.json",
        mesh_effect2="Resources/json/effect/effect_crown_aura_ring.json",
    )
    reward["components"]["MeshEffect"] = {
        "_editorPresent": True,
        "primary": "Resources/json/effect/effect_crown_idle_shell.json",
        "secondary": "Resources/json/effect/effect_crown_aura_ring.json",
        "version": 1,
    }
    reward["lod"] = {
        "enabled": False,
        "levels": [
            {"distance": 35.0, "level": 1, "modelName": "Stages/crown/crown_lod1.obj"},
            {"distance": 70.0, "level": 2, "modelName": "Stages/crown/crown_lod2.obj"},
        ],
    }
    objects.append(reward)
    return objects


def build_boss() -> dict:
    boss_param = default_param()
    boss_param.update({
        "enemyType": "FalseKingSlime",
        "actionMode": 1,
        "attackPower": 1.6,
        "detectionRange": 36.0,
        "gravity": 68.0,
        "maxFallSpeed": 62.0,
        "hp": 720.0,
        "maxHp": 720.0,
        "moveSpeed": 2.1,
        "speed": 2.1,
        "shakeDuration": 1.45,
        "startActive": False,
        "returnOnOff": False,
    })
    return object_data(
        "Stage3_HighCrown_FalseKingSlime", "Enemy", "Characters/false_king_slime",
        [0.0, 0.72, -16.0], scale=[1.30, 1.30, 1.30], rotation=[0.0, math.pi, 0.0],
        collider_type=3, collider_center=[0.0, 1.48, 0.0], collider_size=[2.82, 1.46, 2.28],
        collision_attribute=0, collision_mask=0, save_category="Enemy", enemy_type="FalseKingSlime",
        my_event_id=6102, target_id=6101, param=boss_param, material_type=25,
        color=[0.96, 0.98, 1.0, 1.0], emissive=1.0, metallic=0.0,
        roughness=0.24, env_map=True, env_intensity=0.58,
        normal_map=BOSS_NORMAL_MAP, orm_map=BOSS_ORM_MAP, texture_tiling=[1.35, 1.35],
    )


def build_player() -> dict:
    old_path = OBJECT_DIR / "stage3_player.json"
    old_data = json.loads(old_path.read_text(encoding="utf-8"))
    player = deepcopy(old_data["objects"][0])
    player["guid"] = stable_guid("Stage3_HighCrown_Player")
    player["name"] = "Stage3_HighCrown_Player"
    # 到着床の中央から開始し、階段全体を正面に見せてから闘技場へ入る流れにします。
    # 地面と完全一致させず、わずかに上へ置いて初期フレームのめり込みも防ぎます。
    player["parentGuid"] = ""
    player["parentName"] = ""
    return player


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8", newline="\n")


def write_high_crown_light() -> None:
    # 真珠色のボスと金縁を白飛びさせず、参考ラフの青空と暖色キーライトを両立します。
    write_json(LIGHT_PATH, {
        "clearColor": [0.38, 0.61, 0.84, 1.0],
        "skybox": {"enabled": True, "texture": SKYBOX_PATH},
        "shadow": {"resolution": 2048, "areaSize": 150.0},
        "directionalLight": {
            "ambientColor": [0.40, 0.45, 0.56],
            "color": [1.0, 0.96, 0.90, 1.0],
            "direction": [-0.34, -0.85, 0.40],
            "enableFog": 1,
            "fogColor": [0.58, 0.76, 0.95],
            "fogEnd": 330.0,
            "fogHeightMax": 48.0,
            "fogHeightMin": -62.0,
            "fogStart": 105.0,
            "intensity": 0.88,
            "volumetricIntensity": 0.055,
            "volumetricSteps": 24,
        },
        "pointLights": [],
        "spotLights": [],
    })
    write_json(Path(str(LIGHT_PATH) + ".meta"), {
        "assetType": "JSON",
        "guid": uuid.uuid5(GUID_NAMESPACE, "stage3-high-crown:light").hex,
        "importSettings": {},
        "importer": "JsonImporter",
        "source": "Resources/json/light/stage3_high_crown.json",
        "version": 1,
    })


def validate(objects: list[dict], enemies: list[dict], player: dict) -> None:
    all_objects = [*objects, *enemies, player]
    names = [item["name"] for item in all_objects]
    guids = [item["guid"] for item in all_objects]
    if len(names) != len(set(names)):
        raise RuntimeError("Object名が重複しています。")
    if len(guids) != len(set(guids)):
        raise RuntimeError("GUIDが重複しています。")
    links = {item.get("myEventID", -1): item for item in all_objects if item.get("myEventID", -1) > 0}
    required = {6101, 6102, 6103, 6104}
    if set(links) != required:
        raise RuntimeError(f"ボスイベント接続が不正です: {sorted(links)}")
    reward = links[6104]
    if reward["eventID"] != 5 or reward["param"]["startActive"]:
        raise RuntimeError("撃破報酬の王冠設定が不正です。")
    if links[6102]["targetID"] != 6101 or links[6101]["targetID"] != 6102:
        raise RuntimeError("ボスとEncounterの相互リンクが不正です。")
    if links[6101]["param"]["maxCount"] != 1:
        raise RuntimeError("封鎖ゲート数が報酬ID規約と一致しません。")
    encounter = links[6101]
    gate = links[6103]
    boss = links[6102]
    if gate["position"][2] - encounter["position"][2] < 12.0:
        raise RuntimeError("封鎖ゲートと進入判定の間にカメラ用の奥行きがありません。")
    if gate["normalMapPath"] != GATE_NORMAL_MAP or gate["ormMapPath"] != GATE_ORM_MAP:
        raise RuntimeError("封鎖ゲートに金属用PBRマップが設定されていません。")
    if abs(boss["rotation"][1] - math.pi) > 0.001:
        raise RuntimeError("偽王スライムが進入方向を向いていません。")
    if boss["materialType"] != 25 or not boss["enableNormalMap"]:
        raise RuntimeError("偽王スライムの真珠PBR設定が不正です。")


def main() -> None:
    static_models = build_static_models()
    encounter_objects = build_encounter_objects()
    objects = [*static_models, *encounter_objects]
    enemies = [build_boss()]
    player = build_player()
    validate(objects, enemies, player)

    write_json(OBJECT_DIR / "stage3_object.json", {"objects": objects})
    write_json(OBJECT_DIR / "stage3_enemy.json", {"objects": enemies})
    write_json(OBJECT_DIR / "stage3_player.json", {"objects": [player]})
    write_high_crown_light()
    write_json(OBJECT_DIR / "stage3.json", {
        "_comment": "Actual data is in _player, _enemy, and _object.json",
        "_sceneAsset": {
            "controller": "DEFAULT",
            "displayName": "stage3",
            "id": "stage3",
            "resources": {
                "bgm": "",
                "camera": "",
                "light": "Resources/json/light/stage3_high_crown.json",
                "skybox": SKYBOX_PATH,
            },
            "runtimeScene": "GAMEPLAY",
            "spriteLayout": "Resources/json/sprite/stage3_sprite.json",
            "version": 3,
        },
    })

    stage_select = json.loads(STAGE_SELECT.read_text(encoding="utf-8"))
    for stage in stage_select.get("stages", []):
        if stage.get("id") == "stage3":
            stage["description"] = "黒格子に閉ざされた天空闘技場で偽王スライムに挑むボスステージ"
            stage["lightPath"] = "Resources/json/light/stage3_high_crown.json"
            stage["skyboxPath"] = SKYBOX_PATH
            break
    write_json(STAGE_SELECT, stage_select)

    wall_count = sum(1 for item in objects if "_Wall_" in item["name"])
    print(f"Generated Stage 3 objects: {len(objects)} (walls={wall_count}, arena=80x68m)")
    print("Encounter flow: 6101 -> 6102 / gate=6103 / crown=6104")


if __name__ == "__main__":
    main()
