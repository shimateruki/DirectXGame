"""CG2のレベルJSONとBlender座標系を相互変換する共通処理。"""

from __future__ import annotations

import copy
import json
import os


SPLIT_CATEGORIES = ("player", "enemy", "object", "camera")
SPLIT_SUFFIXES = tuple(f"_{category}" for category in SPLIT_CATEGORIES)
MODEL_EXTENSIONS = (".obj", ".gltf", ".glb")


def _vector3(value, fallback):
    try:
        if value is not None and len(value) >= 3:
            return [float(value[0]), float(value[1]), float(value[2])]
    except (TypeError, ValueError, IndexError):
        pass
    return list(fallback)


def blender_to_engine_position(value):
    x, y, z = _vector3(value, (0.0, 0.0, 0.0))
    return [x, z, y]


def engine_to_blender_position(value):
    x, y, z = _vector3(value, (0.0, 0.0, 0.0))
    return [x, z, y]


def blender_to_engine_rotation(value):
    x, y, z = _vector3(value, (0.0, 0.0, 0.0))
    return [-x, -z, -y]


def engine_to_blender_rotation(value):
    x, y, z = _vector3(value, (0.0, 0.0, 0.0))
    return [-x, -z, -y]


def blender_to_engine_scale(value):
    x, y, z = _vector3(value, (1.0, 1.0, 1.0))
    return [x, z, y]


def engine_to_blender_scale(value):
    x, y, z = _vector3(value, (1.0, 1.0, 1.0))
    return [x, z, y]


def normalize_object(source, inherited_parent_name=""):
    """旧Blender形式を、現行LevelLoader形式の1オブジェクトへ変換する。"""

    data = copy.deepcopy(source) if isinstance(source, dict) else {}
    children = data.pop("children", [])
    legacy_transform = data.pop("transform", None)

    if isinstance(legacy_transform, dict):
        if "position" not in data and "translation" in legacy_transform:
            data["position"] = blender_to_engine_position(legacy_transform["translation"])
        if "rotation" not in data and "rotation" in legacy_transform:
            data["rotation"] = blender_to_engine_rotation(legacy_transform["rotation"])
        if "scale" not in data and "scaling" in legacy_transform:
            data["scale"] = blender_to_engine_scale(legacy_transform["scaling"])

        collider = data.get("collider")
        if isinstance(collider, dict):
            if "center" in collider:
                collider["center"] = blender_to_engine_position(collider["center"])
            if "rotation" in collider:
                collider["rotation"] = blender_to_engine_rotation(collider["rotation"])
            if "size" in collider:
                collider["size"] = blender_to_engine_scale(collider["size"])

    if "modelName" not in data and isinstance(data.get("file_name"), str):
        data["modelName"] = data["file_name"]
    data.pop("file_name", None)

    spawn_enabled = data.get("spawn") is True
    enemy_enabled = data.get("enemy") is True
    object_type = data.get("type", "Model")

    if spawn_enabled:
        data["type"] = "Player"
        data["saveCategory"] = "Player"
    elif enemy_enabled:
        data["type"] = "Enemy"
        data["saveCategory"] = "Enemy"
        data["enemyType"] = data.get("enemyType") or data.get("enemy_type") or "Slime"
    elif object_type in {"MESH", "EMPTY", "CURVE", "SURFACE", "META", "FONT"}:
        data["type"] = "Model"
        data.setdefault("saveCategory", "Object")

    data.pop("spawn", None)
    data.pop("enemy", None)
    data.pop("enemy_type", None)

    if inherited_parent_name and not data.get("parentName"):
        data["parentName"] = inherited_parent_name

    data.setdefault("position", [0.0, 0.0, 0.0])
    data.setdefault("rotation", [0.0, 0.0, 0.0])
    data.setdefault("scale", [1.0, 1.0, 1.0])
    return data, children


def flatten_objects(objects, inherited_parent_name=""):
    flattened = []
    if not isinstance(objects, list):
        return flattened

    for source in objects:
        if not isinstance(source, dict):
            continue
        normalized, children = normalize_object(source, inherited_parent_name)
        flattened.append(normalized)
        flattened.extend(flatten_objects(children, normalized.get("name", "")))
    return flattened


