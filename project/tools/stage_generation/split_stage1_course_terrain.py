"""コース一体モデルを足場単位へ分割する。一度だけ適用し、以後の配置はEditorで行う。"""
import copy
import json
import sys
import uuid
from pathlib import Path

sys.dont_write_bytecode = True
from split_stage1_midboss_terrain import load_obj, write_obj, write_meta

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / 'Resources/json/3Dobject/stage1_object.json'
MODELS = ROOT / 'Resources/3DModel/Stages/ring_star_court'
PREFIX = 'Stage1_TerrainPart_'
SOURCES = {'Soil': 'Stage1_Meadow_Course_soil', 'Turf': 'Stage1_Meadow_Course_turf',
           'Path': 'Stage1_Meadow_Course_path', 'Detail': 'Stage1_Course_V4'}
LABELS = ['MovingIntro', 'GiantArena', 'AbilityRestGarden', 'BlinkIntro', 'BlinkMainExit',
          'DashRunA', 'DashRunB', 'DashRunC', 'PostDashCheckpoint', 'AppearingStart',
          'AppearingRelay', 'AppearingMainLanding', 'GoalApproach', 'GoalPlaza']


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=4) + '\n', encoding='utf-8')


def world(obj, by_name):
    # 今回組み替える親は平行移動のみ。回転・拡縮を発見したら変更せず中止する。
    pos = list(obj['position'])
    parent = obj.get('parentName')
    seen = set()
    while parent:
        assert parent not in seen, '親子が循環しています'
        seen.add(parent)
        p = by_name[parent]
        assert p['scale'] == [1, 1, 1] and p['rotation'] == [0, 0, 0], parent
        pos = [a + b for a, b in zip(pos, p['position'])]
        parent = p.get('parentName')
    return pos


