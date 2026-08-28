"""タイトル／ステージセレクト共有背景の遠景だけを安全に更新します。

現在の stageSelect_object.json にはEditor上の手調整が含まれるため、Scene全体は
再生成しません。このスクリプトは水面と StageSelect_Horizon_ 接頭辞の装飾だけを
所有し、既存のゲート・スター・地形・当たり判定を保持します。
"""

from __future__ import annotations

import argparse
import copy
import json
import math
from pathlib import Path

import generate_stage_select_hub as hub


PROJECT_ROOT = Path(__file__).resolve().parents[2]
SCENE_PATH = PROJECT_ROOT / "Resources" / "json" / "3Dobject" / "stageSelect_object.json"
PREVIEW_PATH = PROJECT_ROOT / ".codex" / "stage_select_horizon_layout.svg"

GROUP_NAME = "[Group] Horizon Scenery"
OWNED_PREFIX = "StageSelect_Horizon_"
HORIZON_MODEL_NAME = "stage_select_horizon_islet"
HORIZON_MODEL_PATH = f"Stages/{HORIZON_MODEL_NAME}"


# index, x, z, scale_x, scale_z, yaw, palm_count
ISLAND_LAYOUTS = (
    (0, -72.0, -42.0, 1.15, 0.82, 0.18, 2),
    (1, -82.0, 3.0, 1.05, 0.78, -0.52, 1),
    (2, -66.0, 56.0, 1.15, 0.84, 0.43, 3),
    (3, -18.0, 72.0, 0.95, 0.70, -0.24, 2),
    (4, 64.0, 56.0, 1.18, 0.86, 0.58, 3),
    (5, 82.0, 3.0, 1.06, 0.78, -0.14, 1),
    (6, 68.0, -58.0, 1.16, 0.86, -0.48, 3),
    (7, -18.0, -78.0, 1.00, 0.74, 0.28, 1),
    (8, -116.0, -18.0, 1.24, 0.86, -0.34, 3),
    (9, -101.0, 86.0, 0.92, 0.68, 0.62, 1),
    (10, -42.0, 116.0, 1.28, 0.88, -0.18, 2),
    (11, 33.0, 121.0, 0.88, 0.66, 0.46, 1),
    (12, 104.0, 91.0, 1.22, 0.82, -0.56, 3),
    (13, 121.0, -18.0, 0.98, 0.72, 0.21, 1),
    (14, 91.0, -104.0, 1.30, 0.90, 0.48, 2),
    (15, -8.0, -124.0, 0.90, 0.65, -0.38, 1),
    (16, -96.0, -102.0, 1.14, 0.80, 0.15, 2),
)

# x, z, model, scale, yaw
ROCK_LAYOUTS = (
    (-66.0, -58.0, "Stages/rock3", 1.60, 0.35),
    (-88.0, 25.0, "Stages/rock5", 1.45, -0.42),
    (-44.0, 72.0, "Stages/rock1", 1.70, 0.66),
    (32.0, 76.0, "Stages/rock4", 1.55, -0.31),
    (84.0, 34.0, "Stages/rock3", 1.75, 0.52),
    (82.0, -38.0, "Stages/rock5", 1.50, -0.72),
    (36.0, -82.0, "Stages/rock1", 1.65, 0.18),
    (-56.0, -76.0, "Stages/rock4", 1.40, 0.48),
    (-126.0, 34.0, "Stages/rock2", 1.65, -0.18),
    (-116.0, 68.0, "Stages/rock5", 1.30, 0.72),
    (-71.0, 111.0, "Stages/rock3", 1.55, -0.44),
    (4.0, 132.0, "Stages/rock1", 1.45, 0.22),
    (72.0, 116.0, "Stages/rock4", 1.72, -0.62),
    (128.0, 53.0, "Stages/rock2", 1.36, 0.38),
    (132.0, -59.0, "Stages/rock5", 1.62, -0.28),
    (54.0, -126.0, "Stages/rock3", 1.48, 0.58),
    (-48.0, -132.0, "Stages/rock1", 1.70, -0.16),
    (-126.0, -66.0, "Stages/rock4", 1.42, 0.46),
)

