"""現行のステージ3を保持し、闘技場・入口・支持構造だけを一度拡張する。"""
from __future__ import annotations
import json
import sys
from copy import deepcopy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/model_generation'))
from generate_ring_burner_model import ObjBuilder
from generate_stage3_high_crown import write_obj
from generate_stage3_high_crown_background import add_elliptic_frustum
from generate_stage3_high_crown import add_elliptic_cylinder
from generate_stage3_high_crown import STAGE_DIR
import uuid

SCENE = ROOT / 'Resources/json/3Dobject/stage3_object.json'
PLAYER = ROOT / 'Resources/json/3Dobject/stage3_player.json'
REVISION = 'crownArenaExpansionV1'
SX, SZ, SHIFT = 1.4, 1.3, 10.2


def build_support_models():
    # 城塞本体と階段橋脚を分け、脱出先には城塞だけを複製できるようにする。
    foundation = ObjBuilder()
    for y,top_x,top_z,bottom_x,bottom_z,height in (
        (-8.0,40.5,34.5,36,30,11), (-19,36,30,28,23,11),
        (-32,28,23,16,13,15), (-44,16,13,8,6,9)):
        add_elliptic_frustum(foundation,'StoneDark','Stone',(0,y,0),top_x,top_z,bottom_x,bottom_z,height,64)
    for y,rx,rz in ((-3.0,40.7,34.7),(-13.4,36.5,30.5),(-24.4,28.4,23.4)):
        add_elliptic_cylinder(foundation,'StoneLight','StoneLight',(0,y,0),rx,rz,.35,64)
    support = ObjBuilder()
    # 上端を階段本体の底へ差し込み、全段が同じ橋台へ載る形にする。
    support.box('StoneDark',(0,-10.55,5),(6.65,7.5,18.5))
    for z in (-11,-3,5,13,21):
        for side in (-1,1):
            support.box('Stone',(side*6.4,-21,z),(1.0,18.1,1.65))
            support.box('StoneLight',(side*6.4,-3.28,z),(1.15,.30,1.90))
            support.box('Gold',(side*7.42,-10,z),(.06,1.2,.42))
        support.box('StoneLight',(0,-16.5,z),(6.55,.36,1.3))
    for name,model in (('high_crown_battle_foundation',foundation),('high_crown_stair_support',support)):
        write_obj(model,STAGE_DIR/(name+'.obj'),'high_crown.mtl',name)


def new_object(template, name):
    item = deepcopy(template)
    item['name'] = name
    item['guid'] = str(uuid.uuid5(uuid.NAMESPACE_URL, 'cg2-stage3:'+name))
    item['parentGuid'] = item['parentName'] = ''
    return item


def refine():
    scene = json.loads(SCENE.read_text(encoding='utf-8'))
    player = json.loads(PLAYER.read_text(encoding='utf-8'))
    if scene.get(REVISION):
        print('Stage expansion already applied; current editor edits preserved.')
        return
    objects = scene['objects']
    by_name = {o['name']:o for o in objects}
    required = ['Stage3_HighCrown_ArenaVisual','Stage3_HighCrown_EntranceGate',
                'Stage3_HighCrown_ApproachStairs','Stage3_HighCrown_Background_Foundation',
                'Stage3_HighCrown_Background_EntranceCitadel']
    assert all(name in by_name for name in required), '必要なステージ3の配置がありません'
    assert by_name[required[0]]['scale'] == [1,1,1], '既に拡張済みの地形を二重変更しないでください'
    for obj in objects:
        name = obj['name']
        if 'ArenaFloor_' in name:
            obj['position'][0] *= SX
            obj['position'][2] *= SZ
            obj['collider']['size'][0] *= SX
            obj['collider']['size'][2] *= SZ
        elif name == 'Stage3_HighCrown_ArenaVisual' or name == 'Stage3_HighCrown_Background_Foundation':
            obj['scale'][0] *= SX
            obj['scale'][2] *= SZ
        elif '_Wall_' in name or '_DecorTower_' in name:
            obj['position'][0] *= SX
            obj['position'][2] *= SZ
            if '_Wall_' in name:
                obj['scale'][0] *= 1.4
                obj['scale'][2] *= 1.4
        elif 'Approach' in name or 'BossGate' in name or 'EntranceGate' in name or 'PreBoss_CopyMemoryStation' in name:
            obj['position'][2] += SHIFT
        elif name == 'Stage3_HighCrown_Background_EntranceCitadel':
            obj['position'][2] += SHIFT
            obj['position'][1] -= .78
    by_name['Stage3_HighCrown_Background_Foundation']['modelName'] = 'Stages/high_crown/high_crown_battle_foundation.obj'
    gate = by_name['Stage3_HighCrown_EntranceGate']
    gate['modelName'] = 'Gimmicks/crown_stage_gate'
    gate['materialType'] = 22
    gate['scale'] = [1.35,1.35,1.35]
    gate['position'][1] = -2.42 + 1.12*1.35
    gate['emissive'] = 1.65
    gate['collider']['center'] = [0,-.65,0]
    gate['collider']['size'] = [.9,1.15,.22]
    # 背景だけだった前庭を実際に歩ける床にする。上面は見た目の石床と一致。
    floor_template = next(o for o in objects if 'ApproachLanding' in o['name'])
    courtyard = new_object(floor_template,'[Collision] Stage3_HighCrown_EntranceCourtyard')
    courtyard['position'] = [0,0,74+SHIFT]
    courtyard['collider']['center'] = [0,-3.42,0]
    courtyard['collider']['size'] = [18.85,1,16.85]
    objects.append(courtyard)
    visual_template = by_name['Stage3_HighCrown_ApproachStairs']
    support = new_object(visual_template,'Stage3_HighCrown_StairSupport')
    support['modelName'] = 'Stages/high_crown/high_crown_stair_support.obj'
    objects.append(support)
    for obj in player['objects']:
        obj['position'][2] += SHIFT
    scene[REVISION] = {'width':112,'depth':88.4,'entranceShift':SHIFT}
    # 変更前に全参照・衝突床を検証し、未知のオブジェクトはそのまま保持する。
    names = [o['name'] for o in objects]
    assert len(names) == len(set(names))
    assert len([o for o in objects if 'ArenaFloor_' in o['name']]) == 13
    assert gate['param']['targetScene'] == 'SELECT'
    assert all(o.get('guid') for o in objects)
    SCENE.write_text(json.dumps(scene,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    PLAYER.write_text(json.dumps(player,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    print('Expanded stage 3: 112 x 88.4 m, entrance restored, supported stairs and walkable courtyard')


if __name__ == '__main__':
    build_support_models()
    refine()