def apply():
    scene = read(SCENE)
    by = {o['name']: o for o in scene['objects']}
    assert not any(n.startswith(PREFIX) for n in by), '分割済みです。Editorの調整を保護します'
    originals = copy.deepcopy(by)
    arrays, faces, _ = load_obj(MODELS / 'Stage1_Meadow_Course_turf.obj')
    assert len(faces) == len(LABELS)
    regions = {}
    for label, (_, face) in zip(LABELS, faces):
        points = [arrays['v'][r[0]-1] for r in face]
        regions[label] = [(min(p[i] for p in points), max(p[i] for p in points)) for i in range(3)]
    origins = {label: [(a+b)/2 for a,b in bounds] for label,bounds in regions.items()}
    # OBJのZは読み込み時に反転する。親の高さは足場上面に置く。
    for pos in origins.values(): pos[2] *= -1
    origins['GoalDais'] = [850, 18.3, 20]
    roots = {}
    for label, pos in origins.items():
        group = copy.deepcopy(by['Stage1_MidbossTerrain_Arena'])
        name = PREFIX + label
        group.update(name=name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL, name)), position=pos,
                     tag='TerrainEditGroup')
        roots[label] = group
        scene['objects'].append(group)
    roots['Entry'] = by['Stage1_EntryGarden_IslandSoil']
    roots['Shrine'] = by['Stage1_StarShrine_Foundation']

    def choose(points):
        # 長いステージ全体のAABBではなく、各芝面の領域と高さで所属を決める。
        c = [sum(p[i] for p in points)/len(points) for i in range(3)]
        if c[0] < -303: return 'Entry'
        if -195 < c[0] < -160 and c[2] > 64.01: return 'Shrine'
        if all(839.99 <= p[0] <= 860.01 and -30.01 <= p[2] <= -9.99 and p[1] >= 16.99
               for p in points): return 'GoalDais'
        def score(label):
            b = regions[label]
            # 全頂点が領域に収まる区画を優先し、境界面だけ中心距離で解決する。
            outside = sum(max(lo-p[i], 0, p[i]-hi)**2
                          for p in points for i,(lo,hi) in enumerate(b) if i != 1)
            center = sum((c[i]-(b[i][0]+b[i][1])/2)**2 for i in [0,2])
            return outside*1000 + center
        return min(regions, key=score)

    total = 0
    for kind, source_name in SOURCES.items():
        source = by[source_name]
        source_pos = world(source, by)
        assert source['scale'] == [1,1,1] and source['rotation'] == [0,0,0]
        arrays, faces, library = load_obj(ROOT / ('Resources/3DModel/' + source['modelName']))
        buckets = {}
        for face in faces:
            label = choose([arrays['v'][r[0]-1] for r in face[1]])
            buckets.setdefault(label, []).append(face)
        assert sum(map(len,buckets.values())) == len(faces)
        total += len(faces)
        for label, part in buckets.items():
            parent = roots[label]
            parent_pos = world(parent, {**by, **{r['name']:r for r in roots.values()}})
            # 既存モデルの材質・UV・法線を保持し、原点だけ各足場の位置へ移す。
            offset = (parent_pos[0]-source_pos[0], parent_pos[1]-source_pos[1],
                      source_pos[2]-parent_pos[2])
            name = PREFIX + label + '_' + kind
            path = MODELS / (name+'.obj')
            write_obj(path, arrays, part, library, offset)
            write_meta(path)
            obj = copy.deepcopy(source)
            obj.update(name=name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL,name)),
                       parentName=parent['name'], parentGuid=parent['guid'], position=[0,0,0],
                       modelName='Stages/ring_star_court/'+name+'.obj')
            scene['objects'].append(obj)
            # 頂点の世界位置が分割前後で一致することを再読込して検査する。
            new_arrays, new_faces, _ = load_obj(path)
            assert len(new_faces) == len(part)
            for old,new in zip(part,new_faces):
                assert old[0] == new[0]
                for a,b in zip(old[1],new[1]):
                    before=arrays['v'][a[0]-1]; after=new_arrays['v'][b[0]-1]
                    assert all(abs(before[i]-offset[i]-after[i]) < 1e-5 for i in range(3))

    changed = []
    for obj in scene['objects']:
        if obj['name'] not in originals or obj['name'] in SOURCES.values(): continue
        old = originals[obj['name']]
        if old.get('parentName') not in SOURCES.values() and obj['name'] not in (
                'Stage1_CoasterApproach_Soil','Stage1_Meadow_Grass_18'):
            continue
        pos = world(old, originals)
        if obj['name'].startswith('Stage1_Collision_V4_'):
            label = obj['name'].removeprefix('Stage1_Collision_V4_')
            if label not in roots: label = choose([(pos[0],pos[1],-pos[2])])
        elif obj['name'] == 'Stage1_CoasterApproach_Soil': label = 'AppearingRelay'
        else: label = choose([(pos[0],pos[1],-pos[2])])
        parent = roots[label]
        p = world(parent, {**by, **{r['name']:r for r in roots.values()}})
        obj.update(parentName=parent['name'], parentGuid=parent['guid'],
                   position=[a-b for a,b in zip(pos,p)])
        changed.append(obj['name'])
    scene['objects'] = [o for o in scene['objects'] if o['name'] not in SOURCES.values()]
    after = {o['name']:o for o in scene['objects']}
    for name in changed:
        assert all(abs(a-b)<1e-6 for a,b in zip(world(originals[name],originals),world(after[name],after)))
    for o in scene['objects']:
        p=o.get('parentName')
        assert not p or (p in after and o.get('parentGuid')==after[p]['guid']), o['name']
    # 衝突形状と識別子は引き継ぐ。地形を動かすだけで判定まで変わることを避ける。
    for name in changed: assert originals[name].get('collider') == after[name].get('collider')
    write(SCENE,scene)
    print(f'PASS: {len(origins)} independent platforms, {total} faces preserved, {len(changed)} children reparented')


if __name__ == '__main__':
    apply()
