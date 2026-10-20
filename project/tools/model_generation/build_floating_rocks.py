"""沈む浮き岩と回転用の円柱岩を生成。シーン・動作・プリセットは変更しない。"""
import math
import sys
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_volcanic_obstacles import material, export, ROOT

OUT = ROOT / 'output/floating-rocks'


def mesh_object(name, vertices, faces, mats, indices):
    # 閉じた立体にし、裏面や底面が欠けないよう法線を外側へそろえる。
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    for mat in mats:
        mesh.materials.append(mat)
    for face, index in zip(mesh.polygons, indices):
        face.material_index = index
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    assert all(edge.is_manifold for edge in bm.edges), name
    bm.to_mesh(mesh)
    bm.free()
    return obj


def floating_rock(mats):
    # 上面は平ら、側面と底は丸く絞り、崩れる角形ブロックと輪郭を区別する。
    n = 12
    radii = [1,.94,1.02,.95,1,.96,1.01,.93,.99,.97,1.02,.95]
    rings = [(-1.65,.55),(-1.4,.83),(-.87,1),(-.25,1.02),(0,.90)]
    vertices = [(math.cos(i*math.tau/n)*2.8*r*radii[i],
                 math.sin(i*math.tau/n)*2.3*r*radii[i],z)
                for z,r in rings for i in range(n)]
    faces = [tuple(reversed(range(n)))]
    indices = [3]
    for row in range(len(rings)-1):
        for i in range(n):
            j=(i+1)%n
            faces.append((row*n+i,row*n+j,(row+1)*n+j,(row+1)*n+i))
            indices.append(3 if row == 0 else (2 if row < 3 else 1))
    # 天面は細かい割れ目を描かず、淡い面差だけで岩肌を表す。
    vertices.append((.12,-.08,0))
    center=len(vertices)-1
    for i in range(n):
        faces.append((center,4*n+i,4*n+(i+1)%n))
        indices.append(0 if i%4 else 1)
    return mesh_object('FloatingBasalt',vertices,faces,mats,indices)


def roller(mats):
    # 回転軸をX、原点を軸中心に置く。真円断面を保ち、回転中の凸凹を抑える。
    n=24
    rings=[(-4,.82),(-3.82,1),(-2.5,1),(-1.1,1),(.4,1),(1.9,1),(3.82,1),(4,.82)]
    vertices=[(x,1.5*r*math.cos(i*math.tau/n),1.5*r*math.sin(i*math.tau/n))
              for x,r in rings for i in range(n)]
    faces=[tuple(reversed(range(n)))]
    indices=[1]
    for row in range(len(rings)-1):
        for i in range(n):
            j=(i+1)%n
            faces.append((row*n+i,row*n+j,(row+1)*n+j,(row+1)*n+i))
            # 軸方向の淡い鉱物帯で、回転していることを遠目からも読み取れるようにする。
            indices.append(1 if i in (2,3,12,13) else (2 if (i+row*3)%11==0 else 0))
    faces.append(tuple((len(rings)-1)*n+i for i in range(n)))
    indices.append(1)
    return mesh_object('BasaltRoller',vertices,faces,mats,indices)


def render_imported(name, scale, target):
    # OBJを読み戻して撮影し、書き出し後の向き・材質・面も確認する。
    for obj in list(bpy.data.objects):
        if obj.type=='MESH':
            bpy.data.objects.remove(obj,do_unlink=True)
    path=ROOT/'Resources/3DModel/Gimmicks'/name/(name+'.obj')
    bpy.ops.wm.obj_import(filepath=str(path),forward_axis='NEGATIVE_Z',up_axis='Y')
    scene=bpy.context.scene
    scene.camera.location=Vector(target)+Vector((10,-14,10))
    scene.camera.rotation_euler=(Vector(target)-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    scene.camera.data.ortho_scale=scale
    scene.render.filepath=str(OUT/(name+'.png'))
    bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(name+'.blend')))


def main():
    # データ生成だけに限定し、既存の沈む床やコライダーを変更しない。
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    mats=[material('BasaltTop',(.30,.32,.35)),material('MineralLight',(.40,.41,.43)),
          material('BasaltSide',(.19,.20,.23)),material('HeatedUnderside',(.32,.15,.075))]
    floating=floating_rock(mats)
    cylinder=roller(mats)
    bpy.context.view_layer.update()
    export('floating_basalt_rock',[floating])
    export('basalt_roller',[cylinder])
    OUT.mkdir(parents=True,exist_ok=True)
    scene=bpy.context.scene
    bpy.ops.object.camera_add()
    scene.camera=bpy.context.object
    scene.camera.data.type='ORTHO'
    for pos,power,size in [((-3,-6,10),1700,7),((5,3,7),1300,5)]:
        bpy.ops.object.light_add(type='AREA',location=pos)
        light=bpy.context.object
        light.data.energy=power
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
    render_imported('floating_basalt_rock',8,(0,0,-.6))
    render_imported('basalt_roller',11,(0,0,0))


if __name__=='__main__':
    main()
