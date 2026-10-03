"""ステージ1入口の非衝突・装飾用草地を決定的に生成する。既存床は変更しない。"""
import json
import math
import random
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Resources/3DModel/Stages/entry_meadow"
SCENE = ROOT / "Resources/json/3Dobject/stage1_object.json"
PREFIX = "Stage1_EntryMeadow_"


def build():
    scene_text = SCENE.read_text(encoding="utf-8")
    scene = json.loads(scene_text)
    if any(o.get("name", "").startswith(PREFIX) for o in scene["objects"]):
        raise SystemExit("入口草地は配置済みです。重複生成を中止します。")
    floor = next(o for o in scene["objects"] if o.get("name") == "Stage1_Collision_V4_StartCourtyard")
    assert floor["position"] == [-330.0, -0.800000011920929, -50.0]
    source = ROOT / "Resources/3DModel/Stages/soft_grass/soft_grass.obj"
    vertices, uvs, normals, faces = [], [], [], []
    for line in source.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "v":
            vertices.append(tuple(map(float, parts[1:])))
        elif parts[0] == "vt":
            uvs.append(tuple(map(float, parts[1:])))
        elif parts[0] == "vn":
            normals.append(tuple(map(float, parts[1:])))
        elif parts[0] == "f":
            faces.append([int(p.split("/")[0]) for p in parts[1:]])
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "meadow.mtl").write_text(
        "newmtl Meadow\nKd 1 1 1\nKs 0.04 0.04 0.04\nNs 4\nd 1\nillum 2\n", encoding="utf-8")
    objects = []

    def add(name, model, pos, material):
        objects.append(dict(name=PREFIX + name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL, PREFIX + name)),
            type="Model", modelName="Stages/entry_meadow/" + model + ".obj", position=pos,
            rotation=[0, 0, 0], scale=[1, 1, 1], color=[1, 1, 1, 1],
            materialType=material, metallic=0, roughness=0.92,
            enableLighting=True, enableEnvMap=False, enableNormalMap=False,
            envIntensity=0.8, emissive=1.0, castShadow=False, isStatic=True,
            collisionAttribute=0, collisionMask=0, collider={"type": 0},
            layer="Default", saveCategory="Object"))

    # OBJ is converted from right-handed coordinates by the model importer.
    ground = ["mtllib meadow.mtl", "o ground", "vn 0 1 0", "usemtl Meadow"]
    for z0, z1 in ((-72, -56), (-44, -28)):
        base = (len([s for s in ground if s.startswith("v ")]))
        for x, z in ((-30, z0 + 50), (30, z0 + 50), (30, z1 + 50), (-30, z1 + 50)):
            ground.append(f"v {x} 0 {-z}")
        for a, b, c in ((1, 2, 3), (1, 3, 4), (3, 2, 1), (4, 3, 1)):
            ground.append(f"f {base+a}//1 {base+b}//1 {base+c}//1")
    (OUT / "ground.obj").write_text("\n".join(ground) + "\n", encoding="utf-8")
    add("Ground", "ground", [-330, 0.035, -50], 29)

    rng = random.Random(230923)
    clumps = 0
    for side in (-1, 1):
        for tile in range(6):
            cx, cz = -355 + tile * 10, -50 + side * 14
            vv, tt, nn, ff = [], [], [], []
            for ix in range(5):
                for iz in range(8):
                    x, z = -4.1 + ix * 1.8 + rng.uniform(-0.45, 0.45), -7 + iz * 1.8 + rng.uniform(-0.45, 0.45)
                    wx, wz = cx + x, cz + z
                    # Leave the portal landing and a clear six-metre half-width path.
                    if wx < -347 or abs(wz + 50) < 6.8 or rng.random() < 0.18:
                        continue
                    scale = rng.uniform(0.52, 0.82)
                    angle = rng.uniform(0, math.tau)
                    co, si = math.cos(angle), math.sin(angle)
                    offset = len(vv)
                    for px, py, pz in vertices:
                        vv.append((x + (px*co-pz*si)*scale, py*scale, -z + (px*si+pz*co)*scale))
                    tt.extend(uvs)
                    nn.extend((nx*co-nz*si, ny, nx*si+nz*co) for nx, ny, nz in normals)
                    ff.extend([[i+offset for i in face] for face in faces])
                    clumps += 1
            if not vv:
                continue
            model = f"grass_{side+1}_{tile}"
            lines = ["mtllib meadow.mtl", "o " + model]
            lines += ["v " + " ".join(f"{q:.6f}" for q in v) for v in vv]
            lines += ["vt " + " ".join(f"{q:.6f}" for q in v) for v in tt]
            lines += ["vn " + " ".join(f"{q:.6f}" for q in v) for v in nn]
            lines += ["usemtl Meadow"]
            lines += ["f " + " ".join(f"{i}/{i}/{i}" for i in f) for f in ff]
            (OUT / (model + ".obj")).write_text("\n".join(lines) + "\n", encoding="utf-8")
            add(f"Grass_{side+1}_{tile}", model, [cx, 0.04, cz], 30)
    # Preserve every existing object's text and settings.
    insertion = ",\n".join("        " + json.dumps(o, ensure_ascii=False, indent=4).replace("\n", "\n        ") for o in objects)
    marker = '"objects": ['
    updated = scene_text.replace(marker, marker + "\n" + insertion + ",", 1)
    parsed = json.loads(updated)
    assert parsed["objects"][len(objects):] == scene["objects"]
    SCENE.write_text(updated, encoding="utf-8")
    print(f"Added {len(objects)} decorative objects, {clumps} grass clumps; existing objects unchanged.")


if __name__ == "__main__":
    build()
