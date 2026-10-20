"""丸太いかだのモデルを生成する。--preview を付けた Blender 実行では透過プレビューも出力する。"""
import math
import sys
import copy
import json
import uuid
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/stage_generation'))
from dress_entry_garden import Mesh

OUT = ROOT / 'Resources/3DModel/Gimmicks/wooden_raft'
PALETTE = {
    'Bark': (.42, .23, .105), 'BarkLight': (.55, .32, .15),
    'Endgrain': (.76, .54, .29), 'GrowthRing': (.48, .29, .13),
    'Plank': (.67, .43, .22), 'PlankLight': (.75, .51, .28),
    'Rope': (.81, .69, .44), 'RopeShade': (.65, .51, .29),
}


def build():
    """乗れる面は平らな板張りとし、その下に丸太と結束縄を見せる。"""
    mesh = Mesh()
    segments = 12
    for row in range(7):
        z = (row - 3) * 1.94
        radius = .97
        length = 9.8 + (.20, -.12, .07, .25, -.08, .15, -.02)[row]
        rings = []
        for x, r in ((-length, .87), (-length + .18, 1), (length - .18, 1), (length, .87)):
            rings.append([(x, -.45 + radius * r * math.cos(i * math.tau / segments),
                           z + radius * r * math.sin(i * math.tau / segments)) for i in range(segments)])
        for band in range(3):
            for i in range(segments):
                j = (i + 1) % segments
                a, b, c, d = rings[band][i], rings[band+1][i], rings[band+1][j], rings[band][j]
                material = 'BarkLight' if (i + row) % 3 == 0 else 'Bark'
                mesh.triangle(a, c, b, material)
                mesh.triangle(a, d, c, material)
        for side in (0, 3):
            x = rings[side][0][0]
            for i in range(segments):
                j = (i + 1) % segments
                a, b = rings[side][i], rings[side][j]
                if side == 0:
                    a, b = b, a
                mesh.triangle((x, -.45, z), a, b, 'Endgrain')
            # 木口の年輪は同一平面の重なりを避け、端面から僅かに離す。
            x += -.004 if side == 0 else .004
            for radius in (.27, .55):
                for i in range(segments):
                    j = (i + 1) % segments
                    pts = [(x, -.45 + r * math.cos(k * math.tau / segments),
                            z + r * math.sin(k * math.tau / segments))
                           for r, k in ((radius, i), (radius, j), (radius+.028, j), (radius+.028, i))]
                    mesh.triangle(pts[0], pts[1], pts[2], 'GrowthRing', True)
                    mesh.triangle(pts[0], pts[2], pts[3], 'GrowthRing', True)
    # 板の外周を矩形に揃え、見えている乗車面と箱判定の範囲を一致させる。
    for i in range(10):
        mesh.block((-8.55+i*1.9, .54, 0), (1.88, .22, 12.8), 'PlankLight' if i % 3 == 1 else 'Plank')
    # 縄を板より低い外側へ回す。乗車面に引っ掛かる装飾を置かない。
    for x in (-6.8, 6.8):
        mesh.block((x, -1.38, 0), (.35, .15, 13.6), 'RopeShade')
        for z in (-6.76, 6.76):
            mesh.block((x, -.50, z), (.36, 1.65, .18), 'Rope')
            mesh.block((x, .35, z*.96), (.36, .17, .58), 'Rope')
            for shift in (-.13, .13):
                mesh.block((x+shift, -.47, z*1.006), (.07, 1.46, .06), 'RopeShade')
    return mesh


def save(mesh):
    """エンジンの左手座標と既存OBJの面順に合わせて、独立した材質付きモデルを保存する。"""
    OUT.mkdir(parents=True, exist_ok=True)
    lines = ['mtllib wooden_raft.mtl', 'o WoodenRaft']
    lines += ['v %.6f %.6f %.6f' % (p[0], p[1], -p[2]) for p, _ in mesh.vertices]
    lines += ['vn %.6f %.6f %.6f' % (n[0], n[1], -n[2]) for _, n in mesh.vertices]
    for material, indices in mesh.faces:
        lines += ['usemtl '+material, 'f '+' '.join(f'{i}//{i}' for i in indices)]
    (OUT/'wooden_raft.obj').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    (OUT/'wooden_raft.mtl').write_text('\n'.join(
        f'newmtl {name}\nKd {r} {g} {b}\nKs 0.025 0.025 0.025\nNs 12\nillum 2\n'
        for name, (r, g, b) in PALETTE.items()), encoding='utf-8')


