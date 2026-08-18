"""CG2モデルをBlenderの共有Collectionとして読み込み、配置シンボルへ表示する。"""

import hashlib
import json
import os

import bpy
from bpy.props import BoolProperty

from .schema import find_project_root, resolve_model_path


MODEL_CACHE_TAG = "cg2_model_cache"
MODEL_PROTOTYPE_TAG = "cg2_model_prototype"
_MANAGED_MODEL_CACHE = {}


def _read_source_data(obj):
    serialized = obj.get("cg2_source_json")
    if not isinstance(serialized, str) or not serialized:
        return {}
    try:
        data = json.loads(serialized)
        return data if isinstance(data, dict) else {}
    except json.JSONDecodeError:
        return {}


def _load_managed_model_names(project_root):
    settings_path = os.path.join(project_root, "Resources", "json", "gameplay", "status_settings.json")
    try:
        settings_mtime = os.path.getmtime(settings_path)
    except OSError:
        settings_mtime = -1.0
    cache_key = os.path.normcase(os.path.abspath(project_root))
    cached = _MANAGED_MODEL_CACHE.get(cache_key)
    if cached is not None and cached[0] == settings_mtime:
        return cached[1]

    result = {
        "Player": "Characters/slime",
        "Enemy": {
            "Slime": "Characters/slime_pink",
            "BossCore": "Stages/block",
            "Bomb": "Gimmicks/blob",
            "Bomber": "Characters/slime_black",
            "Mushroom": "Primitives/cylinder",
            "FireSlime": "Characters/slime_red",
            "ThunderSlime": "Characters/slime_yellow",
            "WindSlime": "Characters/slime_wind",
            "GiantSlime": "Characters/slime_pink",
            "PrismSlime": "Characters/prism_slime",
            "Bat": "Characters/bat",
            "BeamDrone": "Characters/eye",
        },
    }
    try:
        with open(settings_path, "r", encoding="utf-8-sig") as stream:
            settings = json.load(stream)
        player = settings.get("player", {})
        if isinstance(player.get("modelName"), str) and player["modelName"]:
            result["Player"] = player["modelName"]
        enemies = settings.get("enemies", {})
        if isinstance(enemies, dict):
            for enemy_type, status in enemies.items():
                if isinstance(status, dict) and isinstance(status.get("modelName"), str) and status["modelName"]:
                    result["Enemy"][enemy_type] = status["modelName"]
    except (OSError, ValueError, json.JSONDecodeError):
        pass
    _MANAGED_MODEL_CACHE[cache_key] = (settings_mtime, result)
    return result


def effective_model_name(obj, project_root):
    explicit_name = obj.get("file_name")
    if isinstance(explicit_name, str) and explicit_name:
        return explicit_name

    source = _read_source_data(obj)
    source_name = source.get("modelName")
    if isinstance(source_name, str) and source_name:
        return source_name

    object_type = str(obj.get("cg2_type", source.get("type", "Model")))
    managed_models = _load_managed_model_names(project_root)
    if object_type == "Player":
        return managed_models["Player"]
    if object_type == "Enemy":
        enemy_type = obj.get("enemy_type", source.get("enemyType", "Slime"))
        return managed_models["Enemy"].get(str(enemy_type), "Primitives/cube")
    return ""


def _cache_collection_name(model_path):
    digest = hashlib.sha1(os.path.normcase(model_path).encode("utf-8")).hexdigest()[:10]
    stem = os.path.splitext(os.path.basename(model_path))[0]
    return f"CG2_Model_{stem}_{digest}"


def _find_cached_collection(model_path):
    normalized_path = os.path.normcase(os.path.abspath(model_path))
    for collection in bpy.data.collections:
        cached_path = collection.get("cg2_model_path")
        if collection.get(MODEL_CACHE_TAG) is True and isinstance(cached_path, str):
            if os.path.normcase(os.path.abspath(cached_path)) == normalized_path:
                return collection
    return None


def clear_model_cache():
    cached_collections = [
        collection
        for collection in bpy.data.collections
        if collection.get(MODEL_CACHE_TAG) is True
    ]
    for collection in cached_collections:
        for obj in list(collection.objects):
            if obj.get(MODEL_PROTOTYPE_TAG) is True:
                bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(collection)


