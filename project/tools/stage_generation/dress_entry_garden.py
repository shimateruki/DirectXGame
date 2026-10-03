"""入口限定の海辺の遺跡装飾。既存の当たり判定とゲーム進行は変更しない。"""
import copy
import json
import math
import random
import sys
import uuid
from pathlib import Path
sys.dont_write_bytecode = True
from refine_entry_meadow import build_tree

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Resources/3DModel/Stages/entry_meadow'
SCENE = ROOT / 'Resources/json/3Dobject/stage1_object.json'
PREFIX = 'Stage1_EntryGarden_'


class Mesh:
    def __init__(self):
        self.vertices = []
        self.faces = []

    def triangle(self, a, b, c, material, double=False):
        u = [b[k]-a[k] for k in range(3)]
        v = [c[k]-a[k] for k in range(3)]
        normal = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        length = math.sqrt(sum(q*q for q in normal))
        if length < 1e-8:
            return
        normal = tuple(q/length for q in normal)
        start = len(self.vertices)+1
        self.vertices.extend((p, normal) for p in (a,b,c))
        # Engine-authored OBJ faces use clockwise winding viewed from outside.
        self.faces.append((material, (start+2,start+1,start)))
        if double:
            self.triangle(c,b,a,material)

    def rock(self, center, size, seed, material='Stone', segments=9):
        rng = random.Random(seed)
        rings=[]
        radii=[rng.uniform(.85,1.12) for _ in range(segments)]
        for y,r in ((-.8,.48),(-.4,.92),(.35,1),(.85,.6)):
            rings.append([(center[0]+math.cos(j*math.tau/segments)*size[0]*r*radii[j],
                           center[1]+(y+rng.uniform(-.085,.085))*size[1],
                           center[2]+math.sin(j*math.tau/segments)*size[2]*r*radii[j]) for j in range(segments)])
        for row in range(3):
            for j in range(segments):
                k=(j+1)%segments
                a,b,c,d=rings[row][j],rings[row+1][j],rings[row+1][k],rings[row][k]
                self.triangle(a,b,c,material); self.triangle(a,c,d,material)
        for j in range(1,segments-1):
            self.triangle(rings[-1][0],rings[-1][j+1],rings[-1][j],material)
            self.triangle(rings[0][0],rings[0][j],rings[0][j+1],material)

    def block(self, center, size, material):
        x,y,z=center; sx,sy,sz=(q/2 for q in size)
        bevel=min(.12,sx*.2,sy*.2,sz*.2)
        rings=[]
        for yy,inset in ((-sy,bevel),(-sy+bevel,0),(sy-bevel,0),(sy,bevel)):
            rings.append([(x+xx*(sx-inset),y+yy,z+zz*(sz-inset)) for xx,zz in ((-1,-1),(-1,1),(1,1),(1,-1))])
        for row in range(3):
            for j in range(4):
                k=(j+1)%4
                a,b,c,d=rings[row][j],rings[row+1][j],rings[row+1][k],rings[row][k]
                self.triangle(a,c,b,material); self.triangle(a,d,c,material)
        for row,order in ((0,(0,3,2,1)),(3,(0,1,2,3))):
            a,b,c,d=[rings[row][i] for i in order]
            self.triangle(a,b,c,material);self.triangle(a,c,d,material)

    def save(self,name):
        lines=['mtllib garden_props.mtl','o '+name]
        lines += ['v '+' '.join(f'{q:.6f}' for q in (p[0],p[1],-p[2])) for p,n in self.vertices]
        lines += ['vn '+' '.join(f'{q:.6f}' for q in (n[0],n[1],-n[2])) for p,n in self.vertices]
        for mat,face in self.faces:
            lines += ['usemtl '+mat,'f '+' '.join(f'{i}//{i}' for i in face)]
        (OUT/(name+'.obj')).write_text('\n'.join(lines)+'\n',encoding='utf-8')


