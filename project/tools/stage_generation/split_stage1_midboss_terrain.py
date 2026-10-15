"""ステージ1中ボス周辺の一体化した地形を、エディターで動かせる区画に分ける。

既存の配置値を入力として使い、--validate は書き込みを行わない。
再実行でエディター調整を消さないよう、作成済みなら処理を中止する。
"""

import copy
import json
import sys
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODEL_DIR = ROOT / "Resources/3DModel/Stages/ring_star_court"
SCENE_PATH = ROOT / "Resources/json/3Dobject/stage1_object.json"
PREFIX = "Stage1_MidbossTerrain_"
REGIONS = (
    ("Approach", 194.0, -6.0),
    ("Checkpoint", 245.0, -10.0),
    ("EntryBridge", 280.0, -12.0),
    ("Arena", 315.0, -20.0),
    ("ExitBridge", 352.0, -20.0),
    ("DashStart", 380.0, -20.0),
)
SOURCE = {
    "Soil": "Stage1_Meadow_Course_soil",
    "Turf": "Stage1_Meadow_Course_turf",
    "Path": "Stage1_Meadow_Course_path",
    "Remaining": "Stage1_Course_V4",
}
CHILDREN = {
    "Approach": ("Stage1_Collision_V4_BossApproachGarden", "Stage1_Meadow_Grass_09", "Stage1_Meadow_Tree_09_0", "Stage1_Meadow_Rocks_09_0"),
    "Checkpoint": ("Stage1_Collision_V4_PreBossCheckpoint", "Stage1_Meadow_Grass_10", "Stage1_Meadow_Tree_10_1", "Stage1_Meadow_Rocks_10_1"),
    "EntryBridge": ("Stage1_Collision_V4_ArenaEntry",),
    "Arena": ("Stage1_Collision_V4_ArenaCenter", "Stage1_Meadow_Grass_11", "Stage1_Meadow_Tree_11_0", "Stage1_Meadow_Rocks_11_0", "Stage1_Meadow_Tree_11_1", "Stage1_Meadow_Rocks_11_1"),
    "ExitBridge": ("Stage1_Collision_V4_ArenaExit",),
    "DashStart": ("Stage1_Collision_V4_DashStart", "Stage1_Meadow_Grass_12"),
}


def load_obj(path):
    arrays = {"v": [], "vt": [], "vn": []}
    faces = []
    material = ""
    mtllib = ""
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        tag = fields[0]
        if tag in arrays:
            arrays[tag].append(tuple(map(float, fields[1:])))
        elif tag == "mtllib":
            mtllib = " ".join(fields[1:])
        elif tag == "usemtl":
            material = fields[1]
        elif tag == "f":
            faces.append((material, [tuple(int(v) if v else 0 for v in ref.split("/")) for ref in fields[1:]]))
    return arrays, faces, mtllib


def coordinates(arrays, face):
    return [arrays["v"][ref[0] - 1] for ref in face]


def region_for_face(points):
    xmin, xmax = min(p[0] for p in points), max(p[0] for p in points)
    zmin, zmax = min(p[2] for p in points), max(p[2] for p in points)
    if xmax <= 153 + 1e-4 or xmin >= 400 - 1e-4:
        return None
    # 面の広さと位置から、元の地形ブロックへ割り当てる。
    cx, cz = (xmin + xmax) / 2, (zmin + zmax) / 2
    if xmin >= 345 - 1e-4 and xmax <= 359 + 1e-4:
        return "ExitBridge"
    if xmin >= 267 - 1e-4 and xmax <= 293 + 1e-4 and zmax - zmin <= 28:
        return "EntryBridge"
    if xmin >= 279 - 1e-4 and xmax <= 351 + 1e-4 and zmax - zmin >= 30:
        return "Arena"
    if cx < 222:
        return "Approach"
    if cx < 270:
        return "Checkpoint"
    if cx < 294:
        return "EntryBridge"
    if cx < 345:
        return "Arena"
    if cx < 360:
        return "ExitBridge"
    return "DashStart"