def register(mesh):
    """既存プリセットを保ったまま、経路を持たない移動床として一件追加する。"""
    path = ROOT/'Resources/json/preset/presets.json'
    presets = json.loads(path.read_text(encoding='utf-8-sig'))
    key = 'Stage1/Gimmick/WoodenRaft'
    if key in presets:
        raise RuntimeError('既存のいかだプリセットは上書きしません。Editorで調整してください。')
    item = copy.deepcopy(presets['Builtin/Gimmick/MovingFloor'])
    item.pop('builtinCreateTemplate', None)
    item.update(name='木のいかだ', presetDisplayName='木のいかだ（移動床）',
                presetUsage='ギミック', modelName='Gimmicks/wooden_raft/wooden_raft.obj',
                blendMode=0, roughness=.78, metallic=0.0, enableLighting=True,
                castShadow=True, isStatic=False, collisionMask=251,
                translate=[0, 0, 0], rotate=[0, 0, 0], scale=[1, 1, 1])
    # 床上は平らな板の高さ、側面・底面は丸太の外形まで含む単純な箱にする。
    lo = [min(p[i] for p, _ in mesh.vertices) for i in range(3)]
    hi = [max(p[i] for p, _ in mesh.vertices) for i in range(3)]
    item['collider'] = dict(type=3, center=[(a+b)/2 for a, b in zip(lo, hi)],
                            size=[(b-a)/2 for a, b in zip(lo, hi)], rotation=[0, 0, 0])
    item['param'].update(gimmickType='MovingFloor', actionMode=1, moveAmount=0.0, speed=1.0)
    item['components'] = {'PathMover': dict(version=1, _editorPresent=True, path='',
                                          rideSequence='', loop=False, relative=False)}
    presets[key] = item
    path.write_text(json.dumps(presets, ensure_ascii=False, indent=4)+'\n', encoding='utf-8')
    model_path = OUT/'wooden_raft.obj'
    meta = dict(assetType='Model', guid=uuid.uuid5(uuid.NAMESPACE_URL, 'Gimmicks/wooden_raft').hex,
                importSettings=dict(generateTangents=True, scale=1.0), importer='ModelImporter',
                source=model_path.relative_to(ROOT).as_posix(), version=1)
    meta_path = model_path.with_suffix('.obj.meta')
    if not meta_path.exists():
        meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Preset registered:', key, 'bounds:', lo, hi)


def preview(mesh):
    """実際に書き出す頂点から、モデル確認用の透過画像をレンダリングする。"""
    import bpy
    from mathutils import Vector
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    data = bpy.data.meshes.new('WoodenRaft')
    # エンジンの上方向YをBlenderのZへ変換し、外向きに面順を合わせる。
    data.from_pydata([(p[0], -p[2], p[1]) for p, _ in mesh.vertices], [],
                     [tuple(i-1 for i in reversed(face)) for _, face in mesh.faces])
    data.update()
    obj = bpy.data.objects.new('WoodenRaft', data)
    bpy.context.collection.objects.link(obj)
    names = list(PALETTE)
    for name, color in PALETTE.items():
        mat = bpy.data.materials.new(name)
        mat.diffuse_color = (*color, 1)
        mat.use_nodes = True
        node = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        node.inputs['Base Color'].default_value = (*color, 1)
        node.inputs['Roughness'].default_value = .78
        data.materials.append(mat)
    for poly, (material, _) in zip(data.polygons, mesh.faces):
        poly.material_index = names.index(material)
    bpy.ops.object.camera_add(location=(26, -30, 25))
    camera = bpy.context.object
    camera.rotation_euler = (Vector((0, 0, 0))-camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 32
    scene = bpy.context.scene
    scene.camera = camera
    bpy.ops.object.light_add(type='AREA', location=(2, -10, 24))
    bpy.context.object.data.energy = 4500
    bpy.context.object.data.shape = 'DISK'
    bpy.context.object.data.size = 15
    scene.world.color = (.65, .65, .65)
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 32
    scene.render.film_transparent = True
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 1000
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.view_settings.view_transform = 'Standard'
    dest = ROOT/'output/raft-preview'
    dest.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(dest/'wooden_raft.png')
    bpy.ops.wm.save_as_mainfile(filepath=str(dest/'wooden_raft.blend'))
    bpy.ops.render.render(write_still=True)


if __name__ == '__main__':
    raft = build()
    save(raft)
    if '--register' in sys.argv:
        register(raft)
    if '--preview' in sys.argv:
        preview(raft)
    print('Raft triangles:', len(raft.faces))