# x, z, model, scale, yaw, color, emissive
LANDMARK_LAYOUTS = (
    (-115.0, -17.0, "Stages/galaxy_beacon", 0.82, -0.34, (0.74, 0.95, 1.0, 1.0), 1.15),
    (-41.0, 117.0, "Stages/star_observatory", 0.86, -0.18, (0.88, 0.92, 1.0, 1.0), 0.92),
    (103.0, 91.0, "Stages/galaxy_beacon", 0.74, -0.56, (1.0, 0.88, 0.48, 1.0), 1.22),
    (90.0, -103.0, "Stages/star_observatory", 0.80, 0.48, (0.82, 0.94, 1.0, 1.0), 0.88),
    (-98.4, -100.8, "Stages/star_ruin_pillar", 1.08, 0.18, (0.78, 0.90, 1.0, 1.0), 0.70),
    (-94.8, -102.6, "Stages/star_ruin_pillar", 0.82, -0.24, (0.72, 0.88, 1.0, 1.0), 0.62),
)

PALM_OFFSETS = (
    (-2.8, 0.9, 1.08, -0.24),
    (2.4, -0.8, 0.94, 0.38),
    (0.4, 1.8, 0.84, -0.58),
)


def load_scene() -> dict:
    return json.loads(SCENE_PATH.read_text(encoding="utf-8-sig"))


