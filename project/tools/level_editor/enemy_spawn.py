# enemy_spawn.py
import bpy

# --- オペレータ: EnemySpawnオプションを追加 ---
class MYADDON_OT_add_enemy_spawn(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_add_enemy_spawn"
    bl_label = "EnemySpawn 追加"
    bl_description = "このオブジェクトを敵のスポーン地点に設定します"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        # カスタムプロパティ "enemy" を追加
        context.object["enemy"] = True
        return {"FINISHED"}

# --- パネル: EnemySpawnオプションを表示 ---
class OBJECT_PT_enemy_spawn(bpy.types.Panel):
    bl_idname = "OBJECT_PT_enemy_spawn"
    bl_label = "EnemySpawn"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"

    def draw(self, context):
        if "enemy" in context.object:
            self.layout.prop(context.object, '["enemy"]', text="Enemy")
        else:
            self.layout.operator(MYADDON_OT_add_enemy_spawn.bl_idname)