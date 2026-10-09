"""継承の工程を独立して表示できるモデルを作る。元の身体と顔は維持する。"""
from pathlib import Path
import copy, hashlib, json, math, sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/model_generation'))
from generate_stage3_high_crown import ObjBuilder, write_obj
OUT=ROOT/'Resources/3DModel/Characters/false_king_slime'
ROYAL=ROOT/'Resources/3DModel/Characters/slime_crown'

def meta(path):
    target=path.with_suffix(path.suffix+'.meta')
    if not target.exists():
        rel=path.relative_to(ROOT).as_posix()
        target.write_text(json.dumps({'assetType':'Model','guid':hashlib.md5(('cg2:'+rel).encode()).hexdigest(),
            'importSettings':{},'importer':'ModelImporter','source':rel,'version':1},indent=2)+'\n',encoding='utf-8')

def save(model,name):
    path=OUT/(name+'.obj')
    write_obj(model,path,'false_king_slime.mtl',name)
    meta(path)

def tube(model,points,width,material='SlimeHighlight'):
    # 立体の細管にして側面からの観察でも線が消えないようにする。
    for a,b in zip(points,points[1:]):
        d=tuple(b[i]-a[i] for i in range(3)); length=math.sqrt(sum(x*x for x in d))
        if length<1e-6: continue
        d=tuple(x/length for x in d)
        u=(-d[2],0,d[0]); ul=math.sqrt(sum(x*x for x in u))
        u=tuple(x/ul for x in u) if ul>1e-5 else (1,0,0)
        v=(d[1]*u[2]-d[2]*u[1],d[2]*u[0]-d[0]*u[2],d[0]*u[1]-d[1]*u[0])
        def p(center,k):
            angle=k*math.tau/6
            return tuple(center[i]+width*(u[i]*math.cos(angle)+v[i]*math.sin(angle)) for i in range(3))
        for k in range(6):
            model.quad(material,(p(a,k),p(b,k),p(b,k+1),p(a,k+1)),u)