def write_scene(scene: dict) -> None:
    SCENE_PATH.write_text(
        json.dumps(scene, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def update_ocean(ocean: dict) -> None:
    ocean["scale"] = [310.0, 4.97, 280.0]
    ocean["color"] = [0.68, 0.91, 1.0, 1.0]
    ocean["enableEnvMap"] = True
    ocean["envIntensity"] = 0.48
    ocean["roughness"] = 0.26
    ocean["collisionAttribute"] = 0
    ocean["collisionMask"] = 0
    ocean["castShadow"] = False
    ocean["waterParam"] = {
        "billboardScale": 0.50,
        "effectIntensity": 1.08,
        "effectScale": 0.94,
        "effectScaleX": 20.0,
        "effectScaleY": 0.24,
        "effectScaleZ": 1.32,
        "effectSoftness": 0.62,
        "effectType": 0.0,
        "flowSpeedX": 0.018,
        "flowSpeedY": 0.009,
        "waveFrequency": 2.65,
        "waveHeight": 0.25,
        "waveSpeed": 0.74,
    }
    hub.set_parent(ocean, GROUP_NAME)


def make_group(scene: dict) -> dict:
    template = next(
        (item for item in scene["objects"] if item.get("name") == "[Group] Central Decoration"),
        None,
    )
    group = copy.deepcopy(template) if template else hub.make_group_root(GROUP_NAME)
    group["name"] = GROUP_NAME
    group["guid"] = hub.object_guid(GROUP_NAME)
    group["isVisible"] = False
    group["collisionAttribute"] = 0
    group["collisionMask"] = 0
    hub.set_parent(group, "[Group] World")
    return group


def make_island(
    index: int,
    x: float,
    z: float,
    scale_x: float,
    scale_z: float,
    yaw: float,
) -> dict:
    island = hub.base_object(
        f"{OWNED_PREFIX}Island_{index}",
        "Model",
        HORIZON_MODEL_PATH,
        (x, -2.25, z),
        (scale_x, 0.76 + scale_x * 0.08, scale_z),
        yaw,
    )
    island.update(
        {
            "castShadow": False,
            "collisionAttribute": 0,
            "collisionMask": 0,
            "color": [0.76, 0.91, 0.84, 1.0],
            "emissive": 0.42,
            "enableEnvMap": True,
            "envIntensity": 0.34,
            "roughness": 0.78,
        }
    )
    island["collider"]["type"] = 0
    hub.set_parent(island, GROUP_NAME)
    return island


def rotate_offset(offset_x: float, offset_z: float, yaw: float) -> tuple[float, float]:
    cosine = math.cos(yaw)
    sine = math.sin(yaw)
    return (
        offset_x * cosine + offset_z * sine,
        -offset_x * sine + offset_z * cosine,
    )


def make_palms(
    island_index: int,
    x: float,
    z: float,
    scale_x: float,
    scale_z: float,
    yaw: float,
    palm_count: int,
) -> list[dict]:
    palms: list[dict] = []
    for palm_index in range(palm_count):
        offset_x, offset_z, palm_scale, palm_yaw = PALM_OFFSETS[palm_index]
        world_x, world_z = rotate_offset(
            offset_x * scale_x,
            offset_z * scale_z,
            yaw,
        )
        palm = hub.base_object(
            f"{OWNED_PREFIX}Palm_{island_index}_{palm_index}",
            "Model",
            "Stages/tree1",
            (x + world_x, -2.13, z + world_z),
            (palm_scale, palm_scale, palm_scale),
            yaw + palm_yaw,
        )
        palm.update(
            {
                "castShadow": False,
                "collisionAttribute": 0,
                "collisionMask": 0,
                "color": [0.82, 0.94, 0.88, 1.0],
                "emissive": 0.34,
                "envIntensity": 0.22,
                "roughness": 0.88,
            }
        )
        palm["collider"]["type"] = 0
        hub.set_parent(palm, GROUP_NAME)
        palms.append(palm)
    return palms


def make_rocks() -> list[dict]:
    rocks: list[dict] = []
    for index, (x, z, model, scale, yaw) in enumerate(ROCK_LAYOUTS):
        rock = hub.base_object(
            f"{OWNED_PREFIX}Rock_{index}",
            "Model",
            model,
            (x, -4.55, z),
            (scale, scale, scale),
            yaw,
        )
        rock.update(
            {
                "castShadow": False,
                "collisionAttribute": 0,
                "collisionMask": 0,
                "color": [0.70, 0.83, 0.84, 1.0],
                "emissive": 0.26,
                "envIntensity": 0.30,
                "roughness": 0.90,
            }
        )
        rock["collider"]["type"] = 0
        hub.set_parent(rock, GROUP_NAME)
        rocks.append(rock)
    return rocks


def make_landmarks() -> list[dict]:
    landmarks: list[dict] = []
    for index, (x, z, model, scale, yaw, color, emissive) in enumerate(LANDMARK_LAYOUTS):
        landmark = hub.base_object(
            f"{OWNED_PREFIX}Landmark_{index}",
            "Model",
            model,
            (x, -2.10, z),
            (scale, scale, scale),
            yaw,
        )
        landmark.update(
            {
                "castShadow": False,
                "collisionAttribute": 0,
                "collisionMask": 0,
                "color": list(color),
                "emissive": emissive,
                "enableEnvMap": True,
                "envIntensity": 0.42,
                "roughness": 0.62,
            }
        )
        landmark["collider"]["type"] = 0
        hub.set_parent(landmark, GROUP_NAME)
        landmarks.append(landmark)
    return landmarks


def make_horizon_objects() -> list[dict]:
    result: list[dict] = []
    for layout in ISLAND_LAYOUTS:
        index, x, z, scale_x, scale_z, yaw, palm_count = layout
        result.append(make_island(index, x, z, scale_x, scale_z, yaw))
        result.extend(make_palms(index, x, z, scale_x, scale_z, yaw, palm_count))
    result.extend(make_rocks())
    result.extend(make_landmarks())
    return result


def refine_scene(scene: dict) -> dict:
    objects = [
        item
        for item in scene["objects"]
        if item.get("name") != GROUP_NAME
        and not item.get("name", "").startswith(OWNED_PREFIX)
    ]
    scene["objects"] = objects

    ocean = next(
        (item for item in objects if item.get("name") == "StageSelect_Ocean"),
        None,
    )

    if ocean is None:
        raise RuntimeError("StageSelect_Ocean is missing.")

    group = make_group(scene)
    update_ocean(ocean)
    world_index = next(
        (
            index
            for index, item in enumerate(objects)
            if item.get("name") == "[Group] World"
        ),
        -1,
    )
    objects.insert(world_index + 1, group)
    objects.extend(make_horizon_objects())
    return scene


def world_to_preview(x: float, z: float) -> tuple[float, float]:
    return 480.0 + x * 2.5, 480.0 - z * 2.5


def write_preview() -> None:
    shapes: list[str] = []
    for index, x, z, scale_x, scale_z, yaw, palm_count in ISLAND_LAYOUTS:
        preview_x, preview_y = world_to_preview(x, z)
        shapes.append(
            f'<ellipse cx="{preview_x:.1f}" cy="{preview_y:.1f}" '
            f'rx="{27.5 * scale_x:.1f}" ry="{17.5 * scale_z:.1f}" '
            f'transform="rotate({-math.degrees(yaw):.1f} {preview_x:.1f} {preview_y:.1f})" '
            'fill="#7edc8e" stroke="#f2d28b" stroke-width="5"/>'
        )
        shapes.append(
            f'<text x="{preview_x:.1f}" y="{preview_y + 5.0:.1f}" '
            f'text-anchor="middle" class="label">{index + 1}</text>'
        )
        for palm_index in range(palm_count):
            offset_x, offset_z, _, _ = PALM_OFFSETS[palm_index]
            palm_x, palm_z = rotate_offset(
                offset_x * scale_x,
                offset_z * scale_z,
                yaw,
            )
            tree_x, tree_y = world_to_preview(x + palm_x, z + palm_z)
            shapes.append(
                f'<circle cx="{tree_x:.1f}" cy="{tree_y:.1f}" r="4.5" '
                'fill="#167b4b" stroke="#e6ffb5" stroke-width="1.5"/>'
            )
    for x, z, _, scale, _ in ROCK_LAYOUTS:
        preview_x, preview_y = world_to_preview(x, z)
        shapes.append(
            f'<circle cx="{preview_x:.1f}" cy="{preview_y:.1f}" '
            f'r="{scale * 3.0:.1f}" fill="#6f8e99" stroke="#d9f3f2"/>'
        )
    for x, z, _, scale, _, _, _ in LANDMARK_LAYOUTS:
        preview_x, preview_y = world_to_preview(x, z)
        shapes.append(
            f'<rect x="{preview_x - scale * 4.0:.1f}" y="{preview_y - scale * 4.0:.1f}" '
            f'width="{scale * 8.0:.1f}" height="{scale * 8.0:.1f}" '
            'fill="#ffe171" stroke="#ffffff" stroke-width="1.5"/>'
        )

    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="960" height="960">
<style>.title{{font:700 25px sans-serif;fill:#fff}}.label{{font:700 15px sans-serif;fill:#24523b}}</style>
<rect width="960" height="960" fill="#2e86c7"/>
<circle cx="480" cy="480" r="102" fill="#84dd78" stroke="#f6d990" stroke-width="9"/>
{''.join(shapes)}
<text x="480" y="475" text-anchor="middle" class="title">TITLE / STAGE SELECT</text>
</svg>
'''
    PREVIEW_PATH.parent.mkdir(parents=True, exist_ok=True)
    PREVIEW_PATH.write_text(svg, encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Refine the shared title and stage-select horizon."
    )
    parser.add_argument("--preview-only", action="store_true")
    args = parser.parse_args()

    if args.preview_only:
        write_preview()
        print("Updated the horizon layout audit preview.")
        return

    hub.export_model(
        hub.build_island(HORIZON_MODEL_NAME, 11.0, 7.0, 0.72)
    )
    scene = refine_scene(load_scene())
    hub.validate_scene(scene)
    write_scene(scene)
    print("Updated the shared water surface and horizon scenery.")


if __name__ == "__main__":
    main()
