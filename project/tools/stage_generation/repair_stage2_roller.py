"""円柱の描画と軽量な衝突形状を分離し、配置軸と回転軸を親子で分ける。"""
import copy
import json
import math
import uuid
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def main():
    # 保存済みシーンを先に退避し、他の地形や手編集には触れない。
    scene = ROOT/'Resources/json/3Dobject/stage2_object.json'
    raw = scene.read_bytes()
    data = json.loads(raw.decode('utf-8-sig'))
    name = 'Stage2_Route_11_RollingBasalt'
    roller = next(o for o in data['objects'] if o['name'] == name)
    assert not any(o['name'] == name+'_Mount' for o in data['objects'])
    backup = ROOT/'backups/stage2'/datetime.now().strftime('%Y%m%d_%H%M%S_before_roller_repair')
    backup.mkdir(parents=True)
    (backup/scene.name).write_bytes(raw)
    # 24角の閉じた円柱。描画用の細かい凹凸を衝突形状へ取り込まない。
    folder = ROOT/'Resources/3DModel/Gimmicks/basalt_roller_collision'
    folder.mkdir(exist_ok=True)
    lines = ['o BasaltRollerCollision']
    for x in (-4,4):
        for i in range(24):
            a = i*math.tau/24
            lines.append(f'v {x} {1.5*math.cos(a):.8f} {1.5*math.sin(a):.8f}')
    for i in range(24):
        j=(i+1)%24
        lines.append(f'f {i+1} {j+1} {j+25} {i+25}')
    lines += ['f '+' '.join(str(i) for i in range(24,0,-1)),
              'f '+' '.join(str(i) for i in range(25,49))]
    (folder/'basalt_roller_collision.obj').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    def basic(suffix):
        o=copy.deepcopy(roller)
        o.update(name=name+suffix,guid=str(uuid.uuid4()),type='Model',gimmickType='',components={},
                 recorder=dict(recordPathName='',isRecordLoop=False,isRecordRelative=False),
                 collisionAttribute=0,collisionMask=0,isStatic=False)
        o.pop('param',None)
        o['collider']=dict(type=3,center=[0,0,0],size=[4,1.5,1.5],rotation=[0,0,0])
        return o
    mount=basic('_Mount')
    mount.update(modelName='Gimmicks/basalt_roller_collision/basalt_roller_collision.obj',isVisible=False,scale=[1,1,1])
    mount['translate']=mount['position'];mount['rotate']=mount['rotation']
    visual=basic('_Collision')
    visual.update(position=[0,0,0],translate=[0,0,0],rotation=[0,0,0],rotate=[0,0,0],
                  quaternion=[0,0,0,1],scale=[1,1,1],parentName=name,parentGuid=roller['guid'],
                  modelName=mount['modelName'],isVisible=False,collisionAttribute=4,collisionMask=15)
    visual['collider'].update(useModelVolume=True,useModelSurface=True)
    roller['collider']=dict(type=3,center=[0,0,0],size=[4,1.5,1.5],rotation=[0,0,0])
    roller.update(isVisible=True,collisionAttribute=0,collisionMask=0,position=[0,0,0],translate=[0,0,0],
                  rotation=[0,0,0],rotate=[0,0,0],quaternion=[0,0,0,1],
                  parentName=mount['name'],parentGuid=mount['guid'])
    data['objects'] += [mount,visual]
    assert scene.read_bytes()==raw
    scene.write_text(json.dumps(data,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    print('Backup:',backup)

if __name__=='__main__':
    main()
