"""入口の重複装飾面を除いた派生コースと、幹・葉を分離して揺らす樹木を生成。"""
import copy
import json
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Resources/3DModel/Stages/entry_meadow"
SCENE = ROOT / "Resources/json/3Dobject/stage1_object.json"


def build_tree(output_name="garden_tree", soft=False):
    vertices, normals, uv, faces = [], [], [], []

    def vertex(p, n, leaf, tip=0.0, phase=0.5):
        length = math.sqrt(sum(q*q for q in n)) or 1
        vertices.append(p)
        normals.append(tuple(q/length for q in n))
        # Importer flips V. Only canopy has a resulting V above 0.5.
        uv.append((phase, 0.45 - 0.45 * tip if leaf else 1.0))
        return len(vertices)

    def branch(start, end, r0, r1):
        d = [end[i]-start[i] for i in range(3)]
        length = math.sqrt(sum(q*q for q in d))
        d = [q/length for q in d]
        u = [d[1], -d[0], 0]
        ul = math.sqrt(sum(q*q for q in u))
        u = [q/ul for q in u]
        v = [d[1]*u[2]-d[2]*u[1], d[2]*u[0]-d[0]*u[2], d[0]*u[1]-d[1]*u[0]]
        rings = []
        for center, radius in ((start, r0), (end, r1)):
            ring = []
            for j in range(10):
                a = math.tau*j/10
                n = [u[k]*math.cos(a)+v[k]*math.sin(a) for k in range(3)]
                ring.append(vertex(tuple(center[k]+n[k]*radius for k in range(3)), n, False))
            rings.append(ring)
        for j in range(10):
            k = (j+1)%10
            faces.extend(((rings[0][j],rings[1][j],rings[1][k]),(rings[0][j],rings[1][k],rings[0][k])))

    branch((0,0,0), (0.07,0.95,0.02), 0.21,0.14)
    branch((0.07,0.95,0.02), (-0.05,1.8,0), 0.14,0.055)
    if soft:
        # Root flares anchor the tapered trunk, without changing its collision footprint.
        for j in range(5):
            angle=math.tau*j/5
            branch((0,.35,0),(.36*math.cos(angle),.035,.36*math.sin(angle)),.10,.025)
    centers = [(-0.50,1.38,0.04,0.55,0.34,0.48),
               (0.48,1.51,-0.05,0.58,0.37,0.48),
               (-0.17,1.66,0.43,0.58,0.37,0.48),
               (0.12,1.81,-0.35,0.60,0.42,0.48),
               (-0.24,2.02,-0.05,0.56,0.42,0.50),
               (0.33,1.99,0.22,0.44,0.33,0.38)]
    for i,(cx,cy,cz,sx,sy,sz) in enumerate(centers):
        branch((0.04,0.83+(i%3)*0.17,0), (cx,cy,cz), 0.075,0.018)
        # Folded individual leaves replace the opaque canopy shells.
        rng = random.Random(550+i)
        for j in range(110):
            a = rng.uniform(0, math.tau)
            elevation = rng.uniform(-0.65,1.0)
            radius = rng.uniform(0.25,1.0)
            horizontal = math.sqrt(1-elevation*elevation)
            center = (cx+math.cos(a)*sx*horizontal*radius, cy+elevation*sy*radius, cz+math.sin(a)*sz*horizontal*radius)
            if soft: a += rng.uniform(-1.2,1.2)
            direction=(math.cos(a),rng.uniform(-0.25,0.5),math.sin(a))
            across=(-math.sin(a),0,math.cos(a))
            points=[]
            size=rng.uniform(0.8,1.3)
            phase=rng.random()
            shape = ((0,0,0),(.12,.075,0),(.30,0,0),(.12,-.075,0),(.13,0,.035))
            if soft: shape=((0,0,0),(.10,.115,0),(.22,0,0),(.10,-.115,0),(.11,0,.025))
            for along,width,ridge in shape:
                points.append(tuple(center[k]+(direction[k]*along+across[k]*width+(ridge if k==1 else 0))*size for k in range(3)))
            for a0,b0,c0 in ((0,1,4),(1,2,4),(2,3,4),(3,0,4)):
                pa,pb,pc=points[a0],points[b0],points[c0]
                u=[pb[k]-pa[k] for k in range(3)]; v=[pc[k]-pa[k] for k in range(3)]
                n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
                for back in (False,True):
                    ids=[vertex(points[k],tuple(-q for q in n) if back else n,True,(0,.45,1,.45,.5)[k],phase) for k in (a0,b0,c0)]
                    faces.append(tuple(reversed(ids)) if back else tuple(ids))
    lines=["mtllib meadow.mtl","o garden_tree"]
    lines += ["v "+" ".join(f"{q:.6f}" for q in p) for p in vertices]
    lines += ["vt "+" ".join(f"{q:.6f}" for q in p) for p in uv]
    lines += ["vn "+" ".join(f"{q:.6f}" for q in p) for p in normals]
    lines += ["usemtl Meadow"]
    lines += ["f "+" ".join(f"{i}/{i}/{i}" for i in f) for f in faces]
    (OUT/(output_name+".obj")).write_text("\n".join(lines)+"\n",encoding="utf-8")
    return len(faces)


