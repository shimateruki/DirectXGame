"""旧C++演出を編集可能なキーへ移す一度限りの変換。Editor編集後は上書きしない。"""
import argparse
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Resources/json/scenario/crown_defeat.json"
T = json.loads((ROOT / "Resources/json/scenario/crown_defeat_tuning.json").read_text(encoding="utf-8-sig"))
D, R, S, E, V = (T[x] for x in ("duration", "rupture", "shrink", "settle", "victory"))
PI = math.pi


def clamp(x): return max(0, min(1, x))
def ease(x): x = clamp(x); return x*x*(3-2*x)
def mix(a, b, t): return [x+(y-x)*t for x, y in zip(a, b)]


def key(time, p=(0, 0, 0), r=(0, 0, 0), s=(1, 1, 1), c=(1, 1, 1, 1), em=1,
        height=0, bounce=0, visible=True, clip=False):
    return [time, *p, *r, *s, *c, em, height, bounce, int(visible), int(clip)]


def reduce_keys(rows, tolerances, discrete=()):
    """各成分の誤差を検査し、不要な中間キーだけを除去する。"""
    keep = {0, len(rows)-1}
    stack = [(0, len(rows)-1)]
    while stack:
        first, last = stack.pop()
        a, b = rows[first], rows[last]
        worst, at = 1, None
        for i in range(first+1, last):
            t = (rows[i][0]-a[0])/(b[0]-a[0])
            error = max((abs(rows[i][j]-(a[j]+(b[j]-a[j])*t))/tol
                         for j, tol in enumerate(tolerances, 1) if j not in discrete), default=0)
            if any(rows[i][j] != a[j] for j in discrete): error = float("inf")
            if error > worst: worst, at = error, i
        if at is not None:
            keep.add(at); stack.extend(((first, at), (at, last)))
    return [rows[i] for i in sorted(keep)]


special = [0, .03*D, .065*D, .07*D, .09*D, R*D, S*D, E*D, V*D, (V+.11)*D, D]
times = sorted({min(D, max(0, x+offset)) for x in special for offset in (0, -.00001, .00001)} |
               {i/240 for i in range(int(D*240)+1)} | {D})
objects = []
original_samples = {}


def add(name, model, sample, **options):
    original_samples[name]=sample
    rows = [sample(t) for t in times]
    visible=[i for i,k in enumerate(rows) if k[17]]
    if visible:
        # 非表示後の軌道を保存し続けず、最後の見える姿勢と表示境界だけを残す。
        for i in range(visible[-1]+2,len(rows)):
            rows[i]=[rows[i][0],*rows[visible[-1]+1][1:]]
        for i in range(max(0,visible[0]-1)):
            rows[i]=[rows[i][0],*rows[max(0,visible[0]-1)][1:]]
    reduced = reduce_keys(rows, [.001]*3+[.003]*3+[.001]*3+[.003]*4+[.008, .003, .003, 1, 1], (17, 18))
    objects.append(dict(name=name, model=model, source="$Actor", keys=reduced, clipCenterOffset=-.52, **options))


def body(t):
    n=t/D; shrink=ease((n-S)/(E-S)); recoil=math.sin(clamp((t-.12)/.65)*PI)*.14
    charge=ease((n-.07)/(R-.07)); panic=math.sin(t*24)*.015*charge if n<R else 0
    flop=math.sin(clamp((n-E)/.06)*PI); size=1-.83*shrink
    return key(t, r=(panic, 0, recoil*.35), s=(size*(1+recoil+flop*.35), size*(1-recoil-flop*.28), size),
               c=(.78+.22*shrink, .65+.35*shrink, 1, 1))


def cracks(t):
    n=t/D; charge=ease((n-.07)/(R-.07)); pulse=.7+.3*math.sin(t*(8+charge*13))
    return key(t, c=(1, .35+.55*charge, 1, charge*pulse), em=1+charge*4*T['energy'],
               height=3.05+1.55*charge, visible=n>.065 and n<R, clip=True)


