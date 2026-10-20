"""既存の終盤乗車床だけをいかだへ移行する。再配置は --apply を明示した場合のみ。"""
import argparse
import copy
import json
from pathlib import Path
import uuid

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / 'Resources/json/3Dobject/stage1_object.json'
ROUTE = ROOT / 'Resources/json/scenario/stage1_coaster.json'
PRESETS = ROOT / 'Resources/json/preset/presets.json'
JET = 'Stage1_Raft_WaterJet'


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=4) + '\n', encoding='utf-8')


def apply():
    # 名前・GUID・取得済みスターの識別子を維持し、対象以外の編集結果を保護する。
    scene, presets = read(SCENE), read(PRESETS)
    objects = {o['name']: o for o in scene['objects']}
    deck = objects['Stage1_Coaster_Deck']
    raft = presets['Stage1/Gimmick/WoodenRaft']
    for key in ('modelName', 'collider', 'materialType', 'metallic', 'roughness', 'emissive',
                'color', 'collisionAttribute', 'collisionMask', 'enableLighting', 'blendMode'):
        deck[key] = copy.deepcopy(raft[key])
    deck['collisionMaskIds'] = [x for x in deck['collisionMaskIds'] if x != 'ground']
    deck.update(position=[635, 11.35, -14], rotation=[0, 0, 0], quaternion=[0, 0, 0, 1],
                scale=[1, 1, 1], myEventID=9480)
    deck['param'].update(actionMode=1, moveAmount=0, speed=1)

    # ゴール上面17m、いかだ頂点22.65m、スター島24m。通常ジャンプの上昇量5.76m以内。
    objects['Stage1_Coaster_StarIslandGroup']['position'][1] = 24
    objects['Stage1_StarCoin_03']['position'][1] = 26.5
    for i, pos in enumerate(([760, 25.5, 37], [758, 27, 41], [756, 26.5, 46])):
        objects[f'Stage1_Coaster_JumpGuide_{i}']['position'] = pos

    jet = copy.deepcopy(deck)
    jet.update(name=JET, guid=str(uuid.uuid5(uuid.NAMESPACE_URL, JET)),
               position=[762.8, -13.72, 29], modelName='Stages/magma_pressure_column',
               gimmickType='WaterGeyser', myEventID=-1, targetID=9480,
               collisionAttribute=0, collisionMask=0, collisionAttributeIds=[], collisionMaskIds=[],
               castShadow=False, components={}, materialType=0)
    jet['collider'] = dict(type=0, center=[0, 0, 0], size=[4, .5, 4], rotation=[0, 0, 0])
    jet['param'].update(gimmickType='WaterGeyser', actionMode=3, startActive=False,
                        returnOnOff=True, detectionRange=4.5, speed=0)
    scene['objects'] = [o for o in scene['objects'] if o['name'] != JET] + [jet]

    # 既存のTimelineをそのままEditorで開ける。海上では島の側面を避けて航行する。
    points = [(0, [635, 11.35, -14], 0), (.65, [635, 11.35, -14], 1),
              (2, [643, -11.4, -14], 2), (2.35, [643, -10.9, -14], 4),
              (2.7, [643, -11.4, -14], 4), (7, [679, -11.4, -5], 0),
              (8.5, [684, -11.4, -5], 4), (12.5, [712, -11.4, -10], 0),
              (14, [716, -11.4, -10], 4), (19, [762.8, -11.4, 29], 0),
              (19.8, [762.8, -11.4, 29], 2), (22, [762.8, 22, 29], 0),
              (23.3, [762.8, 22, 29], 4), (25, [762.8, 16.35, 13], 0)]
    route = read(ROUTE)
    route.update(name='stage1_raft', duration=25)
    route['tracks'] = [dict(name='いかだ：乗船・落下着水・海上航行・噴射上昇',
        targetName=deck['name'], targetEventId=-1, startTime=0, enabled=True, muted=False,
        relative=False, holdLast=True,
        keys=[dict(time=t, position=p, rotation=[0, 0, 0], scale=[1, 1, 1], easingToNext=e)
              for t, p, e in points])]
    def signal(t, name, kind, target, payload):
        return dict(time=t, name=name, signal=kind, targetName=target, targetEventId=-1,
                    payload=payload, enabled=True)
    route['signals'] = [signal(2, '着水の飛沫', 'ride.splash', deck['name'], ''),
        signal(7, '島からスライム1が落下', 'ride.board', 'Stage1_Coaster_Boarder_0',
               json.dumps(dict(duration=1.15, height=0, falling=True, offset=[-3, 0, 0]))),
        signal(12.5, '島からスライム2が落下', 'ride.board', 'Stage1_Coaster_Boarder_1',
               json.dumps(dict(duration=1.25, height=0, falling=True, offset=[3, 0, 0]))),
        signal(19, '水柱の予兆・噴射', 'ride.waterjet', JET, 'on'),
        signal(23.3, '噴射終了', 'ride.waterjet', JET, 'off')]
    # プリセットから別の場所にも配置でき、接続先はInspectorで設定できる。
    template = copy.deepcopy(jet)
    for key in ('guid', 'position', 'rotation', 'collisionAttributeIds', 'collisionMaskIds'):
        template.pop(key, None)
    template.update(name='水の噴出口', presetDisplayName='水の噴出口（移動床連動）',
                    presetUsage='ギミック', translate=[0, 0, 0], rotate=[0, 0, 0], targetID=-1)
    presets['Stage1/Gimmick/WaterGeyser'] = template
    write(SCENE, scene)
    write(ROUTE, route)
    write(PRESETS, presets)

    # 既存の水滴テクスチャを使い、単発・少数の放出で負荷を限定する。
    particle_dir = ROOT / 'Resources/json/gpu_particles'
    base = read(particle_dir / 'player_base_press_splash.json')
    for name, count, area, speed, life in [('raft_water_landing', 72, [9, .2, 6], 10, 1.1),
                                           ('raft_water_spray', 8, [4, .5, 4], 5, .65)]:
        effect = copy.deepcopy(base)
        effect.update(emitCount=count, emitArea=area, emitVelocity=[0, speed, 0], emitLife=life,
                      baseSize=.3, midSize=.65, endSize=.08, velocityVariance=5,
                      envGravity=[0, -16, 0], shapeType=0, colorIntensity=1.1,
                      baseColor=[.55, .94, 1, .9], midColor=[.8, 1, 1, .75],
                      endColor=[.35, .8, 1, 0], isLooping=False)
        write(particle_dir / (name + '.json'), effect)


def validate():
    # 保存後の実データを検査する。実機での乗船・飛び移り確認とは区別する。
    scene, route = read(SCENE), read(ROUTE)
    objects = {o['name']: o for o in scene['objects']}
    deck = objects['Stage1_Coaster_Deck']
    keys = route['tracks'][0]['keys']
    assert keys[0]['position'] == deck['position']
    assert all(a['time'] < b['time'] for a, b in zip(keys, keys[1:]))
    assert all(s['targetName'] in objects or s['signal'] == 'ride.board' for s in route['signals'])
    assert objects[JET]['targetID'] == deck['myEventID']
    assert sum(o.get('myEventID') == 9480 for o in scene['objects']) == 1
    assert min(k['position'][1] for k in keys) + .65 > -12
    top = max(k['position'][1] for k in keys) + .65
    assert 0 < objects['Stage1_Coaster_StarIslandGroup']['position'][1] - top < 24**2 / (2*50)
    assert deck['collisionMask'] & 4 == 0
    assert Path(ROOT / ('Resources/3DModel/' + deck['modelName'])).is_file()
    print('PASS: route, bindings, water clearance, jump height, model, collision mask')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    if args.apply:
        apply()
    validate()
