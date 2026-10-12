"""炎スライムの Blender アクションを Body Clip と glTF に書き出す。"""

import json
import sys
from pathlib import Path

import bpy

PROJECT = Path(__file__).resolve().parents[2]
SOURCE = PROJECT / "Resources/3DModel/Characters/slime_red/slime_red.gltf"
DESTINATION = PROJECT / "Resources/3DModel/Characters/fireslimeanim"
BLEND = DESTINATION / "fireslimeanim.blend"
GLTF = DESTINATION / "fireslimeanim.gltf"
CLIP_DIRECTORY = PROJECT / "Resources/json/animation_clip"
FPS = 30

# Blender の拡縮 X/Y/Z はゲームの X/Z/Y に対応する。
MOTIONS = {
    "Idle": (0.8, [(0.00, (1, 1, 1), 0), (0.20, (1.02, 1.02, 0.97), 0.01),
                    (0.40, (0.98, 0.98, 1.04), -0.01), (0.60, (1.02, 1.02, 0.97), 0.01), (0.80, (1, 1, 1), 0)]),
    "Move": (0.56, [(0.00, (1, 1, 1), 0), (0.14, (0.88, 0.90, 1.20), -0.04),
                     (0.28, (1.12, 1.12, 0.82), 0.05), (0.42, (0.90, 0.92, 1.17), -0.03), (0.56, (1, 1, 1), 0)]),
    "Breath": (0.62, [(0.00, (1, 1, 1), 0), (0.16, (1.14, 0.92, 1.10), 0.09),
                       (0.38, (1.20, 1.30, 0.78), -0.08), (0.52, (0.95, 1.12, 1.12), -0.04), (0.62, (1, 1, 1), 0)]),
    "Fireball": (0.8, [(0.00, (1, 1, 1), 0), (0.13, (1.06, 0.98, 0.91), 0.04),
                        (0.37, (1.18, 0.92, 0.77), 0.12), (0.47, (1.16, 0.94, 0.80), 0.10),
                        (0.60, (0.94, 1.18, 1.12), -0.13), (0.80, (1, 1, 1), 0)]),
}


def pose_at(poses, seconds):
    for left, right in zip(poses, poses[1:]):
        if seconds <= right[0]:
            t = max(0.0, min(1.0, (seconds - left[0]) / (right[0] - left[0])))
            t = t * t * (3.0 - 2.0 * t)
            scale = tuple(a + (b - a) * t for a, b in zip(left[1], right[1]))
            return scale, left[2] + (right[2] - left[2]) * t
    return poses[-1][1], poses[-1][2]


def create_sample():
    if not SOURCE.is_file():
        raise FileNotFoundError(SOURCE)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(SOURCE))
    slime = next(obj for obj in bpy.data.objects if obj.type == "MESH")
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start = 1
    scene.frame_end = 25
    slime.rotation_mode = "XYZ"
    slime.animation_data_create()
    for name, (duration, poses) in MOTIONS.items():
        action = bpy.data.actions.new(name)
        slime.animation_data.action = action
        for frame in range(1, round(duration * FPS) + 2):
            scale, pitch = pose_at(poses, (frame - 1) / FPS)
            slime.scale = scale
            slime.rotation_euler = (pitch, 0.0, 0.0)
            slime.keyframe_insert(data_path="scale", frame=frame)
            slime.keyframe_insert(data_path="rotation_euler", frame=frame)
        # NLA に紐付けて全アクションを .blend と glTF に保持する。
        track = slime.animation_data.nla_tracks.new()
        track.name = name
        strip = track.strips.new(name, 1, action)
        track.mute = True
        strip.mute = True
    slime.animation_data.action = bpy.data.actions.get("Idle")
    scene.frame_set(1)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))


def export_clips():
    meshes = [obj for obj in bpy.data.objects if obj.type == "MESH"]
    if len(meshes) != 1:
        raise RuntimeError(f"炎スライムのメッシュは1個を想定: {len(meshes)}")
    slime = meshes[0]
    scene = bpy.context.scene
    fps = scene.render.fps / scene.render.fps_base
    if fps <= 0 or not slime.animation_data:
        raise RuntimeError("FPS またはメッシュのアニメーション設定が不正です")
    CLIP_DIRECTORY.mkdir(parents=True, exist_ok=True)
    for name in MOTIONS:
        action = bpy.data.actions.get(name)
        if not action:
            raise RuntimeError(f"アクション {name} がありません")
        slime.animation_data.action = action
        first, last = (round(frame) for frame in action.frame_range)
        if last <= first:
            raise RuntimeError(f"アクション {name} の長さが不正です")
        keys = []
        for frame in range(first, last + 1):
            scene.frame_set(frame)
            if any(abs(axis) > 0.0001 for axis in slime.location) or \
                    abs(slime.rotation_euler.y) > 0.0001 or abs(slime.rotation_euler.z) > 0.0001:
                raise RuntimeError(f"{name} frame {frame}: 位置移動と Y/Z 軸回転は未対応です")
            keys.append({"time": round((frame - first) / fps, 6),
                         "translate": [0.0, 0.0, 0.0],
                         "rotate": [round(slime.rotation_euler.x, 6), 0.0, 0.0],
                         "scale": [round(slime.scale.x, 6), round(slime.scale.z, 6), round(slime.scale.y, 6)],
                         "easingToNext": 0})
        path = CLIP_DIRECTORY / f"enemy_fire_slime_{name.lower()}.json"
        path.write_text(json.dumps({"version": 2, "name": path.stem, "model": "Characters/slime_red",
                                    "duration": round((last - first) / fps, 6), "loop": name in ("Idle", "Move"),
                                    "bodyKeys": keys}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"CG2_BODY_CLIP={path}")
    slime.animation_data.action = None
    for track in slime.animation_data.nla_tracks:
        track.mute = False
        for strip in track.strips:
            strip.mute = False
    scene.frame_set(1)
    bpy.ops.export_scene.gltf(filepath=str(GLTF), export_format="GLTF_SEPARATE", export_animations=True,
                              export_animation_mode="NLA_TRACKS", export_bake_animation=True, export_frame_step=1,
                              export_materials="EXPORT", export_image_format="AUTO")
    print(f"CG2_ANIMATION_MODEL={GLTF}")


def main():
    DESTINATION.mkdir(parents=True, exist_ok=True)
    if "--create-sample" in sys.argv:
        create_sample()
    else:
        if not BLEND.is_file():
            raise FileNotFoundError(f"{BLEND} がありません。初回は -- --create-sample を指定してください")
        bpy.ops.wm.open_mainfile(filepath=str(BLEND))
    export_clips()


if __name__ == "__main__":
    main()
