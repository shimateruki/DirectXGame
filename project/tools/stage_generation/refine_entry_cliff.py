"""入口の側面だけを段状の崖に変更する。歩行面と当たり判定は維持する。"""
import copy
import json
import math
from pathlib import Path
import shutil
import sys
import tempfile
import uuid

sys.dont_write_bytecode = True
from dress_entry_garden import Mesh

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Resources/3DModel/Stages/entry_cliff'
SCENE = ROOT / 'Resources/json/3Dobject/stage1_object.json'


def save(mesh, name):
    lines = ['mtllib entry_cliff.mtl', 'o ' + name]
    lines += ['v ' + ' '.join(f'{q:.6f}' for q in (p[0], p[1], -p[2])) for p, n in mesh.vertices]
    lines += ['vn ' + ' '.join(f'{q:.6f}' for q in (n[0], n[1], -n[2])) for p, n in mesh.vertices]
    for material, face in mesh.faces:
        lines += ['usemtl ' + material, 'f ' + ' '.join(f'{i}//{i}' for i in face)]
    (OUT / (name + '.obj')).write_text('\n'.join(lines) + '\n', encoding='utf-8')


def build_cliff():
    mesh = Mesh()
    corners = [(-30, -22), (-30, 22), (30, 22), (30, -22)]
    border = []
    for edge in range(4):
        a, b = corners[edge], corners[(edge + 1) % 4]
        for j in range(10):
            t = j / 10
            border.append(tuple(a[k] + (b[k] - a[k]) * t for k in range(2)))
    rings = []
    for row, (y, spread) in enumerate(((0, 0), (-.55, .05), (-3.4, .8), (-3.8, .25),
                                      (-7.2, 1.1), (-7.6, .35), (-11.2, .5), (-16, -1.8))):
        ring = []
        for i, (x, z) in enumerate(border):
            noise = math.sin(i * 1.73 + row * .7) * .45 + math.sin(i * .73) * .32
            d = 0 if row == 0 else spread + noise
            radius = math.hypot(x, z)
            yy = y if row == 0 else y + math.sin(i * .91) * min(.28, abs(y) * .1)
            ring.append((x + x / radius * d, yy, z + z / radius * d))
        rings.append(ring)
    for row in range(len(rings) - 1):
        for i in range(len(border)):
            j = (i + 1) % len(border)
            a, b, c, d = rings[row][i], rings[row][j], rings[row + 1][j], rings[row + 1][i]
            material = 'Soil' if row == 0 else ('StoneLight' if row in (2, 4) else ('StoneDark' if i % 4 == 0 else 'Stone'))
            mesh.triangle(a, c, b, material)
            mesh.triangle(a, d, c, material)
    # 外向きの法線と、既存歩行面につながる上端を検証する。
    assert all(n[0] * p[0] + n[2] * p[2] > 0 for p, n in mesh.vertices)
    assert all(p[1] == 0 for p in rings[0])
    return mesh


def main():
    scene = json.loads(SCENE.read_text(encoding='utf-8'))
    before = copy.deepcopy(scene)
    soil = next(o for o in scene['objects'] if o['name'] == 'Stage1_EntryGarden_IslandSoil')
    assert soil['position'] == [-330, 0, -50] and soil['scale'] == [1, 1, 1]
    assert soil['collisionAttribute'] == 0 and soil['collisionMask'] == 0 and soil['collider']['type'] == 0
    cliff = build_cliff()
    backup = Path(tempfile.mkdtemp(prefix='cg2-entry-cliff-'))
    shutil.copy2(SCENE, backup / SCENE.name)
    OUT.mkdir(parents=True, exist_ok=True)
    palette = {'Soil': (.32, .245, .13), 'Stone': (.51, .43, .30),
               'StoneLight': (.61, .53, .38), 'StoneDark': (.43, .36, .26)}
    mtl = 'newmtl DefaultMaterial\nKd 1 1 1\nPr 0.5\nPm 0\n\n'
    mtl += '\n'.join(f'newmtl {name}\nKd {r} {g} {b}\nKs 0.04 0.04 0.04\nPr 0.88\nPm 0\nNs 15.36\nillum 2\n'
                     for name, (r, g, b) in palette.items())
    (OUT / 'entry_cliff.mtl').write_text(mtl, encoding='utf-8')
    save(cliff, 'terraced_cliff')
    pier = Mesh()
    for row in range(3):
        pier.block((0, .4 + row * .78, 0), (1.6, .75, 1.6), 'StoneLight' if row == 2 else 'Stone')
    pier.rock((1.9, .35, .8), (1.1, .55, .75), 74)
    pier.rock((-.7, .18, 1.9), (.7, .3, .5), 75)
    save(pier, 'broken_pier')
    changes = dict(modelName='Stages/entry_cliff/terraced_cliff.obj', materialType=34,
                   roughness=.88, metallic=0, enableNormalMap=False, enableEnvMap=False,
                   normalMapPath='', ormMapPath='', color=[1, 1, 1, 1], castShadow=True)
    soil.update(changes)
    names = {o['name'] for o in scene['objects']}
    for i, position in enumerate(([-349, 0, -34], [-324, 0, -66]), 1):
        name = f'Stage1_EntryCliff_Ruin_{i}'
        if name in names:
            continue
        prop = copy.deepcopy(soil)
        prop.update(name=name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL, name)),
                    modelName='Stages/entry_cliff/broken_pier.obj', position=position,
                    rotation=[0, 0, 0])
        scene['objects'].append(prop)
    # 既存オブジェクトは対象の見た目以外、全フィールドを維持する。
    for old, new in zip(before['objects'], scene['objects']):
        expected = copy.deepcopy(old)
        if old['name'] == soil['name']:
            expected.update(changes)
        assert expected == new, old['name']
    assert {k: v for k, v in before.items() if k != 'objects'} == {k: v for k, v in scene.items() if k != 'objects'}
    SCENE.write_text(json.dumps(scene, ensure_ascii=False, indent=4) + '\n', encoding='utf-8')
    print(f'Backup: {backup}')
    print(f'Cliff: {len(cliff.faces)} triangles; decorations: 2; gameplay fields unchanged')


if __name__ == '__main__':
    main()
