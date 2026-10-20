"""浮き岩と円柱岩へUV・岩肌のPBRマップを焼き込み、実配布モデルを確認する。"""
import json
import math
import sys
import uuid
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector, noise

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_floating_rocks as base

ROOT = base.ROOT
OUT = ROOT / 'output/floating-rocks-pbr'
SIZE = 2048


def procedural_material():
    # 色と凹凸は同じ3D座標を使い、UV境界でも岩肌の模様が途切れないようにする。
    mat = bpy.data.materials.new('BasaltBakingSource')
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Metallic'].default_value = 0
    tex = nodes.new('ShaderNodeTexCoord')

    def noise_node(scale, detail):
        # スケール違いのノイズを分離し、大きな色むらと細粒を独立して調整する。
        n = nodes.new('ShaderNodeTexNoise')
        n.inputs['Scale'].default_value = scale
        n.inputs['Detail'].default_value = detail
        n.inputs['Roughness'].default_value = .72
        links.new(tex.outputs['Object'], n.inputs['Vector'])
        return n

    broad = noise_node(2.8, 5)
    grain = noise_node(65, 3)
    pores = noise_node(14, 3)
    color = nodes.new('ShaderNodeValToRGB')
    color.color_ramp.elements.remove(color.color_ramp.elements[1])
    stops = [(.18,(.055,.061,.066,1)),(.39,(.12,.13,.135,1)),
             (.57,(.23,.225,.21,1)),(.72,(.33,.31,.27,1)),(.85,(.38,.36,.32,1))]
    for i,(pos,c) in enumerate(stops):
        el = color.color_ramp.elements[0] if i == 0 else color.color_ramp.elements.new(pos)
        el.position, el.color = pos,c
    links.new(broad.outputs['Fac'], color.inputs[0])
    links.new(color.outputs['Color'], bsdf.inputs['Base Color'])
    rough = nodes.new('ShaderNodeMapRange')
    rough.inputs['From Min'].default_value=.2
    rough.inputs['From Max'].default_value=.8
    rough.inputs['To Min'].default_value=.63
    rough.inputs['To Max'].default_value=.94
    links.new(grain.outputs['Fac'],rough.inputs['Value'])
    links.new(rough.outputs['Result'],bsdf.inputs['Roughness'])
    coarse = nodes.new('ShaderNodeBump')
    coarse.inputs['Strength'].default_value=.65
    coarse.inputs['Distance'].default_value=.13
    links.new(pores.outputs['Fac'],coarse.inputs['Height'])
    fine = nodes.new('ShaderNodeBump')
    fine.inputs['Strength'].default_value=.38
    fine.inputs['Distance'].default_value=.025
    links.new(grain.outputs['Fac'],fine.inputs['Height'])
    links.new(coarse.outputs['Normal'],fine.inputs['Normal'])
    links.new(fine.outputs['Normal'],bsdf.inputs['Normal'])
    return mat


def refine_geometry(obj, is_roller):
    # シルエットを面取りし、細かな凹凸はテクスチャへ分担してポリゴン数を抑える。
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active=obj
    if is_roller:
        sub=obj.modifiers.new('Rounded stone', 'SUBSURF')
        sub.levels=2
        bpy.ops.object.modifier_apply(modifier=sub.name)
    else:
        bevel=obj.modifiers.new('Weathered edges','BEVEL')
        bevel.width=.12
        bevel.segments=3
        bpy.ops.object.modifier_apply(modifier=bevel.name)
    for vertex in obj.data.vertices:
        p=vertex.co
        n=noise.noise_vector(p*2.1,noise_basis='PERLIN_ORIGINAL')
        if is_roller:
            p.y+=n.y*.035
            p.z+=n.z*.035
        elif p.z < -.08:
            p.x+=n.x*.055
            p.y+=n.y*.055
    obj.data.update()
    for poly in obj.data.polygons:
        poly.use_smooth=True
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(65),island_margin=.025)
    bpy.ops.object.mode_set(mode='OBJECT')


