"""元の王冠から中央装飾を分離し、継承ムービー専用のモデルを生成する。

通常ボスのOBJは変更しない。中央装飾の全三角形が元の座標へ復元できることを検証する。
"""
from pathlib import Path
import hashlib, json, math
from generate_stage3_high_crown import ObjBuilder, add_vertical_crystal, write_obj

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Resources/3DModel/Characters/false_king_slime'
PIVOT=(0.0,3.44,1.76)

def main():
    source=OUT/'false_king_slime.obj'
    lines=source.read_text(encoding='utf-8').splitlines()
    vertices=[tuple(map(float,l.split()[1:])) for l in lines if l.startswith('v ')]
    uv=[tuple(map(float,l.split()[1:])) for l in lines if l.startswith('vt ')]
    normals=[tuple(map(float,l.split()[1:])) for l in lines if l.startswith('vn ')]
    expected=ObjBuilder()
    add_vertical_crystal(expected,'CrownGold',(0,3.44,1.76),.52,1.10,4)
    add_vertical_crystal(expected,'CoreGlow',(0,3.46,2.00),.34,.76,4)
    # 位置を量子化して照合し、近くの金縁や身体を誤って欠片へ含めない。
    key=lambda p:tuple(round(x,6) for x in p)
    positions={m:{key(expected.vertices[i-1]) for f in fs for i in f} for m,fs in expected.faces.items()}
    ornament=ObjBuilder();kept=[];material='';removed=0
    for line in lines:
        if line.startswith('usemtl '):material=line.split()[1]
        if line.startswith('f '):
            refs=[tuple(map(int,r.split('/'))) for r in line.split()[1:]]
            take=material in positions and all(key(vertices[r[0]-1]) in positions[material] for r in refs)
            if take:
                indices=[]
                for v,t,n in refs:
                    p=vertices[v-1]
                    indices.append(ornament.vertex(tuple(p[k]-PIVOT[k] for k in range(3)),uv[t-1],normals[n-1]))
                ornament.face(material,*indices,False);removed+=1;continue
        kept.append(line)
    assert removed==sum(len(v) for v in expected.faces.values())==16,removed
    # 取り除いた三角形を元のピボットへ戻したときの形状を検証する。
    actual={key(tuple(p[k]+PIVOT[k] for k in range(3))) for p in ornament.vertices}
    assert actual=={key(p) for p in expected.vertices}
    damaged=OUT/'false_king_slime_without_emblem.obj'
    damaged.write_text('\n'.join(kept)+'\n',encoding='utf-8')
    write_obj(ornament,OUT/'false_king_crown_emblem.obj','false_king_slime.mtl','CrownEmblem')
    crack=ObjBuilder()
    path=[(-.06,.37,.34),(.045,.20,.45),(-.05,.07,.52),(.05,-.08,.49),(-.015,-.25,.36)]
    for a,b in zip(path,path[1:]):
        crack.quad('CoreGlow',((a[0]-.016,a[1],a[2]),(a[0]+.016,a[1],a[2]),(b[0]+.016,b[1],b[2]),(b[0]-.016,b[1],b[2])),(0,0,1))
    write_obj(crack,OUT/'false_king_crown_emblem_crack.obj','false_king_slime.mtl','CrownEmblemCrack')
    chip=ObjBuilder()
    points=[(-.07,0,-.045),(.09,0,-.04),(.015,.18,.015),(.005,-.06,.06)]
    for ids in [(0,1,2),(0,3,1),(0,2,3),(1,3,2)]:
        ps=tuple(points[i] for i in ids);a,b,c=ps
        u=tuple(b[k]-a[k] for k in range(3));v=tuple(c[k]-a[k] for k in range(3))
        normal=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        chip.face('CrownGold',*[chip.vertex(p,(0,0),normal) for p in ps],False)
    write_obj(chip,OUT/'false_king_crown_emblem_chip.obj','false_king_slime.mtl','CrownEmblemChip')
    pressure=ObjBuilder();pressure.torus('SlimeHighlight',(0,0,0),1.0,.022,128,8)
    write_obj(pressure,OUT/'false_king_pressure_front.obj','false_king_slime.mtl','PressureFront')
    names=['false_king_slime_without_emblem','false_king_crown_emblem','false_king_crown_emblem_crack','false_king_crown_emblem_chip','false_king_pressure_front']
    for name in names:
        p=OUT/(name+'.obj');meta=p.with_suffix('.obj.meta')
        if not meta.exists():
            relative=p.relative_to(ROOT).as_posix()
            meta.write_text(json.dumps({'assetType':'Model','guid':hashlib.md5(('cg2:'+relative).encode()).hexdigest(),'importSettings':{},'importer':'ModelImporter','source':relative,'version':1},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Central emblem split verified:',removed,'triangles. Original model preserved.')

if __name__=='__main__':main()
