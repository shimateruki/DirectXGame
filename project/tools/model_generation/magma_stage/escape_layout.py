"""低い前半を維持し、中ボス出口だけを上昇マグマ区間へ更新する。"""
import copy,math,uuid

PREFIX='Stage2_Escape_'
def add_escape_layout(objects):
    sources={o['name']:copy.deepcopy(o) for o in objects}
    # 差し替える経路と、その経路だけの装飾・コインを取り除く。
    objects[:]=[o for o in objects if not o['name'].startswith((PREFIX,'Stage2_ForgeDrum','Stage2_ForgeExitRamp'))]
    template=sources['Stage2_MagmaSea']
    def make(name,pos,scale,model='Stages/magma_escape_terrace',kind='',event=-1,target=-1,source=None):
        o=copy.deepcopy(source or template);o.pop('components',None);o.pop('waterParam',None)
        o.update(name=PREFIX+name,guid=str(uuid.uuid5(uuid.NAMESPACE_URL,'CG2/'+PREFIX+name)),position=list(pos),scale=list(scale),rotation=[0,0,0],quaternion=[0,0,0,1],
                 type='Gimmick' if kind else 'Model',gimmickType=kind,enemyType='',itemType='',modelName=model,myEventID=event,targetID=target,eventID=0,
                 isStatic=not bool(kind),castShadow=False,materialType=35,metallic=0.,roughness=.9,emissive=.1,color=[1,1,1,1],blendMode=0,
                 collisionAttribute=4,collisionMask=4294967295,enableNormalMap=False,normalMapPath='',ormMapPath='',texturePath='',gpuParticleName='',particleName='')
        o['recorder']={'recordPathName':'','isRecordLoop':False,'isRecordRelative':False}
        o['param']={'gimmickType':kind,'actionMode':0,'startActive':False,'returnOnOff':True}
        o['collider']={'type':3,'center':[0,-.5,0],'size':[.85,.5,.85],'rotation':[0,0,0]}
        objects.append(o);return o
    def deck(n,p,hx=7,hz=7):
        variant=sum(n.encode('utf-8'))%6
        o=make(n,p,(hx,max(3,p[1]+10),hz),'Stages/magma_escape_rock_'+str(variant))
        o.update(materialType=0,color=[.28+variant*.012,.23+variant*.009,.20+variant*.007,1],emissive=0.,textureTiling=[1,1],autoTextureTiling=False)
        o['collider'].update(size=[.70,.5,.70]);return o
    def wall(n,p,s,kind='',ev=-1):
        o=make(n,p,s,'Stages/magma_escape_wall',kind,ev,9100 if kind else -1)
        o['collider'].update(center=[0,0,0],size=[1,1,1]);return o
    def ramp(n,a,b,width):
        dx,dy,dz=[b[i]-a[i] for i in range(3)];run=math.hypot(dx,dz);slope=math.atan2(dy,run);yaw=-math.atan2(dz,dx)
        o=make(n,[(a[i]+b[i])/2 for i in range(3)],(math.hypot(run,dy)/30,1,width/11.2),'Stages/fortress_ramp')
        o['rotation']=[0,yaw,slope];sy,cy=math.sin(yaw/2),math.cos(yaw/2);sz,cz=math.sin(slope/2),math.cos(slope/2)
        o['quaternion']=[sy*sz,sy*cz,cy*sz,cy*cz];o.update(materialType=0,color=[.65,.52,.43,1],emissive=.12);o['collider'].update(center=[0,-.65,0],size=[15,.65,5.6]);return o
    def grate(n,p,kind,event=-1,mode=0,phase=0):
        o=make(n,p,(.58,1,.66),'Stages/metal_grate_platform',kind,event,9100,source=sources['Stage2_AscentMovingGrate'])
        o.update(materialType=0,roughness=.75,metallic=.15,color=[.8,.9,1,1]);o['collider'].update(center=[0,-.38,0],size=[5.8,.38,5])
        o['param'].update(actionMode=mode,speed=1.35,moveAmount=4.,moveSpeed=0.,startActive=False,returnOnOff=True)
        return o
    def marker(n,p,s,event):
        o=make(n,p,s,'','GameplayVolume',event);o.update(collisionAttribute=0,collisionMask=0,materialType=0)
        o['param'].update(volumeMode=3,volumeTriggerOnce=True,volumePayload='',startActive=False)
        o['collider'].update(center=[0,0,0],size=[1,1,1]);return o
    deck('CheckpointLanding',(325,-1.9,-145),15,10)
    ramp('EntryBridge',(317,-1.9,-145),(350,-1,-145),9)
    deck('SwitchLanding',(350,-1,-145),9,9)
    marker('Start',(347,1,-145),(3,4,7),9101)
    marker('Finish',(558,36,-85),(6,4,6),9102)
    marker('Retry',(325,.2,-145),(5,4,6),9103)
    button=make('Switch',(353,-.65,-145),(.9,.45,.9),'Gimmicks/star_dash_panel','Switch',-1,9104,source=sources['Stage2_StarBridgeSwitch'])
    button.update(materialType=24,color=[1,.65,.08,1]);button['param'].update(switchMode=3,interval=120)
    button['collider'].update(center=[0,-.08,0],size=[2.2,.18,.9])
    for i,p in enumerate([(365,0,-145),(379,1.2,-145),(393,2.4,-145)],1):grate('Appear_'+str(i),p,'EventReceiver',9104)
    deck('Turn_A',(407,3.5,-145),11,9)
    # 前半で覚えた出現床を、方向転換と連続着地へ発展させる。
    for i,p in enumerate([(423,4.5,-151),(439,5.7,-141),(455,6.8,-149)],4):grate('Appear_'+str(i),p,'EventReceiver',9104)
    deck('Turn_A_Ext',(471,8,-145),10,9)
    ramp('Rise_A',(471,8,-140),(471,10,-124),8)
    deck('Launch_A',(471,10,-120),9,8)
    for i,p in enumerate([(458,11,-108),(440,12.5,-103),(422,14,-111),(404,15.5,-103)],1):
        o=grate('Moving_'+str(i),p,'MovingFloor',-1,2 if i%2 else 3);o['param'].update(moveAmount=3.,speed=1.1+i*.10)
    deck('Turn_B',(384,17,-108),11,8)
    ramp('Rise_B',(384,17,-105),(384,19,-85),8)
    deck('Launch_B',(384,19,-85),10,9)
    for i,p in enumerate([(402,20,-82),(419,21.5,-91),(437,23,-82),(454,24.5,-92),(472,26,-82)],1):
        deck('Jump_'+str(i),p,[5.4,6.2,5.7,5.1,6.5][i-1],[6.5,5.4,7.0,5.8,6.5][i-1])
    deck('FinalLaunch',(490,27.5,-85),8,8)
    grate('FinalMoving',(509,29,-92),'MovingFloor',-1,3)['param']['moveAmount']=3.
    deck('FinalRest',(523,30.5,-107),6,6)
    grate('FinalMoving_2',(538,32,-92),'MovingFloor',-1,2)['param']['moveAmount']=3.
    deck('Summit',(558,34,-85),15,12)
    # スターは本線より離れた高所。本線の通常ジャンプでは直接届かない。
    branch=make('StarSwitch',(384,17.35,-112),(.9,.45,.9),'Gimmicks/star_dash_panel','Switch',-1,9120,source=button)
    branch.update(materialType=24,color=[1,.75,.1,1]);branch['param'].update(switchMode=3)
    branch['collider'].update(center=[0,-.08,0],size=[2.2,.18,.9])
    for i,p in enumerate([(384,18,-127),(384,19,-145)],1):grate('StarAppear_'+str(i),p,'EventReceiver',9120)
    lift=grate('StarLift',(384,19,-164),'MovingFloor',-1,5)
    lift.update(modelName='Stages/magma_escape_rock_2',scale=[5.5,2.5,5.5],materialType=0,color=[.30,.25,.21,1],targetID=9111)
    lift['collider'].update(center=[0,-.5,0],size=[.70,.5,.70]);lift['param']['moveAmount']=7.
    deck('StarReward',(384,29,-185),8,7)
    # 同じ道を戻らず、下りながら本線の後半へ合流する。
    for i,p in enumerate([(405,22,-185),(422,23,-169),(438,24,-152),(454,24.5,-135),(454,24.5,-115)],1):
        deck('StarReturn_'+str(i),p,5.3+(i%2)*.5,5.6)
    for i,(x,y,z,hx,hz) in enumerate([(350,12,-50,17,11),(387,24,-47,20,13),(432,35,-44,19,13),(478,43,-46,22,14),(526,48,-44,24,14),(568,44,-43,21,12)]):
        deck('Backdrop_'+str(i),(x,y,z),hx,hz)
    # ステージ全体の海を直接上昇させる。局所液面と外周の岩戸は使わない。
    liquid=next(o for o in objects if o['name']=='Stage2_MagmaSea')
    liquid.update(type='Gimmick',gimmickType='LiquidLevel',myEventID=9100,targetID=9101,isStatic=False,materialType=9,
                  modelName='Stages/magma_escape_surface',collisionAttribute=0,collisionMask=0)
    liquid['param'].update(gimmickType='LiquidLevel',actionMode=1,colorType=1,moveAmount=39.5,moveSpeed=.60,shakeDuration=2.5)
    liquid['waterParam'].update(waveHeight=.025,waveFrequency=3.2,effectSoftness=.48,effectIntensity=1.12)
    # 王冠が頂上のステージゴール。3つ目のスターは上昇岩を使う専用分岐の報酬にする。
    next(o for o in objects if o['name']=='goal')['position']=[558,35.8,-85]
    next(o for o in objects if o['name']=='Stage2_StarCoin_03')['position']=[384,31,-185]
    for i,(x,y,z,delay) in enumerate([(410,18,-99,1.0),(445,21,-100,2.0),(481,25,-100,1.4),(510,26,-105,2.4),(538,29,-106,1.7)],1):
        deck('VentShelf_'+str(i),(x,y-.15,z),3.2,3.2)
        o=make('Fireball_'+str(i),(x,y,z),(.9,.9,.9),'Stages/magma_vent','MagmaGeyser',-1,9100,source=sources['Stage2_LiftVent_01'])
        o.update(materialType=0,roughness=.9,metallic=0.)
        o['param'].update(actionMode=1,startActive=False,shakeDuration=.9,fallDuration=.35,interval=1.35,moveSpeed=delay,moveAmount=2.,detectionRange=1.5,
                          gravity=10.,jumpPower=18.,maxCount=1,healAmount=.8,speed=1.,maxFallSpeed=300.)
        o['collider'].update(center=[0,0,0],size=[1,1,1],type=4)
    vent=make('StarLiftVent',(384,16,-164),(.9,.9,.9),'Stages/magma_vent','MagmaGeyser',9111,9100,source=sources['Stage2_LiftVent_01'])
    vent.update(materialType=0,roughness=.9,metallic=0.)
    vent['param'].update(actionMode=2,startActive=False,shakeDuration=1.1,fallDuration=5.6,interval=1.6,moveSpeed=1.0,moveAmount=7.,detectionRange=2.,maxFallSpeed=300.)
    vent['collider'].update(center=[0,0,0],size=[1,1,1],type=4)
    # コインは固定着地地点だけに置き、取るために危険側へ曲がらせない。
    coin=next(o for o in sources.values() if o.get('gimmickType')=='Coin')
    for i,p in enumerate([(350,1,-145),(407,5.5,-145),(471,10,-145),(384,19,-108),(419,23.5,-91),(472,28,-82),(523,32.5,-107),(558,36,-85),(384,19,-112),(384,20,-127),(384,21,-145),(405,24,-185),(438,26,-152),(454,26.5,-115)]):
        o=copy.deepcopy(coin);o.update(name=PREFIX+'Coin_'+str(i),guid=str(uuid.uuid5(uuid.NAMESPACE_URL,PREFIX+'Coin_'+str(i))),position=list(p));objects.append(o)
    assert len({o['name'] for o in objects})==len(objects)
    return objects
