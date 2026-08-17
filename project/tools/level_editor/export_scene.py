# export_scene.py
import bpy
import bpy_extras
import json

class MYADDON_OT_export_scene(bpy.types.Operator, bpy_extras.io_utils.ExportHelper):
    bl_idname = "myaddon.myaddon_ot_export_scene"
    bl_label = "シーン出力 (JSON)"
    bl_description = "シーン情報をJSON形式でExportします"
    filename_ext = ".json"

    def parse_scene_recursive(self, object, objects_list):
        if "disabled" in object and object["disabled"] == True:
            return

        obj_data = {}
        obj_data["name"] = object.name
        obj_data["type"] = object.type

        trans, rot, scale = object.matrix_local.decompose()
        rot_euler = rot.to_euler()

        obj_data["transform"] = {
            "translation": [trans.x, trans.y, trans.z],
            "rotation": [rot_euler.x, rot_euler.y, rot_euler.z],
            "scaling": [scale.x, scale.y, scale.z]
        }

        if object.parent:
            obj_data["parentName"] = object.parent.name

        if "file_name" in object:
            # C++側が "file_name" を期待している場合はキーを合わせます
            obj_data["file_name"] = object["file_name"]

        if "spawn" in object:
            obj_data["spawn"] = True

        if "enemy" in object:
            obj_data["enemy"] = True

        if "collider" in object:
            col_type_int = 2
            if object["collider"] == "SPHERE": col_type_int = 1
            elif object["collider"] == "CYLINDER": col_type_int = 4

            obj_data["collider"] = {
                "type": col_type_int,
                "center": [object["collider_center"][0], object["collider_center"][1], object["collider_center"][2]],
                "size": [object["collider_size"][0], object["collider_size"][1], object["collider_size"][2]],
                "rotation": [0.0, 0.0, 0.0]
            }

        # ★修正: 子オブジェクトを格納するための children 配列を準備
        obj_data["children"] = []

        # このオブジェクトを親のリスト（またはルートのリスト）に追加
        objects_list.append(obj_data)

        # ★修正: 子供をパースする際、自分自身の children 配列を渡す
        for child in object.children:
            self.parse_scene_recursive(child, obj_data["children"])

    def export(self):
        print("シーン情報(JSON)出力開始... %r" % self.filepath)
        scene_data = {"name": "scene", "objects": []}

        for object in bpy.context.scene.objects:
            if not object.parent:
                self.parse_scene_recursive(object, scene_data["objects"])

        with open(self.filepath, "w", encoding="utf-8") as file:
            json.dump(scene_data, file, indent=4, ensure_ascii=False)

    def execute(self, context):
        self.export()
        self.report({'INFO'}, "シーン情報(JSON)をExportしました")
        return {'FINISHED'}
