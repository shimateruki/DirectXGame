"""現行CG2レベルJSONをBlenderの編集用シンボルへ読み込む。"""

import json

import bpy
import bpy_extras
from bpy.props import BoolProperty, StringProperty

from .model_preview import (
    attach_model_preview,
    clear_model_cache,
    effective_model_name,
)
from .schema import (
    engine_to_blender_position,
    engine_to_blender_rotation,
    engine_to_blender_scale,
    find_project_root,
    load_level_objects,
    split_base_path,
)


COLLECTION_NAME = "CG2_Level"


def _set_marker_style(obj, object_type):
    obj.show_in_front = True
    obj.empty_display_size = 1.0

    if object_type == "Player":
        obj.empty_display_type = "ARROWS"
        obj.color = (0.15, 1.0, 0.2, 1.0)
        obj["spawn"] = True
    elif object_type == "Enemy":
        obj.empty_display_type = "SPHERE"
        obj.color = (1.0, 0.12, 0.12, 1.0)
        obj["enemy"] = True
    elif object_type == "Spawner":
        obj.empty_display_type = "CIRCLE"
        obj.color = (1.0, 0.45, 0.05, 1.0)
    elif object_type in {"Camera", "CinematicCamera"}:
        obj.empty_display_type = "CONE"
        obj.color = (0.2, 0.65, 1.0, 1.0)
    else:
        obj.empty_display_type = "CUBE"
        obj.color = (0.85, 0.85, 0.85, 1.0)


def _get_or_create_collection(scene):
    collection = bpy.data.collections.get(COLLECTION_NAME)
    if collection is None:
        collection = bpy.data.collections.new(COLLECTION_NAME)
        scene.collection.children.link(collection)
    return collection


class MYADDON_OT_import_scene(bpy.types.Operator, bpy_extras.io_utils.ImportHelper):
    bl_idname = "myaddon.myaddon_ot_import_scene"
    bl_label = "CG2シーン読込 (JSON)"
    bl_description = "CG2の単一または分割レベルJSONを編集用シンボルとして読み込みます"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".json"
    filter_glob: StringProperty(default="*.json", options={"HIDDEN"})
    replace_imported: BoolProperty(
        name="以前読み込んだシンボルを置換",
        description="CG2レベルエディタが前回作成したシンボルだけを削除してから読み込みます",
        default=True,
    )
    load_models: BoolProperty(
        name="実モデルを表示",
        description="Resources/3DModelから実モデルを読み込み、共有インスタンスとして表示します",
        default=True,
    )
    rebuild_model_cache: BoolProperty(
        name="モデルキャッシュを再構築",
        description="Blender内に読み込み済みのCG2モデルを破棄し、ファイルから読み直します",
        default=False,
    )

    def execute(self, context):
        try:
            source_objects, loaded_paths = load_level_objects(self.filepath)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            self.report({"ERROR"}, f"JSONを読み込めませんでした: {error}")
            return {"CANCELLED"}

        if self.replace_imported:
            imported_objects = [obj for obj in bpy.data.objects if obj.get("cg2_imported") is True]
            for obj in imported_objects:
                bpy.data.objects.remove(obj, do_unlink=True)
        if self.rebuild_model_cache:
            clear_model_cache()

        collection = _get_or_create_collection(context.scene)
        project_root = find_project_root(loaded_paths[0] if loaded_paths else self.filepath)
        created = []
        objects_by_guid = {}
        objects_by_name = {}

        for index, data in enumerate(source_objects):
            name = str(data.get("name") or f"CG2_Object_{index:03d}")
            obj = bpy.data.objects.new(name, None)
            collection.objects.link(obj)

            object_type = str(data.get("type", "Model"))
            _set_marker_style(obj, object_type)
            obj["cg2_imported"] = True
            obj["cg2_type"] = object_type
            obj["cg2_save_category"] = str(data.get("saveCategory", ""))
            obj["cg2_source_json"] = json.dumps(data, ensure_ascii=False, separators=(",", ":"))

            guid = data.get("guid")
            if isinstance(guid, str) and guid:
                obj["cg2_guid"] = guid
                objects_by_guid[guid] = obj

            model_name = data.get("modelName")
            if isinstance(model_name, str) and model_name:
                obj["file_name"] = model_name

            enemy_type = data.get("enemyType")
            if object_type == "Enemy":
                obj["enemy_type"] = enemy_type if isinstance(enemy_type, str) and enemy_type else "Slime"

            obj.location = engine_to_blender_position(data.get("position"))
            obj.rotation_mode = "XYZ"
            obj.rotation_euler = engine_to_blender_rotation(data.get("rotation"))
            obj.scale = engine_to_blender_scale(data.get("scale"))

            collider = data.get("collider")
            if isinstance(collider, dict) and int(collider.get("type", 0)) != 0:
                collider_names = {1: "SPHERE", 2: "BOX", 3: "OBB", 4: "CYLINDER"}
                obj["collider"] = collider_names.get(int(collider.get("type", 2)), "BOX")
                obj["collider_center"] = engine_to_blender_position(collider.get("center"))
                obj["collider_size"] = engine_to_blender_scale(collider.get("size"))
                obj["collider_rotation"] = engine_to_blender_rotation(collider.get("rotation"))

            created.append((obj, data))
            objects_by_name.setdefault(name, obj)

        for obj, data in created:
            parent = None
            parent_guid = data.get("parentGuid")
            parent_name = data.get("parentName")
            if isinstance(parent_guid, str) and parent_guid:
                parent = objects_by_guid.get(parent_guid)
            if parent is None and isinstance(parent_name, str) and parent_name:
                parent = objects_by_name.get(parent_name)
            if parent is not None and parent is not obj:
                obj.parent = parent

        context.scene["cg2_import_base"] = split_base_path(self.filepath)
        context.scene["cg2_import_files"] = "\n".join(loaded_paths)
        if project_root:
            context.scene["cg2_project_root"] = project_root

        loaded_model_count = 0
        model_errors = []
        if self.load_models and project_root:
            for obj, _ in created:
                model_name = effective_model_name(obj, project_root)
                loaded, error = attach_model_preview(obj, model_name, project_root)
                if loaded:
                    loaded_model_count += 1
                elif error:
                    model_errors.append(error)

        if self.load_models and not project_root:
            self.report({"WARNING"}, "JSONは読み込みましたが、Resources/3DModelの場所を特定できませんでした")
        elif model_errors:
            self.report(
                {"WARNING"},
                f"{len(created)}個を読込、{loaded_model_count}個を実モデル表示、{len(model_errors)}個はモデル未解決です",
            )
        else:
            self.report({"INFO"}, f"{len(created)}個を読込、{loaded_model_count}個を実モデル表示しました")
        return {"FINISHED"}
