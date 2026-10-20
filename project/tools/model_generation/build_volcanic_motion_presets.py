"""岩の配置プリセットとGhostDirector用の相対移動素材を追加する。"""
import copy
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    # 対象名はEditorで割り当て直せる。相対座標なので配置位置を原点へ飛ばさない。
    motions = {
        'slide': ('左右往復', [(0,[0,0,0],[0,0,0]),(2,[0,0,6],[0,0,0]),(4,[0,0,0],[0,0,0])]),
        'lift': ('上下往復', [(0,[0,0,0],[0,0,0]),(1.5,[0,3,0],[0,0,0]),(3,[0,0,0],[0,0,0])]),
        'spin': ('水平回転', [(i,[0,0,0],[0,i*math.pi/2,0]) for i in range(5)]),
        'swing': ('振り子', [(i/8,[0,0,0],[0,0,.7*math.sin(i*math.tau/32)])
            for i in range(33)]),
    }
    for key,(label,poses) in motions.items():
        name = 'volcanic_spike_bar_'+key
        data = dict(version=6, name=name, duration=poses[-1][0], tracks=[dict(
            name='棘バー：'+label, targetName='VolcanicSpikeBar', targetEventId=-1,
            startTime=0, enabled=True, muted=False, relative=True, holdLast=True,
            keys=[dict(time=t,position=p,rotation=r,scale=[1,1,1],easingToNext=3 if key in ('slide','lift') else 0)
                  for t,p,r in poses])])
        # 回転は90度ずつ記録し、Quaternion補間で一周が消える問題を避ける。
        (ROOT/'Resources/json/scenario'/f'{name}.json').write_text(
            json.dumps(data,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    path=ROOT/'Resources/json/preset/presets.json'
    presets=json.loads(path.read_text(encoding='utf-8-sig'))
    rock=copy.deepcopy(presets['Builtin/Gimmick/ChainCollapseFloor'])
    rock.pop('builtinCreateTemplate',None)
    rock.update(name='崩れる岩ブロック',presetDisplayName='崩れる岩（踏むとひび割れ）',
        presetUsage='ギミック',modelName='Gimmicks/crumbling_rock_block_intact',
        blendMode=0,color=[1,1,1,1],scale=[1,1,1],roughness=.85,isStatic=False)
    rock['param'].update(gimmickType='ChainCollapseFloor',shakeDuration=.6,returnOnOff=True)
    rock['collider'].update(center=[0,-.475,0],size=[1.95,.475,1.95])
    presets.setdefault('Stage2/Gimmick/CrumblingRock',rock)
    bar=copy.deepcopy(rock)
    bar.update(name='VolcanicSpikeBar',presetDisplayName='棘バー（Director動作用）',
        type='Model',gimmickType='',modelName='Gimmicks/volcanic_spike_bar',
        collisionAttribute=0,collisionMask=0,metallic=.5,roughness=.42)
    bar.pop('param',None)
    bar['collider'].update(center=[0,0,0],size=[4.32,.91,.91])
    presets.setdefault('Stage2/Gimmick/VolcanicSpikeBar',bar)
    path.write_text(json.dumps(presets,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
