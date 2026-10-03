#!/usr/bin/env python3
"""Stage 2 の火山城塞ルート用モデルとシーンを再構築する。

通常実行では専用OBJ/MTL、Stage 2 のシーン、プレイヤー、カメラを更新する。
``--models-only`` を指定した場合はシーンJSONを変更せず、指定モデルだけを生成できる。
``--scene-only`` を指定した場合はモデルを再生成せず、シーンJSONだけを更新する。
同じ入力から同じGUIDを生成するため、何度実行しても差分が増えない。
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import uuid
from pathlib import Path
from typing import Iterable, Sequence


# 直接実行とパッケージ経由の実行の両方で同じ処理を使用します。
if __package__:
    from .magma_stage.common import (
        CAMERA_PATH,
        GUID_NAMESPACE,
        MAGMA_BARRIER_COUNT,
        MAGMA_BOSS_EVENT_ID,
        MAGMA_DEFEAT_EVENT_ID,
        MODEL_ROOT,
        ObjBuilder,
        PLAYER_PATH,
        PROJECT_ROOT,
        RESOURCE_ROOT,
        ROCK_MATERIAL,
        SCENE_PATH,
        Vec2,
        Vec3,
        add_annulus,
        add_bottom_fan,
        add_box,
        add_cylinder,
        add_cylinder_x,
        add_normalized_top_fan,
        add_oriented_box,
        add_surface_ribbon,
        add_tapered_fortress_base,
        add_top_fan,
        chamfered_rect_ring,
        connect_rings,
        cross,
        dot,
        ensure_asset_meta,
        normalize,
        ring_points,
        rotate_y,
        sub,
    )
    from .magma_stage.models import (
        MODEL_BUILDERS,
        build_basalt_causeway,
        build_caldera_arch,
        build_forge_temple,
        build_fortress_gate,
        build_fortress_grate_drum,
        build_fortress_pillar,
        build_fortress_plaza,
        build_fortress_ramp,
        build_magma_fortress_foundation,
        build_magma_horizon_island,
        build_magma_horizon_spire,
        build_magma_vent,
        build_metal_grate_platform,
        build_obsidian_raft,
        build_volcanic_backdrop_ridge,
        build_volcanic_island,
        build_volcanic_spire,
    )
    from .magma_stage.scene import (
        SceneBuilder,
        add_coin_curve,
        build_scene,
        quaternion_from_euler,
        set_collider,
        set_gameplay_link,
        set_transform,
        update_player_and_camera,
        validate_generated_models,
    )
else:
    from magma_stage.common import (
        CAMERA_PATH,
        GUID_NAMESPACE,
        MAGMA_BARRIER_COUNT,
        MAGMA_BOSS_EVENT_ID,
        MAGMA_DEFEAT_EVENT_ID,
        MODEL_ROOT,
        ObjBuilder,
        PLAYER_PATH,
        PROJECT_ROOT,
        RESOURCE_ROOT,
        ROCK_MATERIAL,
        SCENE_PATH,
        Vec2,
        Vec3,
        add_annulus,
        add_bottom_fan,
        add_box,
        add_cylinder,
        add_cylinder_x,
        add_normalized_top_fan,
        add_oriented_box,
        add_surface_ribbon,
        add_tapered_fortress_base,
        add_top_fan,
        chamfered_rect_ring,
        connect_rings,
        cross,
        dot,
        ensure_asset_meta,
        normalize,
        ring_points,
        rotate_y,
        sub,
    )
    from magma_stage.models import (
        MODEL_BUILDERS,
        build_basalt_causeway,
        build_caldera_arch,
        build_forge_temple,
        build_fortress_gate,
        build_fortress_grate_drum,
        build_fortress_pillar,
        build_fortress_plaza,
        build_fortress_ramp,
        build_magma_fortress_foundation,
        build_magma_horizon_island,
        build_magma_horizon_spire,
        build_magma_vent,
        build_metal_grate_platform,
        build_obsidian_raft,
        build_volcanic_backdrop_ridge,
        build_volcanic_island,
        build_volcanic_spire,
    )
    from magma_stage.scene import (
        SceneBuilder,
        add_coin_curve,
        build_scene,
        quaternion_from_euler,
        set_collider,
        set_gameplay_link,
        set_transform,
        update_player_and_camera,
        validate_generated_models,
    )

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Stage 2 火山城塞アセット生成")
    mode_group = parser.add_mutually_exclusive_group()
    mode_group.add_argument(
        "--models-only",
        action="store_true",
        help="シーン、プレイヤー、カメラを変更せずモデルだけを生成する",
    )
    mode_group.add_argument(
        "--scene-only",
        action="store_true",
        help="モデル、プレイヤー、カメラを変更せずシーンだけを生成する",
    )
    parser.add_argument(
        "--model",
        action="append",
        choices=sorted(MODEL_BUILDERS),
        help="生成するモデル名。複数指定可能。省略時は全モデルを生成する",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    selected_names = args.model or list(MODEL_BUILDERS)
    if not args.scene_only:
        for name in selected_names:
            MODEL_BUILDERS[name]().write(MODEL_ROOT / name)
        validate_generated_models(selected_names)

    if args.scene_only:
        build_scene()
        print("Stage 2 のシーンを更新しました。")
    elif not args.models_only:
        build_scene()
        update_player_and_camera()
        print("Stage 2 のモデル、シーン、プレイヤー、カメラを更新しました。")
    else:
        print(f"Stage 2 モデルを生成しました: {', '.join(selected_names)}")


if __name__ == "__main__":
    main()