def rebuild_grass(scene):
    source=ROOT/"Resources/3DModel/Stages/soft_grass/soft_grass.obj"
    v,t,n,f=[],[],[],[]
    for line in source.read_text(encoding="utf-8").splitlines():
        p=line.split()
        if not p: continue
        if p[0]=="v": v.append(tuple(map(float,p[1:])))
        elif p[0]=="vt": t.append(tuple(map(float,p[1:])))
        elif p[0]=="vn": n.append(tuple(map(float,p[1:])))
        elif p[0]=="f": f.append([int(q.split('/')[0]) for q in p[1:]])
    total=0
    for index,obj in enumerate(scene["objects"]):
        if not obj.get("name","").startswith("Stage1_EntryMeadow_Grass_"): continue
        rng=random.Random(9024+index)
        cx,_,cz=obj["position"]
        centers=[(rng.uniform(-3,3),rng.uniform(-5,5)) for _ in range(3)]
        positions=[]
        for j in range(65):
            if j<48:
                center=centers[j%3]
                x,z=rng.gauss(center[0],.95),rng.gauss(center[1],1.25)
            else: x,z=rng.uniform(-4.8,4.8),rng.uniform(-7.6,7.6)
            if not(-4.8<x<4.8 and -7.6<z<7.6): continue
            if cx+x < -347 or abs(cz+z+50)<5.6: continue
            if any((x-a)**2+(z-b)**2<.3**2 for a,b in positions): continue
            positions.append((x,z))
        vv,tt,nn,ff=[],[],[],[]
        for x,z in positions:
            size=rng.uniform(.52,.85); angle=rng.uniform(0,math.tau)
            co,si=math.cos(angle),math.sin(angle); offset=len(vv)
            # 株の根元と乱数列を保ち、道際は短く、奥の群生は高くする。
            verge=min(1.0,max(0.0,(abs(cz+z+50)-5.6)/4.0))
            width=1.18
            height=1.30+0.55*verge
            vv.extend((x+(px*co-pz*si)*size*width,py*size*height,-z+(px*si+pz*co)*size*width) for px,py,pz in v)
            tt.extend(t)
            for nx,ny,nz in n:
                normal=((nx*co-nz*si)/width,ny/height,(nx*si+nz*co)/width)
                length=math.sqrt(sum(q*q for q in normal))
                nn.append(tuple(q/max(length,1e-8) for q in normal))
            ff.extend([[i+offset for i in face] for face in f])
        lines=["mtllib meadow.mtl","o clustered_grass"]
        for tag,items in (("v",vv),("vt",tt),("vn",nn)):
            lines.extend(tag+" "+" ".join(f"{q:.6f}" for q in p) for p in items)
        lines.append("usemtl Meadow")
        lines.extend("f "+" ".join(f"{i}/{i}/{i}" for i in face) for face in ff)
        (ROOT/"Resources/3DModel"/obj["modelName"]).write_text("\n".join(lines)+"\n",encoding="utf-8")
        total+=len(positions)
    print(f"Clustered grass: {total} clumps")


