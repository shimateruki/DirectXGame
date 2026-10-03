"""実スライムの顔とメッシュを維持した覇王スライムを生成する。"""
from __future__ import annotations
import json
import math
import shutil
from pathlib import Path
from generate_ring_burner_model import ObjBuilder, cross, dot, sub
from generate_stage3_high_crown import add_crown_plate, add_vertical_crystal
from add_wind_slime_decorations import TriangleMesh, append_mesh_primitive

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Resources/3DModel/Characters/slime_white'
OUTPUT = ROOT / 'Resources/3DModel/Characters/slime_crown'


def build_regalia():
    model = ObjBuilder()
    # 頭に接する小さな王冠。巨大なボス王冠の縮小ではなく、主人公の丸さに合わせる。
    model.torus('CrownGold', (0, .715, 0), .245, .036, 40, 8)
    model.torus('CrownGold', (0, .775, 0), .258, .019, 40, 8)
    for index in range(5):
        angle = math.tau * index / 5 + math.pi
        x, z = math.sin(angle) * .245, math.cos(angle) * .245
        add_crown_plate(model, 'RoyalEnamel', (x, .733, z), angle, .22,
                        .26 if index == 0 else .19, .024)
        model.sphere('CrownGold', (x, .998 if index == 0 else .928, z), (.027, .027, .027), 12, 6)
    add_vertical_crystal(model, 'HeartGem', (0, .847, -.276), .065, .145, 6)
    # 肩飾りは小さく、正面の目と輪郭を隠さない。
    for side in (-1, 1):
        model.sphere('CrownGold', (side * .44, .40, .055), (.12, .055, .16), 20, 10)
        model.sphere('RoyalEnamel', (side * .46, .428, .04), (.077, .028, .095), 16, 8)
    # 背中だけを覆う短い立体マント。裾の扇状の広がりと布のひだを造形する。
    for index in range(24):
        def point(i, row, inset=0):
            a = math.radians(-68 + 136 * i / 24)
            r = (.40, .54, .60)[row] + .014 * math.cos(i * math.pi / 2) + inset
            return (math.sin(a) * r, (.48, .27, .085)[row], math.cos(a) * r)
        for row in range(2):
            a,b,c,d = point(index,row), point(index+1,row), point(index+1,row+1), point(index,row+1)
            model.quad('RoyalCape', (a,b,c,d), (math.sin(math.radians(-68+136*index/24)), .15, 1))
            model.quad('CapeLining', (point(index,row,-.012),point(index,row+1,-.012),
                                     point(index+1,row+1,-.012),point(index+1,row,-.012)), (0,0,-1))
        a,b = point(index,2), point(index+1,2)
        model.quad('CrownGold', (a,b,(b[0],b[1]+.025,b[2]),(a[0],a[1]+.025,a[2])), (0,0,1))
    return model


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    gltf = json.loads((SOURCE / 'slime_white.gltf').read_text(encoding='utf-8'))
    blob = bytearray((SOURCE / gltf['buffers'][0]['uri']).read_bytes())
    # 元の顔テクスチャをそのまま使い、透明度や目の形は変更しない。
    for image in gltf.get('images', []):
        shutil.copy2(SOURCE / image['uri'], OUTPUT / image['uri'])
    gltf['materials'][0]['pbrMetallicRoughness']['baseColorFactor'] = [.58,.89,1,1]
    gltf['materials'][0]['pbrMetallicRoughness']['roughnessFactor'] = .28
    materials = {
        'CrownGold': ([1,.68,.12,1], .25, .35),
        'RoyalEnamel': ([.17,.09,.36,1], .25, .28),
        'HeartGem': ([.10,.95,1,1], .15, .16),
        'RoyalCape': ([.20,.08,.43,1], 0, .7),
        'CapeLining': ([.88,.80,1,1], 0, .6),
    }
    regalia = build_regalia()
    primitives = []
    for name, faces in regalia.faces.items():
        color,metallic,roughness = materials[name]
        material = len(gltf['materials'])
        gltf['materials'].append({'name':name,'doubleSided':True,
            'pbrMetallicRoughness':{'baseColorFactor':color,'metallicFactor':metallic,'roughnessFactor':roughness}})
        mesh = TriangleMesh()
        for face in faces:
            # ビルダーごとの面順序を法線へ揃え、裏面扱いの金属が黒くなるのを防ぐ。
            points = [regalia.vertices[v-1] for v in face]
            normal = tuple(sum(regalia.normals[v-1][axis] for v in face) for axis in range(3))
            if dot(cross(sub(points[1],points[0]),sub(points[2],points[0])),normal) < 0:
                face = tuple(reversed(face))
            # 元のスライムの正面は+Z。背中用のマントは-Zへ置く。
            for vertex in reversed(face):
                x,y,z = regalia.vertices[vertex-1]
                nx,ny,nz = regalia.normals[vertex-1]
                mesh.positions.append((x,y,-z))
                mesh.normals.append((nx,ny,-nz))
                mesh.uvs.append(regalia.uvs[vertex-1])
        primitives.append(append_mesh_primitive(gltf, blob, mesh, material))
    mesh_index = len(gltf['meshes'])
    gltf['meshes'].append({'name':'CrownSlimeRegalia','primitives':primitives})
    node = len(gltf['nodes'])
    gltf['nodes'].append({'name':'CrownSlimeRegalia','mesh':mesh_index})
    gltf['scenes'][gltf.get('scene',0)]['nodes'].append(node)
    gltf['asset']['generator'] = 'CG2 Crown Slime - original slime mesh with royal regalia'
    gltf['buffers'][0] = {'uri':'slime_crown.bin','byteLength':len(blob)}
    (OUTPUT / 'slime_crown.bin').write_bytes(blob)
    (OUTPUT / 'slime_crown.gltf').write_text(json.dumps(gltf, ensure_ascii=False, indent=2)+'\n',encoding='utf-8')
    assert max(p[1] for p in regalia.vertices) < 1.10
    assert max(abs(p[0]) for p in regalia.vertices) < .72
    print('Crown slime:',len(regalia.vertices),'regalia vertices; original body and face retained')


if __name__ == '__main__':
    main()
