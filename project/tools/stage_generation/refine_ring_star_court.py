"""ステージ1の点滅足場区画を、リング敵と昇降床を使うスター広場へ改修する。

既存の入口・出口・他区画は保持。再実行でEditorの調整を上書きしない。
--validate は配置・接続・報酬数を検査するだけで書き込まない。
"""
import copy
import json
import math
import os
from pathlib import Path
import random
import shutil
import sys
import tempfile
import uuid

sys.dont_write_bytecode = True
from dress_entry_garden import Mesh

ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / 'Resources/3DModel'
OUT = MODELS / 'Stages/ring_star_court'
SCENE = ROOT / 'Resources/json/3Dobject/stage1_object.json'
ENEMIES = ROOT / 'Resources/json/3Dobject/stage1_enemy.json'
PREFIX = 'Stage1_RingCourt_'
EVENT = 7450
OLD_ISLANDS = ((20, -11, 9, 9), (76, 18, 6, 8), (70, 91, 15.5, 7), (111, 66, 6.5, 6.5))


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save_mesh(mesh, name, uv_kind='ground'):
    # 地面シェーダーのワールド座標模様と、土の深さ用UVを維持する。
    lines = ['mtllib surface.mtl', 'o ' + name]
    lines += ['v ' + ' '.join(f'{v:.6f}' for v in (p[0], p[1], -p[2])) for p, n in mesh.vertices]
    lines += ['vn ' + ' '.join(f'{v:.6f}' for v in (n[0], n[1], -n[2])) for p, n in mesh.vertices]
    lines += [('vt 1 ' + str(max(0, -p[1]))) if uv_kind == 'soil' else 'vt 0 0.5' for p, n in mesh.vertices]
    for material, face in mesh.faces:
        lines += ['usemtl Meadow', 'f ' + ' '.join(f'{i}/{i}/{i}' for i in face)]
    (OUT / (name + '.obj')).write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return 'Stages/ring_star_court/' + name + '.obj'


def remove_old_island_faces(source, name):
    vertices, kept, removed = [], [], 0
    for line in source.read_text(encoding='utf-8').splitlines():
        fields = line.split()
        if fields and fields[0] == 'v':
            vertices.append(tuple(map(float, fields[1:])))
        if fields and fields[0] == 'f':
            points = [vertices[int(v.split('/')[0]) - 1] for v in fields[1:]]
            if any(all(abs(x-cx) <= hx+.3 and abs(-z-cz) <= hz+.3 for x, y, z in points)
                   for cx, cz, hx, hz in OLD_ISLANDS):
                removed += 1
                continue
        if fields and fields[0] == 'mtllib':
            line = 'mtllib ' + os.path.relpath(source.parent / fields[1], OUT).replace('\\', '/')
        kept.append(line)
    (OUT / (name + '.obj')).write_text('\n'.join(kept) + '\n', encoding='utf-8')
    print(name, 'removed faces:', removed)
    return 'Stages/ring_star_court/' + name + '.obj'


