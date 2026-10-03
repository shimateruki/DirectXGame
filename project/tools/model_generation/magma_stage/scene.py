"""火山ステージの配置・設定更新。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Iterable, Sequence

from .common import (
    CAMERA_PATH,
    GUID_NAMESPACE,
    MAGMA_BARRIER_COUNT,
    MAGMA_BOSS_EVENT_ID,
    MAGMA_DEFEAT_EVENT_ID,
    MODEL_ROOT,
    PLAYER_PATH,
    SCENE_PATH,
)

def quaternion_from_euler(rotation: Sequence[float]) -> list[float]:
    x, y, z = rotation
    sx, cx = math.sin(x * 0.5), math.cos(x * 0.5)
    sy, cy = math.sin(y * 0.5), math.cos(y * 0.5)
    sz, cz = math.sin(z * 0.5), math.cos(z * 0.5)
    return [
        sx * cy * cz + cx * sy * sz,
        cx * sy * cz - sx * cy * sz,
        cx * cy * sz + sx * sy * cz,
        cx * cy * cz - sx * sy * sz,
    ]


def set_transform(
    obj: dict,
    position: Sequence[float],
    scale: Sequence[float],
    rotation: Sequence[float] = (0.0, 0.0, 0.0),
) -> None:
    obj["position"] = [float(value) for value in position]
    obj["scale"] = [float(value) for value in scale]
    obj["rotation"] = [float(value) for value in rotation]
    obj["quaternion"] = quaternion_from_euler(rotation)


def set_collider(obj: dict, size: Sequence[float], center: Sequence[float], collider_type: int = 3) -> None:
    collider = obj.setdefault("collider", {})
    collider["type"] = collider_type
    collider["size"] = [float(value) for value in size]
    collider["center"] = [float(value) for value in center]
    collider["rotation"] = [0.0, 0.0, 0.0]


def set_gameplay_link(obj: dict, event_id: int, target_id: int) -> None:
    """イベントIDを旧フィールドとComponentの両方へ同期します。"""

    obj["myEventID"] = int(event_id)
    obj["targetID"] = int(target_id)
    components = obj.setdefault("components", {})
    components["GameplayLink"] = {
        "_editorPresent": True,
        "eventId": int(event_id),
        "targetId": int(target_id),
        "version": 1,
    }


class SceneBuilder:
    MODEL_COLLIDERS = {
        # ColliderConfig::size は全長ではなく中心から各面までの半寸法。
        "magma_fortress_foundation": ((16.0, 2.4, 12.0), (0.0, -2.4, 0.0)),
        "basalt_causeway": ((14.0, 1.6, 4.0), (0.0, -1.6, 0.0)),
        "metal_grate_platform": ((5.8, 0.38, 5.0), (0.0, -0.38, 0.0)),
        "fortress_plaza": ((13.0, 1.9, 11.0), (0.0, -1.9, 0.0)),
        "fortress_pillar": ((2.1, 4.45, 2.1), (0.0, 4.45, 0.0)),
        "fortress_ramp": ((15.0, 1.7, 5.6), (0.0, -1.7, 0.0)),
        "fortress_grate_drum": ((9.6, 3.22, 3.22), (0.0, 0.0, 0.0)),
    }

    def __init__(self, scene: dict) -> None:
        self.scene = scene
        self.source = {obj.get("name", ""): obj for obj in scene.get("objects", [])}
        source_aliases = {
            # 生成後の名前を先に探し、旧シーンからの初回実行にも対応する。
            "Stage2_Island_StartShelf": ("Stage2_StartFoundation", "Stage2_Island_Start"),
            "Stage2_Ridge_BomberGuard": ("Stage2_EmberLandingBomber",),
            "Stage2_Cannon_EmberLanding_A": ("Stage2_EmberLandingCannon",),
            "Stage2_CalderaBase_ThunderGuard": ("Stage2_MainCalderaThunderGuard",),
            "Stage2_Ridge_FireGuard_A": ("Stage2_MainCalderaFireGuard",),
            "Stage2_Rim_FireGuard": ("Stage2_RimFireGuard",),
            "Stage2_Rim_BomberGuard": ("Stage2_RimBomberGuard",),
            "Stage2_Cannon_Rim_A": ("Stage2_RimCannon",),
            "Stage2_Final_FireGuard_A": ("Stage2_FinalFireGuard",),
            "Stage2_Final_ThunderGuard": ("Stage2_FinalThunderGuard",),
            "Stage2_Final_BomberGuard": ("Stage2_FinalBomberGuard",),
            "Stage2_RiverCoin_A_01": ("Stage2_StartCoin_01", "Stage2_RaftRouteCoin_01"),
        }
        for alias, generated_names in source_aliases.items():
            if alias in self.source:
                continue
            for generated_name in generated_names:
                if generated_name in self.source:
                    self.source[alias] = self.source[generated_name]
                    break
        self.objects: list[dict] = []

    def clone(self, source_name: str, name: str) -> dict:
        if source_name not in self.source:
            raise KeyError(f"複製元がありません: {source_name}")
        obj = copy.deepcopy(self.source[source_name])
        obj["name"] = name
        obj["guid"] = str(uuid.uuid5(GUID_NAMESPACE, name))
        obj["parentGuid"] = ""
        obj["parentName"] = ""
        self.objects.append(obj)
        return obj

    @staticmethod
    def configure_fortress_material(obj: dict) -> None:
        obj.update({
            "blendMode": 0,
            "materialType": 0,
            "color": [1.0, 1.0, 1.0, 1.0],
            "emissive": 1.03,
            "roughness": 0.48,
            "metallic": 0.18,
            "envIntensity": 1.25,
            "enableLighting": True,
            "enableNormalMap": False,
        })

    @staticmethod
    def configure_as_model(obj: dict) -> None:
        obj["type"] = "Model"
        obj["gimmickType"] = ""
        obj["enemyType"] = ""
        obj["itemType"] = ""
        obj.pop("param", None)
        obj["isStatic"] = True

    def add_fortress_model(
        self,
        name: str,
        model: str,
        position: Sequence[float],
        scale: Sequence[float],
        rotation: Sequence[float] = (0.0, 0.0, 0.0),
        collide: bool = True,
        cast_shadow: bool = True,
    ) -> dict:
        obj = self.clone("Stage2_Island_StartShelf", name)
        self.configure_as_model(obj)
        self.configure_fortress_material(obj)
        obj["modelName"] = f"Stages/{model}"
        obj["castShadow"] = bool(cast_shadow)
        set_transform(obj, position, scale, rotation)
        if collide:
            size, center = self.MODEL_COLLIDERS[model]
            set_collider(obj, size, center)
            obj["collisionAttribute"] = 4
            obj["collisionMask"] = 4294967295
        else:
            set_collider(obj, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0)
            obj["collisionAttribute"] = 0
            obj["collisionMask"] = 0
        return obj

    def add_backdrop_ridge(
        self,
        name: str,
        position: Sequence[float],
        scale: Sequence[float],
        yaw: float,
        tint: Sequence[float],
    ) -> dict:
        """プレイ領域に干渉しない遠景用岩山を追加する。"""

        return self.add_magma_backdrop(
            name,
            "volcanic_backdrop_ridge",
            position,
            scale,
            yaw,
            tint,
            1.62,
        )

    def add_magma_backdrop(
        self,
        name: str,
        model: str,
        position: Sequence[float],
        scale: Sequence[float],
        yaw: float,
        tint: Sequence[float],
        emissive: float,
    ) -> dict:
        """当たり判定や影を持たないマグマ海の背景ランドマークを追加する。"""

        obj = self.add_fortress_model(
            name,
            model,
            position,
            scale,
            (0.0, yaw, 0.0),
            collide=False,
            cast_shadow=False,
        )
        bright_tint = [
            min(1.0, 0.58 + float(tint[0]) * 0.38),
            min(1.0, 0.10 + float(tint[1]) * 0.50),
            min(1.0, 0.06 + float(tint[2]) * 0.35),
            float(tint[3]),
        ]
        obj.update({
            "color": bright_tint,
            "emissive": float(emissive),
            "roughness": 0.86,
            "metallic": 0.04,
            "envIntensity": 0.62,
        })
        return obj

    def add_foundation_skirt(
        self,
        name: str,
        center: Sequence[float],
        footprint: Sequence[float],
        yaw: float = 0.0,
    ) -> dict:
        # 上端の縁石を床面直下へ収めつつ、底をマグマ面より下へ沈める。
        floor_y = float(center[1])
        bottom_y = -9.25
        vertical_scale = (floor_y - 0.12 - bottom_y) / (4.8 + 0.44)
        top_y = bottom_y + 4.8 * vertical_scale
        return self.add_fortress_model(
            f"{name}_Substructure",
            "magma_fortress_foundation",
            (center[0], top_y, center[2]),
            (footprint[0] / 32.0, vertical_scale, footprint[1] / 24.0),
            (0.0, yaw, 0.0),
            collide=False,
            cast_shadow=False,
        )

    def add_keep(
        self,
        name: str,
        model: str,
        center: Sequence[float],
        footprint: Sequence[float],
        yaw: float = 0.0,
    ) -> dict:
        if model == "magma_fortress_foundation":
            # 基礎モデル自体の底面をマグマ面より下へ通す。中央上面はY=0なので平坦さを保てる。
            vertical_scale = (float(center[1]) + 9.25) / 4.8
            nominal_width = 32.0
            nominal_depth = 24.0
        else:
            # 広場の階段を縦に引き伸ばさず、直下の基礎だけをマグマ底へ延ばす。
            self.add_foundation_skirt(name, center, footprint, yaw)
            vertical_scale = 1.0
            nominal_width = 26.0
            nominal_depth = 22.0
        return self.add_fortress_model(
            name,
            model,
            center,
            (footprint[0] / nominal_width, vertical_scale, footprint[1] / nominal_depth),
            (0.0, yaw, 0.0),
        )

    def add_causeway(
        self,
        name: str,
        center: Sequence[float],
        footprint: Sequence[float],
        yaw: float = 0.0,
    ) -> dict:
        vertical_scale = (float(center[1]) + 9.25) / 3.2
        return self.add_fortress_model(
            name,
            "basalt_causeway",
            center,
            (footprint[0] / 28.0, vertical_scale, footprint[1] / 8.0),
            (0.0, yaw, 0.0),
        )

    def add_causeway_between(
        self,
        name: str,
        start: Sequence[float],
        end: Sequence[float],
        width: float,
    ) -> dict:
        run_x = float(end[0]) - float(start[0])
        run_z = float(end[2]) - float(start[2])
        length = math.hypot(run_x, run_z)
        if length <= 0.0:
            raise ValueError(f"土手道の距離が不正です: {name}")
        if abs(float(end[1]) - float(start[1])) > 0.05:
            raise ValueError(f"土手道の両端に高低差があります: {name}")
        center = tuple((float(start[index]) + float(end[index])) * 0.5 for index in range(3))
        yaw = -math.atan2(run_z, run_x)
        return self.add_causeway(name, center, (length, width), yaw)

    def add_ramp(
        self,
        name: str,
        start: Sequence[float],
        end: Sequence[float],
        width: float,
    ) -> dict:
        run_x = float(end[0]) - float(start[0])
        run_z = float(end[2]) - float(start[2])
        horizontal_length = math.hypot(run_x, run_z)
        if horizontal_length <= 0.0:
            raise ValueError(f"坂の水平距離が不正です: {name}")
        height_delta = float(end[1]) - float(start[1])
        slope = math.atan2(height_delta, horizontal_length)
        slope_length = math.hypot(horizontal_length, height_delta)
        yaw = -math.atan2(run_z, run_x)
        midpoint = tuple((float(start[index]) + float(end[index])) * 0.5 for index in range(3))
        ramp = self.add_fortress_model(
            name,
            "fortress_ramp",
            midpoint,
            (slope_length / 30.0, 1.0, width / 11.2),
            (0.0, yaw, slope),
        )
        # 傾斜床自体をマグマ面まで引き伸ばすとOBBも斜め下へ肥大化するため、
        # 支持構造は独立した柱で表現する。
        self.add_pillar(f"{name}_StartSupport", start, float(start[1]) - 2.9)
        self.add_pillar(f"{name}_EndSupport", end, float(end[1]) - 2.9)
        return ramp

    def add_pillar(self, name: str, position: Sequence[float], support_top_y: float) -> dict:
        base_y = -9.25
        vertical_scale = max((support_top_y - base_y) / 8.9, 0.2)
        return self.add_fortress_model(
            name,
            "fortress_pillar",
            (position[0], base_y, position[2]),
            (1.15, vertical_scale, 1.15),
            collide=False,
            cast_shadow=False,
        )

    def add_gate(self, name: str, position: Sequence[float], scale: Sequence[float]) -> dict:
        # 開口を一枚のOBBで塞がないよう、門は見た目専用にする。
        return self.add_fortress_model(
            name,
            "fortress_gate",
            position,
            scale,
            (0.0, math.pi * 0.5, 0.0),
            collide=False,
        )

    def add_stage_entry_gate(self) -> dict:
        obj = self.clone("Stage2_MagmaSea", "Stage2_EntranceGate")
        obj.update({
            "type": "Gimmick",
            "gimmickType": "StageGate",
            "enemyType": "",
            "itemType": "",
            "modelName": "Gimmicks/crown_stage_gate",
            "isStatic": False,
            "castShadow": False,
            "blendMode": 1,
            "materialType": 22,
            "color": [0.35, 0.75, 1.0, 1.0],
            "emissive": 2.0,
            "roughness": 0.5,
            "metallic": 0.0,
            "enableEnvMap": False,
            "collisionAttribute": 16,
            "collisionMask": 1,
            "eventID": 0,
            "targetID": -1,
            "myEventID": -1,
        })
        set_transform(
            obj,
            (-471.0, 4.39, -120.0),
            (2.05, 2.05, 0.94),
            (0.0, math.pi * 0.5, 0.0),
        )
        set_collider(obj, (0.72, 0.78, 0.20), (0.0, 0.15, 0.0))
        obj["param"] = {
            "actionMode": 1,
            "gimmickType": "StageGate",
            "targetScene": "SELECT",
            "startActive": True,
            "returnOnOff": True,
            "moveSpeed": 6.0,
            "speed": 1.0,
            "hp": 100.0,
            "maxHp": 100.0,
        }
        obj["waterParam"] = {
            "billboardScale": 1.48,
            "effectIntensity": 1.72,
            "effectScale": 1.08,
            "effectScaleX": 1.198,
            "effectScaleY": 1.215,
            "effectScaleZ": 0.425,
            "effectSoftness": 0.62,
            "effectType": 1.0,
            "flowSpeedX": 0.0,
            "flowSpeedY": 0.0,
            "waveFrequency": 18.0,
            "waveHeight": 1.1,
            "waveSpeed": 1.8,
        }

        gate_position = obj["position"]
        gate_yaw = float(obj["rotation"][1])
        side = (math.cos(gate_yaw), 0.0, -math.sin(gate_yaw))

        def make_visual(
            name: str,
            model_name: str,
            position: Sequence[float],
            scale: Sequence[float],
            rotation: Sequence[float],
        ) -> dict:
            visual = self.clone("Stage2_MagmaSea", name)
            self.configure_as_model(visual)
            visual.update({
                "modelName": model_name,
                "collisionAttribute": 0,
                "collisionMask": 0,
                "collider": {
                    "center": [0.0, 0.0, 0.0],
                    "rotation": [0.0, 0.0, 0.0],
                    "size": [1.0, 1.0, 1.0],
                    "type": 0,
                },
            })
            visual.pop("waterParam", None)
            set_transform(visual, position, scale, rotation)
            return visual

        pad = make_visual(
            "Stage2_EntryDecor_Pad",
            "Stages/stage_select_gate_pad",
            (gate_position[0], gate_position[1] - 2.27, gate_position[2]),
            (1.0, 1.0, 1.0),
            (0.0, 0.0, 0.0),
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
            "Stage2_EntryDecor_Frame",
            "Stages/gate",
            (gate_position[0], gate_position[1] + 0.82, gate_position[2]),
            (1.0, 1.0, 1.0),
            (0.0, gate_yaw - math.pi * 0.5, math.pi * 0.5),
        )
        frame.update({
            "blendMode": 1,
            "castShadow": True,
            "color": [1.0, 1.0, 1.0, 1.0],
            "emissive": 1.0,
            "enableEnvMap": False,
            "roughness": 0.62,
        })

        for side_index, side_sign in enumerate((-1.0, 1.0), start=1):
            offset = tuple(component * 4.16 * side_sign for component in side)
            brazier_position = (
                gate_position[0] + offset[0],
                gate_position[1] - 2.28,
                gate_position[2] + offset[2],
            )
            brazier = make_visual(
                f"Stage2_EntryDecor_Brazier_{side_index}",
                "Gimmicks/brazier",
                brazier_position,
                (1.55, 1.55, 1.55),
                (0.0, gate_yaw, 0.0),
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
                f"Stage2_EntryDecor_Flame_{side_index}",
                "Effects/flame",
                (brazier_position[0], gate_position[1] - 0.03, brazier_position[2]),
                (1.0, 1.0, 1.0),
                (0.0, 0.0, 0.0),
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
        return obj

    def add_grate(
        self,
        name: str,
        position: Sequence[float],
        gimmick_type: str = "",
        speed: float = 0.0,
        action_mode: int = 0,
        move_amount: float = 0.0,
        rotation: Sequence[float] = (0.0, 0.0, 0.0),
        event_id: int = -1,
        start_active: bool = True,
    ) -> dict:
        if not gimmick_type:
            return self.add_fortress_model(
                name,
                "metal_grate_platform",
                position,
                (12.0 / 11.6, 1.0, 12.0 / 10.0),
            )

        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = gimmick_type
        obj["modelName"] = "Stages/metal_grate_platform"
        self.configure_fortress_material(obj)
        obj["isStatic"] = False
        set_transform(obj, position, (12.0 / 11.6, 1.0, 12.0 / 10.0), rotation)
        size, center = self.MODEL_COLLIDERS["metal_grate_platform"]
        set_collider(obj, size, center)
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        param = obj.setdefault("param", {})
        param["gimmickType"] = gimmick_type
        obj["myEventID"] = int(event_id)
        param["startActive"] = bool(start_active)
        param["returnOnOff"] = True
        if gimmick_type == "RotatingFloor":
            param["actionMode"] = int(action_mode)
            param["speed"] = float(speed)
        elif gimmick_type == "MovingFloor":
            param["actionMode"] = int(action_mode)
            param["speed"] = float(speed)
            param["moveAmount"] = float(move_amount)
        return obj

    def add_switch(self, name: str, position: Sequence[float], target_id: int) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "TimedSwitch"
        obj["modelName"] = "Gimmicks/star_dash_panel"
        obj["isStatic"] = False
        obj["castShadow"] = False
        obj["materialType"] = 24
        obj["color"] = [0.25, 0.75, 1.0, 1.0]
        obj["emissive"] = 1.0
        set_transform(obj, position, (0.9, 0.45, 0.9))
        set_collider(obj, (2.2, 0.18, 0.9), (0.0, -0.08, 0.0))
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        obj["targetID"] = int(target_id)
        obj["myEventID"] = -1
        obj.setdefault("param", {})["gimmickType"] = "TimedSwitch"
        return obj

    def add_receiver_grate(
        self,
        name: str,
        position: Sequence[float],
        event_id: int,
    ) -> dict:
        obj = self.add_grate(name, position, "EventReceiver")
        obj["myEventID"] = int(event_id)
        obj["targetID"] = -1
        obj.setdefault("param", {}).update({
            "gimmickType": "EventReceiver",
            "actionMode": 0,
            "startActive": False,
            "returnOnOff": False,
            "moveSpeed": 6.0,
        })
        return obj

    def add_breakable_block(
        self,
        name: str,
        position: Sequence[float],
        scale: Sequence[float],
    ) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "BreakableBlock"
        obj["modelName"] = "Stages/bomb_break_block"
        obj["isStatic"] = False
        obj.update({
            "texturePath": "Resources/3DModel/Stages/bomb_break_block/bomb_break_block_albedo.png",
            "normalMapPath": "Resources/3DModel/Stages/bomb_break_block/bomb_break_block_normal.png",
            "ormMapPath": "Resources/3DModel/Stages/bomb_break_block/bomb_break_block_orm.png",
            "enableNormalMap": True,
            "materialType": 0,
            "color": [1.0, 1.0, 1.0, 1.0],
            "roughness": 0.72,
            "metallic": 0.0,
        })
        set_transform(obj, position, scale)
        # bomb_break_block は一辺1mなので、ColliderConfigには半寸法0.5mを渡す。
        set_collider(obj, (0.5, 0.5, 0.5), (0.0, 0.0, 0.0))
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        obj.setdefault("param", {})["gimmickType"] = "BreakableBlock"
        return obj

    def add_dash_panel(self, name: str, position: Sequence[float], yaw: float) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "DashPanel"
        obj["modelName"] = "Gimmicks/star_dash_panel"
        obj["isStatic"] = False
        obj["castShadow"] = False
        obj.update({
            "materialType": 24,
            "color": [0.25, 0.95, 1.0, 1.0],
            "roughness": 0.62,
            "metallic": 0.56,
            "emissive": 1.0,
        })
        set_transform(obj, position, (1.35, 1.0, 1.1), (0.0, yaw, 0.0))
        set_collider(obj, (2.2, 0.18, 0.9), (0.0, -0.08, 0.0))
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        obj.setdefault("param", {})["gimmickType"] = "DashPanel"
        return obj

    def add_star_coin(self, name: str, position: Sequence[float], index: int) -> dict:
        obj = self.clone("Stage2_Island_StartShelf", name)
        self.configure_as_model(obj)
        obj["modelName"] = "Gimmicks/star"
        obj["isStatic"] = False
        obj["castShadow"] = False
        obj.update({
            "eventID": 7,
            "targetID": int(index),
            "myEventID": -1,
            "collisionAttribute": 16,
            "collisionMask": 4294967295,
            "materialType": 0,
            "color": [1.0, 0.74, 0.08, 1.0],
            "emissive": 1.65,
            "roughness": 0.32,
            "metallic": 0.18,
        })
        set_transform(obj, position, (0.05, 0.05, 0.05))
        set_collider(obj, (28.62, 5.73, 27.22), (0.0, 0.0, -2.87), 2)
        return obj

    def add_rotating_drum(
        self,
        name: str,
        position: Sequence[float],
        rotation: Sequence[float] = (0.0, 0.0, 0.0),
    ) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "RotatingFloor"
        obj["modelName"] = "Stages/fortress_grate_drum"
        self.configure_fortress_material(obj)
        obj["isStatic"] = False
        set_transform(obj, position, (1.0, 1.0, 1.0), rotation)
        size, center = self.MODEL_COLLIDERS["fortress_grate_drum"]
        set_collider(obj, size, center)
        obj["collisionAttribute"] = 4
        obj["collisionMask"] = 255
        param = obj.setdefault("param", {})
        param.update({
            "gimmickType": "RotatingFloor",
            "actionMode": 0,
            "speed": 28.0,
            "startActive": True,
            "returnOnOff": True,
        })
        return obj

    def add_geyser(
        self,
        name: str,
        position: Sequence[float],
        target_floor_y: float,
        warning: float,
        interval: float,
        initial_delay: float,
        radius: float,
    ) -> dict:
        obj = self.clone("Stage2_MagmaSea", name)
        obj["type"] = "Gimmick"
        obj["gimmickType"] = "MagmaGeyser"
        obj["modelName"] = "Stages/magma_vent"
        self.configure_fortress_material(obj)
        obj["roughness"] = 0.72
        obj["metallic"] = 0.08
        obj["isStatic"] = False
        obj["castShadow"] = False
        set_transform(obj, position, (0.82, 0.82, 0.82))
        height = float(target_floor_y) + 3.0 - float(position[1])
        set_collider(
            obj,
            (radius, height * 0.5, radius),
            (0.0, 0.72 + height * 0.5, 0.0),
            4,
        )
        obj["collisionAttribute"] = 0
        obj["collisionMask"] = 0
        obj.setdefault("param", {}).update({
            "gimmickType": "MagmaGeyser",
            "speed": 4.0,
            "shakeDuration": float(warning),
            "fallDuration": 1.15,
            "interval": float(interval),
            "moveSpeed": float(initial_delay),
            "moveAmount": height,
            "detectionRange": float(radius),
            "gravity": 5.5,
            "jumpPower": 15.0,
            "maxFallSpeed": 110.0,
            "startActive": True,
            "returnOnOff": True,
        })
        return obj

    def add_visual_pillar_pair(
        self,
        prefix: str,
        position: Sequence[float],
        support_top_y: float,
        x_offset: float = 0.0,
        z_offset: float = 0.0,
    ) -> None:
        offsets = ((-x_offset, -z_offset), (x_offset, z_offset)) if x_offset or z_offset else ((0.0, 0.0),)
        for index, (offset_x, offset_z) in enumerate(offsets, start=1):
            self.add_pillar(
                f"{prefix}_Support_{index:02d}",
                (position[0] + offset_x, position[1], position[2] + offset_z),
                support_top_y,
            )

    def add_enemy(self, source_name: str, name: str, position: Sequence[float], yaw: float = 0.0) -> dict:
        obj = self.clone(source_name, name)
        set_transform(obj, position, (1.0, 1.0, 1.0), (0.0, yaw, 0.0))
        return obj

    def add_magma_boss_encounter(self) -> None:
        """上層鍛冶場へ、進入封鎖と出現制御を持つマグマ中ボス戦を配置します。"""

        encounter = self.clone("Stage2_MagmaSea", "Stage2_MagmaArena_Encounter")
        encounter.update({
            "type": "Gimmick",
            "gimmickType": "ArenaEncounter",
            "enemyType": "",
            "itemType": "",
            "modelName": "",
            "isStatic": False,
            "castShadow": False,
            "materialType": 0,
            "color": [1.0, 1.0, 1.0, 1.0],
            "emissive": 1.0,
            "collisionAttribute": 16,
            "collisionMask": 1,
        })
        encounter.pop("waterParam", None)
        set_transform(encounter, (228.0, 33.0, -146.0), (1.0, 1.0, 1.0), (0.0, -0.12, 0.0))
        set_collider(encounter, (5.0, 4.0, 20.0), (0.0, 3.0, 0.0), 3)
        encounter["param"] = {
            "actionMode": 0,
            "gimmickType": "ArenaEncounter",
            "maxCount": MAGMA_BARRIER_COUNT,
            "shakeDuration": 0.74,
            "startActive": True,
            "returnOnOff": False,
        }
        set_gameplay_link(encounter, MAGMA_DEFEAT_EVENT_ID, MAGMA_BOSS_EVENT_ID)

        boss = self.clone("Stage2_Final_FireGuard_A", "Stage2_MagmaArena_Boss")
        boss.update({
            "type": "Enemy",
            "enemyType": "MagmaSlime",
            "gimmickType": "",
            "itemType": "",
            "modelName": "Characters/magma_slime",
            "isStatic": False,
            "castShadow": True,
            "blendMode": 0,
            "materialType": 0,
            "color": [1.0, 0.94, 0.86, 1.0],
            "emissive": 1.18,
            "metallic": 0.12,
            "roughness": 0.26,
            "enableEnvMap": True,
            "envIntensity": 0.42,
            "collisionAttribute": 8,
            "collisionMask": 4294967295,
        })
        boss.pop("waterParam", None)
        set_transform(boss, (270.0, 33.05, -165.0), (1.0, 1.0, 1.0), (0.0, -1.45, 0.0))
        set_collider(boss, (0.96, 0.96, 0.96), (0.0, 0.88, 0.0), 1)
        boss["param"] = {
            "enemyType": "MagmaSlime",
            "actionMode": 1,
            "shakeDuration": 1.16,
            "startActive": False,
        }
        set_gameplay_link(boss, MAGMA_BOSS_EVENT_ID, MAGMA_DEFEAT_EVENT_ID)

        barrier_specs = (
            ("West", (222.0, 37.4, -165.0), (34.0, 6.4, 1.0), math.pi * 0.5),
            ("East", (318.0, 37.4, -165.0), (34.0, 6.4, 1.0), math.pi * 0.5),
            ("North", (270.0, 37.4, -199.0), (48.0, 6.4, 1.0), 0.0),
            ("South", (270.0, 37.4, -131.0), (48.0, 6.4, 1.0), 0.0),
        )
        for index, (side, position, scale, yaw) in enumerate(barrier_specs, start=1):
            barrier = self.clone("Stage2_MagmaSea", f"Stage2_MagmaBarrier_{side}")
            barrier.update({
                "type": "Gimmick",
                "gimmickType": "PrismBarrier",
                "enemyType": "",
                "itemType": "",
                "modelName": "Gimmicks/portal_surface",
                "isStatic": False,
                "castShadow": False,
                "blendMode": 1,
                "materialType": 22,
                "color": [1.0, 0.20, 0.02, 0.86],
                "emissive": 2.65,
                "metallic": 0.0,
                "roughness": 0.22,
                "enableEnvMap": False,
                "collisionAttribute": 0,
                "collisionMask": 0,
            })
            set_transform(barrier, position, scale, (0.0, yaw, 0.0))
            set_collider(barrier, (1.0, 1.0, 0.45), (0.0, 0.0, 0.0), 3)
            barrier["param"] = {
                "gimmickType": "PrismBarrier",
                "moveSpeed": 0.42,
                "interval": 2.7,
                "startActive": False,
                "returnOnOff": True,
            }
            barrier["waterParam"] = {
                "billboardScale": 1.0,
                "effectIntensity": 1.82,
                "effectScale": 1.0,
                "effectScaleX": 1.0,
                "effectScaleY": 1.0,
                "effectScaleZ": 0.08,
                "effectSoftness": 0.46,
                "effectType": 3.0,
                "flowSpeedX": 0.0,
                "flowSpeedY": 0.0,
                "waveFrequency": 18.0,
                "waveHeight": 0.72,
                "waveSpeed": 1.25,
            }
            set_gameplay_link(barrier, MAGMA_BOSS_EVENT_ID + index, -1)

    def add_coin(self, name: str, position: Sequence[float]) -> dict:
        obj = self.clone("Stage2_RiverCoin_A_01", name)
        obj["castShadow"] = False
        set_transform(obj, position, (0.065, 0.065, 0.065))
        return obj

def add_coin_curve(
    builder: SceneBuilder,
    prefix: str,
    start: Sequence[float],
    end: Sequence[float],
    count: int,
    arc_height: float = 0.0,
) -> None:
    for index in range(count):
        t = (index + 1) / (count + 1)
        position = [start[axis] + (end[axis] - start[axis]) * t for axis in range(3)]
        position[1] += math.sin(math.pi * t) * arc_height
        builder.add_coin(f"{prefix}_{index + 1:02d}", position)


def add_magma_pressure_route(builder: SceneBuilder) -> None:
    """噴射で高度を稼ぐ岩盤ルート。生成後の微調整はEditorで行う。"""
    def rock(name, position, target=-1):
        obj = builder.clone("Stage2_MagmaSea", name)
        obj.pop("components", None)
        builder.configure_as_model(obj)
        builder.configure_fortress_material(obj)
        obj.update(modelName="Stages/magma_lift_rock", materialType=35, emissive=0.1, roughness=0.9, metallic=0.0,
                   castShadow=False, collisionAttribute=4, collisionMask=4294967295,
                   eventID=0, gpuParticleName="", particleName="")
        obj["recorder"] = {"recordPathName": "", "isRecordLoop": False, "isRecordRelative": False}
        set_transform(obj, position, (10.0, 2.4, 10.0))
        set_collider(obj, (0.52, 0.65, 0.52), (0.0, -0.65, 0.0), 2)
        set_gameplay_link(obj, -1, target)
        if target >= 0:
            obj.update(type="Gimmick", gimmickType="MovingFloor", isStatic=False)
            obj["param"] = dict(gimmickType="MovingFloor", actionMode=5, speed=0,
                                moveAmount=8, startActive=True)

    def vent(name, position, event_id, mode, delay):
        obj = builder.add_geyser(name, position, 0.0, 1.5, 2.0, delay, 5.3)
        obj.pop("components", None)
        set_gameplay_link(obj, event_id, -1)
        set_transform(obj, position, (1.4, 1.0, 1.4))
        obj["param"].update(actionMode=mode, moveAmount=8, fallDuration=5,
                            maxFallSpeed=160, speed=1.0)
        return obj

    rock("Stage2_MagmaEntryRock", (-218, 8.5, 139))
    vent("Stage2_LiftVent_01", (-239, -7.5, 149), 8311, 2, 0)
    rock("Stage2_LiftRock_01", (-239, 8.5, 149), 8311)
    rock("Stage2_MagmaRestRock", (-258, 16.5, 168))
    vent("Stage2_LiftVent_02", (-278, -7.5, 186), 8312, 2, 2.4)
    rock("Stage2_LiftRock_02", (-278, 16.5, 186), 8312)
    fire = vent("Stage2_MagmaFireballVent", (-247, -7.5, 131), 8313, 1, 1.5)
    fire["param"].update(jumpPower=30, gravity=7, maxCount=2, healAmount=0.65,
                         fallDuration=1.1, interval=3.8, moveAmount=5, detectionRange=2.1)
    set_transform(fire, fire["position"], fire["scale"], (0, 0.9, 0))
    builder.add_keep("Stage2_StarBalcony_01", "fortress_plaza", (-304, 24.5, 205), (38, 32), -0.25)
    builder.add_star_coin("Stage2_StarCoin_01", (-304, 28.0, 205), 0)


def build_scene() -> None:
    scene = json.loads(SCENE_PATH.read_text(encoding="utf-8-sig"))
    builder = SceneBuilder(scene)

    magma = builder.clone("Stage2_MagmaSea", "Stage2_MagmaSea")
    set_transform(magma, (20.0, -10.0, 0.0), (900.0, 2.0, 700.0))
    magma["color"] = [1.0, 0.16, 0.008, 1.0]
    magma["emissive"] = 1.62
    magma["roughness"] = 0.42
    magma["isStatic"] = True
    magma["castShadow"] = False
    magma["textureTiling"] = [32.0, 24.0]
    magma["autoTextureTiling"] = False
    magma["waterParam"] = {
        "billboardScale": 1.0,
        "effectIntensity": 1.48,
        "effectScale": 1.52,
        "effectScaleX": 1.0,
        "effectScaleY": 1.0,
        "effectScaleZ": 1.0,
        "effectSoftness": 0.60,
        "effectType": 0.0,
        "flowSpeedX": 0.072,
        "flowSpeedY": 0.027,
        "waveFrequency": 2.75,
        "waveHeight": 0.46,
        "waveSpeed": 0.68,
    }

    # 低い火口島、中景の火山塔、遠景の稜線を分けて置き、四方向に視差を作る。
    # 背景はすべて当たり判定と影を無効化し、プレイ経路の読みやすさを保つ。
    backdrop_ridges = (
        ("Stage2_Backdrop_Ridge_NorthWest", (-650.0, -9.5, 620.0), (2.20, 1.30, 2.06), 0.18, (0.66, 0.38, 0.27, 1.0)),
        ("Stage2_Backdrop_Ridge_NorthMidA", (-315.0, -9.6, 660.0), (1.92, 1.08, 1.84), -0.11, (0.56, 0.30, 0.23, 1.0)),
        ("Stage2_Backdrop_Ridge_NorthMidB", (70.0, -9.4, 675.0), (2.16, 1.24, 2.02), 0.23, (0.64, 0.35, 0.25, 1.0)),
        ("Stage2_Backdrop_Ridge_NorthEast", (485.0, -9.6, 635.0), (2.02, 1.12, 1.92), -0.20, (0.55, 0.29, 0.22, 1.0)),
        ("Stage2_Backdrop_Ridge_SouthWest", (-640.0, -9.4, -630.0), (2.12, 1.20, 2.00), -0.17, (0.61, 0.33, 0.24, 1.0)),
        ("Stage2_Backdrop_Ridge_SouthMidA", (-285.0, -9.6, -680.0), (1.86, 1.04, 1.80), 0.12, (0.52, 0.28, 0.21, 1.0)),
        ("Stage2_Backdrop_Ridge_SouthMidB", (105.0, -9.4, -670.0), (2.18, 1.27, 2.06), -0.25, (0.63, 0.34, 0.24, 1.0)),
        ("Stage2_Backdrop_Ridge_SouthEast", (520.0, -9.5, -625.0), (2.00, 1.10, 1.90), 0.21, (0.56, 0.30, 0.22, 1.0)),
        ("Stage2_Backdrop_Ridge_WestSouth", (-810.0, -9.6, -330.0), (1.88, 1.06, 1.82), 1.34, (0.55, 0.29, 0.22, 1.0)),
        ("Stage2_Backdrop_Ridge_WestMid", (-835.0, -9.5, 25.0), (2.10, 1.21, 1.98), 1.57, (0.64, 0.35, 0.25, 1.0)),
        ("Stage2_Backdrop_Ridge_WestNorth", (-800.0, -9.6, 390.0), (1.84, 1.02, 1.78), 1.78, (0.53, 0.28, 0.21, 1.0)),
        ("Stage2_Backdrop_Ridge_EastSouth", (845.0, -9.5, -360.0), (1.96, 1.12, 1.88), -1.35, (0.58, 0.31, 0.23, 1.0)),
        ("Stage2_Backdrop_Ridge_EastMid", (875.0, -9.6, -5.0), (2.14, 1.24, 2.00), -1.57, (0.65, 0.36, 0.25, 1.0)),
        ("Stage2_Backdrop_Ridge_EastNorth", (830.0, -9.4, 365.0), (1.88, 1.08, 1.82), -1.80, (0.54, 0.29, 0.22, 1.0)),
    )
    for name, position, scale, yaw, tint in backdrop_ridges:
        builder.add_backdrop_ridge(name, position, scale, yaw, tint)

    backdrop_islands = (
        ("Stage2_Backdrop_Island_NorthWest", (-555.0, -8.8, 330.0), (1.75, 1.32, 1.55), 0.18),
        ("Stage2_Backdrop_Island_NorthMidA", (-225.0, -8.9, 395.0), (1.48, 1.16, 1.35), -0.22),
        ("Stage2_Backdrop_Island_NorthMidB", (145.0, -8.8, 375.0), (1.66, 1.28, 1.48), 0.27),
        ("Stage2_Backdrop_Island_NorthEast", (520.0, -8.9, 315.0), (1.55, 1.20, 1.42), -0.17),
        ("Stage2_Backdrop_Island_SouthWest", (-545.0, -8.8, -335.0), (1.62, 1.24, 1.48), -0.20),
        ("Stage2_Backdrop_Island_SouthMidA", (-175.0, -8.9, -405.0), (1.42, 1.12, 1.34), 0.16),
        ("Stage2_Backdrop_Island_SouthMidB", (220.0, -8.8, -385.0), (1.70, 1.30, 1.52), -0.28),
        ("Stage2_Backdrop_Island_SouthEast", (575.0, -8.9, -305.0), (1.50, 1.18, 1.38), 0.21),
        ("Stage2_Backdrop_Island_West", (-690.0, -8.9, 70.0), (1.58, 1.22, 1.42), 1.48),
        ("Stage2_Backdrop_Island_East", (700.0, -8.8, 105.0), (1.74, 1.34, 1.56), -1.52),
    )
    for name, position, scale, yaw in backdrop_islands:
        builder.add_magma_backdrop(
            name, "magma_horizon_island", position, scale, yaw,
            (0.86, 0.50, 0.34, 1.0), 1.48,
        )

    backdrop_spires = (
        ("Stage2_Backdrop_Spire_NorthWest", (-620.0, -9.0, 455.0), (1.50, 1.46, 1.50), 0.22),
        ("Stage2_Backdrop_Spire_NorthMid", (-65.0, -9.1, 485.0), (1.72, 1.66, 1.72), -0.18),
        ("Stage2_Backdrop_Spire_NorthEast", (500.0, -9.0, 430.0), (1.46, 1.42, 1.46), 0.28),
        ("Stage2_Backdrop_Spire_SouthWest", (-570.0, -9.0, -455.0), (1.58, 1.54, 1.58), -0.24),
        ("Stage2_Backdrop_Spire_SouthMid", (55.0, -9.1, -505.0), (1.78, 1.72, 1.78), 0.20),
        ("Stage2_Backdrop_Spire_SouthEast", (610.0, -9.0, -420.0), (1.48, 1.44, 1.48), -0.30),
        ("Stage2_Backdrop_Spire_East", (735.0, -9.0, 155.0), (1.62, 1.58, 1.62), -1.38),
    )
    for name, position, scale, yaw in backdrop_spires:
        builder.add_magma_backdrop(
            name, "magma_horizon_spire", position, scale, yaw,
            (0.72, 0.38, 0.28, 1.0), 1.82,
        )

    # 主経路を大きく蛇行させ、同じ形の広場が一直線に並ばない火山城塞にする。
    builder.add_keep(
        "Stage2_StartFoundation", "magma_fortress_foundation",
        (-430.0, 0.0, -120.0), (90.0, 62.0), 0.12,
    )
    builder.add_stage_entry_gate()

    sinking_grates = (
        ("Stage2_SinkingGrate_01", (-390.0, 0.0, -104.0)),
        ("Stage2_SinkingGrate_02", (-377.0, 1.0, -86.0)),
        ("Stage2_SinkingGrate_03", (-365.0, 2.0, -67.0)),
        ("Stage2_SinkingGrate_04", (-353.0, 3.0, -48.0)),
    )
    for name, position in sinking_grates:
        builder.add_grate(name, position, "SinkingFloor")
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)

    builder.add_keep("Stage2_GatehouseKeep", "fortress_plaza", (-330.0, 4.0, -22.0), (80.0, 58.0), -0.18)
    builder.add_gate("Stage2_GatehouseGate", (-354.0, 4.0, -43.0), (2.0, 1.45, 1.2))

    builder.add_ramp("Stage2_GatehouseRise", (-293.0, 4.0, -5.0), (-276.0, 8.0, 30.0), 16.0)
    builder.add_causeway_between("Stage2_AshCauseway", (-276.0, 8.0, 30.0), (-256.0, 8.0, 62.0), 13.0)
    builder.add_keep("Stage2_AshCourtyard", "magma_fortress_foundation", (-220.0, 8.0, 94.0), (92.0, 62.0), 0.18)

    # スターコイン1: スイッチで入口と4枚の傾斜回転床が同時に起動する時計仕掛け。
    add_magma_pressure_route(builder)

    # 灰の中庭から高所チェックポイントへ、軸の異なる床を曲線状に渡る。
    ascent_grates = (
        ("Stage2_AscentMovingGrate", (-164.0, 9.5, 116.0), "MovingFloor", 0.85, 2, 4.0, (0.0, 0.12, 0.0)),
        ("Stage2_AscentRotatingGrate", (-149.0, 11.5, 129.0), "RotatingFloor", 14.0, 2, 0.0, (0.0, 0.0, 0.08)),
        ("Stage2_AscentSeesawGrate", (-134.0, 13.5, 141.0), "SeesawFloor", 0.0, 0, 0.0, (0.0, -0.12, 0.0)),
    )
    for name, position, gimmick_type, speed, action_mode, move_amount, rotation in ascent_grates:
        builder.add_grate(name, position, gimmick_type, speed, action_mode, move_amount, rotation)
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)
    builder.add_keep("Stage2_CheckpointKeep", "fortress_plaza", (-78.0, 15.0, 158.0), (90.0, 64.0), 0.28)

    builder.add_ramp("Stage2_CheckpointDescent", (-34.0, 15.0, 142.0), (-19.0, 18.0, 116.0), 17.0)
    builder.add_causeway_between("Stage2_ObliqueCauseway", (-19.0, 18.0, 116.0), (7.0, 18.0, 80.0), 13.0)
    builder.add_keep("Stage2_EastJunction", "magma_fortress_foundation", (45.0, 18.0, 55.0), (84.0, 58.0), -0.26)

    # 南へ折り返すマグマ噴出区間。床種と方向を変えて一直線の見え方を避ける。
    hazard_grates = (
        ("Stage2_HazardRotator", (82.0, 18.0, 17.0), "RotatingFloor", 13.0, 0, 0.0, (0.08, 0.15, 0.0)),
        ("Stage2_HazardMovingGrate", (94.0, 18.5, -3.0), "MovingFloor", 0.90, 3, 4.5, (0.0, -0.18, 0.0)),
    )
    for name, position, gimmick_type, speed, action_mode, move_amount, rotation in hazard_grates:
        builder.add_grate(name, position, gimmick_type, speed, action_mode, move_amount, rotation)
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)
    builder.add_ramp("Stage2_HazardRamp", (100.0, 18.5, -12.0), (113.0, 21.0, -45.0), 12.0)
    builder.add_causeway_between("Stage2_HazardCauseway", (113.0, 21.0, -45.0), (126.0, 21.0, -76.0), 12.0)
    for args in (
        ("Stage2_HazardGeyser_01", (87.0, -8.0, 8.0), 19.0, 1.35, 3.00, 0.00, 2.15),
        ("Stage2_HazardGeyser_02", (105.0, -8.0, -28.0), 20.0, 1.20, 2.80, 0.85, 2.00),
        ("Stage2_HazardGeyser_03", (121.0, -8.0, -61.0), 21.0, 1.40, 3.25, 1.45, 2.20),
    ):
        builder.add_geyser(*args)
    builder.add_keep("Stage2_SouthBastion", "fortress_plaza", (140.0, 21.0, -105.0), (94.0, 66.0), 0.14)

    # スターコイン2: ボム能力で二段の壁を破壊しないと宝物庫へ入れない。
    builder.add_causeway_between("Stage2_BombVaultCauseway", (140.0, 21.0, -137.0), (140.0, 21.0, -188.0), 14.0)
    wall_x = (135.2, 138.4, 141.6, 144.8)
    for row, y in enumerate((22.6, 25.8), start=1):
        for column, x in enumerate(wall_x, start=1):
            builder.add_breakable_block(
                f"Stage2_BombVaultWall_{row:02d}_{column:02d}",
                (x, y, -170.0), (3.2, 3.2, 3.2),
            )
    builder.add_keep("Stage2_StarBalcony_02", "fortress_plaza", (140.0, 21.0, -205.0), (44.0, 34.0), 0.08)
    builder.add_star_coin("Stage2_StarCoin_02", (140.0, 25.0, -205.0), 1)

    builder.add_ramp("Stage2_ForgeRise", (183.0, 21.0, -120.0), (222.0, 31.0, -145.0), 18.0)
    builder.add_keep("Stage2_UpperForge", "magma_fortress_foundation", (270.0, 31.0, -165.0), (96.0, 68.0), -0.12)
    builder.add_gate("Stage2_UpperForgeGate", (226.0, 31.0, -146.0), (2.1, 1.55, 1.25))
    builder.add_magma_boss_encounter()

    # 三本の回転ドラムを斜めに連ね、北側の塔へ上がる。
    drum_route = (
        ("Stage2_ForgeDrum_01", (329.0, 35.0, -106.0), -1.08, 24.0),
        ("Stage2_ForgeDrum_02", (346.0, 36.5, -79.0), -1.02, -29.0),
        ("Stage2_ForgeDrum_03", (362.0, 38.0, -52.0), -1.06, 21.0),
    )
    builder.add_ramp("Stage2_ForgeExitRamp", (314.0, 31.0, -139.0), (321.0, 35.0, -119.0), 14.0)
    for name, position, yaw, speed in drum_route:
        drum = builder.add_rotating_drum(name, position, (0.0, yaw, 0.0))
        drum["param"]["speed"] = speed
        builder.add_visual_pillar_pair(name, position, position[1] - 5.20, 5.5, 0.0)
    builder.add_keep("Stage2_NorthTower", "fortress_plaza", (375.0, 39.0, -8.0), (90.0, 64.0), 0.20)

    # スターコイン3: スイッチで北西へ曲がる出現橋を連動させ、噴出タイミングを読む。
    star_bridge_event = 8402
    builder.add_switch("Stage2_StarBridgeSwitch", (354.0, 39.55, 12.0), star_bridge_event)
    receiver_grates = (
        ("Stage2_StarBridgeReceiver_01", (359.0, 39.5, 34.0)),
        ("Stage2_StarBridgeReceiver_02", (346.0, 40.5, 53.0)),
        ("Stage2_StarBridgeReceiver_03", (334.0, 41.5, 74.0)),
        ("Stage2_StarBridgeReceiver_04", (329.0, 42.5, 97.0)),
        ("Stage2_StarBridgeReceiver_05", (335.0, 43.5, 120.0)),
        ("Stage2_StarBridgeReceiver_06", (345.0, 44.5, 143.0)),
    )
    for name, position in receiver_grates:
        builder.add_receiver_grate(name, position, star_bridge_event)
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)
    star_final_floor = (353.0, 45.0, 160.0)
    builder.add_grate("Stage2_StarBridgeMovingFinish", star_final_floor, "MovingFloor", 0.8, 2, 4.0)
    builder.add_visual_pillar_pair("Stage2_StarBridgeMovingFinish", star_final_floor, 44.26)
    for args in (
        ("Stage2_StarBridgeGeyser_01", (340.0, -8.0, 64.0), 42.0, 1.25, 3.10, 0.30, 2.00),
        ("Stage2_StarBridgeGeyser_02", (332.0, -8.0, 108.0), 44.0, 1.40, 3.35, 1.10, 2.10),
        ("Stage2_StarBridgeGeyser_03", (349.0, -8.0, 151.0), 46.0, 1.20, 2.85, 1.75, 2.05),
    ):
        builder.add_geyser(*args)
    builder.add_keep("Stage2_StarBalcony_03", "fortress_plaza", (350.0, 45.0, 183.0), (46.0, 38.0), -0.20)
    builder.add_star_coin("Stage2_StarCoin_03", (350.0, 49.0, 183.0), 2)

    # ゴールへは北東へ折れ、シーソー、横移動床、最後の坂で高さを変える。
    builder.add_causeway_between("Stage2_GoalApproachCauseway", (412.0, 39.0, 10.0), (430.0, 39.0, 39.0), 13.0)
    goal_grates = (
        ("Stage2_GoalSeesaw", (438.0, 39.5, 53.0), "SeesawFloor", 0.0, 0, 0.0, (0.0, -0.18, 0.0)),
        ("Stage2_GoalMovingGrate", (449.0, 41.0, 71.0), "MovingFloor", 0.75, 3, 4.5, (0.0, 0.12, 0.0)),
    )
    for name, position, gimmick_type, speed, action_mode, move_amount, rotation in goal_grates:
        builder.add_grate(name, position, gimmick_type, speed, action_mode, move_amount, rotation)
        builder.add_visual_pillar_pair(name, position, position[1] - 0.74)
    builder.add_ramp("Stage2_GoalRise", (456.0, 41.0, 82.0), (469.0, 45.0, 103.0), 15.0)
    builder.add_keep("Stage2_GoalKeep", "fortress_plaza", (505.0, 45.0, 130.0), (100.0, 70.0), -0.10)
    builder.add_gate("Stage2_GoalGate", (469.0, 45.0, 105.0), (2.2, 1.65, 1.3))
    builder.add_dash_panel("Stage2_GoalDashPanel", (489.0, 45.55, 119.0), 0.58)

    # 敵と砲台は回避空間のある固定床だけに置く。
    builder.add_enemy("Stage2_Start_FireSlime", "Stage2_Start_FireSlime", (-340.0, 6.0, -20.0), 1.7)
    builder.add_enemy("Stage2_Ridge_FireGuard_A", "Stage2_MainCalderaFireGuard", (-232.0, 10.0, 82.0), 1.1)
    builder.add_enemy("Stage2_CalderaBase_ThunderGuard", "Stage2_MainCalderaThunderGuard", (-80.0, 17.0, 160.0), -2.1)
    builder.add_enemy("Stage2_Rim_FireGuard", "Stage2_RimFireGuard", (39.0, 20.0, 50.0), 0.8)
    builder.add_enemy("Stage2_Ridge_BomberGuard", "Stage2_EmberLandingBomber", (118.0, 23.0, -98.0), -1.5)
    builder.add_enemy("Stage2_Rim_BomberGuard", "Stage2_RimBomberGuard", (160.0, 23.0, -112.0), -2.2)
    builder.add_enemy("Stage2_Final_FireGuard_A", "Stage2_FinalFireGuard", (390.0, 41.0, -5.0), 0.9)

    gatehouse_cannon = builder.clone("Stage2_Cannon_EmberLanding_A", "Stage2_EmberLandingCannon")
    set_transform(gatehouse_cannon, (-311.0, 4.8, -10.0), (1.0, 1.0, 1.0), (0.0, 0.35, 0.0))
    gatehouse_cannon["param"]["interval"] = 2.5
    gatehouse_cannon["param"]["detectionRange"] = 40.0

    upper_cannon = builder.clone("Stage2_Cannon_Rim_A", "Stage2_RimCannon")
    set_transform(upper_cannon, (524.0, 45.8, 142.0), (1.0, 1.0, 1.0), (0.0, -2.25, 0.0))
    upper_cannon["param"]["interval"] = 2.8
    upper_cannon["param"]["detectionRange"] = 44.0

    goal = builder.clone("goal", "goal")
    set_transform(goal, (535.0, 48.0, 135.0), (2.8, 2.8, 2.8))

    # 通常コインは曲がり角だけを案内し、スター分岐は入口だけ示して探索性を残す。
    add_coin_curve(builder, "Stage2_StartCoin", (-465.0, 2.0, -120.0), (-395.0, 2.0, -106.0), 7)
    for index, (_, position) in enumerate(sinking_grates, start=1):
        builder.add_coin(f"Stage2_SinkingCoin_{index:02d}", (position[0], position[1] + 2.2, position[2]))
    add_coin_curve(builder, "Stage2_GatehouseCoin", (-318.0, 6.0, -4.0), (-280.0, 10.0, 29.0), 6, 1.2)
    add_coin_curve(builder, "Stage2_AshCoin", (-260.0, 10.0, 59.0), (-182.0, 10.0, 105.0), 7)
    for index, (_, position, _, _, _, _, _) in enumerate(ascent_grates, start=1):
        builder.add_coin(f"Stage2_AscentCoin_{index:02d}", (position[0], position[1] + 2.2, position[2]))
    add_coin_curve(builder, "Stage2_CheckpointCoin", (-116.0, 17.0, 153.0), (-25.0, 20.0, 124.0), 7, 1.0)
    add_coin_curve(builder, "Stage2_JunctionCoin", (4.0, 20.0, 84.0), (75.0, 20.0, 26.0), 7)
    for index, (_, position, _, _, _, _, _) in enumerate(hazard_grates, start=1):
        builder.add_coin(f"Stage2_HazardCoin_{index:02d}", (position[0], position[1] + 2.2, position[2]))
    add_coin_curve(builder, "Stage2_SouthCoin", (105.0, 21.0, -35.0), (177.0, 23.0, -112.0), 8, 1.1)
    add_coin_curve(builder, "Stage2_ForgeCoin", (190.0, 24.0, -124.0), (311.0, 33.0, -141.0), 8, 1.2)
    for index, (_, position, _, _) in enumerate(drum_route, start=1):
        builder.add_coin(f"Stage2_ForgeDrumCoin_{index:02d}", (position[0], position[1] + 4.6, position[2]))
    add_coin_curve(builder, "Stage2_NorthCoin", (370.0, 41.0, -43.0), (425.0, 41.0, 34.0), 7)
    for index, (_, position, _, _, _, _, _) in enumerate(goal_grates, start=1):
        builder.add_coin(f"Stage2_GoalGrateCoin_{index:02d}", (position[0], position[1] + 2.2, position[2]))
    add_coin_curve(builder, "Stage2_GoalCoin", (465.0, 44.0, 98.0), (535.0, 49.5, 135.0), 8, 1.2)
    add_coin_curve(builder, "Stage2_StarHint_02", (140.0, 23.0, -139.0), (140.0, 23.0, -158.0), 3)
    add_coin_curve(builder, "Stage2_StarHint_03", (355.0, 41.0, 14.0), (358.0, 42.0, 31.0), 3, 0.6)

    route_waypoints = [
        (-465.0, 0.0, -120.0), (-395.0, 0.0, -106.0),
        (-390.0, 0.0, -104.0), (-377.0, 1.0, -86.0), (-365.0, 2.0, -67.0),
        (-353.0, 3.0, -48.0), (-330.0, 4.0, -22.0), (-293.0, 4.0, -5.0),
        (-276.0, 8.0, 30.0), (-256.0, 8.0, 62.0), (-220.0, 8.0, 94.0),
        (-174.0, 8.0, 111.0), (-164.0, 9.5, 116.0), (-149.0, 11.5, 129.0),
        (-134.0, 13.5, 141.0), (-78.0, 15.0, 158.0), (-34.0, 15.0, 142.0),
        (-19.0, 18.0, 116.0), (7.0, 18.0, 80.0), (45.0, 18.0, 55.0),
        (82.0, 18.0, 17.0), (94.0, 18.5, -3.0), (100.0, 18.5, -12.0),
        (113.0, 21.0, -45.0), (126.0, 21.0, -76.0), (140.0, 21.0, -105.0),
        (183.0, 21.0, -120.0), (222.0, 31.0, -145.0), (270.0, 31.0, -165.0),
        (314.0, 31.0, -139.0), (321.0, 35.0, -119.0), (329.0, 35.0, -106.0),
        (346.0, 36.5, -79.0), (362.0, 38.0, -52.0), (375.0, 39.0, -8.0),
        (412.0, 39.0, 10.0), (430.0, 39.0, 39.0), (438.0, 39.5, 53.0),
        (449.0, 41.0, 71.0), (456.0, 41.0, 82.0), (469.0, 45.0, 103.0),
        (505.0, 45.0, 130.0), (535.0, 48.0, 135.0),
    ]
    route_length = sum(math.dist(start, end) for start, end in zip(route_waypoints, route_waypoints[1:]))
    if not 1080.0 <= route_length <= 1450.0:
        raise ValueError(f"Stage 2 の主経路長が想定外です: {route_length:.1f} m")

    route_z = [point[2] for point in route_waypoints]
    branch_z = route_z + [205.0, -205.0, 183.0]
    if max(branch_z) - min(branch_z) < 360.0:
        raise ValueError("Stage 2 の横方向利用が不足しています")

    names = [obj.get("name", "") for obj in builder.objects]
    guids = [obj.get("guid", "") for obj in builder.objects]
    if len(names) != len(set(names)):
        raise ValueError("Stage 2 に重複したオブジェクト名があります")
    if len(guids) != len(set(guids)):
        raise ValueError("Stage 2 に重複したGUIDがあります")

    forbidden_gimmicks = {"LaunchStar", "ChainCollapseFloor"}
    present_forbidden = sorted({obj.get("gimmickType", "") for obj in builder.objects} & forbidden_gimmicks)
    if present_forbidden:
        raise ValueError(f"必須経路に禁止ギミックがあります: {', '.join(present_forbidden)}")

    required_gimmicks = {
        "SinkingFloor", "MovingFloor", "RotatingFloor", "SeesawFloor",
        "TimedSwitch", "EventReceiver", "BreakableBlock", "DashPanel", "MagmaGeyser",
        "ArenaEncounter", "PrismBarrier",
    }
    gimmick_names = {obj.get("gimmickType", "") for obj in builder.objects}
    missing_gimmicks = sorted(required_gimmicks - gimmick_names)
    if missing_gimmicks:
        raise ValueError(f"Stage 2 の必須ギミックが不足しています: {', '.join(missing_gimmicks)}")

    star_coins = [obj for obj in builder.objects if obj.get("eventID") == 7]
    if sorted(obj.get("targetID") for obj in star_coins) != [0, 1, 2]:
        raise ValueError("Stage 2 のスターコイン番号は0、1、2を1枚ずつ指定してください")
    star_positions = [obj["position"] for obj in sorted(star_coins, key=lambda obj: obj["targetID"])]
    if max(position[0] for position in star_positions) - min(position[0] for position in star_positions) < 600.0:
        raise ValueError("Stage 2 のスターコインが左右へ十分に分散していません")

    linked_lifts = [obj for obj in builder.objects
                    if obj.get("gimmickType") == "MovingFloor"
                    and obj.get("param", {}).get("actionMode") == 5
                    and obj.get("targetID") in (8311, 8312)]
    bomb_walls = [obj for obj in builder.objects if obj.get("name", "").startswith("Stage2_BombVaultWall_")]
    linked_bridge = [obj for obj in builder.objects if obj.get("myEventID") == star_bridge_event]
    if len(linked_lifts) != 2 or len(bomb_walls) != 8 or len(linked_bridge) < 6:
        raise ValueError("Stage 2 のスターコイン攻略ギミックが不足しています")

    forbidden_route_models = {"Stages/volcanic_island", "Stages/obsidian_raft"}
    present_old_models = sorted({obj.get("modelName", "") for obj in builder.objects} & forbidden_route_models)
    if present_old_models:
        raise ValueError(f"旧火山足場モデルが残っています: {', '.join(present_old_models)}")

    required_models = {
        "Stages/magma_fortress_foundation", "Stages/basalt_causeway",
        "Stages/metal_grate_platform", "Stages/fortress_plaza", "Stages/fortress_gate",
        "Stages/fortress_pillar", "Stages/fortress_ramp", "Stages/fortress_grate_drum",
        "Stages/volcanic_backdrop_ridge", "Stages/magma_horizon_island",
        "Stages/magma_horizon_spire", "Stages/magma_vent",
        "Stages/bomb_break_block", "Gimmicks/star", "Gimmicks/crown_stage_gate",
        "Gimmicks/star_dash_panel", "Stages/stage_select_gate_pad", "Stages/gate",
        "Gimmicks/brazier", "Effects/flame", "Characters/magma_slime",
        "Gimmicks/portal_surface",
    }
    model_names = {obj.get("modelName", "") for obj in builder.objects}
    missing_models = sorted(required_models - model_names)
    if missing_models:
        raise ValueError(f"Stage 2 の必須モデル参照が不足しています: {', '.join(missing_models)}")

    entrance_gate = next(
        (obj for obj in builder.objects if obj.get("name") == "Stage2_EntranceGate"),
        None,
    )
    entrance_param = entrance_gate.get("param", {}) if entrance_gate else {}
    if (
        not entrance_gate
        or entrance_gate.get("gimmickType") != "StageGate"
        or entrance_gate.get("modelName") != "Gimmicks/crown_stage_gate"
        or entrance_param.get("actionMode") != 1
        or entrance_param.get("targetScene") != "SELECT"
    ):
        raise ValueError("Stage 2 の入口ゲート設定が不正です")

    expected_entry_models = {
        "Stage2_EntryDecor_Pad": "Stages/stage_select_gate_pad",
        "Stage2_EntryDecor_Frame": "Stages/gate",
        "Stage2_EntryDecor_Brazier_1": "Gimmicks/brazier",
        "Stage2_EntryDecor_Brazier_2": "Gimmicks/brazier",
        "Stage2_EntryDecor_Flame_1": "Effects/flame",
        "Stage2_EntryDecor_Flame_2": "Effects/flame",
    }
    objects_by_name = {entry.get("name", ""): entry for entry in builder.objects}
    objects_by_name["Stage2_MagmaSea"].setdefault("waterParam", {}).update(
        waveFrequency=1.1, effectIntensity=1.05, effectSoftness=0.72,
        flowSpeedX=0.12, flowSpeedY=0.045)
    for name, model_name in expected_entry_models.items():
        if objects_by_name.get(name, {}).get("modelName") != model_name:
            raise ValueError(f"{name} の入口ゲート外装が不足または不正です")

    magma_encounter = objects_by_name.get("Stage2_MagmaArena_Encounter", {})
    magma_boss = objects_by_name.get("Stage2_MagmaArena_Boss", {})
    magma_barriers = [
        objects_by_name.get(f"Stage2_MagmaBarrier_{side}", {})
        for side in ("West", "East", "North", "South")
    ]
    if (
        magma_encounter.get("gimmickType") != "ArenaEncounter"
        or magma_encounter.get("myEventID") != MAGMA_DEFEAT_EVENT_ID
        or magma_encounter.get("targetID") != MAGMA_BOSS_EVENT_ID
        or magma_encounter.get("param", {}).get("maxCount") != MAGMA_BARRIER_COUNT
        or magma_boss.get("enemyType") != "MagmaSlime"
        or magma_boss.get("myEventID") != MAGMA_BOSS_EVENT_ID
        or magma_boss.get("targetID") != MAGMA_DEFEAT_EVENT_ID
        or magma_boss.get("param", {}).get("actionMode") != 1
        or magma_boss.get("param", {}).get("startActive") is not False
        or any(barrier.get("gimmickType") != "PrismBarrier" for barrier in magma_barriers)
    ):
        raise ValueError("Stage 2 のマグマ中ボス遭遇リンクが不正です")

    magma_sea = objects_by_name.get("Stage2_MagmaSea", {})
    magma_scale = magma_sea.get("scale", [0.0, 0.0, 0.0])
    magma_param = magma_sea.get("waterParam", {})
    if (
        magma_sea.get("materialType") != 9
        or len(magma_scale) != 3
        or magma_scale[0] < 850.0
        or magma_scale[2] < 650.0
        or magma_param.get("waveFrequency", 0.0) < 0.5
        or magma_param.get("effectIntensity", 0.0) < 0.5
    ):
        raise ValueError("Stage 2 のマグマ海の範囲またはシェーダー設定が不足しています")

    backdrop_objects = [
        obj for obj in builder.objects
        if obj.get("name", "").startswith("Stage2_Backdrop_")
    ]
    backdrop_counts = {
        "ridge": sum(obj.get("name", "").startswith("Stage2_Backdrop_Ridge_") for obj in backdrop_objects),
        "island": sum(obj.get("name", "").startswith("Stage2_Backdrop_Island_") for obj in backdrop_objects),
        "spire": sum(obj.get("name", "").startswith("Stage2_Backdrop_Spire_") for obj in backdrop_objects),
    }
    if backdrop_counts != {"ridge": 14, "island": 10, "spire": 7}:
        raise ValueError(f"Stage 2 の三層遠景数が不正です: {backdrop_counts}")
    if any(
        obj.get("collider", {}).get("type") != 0
        or obj.get("collisionAttribute") != 0
        or obj.get("collisionMask") != 0
        or obj.get("castShadow") is not False
        for obj in backdrop_objects
    ):
        raise ValueError("Stage 2 の遠景に当たり判定または影が設定されています")

    from .low_magma_layout import lower_stage2_layout
    lower_stage2_layout(builder.objects)
    from .escape_layout import add_escape_layout
    add_escape_layout(builder.objects)
    from .ferry_layout import add_ferry_layout
    add_ferry_layout(builder.objects)
    scene["objects"] = builder.objects
    SCENE_PATH.write_text(
        json.dumps(scene, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def update_player_and_camera() -> None:
    player_json = json.loads(PLAYER_PATH.read_text(encoding="utf-8-sig"))
    player = player_json["objects"][0]
    set_transform(player, (-465.0, -2.8, -120.0), (1.0, 1.0, 1.0), (0.0, math.pi * 0.5, 0.0))
    PLAYER_PATH.write_text(
        json.dumps(player_json, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    camera_json = json.loads(CAMERA_PATH.read_text(encoding="utf-8-sig"))
    camera = camera_json["objects"][0]
    set_transform(camera, (-486.0, 11.0, -142.0), (1.0, 1.0, 1.0), (-0.22, 0.82, 0.0))
    CAMERA_PATH.write_text(
        json.dumps(camera_json, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def validate_generated_models(names: Iterable[str]) -> None:
    for name in names:
        obj_path = MODEL_ROOT / name / f"{name}.obj"
        lines = obj_path.read_text(encoding="utf-8").splitlines()
        vertex_count = sum(line.startswith("v ") for line in lines)
        uv_count = sum(line.startswith("vt ") for line in lines)
        normal_count = sum(line.startswith("vn ") for line in lines)
        face_count = sum(line.startswith("f ") for line in lines)
        if min(vertex_count, uv_count, normal_count, face_count) <= 0:
            raise ValueError(f"生成モデルの属性が不足しています: {name}")
        if not all("/" in line for line in lines if line.startswith("f ")):
            raise ValueError(f"UV/法線参照のない面があります: {name}")
