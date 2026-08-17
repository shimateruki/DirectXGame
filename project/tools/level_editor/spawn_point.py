# spawn_point.py
import bpy

# --- オペレータ: SpawnPointオプションを追加 ---
class MYADDON_OT_add_spawn_point(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_add_spawn_point"
    bl_label = "SpawnPoint 追加"
    bl_description = "このオブジェクトをプレイヤーのスポーン地点に設定します"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        # カスタムプロパティ "spawn" を追加
        context.object["spawn"] = True
        return {"FINISHED"}

# --- パネル: SpawnPointオプションを表示 ---
class OBJECT_PT_spawn_point(bpy.types.Panel):
    bl_idname = "OBJECT_PT_spawn_point"
    bl_label = "SpawnPoint"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"

    def draw(self, context):
        if "spawn" in context.object:
            self.layout.prop(context.object, '["spawn"]', text="Spawn")
        else:
            self.layout.operator(MYADDON_OT_add_spawn_point.bl_idname)