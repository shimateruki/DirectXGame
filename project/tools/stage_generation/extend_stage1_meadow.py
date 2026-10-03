"""入口の景観をステージ1へ展開。描画面のみ分割し、ゲーム用オブジェクトは保持する。"""
import copy
import json
import math
import random
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / 'Resources/3DModel'
OUT = MODELS / 'Stages/stage1_meadow'
SCENE = ROOT / 'Resources/json/3Dobject/stage1_object.json'
PREFIX = 'Stage1_Meadow_'


def read_obj(path):
    arrays = {'v': [], 'vt': [], 'vn': []}
    faces = []
    material = ''
    for line in path.read_text(encoding='utf-8').splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] in arrays:
            arrays[p[0]].append(tuple(map(float, p[1:])))
        elif p[0] == 'usemtl':
            material = p[1]
        elif p[0] == 'f':
            faces.append((material, [tuple(int(i) if i else 0 for i in q.split('/')) for q in p[1:]]))
    return arrays, faces


def write_obj(name, arrays, faces, kind):
    # 個別頂点に展開し、元の面の向きと法線をそのまま保持する。
    vertices, uvs, normals, indices = [], [], [], []
    for mat, face in faces:
        top = max(arrays['v'][i[0]-1][1] for i in face)
        ids = []
        for ref in face:
            p = arrays['v'][ref[0]-1]
            uv = arrays['vt'][ref[1]-1] if len(ref)>1 and ref[1] else (0, 0)
            n = arrays['vn'][ref[2]-1] if len(ref)>2 and ref[2] else (0, 1, 0)
            if kind == 'soil': uv = (1, max(0, top-p[1]))
            if kind == 'path': uv = (0, .5)
            vertices.append(p); uvs.append(uv); normals.append(n); ids.append(len(vertices))
        indices.append((mat if kind == 'remaining' else 'Meadow', ids))
    lines = ['mtllib surface.mtl' if kind != 'remaining' else 'mtllib '+name.split('_')[0]+'_original.mtl', 'o '+name]
    for tag, values in (('v', vertices), ('vt', uvs), ('vn', normals)):
        lines.extend(tag+' '+' '.join(f'{v:.6f}' for v in p) for p in values)
    for mat, ids in indices:
        lines.extend(['usemtl '+mat, 'f '+' '.join(f'{i}/{i}/{i}' for i in ids)])
    (OUT/(name+'.obj')).write_text('\n'.join(lines)+'\n', encoding='utf-8')


def inside(x, z, polygon):
    hit = False
    for a,b in zip(polygon, polygon[1:]+polygon[:1]):
        if (a[2]>z) != (b[2]>z) and x < (b[0]-a[0])*(z-a[2])/(b[2]-a[2])+a[0]: hit = not hit
    return hit


