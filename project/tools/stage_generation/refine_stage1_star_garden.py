#!/usr/bin/env python3
"""Stage 1を立体的な庭園遺跡コースへ再構成する。"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Any, Iterable


# 直接実行とパッケージ経由の実行の両方で同じ処理を使用します。
if __package__:
    from .star_garden.common import (
        ANIMATION_ROOT,
        ARENA_CENTER,
        BRIDGE_SPECS,
        CLIFF_CENTER,
        CRITICAL_VISIBLE_SUPPORT_COLLIDERS,
        EAST_RAMPART_CENTER,
        ENTRY_CENTER,
        FINAL_APPROACH_EVENT_ID,
        FINAL_CHOICE_EVENT_ID,
        FINAL_RETURN_EVENT_ID,
        GOAL_CENTER,
        GUID_NAMESPACE,
        INTEGRATED_COLLIDER_VISUAL_GROUPS,
        MAIN_ROUTE_STRIP_SPECS,
        MODEL_ROOT,
        ORCHARD_CENTER,
        PLAYER_START,
        PRISM_BARRIER_COUNT,
        PRISM_BOSS_EVENT_ID,
        PRISM_DEFEAT_EVENT_ID,
        ROOT,
        SAMPLE_OBJECT_PATH,
        SECRET_STAIR_EVENT_ID,
        STAGE1_PATH,
        STAGE1_PLAYER_PATH,
        STAGE2_PATH,
        STAGE3_ENEMY_PATH,
        WATERWORKS_CENTER,
        WATER_SURFACE_Y,
        add_coin_line,
        add_coin_positions,
        add_model_colliders,
        add_visible_support_models,
        build_loop_path_frames,
        clone_entry,
        configure_star,
        critical_visible_support_colliders,
        find_enemy,
        first_gimmick,
        first_model,
        load_json,
        make_appearing_floor,
        make_arena_encounter,
        make_blink_floor,
        make_breakable_block,
        make_breakable_glass,
        make_checkpoint,
        make_coin,
        make_enemy,
        make_falling_spike,
        make_ghost_moving_floor,
        make_giant_rush_gate,
        make_gimmick,
        make_hazard_ride_floor,
        make_invisible_box,
        make_mechanical_floor,
        make_prism_barrier,
        make_prism_seal,
        make_route_strip,
        make_stage_entry_assembly,
        make_switch_floor,
        make_timed_floor_switch,
        make_visual_anchor_tower,
        make_visual_model,
        make_wall_box,
        make_water,
        object_map,
        quaternion_from_euler,
        revise_player,
        set_gameplay_link,
        set_no_collider,
        set_transform,
        stable_guid,
        stage_v2_floor_specs,
        stage_v3_floor_specs,
        stage_v4_appearing_route_specs,
        stage_v4_blink_floor_specs,
        stage_v4_blink_paths,
        stage_v4_final_switch_specs,
        stage_v4_floor_specs,
        visible_support_name,
        write_json,
        write_stage1_v4_ghost_paths,
        yaw_from_xz_direction,
    )
    from .star_garden.generation import (
        revise_stage,
        revise_stage_v2,
        revise_stage_v3,
        revise_stage_v4,
    )
    from .star_garden.geometry import (
        collider_world_box,
        load_obj_top_faces_in_engine_space,
        oriented_box_edge_gap,
        oriented_boxes_overlap,
        point_inside_polygon,
        point_inside_terrain_collider,
        point_inside_world_box,
        point_segment_distance,
        projected_interval,
        validate_bomb_grotto_wall,
        validate_bridge_alignment,
        validate_floor_link,
        validate_integrated_collider_visual_contract,
        validate_main_route_strips,
        validate_mechanical_collider_tops,
        validate_model_reference,
        validate_star_supports,
        validate_supported_guide_coins,
        validate_v3_course_mesh_alignment,
        validate_v3_glass_vault,
        validate_visible_support_models,
    )
    from .star_garden.validation import (
        validate_route_support,
        validate_stage,
        validate_stage_v2,
        validate_stage_v3,
        validate_stage_v4,
        validate_v4_course_mesh_alignment,
    )
else:
    from star_garden.common import (
        ANIMATION_ROOT,
        ARENA_CENTER,
        BRIDGE_SPECS,
        CLIFF_CENTER,
        CRITICAL_VISIBLE_SUPPORT_COLLIDERS,
        EAST_RAMPART_CENTER,
        ENTRY_CENTER,
        FINAL_APPROACH_EVENT_ID,
        FINAL_CHOICE_EVENT_ID,
        FINAL_RETURN_EVENT_ID,
        GOAL_CENTER,
        GUID_NAMESPACE,
        INTEGRATED_COLLIDER_VISUAL_GROUPS,
        MAIN_ROUTE_STRIP_SPECS,
        MODEL_ROOT,
        ORCHARD_CENTER,
        PLAYER_START,
        PRISM_BARRIER_COUNT,
        PRISM_BOSS_EVENT_ID,
        PRISM_DEFEAT_EVENT_ID,
        ROOT,
        SAMPLE_OBJECT_PATH,
        SECRET_STAIR_EVENT_ID,
        STAGE1_PATH,
        STAGE1_PLAYER_PATH,
        STAGE2_PATH,
        STAGE3_ENEMY_PATH,
        WATERWORKS_CENTER,
        WATER_SURFACE_Y,
        add_coin_line,
        add_coin_positions,
        add_model_colliders,
        add_visible_support_models,
        build_loop_path_frames,
        clone_entry,
        configure_star,
        critical_visible_support_colliders,
        find_enemy,
        first_gimmick,
        first_model,
        load_json,
        make_appearing_floor,
        make_arena_encounter,
        make_blink_floor,
        make_breakable_block,
        make_breakable_glass,
        make_checkpoint,
        make_coin,
        make_enemy,
        make_falling_spike,
        make_ghost_moving_floor,
        make_giant_rush_gate,
        make_gimmick,
        make_hazard_ride_floor,
        make_invisible_box,
        make_mechanical_floor,
        make_prism_barrier,
        make_prism_seal,
        make_route_strip,
        make_stage_entry_assembly,
        make_switch_floor,
        make_timed_floor_switch,
        make_visual_anchor_tower,
        make_visual_model,
        make_wall_box,
        make_water,
        object_map,
        quaternion_from_euler,
        revise_player,
        set_gameplay_link,
        set_no_collider,
        set_transform,
        stable_guid,
        stage_v2_floor_specs,
        stage_v3_floor_specs,
        stage_v4_appearing_route_specs,
        stage_v4_blink_floor_specs,
        stage_v4_blink_paths,
        stage_v4_final_switch_specs,
        stage_v4_floor_specs,
        visible_support_name,
        write_json,
        write_stage1_v4_ghost_paths,
        yaw_from_xz_direction,
    )
    from star_garden.generation import (
        revise_stage,
        revise_stage_v2,
        revise_stage_v3,
        revise_stage_v4,
    )
    from star_garden.geometry import (
        collider_world_box,
        load_obj_top_faces_in_engine_space,
        oriented_box_edge_gap,
        oriented_boxes_overlap,
        point_inside_polygon,
        point_inside_terrain_collider,
        point_inside_world_box,
        point_segment_distance,
        projected_interval,
        validate_bomb_grotto_wall,
        validate_bridge_alignment,
        validate_floor_link,
        validate_integrated_collider_visual_contract,
        validate_main_route_strips,
        validate_mechanical_collider_tops,
        validate_model_reference,
        validate_star_supports,
        validate_supported_guide_coins,
        validate_v3_course_mesh_alignment,
        validate_v3_glass_vault,
        validate_visible_support_models,
    )
    from star_garden.validation import (
        validate_route_support,
        validate_stage,
        validate_stage_v2,
        validate_stage_v3,
        validate_stage_v4,
        validate_v4_course_mesh_alignment,
    )

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validate-only", action="store_true", help="現在のJSONを検証するだけで保存しない")
    args = parser.parse_args()

    stage1 = load_json(STAGE1_PATH)
    player_data = load_json(STAGE1_PLAYER_PATH)
    if not args.validate_only:
        stage2 = load_json(STAGE2_PATH)
        stage3_enemy = load_json(STAGE3_ENEMY_PATH)
        sample_objects = load_json(SAMPLE_OBJECT_PATH)
        revise_stage_v4(stage1, stage2, stage3_enemy, sample_objects)
        revise_player(player_data)
        write_stage1_v4_ghost_paths()

    errors = validate_stage_v4(stage1, player_data)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    if not args.validate_only:
        write_json(STAGE1_PATH, stage1)
        write_json(STAGE1_PLAYER_PATH, player_data)
        print(f"Stage 1 V4を再構成しました: {len(stage1['objects'])} objects")
    else:
        print(f"Stage 1 V4検証OK: {len(stage1['objects'])} objects")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
