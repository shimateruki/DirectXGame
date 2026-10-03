"""スター1の祠を接地した石造建築に改装する。既存の報酬と破壊条件は保持する。"""
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
from refine_entry_cliff import build_cliff

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Resources/3DModel/Stages/star_vault'
SCENE = ROOT / 'Resources/json/3Dobject/stage1_object.json'
PREFIX = 'Stage1_StarShrine_'


def save(mesh, name):
    lines = ['mtllib shrine.mtl', 'o ' + name]
    lines += ['v ' + ' '.join(f'{v:.6f}' for v in (p[0], p[1], -p[2])) for p, n in mesh.vertices]
    lines += ['vn ' + ' '.join(f'{v:.6f}' for v in (n[0], n[1], -n[2])) for p, n in mesh.vertices]
    for mat, face in mesh.faces:
        lines += ['usemtl ' + mat, 'f ' + ' '.join(f'{i}//{i}' for i in face)]
    (OUT / (name + '.obj')).write_text('\n'.join(lines) + '\n', encoding='utf-8')


def masonry(mesh, center, size):
    # 薄い目地と面取りを加えつつ、通り抜ける穴は作らない。
    along_x = size[0] > size[2]
    length = size[0] if along_x else size[2]
    count = math.ceil(length / 2.8)
    rows = math.ceil(size[1] / 1.45)
    for row in range(rows):
        for col in range(count):
            p = list(center)
            p[1] += -size[1] / 2 + (row + .5) * size[1] / rows
            axis = 0 if along_x else 2
            p[axis] += -length / 2 + (col + .5) * length / count
            s = list(size)
            s[axis] = length / count
            s[1] = size[1] / rows
            mesh.block(p, s, 'StoneLight' if (col + row * 3) % 7 == 0 else 'Stone')


def validate(scene):
    """接地、封印との接続、中央の歩行経路を数値検証する。実機試遊とは分けて扱う。"""
    by_name = {o['name']: o for o in scene['objects']}

    def bounds(name):
        o = by_name[name]
        p, s = o['position'], o['collider']['size']
        return tuple((p[k] - s[k], p[k] + s[k]) for k in range(3))

    left = bounds(PREFIX + 'LeftWallCollision')
    right = bounds(PREFIX + 'RightWallCollision')
    back = bounds(PREFIX + 'BackWallCollision')
    roof = bounds(PREFIX + 'RoofCollision')
    gate = bounds('Stage1_V4_ImpactBreakGate')
    lintel = bounds(PREFIX + 'LintelCollision')
    assert abs(left[1][0] - 6) < .001 and abs(right[1][0] - 6) < .001
    assert abs(left[1][1] - roof[1][0]) < .001
    assert abs(left[0][1] - gate[0][0]) < .001
    assert abs(right[0][0] - gate[0][1]) < .001
    assert left[2][1] >= gate[2][0] and back[2][1] > left[2][0]
    assert abs(lintel[1][0] - gate[1][1]) < .001
    floors = [bounds(n) for n in ('Stage1_Collision_V4_StarVaultCorridor',
                                 'Stage1_Collision_V4_StarVaultRoom', PREFIX + 'ThresholdCollision')]
    walls = [left, right, back, roof, lintel]
    for step in range(115):
        z = -64 - step * .25
        assert any(b[0][0] <= -178 <= b[0][1] and b[2][0] <= z <= b[2][1]
                   and abs(b[1][1] - 6) < .001 for b in floors), ('床の隙間', z)
        assert not any(b[0][0] < -177 and b[0][1] > -179 and b[2][0] < z + .5
                       and b[2][1] > z - .5 and b[1][0] < 9 and b[1][1] > 6.1 for b in walls), ('通路遮断', z)
    assert by_name['Stage1_V4_ImpactBreakGate']['param']['actionMode'] in (0, 7)
    assert by_name['Stage1_StarCoin_01']['position'] == [-178, 9.649999618530273, -98]
    print('Validated: grounded walls, sealed edges, continuous approach, clear center route, original reward')