def repair_turf_overdraw(arrays, faces):
    # 前半の芝面とチェックポイントの芝面は同じYに重なる。
    # 後者から交差部分だけを抜き、外周の形とUVを保つ。
    out = []
    repaired = 0
    for material, face in faces:
        points = coordinates(arrays, face)
        xs = (min(p[0] for p in points), max(p[0] for p in points))
        zs = (min(p[2] for p in points), max(p[2] for p in points))
        if xs != (222.0, 268.0) or zs != (-5.0, 25.0):
            out.append((material, face))
            continue
        assert len(face) == 4 and all(p[1] == 10.0 for p in points)
        for x0, x1, z0, z1 in ((235.0, 268.0, -5.0, 25.0), (222.0, 235.0, 22.0, 25.0)):
            refs = []
            for x, z in ((x0, z0), (x1, z0), (x1, z1), (x0, z1)):
                arrays["v"].append((x, 10.0, z))
                arrays["vt"].append((x * .08, -z * .08))
                arrays["vn"].append((0.0, 1.0, 0.0))
                refs.append((len(arrays["v"]), len(arrays["vt"]), len(arrays["vn"])))
            out.append((material, refs))
        repaired += 1
    assert repaired == 1, "重複する芝面が見つかりません"
    return out


def repair_path_overdraw(arrays, faces):
    # 道同士が重なる部分は切り欠き、橋の幅広い下地は芝より少しだけ高くする。
    out = []
    counts = {"checkpoint": 0, "entry_base": 0, "entry_trim": 0,
              "exit_base": 0, "exit_trim": 0}
    for material, face in faces:
        points = coordinates(arrays, face)
        xs = (round(min(p[0] for p in points), 2), round(max(p[0] for p in points), 2))
        zs = (round(min(p[2] for p in points), 2), round(max(p[2] for p in points), 2))
        y = round(points[0][1], 2)
        if xs == (222.0, 268.0) and zs == (5.2, 14.8) and y == 10.14:
            # 手前の道が覆う x=222～235, z=5.2～10.8 を除く。
            for x0, x1, z0, z1 in ((235.0, 268.0, 5.2, 14.8),
                                   (222.0, 235.0, 10.8, 14.8)):
                refs = []
                for x, z in ((x0, z0), (x1, z0), (x1, z1), (x0, z1)):
                    arrays["v"].append((x, 10.14, z))
                    arrays["vt"].append((0.0, 0.5))
                    arrays["vn"].append((0.0, 1.0, 0.0))
                    refs.append((len(arrays["v"]), len(arrays["vt"]), len(arrays["vn"])))
                out.append((material, refs))
            counts["checkpoint"] += 1
            continue
        if xs == (345.0, 359.0) and zs == (15.2, 24.8) and y == 10.14:
            # アリーナの道が覆う x=345～351 を除く。
            refs = []
            for x, z in ((351.0, 15.2), (359.0, 15.2), (359.0, 24.8), (351.0, 24.8)):
                arrays["v"].append((x, 10.14, z))
                arrays["vt"].append((0.0, 0.5))
                arrays["vn"].append((0.0, 1.0, 0.0))
                refs.append((len(arrays["v"]), len(arrays["vt"]), len(arrays["vn"])))
            out.append((material, refs))
            counts["exit_trim"] += 1
            continue
        target_y = None
        if xs == (267.28, 292.72) and y == 10.0:
            target_y = 10.01
            counts["entry_base"] += 1
        elif xs == (267.75, 292.25) and y == 10.14:
            target_y = 10.16
            counts["entry_trim"] += 1
        elif xs == (345.0, 359.0) and zs == (7.0, 33.0) and y == 10.0:
            target_y = 10.01
            counts["exit_base"] += 1
        if target_y is not None:
            changed = []
            for ref, point in zip(face, points):
                arrays["v"].append((point[0], target_y, point[2]))
                changed.append((len(arrays["v"]), *ref[1:]))
            out.append((material, changed))
        else:
            out.append((material, face))
    assert all(value == 1 for value in counts.values()), counts
    return out


