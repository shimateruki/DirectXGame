"""セレクトの配置を保持して草地化する一度限りの移行。既定は検証のみ。"""
import argparse
import copy
import json
import math
import random
import shutil
import sys
import tempfile
import uuid
from pathlib import Path

sys.dont_write_bytecode = True
from extend_stage1_meadow import read_obj

ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / 'Resources/3DModel'
OUT = MODELS / 'Stages/stage_select_garden'
SCENE = ROOT / 'Resources/json/3Dobject/stageSelect_object.json'
PREFIX = 'StageSelect_Garden_'
EXPANSION = 1.35


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='検証後に生成・適用する')
    args = parser.parse_args()
    scene = json.loads(SCENE.read_text(encoding='utf-8'))
    before = copy.deepcopy(scene)
    objects = scene['objects']
    if any(o['name'].startswith(PREFIX) for o in objects):
        raise SystemExit('移行済み。エディターでの調整を保護するため再適用しません。')
    by_name = {o['name']: o for o in objects}
    by_guid = {o['guid']: o for o in objects}
    # 階層は整理用の単位変換であることを確認。親を二重に拡大しない。
    for o in objects:
        if o.get('parentGuid'):
            parent = by_guid[o['parentGuid']]
            assert parent['position'] == [0, 0, 0] and parent['scale'] == [1, 1, 1]
            assert all(abs(v) < 1e-5 for v in parent['rotation'])

    expanded = set()
    for o in objects:
        if o['name'].startswith('[Group]'):
            continue
        o['position'][0] *= EXPANSION
        o['position'][2] *= EXPANSION
        model = o.get('modelName', '')
        if o['type'] == 'InvisibleBox' or model.startswith('Stages/stage_select_') or o['name'] == 'StageSelect_Ocean':
            o['scale'][0] *= EXPANSION
            o['scale'][2] *= EXPANSION
            expanded.add(o['name'])

    stage1 = json.loads((ROOT/'Resources/json/3Dobject/stage1_object.json').read_text(encoding='utf-8'))
    ocean_source = next(o for o in stage1['objects'] if o['name'] == 'Stage1_Ocean')
    ocean = by_name['StageSelect_Ocean']
    for field in ('waterParam', 'color', 'envIntensity', 'roughness', 'enableEnvMap', 'enableLighting'):
        ocean[field] = copy.deepcopy(ocean_source[field])
    assert ocean['waterParam']['effectType'] == 2
    added = []
    generated = {}

    def add(label, model, position, material, parent, scale=(1, 1, 1)):
        name = PREFIX + label
        o = dict(name=name, guid=str(uuid.uuid5(uuid.NAMESPACE_URL, name)), type='Model',
                 modelName=model, position=list(position), rotation=[0, 0, 0], scale=list(scale),
                 color=[1, 1, 1, 1], materialType=material, roughness=.92, metallic=0,
                 enableLighting=True, enableEnvMap=False, enableNormalMap=False,
                 envIntensity=.8, emissive=1, castShadow=True, isStatic=True,
                 collisionAttribute=0, collisionMask=0, collider={'type': 0},
                 parentGuid=parent['guid'], parentName=parent['name'], layer='Default', saveCategory='Object')
        added.append(o)
        return o

    def mesh(name, arrays, faces, kind):
        vertices, uvs, normals, out_faces = [], [], [], []
        for material, face in faces:
            ids = []
            for ref in face:
                p = arrays['v'][ref[0]-1]
                uv = arrays['vt'][ref[1]-1] if len(ref)>1 and ref[1] else (0, 0)
                normal = arrays['vn'][ref[2]-1] if len(ref)>2 and ref[2] else (0, 1, 0)
                if kind == 'soil': uv = (1, max(0, -p[1]))
                vertices.append(p); uvs.append(uv); normals.append(normal); ids.append(len(vertices))
            out_faces.append((material if kind == 'rim' else 'Meadow', ids))
        lines = ['mtllib surface.mtl', 'o '+name]
        for tag, values in (('v',vertices), ('vt',uvs), ('vn',normals)):
            lines.extend(tag+' '+' '.join(f'{v:.6f}' for v in p) for p in values)
        for material, ids in out_faces:
            lines.extend(['usemtl '+material, 'f '+' '.join(f'{i}/{i}/{i}' for i in ids)])
        generated[OUT/(name+'.obj')] = '\n'.join(lines)+'\n'
        return 'Stages/stage_select_garden/'+name+'.obj'

    material_path = MODELS/'Stages/stage_select_hub_island/stage_select_hub_island.mtl'
    generated[OUT/'surface.mtl'] = material_path.read_text(encoding='utf-8')+'\nnewmtl Meadow\nKd 1 1 1\nKs 0.04 0.04 0.04\nNs 4\nillum 2\n'
    partition = {}
    for kind in ('hub', 'gate', 'horizon'):
        base = 'stage_select_horizon_islet' if kind == 'horizon' else f'stage_select_{kind}_island'
        arrays, faces = read_obj(MODELS/f'Stages/{base}/{base}.obj')
        groups = {'turf': [], 'soil': [], 'rim': []}
        for mat, face in faces:
            target = 'turf' if mat == 'GrassTop' else 'soil' if mat in ('GrassEdge','CliffWarm','CliffDeep') else 'rim'
            groups[target].append((mat, face))
        assert sum(map(len, groups.values())) == len(faces)
        partition[kind] = {part: mesh(kind+'_'+part, arrays, group, part) for part, group in groups.items()}

    islands = [('Hub', 'StageSelect_CentralIsland', (28,24), 'hub')]
    islands += [(str(i), 'StageIsland_'+str(i), (15,12.5), 'gate') for i in range(3)]
    grass_arrays, grass_faces = read_obj(MODELS/'Stages/soft_grass/soft_grass.obj')
    grass_count = 0
    for number, (label, owner_name, radii, kind) in enumerate(islands):
        owner = by_name[owner_name]
        parent = by_guid[owner['parentGuid']]
        owner['modelName'] = partition[kind]['rim']
        owner.update(materialType=34, color=[1,1,1,1], emissive=1, roughness=.92, castShadow=True, enableLighting=True)
        for part, material in (('turf',29), ('soil',33)):
            add(label+'_'+part, partition[kind][part], owner['position'], material, parent, owner['scale'])
        cx, y, cz = owner['position']
        rx, rz = radii[0]*owner['scale'][0], radii[1]*owner['scale'][2]
        # 通路・到着地点・ゲートを空ける。草は群生と疎らな場所を混在させる。
        gates = [o['position'] for o in objects if o.get('gimmickType') == 'StageGate']
        def allowed(x,z,margin=1.5):
            if ((x-cx)/(rx-margin))**2 + ((z-cz)/(rz-margin))**2 > .87**2: return False
            if any((x-g[0])**2+(z-g[2])**2 < (6.5+margin)**2 for g in gates): return False
            if label == 'Hub':
                if x*x+z*z < (11+margin)**2: return False
                for dx,dz in ((-48,28),(50,24),(6,-54),(0,15)):
                    length=math.hypot(dx,dz); dx/=length; dz/=length
                    if x*dx+z*dz > 0 and abs(x*dz-z*dx)<4.7+margin: return False
            else:
                length=math.hypot(cx,cz); dx,dz=cx/length,cz/length
                if abs((x-cx)*dz-(z-cz)*dx)<4.8+margin: return False
            return True
        rng=random.Random(92600+number)
        positions=[]
        for _ in range(5000):
            if len(positions) >= (260 if label=='Hub' else 95): break
            x,z=rng.uniform(cx-rx,cx+rx),rng.uniform(cz-rz,cz+rz)
            density=.40+.28*math.sin(x*.36+number)+.22*math.cos(z*.47-number)
            if not allowed(x,z) or rng.random()>density: continue
            if any((x-a)**2+(z-b)**2<.75**2 for a,b in positions): continue
            positions.append((x,z))
        for chunk in range(0,len(positions),40):
            ga={'v':[],'vt':[],'vn':[]}; gf=[]
            for x,z in positions[chunk:chunk+40]:
                size=rng.uniform(.95,1.5); stretch=1.8
                angle=rng.uniform(0,math.tau); co,si=math.cos(angle),math.sin(angle)
                offsets={k:len(v) for k,v in ga.items()}
                ga['v'].extend((x-cx+(px*co-pz*si)*size,py*size*stretch,-(z-cz)+(px*si+pz*co)*size) for px,py,pz in grass_arrays['v'])
                ga['vt'].extend(grass_arrays['vt'])
                for nx,ny,nz in grass_arrays['vn']:
                    n=(nx*co-nz*si,ny/stretch,nx*si+nz*co); length=math.sqrt(sum(v*v for v in n))
                    ga['vn'].append(tuple(v/length for v in n))
                gf.extend(('Meadow',[tuple(i+offsets[k] for k,i in zip(('v','vt','vn'),ref)) for ref in f]) for _,f in grass_faces)
            name=label+'_Grass_'+str(chunk//40)
            add(name,mesh(name,ga,gf,'grass'),(cx,y+.025,cz),30,parent)
        grass_count+=len(positions)
        # 人の導線外へ木と低木・花・石をまとめる。個別Objectなので後から編集できる。
        beds=[]
        for _ in range(300):
            if len(beds)>= (2 if label=='Hub' else 2): break
            x,z=rng.uniform(cx-rx,cx+rx),rng.uniform(cz-rz,cz+rz)
            if not allowed(x,z,4) or any((x-a)**2+(z-b)**2<100 for a,b in beds): continue
            beds.append((x,z))
        for index,(x,z) in enumerate(beds):
            tree=add(label+f'_Tree_{index}','Stages/entry_meadow/garden_tree_soft.obj',(x,y,z),31,parent,(4.0,4.3,4.0))
            tree['rotation'][1]=rng.uniform(0,math.tau);tree['roughness']=.62
            for suffix,model,dx,dz,scale in (('Rock','bed_rocks_0',1.3,.5,1.2),('Flowers','bed_flowers_0',-1.4,.9,1.5),('Shrub','bed_shrub_0',.3,-1.2,1.0)):
                add(label+f'_{suffix}_{index}','Stages/entry_meadow/'+model+'.obj',(x+dx,y,z+dz),34,parent,(scale,)*3)

    for o in objects:
        if o.get('modelName') == 'Stages/stage_select_horizon_islet':
            o.update(modelName=partition['horizon']['rim'], materialType=34, color=[1,1,1,1],
                     emissive=1, roughness=.92, enableLighting=True)
            parent=by_guid[o['parentGuid']]
            for part,material in (('turf',29),('soil',33)):
                surface=add(o['name']+'_'+part,partition['horizon'][part],o['position'],material,parent,o['scale'])
                surface['rotation']=copy.deepcopy(o['rotation'])
                surface['quaternion']=copy.deepcopy(o['quaternion'])
        if o['name'].startswith('StageSelect_Horizon_Palm_'):
            o.update(materialType=34,roughness=.9,enableLighting=True,castShadow=True)
        if o['name'].startswith('StageSelect_Tree_'):
            o.update(modelName='Stages/entry_meadow/garden_tree_soft.obj', materialType=31,
                     scale=[4.2,4.5,4.2],color=[1,1,1,1],emissive=1,roughness=.62,castShadow=True,enableLighting=True)
        if o['name']=='StageSelect_HubPlaza': o.update(materialType=34,roughness=.9,castShadow=True)
    # ID・進行条件・ローカルCollider定義は保持し、Transformのみで地形と判定を同率拡張。
    for old,new in zip(before['objects'],objects):
        for key in ('guid','name','parentGuid','parentName','param','components','targetID','collider','rotation','quaternion'):
            assert old.get(key)==new.get(key),(old['name'],key)
        if old['name'] in expanded:
            for axis in (0,2): assert abs(new['scale'][axis]-old['scale'][axis]*EXPANSION)<1e-6
        assert old['position'][1]==new['position'][1]
    objects.extend(added)
    assert len({o['guid'] for o in objects})==len(objects)
    for o in added:
        model=MODELS/o['modelName']
        assert model in generated or model.exists(),model
    player_path=ROOT/'Resources/json/3Dobject/stageSelect_player.json'
    player=json.loads(player_path.read_text(encoding='utf-8'))
    for o in player['objects']:
        for axis in (0,2): o['position'][axis]*=EXPANSION
    light_path=ROOT/'Resources/json/light/stageSelect.json'
    light=json.loads(light_path.read_text(encoding='utf-8'))
    sunny=json.loads((ROOT/'Resources/json/light/stage1_sunny.json').read_text(encoding='utf-8'))
    light['directionalLight']=sunny['directionalLight']
    light['shadow']={'resolution':4096,'areaSize':120.0}
    descriptor_path=ROOT/'Resources/json/3Dobject/stageSelect.json'
    descriptor=json.loads(descriptor_path.read_text(encoding='utf-8'))
    descriptor['_sceneAsset']['resources']['light']='Resources/json/light/stageSelect.json'
    for path,data in ((SCENE,scene),(player_path,player),(light_path,light),(descriptor_path,descriptor)):
        generated[path]=json.dumps(data,ensure_ascii=False,indent=4)+'\n'
    print(f'Validated: {len(before["objects"])} existing objects preserved, {len(added)} decorations, {grass_count} grass clumps; horizontal scale {EXPANSION}; water mode 2')
    if args.apply:
        assert not OUT.exists(), '生成先が存在します。既存アセットを上書きしません。'
        backup=Path(tempfile.mkdtemp(prefix='cg2-select-garden-'))
        for path in (SCENE,player_path,light_path,descriptor_path):
            shutil.copy2(path,backup/(path.parent.name+'_'+path.name))
        for path,text in generated.items():
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text(text,encoding='utf-8')
        print('Applied. Backup:',backup)
    else: print('Dry run only. --applyで適用します。')


if __name__=='__main__': main()
