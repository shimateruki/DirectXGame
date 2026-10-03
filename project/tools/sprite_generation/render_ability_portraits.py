"""実ゲームのglTFモデルから能力ガイド用の透過肖像を生成する。"""
import argparse
from pathlib import Path
import bpy
from mathutils import Vector

def render_portrait(root, key, model):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(root / "Resources/3DModel/Characters" / model / (model + ".gltf")))
    scene = bpy.context.scene
    meshes = [obj for obj in scene.objects if obj.type == "MESH"]
    bounds = [obj.matrix_world @ Vector(corner) for obj in meshes for corner in obj.bound_box]
    low = Vector(tuple(min(p[i] for p in bounds) for i in range(3)))
    high = Vector(tuple(max(p[i] for p in bounds) for i in range(3)))
    center = (low + high) * 0.5
    extent = max(high.x - low.x, high.y - low.y, high.z - low.z)
    print("PORTRAIT", key, "bounds", tuple(low), tuple(high), flush=True)
    for obj in meshes:
        print("MESH", obj.name, [m.name if m else "" for m in obj.data.materials], flush=True)
        for material in obj.data.materials:
            if not material or not material.use_nodes:
                continue
            for node in material.node_tree.nodes:
                if node.type == "BSDF_PRINCIPLED":
                    node.inputs["Roughness"].default_value = 0.38
    bpy.ops.object.camera_add(location=center + Vector((extent * 0.16, -extent * 2.8, extent * 0.65)))
    camera = bpy.context.object
    camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = extent * 1.25
    scene.camera = camera
    for offset, energy, size in [((-1.7, -2.5, 3), 120, 2.0), ((2, -1, 1.5), 28, 2.0), ((0, 2, 2.8), 90, 1.5)]:
        bpy.ops.object.light_add(type="AREA", location=center + Vector(offset) * extent)
        lamp = bpy.context.object
        lamp.data.energy = energy * extent * extent
        lamp.data.shape = "DISK"
        lamp.data.size = size * extent
        lamp.rotation_euler = (center - lamp.location).to_track_quat("-Z", "Y").to_euler()
    scene.world = bpy.data.worlds.new("PortraitWorld")
    scene.world.use_nodes = True
    background = next(node for node in scene.world.node_tree.nodes if node.type == "BACKGROUND")
    background.inputs[0].default_value = (0.65, 0.75, 0.9, 1)
    background.inputs[1].default_value = 0.35
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = True
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    output = root / "Resources/sprite/ui/control_guide/book/portraits"
    output.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(output / (key + ".png"))
    bpy.ops.render.render(write_still=True)

if __name__ == "__main__":
    import sys
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--only", default="")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    models = {"normal": "slime", "pink": "slime_pink", "fire": "slime_red", "thunder": "slime_yellow", "wind": "slime_wind", "bomb": "slime_black", "crown": "slime_crown"}
    for key, model in models.items():
        if not args.only or args.only == key:
            render_portrait(Path(args.root), key, model)
