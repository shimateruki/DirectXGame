"""スター2：ボムで進路を開通し、浮き岩を乗り継ぐ寄り道。座標は低床化後。"""
import copy
import json
import math
import uuid
from pathlib import Path

PREFIX = 'Stage2_Ferry_'
ASSET_ROOT = Path(__file__).resolve().parents[3] / 'Resources'


def sequences():
    # 停止位置は壁より手前。GhostDirectorで移動キーと破壊待ち信号を編集できる。
    routes = [
        [(0,(140,-3,-160)),(1,(140,-3,-160)),(7,(108,-3,-180)),
         (10,(108,-3,-180)),(15,(90,-3,-207)),(18,(90,-3,-234)),
         (24,(55,-3,-250)),(27,(55,-3,-250)),(33,(80,-3,-280)),(38,(108,-3,-290))],
        [(0,(164,-3,-290)),(1,(164,-3,-290)),(8,(195,-3,-320)),
         (11,(195,-3,-320)),(17,(208,-3,-360)),(23,(244,-3,-341)),
         (26,(244,-3,-341)),(32,(268,-3,-307)),(38,(260,-3,-279))],
    ]
    result = {}
    for i, points in enumerate(routes, 1):
        name = PREFIX + 'Raft_' + str(i)
        signals = [dict(time=0, name='乗り場に戻ると呼び戻せる浮き岩', signal='ride.ferry', payload='', targetName=name, targetEventId=-1, enabled=True)]
        signals.append(dict(time=15 if i==1 else 0, name='岩を破壊して開通' if i==1 else '出航スイッチ待ち',
            signal='ride.wait_clear' if i==1 else 'ride.wait_switch',payload='',
            targetName=PREFIX+('Barrier_1' if i==1 else 'DepartureSwitch'),targetEventId=-1,enabled=True))
        for j,t in enumerate((7,24) if i==1 else (8,23)):
            signals.append(dict(time=t+.03,name='停船中の着弾予告・横へ回避',signal='ride.fireball',
                payload='{"offsetX":0}',targetName=PREFIX+'Fireball_'+str((i-1)*2+j+1),targetEventId=-1,enabled=True))
        result[f'stage2_ferry_{i}.json'] = dict(version=6, name=f'スター2・浮き岩{i}', duration=points[-1][0],
            tracks=[dict(name='乗車経路（岩の上面が高さの基準）', targetName=name, targetEventId=-1,
                startTime=0, enabled=True, muted=False, relative=False, holdLast=True,
                keys=[dict(time=t, position=list(p), rotation=[0,0,0], scale=[10,3,6], easingToNext=0) for t,p in points])],
            signals=signals)
    return result


