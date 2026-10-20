"""終盤の斜めに焼き込まれた足場を、親の回転でいかだと平行に揃える。"""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / 'Resources/json/3Dobject/stage1_object.json'


def main():
    # 親子構造を利用し、芝生・道・当たり判定を別々に動かさない。
    scene = json.loads(SCENE.read_text(encoding='utf-8-sig'))
    objects = {o['name']: o for o in scene['objects']}
    angles = {'AppearingStart': math.atan2(3, 5), 'AppearingRelay': math.atan2(3, 5),
              'AppearingMainLanding': .2, 'GoalApproach': .2}
    for label, angle in angles.items():
        parent = objects['Stage1_TerrainPart_' + label]
        assert parent['rotation'] == [0, 0, 0], '調整済みの親回転は上書きしません'
        parent['rotation'] = [0, angle, 0]
        parent['quaternion'] = [0, math.sin(angle/2), 0, math.cos(angle/2)]
        # 旧一体モデルに残っていた0.6mの横ずれも、分割済みの側面・道だけ補正する。
        detail = objects.get(parent['name'] + '_Detail')
        if detail:
            assert detail['position'] == [0, 0, 0]
            detail['position'][0] = -.6000000238418579
        collider = objects['Stage1_Collision_V4_' + label]
        assert abs(collider['rotation'][1] + angle) < 1e-5

        # 芝面の4辺がワールドX/Zに平行になることをモデル頂点から確認する。
        turf = objects[parent['name'] + '_Turf']
        vertices = []
        for line in (ROOT / ('Resources/3DModel/' + turf['modelName'])).read_text(encoding='utf-8-sig').splitlines():
            if line.startswith('v '):
                x,y,z = map(float,line.split()[1:]);z=-z
                vertices.append((x*math.cos(angle)+z*math.sin(angle),
                                 -x*math.sin(angle)+z*math.cos(angle)))
        xs = sorted(set(round(v[0],4) for v in vertices))
        zs = sorted(set(round(v[1],4) for v in vertices))
        assert len(xs)==2 and len(zs)==2, (label,xs,zs)
        size = collider['collider']['size']
        assert abs(xs[-1]-xs[0]-size[0]*2)<1e-3
        assert abs(zs[-1]-zs[0]-size[2]*2)<1e-3
        print(label, 'aligned', xs, zs)

    # 乗船面・着岸面といかだの高さ、左右の縁の間隔を確認する。
    deck = objects['Stage1_Coaster_Deck']
    start = objects['Stage1_TerrainPart_AppearingStart']['position']
    half = deck['collider']['size'][0]
    assert abs(deck['position'][1]+.65-start[1])<1e-4
    assert 0 < deck['position'][0]-half-(start[0]+18) < 2
    route=json.loads((ROOT/'Resources/json/scenario/stage1_coaster.json').read_text(encoding='utf-8-sig'))
    end=route['tracks'][0]['keys'][-1]['position']
    landing=objects['Stage1_TerrainPart_AppearingMainLanding']['position']
    assert abs(end[1]+.65-landing[1])<1e-4
    assert 0 < landing[0]-12-(end[0]+half) < .2
    assert end[2]==landing[2]
    SCENE.write_text(json.dumps(scene,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    print('PASS: parallel edges, collider alignment, boarding and landing clearance')


if __name__ == '__main__':
    main()
