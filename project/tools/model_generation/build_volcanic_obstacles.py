"""崩れる岩と棘バーのモデルだけを生成する。シーン・プリセットは変更しない。"""
import json
import math
import uuid
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
PREVIEW = ROOT / 'output/volcanic-obstacles'


def material(name, color, metallic=0, roughness=.8):
    # OBJ用の基本色とBlenderの確認用マテリアルを同じ値にそろえる。
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    node = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    node.inputs['Base Color'].default_value = (*color, 1)
    node.inputs['Metallic'].default_value = metallic
    node.inputs['Roughness'].default_value = roughness
    return mat


def bevel(obj, width):
    # 面取りを実メッシュに焼き込み、ゲーム側でも輪郭を保つ。
    bpy.context.view_layer.objects.active = obj
    mod = obj.modifiers.new('Edge chips', 'BEVEL')
    mod.width = width
    mod.segments = 1
    bpy.ops.object.modifier_apply(modifier=mod.name)


def prism(name, outline, top, bottom, mat):
    # 割れ目の両側を独立した閉じた破片にし、後の崩壊演出にも流用できる。
    count = len(outline)
    vertices = [(x, y, z) for z in (bottom, top) for x, y in outline]
    faces = [tuple(reversed(range(count))), tuple(range(count, count * 2))]
    faces += [(i, (i + 1) % count, (i + 1) % count + count, i + count)
              for i in range(count)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    bevel(obj, .09)
    return obj


def rod(name, a, b, radius, mat, tip=None, segments=12):
    # 両端指定で軸を決め、バー・帯金・棘のローカル方向を統一する。
    delta = Vector(b) - Vector(a)
    bpy.ops.mesh.primitive_cone_add(vertices=segments, radius1=radius,
                                   radius2=radius if tip is None else tip,
                                   depth=delta.length, location=(Vector(a)+Vector(b))*.5)
    obj = bpy.context.object
    obj.name = name
    obj.rotation_euler = delta.to_track_quat('Z', 'Y').to_euler()
    obj.data.materials.append(mat)
    if tip is None:
        bevel(obj, .035)
    return obj


def export(name, objects):
    # BlenderのZ上向きをOBJのY上向きへ回転。法線と頂点へ同じ変換を適用する。
    dest = ROOT / 'Resources/3DModel/Gimmicks' / name
    dest.mkdir(parents=True, exist_ok=True)
    lines = ['# Y-up model; independent named mesh parts', f'mtllib {name}.mtl']
    mats = {}
    offset = 0
    triangle_count = 0
    bounds = []
    for obj in objects:
        mesh = obj.data
        mesh.calc_loop_triangles()
        matrix = obj.matrix_world
        normal_matrix = matrix.to_3x3().inverted().transposed()
        lines.append('o ' + obj.name)
        for tri in mesh.loop_triangles:
            mat = mesh.materials[tri.material_index]
            mats[mat.name] = mat
            lines.append('usemtl ' + mat.name)
            normal = (normal_matrix @ tri.normal).normalized()
            for index in tri.vertices:
                point = matrix @ mesh.vertices[index].co
                coords = (point.x, point.z, -point.y)
                assert all(math.isfinite(v) for v in coords)
                bounds.append(coords)
                lines.append('v %.6f %.6f %.6f' % coords)
                lines.append('vn %.6f %.6f %.6f' % (normal.x, normal.z, -normal.y))
            lines.append('f ' + ' '.join(f'{offset+i}//{offset+i}' for i in (1, 2, 3)))
            offset += 3
            triangle_count += 1
    (dest / f'{name}.obj').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    mtl = []
    for key, mat in mats.items():
        node = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        rough = node.inputs['Roughness'].default_value
        metal = node.inputs['Metallic'].default_value
        mtl += [f'newmtl {key}', 'Kd %.5f %.5f %.5f' % tuple(mat.diffuse_color[:3]),
                'Ks 0.12 0.12 0.12', f'Ns {max(2, (1-rough)*100):.2f}',
                f'Pr {rough:.3f}', f'Pm {metal:.3f}', 'illum 2', '']
    (dest / f'{name}.mtl').write_text('\n'.join(mtl), encoding='utf-8')
    meta = dest / f'{name}.obj.meta'
    if not meta.exists():
        meta.write_text(json.dumps(dict(assetType='Model',
            guid=uuid.uuid5(uuid.NAMESPACE_URL, 'Gimmicks/'+name).hex,
            importSettings=dict(generateTangents=True, scale=1.0), importer='ModelImporter',
            source=(dest/f'{name}.obj').relative_to(ROOT).as_posix(), version=1), indent=2)+'\n', encoding='utf-8')
    lo = [min(p[i] for p in bounds) for i in range(3)]
    hi = [max(p[i] for p in bounds) for i in range(3)]
    print(name, 'triangles=', triangle_count, 'bounds=', lo, hi, flush=True)


def preview(name, objects, scale, target):
    # 書き出した形状と同じメッシュを透過レンダーし、表裏・割れ目・棘を確認する。
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            obj.hide_render = obj not in objects
    scene = bpy.context.scene
    scene.camera.location = Vector(target) + Vector((9, -13, 10))
    scene.camera.rotation_euler = (Vector(target)-scene.camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.camera.data.ortho_scale = scale
    scene.render.filepath = str(PREVIEW / f'{name}.png')
    bpy.ops.render.render(write_still=True)


def main():
    # 再生成の影響範囲をこの2モデル専用の出力先に限定する。
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    rock_mats = [material('WarmBasalt'+str(i), c) for i, c in enumerate(
        ((.36,.24,.16), (.43,.30,.20), (.39,.26,.17), (.47,.33,.23)))]
    dark = material('ForgedIron', (.12,.16,.19), .65, .42)
    bands = material('HeatBronze', (.40,.23,.09), .65, .4)
    spikes = material('SpikeIvoryMetal', (.76,.59,.30), .5, .35)
    # 外周はほぼ4m四方、天面0m。非対称の割れ目で人工的なX印を避ける。
    outlines = [
        [(-1.95,-1.78),(-1.75,-1.95),(.30,-1.95),(.12,-1.18),(.42,-.51),(-.08,-.12),(-1.95,-.44)],
        [(.39,-1.95),(1.75,-1.95),(1.95,-1.75),(1.95,.12),(.82,.31),(.03,-.05),(.51,-.47),(.22,-1.18)],
        [(.01,.05),(.83,.41),(1.95,.22),(1.95,1.75),(1.75,1.95),(-.35,1.95),(-.15,.98),(-.42,.57)],
        [(-1.95,-.34),(-.18,-.02),(-.53,.56),(-.26,.98),(-.45,1.95),(-1.75,1.95),(-1.95,1.75)],
    ]
    rock = [prism(f'RockFragment_{i+1:02}', outline, 0, -.95, rock_mats[i])
            for i, outline in enumerate(outlines)]
    intact = [prism('IntactRock', [(-1.95,-1.75),(-1.75,-1.95),(1.75,-1.95),
        (1.95,-1.75),(1.95,1.75),(1.75,1.95),(-1.75,1.95),(-1.95,1.75)],
        0, -.95, rock_mats[1])]
    bar = [rod('BarCore',(-4,0,0),(4,0,0),.34,dark,segments=16)]
    for x in (-3.8,-2,0,2,3.8):
        bar.append(rod(f'Collar_{x}',(x-.10,0,0),(x+.10,0,0),.40,bands,segments=16))
    for j, x in enumerate((-3.25,-2.25,-1.25,-.25,.75,1.75,2.75,3.4)):
        for k in range(4):
            angle = k*math.tau/4 + (j%2)*math.pi/4
            y,z = math.cos(angle),math.sin(angle)
            bar.append(rod(f'Spike_{j:02}_{k}',(x,y*.28,z*.28),(x,y*.91,z*.91),.19,spikes,tip=.015,segments=8))
    for sign in (-1,1):
        bar.append(rod('EndCap'+str(sign),(sign*3.95,0,0),(sign*4.32,0,0),.3,bands,tip=.05))
    bpy.context.view_layer.update()
    export('crumbling_rock_block', rock)
    export('crumbling_rock_block_intact', intact)
    export('volcanic_spike_bar', bar)
    PREVIEW.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    bpy.ops.object.camera_add()
    scene.camera = bpy.context.object
    scene.camera.data.type = 'ORTHO'
    for pos, power, size in (((-3,-6,10),1700,7),((5,2,7),1300,5)):
        bpy.ops.object.light_add(type='AREA', location=pos)
        light=bpy.context.object
        light.data.energy=power
        light.data.shape='DISK'
        light.data.size=size
        light.rotation_euler=(-light.location).to_track_quat('-Z','Y').to_euler()
    scene.world.color=(.3,.3,.3)
    scene.render.engine='CYCLES'
    scene.cycles.samples=32
    scene.render.film_transparent=True
    scene.render.resolution_x=1200
    scene.render.resolution_y=900
    scene.render.resolution_percentage=100
    scene.render.image_settings.color_mode='RGBA'
    scene.view_settings.view_transform='AgX'
    preview('crumbling_rock_block',rock,6,(0,0,-.4))
    preview('crumbling_rock_block_intact',intact,6,(0,0,-.4))
    preview('volcanic_spike_bar',bar,10,(0,0,0))
    for obj in rock+bar:
        obj.hide_render=False
    bpy.ops.wm.save_as_mainfile(filepath=str(PREVIEW/'volcanic_obstacles.blend'))


if __name__ == '__main__':
    main()
