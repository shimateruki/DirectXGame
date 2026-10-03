"""コピーしたプレイヤー専用の小さな立体装飾。元の敵モデルは変更しない。"""
from __future__ import annotations
import json
import math
import struct
from pathlib import Path
from generate_ring_burner_model import ObjBuilder, cross, dot, sub, write_meta

ROOT = Path(__file__).resolve().parents[2]
MATERIALS = {
    "Cloth": (0.82, 0.055, 0.12), "Trim": (1.0, 0.88, 0.64),
    "Gold": (1.0, 0.60, 0.055), "Ember": (1.0, 0.17, 0.025),
    "Navy": (0.12, 0.10, 0.26), "Mint": (0.12, 0.66, 0.42),
    "Ivory": (0.91, 1.0, 0.83), "Metal": (0.19, 0.21, 0.30),
}


def body_points(name):
    folder = ROOT / "Resources/3DModel/Characters" / name
    data = json.loads((folder / (name + ".gltf")).read_text(encoding="utf-8"))
    primitive = data["meshes"][0]["primitives"][0]
    accessor = data["accessors"][primitive["attributes"]["POSITION"]]
    view = data["bufferViews"][accessor["bufferView"]]
    blob = (folder / data["buffers"][view["buffer"]]["uri"]).read_bytes()
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", 12)
    return [struct.unpack_from("<fff", blob, offset + n * stride) for n in range(accessor["count"])]


def band(model, points, material):
    # 頭部断面に沿う薄い布。上端を絞り、球を貫通する単純なトーラスにしない。
    def radius(y):
        section = [math.hypot(x, z) for x, py, z in points if abs(py - y) < .035]
        return max(section, default=.34) + .014
    bottom, top = .51, .615
    for i in range(64):
        a, b = math.tau * i / 64, math.tau * (i + 1) / 64
        def p(angle, y, inset=0):
            r = radius(y) + inset
            return (math.sin(angle)*r, y, math.cos(angle)*r)
        n = (math.sin((a+b)/2), 0, math.cos((a+b)/2))
        model.quad(material, (p(a,bottom),p(b,bottom),p(b,top),p(a,top)), n)
        for y in (bottom, top):
            model.quad("Trim", (p(a,y),p(b,y),p(b,y,-.018),p(a,y,-.018)), (0,1 if y==top else -1,0))
    return radius(.56)


def ribbon(model, side, radius):
    centers = [(side*.12,.57,-radius), (side*.27,.52,-radius-.13),
               (side*.43,.40,-radius-.19), (side*.51,.32,-radius-.16)]
    for a,b in zip(centers, centers[1:]):
        for depth in (-.012,.012):
            model.quad("Cloth", ((a[0]-.055,a[1],a[2]+depth),(b[0]-.05,b[1],b[2]+depth),
                                 (b[0]+.05,b[1],b[2]+depth),(a[0]+.055,a[1],a[2]+depth)), (0,0,1 if depth>0 else -1))


def crest(model, material, polygon, z, thickness=.035):
    # 正面・裏面・側面を備えた小さなワッペン形状。
    # 炎・稲妻の凹形状を扇形分割すると面が重なるので、耳切りで分割します。
    area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(polygon,polygon[1:]+polygon[:1]))
    if area<0: polygon=list(reversed(polygon))
    def turn(a,b,c):
        return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    remaining=list(range(len(polygon)))
    triangles=[]
    while len(remaining)>3:
        for n,i in enumerate(remaining):
            prev,nxt=remaining[n-1],remaining[(n+1)%len(remaining)]
            a,b,c=polygon[prev],polygon[i],polygon[nxt]
            if turn(a,b,c)<=1e-8: continue
            inside=any(turn(a,b,polygon[j])>=-1e-8 and turn(b,c,polygon[j])>=-1e-8
                       and turn(c,a,polygon[j])>=-1e-8 for j in remaining if j not in (prev,i,nxt))
            if inside: continue
            triangles.append((prev,i,nxt))
            remaining.pop(n)
            break
        else: raise ValueError("Invalid crest polygon")
    triangles.append(tuple(remaining))
    for dz, sign in ((-thickness,-1),(thickness,1)):
        for triangle in triangles:
            pts = [polygon[j] for j in triangle]
            indices = [model.vertex((x,y,z+dz),(0,0),(0,0,sign)) for x,y in pts]
            model.face(material,*indices,False)
    for i,a in enumerate(polygon):
        b=polygon[(i+1)%len(polygon)]
        model.quad(material, ((a[0],a[1],z-thickness),(b[0],b[1],z-thickness),
                              (b[0],b[1],z+thickness),(a[0],a[1],z+thickness)), (b[1]-a[1],a[0]-b[0],0))