def bake(obj, source, dest, kind, suffix):
    # 照明をベースカラーへ焼かず、実行時の照明で陰影が変わる素材として保存する。
    image=bpy.data.images.new(obj.name+'_'+suffix,width=SIZE,height=SIZE,alpha=False)
    image.colorspace_settings.name='sRGB' if suffix=='basecolor' else 'Non-Color'
    nodes=source.node_tree.nodes
    node=nodes.new('ShaderNodeTexImage')
    node.image=image
    nodes.active=node
    scene=bpy.context.scene
    scene.render.bake.use_pass_direct=False
    scene.render.bake.use_pass_indirect=False
    scene.render.bake.use_pass_color=True
    scene.render.bake.margin=16
    bpy.ops.object.bake(type=kind)
    if suffix in ('basecolor','normal'):
        image.filepath_raw=str(dest/(suffix+'.png'))
        image.file_format='PNG'
        image.save()
    nodes.remove(node)
    return image


def packed_material(obj, images, dest):
    # エンジン仕様のR=AO/G=Roughness/B=Metallicに合わせて詰める。岩なので金属度は0。
    pixels=[]
    for image in images[2:]:
        values=np.empty(SIZE*SIZE*4,dtype=np.float32)
        image.pixels.foreach_get(values)
        pixels.append(values.reshape(-1,4))
    values=np.ones((SIZE*SIZE,4),dtype=np.float32)
    values[:,0]=pixels[1][:,0]
    values[:,1]=pixels[0][:,0]
    values[:,2]=0
    orm=bpy.data.images.new(obj.name+'_orm',width=SIZE,height=SIZE,alpha=False)
    orm.colorspace_settings.name='Non-Color'
    orm.pixels.foreach_set(values.ravel())
    orm.filepath_raw=str(dest/'orm.png')
    orm.file_format='PNG'
    orm.save()
    mat=bpy.data.materials.new(obj.name+'_PBR')
    mat.use_nodes=True
    mat.use_backface_culling=True
    nodes,links=mat.node_tree.nodes,mat.node_tree.links
    bsdf=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
    for image,slot in [(images[0],'Base Color'),(images[1],'Normal'),(orm,'ORM')]:
        node=nodes.new('ShaderNodeTexImage')
        node.image=image
        if slot=='Normal':
            normal=nodes.new('ShaderNodeNormalMap')
            links.new(node.outputs['Color'],normal.inputs['Color'])
            links.new(normal.outputs['Normal'],bsdf.inputs['Normal'])
        elif slot=='ORM':
            split=nodes.new('ShaderNodeSeparateColor')
            links.new(node.outputs['Color'],split.inputs['Color'])
            links.new(split.outputs['Green'],bsdf.inputs['Roughness'])
            links.new(split.outputs['Blue'],bsdf.inputs['Metallic'])
        else:
            links.new(node.outputs['Color'],bsdf.inputs[slot])
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    return mat