def main():
    scene = json.loads(SCENE.read_text(encoding='utf-8'))
    if '--validate' in sys.argv:
        validate(scene)
        return
    if any(o['name'].startswith(PREFIX) for o in scene['objects']):
        raise SystemExit('配置済みです。エディターでの調整を保護するため再配置しません。')
    before = copy.deepcopy(scene)
    by_name = {o['name']: o for o in scene['objects']}
    assert by_name['Stage1_V4_ImpactBreakGate']['param']['actionMode'] in (0, 7)
    backup = Path(tempfile.mkdtemp(prefix='cg2-star-shrine-'))
    shutil.copy2(SCENE, backup / SCENE.name)
    OUT.mkdir(parents=True, exist_ok=True)
    palette = {'Stone': (.51, .47, .36), 'StoneLight': (.65, .60, .46),
               'StoneDark': (.37, .36, .29), 'Soil': (.32, .245, .13),
               'Moss': (.26, .39, .10), 'Gold': (.73, .55, .21)}
    text = 'newmtl DefaultMaterial\nKd 1 1 1\nPr .5\nPm 0\n\n'
    text += '\n'.join(f'newmtl {n}\nKd {r} {g} {b}\nKs .04 .04 .04\nPr .86\nPm 0\nNs 8\nillum 2\n'
                      for n, (r, g, b) in palette.items())
    (OUT / 'shrine.mtl').write_text(text, encoding='utf-8')
    modified = set()

    def add(name, model, position, collider=None):
        name = PREFIX + name
        obj = dict(name=name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL, name)),
                   type='InvisibleBox' if collider else 'Model', position=list(position),
                   rotation=[0, 0, 0], scale=[1, 1, 1], color=[1, 1, 1, 1],
                   materialType=34, roughness=.86, metallic=0, emissive=1,
                   enableLighting=True, enableNormalMap=False, enableEnvMap=False,
                   castShadow=not bool(collider), isStatic=True, layer='Default', saveCategory='Object',
                   collisionAttribute=4 if collider else 0, collisionMask=4294967295 if collider else 0,
                   collider=dict(type=3, center=[0, 0, 0], size=list(collider), rotation=[0, 0, 0]) if collider else {'type': 0})
        if model:
            obj['modelName'] = 'Stages/star_vault/' + model + '.obj'
        scene['objects'].append(obj)
        return obj

    # 土台の元の面だけを除いた派生モデル。元OBJは他区画・再生成用に残す。
    course = by_name['Stage1_Course_V4']
    source = ROOT / 'Resources/3DModel' / course['modelName']
    vertices, kept, removed = [], [], 0
    for line in source.read_text(encoding='utf-8').splitlines():
        fields = line.split()
        if fields and fields[0] == 'v':
            vertices.append(tuple(map(float, fields[1:])))
        if fields and fields[0] == 'f':
            points = [vertices[int(f.split('/')[0]) - 1] for f in fields[1:]]
            old_room = all(-194.01 <= x <= -161.99 and 81.99 <= z <= 110.01 for x, y, z in points)
            old_frame = all(-187.01 <= x <= -168.99 and 79.29 <= z <= 81.11 and 5.99 <= y <= 17.01 for x, y, z in points)
            if old_room or old_frame:
                removed += 1
                continue
        if fields and fields[0] == 'mtllib':
            line = 'mtllib ../stage1_meadow/Course_original.mtl'
        kept.append(line)
    assert removed >= 17, removed
    (OUT / 'course_without_vault.obj').write_text('\n'.join(kept) + '\n', encoding='utf-8')
    course['modelName'] = 'Stages/star_vault/course_without_vault.obj'
    modified.add(course['name'])

    # 四角い歩行面は保ち、海面まで連続する岩の裾に置き換える。
    cliff = build_cliff()
    factors = (16 / 30, 22 / 16, 14 / 22)
    cliff.vertices = [(tuple(p[k] * factors[k] for k in range(3)),
                       tuple((n[k] / factors[k]) / math.sqrt(sum((n[j] / factors[j]) ** 2 for j in range(3))) for k in range(3)))
                      for p, n in cliff.vertices]
    save(cliff, 'foundation')
    add('Foundation', 'foundation', (-178, 6, -96))
    floor = Mesh()
    floor.block((0, -.15, 0), (32, .3, 28), 'Stone')
    floor.block((0, .65, -2), (8.4, 1.3, 6.8), 'StoneLight')
    # 屋外の左右には苔色を少量入れ、草地から石造りへの境界を和らげる。
    for x in (-12, 12):
        for z in (-9, -3, 4, 10):
            floor.block((x, .012, z), (3.2, .024, 2.5), 'Moss')
    save(floor, 'courtyard')
    add('Courtyard', 'courtyard', (-178, 6, -96))

    # 床Y=6から天井まで連続。封印の両端X=±7.2に壁の内面を接続する。
    specs = [('LeftWall', (-8.1, 4.7, 3.8), (1.8, 9.4, 24)),
             ('RightWall', (8.1, 4.7, 3.8), (1.8, 9.4, 24)),
             ('BackWall', (0, 4.7, -7.3), (14.4, 9.4, 1.8)),
             ('Roof', (0, 9.8, 3.8), (18, .8, 24))]
    for name, center, size in specs:
        mesh = Mesh()
        masonry(mesh, center, size)
        if name == 'Roof':
            mesh.block((0, 10.4, 3.8), (16.8, .4, 22.8), 'StoneLight')
        save(mesh, name)
        obj = by_name['Stage1_V4_StarVault_' + name]
        obj.update(modelName='Stages/star_vault/' + name + '.obj', position=[-178, 6, -96],
                   scale=[1, 1, 1], color=[1, 1, 1, 1], materialType=34, roughness=.86,
                   metallic=0, emissive=1, enableEnvMap=False, enableNormalMap=False, castShadow=True)
        modified.add(obj['name'])
        add(name + 'Collision', None, tuple(center[k] + (-178, 6, -96)[k] for k in range(3)), tuple(s / 2 for s in size))

    # 封印上の隙間を塞ぐ梁。大きなピンクの面は能力の目印として残す。
    frame = Mesh()
    frame.block((0, 9.2, 15.8), (14.4, 1.6, 3), 'StoneLight')
    for x in (-8.1, 8.1):
        frame.block((x, 4.7, 15.8), (2, 9.4, 2.2), 'StoneLight')
    save(frame, 'portal_frame')
    add('PortalFrame', 'portal_frame', (-178, 6, -96))
    add('LintelCollision', None, (-178, 15.2, -80.2), (7.2, .8, 1.5))
    bridge = Mesh()
    bridge.block((0, -.2, 0), (14.4, .4, 5), 'StoneLight')
    save(bridge, 'threshold')
    add('Threshold', 'threshold', (-178, 6, -80.5))
    add('ThresholdCollision', None, (-178, 5.8, -80.5), (7.2, .2, 2.5))
    # 操作の邪魔をせず入口から台座へ視線をつなぐ、薄い床の象嵌。
    guide = Mesh()
    for z in (-74, -77, -84, -88, -91):
        guide.block((0, .016, z + 96), (2.1, .032, .55), 'Gold')
    save(guide, 'path_inlay')
    add('PathInlay', 'path_inlay', (-178, 6, -96))

    # 既存の報酬・ギミック・他区画は一切変更しない。
    for old, new in zip(before['objects'], scene['objects']):
        if old['name'] not in modified:
            assert old == new, old['name']
        else:
            for field in ('guid', 'collider', 'collisionAttribute', 'collisionMask', 'param', 'eventID', 'targetID'):
                assert old.get(field) == new.get(field), (old['name'], field)
    assert len(modified) == 5
    validate(scene)
    SCENE.write_text(json.dumps(scene, ensure_ascii=False, indent=4) + '\n', encoding='utf-8')
    print('Backup:', backup)
    print('Replaced faces:', removed, 'Existing visual objects:', len(modified), 'Added:', len(scene['objects']) - len(before['objects']))


if __name__ == '__main__':
    main()
