"""Blenderから実行し、ステージ3の玉座の石角を小さく面取りします。"""
import bpy
import bmesh
from pathlib import Path
import sys

def bevel_obj(path):
    vertices, texcoords, faces, face_uvs, face_materials = [], [], [], [], []
    materials = []
    material = ""
    for line in path.read_text(encoding="utf8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v":
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "vt":
            texcoords.append(tuple(map(float, fields[1:3])))
        elif fields[0] == "usemtl":
            material = fields[1]
            if material not in materials:
                materials.append(material)
        elif fields[0] == "f":
            corners = [item.split("/") for item in fields[1:]]
            faces.append([int(item[0]) - 1 for item in corners])
            face_uvs.append([texcoords[int(item[1]) - 1] for item in corners])
            face_materials.append(materials.index(material))
    mesh = bpy.data.meshes.new(path.stem)
    mesh.from_pydata(vertices, [], faces)
    for name in materials:
        mesh.materials.append(bpy.data.materials.new(name))
    uv = mesh.uv_layers.new()
    for polygon, uvs, material_index in zip(mesh.polygons, face_uvs, face_materials):
        polygon.material_index = material_index
        for loop_index, coord in zip(polygon.loop_indices, uvs):
            uv.data[loop_index].uv = coord
    obj = bpy.data.objects.new(path.stem, mesh)
    bpy.context.collection.objects.link(obj)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.00001)
    # 平面の三角形境界を残し、立体の角だけ幅6cmで面取りする。
    edges = [edge for edge in bm.edges if edge.is_manifold and edge.calc_face_angle() > 0.4]
    bmesh.ops.bevel(bm, geom=edges, offset=0.06, segments=2, affect="EDGES", clamp_overlap=True)
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    lines = ["# Stage 3 stone edge finish", "mtllib high_crown.mtl", "o " + path.stem]
    for vertex in mesh.vertices:
        lines.append("v " + " ".join(f"{value:.7f}" for value in vertex.co))
    for loop in mesh.loops:
        coord = mesh.uv_layers.active.data[loop.index].uv
        lines.append(f"vt {coord.x:.7f} {coord.y:.7f}")
    for polygon in mesh.polygons:
        for _ in polygon.loop_indices:
            lines.append("vn " + " ".join(f"{value:.7f}" for value in polygon.normal))
    active = None
    for polygon in mesh.polygons:
        name = materials[polygon.material_index]
        if active != name:
            lines.append("usemtl " + name)
            active = name
        lines.append("f " + " ".join(f"{mesh.loops[i].vertex_index+1}/{i+1}/{i+1}" for i in polygon.loop_indices))
    path.write_text("\n".join(lines) + "\n", encoding="utf8")
    bpy.data.objects.remove(obj, do_unlink=True)

if __name__ == "__main__":
    bevel_obj(Path(sys.argv[sys.argv.index("--") + 1]))