def build_one(name, obj, source):
    # OBJの既存パスを維持しつつ、PBR参照が明確なglTFも別名で併設する。
    dest=ROOT/'Resources/3DModel/Gimmicks'/name
    refine_geometry(obj,name=='basalt_roller')
    obj.name=name
    obj.data.materials.clear()
    obj.data.materials.append(source)
    for face in obj.data.polygons:
        face.material_index=0
    images=[bake(obj,source,dest,kind,suffix) for kind,suffix in
            [('DIFFUSE','basecolor'),('NORMAL','normal'),('ROUGHNESS','roughness'),('AO','ao')]]
    packed_material(obj,images,dest)
    # n角形のままだとglTF接線を書けないため、ベイクと同じ面分割を確定する。
    triangulate=obj.modifiers.new('Export triangles','TRIANGULATE')
    bpy.ops.object.modifier_apply(modifier=triangulate.name)
    bpy.ops.wm.obj_export(filepath=str(dest/(name+'.obj')),export_selected_objects=True,
        forward_axis='NEGATIVE_Z',up_axis='Y',export_materials=True,export_pbr_extensions=True,
        export_triangulated_mesh=True,path_mode='STRIP')
    # AssimpのMTL読込でも法線・ORMを拾える明示的な参照へ統一する。
    mtl=dest/(name+'.mtl')
    mtl.write_text(f'newmtl {obj.data.materials[0].name}\nKd 1 1 1\nKs 0.04 0.04 0.04\n'
        'Ns 20\nPr 1\nPm 0\nillum 2\nmap_Kd basecolor.png\nnorm normal.png\n'
        'map_Bump normal.png\nmap_Pr orm.png\n',encoding='utf-8')
    path=dest/(name+'_pbr.gltf')
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLTF_SEPARATE',
        use_selection=True,export_tangents=True,export_image_format='AUTO')
    data=json.loads(path.read_text(encoding='utf-8'))
    for mat in data['materials']:
        # 同じORM画像のRをAOとして使い、複製画像を増やさない。
        tex=mat['pbrMetallicRoughness']['metallicRoughnessTexture']
        mat['occlusionTexture']=dict(index=tex['index'],strength=1)
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    meta=path.with_suffix('.gltf.meta')
    if not meta.exists():
        meta.write_text(json.dumps(dict(assetType='Model',guid=uuid.uuid5(uuid.NAMESPACE_URL,str(path.relative_to(ROOT))).hex,
            importer='ModelImporter',importSettings=dict(generateTangents=True,scale=1),
            source=path.relative_to(ROOT).as_posix(),version=1),indent=2)+'\n',encoding='utf-8')
    obj.data.calc_loop_triangles()
    print('PBR_READY',name,'triangles',len(obj.data.loop_triangles),flush=True)
    # 書き出したglTFを再読込してプレビューする。
    bpy.data.objects.remove(obj,do_unlink=True)
    bpy.ops.import_scene.gltf(filepath=str(path))
    return [o for o in bpy.context.selected_objects if o.type=='MESH']


def main():
    # モデルだけを変更し、ステージ設定やゲームコードには触れない。
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    OUT.mkdir(parents=True,exist_ok=True)
    scene=bpy.context.scene
    scene.render.engine='CYCLES'
    scene.cycles.samples=16
    source=procedural_material()
    for name,is_roller in [('floating_basalt_rock',False),('basalt_roller',True)]:
        for obj in list(bpy.data.objects):
            bpy.data.objects.remove(obj,do_unlink=True)
        obj=base.roller([source]*4) if is_roller else base.floating_rock([source]*4)
        build_one(name,obj,source)
        bpy.ops.object.camera_add()
        scene.camera=bpy.context.object
        target=Vector((0,0,0 if is_roller else -.6))
        scene.camera.location=target+Vector((10,-14,10))
        scene.camera.rotation_euler=(target-scene.camera.location).to_track_quat('-Z','Y').to_euler()
        scene.camera.data.type='ORTHO'
        scene.camera.data.ortho_scale=10.5 if is_roller else 7.5
        for pos,power,size in [((-3,-6,9),1400,5),((5,3,7),1700,4)]:
            bpy.ops.object.light_add(type='AREA',location=pos)
            light=bpy.context.object
            light.data.energy=power
            light.data.size=size
            light.rotation_euler=(-light.location).to_track_quat('-Z','Y').to_euler()
        scene.world.color=(.25,.25,.25)
        scene.render.film_transparent=True
        scene.render.resolution_x=1400
        scene.render.resolution_y=1000
        scene.render.resolution_percentage=100
        scene.render.image_settings.color_mode='RGBA'
        scene.cycles.samples=32
        scene.view_settings.view_transform='AgX'
        scene.render.filepath=str(OUT/(name+'.png'))
        bpy.ops.render.render(write_still=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(name+'.blend')))


if __name__=='__main__':
    main()
