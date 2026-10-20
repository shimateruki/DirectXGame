"""Stage2の地形判定をモデル全体へ揃える。既存の配置・GUID・マテリアルは維持する。"""
from pathlib import Path
import copy
import json
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/model_generation"))
from magma_stage.models import build_fortress_plaza


def write_flat_plaza():
    """生成器の平坦な広場のみ書き出し、手調整済みMTLとmetaは変更しない。"""
    model = build_fortress_plaza()
    lines = ["mtllib fortress_plaza.mtl", "o fortress_plaza"]
    lines += [f"v {x:.6f} {y:.6f} {z:.6f}" for x, y, z in model.positions]
    lines += [f"vt {u:.6f} {v:.6f}" for u, v in model.uvs]
    lines += [f"vn {x:.6f} {y:.6f} {z:.6f}" for x, y, z in model.normals]
    material = None
    for mat, indices in model.faces:
        if mat != material:
            lines.append("usemtl " + mat)
            material = mat
        lines.append("f " + " ".join(f"{i}/{i}/{i}" for i in indices))
    (ROOT / "Resources/3DModel/Stages/fortress_plaza/fortress_plaza.obj").write_text(
        "\n".join(lines) + "\n", encoding="utf-8", newline="\r\n")


def ground(obj):
    """カテゴリID優先のローダーでも確実に地形として登録する。"""
    # 動く床も地形自身を押し戻さない。地形同士の不要な精密判定だけを除外する。
    obj.update(collisionAttribute=4, collisionAttributeIds=["ground"], collisionAttributeUnknownBits=0,
               collisionMask=4294967291, collisionMaskIds=[], collisionMaskUnknownBits=4294967291)


def vertices(model):
    """OBJの実寸で広域判定も更新し、Editorを開いた直後から厚みを持たせる。"""
    path = ROOT / "Resources/3DModel" / model / (model.split("/")[-1] + ".obj")
    return [list(map(float, line.split()[1:4])) for line in path.read_text(encoding="utf-8").splitlines() if line.startswith("v ")]


def main():
    """下部地形の判定漏れと門の開口をまとめて直す。再実行しても子を重複作成しない。"""
    write_flat_plaza()
    path = ROOT / "Resources/json/3Dobject/stage2_object.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    objects = data["objects"]
    template = next(o for o in objects if o.get("type") == "InvisibleBox" and
                    (o["name"].endswith("_LowerCliffCollision") or "_SolidPart_" in o["name"]))
    # 前回の上下二層判定を全体の凸包へ置き換える。名前と型の両方で対象を限定する。
    objects = [o for o in objects if not (o.get("type") == "InvisibleBox" and
               (o["name"].endswith("_LowerCliffCollision") or "_SolidPart_" in o["name"]))]
    terrain = {"magma_fortress_foundation", "fortress_plaza", "fortress_ramp", "basalt_causeway",
               "magma_lift_rock", "magma_ferry_break_rock", "metal_grate_platform", "stage_select_gate_pad"}
    terrain.update("magma_escape_rock_" + str(i) for i in range(6))
    changed = []
    children = []
    for obj in objects:
        model = obj.get("modelName", "")
        if model.startswith("Stages/") and model.split("/")[-1] in terrain:
            points = vertices(model)
            lo = [min(v[a] for v in points) for a in range(3)]
            hi = [max(v[a] for v in points) for a in range(3)]
            obj["collider"] = dict(type=3, center=[(a+b)/2 for a,b in zip(lo,hi)],
                                   size=[(b-a)/2 for a,b in zip(lo,hi)], rotation=[0,0,0], useModelVolume=True)
            ground(obj)
            changed.append(obj["name"])
        elif model == "Stages/fortress_gate":
            # 生成器の箱は24頂点ずつ独立している。各部材を親に連動する子判定へする。
            points = vertices(model)
            assert len(points) % 24 == 0
            obj["collider"]["type"] = 0
            obj.update(collisionAttribute=0, collisionAttributeIds=[], collisionAttributeUnknownBits=0)
            for index in range(len(points)//24):
                part = points[index*24:(index+1)*24]
                lo = [min(v[a] for v in part) for a in range(3)]
                hi = [max(v[a] for v in part) for a in range(3)]
                child = copy.deepcopy(template)
                child.update(name=f'{obj["name"]}_SolidPart_{index+1:02}',
                             guid=str(uuid.uuid5(uuid.UUID(obj["guid"]), f"solid-part-{index}")),
                             parentGuid=obj["guid"], parentName=obj["name"], position=[0,0,0],
                             rotation=[0,0,0], quaternion=[0,0,0,1], scale=[1,1,1])
                child["collider"] = dict(type=3, center=[(a+b)/2 for a,b in zip(lo,hi)],
                                         size=[(b-a)/2 for a,b in zip(lo,hi)], rotation=[0,0,0])
                ground(child)
                children.append(child)
    # 同じ向きの広場と下部地形は、ワールド配置を保ったまま親子化する。
    by_name = {o["name"]: o for o in objects}
    for obj in objects:
        if not obj["name"].endswith("_Substructure") or obj.get("parentGuid"): continue
        parent = by_name[obj["name"].removesuffix("_Substructure")]
        assert obj["rotation"] == parent["rotation"]
        assert obj["position"][0] == parent["position"][0] and obj["position"][2] == parent["position"][2]
        obj.update(parentGuid=parent["guid"], parentName=parent["name"],
                   position=[0, (obj["position"][1]-parent["position"][1])/parent["scale"][1], 0],
                   scale=[a/b for a,b in zip(obj["scale"],parent["scale"])], rotation=[0,0,0], quaternion=[0,0,0,1])
    data["objects"] = objects + children
    assert len({o["guid"] for o in data["objects"]}) == len(data["objects"])
    path.write_text(json.dumps(data, ensure_ascii=False, indent=4)+"\n", encoding="utf-8", newline="\r\n")
    print(f"Full-volume terrain: {len(changed)}, gate parts: {len(children)}, total: {len(data['objects'])}")


if __name__ == "__main__":
    main()
