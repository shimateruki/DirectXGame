# properties.py
import bpy
import mathutils
import gpu
import gpu_extras.batch
import copy

# --- 描画クラス ---
class DrawCollider:
    handle = None

    @staticmethod
    def draw_collider():
        col_vertices = {"pos": []}
        col_indices = []

        spawn_vertices = {"pos": []}
        spawn_indices = []

        # ★追加: 敵スポーン用の頂点とインデックス
        enemy_vertices = {"pos": []}
        enemy_indices = []

        offsets = [
            [-0.5, -0.5, -0.5], [+0.5, -0.5, -0.5],
            [-0.5, +0.5, -0.5], [+0.5, +0.5, -0.5],
            [-0.5, -0.5, +0.5], [+0.5, -0.5, +0.5],
            [-0.5, +0.5, +0.5], [+0.5, +0.5, +0.5],
        ]

        for object in bpy.context.scene.objects:
            # 1. プレイヤースポーンポイントの描画計算 (緑ライン)
            if "spawn" in object:
                start = len(spawn_vertices["pos"])
                p1 = object.matrix_world @ mathutils.Vector((0, 0, 0))
                p2 = object.matrix_world @ mathutils.Vector((0, -2, 0))
                spawn_vertices["pos"].append(p1)
                spawn_vertices["pos"].append(p2)
                spawn_indices.append([start, start+1])

            #: 2. 敵スポーンポイントの描画計算 (赤ライン)
            if "enemy" in object:
                start = len(enemy_vertices["pos"])
                p1 = object.matrix_world @ mathutils.Vector((0, 0, 0))
                p2 = object.matrix_world @ mathutils.Vector((0, -2, 0))
                enemy_vertices["pos"].append(p1)
                enemy_vertices["pos"].append(p2)
                enemy_indices.append([start, start+1])

            # 3. コライダーの描画計算 (水色ボックス)
            if "collider" in object:
                center = mathutils.Vector((0, 0, 0))
                size = mathutils.Vector((2, 2, 2))

                center[0] = object["collider_center"][0]
                center[1] = object["collider_center"][1]
                center[2] = object["collider_center"][2]
                size[0] = object["collider_size"][0]
                size[1] = object["collider_size"][1]
                size[2] = object["collider_size"][2]

                start = len(col_vertices["pos"])

                for offset in offsets:
                    pos = copy.copy(center)
                    pos[0] += offset[0] * size[0]
                    pos[1] += offset[1] * size[1]
                    pos[2] += offset[2] * size[2]
                    pos = object.matrix_world @ pos
                    col_vertices["pos"].append(pos)

                col_indices.extend([
                    [start+0, start+1], [start+2, start+3], [start+0, start+2], [start+1, start+3],
                    [start+4, start+5], [start+6, start+7], [start+4, start+6], [start+5, start+7],
                    [start+0, start+4], [start+1, start+5], [start+2, start+6], [start+3, start+7]
                ])

        # --- 実際の描画処理 ---
        shader = gpu.shader.from_builtin("UNIFORM_COLOR")
        shader.bind()

        # コライダーの描画（水色）
        if len(col_vertices["pos"]) > 0:
            batch_col = gpu_extras.batch.batch_for_shader(shader, "LINES", col_vertices, indices=col_indices)
            shader.uniform_float("color", [0.5, 1.0, 1.0, 1.0])
            batch_col.draw(shader)

        # プレイヤースポーンの描画（緑色）
        if len(spawn_vertices["pos"]) > 0:
            batch_spawn = gpu_extras.batch.batch_for_shader(shader, "LINES", spawn_vertices, indices=spawn_indices)
            shader.uniform_float("color", [0.0, 1.0, 0.0, 1.0])
            batch_spawn.draw(shader)

        # ★追加: 敵スポーンの描画（赤色）
        if len(enemy_vertices["pos"]) > 0:
            batch_enemy = gpu_extras.batch.batch_for_shader(shader, "LINES", enemy_vertices, indices=enemy_indices)
            shader.uniform_float("color", [1.0, 0.0, 0.0, 1.0]) # 赤色
            batch_enemy.draw(shader)

# --- 以下略 (既存の MYADDON_OT_add_filename 等はそのまま残してください) ---
class MYADDON_OT_add_filename(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_add_filename"
    bl_label = "FileName 追加"
    bl_options = {"REGISTER", "UNDO"}
    def execute(self, context):
        context.object["file_name"] = "cube"
        return {"FINISHED"}

class OBJECT_PT_file_name(bpy.types.Panel):
    bl_idname = "OBJECT_PT_file_name"
    bl_label = "FileName (Model)"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"
    def draw(self, context):
        if "file_name" in context.object:
            self.layout.prop(context.object, '["file_name"]', text="Model Name")
        else:
            self.layout.operator(MYADDON_OT_add_filename.bl_idname)

class MYADDON_OT_add_collider(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_add_collider"
    bl_label = "コライダー 追加"
    bl_options = {"REGISTER", "UNDO"}
    def execute(self, context):
        context.object["collider"] = "BOX"
        context.object["collider_center"] = mathutils.Vector((0, 0, 0))
        context.object["collider_size"] = mathutils.Vector((1, 1, 1))
        return {"FINISHED"}

class OBJECT_PT_collider(bpy.types.Panel):
    bl_idname = "OBJECT_PT_collider"
    bl_label = "Collider"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"
    def draw(self, context):
        if "collider" in context.object:
            self.layout.prop(context.object, '["collider"]', text="Type")
            self.layout.prop(context.object, '["collider_center"]', text="Center")
            self.layout.prop(context.object, '["collider_size"]', text="Size")
        else:
            self.layout.operator(MYADDON_OT_add_collider.bl_idname)