def save(name, model):
    folder=ROOT/"Resources/3DModel/Characters"/name
    folder.mkdir(parents=True,exist_ok=True)
    obj=folder/(name+".obj")
    mtl=folder/(name+".mtl")
    lines=["# Player copy adornment",f"mtllib {mtl.name}",f"o {name}"]
    for prefix,values in (("v",model.vertices),("vt",model.uvs),("vn",model.normals)):
        lines += [prefix+" "+" ".join(f"{v:.7f}" for v in row) for row in values]
    for material,faces in model.faces.items():
        lines += [f"usemtl {material}","s 1"]
        for face in faces:
            p=[model.vertices[i-1] for i in face]
            normal=tuple(sum(model.normals[i-1][axis] for i in face) for axis in range(3))
            if dot(cross(sub(p[1],p[0]),sub(p[2],p[0])),normal)<0: face=tuple(reversed(face))
            lines.append("f "+" ".join(f"{i}/{i}/{i}" for i in face))
    obj.write_text("\n".join(lines)+"\n",encoding="utf-8")
    mtl.write_text("\n".join(f"newmtl {key}\nKd {r} {g} {b}\nKa 0.15 0.15 0.15\nKs 0.15 0.15 0.15\nNs 24\nd 1\n"
                               for key,(r,g,b) in MATERIALS.items())+"\n",encoding="utf-8")
    write_meta(obj,"Model","ModelImporter",{"generateTangents":True,"scale":1.0})
    write_meta(mtl,"Material","MaterialImporter",{})
    assert all(math.isfinite(v) for p in model.vertices for v in p)
    assert max(abs(p[0]) for p in model.vertices)<.8
    print(name, len(model.vertices), "vertices", sum(map(len,model.faces.values())), "triangles")


def main():
    points=body_points("slime_pink")
    for kind,material in (("pink","Cloth"),("fire","Navy"),("thunder","Navy"),("wind","Mint"),("bomb","Metal")):
        m=ObjBuilder()
        r=band(m,points,material)
        if kind=="pink":
            m.sphere("Cloth",(0,.565,-r-.025),(.12,.075,.065),16,8)
            ribbon(m,-1,r); ribbon(m,1,r)
            crest(m,"Trim",[(-.045,.54),(.045,.54),(.045,.59),(-.045,.59)],r+.01,.008)
        elif kind=="fire":
            crest(m,"Gold",[(-.11,.56),(.11,.56),(.16,.73),(.075,.695),(.025,.94),(-.07,.78),(-.095,.84)],r-.03)
            crest(m,"Ember",[(-.065,.585),(.065,.585),(.03,.79),(-.02,.72)],r+.025,.008)
        elif kind=="thunder":
            for side in (-1,1):
                crest(m,"Gold",[(side*.25,.58),(side*.39,.58),(side*.32,.73),(side*.44,.78),
                                (side*.25,.96),(side*.28,.78),(side*.18,.74)],.02)
        elif kind=="wind":
            for side in (-1,1):
                crest(m,"Ivory",[(side*.25,.57),(side*.37,.58),(side*.58,.89),(side*.40,.83),
                                 (side*.49,1.00),(side*.30,.88)],-.04,.023)
        else:
            m.cylinder("Gold",(0,.74,0),.14,.08,24)
            # 短い導火線と先端の熾火で、敵の黒い本体と差を付ける。
            for i in range(7):
                m.sphere("Trim",(.015*i,.79+.022*i,0),(.03,.031,.03),12,6)
            m.sphere("Ember",(.11,.95,0),(.046,.046,.046),12,6)
        save("copy_band_"+kind,m)


if __name__=="__main__": main()
