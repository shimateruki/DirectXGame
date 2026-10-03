"""ステージ2をマグマ面付近の低いコースへ変換する。生成時に一度だけ適用。"""
import math,copy

def lower_stage2_layout(objects):
    original={o['name']:copy.deepcopy(o) for o in objects}
    floor_models={'Stages/magma_fortress_foundation','Stages/fortress_plaza','Stages/basalt_causeway','Stages/fortress_ramp','Stages/metal_grate_platform','Stages/fortress_grate_drum','Stages/magma_lift_rock'}
    floors=[o for o in original.values() if o.get('modelName') in floor_models and not o['name'].endswith('_Substructure')]
    def height(y): return -5.0+y*.10
    def base_at(o):
        x,y,z=o['position']
        def score(f):
            fx,fy,fz=f['position'];a=f.get('rotation',[0,0,0])[1]
            dx=(x-fx)*math.cos(a)-(z-fz)*math.sin(a);dz=(x-fx)*math.sin(a)+(z-fz)*math.cos(a)
            size=f.get('collider',{}).get('size',[1,1,1]);sx,_,sz=f['scale']
            gap=math.hypot(max(0,abs(dx)-size[0]*sx),max(0,abs(dz)-size[2]*sz))
            return gap*2+abs(y-fy)*.15
        f=min(floors,key=score);return f['position'][1]
    objects[:]=[o for o in objects if o.get('modelName')!='Stages/fortress_pillar']
    for o in objects:
        name=o['name'];old=original[name];model=o.get('modelName','');p=o.get('param',{})
        if name=='Stage2_MagmaSea' or name.startswith('Stage2_Backdrop_'): continue
        if o.get('gimmickType')=='MagmaGeyser':
            o['position'][1]=-8.9
            if p.get('actionMode',0)==0: p['moveAmount']=max(3.0,height(base_at(old))+2.5-(-8.9+.72*o['scale'][1]))
            if p.get('actionMode')==1: p.update(jumpPower=20,gravity=7)
            continue
        if model in floor_models:
            o['position'][1]=height(old['position'][1])
            if model in {'Stages/magma_fortress_foundation','Stages/basalt_causeway'}:
                if name.endswith('_Substructure'):
                    parent=original.get(name.removesuffix('_Substructure'))
                    if parent: o['position'][1]=height(parent['position'][1])-1.2
                depth=4.8 if model.endswith('magma_fortress_foundation') else 3.2
                o['scale'][1]=max(.3,(o['position'][1]+10.5)/depth)
            if model=='Stages/fortress_ramp':
                angle=old['rotation'][2];new=math.atan(math.tan(angle)*.10)
                o['scale'][0]*=math.cos(angle)/math.cos(new)
                o['rotation'][2]=new
                x,y,z=o['rotation'];sx,cx=math.sin(x/2),math.cos(x/2);sy,cy=math.sin(y/2),math.cos(y/2);sz,cz=math.sin(z/2),math.cos(z/2)
                o['quaternion']=[sx*cy*cz+cx*sy*sz,cx*sy*cz-sx*cy*sz,cx*cy*sz+sx*sy*cz,cx*cy*cz-sx*sy*sz]
            if o.get('gimmickType')=='SinkingFloor':
                p.update(actionMode=1,moveAmount=o['position'][1]+9.2,speed=3.0,shakeDuration=.65,interval=1.5)
            continue
        # 敵、スター、門、バリアはモデルを潰さず、接地先と同量だけ下げる。
        if name.startswith(('Stage2_MagmaArena_','Stage2_MagmaBarrier_','Stage2_UpperForgeGate')): base=31.0
        elif name.startswith(('Stage2_EntryDecor_','Stage2_EntranceGate')): base=0.0
        elif name.startswith('Stage2_BombVaultWall_'): base=21.0
        else: base=base_at(old)
        o['position'][1]+=height(base)-base
    by={o['name']:o for o in objects}
    # 噴射岩は低い位置へ戻ると沈み、上昇している間だけ渡れる。
    for suffix,top in [('01',-3.8),('02',height(24.5))]:
        by['Stage2_LiftRock_'+suffix]['position'][1]=-8.7
        by['Stage2_LiftVent_'+suffix]['param']['moveAmount']=top+8.7
    by['Stage2_MagmaRestRock']['position'][1]=-3.8
