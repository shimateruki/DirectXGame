# __init__.py
import bpy

bl_info = {
    "name": "レベルエディタ",
    "author": "Taro Kamata",
    "version": (1, 0),
    "blender": (3, 3, 1),
    "location": "",
    "description": "レベルエディタ",
    "warning": "",
    "wiki_url": "",
    "tracker_url": "",
    "category": "Object"
}

# --- モジュールのインポート ---
from .stretch_vertex import MYADDON_OT_stretch_vertex
from .create_ico_sphere import MYADDON_OT_create_ico_sphere
from .export_scene import MYADDON_OT_export_scene
from .properties import (
    DrawCollider,
    MYADDON_OT_add_filename,
    OBJECT_PT_file_name,
    MYADDON_OT_add_collider,
    OBJECT_PT_collider
)
from .disabled import (
    MYADDON_OT_add_disabled,
    OBJECT_PT_disabled
)
from .spawn_point import (
    MYADDON_OT_add_spawn_point,
    OBJECT_PT_spawn_point
)
from .enemy_spawn import (
    MYADDON_OT_add_enemy_spawn,
    OBJECT_PT_enemy_spawn
)
# ★追加
from .create_spawn_symbols import (
    MYADDON_OT_create_player_spawn_symbol,
    MYADDON_OT_create_enemy_spawn_symbol
)

# --- トップメニュークラス ---
class TOPBAR_MT_my_menu(bpy.types.Menu):
    bl_idname = "myaddon.topbar_mt_my_menu"
    bl_label = "MyMenu"
    def draw(self, context):
        layout = self.layout
        layout.operator(MYADDON_OT_stretch_vertex.bl_idname, text=MYADDON_OT_stretch_vertex.bl_label)
        layout.operator(MYADDON_OT_create_ico_sphere.bl_idname, text=MYADDON_OT_create_ico_sphere.bl_label)

        layout.separator() # 区切り線

        # ★メニュー項目の追加
        layout.operator(MYADDON_OT_create_player_spawn_symbol.bl_idname, text=MYADDON_OT_create_player_spawn_symbol.bl_label)
        layout.operator(MYADDON_OT_create_enemy_spawn_symbol.bl_idname, text=MYADDON_OT_create_enemy_spawn_symbol.bl_label)

        layout.separator()

        layout.operator(MYADDON_OT_export_scene.bl_idname, text=MYADDON_OT_export_scene.bl_label)

def submenu(self, context):
    self.layout.menu(TOPBAR_MT_my_menu.bl_idname)

# --- 登録クラスリスト ---
classes = (
    MYADDON_OT_stretch_vertex,
    MYADDON_OT_create_ico_sphere,
    MYADDON_OT_export_scene,
    MYADDON_OT_add_filename,
    OBJECT_PT_file_name,
    MYADDON_OT_add_collider,
    OBJECT_PT_collider,
    MYADDON_OT_add_disabled,
    OBJECT_PT_disabled,
    MYADDON_OT_add_spawn_point,
    OBJECT_PT_spawn_point,
    MYADDON_OT_add_enemy_spawn,
    OBJECT_PT_enemy_spawn,
    MYADDON_OT_create_player_spawn_symbol,
    MYADDON_OT_create_enemy_spawn_symbol,
    TOPBAR_MT_my_menu,
)

def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_editor_menus.append(submenu)
    DrawCollider.handle = bpy.types.SpaceView3D.draw_handler_add(DrawCollider.draw_collider, (), "WINDOW", "POST_VIEW")
    print("レベルエディタが有効化されました。")

def unregister():
    bpy.types.TOPBAR_MT_editor_menus.remove(submenu)
    if DrawCollider.handle is not None:
        bpy.types.SpaceView3D.draw_handler_remove(DrawCollider.handle, "WINDOW")
        DrawCollider.handle = None
    for cls in classes:
        bpy.utils.unregister_class(cls)
    print("レベルエディタが無効化されました。")

if __name__ == "__main__":
    register()
