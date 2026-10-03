"""祠をボム専用のひび入りガラスに変更し、手前の広場にボマーを配置する。"""
import copy
import json
from pathlib import Path
import shutil
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[2]
SCENES = ROOT / 'Resources/json/3Dobject'


def main():
    objects_path = SCENES / 'stage1_object.json'
    enemies_path = SCENES / 'stage1_enemy.json'
    objects = json.loads(objects_path.read_text(encoding='utf-8'))
    enemies = json.loads(enemies_path.read_text(encoding='utf-8'))
    before = copy.deepcopy(objects)
    gate = next(o for o in objects['objects'] if o['name'] == 'Stage1_V4_ImpactBreakGate')
    backup = Path(tempfile.mkdtemp(prefix='cg2-vault-bomb-'))
    for p in (objects_path, enemies_path):
        shutil.copy2(p, backup / p.name)
    # actionMode=0は爆風のみ。6/7のプレイヤー衝撃による破壊を有効にしない。
    gate['param']['actionMode'] = 0
    gate.update(materialType=10, color=[.55, .84, .90, .10], emissive=1,
                texturePath='', normalMapPath='', ormMapPath='', enableNormalMap=False,
                roughness=.12, metallic=0, castShadow=False)
    gate['waterParam'].update(effectType=1, effectScale=.65, effectIntensity=.6,
                             effectScaleX=7.2, effectScaleY=4.2, effectScaleZ=1.5)
    name = 'Stage1_StarShrine_Bomber'
    if not any(o['name'] == name for o in enemies['objects']):
        sample = json.loads((SCENES / 'sample_enemy.json').read_text(encoding='utf-8'))
        enemy = copy.deepcopy(next(o for o in sample['objects'] if o.get('enemyType') == 'Bomber'))
        enemy.update(name=name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL, name)),
                     position=[-187, 6.1, -55], rotation=[0, 0, 0], quaternion=[0, 0, 0, 1],
                     parentName='', parentGuid='')
        enemies['objects'].append(enemy)
    # サンプルの大きな仮判定ではなく、実ステージの同じモデルの判定を使う。
    stage2 = json.loads((SCENES / 'stage2_object.json').read_text(encoding='utf-8'))
    reference = next(o for o in stage2['objects'] if o.get('enemyType') == 'Bomber')
    enemy = next(o for o in enemies['objects'] if o['name'] == name)
    for key in ('collider', 'collisionAttribute', 'collisionMask'):
        enemy[key] = copy.deepcopy(reference[key])
    for old, new in zip(before['objects'], objects['objects']):
        if old['name'] != gate['name']:
            assert old == new, old['name']
        else:
            for key in ('position', 'scale', 'collider', 'guid'):
                assert old[key] == new[key], key
    objects_path.write_text(json.dumps(objects, ensure_ascii=False, indent=4) + '\n', encoding='utf-8')
    enemies_path.write_text(json.dumps(enemies, ensure_ascii=False, indent=4) + '\n', encoding='utf-8')
    print('Backup:', backup)
    print('Bomb-only glass configured; nearby Bomber placed; other objects unchanged')


if __name__ == '__main__':
    main()
