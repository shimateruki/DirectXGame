"""Blenderの編集内容を現行CG2 LevelLoader形式で書き出す。"""

import json
import uuid

import bpy
import bpy_extras
from bpy.props import BoolProperty, StringProperty

from .schema import (
    SPLIT_CATEGORIES,
    blender_to_engine_position,
    blender_to_engine_rotation,
    blender_to_engine_scale,
    category_for_object,
    split_base_path,
    write_json_atomic,
)


def _source_data(obj):
    serialized = obj.get("cg2_source_json")
    if isinstance(serialized, str) and serialized:
        try:
            source = json.loads(serialized)
            if isinstance(source, dict):
                return source
        except json.JSONDecodeError:
            pass
    return {}


def _is_enabled_flag(obj, key):
    return key in obj and obj.get(key) is True


def _object_type(obj, source):
    if _is_enabled_flag(obj, "spawn"):
        return "Player"

    custom_type = obj.get("cg2_type")
    if isinstance(custom_type, str) and custom_type:
        return custom_type

    if _is_enabled_flag(obj, "enemy"):
        return "Enemy"
    if obj.type == "CAMERA":
        return "Camera"
    return str(source.get("type", "Model")) if source.get("type") else "Model"


def _ensure_unique_guids(objects):
    used_guids = set()
    for obj in objects:
        guid = obj.get("cg2_guid")
        if not isinstance(guid, str) or not guid or guid in used_guids:
            guid = str(uuid.uuid4())
            obj["cg2_guid"] = guid
        used_guids.add(guid)


def _export_object(obj):
    data = _source_data(obj)
    data.pop("children", None)
    data.pop("transform", None)
    data.pop("quaternion", None)
    data.pop("spawn", None)
    data.pop("enemy", None)
    data.pop("enemy_type", None)
    data.pop("file_name", None)

    object_type = _object_type(obj, data)
    data["name"] = obj.name
    data["type"] = object_type

    save_category = obj.get("cg2_save_category")
    if object_type == "Player":
        data["saveCategory"] = "Player"
    elif object_type in {"Enemy", "Spawner"}:
        data["saveCategory"] = "Enemy"
    elif object_type in {"Camera", "CinematicCamera"}:
        data["saveCategory"] = "Camera"
    elif isinstance(save_category, str) and save_category:
        data["saveCategory"] = save_category
    else:
        data["saveCategory"] = "Object"

    translation, rotation_quaternion, scaling = obj.matrix_local.decompose()
    rotation = rotation_quaternion.to_euler("XYZ")
    data["position"] = blender_to_engine_position(translation)
    data["rotation"] = blender_to_engine_rotation(rotation)
    data["scale"] = blender_to_engine_scale(scaling)

    model_name = obj.get("file_name")
    if isinstance(model_name, str) and model_name:
        data["modelName"] = model_name
    elif object_type == "Model" and "modelName" not in data:
        data["modelName"] = ""

    if object_type == "Enemy":
        enemy_type = obj.get("enemy_type")
        data["enemyType"] = enemy_type if isinstance(enemy_type, str) and enemy_type else data.get("enemyType", "Slime")
        if not data["enemyType"]:
            data["enemyType"] = "Slime"

    if obj.parent is not None:
        data["parentName"] = obj.parent.name
        parent_guid = obj.parent.get("cg2_guid")
        if isinstance(parent_guid, str) and parent_guid:
            data["parentGuid"] = parent_guid
        else:
            data.pop("parentGuid", None)
    else:
        data["parentName"] = ""
        data["parentGuid"] = ""

    guid = obj.get("cg2_guid")
    if isinstance(guid, str) and guid:
        data["guid"] = guid

    if "collider" in obj:
        collider_types = {"NONE": 0, "SPHERE": 1, "BOX": 2, "AABB": 2, "OBB": 3, "CYLINDER": 4}
        collider_type = collider_types.get(str(obj.get("collider", "BOX")).upper(), 2)
        data["collider"] = {
            "type": collider_type,
            "center": blender_to_engine_position(obj.get("collider_center", (0.0, 0.0, 0.0))),
            "size": blender_to_engine_scale(obj.get("collider_size", (1.0, 1.0, 1.0))),
            "rotation": blender_to_engine_rotation(obj.get("collider_rotation", (0.0, 0.0, 0.0))),
        }

    return data


class MYADDON_OT_export_scene(bpy.types.Operator, bpy_extras.io_utils.ExportHelper):
    bl_idname = "myaddon.myaddon_ot_export_scene"
    bl_label = "CG2シーン出力 (JSON)"
    bl_description = "現行LevelLoaderが読み込めるJSON形式でシーンを出力します"
    bl_options = {"REGISTER"}

    filename_ext = ".json"
    filter_glob: StringProperty(default="*.json", options={"HIDDEN"})
    split_files: BoolProperty(
        name="Player／Enemy／Object／Cameraに分割",
        description="LevelLoaderの分割ファイル規則に合わせて4ファイルを出力します",
        default=True,
    )

    def execute(self, context):
        exported_objects = []
        missing_models = []
        scene_objects = [obj for obj in context.scene.objects if obj.get("disabled") is not True]
        _ensure_unique_guids(scene_objects)
        for obj in scene_objects:
            data = _export_object(obj)
            exported_objects.append(data)
            if data.get("type") == "Model" and not data.get("modelName"):
                missing_models.append(obj.name)

        try:
            if self.split_files:
                base_path = split_base_path(self.filepath)
                categorized = {category: [] for category in SPLIT_CATEGORIES}
                for data in exported_objects:
                    categorized[category_for_object(data)].append(data)
                for category in SPLIT_CATEGORIES:
                    write_json_atomic(
                        f"{base_path}_{category}.json",
                        {"name": "scene", "objects": categorized[category]},
                    )
                context.scene["cg2_import_base"] = base_path
                message = f"CG2分割JSONへ{len(exported_objects)}個を出力しました"
            else:
                write_json_atomic(
                    self.filepath,
                    {"name": "scene", "objects": exported_objects},
                )
                message = f"CG2 JSONへ{len(exported_objects)}個を出力しました"
        except OSError as error:
            self.report({"ERROR"}, f"JSONを書き込めませんでした: {error}")
            return {"CANCELLED"}

        if missing_models:
            self.report({"WARNING"}, f"出力完了。ただしmodelName未設定が{len(missing_models)}個あります")
        else:
            self.report({"INFO"}, message)
        return {"FINISHED"}
