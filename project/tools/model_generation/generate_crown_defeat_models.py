"""中央装飾を失った王冠を実際の三角形から破片へ分割する。元の形状は変更しない。"""
from pathlib import Path
import math,json,hashlib
from generate_stage3_high_crown import ObjBuilder,write_obj,add_vertical_crystal

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Resources/3DModel/Characters/false_king_slime'

def save(model,name,mtl='false_king_slime.mtl'):
    path=OUT/(name+'.obj');write_obj(model,path,mtl,name)
    meta=path.with_suffix('.obj.meta')
    if not meta.exists():
        relative=path.relative_to(ROOT).as_posix()
        meta.write_text(json.dumps(dict(assetType='Model',guid=hashlib.md5(('cg2:'+relative).encode()).hexdigest(),importSettings={},importer='ModelImporter',source=relative,version=1),indent=2)+'\n',encoding='utf-8')

def main():
    lines=(OUT/'false_king_slime_without_emblem.obj').read_text(encoding='utf-8').splitlines()
    vertices=[tuple(map(float,l.split()[1:])) for l in lines if l.startswith('v ')]
    uvs=[tuple(map(float,l.split()[1:])) for l in lines if l.startswith('vt ')]
    normals=[tuple(map(float,l.split()[1:])) for l in lines if l.startswith('vn ')]
    groups={};material='';source_count=0
    for l in lines:
        if l.startswith('usemtl '):material=l.split()[1]
        if not l.startswith('f '):continue
        refs=[tuple(map(int,s.split('/'))) for s in l.split()[1:]]
        center=tuple(sum(vertices[r[0]-1][i] for r in refs)/3 for i in range(3))
        key='body' if material.startswith('Slime') or material=='Face' else ('gem_'+material if material.startswith('Gem') else 'frame_'+str(int((math.atan2(center[2],center[0])+math.pi)/(2*math.pi)*12)%12))
        groups.setdefault(key,[]).append((material,refs));source_count+=1
    manifest=[];written=0
    for key,faces in sorted(groups.items()):
        points=[vertices[r[0]-1] for _,refs in faces for r in refs]
        pivot=(0,0,0) if key=='body' else tuple((min(p[i] for p in points)+max(p[i] for p in points))/2 for i in range(3))
        model=ObjBuilder()
        for mat,refs in faces:
            ids=[model.vertex(tuple(vertices[v-1][i]-pivot[i] for i in range(3)),uvs[t-1],normals[n-1]) for v,t,n in refs]
            model.face(mat,*ids,True);written+=1
        name='false_king_defeat_'+key;save(model,name)
        if key!='body':manifest.append(dict(model='Characters/false_king_slime/'+name+'.obj',pivot=[pivot[0],pivot[1],-pivot[2]],gem=key.startswith('gem_')))
    assert written==source_count
    (OUT/'false_king_defeat_parts.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    # 王冠の外周に沿った立体の裂け目。板一枚の裏面消失を避けるため四角柱で生成する。
    crack=ObjBuilder()
    angles=[-1.28,-.82,-.35,.12,.59,1.06,2.22,3.14,4.10]
    heights=[.86,1.16,1,1.40,1,1.16,.94,1.22,.92]
    for i,(angle,height) in enumerate(zip(angles,heights)):
        radius=1.48 if i%2==0 else 1.58
        radial=(math.sin(angle),0,math.cos(angle));tangent=(math.cos(angle),0,-math.sin(angle))
        # 黒い板の実際の前後面へ亀裂を載せる。一定半径の円筒へ貼って埋没させない。
        for side in [-1,1]:
            points=[((.038 if j%2 else -.044),height*j/7) for j in range(7)]
            for a,b in zip(points,points[1:]):
                dx=b[0]-a[0];dy=b[1]-a[1];length=math.hypot(dx,dy)
                ox=-dy/length*.018;oy=dx/length*.018
                def at(x,y):return (radial[0]*(radius+side*.108)+tangent[0]*x,3.16+y,radial[2]*(radius+side*.108)+tangent[2]*x)
                q=(at(a[0]+ox,a[1]+oy),at(b[0]+ox,b[1]+oy),at(b[0]-ox,b[1]-oy),at(a[0]-ox,a[1]-oy))
                crack.quad('SlimeHighlight',q,tuple(side*x for x in radial))
                crack.quad('SlimeHighlight',tuple(reversed(q)),tuple(-side*x for x in radial))
    save(crack,'false_king_defeat_cracks')
    # 破砕の瞬間だけ伸びる立体の光条。短い寿命で衝撃の輪郭を作る。
    ray=ObjBuilder();add_vertical_crystal(ray,'SlimeHighlight',(0,.5,0),.09,1,4)
    save(ray,'false_king_defeat_ray')
    print(f'Verified {written} source triangles -> body and {len(manifest)} crown fragments')
    # 粒子は既存の適した火花から専用品へ分離し、GPUパーティクルエディターで調整可能にする。
    base=json.loads((ROOT/'Resources/json/gpu_particles/false_king_seal_impact.json').read_text(encoding='utf-8-sig'))
    base.update(alignToVelocity=True,velocityStretch=.10,emitCount=140,emitLife=1.6,baseColor=[2.6,1.9,4,1],midColor=[1,.12,2.4,.8],endColor=[.12,.015,.3,0],colorIntensity=1.3,baseSize=.075,midSize=.13,endSize=.005,emitVelocity=[0,3,0],velocityVariance=7,envGravity=[0,-3,0])
    (ROOT/'Resources/json/gpu_particles/crown_defeat_sparks.json').write_text(json.dumps(base,indent=2)+'\n',encoding='utf-8')
    charge=json.loads((ROOT/'Resources/json/gpu_particles/goal_clear_gold_motes.json').read_text(encoding='utf-8-sig'))
    charge.update(emitCount=12,emitLife=.85,baseColor=[.65,.18,1,0],midColor=[1,.55,1,.75],endColor=[.3,.02,.7,0],colorIntensity=2.0,midSize=.16,emitVelocity=[0,.7,0],velocityVariance=.55,envGravity=[0,0,0],envDrag=.2)
    (ROOT/'Resources/json/gpu_particles/crown_defeat_charge.json').write_text(json.dumps(charge,indent=2)+'\n',encoding='utf-8')
    cue={'version':2,'events':[{'type':0,'presetName':'crown_defeat_sparks','triggerTime':0,'offset':[0,0,0],'rotation':[0,0,0],'scale':[1,1,1]}, {'type':6,'presetName':'','triggerTime':0,'duration':.55,'intensity':4,'lightRadius':22,'lightDecay':1.8,'lightColor':[.7,.25,1,1]}]}
    (ROOT/'Resources/json/vfx_sequence/crown_defeat_rupture_cue.json').write_text(json.dumps(cue,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