add("defeat_body", "Characters/false_king_slime/false_king_defeat_body.obj", body)
add("defeat_cracks", "Characters/false_king_slime/false_king_defeat_cracks.obj", cracks,
    additive=True, lit=False, clipScaleHeight=True, clipEdgeWidth=.045, clipDissolve=.07, clipColor=[1,.7,1,1])

parts=json.loads((ROOT / "Resources/3DModel/Characters/false_king_slime/false_king_defeat_parts.json").read_text())
for index, part in enumerate(parts):
    def sample(t, i=index, gem=part['gem']):
        n=t/D; charge=ease((n-.07)/(R-.07)); age=max(0,t-R*D); dissolve=ease((age-2)/1.7)
        if n>=R:
            travel=min(age,1.65); spin=min(age,1.6)
            p=((5.8 if gem else 4.1)*T['spread']*travel,2.4*age-5.4*age*age,0)
            r=(spin*(1.2+i*.07),spin*(1.5 if i%2 else -1.2),spin*.7)
        else: p=(.04*charge*math.sin(t*30+i),0,0); r=(0,0,0)
        return key(t,p=p,r=r,em=1+charge*.15*T['energy']*(n<R),height=5-10*dissolve,
                   bounce=.16*abs(math.sin(age*13))*math.exp(-age*1.1),visible=dissolve<.999,clip=dissolve>0)
    add(f"defeat_part_{index}",part['model'],sample,positionSpace="radial",pivot=part['pivot'],
        floorHeight=.18,clipColor=[.9,.5,1,1])


def ring(t):
    age=max(0,t-R*D); radius=1+age*23
    return key(t,s=(radius,.9,radius),c=(.9,.55,1,(1-ease(age/.85))*.8),em=2,visible=t>=R*D and age<.85)


add("defeat_ring","Characters/false_king_slime/false_king_pressure_front.obj",ring,
    pivot=[0,3.05,0],inheritScale=False,inheritRotation=False,additive=True,lit=False)
for index in range(24):
    def ray(t,i=index):
        age=max(0,t-R*D); local=age-(i%3)*.025; life=.35+(i%4)*.045
        fade=1-ease(local/life)
        return key(t,r=(.5+(i%5)*.43,i*2.399963,0),s=(fade,(2+i%7)*(.4+.6*ease(local/.06)),fade),
                   c=(.85,.62,1,fade),em=2.5*T['energy'],visible=t>=R*D and 0<=local<life)
    add(f"defeat_ray_{index}","Characters/false_king_slime/false_king_defeat_ray.obj",ray,
        pivot=[0,3.05,0],inheritScale=False,inheritRotation=False,additive=True,lit=False)

add("$Gate","",lambda t:key(t,p=(0,math.sin(clamp((t/D-V)/.11)*PI)*1.1,0),c=(1,.77,.25,1)),
    spawn=False,source_override="$Gate")
# source_overrideは変換スクリプトの便宜上の項目。実行形式には含めない。
objects[-1]['source']=objects[-1].pop('source_override')
add("charge_orbit","",lambda t:key(t,p=(math.cos(t*5)*(1-ease((t/D-.07)/(R-.07))*.45)*4,1,
    math.sin(t*5)*(1-ease((t/D-.07)/(R-.07))*.45)*4),visible=False),pivot=[0,3.05,0])
add("charge_body","",lambda t:key(t,p=(0,body(t)[8]*1.3,0),visible=False),positionSpace="model")
add("victory_sparkles","",lambda t:key(t,p=(0,1+math.sin(clamp((t/D-V)/.11)*PI)*1.1,0),visible=False),
    source_override="$Gate")
objects[-1]['source']=objects[-1].pop('source_override')


