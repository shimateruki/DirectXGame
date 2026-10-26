"""ゲート案内用の立体矢印を生成する。シーンやプリセットは変更しない。

Blender --background --python このファイル -- --preview-dir 出力先
エンジン読込後はY上向き・先端+Z、原点は底面の中央。寸法は幅2.4・長さ3.5・高さ0.62。
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import uuid
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
NAME = "tutorial_gate_arrow"
DEST = ROOT / "Resources" / "3DModel" / "Stages" / NAME
# ゲーム座標のXZ輪郭。幅広の先端と太い軸で、斜めからも矢印として読める形にする。
OUTLINE = [(0, 1.85), (-1.20, .25), (-.50, .25), (-.50, -1.65),
           (.50, -1.65), (.50, .25), (1.20, .25)]


def material(name, color, roughness, metallic):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    node = mat.node_tree.nodes.get("Principled BSDF")
    node.inputs["Base Color"].default_value = (*color, 1)
    node.inputs["Roughness"].default_value = roughness
    node.inputs["Metallic"].default_value = metallic
    return mat


def layer(name, scale, bottom, top, bevel, mat):
    # 輪郭・側面・底面を閉じ、面取りを実形状へ焼き込む。薄い板の両面表示には頼らない。
    n = len(OUTLINE)
    vertices = [(x * scale, -z * scale, h) for h in (bottom, top) for x, z in OUTLINE]
    faces = [tuple(reversed(range(n))), tuple(range(n, n * 2))]
    faces += [(i, (i + 1) % n, (i + 1) % n + n, i + n) for i in range(n)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    mesh.materials.append(mat)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    modifier = obj.modifiers.new("RoundedEdge", "BEVEL")
    modifier.width = bevel
    modifier.segments = 3
    modifier.harden_normals = True
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    # 大きな天面は平らに、面取りは連続したハイライトになるよう重み付き法線を保存する。
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    modifier = obj.modifiers.new("WeightedSurfaceNormals", "WEIGHTED_NORMAL")
    modifier.keep_sharp = True
    modifier.weight = 50
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.select_set(False)
    return obj


def validate_mesh(obj):
    # 生成時の形状検査。外向きの閉じた立体で、ゼロ面積の三角形がないことを確認する。
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    assert all(edge.is_manifold for edge in bm.edges), f"Open edge: {obj.name}"
    assert bm.calc_volume(signed=True) > 0, f"Inverted mesh: {obj.name}"
    bm.free()
    obj.data.calc_loop_triangles()
    assert all(tri.area > 1.e-9 for tri in obj.data.loop_triangles), obj.name


def export(objects):
    DEST.mkdir(parents=True, exist_ok=True)
    # Assimpが読込時にZを反転するため、OBJ側を逆にし、先端がゲーム内で+Zになるようにする。
    lines = ["# Y-up; tip becomes +Z after Assimp left-handed conversion; bottom pivot", f"mtllib {NAME}.mtl"]
    count = 0
    materials = {}
    bounds = []
    for obj in objects:
        mesh = obj.data
        mesh.calc_loop_triangles()
        uv_data = mesh.uv_layers.active.data
        mat = mesh.materials[0]
        materials[mat.name] = mat
        lines.extend([f"o {obj.name}", f"usemtl {mat.name}", "s 1"])
        for triangle in mesh.loop_triangles:
            for loop_id in reversed(triangle.loops):
                loop = mesh.loops[loop_id]
                p = mesh.vertices[loop.vertex_index].co
                normal = mesh.corner_normals[loop_id].vector
                uv = uv_data[loop_id].uv
                point = (p.x, p.z, p.y)
                assert all(math.isfinite(value) for value in point)
                bounds.append(point)
                lines.extend(["v %.7f %.7f %.7f" % point,
                              "vt %.7f %.7f" % tuple(uv),
                              "vn %.7f %.7f %.7f" % (normal.x, normal.z, normal.y)])
            lines.append("f " + " ".join(f"{count+i}/{count+i}/{count+i}" for i in (1, 2, 3)))
            count += 3
    (DEST / f"{NAME}.obj").write_text("\n".join(lines) + "\n", encoding="utf-8")
    mtl = ["# Material colors and PBR values are shared with the Blender preview."]
    for name, mat in materials.items():
        node = mat.node_tree.nodes.get("Principled BSDF")
        rough = node.inputs["Roughness"].default_value
        metal = node.inputs["Metallic"].default_value
        mtl += [f"newmtl {name}", "Kd %.6f %.6f %.6f" % tuple(mat.diffuse_color[:3]),
                "Ks 0.15 0.15 0.15", f"Ns {(1-rough)*128:.4f}",
                f"Pr {rough:.6f}", f"Pm {metal:.6f}", "d 1.0", "illum 2", ""]
    (DEST / f"{NAME}.mtl").write_text("\n".join(mtl), encoding="utf-8")
    for extension, kind, importer, settings in (("obj", "Model", "ModelImporter", {"generateTangents": True, "scale": 1.0}),
                                               ("mtl", "Binary", "BinaryImporter", {})):
        relative = (DEST / f"{NAME}.{extension}").relative_to(ROOT).as_posix()
        path = DEST / f"{NAME}.{extension}.meta"
        if not path.exists():
            meta = dict(assetType=kind, guid=uuid.uuid5(uuid.NAMESPACE_URL, relative).hex,
                        importSettings=settings, importer=importer, source=relative, version=1)
            path.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(triangles=count // 3, parts=len(objects),
                         minimum=[min(p[i] for p in bounds) for i in range(3)],
                         maximum=[max(p[i] for p in bounds) for i in range(3)])), flush=True)


def preview(directory):
    # 書き出したOBJを読み戻してから撮影し、生成途中だけでなく納品モデルの形と材質を確認する。
    directory.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.obj_import(filepath=str(DEST / f"{NAME}.obj"), forward_axis="NEGATIVE_Z", up_axis="Y")
    scene = bpy.context.scene
    scene.world.color = (.18, .18, .18)
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 48
    scene.render.film_transparent = True
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 1000
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "AgX"
    for position, energy, size in (((-3, -4, 7), 850, 5), ((5, 3, 5), 650, 4), ((-4, 4, 2), 550, 3)):
        bpy.ops.object.light_add(type="AREA", location=position)
        light = bpy.context.object
        light.data.energy = energy
        light.data.size = size
        light.rotation_euler = (Vector((0, 0, .2)) - light.location).to_track_quat("-Z", "Y").to_euler()
    bpy.ops.object.camera_add(location=(6, 8, 9))
    scene.camera = bpy.context.object
    scene.camera.data.type = "ORTHO"
    scene.camera.data.ortho_scale = 4.8
    scene.camera.rotation_euler = (Vector((0, 0, .2)) - scene.camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = str(directory / f"{NAME}_perspective.png")
    bpy.ops.render.render(write_still=True)
    scene.camera.location = (0, 0, 10)
    scene.camera.rotation_euler = (0, 0, math.pi)
    scene.camera.data.ortho_scale = 4.4
    scene.render.filepath = str(directory / f"{NAME}_top.png")
    bpy.ops.render.render(write_still=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview-dir", type=Path)
    options = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    navy = material("GuideNavy", (.012, .050, .105), .38, .12)
    ivory = material("GuideIvory", (1.0, .93, .78), .32, .08)
    gold = material("GuideGold", (1.0, .57, .018), .34, .18)
    objects = [layer("ArrowBody", 1.0, 0, .42, .075, navy),
               layer("ArrowBorder", .98, .32, .52, .055, ivory),
               layer("ArrowFace", .83, .46, .62, .045, gold)]
    for obj in objects:
        validate_mesh(obj)
    export(objects)
    if options.preview_dir:
        options.preview_dir.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(options.preview_dir / f"{NAME}.blend"))
        preview(options.preview_dir)


if __name__ == "__main__":
    main()