import argparse
import os
import sys

import bpy


def parse_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []

    parser = argparse.ArgumentParser(description="Generate a LOD model with Blender Decimate.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--ratio", required=True, type=float)
    return parser.parse_args(argv)


def reset_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def import_model(path):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".obj":
        if hasattr(bpy.ops.wm, "obj_import"):
            bpy.ops.wm.obj_import(filepath=path)
        else:
            bpy.ops.import_scene.obj(filepath=path)
        return

    if ext in (".gltf", ".glb"):
        bpy.ops.import_scene.gltf(filepath=path)
        return

    raise RuntimeError(f"Unsupported input extension: {ext}")


def apply_decimate(ratio):
    ratio = max(0.02, min(0.98, ratio))
    mesh_objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not mesh_objects:
        raise RuntimeError("No mesh objects were imported.")

    for obj in mesh_objects:
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj

        modifier = obj.modifiers.new(name="LOD_Decimate", type="DECIMATE")
        modifier.ratio = ratio
        if hasattr(modifier, "use_collapse_triangulate"):
            modifier.use_collapse_triangulate = True

        bpy.ops.object.modifier_apply(modifier=modifier.name)


def export_model(path):
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)

    ext = os.path.splitext(path)[1].lower()
    if ext == ".obj":
        if hasattr(bpy.ops.wm, "obj_export"):
            bpy.ops.wm.obj_export(filepath=path)
        else:
            bpy.ops.export_scene.obj(filepath=path, use_selection=False, use_materials=True, use_uvs=True, use_normals=True)
        return

    if ext == ".gltf":
        bpy.ops.export_scene.gltf(filepath=path, export_format="GLTF_SEPARATE")
        return

    if ext == ".glb":
        bpy.ops.export_scene.gltf(filepath=path, export_format="GLB")
        return

    raise RuntimeError(f"Unsupported output extension: {ext}")


def main():
    args = parse_args()
    reset_scene()
    import_model(os.path.abspath(args.input))
    apply_decimate(args.ratio)
    export_model(os.path.abspath(args.output))


if __name__ == "__main__":
    main()
