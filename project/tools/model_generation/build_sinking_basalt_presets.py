"""乗車中に沈むPBR岩足場3種と、対応する録画・配置プリセットを生成する。"""
import sys, json, copy, uuid
from pathlib import Path
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
from mathutils import Vector
import refine_floating_rocks_pbr as pbr
from build_floating_rocks import mesh_object
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'output/sinking-rock-platforms'
SPECS = [('short','沈む岩足場：短め',4.5,7.,1.4,7.,4.5),
         ('long','沈む岩足場：長い直線',4.,14.,1.6,11.,5.5),
         ('wide','沈む岩足場：幅広ロング',7.,12.,1.8,13.,6.)]

def metadata(path,kind,importer):
    # 再生成でGUIDを変えず、配置済みの参照を維持する。
    meta=Path(str(path)+'.meta')
    if not meta.exists():
        meta.write_text(json.dumps(dict(assetType=kind,guid=uuid.uuid5(uuid.NAMESPACE_URL,path.relative_to(ROOT).as_posix()).hex,
            importer=importer,source=path.relative_to(ROOT).as_posix(),version=1),indent=2)+'\n',encoding='utf-8')

def main():
    # 見た目は従来の玄武岩と共通の素材を使い、サイズごとにUVを焼き直す。
    OUT.mkdir(parents=True,exist_ok=True)
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=16
    source=pbr.procedural_material()
    presetpath=ROOT/'Resources/json/preset/presets.json'
    presets=json.loads(presetpath.read_text(encoding='utf-8-sig'))
    records=[]
    for index,(key,label,width,length,depth,seconds,sink) in enumerate(SPECS):
        for o in list(bpy.data.objects): bpy.data.objects.remove(o,do_unlink=True)
        name='sinking_basalt_'+key;dest=ROOT/'Resources/3DModel/Gimmicks'/name;dest.mkdir(parents=True,exist_ok=True)
        x,y,c=width/2,length/2,.4
        outline=[(-x+c,-y),(x-c,-y),(x,-y+c),(x,y-c),(x-c,y),(-x+c,y),(-x,y-c),(-x,-y+c)]
        vertices=[(a,b,z) for z in (-depth,0) for a,b in outline]
        faces=[tuple(reversed(range(8))),tuple(range(8,16))]+[(i,(i+1)%8,(i+1)%8+8,i+8) for i in range(8)]
        obj=mesh_object(name,vertices,faces,[source],[0]*len(faces))
        models=pbr.build_one(name,obj,source)
        metadata(dest/(name+'.obj'),'Model','ModelImporter')
        # 寸法はモデルと共通。側面・底面もある軽量な凸柱判定にする。
        preset=copy.deepcopy(presets['Builtin/Gimmick/MovingFloor'])
        preset.pop('builtinCreateTemplate',None)
        path='sinking_basalt_'+key+'_ride'
        preset.update(name='SinkingBasalt_'+key,presetDisplayName=label,presetUsage='ギミック',
            modelName='Gimmicks/'+name+'/'+name+'_pbr.gltf',blendMode=0,color=[1,1,1,1],
            emissive=0.,metallic=0.,roughness=1.,enableNormalMap=True,enableEnvMap=True,envIntensity=.25,
            isStatic=False,materialType=0,texturePath='',normalMapPath='',ormMapPath='',scale=[1,1,1])
        preset['param'].update(gimmickType='MovingFloor',actionMode=0,speed=0.,returnOnOff=False)
        preset['collider']=dict(type=3,center=[0,-depth/2,0],size=[x,depth/2,y],rotation=[0,0,0],
            footprint=[[a,-b] for a,b in reversed(outline)],useModelSurface=False,useModelVolume=False)
        preset['components']=dict(PathMover=dict(version=1,_editorPresent=True,path=path,loop=False,
            relative=True,rideSequence='',playbackMode='whileRiding'))
        preset['recorder']=dict(recordPathName=path,isRecordLoop=False,isRecordRelative=True)
        frames=[dict(pos=[0,round(-sink*i/(seconds*60),6),0],rot=[0,0,0],scale=[1,1,1],eventID=0)
                for i in range(int(seconds*60)+1)]
        motion=ROOT/'Resources/json/animation'/(path+'.json');motion.parent.mkdir(exist_ok=True,parents=True)
        motion.write_text(json.dumps(dict(frames=frames,genParams=dict(generateRelative=True)),ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
        metadata(motion,'Json','JsonImporter')
        presets['Stage2/Gimmick/SinkingBasalt_'+key]=preset
        # 配布glTFを読み戻したモデルを撮影する。
        bpy.ops.object.camera_add(location=(12,-18,15));scene.camera=bpy.context.object
        scene.camera.rotation_euler=(Vector((0,0,-.5))-scene.camera.location).to_track_quat('-Z','Y').to_euler()
        scene.camera.data.type='ORTHO';scene.camera.data.ortho_scale=max(length,width)*1.3
        for pos,power in [((-4,-6,12),2300),((7,5,9),2800)]:
            bpy.ops.object.light_add(type='AREA',location=pos);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=7
            light.rotation_euler=(-light.location).to_track_quat('-Z','Y').to_euler()
        scene.world.color=(.22,.22,.22);scene.render.film_transparent=True
        scene.render.resolution_x=1000;scene.render.resolution_y=850;scene.render.resolution_percentage=100
        scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA'
        scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
        records.append(dict(key=key,width=width,length=length,depth=depth,seconds=seconds,sink=sink))
    presetpath.write_text(json.dumps(presets,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
    (OUT/'specs.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__': main()
