"""終盤いかだを左右4組の島と4波の乗り込みへ拡張する一度限りの配置変更。"""
import copy
import json
from pathlib import Path
import uuid

ROOT = Path(__file__).resolve().parents[2]
OBJECTS = ROOT / 'Resources/json/3Dobject/stage1_object.json'
ENEMIES = ROOT / 'Resources/json/3Dobject/stage1_enemy.json'
ROUTE = ROOT / 'Resources/json/scenario/stage1_coaster.json'


def read(path):
    # Windowsの既定コードページに依存せず、日本語の信号名も保持する。
    return json.loads(path.read_text(encoding='utf-8-sig'))


def guid(name):
    # 再生成しても識別子を追跡できるよう、名前から固定GUIDを作る。
    return str(uuid.uuid5(uuid.NAMESPACE_URL, name))


def main():
    # 既存の調整を再実行で重ねない。後の調整は通常のEditorから行う。
    scene, enemies, route = read(OBJECTS), read(ENEMIES), read(ROUTE)
    objects = {o['name']: o for o in scene['objects']}
    assert route['duration'] == 25, '拡張済み・調整済みの航路は上書きしません'
    assert not any(n.startswith('Stage1_Raft_Island_') for n in objects)
    terrain = objects['Stage1_TerrainRoot']
    source = objects['Stage1_TerrainPart_AppearingRelay']
    subtree_ids = {source['guid']}
    while True:
        expanded = subtree_ids | {o['guid'] for o in scene['objects'] if o.get('parentGuid') in subtree_ids}
        if expanded == subtree_ids:
            break
        subtree_ids = expanded
    templates = [copy.deepcopy(o) for o in scene['objects'] if o['guid'] in subtree_ids]

    # 終点の相対配置を保ったまま、ゴール・スター・誘導コイン・周辺地形を90m奥へ移す。
    # 子のローカル座標は変更せず、独立した地形単位を維持する。
    for o in scene['objects'] + enemies['objects']:
        if o['position'][0] >= 740 and o.get('parentGuid', '') in ('', terrain['guid']):
            o['position'][0] += 90

    # 左右の島は1個ずつ独立した親にする。旧足場も同じ土・芝生の島へ揃える。
    old_perch = objects['Stage1_Coaster_EnemyPerch']
    scene['objects'].remove(old_perch)
    islands = []
    for wave, x in enumerate((679, 718, 758, 798)):
        for side, z in (('Left', 16), ('Right', -32)):
            position = [x, (2, 0, 1, 2)[wave], z]
            if wave == 0 and side == 'Left':
                source['position'] = position
                source['scale'] = [.7, 1, .7]
                islands.append(source)
                continue
            name = f'Stage1_Raft_Island_{wave + 1}_{side}'
            root_guid = old_perch['guid'] if wave == 1 and side == 'Right' else guid(name)
            ids = {o['guid']: (root_guid if o['guid'] == source['guid'] else guid(name + '/' + o['name'])) for o in templates}
            names = {o['guid']: (name if o['guid'] == source['guid'] else name + '_' + o['name']) for o in templates}
            for template in templates:
                o = copy.deepcopy(template)
                o['guid'], o['name'] = ids[template['guid']], names[template['guid']]
                if template['guid'] == source['guid']:
                    o['position'], o['scale'] = position, [.7, 1, .7]
                    islands.append(o)
                else:
                    o['parentGuid'] = ids[template['parentGuid']]
                    o['parentName'] = names[template['parentGuid']]
                scene['objects'].append(o)

    # 1体、反対側1体、左右同時2体、左右同時2体。着地点を分けて即重ならないようにする。
    wave_specs = [(0, 7, 679, 2, 1), (1, 14, 718, 0, -1),
                  (2, 21, 758, 1, 1), (3, 21, 758, 1, -1),
                  (4, 28, 798, 2, 1), (5, 28, 798, 2, -1)]
    enemy_map = {o['name']: o for o in enemies['objects']}
    enemy_template = copy.deepcopy(enemy_map['Stage1_Coaster_Boarder_0'])
    board_signals = []
    for index, time, x, top, side in wave_specs:
        name = f'Stage1_Coaster_Boarder_{index}'
        enemy = enemy_map.get(name)
        if enemy is None:
            enemy = copy.deepcopy(enemy_template)
            enemy.update(name=name, guid=guid(name))
            enemies['objects'].append(enemy)
        enemy['position'] = [x, top + .1, -8 + side * 18.5]
        board_signals.append(dict(time=time, name=f'第{(7,14,21,28).index(time)+1}波：{"左" if side>0 else "右"}の島から乗り込み',
            signal='ride.board', targetName=name, targetEventId=-1, enabled=True,
            payload=json.dumps(dict(duration=1.25, height=0, falling=True, offset=[-3 if side>0 else 3, 0, side*3]))))

    # 航路は島の中央を直進する。波の間に約6秒の対処時間を取り、最後の水柱前にも余裕を置く。
    points = [(0, [635,11.35,-14],0), (.65,[635,11.35,-14],1),
              (2,[643,-11.4,-14],2), (2.35,[643,-10.9,-14],4), (2.7,[643,-11.4,-14],0),
              (7,[679,-11.4,-8],0), (14,[718,-11.4,-8],0), (21,[758,-11.4,-8],0),
              (28,[798,-11.4,-8],0), (34,[852.8,-11.4,29],0), (34.8,[852.8,-11.4,29],2),
              (37,[852.8,22,29],0), (38.3,[852.8,22,29],4), (40,[852.8,16.35,13],0)]
    route['duration'] = 40
    route['tracks'][0]['keys'] = [dict(time=t,position=p,rotation=[0,0,0],scale=[1,1,1],easingToNext=e) for t,p,e in points]
    route['tracks'][0]['name'] = 'いかだ：左右の島・4波の乗り込み・水柱・着岸'
    signals = [s for s in route['signals'] if s['signal'] != 'ride.board']
    for s in signals:
        if s['signal'] == 'ride.waterjet':
            s['time'] = 34 if s['payload'] == 'on' else 38.3
    route['signals'] = sorted(signals + board_signals, key=lambda s: s['time'])

    # ファイルへ書く前に参照・同時乗り込み・通路幅・終点を検査する。
    all_objects = scene['objects'] + enemies['objects']
    all_ids = {o['guid'] for o in all_objects}
    assert len(all_ids) == len(all_objects)
    assert len({o['name'] for o in all_objects}) == len(all_objects)
    assert all(not o.get('parentGuid') or o['parentGuid'] in all_ids for o in all_objects)
    all_names = {o['name'] for o in all_objects}
    assert all(s['targetName'] in all_names for s in route['signals'])
    assert [sum(s['time']==t for s in board_signals) for t in (7,14,21,28)] == [1,1,2,2]
    assert 24 - 11*.7 - 6.85 > 9  # 島の縁といかだ側面の間隔。
    assert abs(objects['Stage1_TerrainPart_AppearingMainLanding']['position'][0]-12-(852.8+10.054)) < .2
    assert objects['Stage1_Raft_WaterJet']['position'][0] == 852.8
    assert objects['goal']['position'][0] == 940

    backup = ROOT / 'output/raft-expansion/before'
    backup.mkdir(parents=True, exist_ok=True)
    for path, data in ((OBJECTS,scene),(ENEMIES,enemies),(ROUTE,route)):
        (backup/path.name).write_bytes(path.read_bytes())
        path.write_text(json.dumps(data,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    print('PASS: 8 independent islands, 6 enemies / 4 waves, paired arrivals, 40s route, goal +90m')


if __name__ == '__main__':
    main()
