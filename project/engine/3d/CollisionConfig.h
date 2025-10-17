#pragma once
#include <cstdint>

// 衝突判定属性 (ビットフラグで管理)
enum CollisionAttribute : uint32_t {
    kPlayer = 1 << 0,  // プレイヤー
    kEnemy = 1 << 1,  // 敵
    kGround = 1 << 2,  // 通常の地形
};

// 地形属性をまとめたマスク (押し戻し処理の対象をまとめるのに便利)
const uint32_t kAllGround = kGround; 