def main():
    original = ROOT/"Resources/3DModel/Stages/star_garden_course_v4"
    verts, lines, mat, removed = [], [], "", 0
    for line in (original/"star_garden_course_v4.obj").read_text(encoding="utf-8").splitlines():
        p=line.split()
        if p and p[0]=="mtllib":
            line="mtllib course_meadow.mtl"
        elif p and p[0]=="v":
            point=tuple(map(float,p[1:]))
            verts.append(point)
            # Clip the adjoining decorative slab at the entrance boundary.
            x,y,z=point
            if abs(x+302)<.001 and -0.001<=y<=.141 and 36.99<=z<=63.01:
                line=f"v -300.000000 {y:.6f} {z:.6f}"
        elif p and p[0]=="usemtl":
            mat=p[1]
        elif p and p[0]=="f":
            points=[verts[int(q.split("/")[0])-1] for q in p[1:]]
            # Remove the entrance road slab including its coplanar bottom and sides.
            if ("grass" in mat and all(-360.01<=x<=-299.99 and 27.99<=z<=72.01 and abs(y)<0.001 for x,y,z in points)) or (
                mat in ("ruin_path", "ruin_stone") and all(-360.01<=x<=-299.99 and 45.59<=z<=54.41 and -0.001<=y<0.3 for x,y,z in points)):
                removed+=1
                continue
        lines.append(line)
    assert removed>0, "重複草地面が見つかりません"
    (OUT/"course_meadow.obj").write_text("\n".join(lines)+"\n",encoding="utf-8")
    (OUT/"course_meadow.mtl").write_bytes((original/"star_garden_course_v4.mtl").read_bytes())
    triangles=build_tree()
    # One continuous surface replaces the old turf; no near-coplanar overlay.
    (OUT/"ground.obj").write_text(
        "mtllib meadow.mtl\no ground\nv -30 0 22\nv 30 0 22\nv 30 0 -22\nv -30 0 -22\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 1 0\nusemtl Meadow\nf 3/3/1 2/2/1 1/1/1\nf 4/4/1 3/3/1 1/1/1\n",encoding="utf-8")
    scene=json.loads(SCENE.read_text(encoding="utf-8"))
    before=copy.deepcopy(scene)
    rebuild_grass(scene)
    changed=[]
    for obj in scene["objects"]:
        if obj.get("name")=="Stage1_Course_V4":
            obj["modelName"]="Stages/entry_meadow/course_meadow.obj"
            changed.append(obj["name"])
        elif obj.get("name")=="Stage1_EntryMeadow_Ground":
            obj["position"][1]=0.0
            obj["materialType"]=32
            obj["envIntensity"]=4.4
            changed.append(obj["name"])
        elif obj.get("name","").startswith("Stage1_EntryMeadow_Grass_"):
            obj["scale"][1]=1.4
            changed.append(obj["name"])
        elif obj.get("modelName")=="Stages/soft_tree":
            obj["modelName"]="Stages/entry_meadow/garden_tree.obj"
            obj["materialType"]=31
            obj["color"]=[1,1,1,1]
            obj["roughness"]=0.92
            obj["metallic"]=0
            obj["envIntensity"]=0.8
            obj["enableEnvMap"]=False
            changed.append(obj["name"])
    for old,new in zip(before["objects"],scene["objects"]):
        if old.get("name") not in changed: assert old==new
        for field in ("position","rotation","scale","collider","collisionMask","collisionAttribute"):
            if old.get("name")=="Stage1_EntryMeadow_Ground" and field=="position": continue
            if old.get("name","").startswith("Stage1_EntryMeadow_Grass_") and field=="scale": continue
            assert old.get(field)==new.get(field)
    SCENE.write_text(json.dumps(scene,ensure_ascii=False,indent=4)+"\n",encoding="utf-8")
    print(f"Removed {removed} overlapping grass faces; tree {triangles} triangles; updated {changed}")


if __name__=="__main__":
    main()
