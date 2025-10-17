#pragma once
#include <cstdint>

// 衝突判定属性 (ビットフラグで管理)
enum CollisionAttribute : uint32_t {
    kPlayer = 1 << 0,  // プレイヤー
    kEnemy = 1 << 1,  // 敵
    kGround = 1 << 2,  // 地面や壁
    // 必要に応じて追加していく
};