def split_base_path(filepath):
    absolute_path = os.path.abspath(filepath)
    root, extension = os.path.splitext(absolute_path)
    if extension.lower() != ".json":
        root = absolute_path

    for suffix in SPLIT_SUFFIXES:
        if root.lower().endswith(suffix):
            root = root[: -len(suffix)]
            break
    return root


def find_project_root(source_path=""):
    seeds = [source_path, __file__]
    for seed in seeds:
        if not seed:
            continue
        current = os.path.abspath(seed)
        if os.path.isfile(current) or os.path.splitext(current)[1]:
            current = os.path.dirname(current)

        while True:
            model_root = os.path.join(current, "Resources", "3DModel")
            if os.path.isdir(model_root):
                return current
            parent = os.path.dirname(current)
            if parent == current:
                break
            current = parent
    return ""


def resolve_model_path(model_name, project_root):
    """ModelManager::ResolveModelPathと同じ規則で実モデルを探す。"""

    if not isinstance(model_name, str) or not model_name.strip() or not project_root:
        return ""

    normalized_name = model_name.strip().replace("\\", "/")
    resource_prefix = "Resources/3DModel/"
    if normalized_name.lower().startswith(resource_prefix.lower()):
        normalized_name = normalized_name[len(resource_prefix):]

    if os.path.isabs(normalized_name) and os.path.isfile(normalized_name):
        return os.path.abspath(normalized_name)

    relative_path = normalized_name.replace("/", os.sep)
    parent_path = os.path.dirname(relative_path)
    filename = os.path.basename(relative_path)
    stem, extension = os.path.splitext(filename)
    model_root = os.path.join(project_root, "Resources", "3DModel")
    legacy_directory = os.path.join(model_root, parent_path, stem)
    direct_directory = os.path.join(model_root, parent_path)

    if extension:
        for candidate in (
            os.path.join(legacy_directory, filename),
            os.path.join(direct_directory, filename),
        ):
            if os.path.isfile(candidate):
                return os.path.abspath(candidate)
        return ""

    for model_extension in MODEL_EXTENSIONS:
        candidate = os.path.join(legacy_directory, f"{stem}{model_extension}")
        if os.path.isfile(candidate):
            return os.path.abspath(candidate)

    if os.path.isdir(legacy_directory):
        candidates = sorted(
            os.path.join(legacy_directory, entry)
            for entry in os.listdir(legacy_directory)
            if os.path.splitext(entry)[1].lower() in MODEL_EXTENSIONS
            and os.path.isfile(os.path.join(legacy_directory, entry))
        )
        if candidates:
            return os.path.abspath(candidates[0])
    return ""


def discover_level_paths(filepath):
    """LevelLoaderと同じく、分割ファイルが1つでもあれば分割側を優先する。"""

    base_path = split_base_path(filepath)
    split_paths = [
        f"{base_path}_{category}.json"
        for category in SPLIT_CATEGORIES
        if os.path.isfile(f"{base_path}_{category}.json")
    ]
    if split_paths:
        return split_paths

    single_path = f"{base_path}.json"
    if os.path.isfile(filepath):
        return [os.path.abspath(filepath)]
    if os.path.isfile(single_path):
        return [single_path]
    raise FileNotFoundError(filepath)


def load_level_objects(filepath):
    objects = []
    loaded_paths = discover_level_paths(filepath)
    for path in loaded_paths:
        with open(path, "r", encoding="utf-8-sig") as stream:
            document = json.load(stream)
        objects.extend(flatten_objects(document.get("objects", [])))
    return objects, loaded_paths


def category_for_object(data):
    object_type = str(data.get("type", "Model"))
    save_category = str(data.get("saveCategory", ""))
    if object_type == "Player" or save_category == "Player":
        return "player"
    if object_type in {"Enemy", "Spawner"} or save_category == "Enemy":
        return "enemy"
    if object_type in {"Camera", "CinematicCamera"} or save_category == "Camera":
        return "camera"
    return "object"


def write_json_atomic(filepath, document):
    directory = os.path.dirname(os.path.abspath(filepath))
    if directory:
        os.makedirs(directory, exist_ok=True)
    temporary_path = f"{filepath}.tmp"
    with open(temporary_path, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(document, stream, indent=4, ensure_ascii=False)
        stream.write("\n")
    os.replace(temporary_path, filepath)
