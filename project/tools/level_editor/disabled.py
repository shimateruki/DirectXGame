# disabled.py
import bpy

# --- オペレータ: 無効オプションを追加 ---
class MYADDON_OT_add_disabled(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_add_disabled"
    bl_label = "Add Disabled (無効フラグ追加)"
    bl_description = "オブジェクトに無効フラグを追加します"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        # Pythonのbool型の True を設定することで、自動的にチェックボックスになる
        context.object["disabled"] = True
        return {"FINISHED"}

# --- パネル: 無効オプションを表示 ---
class OBJECT_PT_disabled(bpy.types.Panel):
    bl_idname = "OBJECT_PT_disabled"
    bl_label = "Disabled (無効化)"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"

    def draw(self, context):
        # 既にプロパティがあれば、チェックボックスを表示
        if "disabled" in context.object:
            self.layout.prop(context.object, '["disabled"]', text="disabled")
        # なければ、追加用のボタンを表示
        else:
            self.layout.operator(MYADDON_OT_add_disabled.bl_idname)