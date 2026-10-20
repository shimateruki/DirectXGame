"""前半を長い沈降岩・円柱・高低差のある棘区間に改修する。一度だけ適用する。"""
import argparse
import copy
import hashlib
import json
import math
from datetime import datetime
from pathlib import Path
import uuid

ROOT=Path(__file__).resolve().parents[2]
SCENE=ROOT/'Resources/json/3Dobject/stage2_object.json'
PREFIX='Stage2_Route_'
SEA=-8.0  # MagmaSeaの原点-10 + モデル頂点Y=1×ScaleY=2。

def read(path):
    # Editorが出力するBOM付きJSONも読めるようにする。
    return json.loads(path.read_text(encoding='utf-8-sig'))

def main():
    # 前案以降の手編集があれば上書きせず止まる。バックアップはResources外に置く。
    parser=argparse.ArgumentParser();parser.add_argument('--apply',action='store_true');args=parser.parse_args()
    original=SCENE.read_bytes();scene=read(SCENE)
    previous=read(ROOT/'output/stage2-route/proposed_objects.json')['objects']
    lookup={o['name']:o for o in scene['objects']}
    assert all(lookup.get(o['name'])==o for o in previous),'Previous route was edited; merge manually'
    removed={o['name'] for o in previous}
    preserved=[o for o in scene['objects'] if o['name'] not in removed]
    templates=read(ROOT/'Resources/json/preset/presets.json')
    added=[];surfaces={};links=[];motions={}

    def clone(template,name,pos,scale=(1,1,1),yaw=0):
        # 足場は独立Root。コインと旗だけを各足場の子にする。
        o=copy.deepcopy(template)
        for k in list(o):
            if k.startswith('preset') or k in ('components','builtinCreateTemplate'):o.pop(k)
        o.update(name=PREFIX+name,guid=str(uuid.uuid4()),position=list(pos),scale=list(scale),rotation=[0,yaw,0],
                 quaternion=[0,math.sin(yaw/2),0,math.cos(yaw/2)],parentName='',parentGuid='',eventID=0,myEventID=-1,targetID=-1)
        o['recorder']=dict(recordPathName='',isRecordLoop=False,isRecordRelative=False)
        added.append(o);return o

    def mark(o,w,l,yaw=0,kind='rock'):
        # 検証・配置図にも、出力した座標と寸法を使う。
        surfaces[o['name']]=dict(position=o['position'],width=w,length=l,yaw=yaw,kind=kind)
        return o

    def rock(name,pos,w=18,l=18,yaw=0):
        # 高い足場も溶岩まで柱を伸ばす。上面だけが浮く配置を作らない。
        o=clone(lookup['Stage2_MagmaEntryRock'],name,pos,(w/2.13,(pos[1]-SEA+1.5)/1.59,l/1.97),yaw)
        o['isStatic']=True
        return mark(o,w,l,yaw)

    def sink(name,pos,w,l,yaw=0,triangle=False):
        # 表面から1.6m出し、底面は必ずマグマ内に入れる。細切れではなく走破距離を作る。
        pos=[pos[0],SEA+1.6,pos[2]]
        o=clone(templates['Stage2/Gimmick/SinkingBasalt_wide'],name,pos,(w/7,2,l/12),yaw)
        if triangle:
            o.update(modelName='Gimmicks/sinking_basalt_triangle/sinking_basalt_triangle_pbr.gltf',scale=[w/12,1,l/20])
            o['collider']=dict(type=3,center=[0,-1.5,0],size=[6,1.5,10],rotation=[0,0,0],useModelVolume=True)
        o.update(type='Gimmick',gimmickType='SinkingFloor',isStatic=False)
        # 沈み始めてから飛び直しても時計は止まらない。失敗後は自動で戻る。
        duration=max(2.8,l/20+1.2)
        o['param'].update(gimmickType='SinkingFloor',actionMode=1,shakeDuration=.65,speed=1.6/duration,
                          moveAmount=4.5,interval=1.5,returnOnOff=True)
        return mark(o,w,l,yaw,'triangle' if triangle else 'sink')

    def edge(a,b,kind='main'):
        # スター分岐も含め、隣接する足場の到達性を後で確認する。
        links.append(dict(a=a['name'],b=b['name'],kind=kind))

    def span(a,b,name,kind='rock',w=12):
        # 両端の地形との間隔を取り、一枚の長い足場としてつなぐ。
        dx=b['position'][0]-a['position'][0];dz=b['position'][2]-a['position'][2]
        distance=math.hypot(dx,dz);yaw=math.atan2(dx,dz)
        def radius(o):
            s=surfaces[o['name']];t=yaw-s['yaw']
            return min(s['width']/2/max(abs(math.sin(t)),1e-8),s['length']/2/max(abs(math.cos(t)),1e-8))*.85
        ra,rb=radius(a),radius(b);length=distance-ra-rb-8
        t=(ra+4+length/2)/distance
        p=[a['position'][i]+(b['position'][i]-a['position'][i])*t for i in range(3)]
        o=sink(name,p,w,length,yaw) if kind=='sink' else rock(name,p,w,length,yaw)
        edge(a,o);edge(o,b);return o

    def stairs(a,b,name,count=3,w=12):
        # 一段の上昇量を通常ジャンプ以下にし、上り下りの着地に変化を付ける。
        dx=b['position'][0]-a['position'][0];dz=b['position'][2]-a['position'][2]
        distance=math.hypot(dx,dz);yaw=math.atan2(dx,dz);prev=a
        for i in range(1,count+1):
            t=i/(count+1);p=[a['position'][j]+(b['position'][j]-a['position'][j])*t for j in range(3)]
            o=rock(name+str(i),p,w,max(10,distance/(count+1)-5),yaw);edge(prev,o);prev=o
        edge(prev,b)

    def child(template,name,parent,offset,event):
        # 親の拡大率を相殺し、旗やコインの見た目を変えずに追従させる。
        o=clone(template,name,[v/s for v,s in zip(offset,parent['scale'])],[v/s for v,s in zip(template['scale'],parent['scale'])])
        o.update(parentName=parent['name'],parentGuid=parent['guid'],eventID=event)
        return o

    def path(o,name,frames,mode='always'):
        # EditorのPathMoverで再生条件と軌道を再調整できる形式にする。
        name='stage2_revised_'+name
        motions[name]=dict(frames=frames,genParams=dict(generateRelative=True))
        o['recorder']=dict(recordPathName=name,isRecordLoop=True,isRecordRelative=True)
        o['components']={'PathMover':dict(version=1,_editorPresent=True,path=name,loop=True,relative=True,playbackMode=mode,rideSequence='')}

    def bar(name,parent,mode,yaw=0):
        # 既存の敵攻撃属性を使う。回転・往復・昇降・振り子は別の判断を要求する。
        p=parent['position'];o=clone(templates['Stage2/Gimmick/VolcanicSpikeBar'],name,[p[0],p[1]+1.8,p[2]],yaw=yaw)
        o.update(collisionAttribute=128,collisionAttributeIds=['enemy_attack'],collisionAttributeUnknownBits=0,
                 collisionMask=1,collisionMaskIds=['player'],collisionMaskUnknownBits=0)
        frames=[]
        for i in range(360):
            t=i/360*math.tau;pos=[0,0,0];rot=[0,0,0]
            if mode=='slide':pos=[math.cos(yaw)*5*math.sin(t),0,-math.sin(yaw)*5*math.sin(t)]
            elif mode=='lift':pos[1]=4*(1-math.cos(t))/2
            elif mode=='spin':rot[1]=t
            else:
                # 棒の片端を支点にする。原点回転だけのシーソーにしない。
                angle=.85*math.sin(t);rot[2]=angle;pos=[4*(1-math.cos(angle)),-4*math.sin(angle),0]
            frames.append(dict(pos=pos,rot=rot,scale=o['scale'],eventID=0))
        path(o,name.lower(),frames);return o

    start=lookup['Stage2_StartFoundation'];mark(start,82,55,start['rotation'][1])
    a=rock('01_Landing',[-365,-6.4,-60]);b=rock('02_Turn',[-325,-6.4,-8],22,22)
    c=rock('03_Rest',[-273,-2,46]);d=rock('04_GeyserApproach',[-232,-4.15,105],24,24)
    span(start,a,'01_StoneCauseway',w=16)
    span(a,b,'02_LongSinkingRun','sink',w=14)
    stairs(b,c,'03_RisingColumns',3)
    ramp=span(c,d,'04_LiftGateCauseway',w=15);bar('Bar_Lift',ramp,'lift',surfaces[ramp['name']]['yaw'])
    entry=lookup['Stage2_MagmaEntryRock'];mark(entry,19,18)
    span(d,entry,'05_GeyserSpur',w=11)
    # 噴射岩は元のまま。スター1の高台を本線の経由点にしない。
    junction=rock('10_BypassLanding',[-155,-6.4,100],22,22)
    span(d,junction,'10_LongBasaltRun','sink',w=13)
    e=rock('11_RollerExit',[-100,-2,72],20,20)
    # 円柱は長軸を進路に合わせ、下部を溶岩へ沈める。乗車中だけゆっくり回る。
    dx=e['position'][0]-junction['position'][0];dz=e['position'][2]-junction['position'][2]
    yaw=math.atan2(-dz,dx);length=math.hypot(dx,dz)-25
    roller=clone(templates['Stage2/Gimmick/SinkingBasalt_long'],'11_RollingBasalt',[-127.5,-6.8,86],(length/8,2,2),yaw)
    roller.update(modelName='Gimmicks/basalt_roller/basalt_roller_pbr.gltf',gimmickType='MovingFloor',type='Gimmick')
    roller['param'].update(gimmickType='MovingFloor',actionMode=0)
    roller['collider']=dict(type=3,center=[0,0,0],size=[4,1.5,1.5],rotation=[0,0,0],useModelVolume=True,useModelSurface=True)
    path(roller,'roller',[dict(pos=[0,0,0],rot=[i/720*math.tau,0,0],scale=roller['scale'],eventID=0) for i in range(720)],'whileRiding')
    mark(roller,6,length,math.atan2(dx,dz),'roller');surfaces[roller['name']]['position']=[-127.5,-3.8,86]
    edge(junction,roller);edge(roller,e)
    f=rock('12_HighTerrace',[-55,4,35],22,22);stairs(e,f,'12_Climb',3)
    g=rock('13_Star2Junction',[20,4,30],24,24)
    bridge=span(f,g,'13_SpinBridge',w=18);bar('Bar_Spin',bridge,'spin')
    h=rock('14_BarLanding',[90,-1.5,5],20,24);stairs(g,h,'14_DescendingColumns',3,w=14)
    bar('Bar_Slide',h,'slide',-.7)
    i=rock('15_LowRest',[160,-6.4,-65],22,22)
    # 長い岩の途中を三角形で折り返す。各岩は独立編集・独立沈降する。
    tri=sink('15_TriangleTurn',[117,-6.4,-20],24,30,math.pi/4,True)
    edge(h,tri);span(tri,i,'15_FinalLongSink','sink',w=12)
    j=rock('16_PreBoss',[210,-2,-135],22,22);stairs(i,j,'16_RisingColumns',3,w=14)
    bar('Bar_Swing',added[-1],'swing')
    boss=lookup['Stage2_UpperForge'];mark(boss,90,65,boss['rotation'][1]);edge(j,boss)
    for n,parent in [('Geyser',d),('Roller',junction),('Star2',g),('PreBoss',j)]:
        child(lookup['Stage2_PostBoss_Checkpoint'],'Checkpoint_'+n,parent,(0,.05,0),4)
    # スター2は高所の細い崩壊岩へ自分で入る挑戦。帰路から逆走して取れない高さにする。
    prev=g
    for n,p in enumerate([(15,7,65),(30,10,95)]):
        o=clone(templates['Stage2/Gimmick/CrumblingRock'],'Star2_Crumble'+str(n),p,(2.6,(p[1]-SEA+1)/.95,4))
        o['param'].update(shakeDuration=.8,returnOnOff=True)
        mark(o,10,15,kind='crumble');edge(prev,o,'star2');prev=o
    reward=rock('Star2_Reward',[55,13,120],18,18);edge(prev,reward,'star2')
    star=child(lookup['Stage2_StarCoin_02'],'StarCoin_02',reward,(0,3,0),7)
    star.update(name='Stage2_StarCoin_02',targetID=1)
    star['components']={'GameplayLink':dict(_editorPresent=True,version=1,eventId=-1,targetId=1)}
    prev=reward
    for n,p in enumerate([(85,5,116),(89,1,80),(91,-1.5,45)]):
        o=rock('Star2_Return'+str(n),p,16,18);edge(prev,o,'return');prev=o
    edge(prev,h,'return')
    # 砲撃は高台の分岐前に一台だけ。待機場所と沈降区間に攻撃を重ねない。
    shelf=rock('Mortar_Shelf',[-10,4,57],10,10)
    cannon=clone(lookup['Stage2_Route_Mortar_A'],'Mortar',shelf['position'])
    cannon['param'].update(detectionRange=34,interval=4.4,shakeDuration=1.6,healAmount=2.2)
    for n,parent in enumerate([a,b,c,d,junction,e,f,g,h,i,j,reward]):
        child(lookup['Stage2_StartCoin_01'],'GuideCoin'+str(n),parent,(0,2.4,0),lookup['Stage2_StartCoin_01']['eventID'])

    # 静的な飛距離検査。動く床・敵を含む実プレイの難度検査とは区別する。
    physics=read(ROOT/'Resources/json/gameplay/status_settings.json')['player']
    for link in links:
        a,b=surfaces[link['a']],surfaces[link['b']]
        dx,dz=b['position'][0]-a['position'][0],b['position'][2]-a['position'][2]
        angle=math.atan2(dx,dz)
        def radius(s):
            t=angle-s['yaw'];factor=.65 if s['kind']=='triangle' else .85
            return min(s['width']/2/max(abs(math.sin(t)),1e-8),s['length']/2/max(abs(math.cos(t)),1e-8))*factor
        gap=max(0,math.hypot(dx,dz)-radius(a)-radius(b));rise=b['position'][1]-a['position'][1]
        discriminant=physics['jumpPower']**2-2*physics['gravity']*rise
        assert discriminant>0,(link,'too high',rise)
        reach=physics['speed']*(physics['jumpPower']+math.sqrt(discriminant))/physics['gravity']
        link.update(gap=round(gap,2),rise=round(rise,2),reach=round(reach,2))
        assert gap+2<reach*.95,(link,'gap too long')
    all_objects=preserved+added;names={o['name'] for o in all_objects}
    assert len(names)==len(all_objects)
    assert len({o['guid'] for o in all_objects})==len(all_objects)
    assert all(not o.get('parentName') or o['parentName'] in names for o in all_objects)
    for o in added:
        if o.get('gimmickType')=='SinkingFloor':
            half=o['collider']['size'][1]*o['scale'][1]
            assert o['position'][1]-2*half<SEA<o['position'][1],o['name']
    output=ROOT/'output/stage2-route-revised';output.mkdir(exist_ok=True,parents=True)
    report=dict(surfaces=surfaces,links=links,added=len(added),preserved=len(preserved),seaSurface=SEA,runtimeVerified=False)
    (output/'layout.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    (output/'proposed_objects.json').write_text(json.dumps({'objects':added},ensure_ascii=False,indent=4),encoding='utf-8')
    if args.apply:
        backup=ROOT/'backups/stage2'/datetime.now().strftime('%Y%m%d_%H%M%S_before_varied_route')
        backup.mkdir(parents=True);(backup/SCENE.name).write_bytes(original)
        assert (backup/SCENE.name).read_bytes()==original
        for name,data in motions.items():
            target=ROOT/'Resources/json/animation'/f'{name}.json';assert not target.exists()
            target.write_text(json.dumps(data,separators=(',',':')),encoding='utf-8')
        assert SCENE.read_bytes()==original,'Scene edited concurrently'
        scene['objects']=all_objects;SCENE.write_text(json.dumps(scene,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
        (backup/'manifest.json').write_text(json.dumps(dict(sha256=hashlib.sha256(original).hexdigest(),removed=sorted(removed),added=sorted(o['name'] for o in added)),indent=2),encoding='utf-8')
        print('Backup:',backup)
    print('Objects:',len(all_objects),'added:',len(added),'jumps:',len(links))

if __name__=='__main__':
    main()