def main():
    scene=json.loads(SCENE.read_text(encoding='utf-8'))
    before=copy.deepcopy(scene)
    palette={'Stone':(.43,.43,.34),'StoneLight':(.57,.55,.43),'StoneDark':(.32,.35,.29),
             'Moss':(.25,.39,.10),'Leaf':(.21,.40,.09),'LeafLight':(.34,.51,.13),
             'Stem':(.16,.30,.065),'Blue':(.22,.44,.82),'Violet':(.63,.34,.72),'Pollen':(.96,.73,.22)}
    (OUT/'garden_props.mtl').write_text('newmtl DefaultMaterial\nKd 1 1 1\n\n'+'\n'.join(f'newmtl {name}\nKd {r} {g} {b}\nKs 0.02 0.02 0.02\nNs 4\nillum 2\n' for name,(r,g,b) in palette.items()),encoding='utf-8')
    build_tree('garden_tree_soft',soft=True)
    added=[]
    def add(name,model,pos,scale=(1,1,1)):
        added.append(dict(name=PREFIX+name,guid=str(uuid.uuid5(uuid.NAMESPACE_URL,PREFIX+name)),
            type='Model',modelName='Stages/entry_meadow/'+model+'.obj',position=list(pos),rotation=[0,0,0],scale=list(scale),
            color=[1,1,1,1],materialType=34,roughness=.9,metallic=0,enableLighting=True,enableEnvMap=False,
            enableNormalMap=False,emissive=1,castShadow=True,isStatic=True,collisionAttribute=0,collisionMask=0,
            collider={'type':0},saveCategory='Object',layer='Default'))

    # Remove only the old visual gateway, retaining all collision/trigger objects.
    path=OUT/'course_meadow.obj'; vertices=[]; lines=[]; removed=0; material=''
    for line in path.read_text(encoding='utf-8').splitlines():
        p=line.split()
        if p and p[0]=='v': vertices.append(tuple(map(float,p[1:])))
        if p and p[0]=='usemtl': material=p[1]
        if p and p[0]=='f':
            points=[vertices[int(q.split('/')[0])-1] for q in p[1:]]
            if material=='garden_cliff_warm' and all(-360.001<=x<=-299.999 and -13.001<=y<=.001 and 27.999<=z<=72.001 for x,y,z in points):
                if any(all(abs(q[axis]-value)<.001 for q in points) for axis,value in ((0,-360),(0,-300),(2,28),(2,72))):
                    removed+=1;continue
            if all(-310.001<=x<=-299.999 and -.001<=y<=7.001 and 48.999<=z<=51.001 for x,y,z in points):
                removed+=1;continue
        lines.append(line)
    path.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    gate=Mesh()
    for x in (-4.1,4.1):
        for row in range(6):
            gate.block((x,.45+row*.9,0),(1.8,.87,2), 'StoneLight' if row%3==0 else 'Stone')
    for j in range(5):
        gate.block((-4+j*2,6.2,0),(1.96,1.6,2),'StoneLight' if j%2 else 'Stone')
    for x in (-4.1,4.1):
        gate.block((x,5.38,0),(1.8,.08,1.9),'Moss')
    gate.save('ruin_gateway'); add('RuinGateway','ruin_gateway',(-305,0,-50))

    # Keep the original island silhouette/collision, replacing only its side finish.
    soil=Mesh()
    corners=[(-30,-22),(-30,22),(30,22),(30,-22)]
    for j in range(4):
        x,z=corners[j]; xx,zz=corners[(j+1)%4]
        a,b,c,d=(x,0,z),(xx,0,zz),(xx,-13,zz),(x,-13,z)
        soil.triangle(a,c,b,'DefaultMaterial');soil.triangle(a,d,c,'DefaultMaterial')
    soil.save('island_soil');add('IslandSoil','island_soil',(-330,0,-50))
    added[-1]['materialType']=33
    for index,(x,z,size,angle) in enumerate(((-319,-32,3.75,37),(-309,-68,4.93,122),(-340,-38,3.68,213))):
        add(f'Tree{index+1}','garden_tree_soft',(x,0,z),(size,size,size))
        added[-1].update(materialType=31,roughness=.62,envIntensity=.8,rotation=[0,angle,0])

    # Group props as individual editor objects, leaving the path and arrival zone clear.
    beds=[(-331,-66,1.0),(-320,-35,1.15),(-307,-65,.85),(-341,-34,.85),(-313,-60,.7)]
    for index,(x,z,size) in enumerate(beds):
        rocks=Mesh()
        rocks.rock((0,.48,0),(1.65,.9,1.2),index)
        rocks.rock((1.6,.22,.75),(.85,.5,.7),index+20,'StoneLight')
        rocks.save(f'bed_rocks_{index}');add(f'Bed{index}_Rocks',f'bed_rocks_{index}',(x,0,z),(size*1.6,)*3)
        shrub=Mesh(); rng=random.Random(5500+index)
        for j in range(95):
            a=rng.uniform(0,math.tau); r=math.sqrt(rng.random())*1.6
            center=(math.cos(a)*r, .35+1.15*math.sqrt(max(0,1-(r/1.7)**2)),math.sin(a)*r)
            angle=rng.uniform(0,math.tau); dx,dz=math.cos(angle),math.sin(angle)
            points=[(center[0]+dx*along-dz*wide,center[1]+ridge,center[2]+dz*along+dx*wide) for along,wide,ridge in ((-.22,0,0),(0,.20,.06),(.3,0,0),(0,-.20,.06),(0,0,.13))]
            mat='LeafLight' if j%3 else 'Leaf'
            for a,b,c in ((0,1,4),(1,2,4),(2,3,4),(3,0,4)):shrub.triangle(points[a],points[b],points[c],mat,True)
        shrub.save(f'bed_shrub_{index}');add(f'Bed{index}_Shrub',f'bed_shrub_{index}',(x+2.2,0,z+1.6),(size*1.3,)*3)
        flowers=Mesh()
        for j in range(16):
            fx,fz=rng.uniform(-2,2),rng.uniform(-1,1)
            h=rng.uniform(.35,.65)
            flowers.block((fx,h*.5,fz),(.04,h,.04),'Stem')
            for k in range(5):
                a=k*math.tau/5
                center=(fx,h,fz); tip=(fx+math.cos(a)*.23,h+.06,fz+math.sin(a)*.23)
                left=(fx+math.cos(a-.5)*.13,h+.1,fz+math.sin(a-.5)*.13)
                right=(fx+math.cos(a+.5)*.13,h+.1,fz+math.sin(a+.5)*.13)
                mat='Blue' if index%2==0 else 'Violet'
                flowers.triangle(center,left,tip,mat,True);flowers.triangle(center,tip,right,mat,True)
            flowers.rock((fx,h+.04,fz),(.055,.04,.055),j,'Pollen',5)
        flowers.save(f'bed_flowers_{index}');add(f'Bed{index}_Flowers',f'bed_flowers_{index}',(x-.5,0,z+2.5),(size*1.25,size*1.5,size*1.25))
    for obj in scene['objects']:
        if obj.get('name','').startswith(PREFIX) and obj.get('materialType')==0:
            obj['materialType']=34
        if obj.get('name','').startswith('Stage1_EntryMeadow_Grass_'):
            obj['castShadow']=True
        if obj.get('name')=='Stage1_Decor_V4_01':
            obj['modelName']='Stages/entry_meadow/garden_tree_soft.obj'
    for old,obj in zip(before['objects'],scene['objects']):
        expected=copy.deepcopy(old)
        if obj.get('name','').startswith(PREFIX) and old.get('materialType')==0:expected['materialType']=34
        if obj['name'].startswith('Stage1_EntryMeadow_Grass_'):expected['castShadow']=True
        if obj['name']=='Stage1_Decor_V4_01':expected['modelName']='Stages/entry_meadow/garden_tree_soft.obj'
        assert obj==expected
    scene['objects']=[o for o in scene['objects'] if o.get('name')!=PREFIX+'CoastalRim']
    existing={o['name'] for o in scene['objects']}
    # Idempotent: preserve user edits to placed objects on subsequent asset regeneration.
    scene['objects'].extend(o for o in added if o['name'] not in existing)
    SCENE.write_text(json.dumps(scene,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    print(f'Garden: {len(added)} prop groups, removed {removed} replaced surface faces; gameplay objects unchanged')


if __name__=='__main__':main()