def main():
    scene = json.loads(SCENE.read_text(encoding='utf-8'))
    if any(o['name'].startswith(PREFIX) for o in scene['objects']):
        if '--refresh-owned' not in sys.argv:
            raise SystemExit('配置済みです。エディターで調整した値を保護するため再配置を中止します。')
        scene['objects'] = [o for o in scene['objects'] if not o['name'].startswith(PREFIX)]
    before = copy.deepcopy(scene)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'surface.mtl').write_text('newmtl Meadow\nKd 1 1 1\nKs 0.04 0.04 0.04\nNs 4\nillum 2\n', encoding='utf-8')
    source = MODELS/'Stages/entry_meadow/course_meadow.obj'
    added = []

    def add(name, model, position, material, scale=(1,1,1)):
        obj = dict(name=PREFIX+name,guid=str(uuid.uuid5(uuid.NAMESPACE_URL,PREFIX+name)),
            type='Model',modelName=model,position=list(position),rotation=[0,0,0],scale=list(scale),
            color=[1,1,1,1],materialType=material,roughness=.92,metallic=0,
            enableLighting=True,enableEnvMap=False,enableNormalMap=False,envIntensity=.8,
            emissive=1,castShadow=True,isStatic=True,collisionAttribute=0,collisionMask=0,
            collider={'type':0},layer='Default',saveCategory='Object')
        added.append(obj)
        return obj

    grass_source, grass_faces = read_obj(MODELS/'Stages/soft_grass/soft_grass.obj')
    exclusions = [o['position'] for o in scene['objects'] if o.get('type') not in ('Model','InvisibleBox') and 'position' in o]
    enemy_path = ROOT/'Resources/json/3Dobject/stage1_enemy.json'
    enemy_data = json.loads(enemy_path.read_text(encoding='utf-8'))
    exclusions += [o['position'] for o in enemy_data.get('objects',[]) if 'position' in o]
    total_clumps = 0
    total_faces = 0
    for label, owner_name, path in (
        ('Course','Stage1_Course_V4',source),
        ('Backdrop','Stage1_Backdrop_V4',MODELS/'Stages/star_garden_backdrop_v2/star_garden_backdrop_v2.obj')):
        owner = next(o for o in scene['objects'] if o['name']==owner_name)
        assert all(abs(v)<1e-6 for v in owner['rotation']), '回転済み地形は対応確認が必要です'
        arrays, faces = read_obj(path)
        (OUT/(label+'_original.mtl')).write_bytes(path.with_suffix('.mtl').read_bytes())
        groups = {k:[] for k in ('turf','soil','path','remaining')}
        for mat,face in faces:
            kind = 'turf' if 'grass' in mat else 'soil' if mat in ('garden_cliff_warm','garden_cliff_rose','arena_cliff','cliff_moss','backdrop_cliff') else 'path' if mat=='ruin_path' else 'remaining'
            groups[kind].append((mat,face))
        assert sum(map(len,groups.values())) == len(faces)
        for kind, subset in groups.items():
            if not subset: continue
            name=label+'_'+kind
            write_obj(name,arrays,subset,kind)
            model='Stages/stage1_meadow/'+name+'.obj'
            if kind=='remaining': owner['modelName']=model
            else: add(name,model,owner['position'],{'turf':29,'soil':33,'path':32}[kind],owner['scale'])
        total_faces += len(faces)
        print(label,{k:len(v) for k,v in groups.items()})
        if label=='Backdrop': continue  # 遠景は面の質感のみ。大量の草は描画しない。
        paths=[[arrays['v'][r[0]-1] for r in f] for m,f in groups['path']]
        for patch,(_,face) in enumerate(groups['turf']):
            poly=[arrays['v'][r[0]-1] for r in face]
            if max(p[1] for p in poly)-min(p[1] for p in poly)>.01: continue
            x0,x1=min(p[0] for p in poly),max(p[0] for p in poly)
            z0,z1=min(p[2] for p in poly),max(p[2] for p in poly)
            y=poly[0][1]; width=x1-x0; depth=z1-z0
            if min(width,depth)<3: continue
            rng=random.Random(92400+patch)
            positions=[]
            def allowed(x,z,margin):
                if not inside(x,z,poly): return False
                if not all(inside(x+dx,z+dz,poly) for dx,dz in ((margin,0),(-margin,0),(0,margin),(0,-margin))): return False
                if any(abs(p[0][1]-y)<1 and any(inside(x+dx,z+dz,p) for dx,dz in ((0,0),(margin,0),(-margin,0),(0,margin),(0,-margin))) for p in paths): return False
                if any(abs(e[1]-y)<8 and (x-e[0])**2+(-z-e[2])**2<36 for e in exclusions): return False
                return True
            # 大きな床でも中央は空け、移動・戦闘の視認性を優先する。
            for _ in range(min(1200,int(width*depth*.65))):
                x,z=rng.uniform(x0,x1),rng.uniform(z0,z1)
                edge=min(x-x0,x1-x,z-z0,z1-z)
                if edge>min(5,min(width,depth)*.22) or not allowed(x,z,.9): continue
                cluster=.5+.25*math.sin(x*.6+patch)+.25*math.sin(z*.7-patch)
                if rng.random()>cluster or any((x-a)**2+(z-b)**2<.7**2 for a,b in positions): continue
                positions.append((x,z))
            if positions:
                ga={'v':[],'vt':[],'vn':[]}; gf=[]
                cx,cz=(x0+x1)/2,(z0+z1)/2
                for x,z in positions:
                    size=rng.uniform(.65,1.05); h=1.7 if min(width,depth)>20 else 1.15
                    angle=rng.uniform(0,math.tau); co,si=math.cos(angle),math.sin(angle)
                    offsets={k:len(v) for k,v in ga.items()}
                    ga['v'].extend((x-cx+(px*co-pz*si)*size,py*size*h,z-cz+(px*si+pz*co)*size) for px,py,pz in grass_source['v'])
                    ga['vt'].extend(grass_source['vt'])
                    for nx,ny,nz in grass_source['vn']:
                        n=(nx*co-nz*si,ny/h,nx*si+nz*co); length=math.sqrt(sum(v*v for v in n))
                        ga['vn'].append(tuple(v/max(length,1e-8) for v in n))
                    gf.extend(('Meadow',[tuple(i+offsets[k] for k,i in zip(('v','vt','vn'),ref)) for ref in f]) for _,f in grass_faces)
                name=f'Grass_{patch:02d}'
                write_obj(name,ga,gf,'grass')
                add(name,'Stages/stage1_meadow/'+name+'.obj',(cx,y+.025,-cz),30)
                total_clumps+=len(positions)
            if width>=26 and depth>=24:
                for side in (0,1):
                    x=x0+5 if side==0 else x1-5; z=z0+5 if side==0 else z1-5
                    if not allowed(x,z,3.5): continue
                    if any(o.get('materialType')==31 and (o['position'][0]-x)**2+(o['position'][2]+z)**2<100 for o in scene['objects']+added): continue
                    size=3.8+rng.random()*.8
                    tree=add(f'Tree_{patch:02d}_{side}','Stages/entry_meadow/garden_tree_soft.obj',(x,y,-z),31,(size,)*3)
                    tree['roughness']=.62; tree['rotation'][1]=rng.uniform(0,360)
                    # 岩は木からずらし、地面境界から十分に内側へ置く。
                    rx=x+(2 if side==0 else -2)
                    if allowed(rx,z,3):
                        add(f'Rocks_{patch:02d}_{side}','Stages/entry_meadow/bed_rocks_0.obj',(rx,y,-z),34,(1.4,)*3)
    for o in scene['objects']:
        if o.get('modelName')=='Stages/entry_meadow/garden_tree.obj':
            o['modelName']='Stages/entry_meadow/garden_tree_soft.obj'
            o['scale']=[round(v*1.6,5) for v in o['scale']]; o['roughness']=.62
    for old,new in zip(before['objects'],scene['objects']):
        expected=copy.deepcopy(old)
        if old['name'] in ('Stage1_Course_V4','Stage1_Backdrop_V4'): expected['modelName']=new['modelName']
        if old.get('modelName')=='Stages/entry_meadow/garden_tree.obj':
            for key in ('modelName','scale','roughness'): expected[key]=new[key]
        assert expected==new, old['name']
    scene['objects'].extend(added)
    SCENE.write_text(json.dumps(scene,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    print(f'{total_faces} original faces partitioned; {total_clumps} grass clumps; {len(added)} editor objects')


if __name__=='__main__': main()