def validate(scene, enemies):
    by = {o['name']: o for o in scene['objects']}
    owned = [o for o in scene['objects'] if o['name'].startswith(PREFIX)]
    assert len({o['name'] for o in owned}) == len(owned), '追加オブジェクト名の重複'
    guids = [o['guid'] for o in owned]
    assert len(guids) == len(set(guids)), 'GUIDの重複'
    assert sum(o.get('gimmickType') == 'BlinkBlock' for o in scene['objects']) == 6
    assert sum(o['name'].startswith('Stage1_StarCoin_') for o in scene['objects']) == 3
    switch = by[PREFIX+'StarSwitch']
    assert switch['targetID'] == EVENT == switch['components']['GameplayLink']['targetId']
    for i in range(3):
        floor = by[PREFIX+f'TimedBridge_{i}']
        assert floor['myEventID'] == EVENT == floor['components']['GameplayLink']['eventId']
        assert floor['param']['interval'] == 8.5
    lift = by[PREFIX+'StarLift']
    assert not lift['recorder']['recordPathName'] and 'PathMover' not in lift['components']
    top = lift['position'][1] + lift['collider']['center'][1] + lift['collider']['size'][1]
    assert abs(top-lift['param']['moveAmount']-8.42) < .001
    assert abs(top+lift['param']['moveAmount']-26.42) < .001
    assert by['Stage1_StarCoin_02']['position'] == [88, 28.5, 69]
    ring = next(o for o in enemies['objects'] if o.get('enemyType') == 'RingBurner')
    assert sum(o.get('enemyType') == 'RingBurner' for o in enemies['objects']) == 1
    assert math.dist(ring['position'][::2], switch['position'][::2]) < ring['param']['moveAmount']
    settings = read(ROOT/'Resources/json/gameplay/status_settings.json')
    assert settings['enemies']['RingBurner']['detectionRange'] >= ring['param']['moveAmount']
    # 入口・出口と常設本線はスター取得状態に依存しない。
    assert by[PREFIX+'MainCollision']['position'] == [50, -4, -10]
    assert by[PREFIX+'MainCollision']['collider']['size'] == [50, 12, 26]
    for i in range(3):
        assert by[PREFIX+f'ExitPad_{i}']['collisionAttribute'] == 4
    # 通常ジャンプの高さを超える落差で、帰り道からの逆走を防ぐ。
    status = read(ROOT/'Resources/json/gameplay/status_settings.json')['player']
    height = status['jumpPower']**2/(2*status['gravity'])
    assert 26-18 > height and 18-10 > height
    assert 8.42 > height  # 下段から昇降床への直接の飛び乗りは不可。
    # 最悪の位相で到着しても、橋の寿命内に昇降床へ跳べる時間窓がある。
    speed = lift['param']['speed']
    reachable_sine = (8.42 + height - top) / lift['param']['moveAmount']
    longest_wait = (math.pi - 2 * math.asin(reachable_sine)) / speed
    approach_time = 44 / status['speed']
    assert longest_wait + approach_time < 8.5
    # 描画の半径・寿命・補間方法と、ゲーム側の波を一致させる。
    for suffix in ('wave', 'wave_heat'):
        effect = read(ROOT/f'Resources/json/effect/effect_ring_burner_{suffix}.json')
        assert effect['Lifetime'] == 3.6 and effect['EasingType'] == 0
        assert effect['EnableReveal'] is False
        assert effect['StartScale'][0] == 1.6 and effect['EndScale'][0] == 11
        assert effect['NoiseTexturePath'].endswith('/white.png')
    # 低フレームレートでも地上の相手を波が飛び越えないことを数値検査する。
    radius = ring['param']['moveAmount']
    start = radius * 1.6 / 11
    for fps in (15, 30, 60, 144):
        for distance in (start + 1, 10, 17, radius - 1):
            previous = start
            detected = False
            for frame in range(1, math.ceil(3.6 * fps) + 1):
                current = start + (radius-start) * min(1, frame/fps/3.6)
                width = max(.72, current*.045)
                detected |= previous-width <= distance <= current+width
                previous = current
            assert detected, (fps, distance)
    # モデル参照とイベント参照の整合性。
    for o in scene['objects']:
        if o['name'].startswith(PREFIX) and o.get('modelName'):
            p = MODELS/o['modelName']
            assert p.exists() or p.with_suffix('.obj').exists() or (p/p.name).with_suffix('.obj').exists(), p
    print('Validated: 6 blink pads, permanent main route, switch link, lift endpoints, recovery, 3 stars')