def view(t):
    n=t/D; shrink=ease((n-S)/(E-S)); finish=ease((n-V)/.09)
    offset=[5,4,13]; scaled=[0,3,0]; focus=[0,0,0]; blend=0
    if n<R: offset=mix([7,3,19],offset,ease((n-.03)/(R-.03)))
    elif n<S: offset=[9,7,20]
    else:
        scaled=[0,3*(1-shrink),0]; focus=[0,.65*shrink,0]
        offset=mix([9,7,20],[3,2.3,7],shrink)
    if n>=V:
        blend=finish; scaled=[x*(1-finish) for x in scaled]
        focus=mix(focus,[0,1+math.sin(clamp((n-V)/.11)*PI)*1.1,0],finish)
        offset=mix(offset,[1.8,1.5,7],finish)
    offset=[x*T['cameraDistance'] for x in offset]
    shake=(math.exp(-max(0,t-R*D)*6)*.22 if n>=R else 0)*T['shake']
    return [t,blend,*focus,*scaled,*offset,math.sin(t*93)*shake,math.sin(t*117)*shake,0,.62,.10*ease(t/.2)]


view_rows=[view(t) for t in times]
view_keys=reduce_keys(view_rows,[.0005]+[.002]*6+[.003]*6+[.001,.001])


def particles(target,preset,start,end,scale,direction=(0,1,0),mode="fixed"):
    # 旧実装の絶対時刻9Hzと発火区間を一致させる。
    return dict(target=target,preset=preset,startTime=(math.floor(start*9)+1)/9,
        endTime=(math.ceil(end*9)-1)/9,interval=1/9,scale=scale,direction=list(direction),directionMode=mode)


asset=json.loads((ROOT / "Resources/json/scenario/crown_defeat_vfx.json").read_text())
asset.update(version=7,name="crown_defeat",duration=D,
    roleBindings=[dict(targetName="Stage3_HighCrown_FalseKingSlime",targetEventId=-1),
                  dict(targetName="Stage3_HighCrown_Player",targetEventId=-1),dict(targetName="",targetEventId=-1)],
    previewModels=["Characters/false_king_slime/false_king_defeat_body.obj","Characters/slime_crown/slime_crown_body.gltf"])
asset['vfxCues'][0]['startTime']=R*D
asset['vfxCues'][0]['rootScale']=[T['energy']]*3
asset['vfxCues'][1].update(startTime=(V+.11)*D,targetName="$Gate")
asset['signals']=[dict(time=ratio*D,name=name,signal=name,payload="",enabled=True) for name,ratio in
                  [('rupture',R),('shrink',S),('settle',E),('victory',V)]]
asset['presentation']=dict(reference="$Actor",lookToward="$Gate",hideObjects=["$Actor"],objects=objects,view=view_keys,
    particles=[particles("charge_orbit","crown_defeat_charge",.09*D,R*D,.16*T['energy'],(0,-.4,0),"radialIn"),
               particles("charge_body","crown_defeat_charge",S*D,E*D,.2*T['energy']),
               particles("victory_sparkles","goal_clear_gold_motes",V*D,D,.35)])

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--force',action='store_true')
    args=parser.parse_args()
    if OUT.exists() and not args.force: raise SystemExit('Editorでの編集保護のため既存素材は上書きしません。')
    # 読みやすいメタデータとコンパクトな数値キーを両立する。
    text=json.dumps(asset,ensure_ascii=False,indent=2)
    import re
    text=re.sub(r'\[\s*([-+\d][\d.eE+\-,\s]*)\]',lambda m:'['+re.sub(r'\s+','',m[1])+']',text)
    # 通常JSONへ戻せることを必ず確認する。
    try: json.loads(text)
    except ValueError: text=json.dumps(asset,ensure_ascii=False,separators=(',',':'))
    OUT.write_text(text+'\n',encoding='utf-8')
    print(f'{len(objects)} clips / {sum(len(o["keys"]) for o in objects)} visual keys / {len(view_keys)} camera keys / {OUT.stat().st_size:,} bytes')