def add_ferry_layout(objects):
    source = {o['name']:copy.deepcopy(o) for o in objects}
    objects[:] = [o for o in objects if not o['name'].startswith((PREFIX,'Stage2_BombVault','Stage2_StarBalcony_02','Stage2_StarHint_02'))]
    # 新しい航路へ重なっていた遠景島だけ、南側の背景へ退避する。
    for o in objects:
        if o['name']=='Stage2_Backdrop_Island_SouthMidB': o['position'][2]=-470.0
    def make(name,pos,scale,model='',kind='',event=-1,target=-1):
        o=copy.deepcopy(source['Stage2_MagmaSea'])
        for field in ('components','waterParam','parentGuid','parentName'): o.pop(field,None)
        o.update(name=PREFIX+name,guid=str(uuid.uuid5(uuid.NAMESPACE_URL,'CG2/'+PREFIX+name)),
            type='Gimmick' if kind else 'Model',gimmickType=kind,enemyType='',itemType='',modelName=model,
            position=list(pos),scale=list(scale),rotation=[0,0,0],quaternion=[0,0,0,1],
            myEventID=event,targetID=target,eventID=0,isStatic=not bool(kind),saveCategory='Object',
            isVisible=True,castShadow=False,materialType=0,metallic=0.,roughness=.88,emissive=0.,
            color=[.58,.49,.40,1],blendMode=0,collisionAttribute=4,collisionMask=4294967295,
            enableNormalMap=False,normalMapPath='',ormMapPath='',texturePath='',gpuParticleName='',particleName='',
            textureTiling=[1,1],autoTextureTiling=False)
        o['recorder']=dict(recordPathName='',isRecordLoop=False,isRecordRelative=False)
        o['param']=dict(gimmickType=kind,actionMode=0,startActive=True,returnOnOff=False)
        o['collider']=dict(type=3,center=[0,-.5,0],size=[.70,.5,.70],rotation=[0,0,0])
        objects.append(o)
        return o
    def rock(name,pos,hx,hz,kind='',event=-1):
        variant=sum(name.encode())%6
        return make(name,pos,(hx,max(3,pos[1]+10),hz),'Stages/magma_escape_rock_'+str(variant),kind,event)
    def supply(name,pos):
        o=make(name,pos,(1,1,1),'Gimmicks/copy_memory_station_single','CopyMemoryStation')
        o.update(color=[1,1,1,1],collisionAttribute=16,collisionMask=1)
        o['collider'].update(center=[0,.95,0],size=[3.1,1.6,1.85])
        o['param'].update(copyMemoryTypeA='',copyMemoryTypeB='Bomber',copyMemoryTypeC='',copyMemoryTypeD='',
                          copyMemoryActivationRadius=1.15,copyMemoryUnlimitedDuration=True)
    def checkpoint(name,pos):
        o=make(name,pos,(4,3,4),'','GameplayVolume')
        o.update(collisionAttribute=16,collisionMask=1)
        o['collider'].update(center=[0,0,0],size=[1,1,1])
        o['param'].update(volumeMode=1,volumeTriggerOnce=True,volumeTriggerOnExit=False)
        f=make(name+'Flag',(pos[0]+5,pos[1]-.5,pos[2]),(1,1,1),'Gimmicks/checkpoint_flag')
        f.update(color=[1,1,1,1],collisionAttribute=0,collisionMask=0);f['collider']['type']=0

    # 元の大岩を乗り場として使い、同じ高さへ別の岩を重ねない。
    supply('BoardingSupply',(140,-2.85,-122));checkpoint('StartCheckpoint',(140,-2.4,-128))
    rock('TransferIsland',(136,-3,-290),8,8)
    supply('TransferSupply',(136,-2.95,-292));checkpoint('TransferCheckpoint',(136,-2.5,-287))
    rock('RewardIsland',(260,-1,-244),12,11)
    for i, seq in enumerate(sequences().values(),1):
        raft=make('Raft_'+str(i),seq['tracks'][0]['keys'][0]['position'],(10,3,6),'Stages/magma_escape_rock_'+str(i),'MovingFloor')
        raft['components']={'PathMover':dict(version=1,_editorPresent=True,path='',loop=False,relative=False,
                                rideSequence=f'Resources/json/scenario/stage2_ferry_{i}.json')}
        raft['param'].update(speed=1,moveAmount=0)

    # 第1航路だけボム壁。左右の岸を広げ、船の見た目の幅でも接触させない。
    b=make('Barrier_1',(90,-3,-219),(1.65,1,1),'Stages/magma_ferry_break_rock','BreakableBlock')
    b.update(color=[1,1,1,1]);b['collider'].update(center=[0,1,0],size=[6,5,1.8])
    # 補給は右へ離れた小島に1個。岸と小島を二重に重ねない。
    rock('ServiceDock_1',(127,-3,-214),7,8)
    supply('ServiceSupply_1',(127,-2.95,-214))

    # 乗り継ぎ地点から下船して踏破する枝道。沈下量8でマグマ面より下へ沈む。
    a=rock('SinkStep_1',(136,-3,-316),5.5,5.5,'SinkingFloor')
    a['param'].update(actionMode=1,shakeDuration=.6,moveAmount=8,speed=5,interval=2.5)
    a=rock('CrossingStep',(136,-2,-337),5.5,5.5,'MovingFloor')
    a['param'].update(actionMode=2,moveAmount=7,speed=1.25)
    a=rock('SinkStep_2',(136,-1,-358),5.5,5.5,'SinkingFloor')
    a['param'].update(actionMode=1,shakeDuration=.5,moveAmount=10,speed=5.5,interval=2.5)
    rock('SwitchIsland',(136,0,-381),9,8)
    button=make('DepartureSwitch',(136,.35,-381),(.9,.45,.9),'Gimmicks/star_dash_panel','Switch',-1,9201)
    button.update(materialType=24,color=[.1,.8,1,1]);button['collider'].update(center=[0,-.08,0],size=[2.2,.18,.9])
    button['param'].update(switchMode=3)
    # 押した後は枝道脇の固定足場が現れ、安全に船まで戻れる。
    for i,pos in enumerate(((156,-1,-365),(162,-2,-344),(155,-3,-321)),1):
        a=rock('SwitchReturn_'+str(i),pos,5.5,5.5,'EventReceiver',9201)
        a['param']['startActive']=False
    lamp=make('DepartureLamp',(140,-2.8,-285),(2.0,.1,2.0),'Gimmicks/star_dash_panel','EventReceiver',9201)
    lamp['param']['startActive']=False
    lamp.update(materialType=24,color=[.1,1,.5,1],collisionAttribute=0,collisionMask=0)
    lamp['collider']['type']=0

    # Timelineが停船した時だけ、甲板へ着弾する一発を予告して発射する。
    for i,pos in enumerate(((82,-6.5,-177),(30,-6.5,-247),(220,-6.5,-318),(270,-6.5,-342)),1):
        o=make('Fireball_'+str(i),pos,(1.6,1.6,1.6),'Stages/magma_vent','MagmaGeyser')
        o['param'].update(actionMode=1,startActive=False,shakeDuration=1.0,fallDuration=.3,interval=2.4,
            moveSpeed=0,moveAmount=2,detectionRange=1.6,gravity=18,jumpPower=18,
            maxCount=1,healAmount=1.15,speed=1,maxFallSpeed=160)
        o['collider'].update(type=4,center=[0,0,0],size=[1,1,1])

    star=next(o for o in objects if o['name']=='Stage2_StarCoin_02');star['position']=[260,1,-244]
    # スターの島に着いてからだけ帰路が開く。途中の床から逆走取得させない。
    button=make('ReturnSwitch',(252,-.65,-239),(.9,.45,.9),'Gimmicks/star_dash_panel','Switch',-1,9200)
    button.update(materialType=24,color=[1,.75,.15,1]);button['collider'].update(center=[0,-.08,0],size=[2.2,.18,.9])
    button['param'].update(switchMode=3)
    for i,pos in enumerate(((230,-1.5,-230),(209,-2,-213),(191,-2.5,-193),(205,-2.9,-170)),1):
        o=rock('Return_'+str(i),pos,5.5,5.5,'EventReceiver',9200);o['param']['startActive']=False
    coin=next(o for o in source.values() if o.get('gimmickType')=='Coin')
    for i,pos in enumerate(((140,-.9,-128),(136,-1,-294),(136,-1,-316),(136,1,-358),(136,2,-376),(260,1,-249),(230,.5,-230),(209,0,-213)),1):
        o=copy.deepcopy(coin);o.update(name=PREFIX+'Coin_'+str(i),guid=str(uuid.uuid5(uuid.NAMESPACE_URL,PREFIX+'Coin_'+str(i))),position=list(pos));objects.append(o)
    assert len({o['name'] for o in objects})==len(objects)


