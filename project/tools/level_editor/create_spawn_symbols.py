# create_spawn_symbols.py
import bpy

# --- プレイヤースポーン地点のシンボル（エンプティ）を作成 ---
class MYADDON_OT_create_player_spawn_symbol(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_create_player_spawn_symbol"
    bl_label = "出現ポイントシンボルの作成"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        # 十字のエンプティを作成
        bpy.ops.object.empty_add(type='PLAIN_AXES')
        # 名前をわかりやすく変更
        context.object.name = "PlayerSpawn"
        # スポーンフラグを自動でTrueにする
        context.object["spawn"] = True

        print("プレイヤースポーンシンボルを作成しました。")
        return {'FINISHED'}

# --- 敵スポーン地点のシンボル（エンプティ）を作成 ---
class MYADDON_OT_create_enemy_spawn_symbol(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_create_enemy_spawn_symbol"
    bl_label = "敵出現ポイントシンボルの作成"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        # 十字のエンプティを作成
        bpy.ops.object.empty_add(type='PLAIN_AXES')
        # 名前をわかりやすく変更
        context.object.name = "EnemySpawn"
        # 敵フラグを自動でTrueにする
        context.object["enemy"] = True

        print("敵スポーンシンボルを作成しました。")
        return {'FINISHED'}
