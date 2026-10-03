"""終盤の出現床を乗車型Timelineへ置換する。一度だけ適用し、以後はEditorで編集する。"""
import copy
import json
import math
from pathlib import Path
import shutil
import sys
import tempfile
import uuid
sys.dont_write_bytecode = True
from dress_entry_garden import Mesh

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT/'Resources/json/3Dobject/stage1_object.json'
ENEMIES = ROOT/'Resources/json/3Dobject/stage1_enemy.json'
ASSET = ROOT/'Resources/json/scenario/stage1_coaster.json'
MODELS = ROOT/'Resources/3DModel/Stages/coaster'
PREFIX = 'Stage1_Coaster_'

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def write(path, data):
    path.write_text(json.dumps(data,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')

def mesh_file(mesh, name):
    lines=['mtllib coaster.mtl','o '+name]
    lines += ['v '+' '.join(map(str,(p[0],p[1],-p[2]))) for p,n in mesh.vertices]
    lines += ['vn '+' '.join(map(str,(n[0],n[1],-n[2]))) for p,n in mesh.vertices]
    lines += ['vt 1 '+str(max(0,-p[1])) for p,n in mesh.vertices]
    for material,face in mesh.faces:
        lines += ['usemtl '+material,'f '+' '.join(f'{i}/{i}/{i}' for i in face)]
    (MODELS/(name+'.obj')).write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return 'Stages/coaster/'+name+'.obj'

def sample(keys,t):
    for a,b in zip(keys,keys[1:]):
        if t<=b['time']:
            u=max(0,min(1,(t-a['time'])/(b['time']-a['time'])))
            e=a['easingToNext']
            if e==1: u=u**3
            if e==2: u=1-(1-u)**3
            if e==4: u=u**3*(u*(u*6-15)+10)
            return [x+(y-x)*u for x,y in zip(a['position'],b['position'])]
    return keys[-1]['position']

def validate(scene,enemies,sequence):
    by={o['name']:o for o in scene['objects']}
    owned=[o for o in scene['objects'] if o['name'].startswith(PREFIX)]
    assert len({o['guid'] for o in owned})==len(owned)
    assert not any(o['name'].startswith('Stage1_V4_Appearing') for o in scene['objects'])
    assert sum(o['name'].startswith('Stage1_StarCoin_') for o in scene['objects'])==3
    floor=by[PREFIX+'Deck']
    assert floor['components']['PathMover']['rideSequence']=='Resources/json/scenario/stage1_coaster.json'
    assert not floor['recorder']['recordPathName']
    keys=sequence['tracks'][0]['keys']
    assert all(a['time']<b['time'] for a,b in zip(keys,keys[1:]))
    assert keys[0]['position']==floor['position']
    assert keys[-1]['position']==[763,16.4,13]
    assert all(k['rotation']==[0,0,0] for k in keys)
    assert all(k['scale']==[1,1,1] for k in keys)
    names={o['name'] for o in enemies['objects']}
    assert len(sequence['signals'])==2
    assert all(s['targetName'] in names and s['signal']=='ride.board' for s in sequence['signals'])
    assert max(s['time']+json.loads(s['payload'])['duration'] for s in sequence['signals'])<19
    assert by['Stage1_StarCoin_03']['position']==[756,34.5,51]
    # 初期床と終点の隙間。主経路の下に救済床を追加していない。
    assert 632-10 <= 605+18 and 763+10 == 785-12
    assert not any('Recovery' in o['name'] for o in owned)
    # 通常ジャンプでスターへ届く時刻を探索。床の水平慣性も実装と同じ減衰を使う。
    status=read(ROOT/'Resources/json/gameplay/status_settings.json')['player']
    success=[]
    for step in range(2300,2480):
        launch=step/100
        p=sample(keys,launch); prev=sample(keys,launch-.001)
        vx,vz=[(p[i]-prev[i])/.001*.55 for i in (0,2)]
        length=math.hypot(vx,vz)
        if length>24: vx*=24/length; vz*=24/length
        for frame in range(1,150):
            t=frame/100
            y=p[1]+.6+status['jumpPower']*t-.5*status['gravity']*t*t
            old_y=p[1]+.6+status['jumpPower']*(t-.01)-.5*status['gravity']*(t-.01)**2
            x=p[0]+vx*(1-math.exp(-.8*t))/.8
            z=p[2]+status['speed']*t+vz*(1-math.exp(-.8*t))/.8
            if old_y>=32>=y and 744<=x<=768 and 42<=z<=60:
                success.append(launch);break
    assert len(success)>=30, 'スターへの通常ジャンプ経路が狭すぎます'
    print('Validated: boarding sequence, two enemies, 3 stars, no rescue floor')
    print('Star jump feasible launch window (straight +Z input):',min(success),max(success))
    # 実装と同じ重力の半陰的積分でも、フレームレート別に到達可能か確認する。
    # コリジョンやカメラを含む実プレイの代替ではなく、配置の数値チェック。
    for fps in (30,60,144):
        dt=1/fps
        reachable=[]
        for launch_frame in range(int(23*fps),int(24.8*fps)):
            launch=launch_frame*dt
            p=sample(keys,launch); previous=sample(keys,launch-dt)
            vx,vz=[(p[i]-previous[i])/dt*.55 for i in (0,2)]
            speed=math.hypot(vx,vz)
            if speed>24: vx*=24/speed; vz*=24/speed
            x,y,z=p[0],p[1]+.6,p[2]
            vy=status['jumpPower']
            for frame in range(int(1.5*fps)):
                old_y=y
                vy-=status['gravity']*dt
                y+=vy*dt
                x+=vx*dt; z+=(status['speed']+vz)*dt
                vx*=math.exp(-.8*dt); vz*=math.exp(-.8*dt)
                if old_y>=32>=y and 744<=x<=768 and 42<=z<=60:
                    reachable.append(launch);break
        assert reachable, f'{fps}fpsでスターに届きません'
        print(f'Star jump at {fps}fps: {min(reachable):.3f} - {max(reachable):.3f}')

def main():
    scene,enemies=read(SCENE),read(ENEMIES)
    if '--validate' in sys.argv:
        validate(scene,enemies,read(ASSET));return
    if '--tune-star' in sys.argv:
        # この生成器が所有する高台だけを調整する。既存の他の編集には触らない。
        backup=Path(tempfile.mkdtemp(prefix='cg2-coaster-star-'))
        shutil.copy2(SCENE,backup/SCENE.name)
        by={o['name']:o for o in scene['objects']}
        by[PREFIX+'StarIsland']['position']=[756,32,51]
        by[PREFIX+'StarIslandCollision']['position']=[756,30,51]
        by[PREFIX+'StarIslandCollision']['collider']['size']=[12,2,9]
        by['Stage1_StarCoin_03']['position']=[756,34.5,51]
        m=Mesh();m.block((0,-2,0),(24,4,18),'Stone')
        m.block((0,.025,0),(23.6,.05,17.6),'Gold')
        mesh_file(m,'StarIsland')
        validate(scene,enemies,read(ASSET));write(SCENE,scene);return
    if any(o['name'].startswith(PREFIX) for o in scene['objects']):
        raise SystemExit('配置済みです。再生成せずEditorで調整してください。')
    backup=Path(tempfile.mkdtemp(prefix='cg2-coaster-'))
    for f in (SCENE,ENEMIES): shutil.copy2(f,backup/f.name)
    print('Backup:',backup)
    original=copy.deepcopy(scene)
    by={o['name']:o for o in scene['objects']}
    removed={o['name'] for o in scene['objects'] if o['name'].startswith('Stage1_V4_Appearing')}
    scene['objects']=[o for o in scene['objects'] if o['name'] not in removed]
    MODELS.mkdir(parents=True,exist_ok=True)
    (MODELS/'coaster.mtl').write_text('newmtl Stone\nKd .62 .63 .49\nKs .1 .1 .1\nNs 16\n\nnewmtl Wood\nKd .48 .29 .12\nKs .1 .1 .1\nNs 8\n\nnewmtl Gold\nKd .85 .64 .2\nKs .3 .3 .3\nNs 40\n\nnewmtl Cyan\nKd .15 .85 .78\nKs .2 .2 .2\nNs 24\n',encoding='utf-8')
    def add(name,pos,model=None,half=None,material=0):
        name=PREFIX+name
        o=dict(name=name,guid=str(uuid.uuid5(uuid.NAMESPACE_URL,name)),type='InvisibleBox' if half else 'Model',
            position=list(pos),rotation=[0,0,0],quaternion=[0,0,0,1],scale=[1,1,1],color=[1,1,1,1],
            materialType=material,roughness=.8,metallic=.1,enableLighting=True,castShadow=not bool(half),
            isStatic=True,layer='Default',saveCategory='Object',collisionAttribute=4 if half else 0,
            collisionMask=255 if half else 0,collider=dict(type=3,center=[0,0,0],size=list(half),rotation=[0,0,0]) if half else {'type':0})
        if model:o['modelName']=model
        scene['objects'].append(o);return o
    def island(name,x,z,hx,hz,top):
        m=Mesh();m.block((0,-2,0),(hx*2,4,hz*2),'Stone')
        m.block((0,.025,0),(hx*2-.4,.05,hz*2-.4),'Gold')
        add(name,(x,top,z),mesh_file(m,name))
        add(name+'Collision',(x,top-2,z),half=(hx,2,hz))
    # 古い中継島は敵の待機場所として活用。二つ目の乗り込み場所のみ追加。
    island('EnemyPerch',712,-27,7,6,23)
    island('StarIsland',756,51,12,9,32)
    island('StarReturnHigh',786,51,7,7,28)
    island('StarReturnLow',806,47,7,7,20)
    deck=Mesh();deck.block((0,-.4,0),(20,2,14),'Stone')
    for i in range(10):deck.block((-9+i*2,.4,0),(1.9,.4,12),'Wood')
    for z in (-6.65,6.65):deck.block((0,.38,z),(20,.44,.7),'Gold')
    for x in (-9.65,9.65):deck.block((x,.38,0),(.7,.44,12.6),'Gold')
    deck.block((0,.61,0),(2,.04,4),'Cyan')
    floor=copy.deepcopy(by['Stage1_V4_MovingIntro_A'])
    floor.update(name=PREFIX+'Deck',guid=str(uuid.uuid5(uuid.NAMESPACE_URL,PREFIX+'Deck')),
        position=[632,11.4,-14],rotation=[0,0,0],quaternion=[0,0,0,1],scale=[1,1,1],
        modelName=mesh_file(deck,'deck'),color=[1,1,1,1],isStatic=False,
        collider=dict(type=3,center=[0,-.4,0],size=[10,1,7],rotation=[0,0,0]),
        recorder=dict(recordPathName='',isRecordLoop=False,isRecordRelative=False),
        components={'PathMover':dict(version=1,_editorPresent=True,path='',loop=False,relative=False,
            rideSequence='Resources/json/scenario/stage1_coaster.json')})
    scene['objects'].append(floor)
    star=by['Stage1_StarCoin_03'];star['position']=[756,34.5,51]
    # 星の取得ID/GUIDは変更しない。コイン列は任意ジャンプの方向を示す。
    for i,pos in enumerate(((748,39,37),(752,40,43),(756,39,48))):
        coin=copy.deepcopy(by['Stage1_V4_AppearingGuide_StarOut_01'])
        coin.update(name=PREFIX+f'JumpGuide_{i}',guid=str(uuid.uuid5(uuid.NAMESPACE_URL,PREFIX+f'JumpGuide_{i}')),position=list(pos))
        scene['objects'].append(coin)
    for i,pos in enumerate(((679,15.1,18),(712,23.1,-27))):
        name=PREFIX+f'Boarder_{i}'
        enemies['objects'].append(dict(name=name,guid=str(uuid.uuid5(uuid.NAMESPACE_URL,name)),type='Enemy',enemyType='Slime',
            param={'enemyType':'Slime'},position=list(pos),rotation=[0,0,0],modelName='Characters/slime_pink',
            color=[1,.55,.75,1],materialType=25,roughness=.4,metallic=0,enableLighting=True,castShadow=True,
            saveCategory='Enemy',isStatic=False,collisionAttribute=2,collisionMask=255,
            collider=dict(type=2,center=[0,.36,0],size=[.52,.37,.52],rotation=[0,0,0])))
    points=[(0,[632,11.4,-14],0),(1.1,[632,11.4,-14],4),(4,[650,15,-32],4),
        (7,[672,18,-10],4),(8.5,[681,18,0],0),(10.6,[681,18,0],4),
        (14,[706,23,-8],0),(15.7,[706,23,-8],4),(19,[718,36,14],4),
        (22,[727,50,29],0),(23,[727,50,29],1),(24.8,[752,23,18],2),
        (26.3,[763,16.4,13],0)]
    keys=[dict(time=t,position=p,rotation=[0,0,0],scale=[1,1,1],easingToNext=e) for t,p,e in points]
    sequence=dict(version=6,name='stage1_coaster',duration=26.3,
        tracks=[dict(name='乗車床：出発・乗り込み停車・上昇・急降下',targetName=PREFIX+'Deck',targetEventId=-1,
            startTime=0,enabled=True,muted=False,relative=False,holdLast=True,keys=keys)],
        signals=[dict(time=t,name=f'スライム{i+1}が乗り込む',signal='ride.board',
            payload=json.dumps(dict(duration=1.1,height=6,offset=[-3 if i==0 else 3,0,0])),
            targetName=PREFIX+f'Boarder_{i}',targetEventId=-1,enabled=True) for i,t in enumerate((8.8,14.2))])
    validate(scene,enemies,sequence)
    for old in original['objects']:
        if old['name'] not in removed|{'Stage1_StarCoin_03'}:assert old in scene['objects'],old['name']
    write(SCENE,scene);write(ENEMIES,enemies);write(ASSET,sequence)
    print('Removed old appearing-floor objects:',len(removed))

if __name__=='__main__':main()