def write_assets(root=ASSET_ROOT):
    folder=root/'json/scenario';folder.mkdir(parents=True,exist_ok=True)
    for name,data in sequences().items(): (folder/name).write_text(json.dumps(data,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    single=root/'3DModel/Gimmicks/copy_memory_station_single';single.mkdir(parents=True,exist_ok=True)
    vertices=[];faces=[]
    rings=[(1.12,.04),(1.25,.18),(1.0,.34),(.92,.69),(1.08,.78),(.80,.84)]
    n=24
    for radius,y in rings:
        vertices.extend((math.cos(i*math.tau/n)*radius,y,math.sin(i*math.tau/n)*radius) for i in range(n))
    for j in range(len(rings)-1):
        for i in range(n):
            a=j*n+i;b=j*n+(i+1)%n;c=b+n;d=a+n
            faces.extend([(a,c,b,'gold' if j in (0,3) else 'stone'),(a,d,c,'gold' if j in (0,3) else 'stone')])
    vertices.append((0,.84,0));center=len(vertices)-1
    for i in range(n):faces.append((center,(len(rings)-1)*n+(i+1)%n,(len(rings)-1)*n+i,'socket'))
    lines=['mtllib copy_memory_station_single.mtl']
    for v in vertices:lines.append('v '+' '.join(map(str,v)))
    for a,b,c,mat in faces:
        u=[vertices[b][k]-vertices[a][k] for k in range(3)];v=[vertices[c][k]-vertices[a][k] for k in range(3)]
        normal=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
        length=math.sqrt(sum(x*x for x in normal)) or 1
        lines.append('vn '+' '.join(str(x/length) for x in normal))
    for j,(a,b,c,mat) in enumerate(faces,1):lines.extend(['usemtl '+mat,f'f {a+1}//{j} {b+1}//{j} {c+1}//{j}'])
    (single/'copy_memory_station_single.obj').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    (single/'copy_memory_station_single.mtl').write_text('newmtl gold\nKd .95 .72 .16\nNs 30\nnewmtl stone\nKd .30 .34 .42\nNs 12\nnewmtl socket\nKd .55 .44 .72\nNs 18\n',encoding='utf-8')
    # 不規則な岩塊と正面の割れ目を一つのメッシュにする。破壊時に一緒に消える。
    folder=root/'3DModel/Stages/magma_ferry_break_rock';folder.mkdir(parents=True,exist_ok=True)
    poly=[(-6,-4),(-6.7,1),(-4.8,5.4),(-1.1,6.1),(3.7,5.5),(6.5,2.8),(5.8,-4)]
    lines=['mtllib magma_ferry_break_rock.mtl','o magma_ferry_break_rock'];count=0
    def triangle(a,b,c,mat):
        nonlocal count
        if mat != 'crack': b,c=c,b
        u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
        n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];l=math.sqrt(sum(x*x for x in n)) or 1
        lines.append('usemtl '+mat)
        for p in (a,b,c):
            lines.extend(['v '+' '.join(map(str,p)),'vt 0 0','vn '+' '.join(str(x/l) for x in n)])
        lines.append('f '+' '.join(f'{j}/{j}/{j}' for j in range(count+1,count+4)))
        if mat == 'crack': lines.append('f '+' '.join(f'{j}/{j}/{j}' for j in range(count+3,count,-1)))
        count+=3
    for i,a in enumerate(poly):
        b=poly[(i+1)%len(poly)]
        triangle((0,1,2.0),(*a,1.7),(*b,1.7),'stone'+str(i%3))
        triangle((0,1,-1.8),(*b,-1.7),(*a,-1.7),'stone'+str(i%3))
        triangle((*a,1.7),(*a,-1.7),(*b,-1.7),'stone1');triangle((*a,1.7),(*b,-1.7),(*b,1.7),'stone2')
    # カメラ側からも読み取れる太めの暗い亀裂。自己発光の線にはしない。
    for a,b in [((-.8,5.2),(.5,2.6)),((.5,2.6),(-1.1,.8)),((-1.1,.8),(.6,-2.8)),((-1.1,.8),(-4,2)),((.5,2.6),(3.8,4.4))]:
        w=.16
        triangle((a[0]-w,a[1],2.025),(b[0]-w,b[1],2.025),(b[0]+w,b[1],2.025),'crack')
        triangle((a[0]-w,a[1],2.025),(b[0]+w,b[1],2.025),(a[0]+w,a[1],2.025),'crack')
        triangle((a[0]-w,a[1],-2.025),(b[0]-w,b[1],-2.025),(b[0]+w,b[1],-2.025),'crack')
        triangle((a[0]-w,a[1],-2.025),(b[0]+w,b[1],-2.025),(a[0]+w,a[1],-2.025),'crack')
    (folder/'magma_ferry_break_rock.obj').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    (folder/'magma_ferry_break_rock.mtl').write_text('newmtl stone0\nKd .36 .27 .19\nNs 8\nnewmtl stone1\nKd .30 .23 .17\nNs 8\nnewmtl stone2\nKd .42 .33 .23\nNs 8\nnewmtl crack\nKd .035 .018 .009\nNs 1\n',encoding='utf-8')