def main():
    scene, enemies = read(SCENE), read(ENEMIES)
    if '--validate' in sys.argv:
        validate(scene, enemies)
        return
    if any(o['name'].startswith(PREFIX) for o in scene['objects']):
        raise SystemExit('配置済みです。Editor変更を保護するため再配置しません。')
    before, enemies_before = copy.deepcopy(scene), copy.deepcopy(enemies)
    by = {o['name']: o for o in scene['objects']}
    assert not any(o.get('myEventID') == EVENT or o.get('targetID') == EVENT for o in scene['objects'])
    backup = Path(tempfile.mkdtemp(prefix='cg2-ring-star-court-'))
    shutil.copy2(SCENE, backup/SCENE.name)
    shutil.copy2(ENEMIES, backup/ENEMIES.name)
    print('Backup:', backup)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'surface.mtl').write_text('newmtl Meadow\nKd 1 1 1\nKs .04 .04 .04\nNs 4\nillum 2\n', encoding='utf-8')
    changed = set()
    removed = set()
    old_colliders = {'Stage1_Collision_V4_'+n for n in ('BlinkRelay','BlinkJunction','BlinkStarBalcony','BlinkReturn')}
    for o in scene['objects']:
        p = o.get('position', [0,0,0])
        vegetation = o['name'].startswith('Stage1_Meadow_') and 0 < p[0] < 130 and -60 < p[2] < 110
        if o['name'].startswith(('Stage1_V4_BlinkFloor_', 'Stage1_V4_BlinkGuide_')) or o['name'] in old_colliders or vegetation:
            removed.add(o['name'])
    scene['objects'] = [o for o in scene['objects'] if o['name'] not in removed]
    for name in ('Stage1_Course_V4', 'Stage1_Meadow_Course_turf', 'Stage1_Meadow_Course_soil', 'Stage1_Meadow_Course_path'):
        o = by[name]
        o['modelName'] = remove_old_island_faces(MODELS/o['modelName'], name)
        changed.add(name)

    def add(name, model, pos, material=29, size=None):
        name = PREFIX+name
        o = dict(name=name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL,name)), type='InvisibleBox' if size else 'Model',
                 position=list(pos), rotation=[0,0,0], scale=[1,1,1], color=[1,1,1,1], materialType=material,
                 roughness=.9, metallic=0, emissive=1, enableLighting=True, enableEnvMap=False,
                 enableNormalMap=False, castShadow=not bool(size), isStatic=True, layer='Default', saveCategory='Object',
                 collisionAttribute=4 if size else 0, collisionMask=255 if size else 0,
                 collider=dict(type=3,center=[0,0,0],size=list(size),rotation=[0,0,0]) if size else {'type':0})
        if model: o['modelName']=model
        scene['objects'].append(o)
        return o

    def island(name,x,z,hx,hz,top,path=True,bottom=-16):
        soil=Mesh();soil.block((0,-(top-bottom)/2,0),(hx*2,top-bottom,hz*2),'Meadow')
        turf=Mesh();turf.block((0,-.05,0),(hx*2,.1,hz*2),'Meadow')
        add(name+'Soil',save_mesh(soil,name+'_soil','soil'),(x,top,z),33)
        add(name+'Turf',save_mesh(turf,name+'_turf'),(x,top+.01,z),29)
        add(name+'Collision',None,(x,(top+bottom)/2,z),size=(hx,(top-bottom)/2,hz))
        if path:
            m=Mesh();m.block((0,0,0),(max(2,hx*2-4),.035,min(8,hz*2-2)),'Meadow')
            add(name+'Path',save_mesh(m,name+'_path'),(x,top+.04,z),32)

    def clone(template,name,pos):
        o=copy.deepcopy(template)
        o.update(name=PREFIX+name,guid=str(uuid.uuid5(uuid.NAMESPACE_URL,PREFIX+name)),position=list(pos),
                 rotation=[0,0,0],quaternion=[0,0,0,1],parentGuid='',parentName='',components={},
                 recorder=dict(recordPathName='',isRecordLoop=False,isRecordRelative=False),
                 myEventID=-1,targetID=-1)
        scene['objects'].append(o)
        return o

    island('Main',50,-10,50,26,8)
    # 入口は二色の短い3列。どちらのジャンプ回数からでも渡れる。
    for i,x in enumerate((-31,-19,-7)):
        for row,z in enumerate((-42,-29)):
            o=clone(by['Stage1_V4_BlinkFloor_FieldA_01'],f'EntryBlink_{i}_{row}',(x,6.3+i*.5,z))
            o['param']['colorType']=(i+row)%2
    # リング回避で切り替わらない常設出口。ここから先の既存地形は変更しない。
    for i,x in enumerate((108,119,129)):
        o=clone(by['Stage1_V4_BlinkFloor_FieldA_01'],f'ExitPad_{i}',(x,8.1+i*.65,0))
        o.update(type='Model',gimmickType='',param={},color=[.68,.82,.73,1],collisionAttribute=4,isStatic=True)
    island('StarTower',88,69,10,8,26)
    # 帰路は大きな下り二段。通常ジャンプで逆登りはできないが、落下して戻れる。
    island('ReturnHigh',109,69,9,8,18)
    island('ReturnLow',127,52,9,17,10)
    island('ReturnWalk',127,22,7,13,10)
    # 下段は昇降床の最低位置より8以上低くし、失敗から入口へ戻す専用にする。
    island('Recovery',85,37,13,21,0,False)
    for i,(z,h) in enumerate(((44,0),(36,2),(28,4),(20,6),(13,8))):
        island(f'RecoveryStep_{i}',66,z,6,4,h,False)
    switch=clone(by['Stage1_V4_AppearingSwitch_Approach'],'StarSwitch',(88,8.22,12))
    switch['targetID']=EVENT
    switch['components']={'GameplayLink':dict(_editorPresent=True,eventId=-1,targetId=EVENT,version=1)}
    for i,z in enumerate((23,34,45)):
        o=clone(by['Stage1_V4_AppearingFloor_Approach_01'],f'TimedBridge_{i}',(88,8,z))
        o['myEventID']=EVENT;o['param']['interval']=8.5
        o['components']={'GameplayLink':dict(_editorPresent=True,eventId=EVENT,targetId=-1,version=1)}
    lift=clone(by['Stage1_V4_MovingIntro_A'],'StarLift',(88,17,56))
    # 既存MovingFloorのY軸往復。PathMoverは複製せず、二重移動を防ぐ。
    lift['param'].update(actionMode=4,moveAmount=9,speed=.65)
    # moving_platformの上面はローカルY=.51。
    lift['position'][1]=16.91
    star=by['Stage1_StarCoin_02'];star['position']=[88,28.5,69];changed.add(star['name'])
    # 元の星GUID・コンポーネント・取得IDを保ち、セーブデータとの対応を変えない。
    enemy=next(o for o in enemies['objects'] if o['name']=='Stage1_Enemy_RingBurner_BlinkJunction')
    enemy['position']=[76,8,0]
    enemy['param'].update(moveAmount=28,interval=2.2,shakeDuration=1.0)
    enemies['objects']=[o for o in enemies['objects'] if o['name']!='Stage1_Enemy_RingBurner_BlinkReturn']
    # 草は広場の縁だけ。波と踏める天板の視認性を優先する。
    rng=random.Random(927)
    for i in range(42):
        x=rng.uniform(5,95);z=rng.choice((-32,13))+rng.uniform(-1,1)
        if (x-88)**2+(z-12)**2<60: continue
        grass=add(f'Grass_{i}','Stages/soft_grass/soft_grass.obj',(x,8.02,z),30)
        grass['scale']=[1.2,1.6,1.2];grass['rotation'][1]=rng.uniform(0,360)
    for i,x in enumerate((12,42,64,93)):
        clone(by['Stage1_V4_BlinkGuide_FieldA_02'],f'Guide_{i}',(x,10,-10))
    # このパッチの対象以外は厳密に不変。
    for old in before['objects']:
        # 既存には同名の子エフェクトがあるため、名前辞書で潰さず内容を比較する。
        if old['name'] not in removed|changed: assert old in scene['objects'],old['name']
    for old in enemies_before['objects']:
        if old.get('enemyType')!='RingBurner': assert old in enemies['objects'],old['name']
    validate(scene,enemies)
    for path,data in ((SCENE,scene),(ENEMIES,enemies)):
        path.write_text(json.dumps(data,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    print('Removed objects:',len(removed),'Changed existing:',sorted(changed))
    print('Added objects:',sum(o['name'].startswith(PREFIX) for o in scene['objects']))


if __name__=='__main__': main()
