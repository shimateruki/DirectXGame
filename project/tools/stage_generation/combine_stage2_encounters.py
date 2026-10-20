"""既存の足場を保ち、棘を足場の判断と組み合わせ、支持部を追加する。"""
import copy
import json
import math
import uuid
from datetime import datetime
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]

def main():
    # 大規模な再配置をせず、各区間の終端に一つずつ判断を加える。
    file=ROOT/'Resources/json/3Dobject/stage2_object.json';raw=file.read_bytes()
    scene=json.loads(raw.decode('utf-8-sig'));objects=scene['objects'];by={o['name']:o for o in objects}
    prefix='Stage2_Route_';get=lambda n:by[prefix+n]
    assert prefix+'Mechanism_LiftLeft' not in by
    folder=ROOT/'backups/stage2'/datetime.now().strftime('%Y%m%d_%H%M%S_before_combined_encounters')
    folder.mkdir(parents=True);(folder/file.name).write_bytes(raw)
    def pose(o,p,yaw=0):
        o.update(position=list(p),translate=list(p),rotation=[0,yaw,0],rotate=[0,yaw,0],
                 quaternion=[0,math.sin(yaw/2),0,math.cos(yaw/2)])
    def support(name,p,size,yaw=0):
        # マグマ底まで伸びる固定支持部。棘そのものとは別Objectとして調整可能。
        o=copy.deepcopy(get('13_SpinBridge'))
        o.update(name=prefix+'Mechanism_'+name,guid=str(uuid.uuid4()),modelName='Primitives/cube/cube.obj',
                 type='Model',gimmickType='',scale=list(size),color=[.16,.19,.22,1],metallic=.7,roughness=.42,
                 parentName='',parentGuid='',components={},isStatic=True,emissive=0,enableNormalMap=False,
                 collisionAttribute=4,collisionAttributeIds=['ground'],collisionMask=15)
        o.pop('param',None)
        o['collider']=dict(type=3,center=[0,0,0],size=[1,1,1],rotation=[0,0,0])
        o['recorder']=dict(recordPathName='',isRecordLoop=False,isRecordRelative=False)
        pose(o,p,yaw);objects.append(o)
    def post(name,x,z,top,width=.45):
        bottom=-10
        support(name,[x,(top+bottom)/2,z],[width,(top-bottom)/2,width])
    # 沈降の残り時間を見ながら、昇降バーが開く瞬間に通過する。
    sink=get('02_LongSinkingRun');lift=get('Bar_Lift');p=sink['position'];yaw=sink['rotation'][1]
    pose(lift,[p[0]+math.sin(yaw)*8,p[1]+1.5,p[2]+math.cos(yaw)*8],yaw)
    for side,label in [(-1,'Left'),(1,'Right')]:
        post('Lift'+label,lift['position'][0]+side*4.5*math.cos(yaw),
             lift['position'][2]-side*4.5*math.sin(yaw),p[1]+6)
    # 円柱を抜けた安定足場から、横往復バーの隙を見て段差へ乗り移る。
    landing=get('11_RollerExit');slide=get('Bar_Slide');p=landing['position']
    pose(slide,[p[0]+5,p[1]+1.4,p[2]-4],0)
    post('SlideLeft',p[0]-4,p[2]-4,p[1]+1.4)
    post('SlideRight',p[0]+14,p[2]-4,p[1]+1.4)
    support('SlideRail',[p[0]+5,p[1]-.3,p[2]-4],[9,.25,.25])
    # 三角岩の先へ踏み込む前に回転の隙を見る。沈降中の引き返しも可能にする。
    tri=get('15_TriangleTurn');spin=get('Bar_Spin');p=tri['position']
    pose(spin,[p[0]+7,p[1]+1.5,p[2]-7])
    post('SpinAxle',spin['position'][0],spin['position'][2],spin['position'][1],.65)
    # 振り子は任意のスター分岐だけ。支点を固定し、バーが岩を貫通しない高さにする。
    crumble=get('Star2_Crumble0');swing=get('Bar_Swing');p=crumble['position']
    pose(swing,[p[0],p[1]+4.5,p[2]+3])
    post('SwingAxle',p[0]+4,p[2]+3,p[1]+4.5,.55)
    # 支持柱の上端が回転軸に一致する。空中を理由なく漂う動きにしない。
    mount=get('11_RollingBasalt_Mount');p=mount['position'];yaw=mount['rotation'][1]
    half=get('11_RollingBasalt')['scale'][0]*4
    for sign,label in [(-1,'Left'),(1,'Right')]:
        post('Roller'+label,p[0]+sign*half*math.cos(yaw),p[2]-sign*half*math.sin(yaw),p[1],.8)
    # 最初の沈降は導入を兼ねるため、待機できる余裕を増やす。
    sink['param']['shakeDuration']=1.0
    sink['param']['speed']=.28
    assert file.read_bytes()==raw
    file.write_text(json.dumps(scene,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    print('Backup:',folder)

if __name__=='__main__':
    main()
