"""溶岩に浸かる厚みを持つ三角岩を、既存玄武岩と同じPBR工程で作る。"""
import sys
from pathlib import Path
import bpy
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import refine_floating_rocks_pbr as pbr
from build_floating_rocks import mesh_object

def main():
    # 三角形の角を少し切り、側面・底面を閉じた凸形状にする。
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.context.scene.render.engine='CYCLES'
    bpy.context.scene.cycles.samples=16
    outline=[(-5.6,-10),(5.6,-10),(5.9,-9.4),(.3,9.6),(-.3,9.6),(-5.9,-9.4)]
    vertices=[(x,y,z) for z in (-3,0) for x,y in outline]
    faces=[tuple(reversed(range(6))),tuple(range(6,12))]+[(i,(i+1)%6,(i+1)%6+6,i+6) for i in range(6)]
    name='sinking_basalt_triangle'
    (pbr.ROOT/'Resources/3DModel/Gimmicks'/name).mkdir(parents=True,exist_ok=True)
    material=pbr.procedural_material()
    obj=mesh_object(name,vertices,faces,[material],[0]*len(faces))
    pbr.build_one(name,obj,material)

if __name__=='__main__':
    main()
