"""ステージ2の前半を、独立編集できる足場と寄り道へ組み直す。"""
import argparse
import copy
import hashlib
import json
import math
from datetime import datetime
from pathlib import Path
import uuid

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / 'Resources/json/3Dobject/stage2_object.json'
PREFIX = 'Stage2_Route_'


def read(path):
    # BOM付きの既存Editor保存にも対応する。
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    # 明示的な適用時だけSceneを書き換え、毎回復元用の原本を保存する。
    parser = argparse.ArgumentParser()
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    original = SCENE.read_bytes()
    scene = read(SCENE)
    assert not any(o['name'].startswith(PREFIX) for o in scene['objects']), 'Already placed: edit in Editor instead of regenerating'
    presets = read(ROOT / 'Resources/json/preset/presets.json')
    old = read(ROOT / 'backups/stage2/20261019_060758_before_preboss_removal/stage2_object.json')['objects']
    lookup = {o['name']: o for o in scene['objects']}
    old_lookup = {o['name']: o for o in old}
    added, surfaces, links, motions = [], {}, [], {}

    def clone(template, suffix, pos, scale=None, yaw=0):
        # 全足場を独立Rootにし、旗・砲台など付属品だけを子にする。
        o = copy.deepcopy(template)
        for k in list(o):
            if k.startswith('preset') or k == 'builtinCreateTemplate':
                del o[k]
        o.update(name=PREFIX + suffix, guid=str(uuid.uuid4()), position=list(pos),
                 rotation=[0, yaw, 0], quaternion=[0, math.sin(yaw/2), 0, math.cos(yaw/2)],
                 parentName='', parentGuid='', myEventID=-1, targetID=-1, eventID=0,
                 layer='Default', tag='', isStatic=False, saveCategory='Object')
        if scale is not None:
            o['scale'] = list(scale)
        o.pop('components', None)
        o['recorder'] = dict(isRecordLoop=False, isRecordRelative=False, recordPathName='')
        added.append(o)
        return o

    def collision(o, attribute=4, ids=None):
        # 数値ビットと名前の両方を揃え、読込時の優先順位に依存させない。
        o.update(collisionAttribute=attribute, collisionAttributeIds=ids or ['ground'],
                 collisionAttributeUnknownBits=0, collisionMask=1 if attribute==128 else 255,
                 collisionMaskIds=['player'] if attribute==128 else
                 ['player','enemy','ground','player_bullet','trigger','map_block','player_attack','enemy_attack'],
                 collisionMaskUnknownBits=0)

    def mark(o, width, length, yaw=0):
        # 到達性確認と配置図は、実際に出力するTransformから作る。
        surfaces[o['name']] = dict(position=o['position'], width=width, length=length, yaw=yaw)
        return o

    def safe(suffix, pos, width=18, length=18):
        # 安定した足場は既存の玄武岩。模型全体の高さと凸輪郭で側面も塞ぐ。
        o=clone(lookup['Stage2_MagmaEntryRock'], suffix, pos, [width/2.13, 2.4, length/1.97])
        o['isStatic']=True
        # 既存岩のVolume設定を維持し、専用の不可視判定は重ねない。
        collision(o)
        return mark(o,width,length)

    def floor(suffix, pos, width, length, yaw, kind):
        # 長い床は既存PBRモデルを使い、崩壊床にはひび割れモデルを割り当てる。
        if kind=='crumble':
            o=clone(presets['Stage2/Gimmick/CrumblingRock'], suffix,pos,[width/3.9,1,length/3.9],yaw)
            o['param'].update(shakeDuration=0.95, returnOnOff=True)
        else:
            key='wide' if width>=7 else 'long'
            w,l=(7,12) if key=='wide' else (4,14)
            o=clone(presets['Stage2/Gimmick/SinkingBasalt_'+key],suffix,pos,[width/w,1,length/l],yaw)
            o.update(gimmickType='SinkingFloor',type='Gimmick')
            o['param'].update(gimmickType='SinkingFloor',actionMode=1,shakeDuration=0.7,
                              speed=0.85,moveAmount=8,interval=2.5,returnOnOff=True)
        collision(o)
        return mark(o,width,length,yaw)

    def link(a,b,kind='main'):
        # 各ジャンプを検証するため、進行方向を保存する。
        links.append(dict(a=a['name'],b=b['name'],kind=kind))

    def checkpoint(parent, suffix):
        # 旗だけを足場の子にし、足場を編集したときに一緒に移動する。
        o=clone(lookup['Stage2_PostBoss_Checkpoint'],suffix,[0,0,0])
        o.update(parentName=parent['name'],parentGuid=parent['guid'],eventID=4)
        o['scale']=[v/s for v,s in zip(o['scale'],parent['scale'])]
        return o

    def coin(parent, suffix, offset=(0,2.4,0)):
        # 安定した着地点と分岐の方向を少数のコインで示す。
        o=clone(lookup['Stage2_StartCoin_01'],suffix,offset)
        o.update(parentName=parent['name'],parentGuid=parent['guid'],eventID=lookup['Stage2_StartCoin_01']['eventID'])
        o['position']=[v/s for v,s in zip(offset,parent['scale'])]
        o['scale']=[v/s for v,s in zip(o['scale'],parent['scale'])]

    def connect(a,b,suffix,kind='sink',width=9,gap=6):
        # 長い区間も同じ間隔の点列にせず、踊り場と方向転換を挟む。
        ap,bp=a['position'],b['position'];dx,dz=bp[0]-ap[0],bp[2]-ap[2]
        distance=math.hypot(dx,dz);ux,uz=dx/distance,dz/distance;yaw=math.atan2(ux,uz)
        def extent(obj):
            f=surfaces[obj['name']];rel=math.atan2(ux,uz)-f['yaw']
            # 進行線が実際に矩形境界を出る距離。投影半径だと角で隙間を見落とす。
            return min(f['width']/2/max(abs(math.sin(rel)),1e-8),f['length']/2/max(abs(math.cos(rel)),1e-8))*.88
        ea,eb=extent(a),extent(b)
        n=max(1,math.ceil((distance-ea-eb)/(20 if kind=='crumble' else 30)))
        length=(distance-ea-eb-gap*(n+1))/n
        assert length>3,(suffix,length)
        previous=a
        for i in range(n):
            t=ea+gap+length/2+i*(length+gap)
            pos=[ap[0]+ux*t,ap[1]+(bp[1]-ap[1])*t/distance,ap[2]+uz*t]
            o=floor(f'{suffix}_{i+1:02}',pos,width,length,yaw,kind)
            link(previous,o);previous=o
            if i%2==0:coin(o,f'{suffix}_Coin_{i+1:02}')
        link(previous,b)

    def mortar(suffix,pos):
        # 射程を区間内に絞り、休憩地点や他の砲台と攻撃が重なり続けないようにする。
        base=safe(suffix+'_Shelf',pos,9,9)
        # 砲口の計算がローカルScaleを使用するため、砲台は等倍Rootで置く。
        o=clone(old_lookup['Stage2_Ferry_Mortar_1'],suffix,pos)
        o['param'].update(detectionRange=34,interval=3.8,shakeDuration=1.5,healAmount=2.2)
        return o

    def bar(suffix,parent,yaw=0,lift=False):
        # 既存Playerのenemy_attack処理を使う。見た目だけの棘にしない。
        o=clone(presets['Stage2/Gimmick/VolcanicSpikeBar'],suffix,
                [parent['position'][0],parent['position'][1]+1.2,parent['position'][2]], [1,1,1],yaw)
        collision(o,128,['enemy_attack'])
        path='stage2_route_'+suffix.lower()
        frames=[]
        for i in range(240):
            t=i/240*math.tau
            offset=[0,3.5*(1-math.cos(t))/2,0] if lift else [math.cos(yaw)*4*math.sin(t),0,-math.sin(yaw)*4*math.sin(t)]
            frames.append(dict(pos=offset,rot=[0,0,0],scale=[1,1,1],eventID=0))
        motions[path]=dict(frames=frames,genParams=dict(generateRelative=True))
        o['recorder']=dict(isRecordLoop=True,isRecordRelative=True,recordPathName=path)
        o['components']={'PathMover':dict(_editorPresent=True,version=1,path=path,loop=True,relative=True,playbackMode='always',rideSequence='')}
        return o

    # 入口から噴射岩へ。広い沈む床→折り返し→崩れる足場の順に単体で覚える。
    start=lookup['Stage2_StartFoundation'];mark(start,82,55,start['rotation'][1])
    entry=lookup['Stage2_MagmaEntryRock'];mark(entry,19,18)
    a=safe('01_Landing',[-365,-5,-60]);b=safe('02_Turn',[-325,-4.7,-8])
    c=safe('03_Rest',[-273,-4.4,46]);e=safe('04_GeyserApproach',[-232,-4.15,105])
    connect(start,a,'01_Warmup',width=12)
    connect(a,b,'02_Sinking',width=9)
    connect(b,c,'03_Crumbling','crumble',width=8)
    connect(c,e,'04_Offset',width=9)
    connect(e,entry,'05_Entry',width=10,gap=5)
    checkpoint(e,'Checkpoint_Geyser')

    # スター1の高台から本線へ。噴射岩の既存配置は動かさない。
    balcony=lookup['Stage2_StarBalcony_01'];mark(balcony,35,29,balcony['rotation'][1])
    b=safe('10_Star1Exit',[-205,-2.7,220],22,20)
    c=safe('11_MortarExit',[-130,-3,195],20,20)
    d=safe('12_TurnRest',[-65,-3,115],22,22)
    e=safe('13_Star2Junction',[20,-3,60],24,24)
    f=safe('14_BarLanding',[90,-3,5],12,24)
    g=safe('15_BarRest',[160,-2.7,-65],16,24)
    h=safe('16_PreBoss',[210,-2,-135],22,22)
    connect(balcony,b,'10_Return',width=10)
    connect(b,c,'11_MortarRun','crumble',width=9)
    connect(c,d,'12_CornerRun','crumble',width=7)
    connect(d,e,'13_SinkingRun',width=10)
    connect(e,f,'14_Timing',width=8)
    connect(f,g,'15_Timing',width=8)
    connect(g,h,'16_FinalRun','crumble',width=8)
    boss=lookup['Stage2_UpperForge'];mark(boss,90,65,boss['rotation'][1])
    link(h,boss)
    checkpoint(b,'Checkpoint_AfterStar1');checkpoint(e,'Checkpoint_Star2');checkpoint(h,'Checkpoint_PreBoss')
    mortar('Mortar_A',[-166,-2.8,237]);mortar('Mortar_B',[-85,-3,171])
    bar('Bar_Slide',f,yaw=-.7);bar('Bar_Lift',g,yaw=-.7,lift=True)

    # スター2は本線から外れた上りルート。細い崩壊足場で着地を要求する。
    branch=[floor('Star2_Sink',[20,-3,88],6,14,0,'sink'),
            floor('Star2_CrumbleA',[36,-1,108],6,6,0,'crumble'),
            floor('Star2_CrumbleB',[56,1,125],6,6,0,'crumble'),
            safe('Star2_Reward',[76,3,144],14,14)]
    prev=e
    for i,o in enumerate(branch):
        link(prev,o,'star2');prev=o
        if i<3:coin(o,'Star2_Guide_'+str(i))
    star=clone(old_lookup['Stage2_StarCoin_02'],'StarCoin_02',[0,3,0])
    star.update(name='Stage2_StarCoin_02',parentName=branch[-1]['name'],parentGuid=branch[-1]['guid'],eventID=7,targetID=1)
    star['position']=[0,3/branch[-1]['scale'][1],0]
    star['scale']=[v/s for v,s in zip(star['scale'],branch[-1]['scale'])]
    star['components']={'GameplayLink':dict(_editorPresent=True,version=1,eventId=-1,targetId=1)}
    # 帰路は7m下に降りる一方通行。逆走してスターへ直接飛び乗れない。
    returns=[safe('Star2_ReturnA',[105,-4,129],14,14),safe('Star2_ReturnB',[102,-4,100],14,14),
             safe('Star2_ReturnC',[78,-3.5,78],16,16),safe('Star2_ReturnD',[48,-3,64],16,16)]
    for o in returns:
        link(prev,o,'return');prev=o
    link(prev,e,'return')

    # 同じ高さでの理論飛距離より十分短い隙間を採用する。実操作試験とは区別する。
    status=read(ROOT/'Resources/json/gameplay/status_settings.json')['player']
    speed,jump,gravity=status['speed'],status['jumpPower'],status['gravity']
    for edge in links:
        a,b=surfaces[edge['a']],surfaces[edge['b']]
        dx,dz=b['position'][0]-a['position'][0],b['position'][2]-a['position'][2]
        distance=math.hypot(dx,dz);theta=math.atan2(dx,dz)
        def radius(s):
            angle=theta-s['yaw']
            return min(s['width']/2/max(abs(math.sin(angle)),1e-8),s['length']/2/max(abs(math.cos(angle)),1e-8))*.88
        gap=max(0,distance-radius(a)-radius(b))
        rise=b['position'][1]-a['position'][1]
        discriminant=jump*jump-2*gravity*rise
        assert discriminant>0,edge
        reach=speed*(jump+math.sqrt(discriminant))/gravity
        edge.update(gap=round(gap,2),rise=round(rise,2),ballisticReach=round(reach,2))
        assert gap+2<reach*.9,(edge,'insufficient landing margin')
    all_objects=scene['objects']+added
    assert len({o['name'] for o in all_objects})==len(all_objects)
    assert len({o['guid'] for o in all_objects})==len(all_objects)
    names={o['name'] for o in all_objects}
    assert all(not o.get('parentName') or o['parentName'] in names for o in all_objects)
    assert sum(o.get('eventID')==7 and o.get('targetID')==1 for o in all_objects)==1
    report=dict(added=len(added),surfaces=surfaces,links=links,motions=list(motions),
                physics=dict(speed=speed,jump=jump,gravity=gravity),runtimeVerified=False)
    output=ROOT/'output/stage2-route';output.mkdir(parents=True,exist_ok=True)
    (output/'layout.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    (output/'proposed_objects.json').write_text(json.dumps({'objects':added},ensure_ascii=False,indent=4),encoding='utf-8')
    if args.apply:
        backup=ROOT/'backups/stage2'/datetime.now().strftime('%Y%m%d_%H%M%S_before_route_layout')
        backup.mkdir(parents=True,exist_ok=False)
        (backup/SCENE.name).write_bytes(original)
        assert (backup/SCENE.name).read_bytes()==original
        for name,data in motions.items():
            path=ROOT/'Resources/json/animation'/f'{name}.json'
            assert not path.exists(),path
            path.write_text(json.dumps(data,separators=(',',':')),encoding='utf-8')
        assert SCENE.read_bytes()==original,'Scene edited concurrently'
        scene['objects']=all_objects
        SCENE.write_text(json.dumps(scene,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
        (backup/'manifest.json').write_text(json.dumps(dict(sha256=hashlib.sha256(original).hexdigest(),addedNames=[o['name'] for o in added],motions=list(motions)),indent=2),encoding='utf-8')
        print('Backup:',backup.name)
    print('Added:',len(added),'surfaces:',len(surfaces),'validated jumps:',len(links),'max gap:',max(x['gap'] for x in links))


if __name__=='__main__':
    main()