# 最終装備と形成途中の身体・装飾は同じ座標・同じ材質から分離する。
gltf=json.loads((ROYAL/'slime_crown.gltf').read_text(encoding='utf-8'))
gltf['materials'][0]['pbrMetallicRoughness']['baseColorFactor']=[1,.77,.25,1]
gltf['materials'][0]['pbrMetallicRoughness']['roughnessFactor']=.24
regalia=next(i for i,n in enumerate(gltf['nodes']) if n.get('name')=='CrownSlimeRegalia')
(ROYAL/'slime_crown.gltf').write_text(json.dumps(gltf,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
for name,roots in [('slime_crown_body',[i for i in gltf['scenes'][0]['nodes'] if i!=regalia]),('slime_crown_regalia',[regalia])]:
    variant=copy.deepcopy(gltf);variant['scenes'][0]['nodes']=roots
    path=ROYAL/(name+'.gltf');path.write_text(json.dumps(variant,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');meta(path)

# 欠片の菱形と亀裂を、身体内部にある独立した発光核として再利用する。
rune=ObjBuilder()
tube(rune,[(0,.15,.025),(.18,.38,.025),(0,.62,.025),(-.18,.38,.025),(0,.15,.025)],.016)
tube(rune,[(0,.25,.025),(.035,.34,.025),(-.035,.43,.025),(0,.53,.025)],.012)
save(rune,'crown_inheritance_inner_rune')

# 羽の白い部分に王冠の金色テクスチャを流用しない専用材質。
(OUT/'crown_inheritance_wings.mtl').write_text('newmtl CrownGold\nKd 0.925490 0.737255 0.298039\nPr 0.26\nPm 0.7\n\nnewmtl SlimeHighlight\nKd 1 0.988235 1\nPr 0.5\nPm 0\n',encoding='utf-8')
# 左右の羽は根元から先端へ湾曲する立体形状。直方体を翼として代用しない。
for side,label in [(-1,'left'),(1,'right')]:
    wing=ObjBuilder()
    tube(wing,[(side*.20,.28,-.16),(side*.56,.52,-.18),(side*.95,.75,-.30),(side*1.32,.88,-.46)],.042,'CrownGold')
    for feather in range(8):
        base=(side*(.38+feather*.115),.44+feather*.065,-.20-feather*.035)
        tip=(side*(1.08+feather*.135),.68+feather*.10,-.65-feather*.11)
        d=tuple(tip[i]-base[i] for i in range(3))
        # 扁平な羽の断面。先端は尖り、表面に稜線と金縁を付ける。
        previous=None
        for step in range(13):
            t=step/12;w=.087*(math.sin(math.pi*t)**.65)
            c=tuple(base[i]+d[i]*t for i in range(3));c=(c[0],c[1]+.10*math.sin(math.pi*t),c[2])
            ring=[(c[0],c[1]+w,c[2]),(c[0],c[1],c[2]+w*.24),(c[0],c[1]-w,c[2]),(c[0],c[1],c[2]-w*.24)]
            if previous:
                for k in range(4):
                    wing.quad('SlimeHighlight',(previous[k],ring[k],ring[(k+1)%4],previous[(k+1)%4]),(0,1,0))
            previous=ring
        tube(wing,[base,tip],.009,'CrownGold')
    path=OUT/('crown_inheritance_wing_'+label+'.obj');write_obj(wing,path,'crown_inheritance_wings.mtl','Wing'+label);meta(path)

crack_segments=[]
# 地割れは地面へ固定し、テクスチャのスクロールで動かさない。
cracks=ObjBuilder()
for branch in range(18):
    angle=branch*math.tau/18
    points=[]
    for k in range(11):
        r=3+k*3.8;theta=angle+.04*math.sin(branch*5+k*3)
        points.append((math.sin(theta)*r,.015+branch*.0002,math.cos(theta)*r))
    crack_segments.extend(zip(points,points[1:]));tube(cracks,points,.028,'CrownBlack');tube(cracks,[(x,y+.04,z) for x,y,z in points],.009)
    for k in (4,7):
        p=points[k];theta=angle+.30*(-1 if k%2 else 1)
        tube(cracks,[p,(p[0]+math.sin(theta)*3,p[1],p[2]+math.cos(theta)*3),
            (p[0]+math.sin(theta+.12)*7,p[1],p[2]+math.cos(theta+.12)*7)],.008)
save(cracks,'crown_inheritance_ground_fractures')
# 地割れを床片と同じ格子で切り、落下中に空中の線だけが残ることを防ぐ。
for ix in range(8):
 for iz in range(8):
  if (ix<=1 or ix>=6) and iz in (0,7): continue
  part=ObjBuilder();pivot=(-35+ix*10,0,-35+iz*10)
  for a,b in crack_segments:
   lo,hi=0.,1.
   for axis in (0,2):
    lower=pivot[axis]-5;upper=pivot[axis]+5;d=b[axis]-a[axis]
    if abs(d)<1e-8:
     if not lower<=a[axis]<=upper:hi=-1;break
    else:
     x,y=sorted(((lower-a[axis])/d,(upper-a[axis])/d));lo=max(lo,x);hi=min(hi,y)
   if hi<=lo:continue
   points=[tuple(a[k]+(b[k]-a[k])*t-pivot[k]+(.89 if k==1 else 0) for k in range(3)) for t in (lo,hi)]
   tube(part,points,.042,'CrownBlack')
   tube(part,[(x,y+.035,z) for x,y,z in points],.009,'CoreGlow')
  if not part.faces:continue
  path=OUT/f'crown_inheritance_cracks_{ix*8+iz:02}.obj'
  write_obj(part,path,'false_king_slime.mtl','FloorCracks');meta(path)
print('Body, regalia, internal rune, curved wings and fixed ground fractures generated.')

# 戦闘床の表示モデルは一枚です。衝突用の箱だけ落としても床が割れて見えないので、
# 元の三角形を10m格子で切り、材質・UV・法線を保った64個の実際の床片へ分けます。
arena=ROOT/'Resources/3DModel/Stages/high_crown/high_crown_arena.obj'
lines=arena.read_text(encoding='utf-8').splitlines()
vertices=[tuple(map(float,l.split()[1:])) for l in lines if l.startswith('v ')]
uvs=[tuple(map(float,l.split()[1:])) for l in lines if l.startswith('vt ')]
normals=[tuple(map(float,l.split()[1:])) for l in lines if l.startswith('vn ')]
chunks=[ObjBuilder() for _ in range(64)]
def clip(poly,axis,bound,sign):
    result=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        da=sign*(a[0][axis]-bound);db=sign*(b[0][axis]-bound)
        if da>=-1e-7: result.append(a)
        if (da<0)!=(db<0):
            t=da/(da-db)
            result.append(tuple(tuple(x+(y-x)*t for x,y in zip(av,bv)) for av,bv in zip(a,b)))
    return result
material='Stone'
for line in lines:
    if line.startswith('usemtl '):material=line.split()[1]
    if not line.startswith('f '):continue
    refs=[tuple(map(int,r.split('/'))) for r in line.split()[1:]]
    poly=[(vertices[v-1],uvs[t-1],normals[n-1]) for v,t,n in refs]
    xlo=max(0,min(7,int((min(p[0][0] for p in poly)+40)/10)));xhi=max(0,min(7,int((max(p[0][0] for p in poly)+40)/10)))
    zlo=max(0,min(7,int((min(p[0][2] for p in poly)+40)/10)));zhi=max(0,min(7,int((max(p[0][2] for p in poly)+40)/10)))
    for ix in range(xlo,xhi+1):
        for iz in range(zlo,zhi+1):
            cut=poly
            for axis,bound,sign in [(0,-40+ix*10,1),(0,-30+ix*10,-1),(2,-40+iz*10,1),(2,-30+iz*10,-1)]:
                if cut:cut=clip(cut,axis,bound,sign)
            model=chunks[ix*8+iz];pivot=(-35+ix*10,0,-35+iz*10)
            if len(cut)>=3:
                ids=[model.vertex(tuple(p[i]-pivot[i] for i in range(3)),uv,n) for p,uv,n in cut]
                for k in range(1,len(ids)-1):model.face(material,ids[0],ids[k],ids[k+1],False)
for i,model in enumerate(chunks):
    if not model.faces: continue
    path=arena.parent/f'high_crown_fracture_{i:02}.obj'
    write_obj(model,path,'high_crown.mtl',f'ArenaFracture{i:02}');meta(path)
assert sum(bool(model.faces) for model in chunks)==56
print('Original arena geometry cut into 56 textured floor chunks.')
