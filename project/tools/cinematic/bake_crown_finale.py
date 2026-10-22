"""旧継承演出の装飾とカメラをDirectorへ移す初回変換。編集済み素材は上書きしない。"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from bake_crown_defeat import key, reduce_keys, ease, clamp, mix

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Resources/json/scenario/crown_finale.json'
defaults = (ROOT / 'game/crown/CrownFinaleTuning.h').read_text(encoding='utf-8-sig')
T = {k: float(v) for k, v in re.findall(r'float (\w+) = ([\d.]+)f;', defaults)}
T.update(json.loads((ROOT / 'Resources/json/scenario/crown_finale_tuning.json').read_text(encoding='utf-8-sig')))
D = T['movieDuration']
B, L, R, P, A, BODY, C, W, E, LAND = (T[k] for k in (
    'movieFragmentBreakTime', 'movieFragmentLandTime', 'movieRageTime', 'movieFragmentPullTime',
    'movieFragmentAbsorbTime', 'movieBodyTime', 'movieCrownTime', 'movieWingsTime', 'movieEscapeTime', 'movieArriveTime'))
points = [0, .005, .035, .04, B, L, R, R+.04, R+.07, P, A, BODY, C-.012, C, W-.012, W, E, LAND, .96, 1]
times = sorted({i/240 for i in range(round(D*240)+1)} |
               {clamp(n+delta)*D for n in points for delta in (0, -.000001, .000001)} | {D})
objects = []
original_samples = {}


def add(name, model, sample, source='boss', **options):
    original_samples[name] = sample
    rows = [sample(t) for t in times]
    visible=[i for i,k in enumerate(rows) if k[17]]
    if visible:
        for i in range(visible[-1]+2,len(rows)): rows[i]=[rows[i][0],*rows[visible[-1]+1][1:]]
        for i in range(max(0,visible[0]-1)): rows[i]=[rows[i][0],*rows[max(0,visible[0]-1)][1:]]
    rows = reduce_keys(rows, [.001]*3+[.003]*3+[.001]*3+[.003]*4+[.008, .003, .003, 1, 1], (17, 18))
    objects.append(dict(name=name, model=model, source=source, keys=rows, **options))


# 欠片の地面探索と操作用フックはゲーム固有。所有・描画・寿命だけ既存再生器へ任せる。
objects.append(dict(name='fragment', model='Characters/false_king_slime/false_king_crown_emblem.obj',
                    source='boss', animate=False, keys=[key(0)]))
objects.append(dict(name='seal_anchor', model='', source='boss', animate=False, keys=[key(0)]))
objects.append(dict(name='release_anchor', model='', source='boss', animate=False, keys=[key(0)]))
add('last_seal', 'Effects/false_king_seal_core',
    lambda t: key(t, c=(.86,.32,1,1), em=1.5, height=3-6*ease((t-.08)/.48), visible=t<.58, clip=True),
    source='seal_anchor', followPosition=True, inheritScale=False, inheritRotation=False,
    clipCenterOffset=-.52, clipEdgeWidth=.10, clipDissolve=.18, clipColor=[1,.85,1,1])
add('fragment_crack', 'Characters/false_king_slime/false_king_crown_emblem_crack.obj',
    lambda t: key(t, em=6, visible=.04 <= t/D < B), source='fragment',
    followPosition=True, followRotation=True, followScale=True)
for i in range(5):
    def chip(t, i=i):
        age=(t/D-B)*D
        angle=i*2.3999632
        return key(t, p=(math.cos(angle)*age*2.5, age*(1.6+i*.18)-4.9*age*age,math.sin(angle)*age*2.5),
                   r=(age*3, angle+age*4, age*2), visible=0<=age<1.3)
    add(f'fragment_chip_{i}', 'Characters/false_king_slime/false_king_crown_emblem_chip.obj', chip,
        source='release_anchor', inheritRotation=False)


def shockwave(t):
    n=t/D; p=clamp((n-R)/.07)
    return key(t, p=(0,.18,0), s=(1+p*47,.42+math.sin(p*math.pi)*.7,1+p*40),
               c=(1,.91,.76,(1-p)*.85), em=3, visible=R<=n<R+.07)


add('shockwave', 'Characters/false_king_slime/false_king_pressure_front.obj', shockwave,
    inheritScale=False, inheritRotation=False, additive=True)
add('inner_rune', 'Characters/false_king_slime/crown_inheritance_inner_rune.obj',
    lambda t: key(t, r=(0,-math.pi,0), c=(1,.8,.24,1), em=2+2*math.sin(t*7)**2, visible=A<=t/D<C),
    source='player', followPosition=True, followRotation=True, lit=False, additive=True)
for name, model, start, end, low, high, size, color in (
    ('gold_body','Characters/slime_crown/slime_crown_body.gltf',BODY,C-.012,-.1,1.65,1.002,(1,.77,.25,1)),
    ('regalia','Characters/slime_crown/slime_crown_regalia.gltf',C,W-.012,0,2.2,1,(1,.68,.12,1))):
    add(name, model,
        lambda t, a=start,b=end,lo=low,hi=high,s=size,c=color: key(t, s=(s,s,s), c=c,
            height=lo+(hi-lo)*ease((t/D-a)/(b-a)), visible=a<=t/D<b, clip=ease((t/D-a)/(b-a))<.999),
        source='player', followPosition=True, followRotation=True,
        clipCenterOffset=-.52, clipEdgeWidth=.035, clipDissolve=.055, clipColor=[1,.85,.3,1])
for name, side in (('left',-1),('right',1)):
    def wing(t, side=side):
        n=t/D; opened=ease((n-W)/(E-W-.02)); fold=1-ease((n-LAND)/max(.02,1-LAND))
        flap=math.sin((n-E)*D*9) if E<=n<LAND else 0
        size=T['movieWingScale']*fold
        return key(t, p=(0,.35,0), r=(0,0,side*((1-opened)*1.15+flap*.22)), s=(size,size,size),
                   em=1+(1-opened)*1.6, height=-.1+2.9*opened, visible=n>=W and fold>.001, clip=opened<.999)
    add(f'{name}_wing', f'Characters/false_king_slime/crown_inheritance_wing_{name}.obj', wing,
        source='player', followPosition=True, followRotation=True, rotationMask=[0,1,0], lit=False,
        clipCenterOffset=-.52, clipEdgeWidth=.04, clipDissolve=.06, clipColor=[1,.9,.4,1])
for i in range(64):
    x,z=divmod(i,8)
    if ((x<=1 or x>=6) and z in (0,7)) or i in (1,6,62): continue
    add(f'floor_crack_{i}', f'Characters/false_king_slime/crown_inheritance_cracks_{i:02}.obj',
        lambda t: key(t, em=1+1.5*math.exp(-(t/D-R)*12), visible=t/D>=R),
        source=f'CrownFracture_{i}', followPosition=True, followRotation=True, inheritVisibility=True, lit=False)


def view(t):
    n=t/D; source=other='player_start'; blend=0; offset=[0,0,0]
    distance=T['movieFragmentCameraDistance']
    if n<.035: source=other='last_seal'; eye=[3.2,1,5.5]
    elif n<B: source=other='fragment'; eye=[2.4*distance,.7*distance,4.8*distance]
    elif n<L:
        source=other='fragment'; eye=[x*distance for x in mix([2.4,.7,4.8],[2.6,1.2,4.2],ease((n-B)/(L-B)))]
    elif n<R: source=other='fragment_ground'; eye=[2.5*distance,distance,4*distance]
    elif n<R+.04: source=other='boss_start'; offset=[0,1.8,0]; eye=[13,7,24]
    elif n<P: source='player_start';other='fragment_ground';blend=.45;offset=[0,.8*.55,0];eye=[5,2.6,7]
    elif n<A: source='mouth';other='fragment_ground';blend=.38;eye=[4*distance,1.8*distance,5.8*distance]
    elif n<W: offset=[0,1,0];eye=None
    elif n<E: offset=[0,1.3,0];eye=[-7,3,9]
    elif n<LAND: source=other='player';offset=[0,.9,0];eye=mix([-14,7,24],[-9,5,15],ease((n-E)/(LAND-E)))
    else: source='destination';other='final_arena_focus';blend=.5*ease((n-LAND)/(1-LAND));offset=[0,1.2*(1-blend),0];eye=[8,10,32]
    # 吸収カットの目線方向はゲーム側の位置Resolverから受ける。固定カメラObjectを増やさない。
    eye_reference='transform_eye' if eye is None else ''
    if eye is None: eye=[0,0,0]
    impact=.25*math.exp(-(n-R)*48) if n>=R else 0
    tremor=.055 if R<=n<E else 0
    shake=(impact+tremor)*T['movieShake']
    jitter=[math.sin(t*47)*shake,math.sin(t*61)*shake*.65,0]
    offset=[x+y*.35 for x,y in zip(offset,jitter)]
    bars=.075*ease(n/.04)*(1-ease((n-.96)/.04))
    return [t,blend,*offset,0,0,0,*eye,*[v*(1 if eye_reference else .65) for v in jitter],.58,bars,source,other,eye_reference]


# 同じ補間器を再利用し、対象名の切替境界だけは数値キーを減らさない。
raw=[view(t) for t in times]
view_keys=[]; first=0
for i in range(1,len(raw)+1):
    if i<len(raw) and raw[i][16:]==raw[first][16:]: continue
    rows=reduce_keys([r[:16] for r in raw[first:i]], [.00001]+[.001]*12+[.0001,.0001]) if i-first>1 else [raw[first][:16]]
    view_keys.extend([r+raw[first][16:] for r in rows]); first=i
asset=json.loads((ROOT/'Resources/json/scenario/crown_finale_vfx.json').read_text(encoding='utf-8-sig'))
asset.update(version=8,name='crown_finale',duration=D)
asset['signals']=[dict(time=n*D,name=name,signal=name,payload='',enabled=True) for name,n in zip(
    ('break','land','rage','pull','absorb','body','crown','wings','escape','arrive'),(B,L,R,P,A,BODY,C,W,E,LAND))]
asset['presentation']=dict(reference='player',lookToward='boss',viewWorldSpace=True,
                           objects=objects,view=view_keys,particles=[])
asset['roleBindings']=[dict(targetName='Stage3_HighCrown_FalseKingSlime',targetEventId=-1),
                       dict(targetName='Stage3_HighCrown_Player',targetEventId=-1),dict(targetName='',targetEventId=-1)]
# 通常のDirectorでも役割を解決できるよう、本番の役割名に統一する。
asset['presentation']['reference']='$Gate';asset['presentation']['lookToward']='$Actor'
for o in objects:
    o['source']={'boss':'$Actor','player':'$Gate'}.get(o['source'],o['source'])
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--force',action='store_true');args=parser.parse_args()
    if OUT.exists() and not args.force: raise SystemExit('Editor編集の保護のため既存素材は上書きしません。')
    text=json.dumps(asset,ensure_ascii=False,indent=2)
    text=re.sub(r'\[\s*([-+\d][^\[\]]*?)\s*\]',
                lambda m:json.dumps(json.loads(m.group(0)),ensure_ascii=False,separators=(',',':')),text)
    OUT.write_text(text+'\n',encoding='utf-8')
    print(f'{len(objects)} clips / {sum(len(o["keys"]) for o in objects)} visual keys / {len(view_keys)} camera keys')