def write_obj(path, arrays, faces, mtllib, offset=(0, 0, 0), extend_exit=False):
    # 参照された頂点のみを再採番し、UV・法線・材質を引き継ぐ。
    remap = {tag: {} for tag in arrays}
    used = {tag: [] for tag in arrays}
    rewritten = []
    for material, face in faces:
        refs = []
        for ref in face:
            out = []
            for tag, old in zip(("v", "vt", "vn"), ref):
                if old and old not in remap[tag]:
                    remap[tag][old] = len(used[tag]) + 1
                    used[tag].append(arrays[tag][old - 1])
                out.append(remap[tag].get(old, 0))
            refs.append(out)
        rewritten.append((material, refs))
    lines = [f"mtllib {mtllib}", f"o {path.stem}"]
    for tag in ("v", "vt", "vn"):
        for values in used[tag]:
            if tag == "v":
                # 出口の橋とダッシュ開始床の間の1mの隙間を埋める。
                if extend_exit and abs(values[0] - 359.0) < 1e-4:
                    values = (360.0, values[1], values[2])
                values = tuple(values[i] - offset[i] for i in range(3))
            lines.append(tag + " " + " ".join(f"{value:.6f}" for value in values))
    previous = None
    for material, face in rewritten:
        if material != previous:
            lines.append("usemtl " + material)
            previous = material
        lines.append("f " + " ".join("/".join(str(v) for v in ref) for ref in face))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_meta(path):
    # EditorのAssetDatabaseが初回起動時に別GUIDを生成しないよう固定する。
    relative = path.relative_to(ROOT).as_posix()
    meta = {
        "assetType": "Model",
        "guid": uuid.uuid5(uuid.NAMESPACE_URL, relative).hex,
        "importSettings": {"generateTangents": True, "scale": 1.0},
        "importer": "ModelImporter",
        "source": relative,
        "version": 1,
    }
    Path(str(path) + ".meta").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def check_scene(scene):
    by_name = {obj["name"]: obj for obj in scene["objects"]}
    assert len(by_name) == len(scene["objects"]), "オブジェクト名が重複しています"
    assert not any(name.startswith(PREFIX) for name in by_name), "分割済みです。Editorでの調整を保護します"
    for name in SOURCE.values():
        assert name in by_name, name
    for names in CHILDREN.values():
        for name in names:
            assert name in by_name, name
            assert by_name[name].get("parentName") == "Stage1_Meadow_Course_soil", name
    return by_name


def split(scene, write):
    by_name = check_scene(scene)
    original_count = len(scene["objects"])
    root = by_name["Stage1_TerrainRoot"]
    roots = {}
    for label, x, z in REGIONS:
        name = PREFIX + label
        obj = copy.deepcopy(root)
        obj.update(name=name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL, name)),
                   parentName=root["name"], parentGuid=root["guid"], position=[x, 0.0, z],
                   modelName="", isStatic=True, tag="TerrainEditGroup")
        scene["objects"].append(obj)
        roots[label] = obj

    for kind, owner_name in SOURCE.items():
        owner = by_name[owner_name]
        model_path = MODEL_DIR / (Path(owner["modelName"]).stem + ".obj")
        arrays, faces, mtllib = load_obj(model_path)
        if kind == "Turf":
            faces = repair_turf_overdraw(arrays, faces)
        elif kind == "Path":
            faces = repair_path_overdraw(arrays, faces)
        buckets = {label: [] for label, _, _ in REGIONS}
        outside = []
        for material, face in faces:
            points = coordinates(arrays, face)
            label = region_for_face(points)
            if label is None:
                outside.append((material, face))
            elif kind != "Remaining":
                buckets[label].append((material, face))
            # 旧残余モデルの同一高さの道・底面は、分割済み地形と重複するため取り除く。
        assert outside and sum(map(len, buckets.values())) > 0 or kind == "Remaining"
        if write:
            write_obj(model_path, arrays, outside, mtllib)
        print(kind, "outside", len(outside), "near boss", {k: len(v) for k, v in buckets.items()})
        if kind == "Remaining":
            continue
        for label, x, z in REGIONS:
            part = buckets[label]
            if not part:
                continue
            name = f"{PREFIX}{label}_{kind}"
            file_path = MODEL_DIR / f"{name}.obj"
            # OBJのZはエンジン内で反転されるため、親の世界Zに対応する値を加算する。
            if write:
                write_obj(file_path, arrays, part, mtllib,
                          (x - owner["position"][0], 0, -z - owner["position"][2]),
                          extend_exit=label == "ExitBridge")
                write_meta(file_path)
            obj = copy.deepcopy(owner)
            obj.update(name=name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL, name)),
                       modelName=f"Stages/ring_star_court/{name}.obj",
                       parentName=roots[label]["name"], parentGuid=roots[label]["guid"],
                       position=[0.0, 0.0, 0.0])
            scene["objects"].append(obj)

    for label, x, z in REGIONS:
        for name in CHILDREN[label]:
            obj = by_name[name]
            obj["parentName"] = roots[label]["name"]
            obj["parentGuid"] = roots[label]["guid"]
            obj["position"][0] -= x
            obj["position"][2] -= z
    # 分割した出口橋の描画幅に床判定を一致させる。
    exit_collision = by_name["Stage1_Collision_V4_ArenaExit"]["collider"]
    exit_collision["center"][0] += 0.5
    exit_collision["size"][0] += 0.5
    assert len(scene["objects"]) > original_count
    if write:
        SCENE_PATH.write_text(json.dumps(scene, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")


def main():
    scene = json.loads(SCENE_PATH.read_text(encoding="utf-8-sig"))
    split(scene, "--validate" not in sys.argv)


if __name__ == "__main__":
    main()