def _import_model_file(model_path):
    extension = os.path.splitext(model_path)[1].lower()
    if extension == ".obj":
        if hasattr(bpy.ops.wm, "obj_import"):
            return bpy.ops.wm.obj_import(filepath=model_path)
        return bpy.ops.import_scene.obj(filepath=model_path, axis_forward="-Z", axis_up="Y")
    if extension in {".gltf", ".glb"}:
        return bpy.ops.import_scene.gltf(filepath=model_path)
    return {"CANCELLED"}


def load_model_collection(model_name, project_root):
    model_path = resolve_model_path(model_name, project_root)
    if not model_path:
        return None, f"モデルが見つかりません: {model_name}"

    cached = _find_cached_collection(model_path)
    if cached is not None:
        return cached, ""

    prototype_collection = bpy.data.collections.new(_cache_collection_name(model_path))
    prototype_collection[MODEL_CACHE_TAG] = True
    prototype_collection["cg2_model_name"] = model_name
    prototype_collection["cg2_model_path"] = model_path

    before_objects = set(bpy.data.objects)
    before_collections = set(bpy.data.collections)
    bpy.ops.object.select_all(action="DESELECT")
    try:
        result = _import_model_file(model_path)
    except (RuntimeError, TypeError) as error:
        bpy.data.collections.remove(prototype_collection)
        return None, f"モデル読込失敗: {model_name} ({error})"

    imported_objects = [obj for obj in bpy.data.objects if obj not in before_objects]
    if result != {"FINISHED"} or not imported_objects:
        for obj in imported_objects:
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(prototype_collection)
        return None, f"モデル読込失敗: {model_name}"

    prototype_prefix = prototype_collection.name
    for index, imported in enumerate(imported_objects):
        imported.name = f"__{prototype_prefix}_{index:03d}_{imported.name}"
        imported[MODEL_PROTOTYPE_TAG] = True
        prototype_collection.objects.link(imported)
        for collection in list(imported.users_collection):
            if collection is not prototype_collection:
                collection.objects.unlink(imported)

    created_collections = [
        collection
        for collection in bpy.data.collections
        if collection not in before_collections and collection is not prototype_collection
    ]
    for collection in reversed(created_collections):
        bpy.data.collections.remove(collection)

    bpy.ops.object.select_all(action="DESELECT")
    return prototype_collection, ""


def attach_model_preview(obj, model_name, project_root):
    obj.instance_type = "NONE"
    obj.instance_collection = None
    obj.pop("cg2_model_path", None)
    obj.pop("cg2_model_error", None)

    if not model_name:
        return False, ""

    collection, error = load_model_collection(model_name, project_root)
    if collection is None:
        obj["cg2_model_error"] = error
        return False, error

    obj.instance_type = "COLLECTION"
    obj.instance_collection = collection
    obj["cg2_effective_model_name"] = model_name
    obj["cg2_model_path"] = collection.get("cg2_model_path", "")
    obj.empty_display_size = 0.35
    return True, ""


class MYADDON_OT_refresh_model_previews(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_refresh_model_previews"
    bl_label = "実モデル表示を更新"
    bl_description = "選択したCG2オブジェクトのModel Nameを再解決して実モデル表示を更新します"
    bl_options = {"REGISTER", "UNDO"}

    selected_only: BoolProperty(
        name="選択中のみ",
        default=True,
    )

    def execute(self, context):
        project_root = context.scene.get("cg2_project_root") or find_project_root(
            context.scene.get("cg2_import_base", "")
        )
        if not project_root:
            self.report({"ERROR"}, "Resources/3DModelを含むCG2プロジェクトを特定できません")
            return {"CANCELLED"}

        candidates = context.selected_objects if self.selected_only else context.scene.objects
        targets = [obj for obj in candidates if obj.get("cg2_imported") is True or "cg2_type" in obj]
        loaded_count = 0
        errors = []
        for obj in targets:
            model_name = effective_model_name(obj, project_root)
            loaded, error = attach_model_preview(obj, model_name, project_root)
            if loaded:
                loaded_count += 1
            elif error:
                errors.append(error)

        if errors:
            self.report({"WARNING"}, f"{loaded_count}個を更新、{len(errors)}個のモデルが見つかりません")
        else:
            self.report({"INFO"}, f"{loaded_count}個の実モデル表示を更新しました")
        return {"FINISHED"}
