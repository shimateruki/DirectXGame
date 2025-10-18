#pragma once
#include <cstdint>
#include "engine/base/Math.h" // Vector3を使うためにインクルード

// 衝突判定属性 (ビットフラグで管理)
enum CollisionAttribute : uint32_t {
    kPlayer = 1 << 0,  // プレイヤー
    kEnemy = 1 << 1,  // 敵
    kGround = 1 << 2,  // 通常の地形
    // 例: kIceGround   = 1 << 3,
};

// 地形属性をまとめたマスク (押し戻し処理の対象をまとめるのに便利)
const uint32_t kAllGround = kGround; // | kIceGround;

/// <summary>
/// コライダー（あたり判定）の形状タイプ
/// </summary>
enum class ColliderType {
    kNone,   // 当たり判定なし
    kSphere, // 球
    kAABB,   // AABB（回転しない箱）
};

// AABB構造体
struct AABB {
    Vector3 min; // 箱の最小座標
    Vector3 max; // 箱の最大座標
};

// 衝突情報（結果）を格納する構造体
struct CollisionInfo {
    bool isColliding = false;      // 衝突しているか
    Vector3 normal = { 0,0,0 };   // 衝突法線 (押し戻す方向)
    float penetration = 0.0f;    // めり込み量
};

// --- 衝突判定ヘルパー関数群 (宣言) ---
CollisionInfo CheckAABBCollision(const AABB& a, const AABB& b);
CollisionInfo CheckSphereCollision(const Vector3& posA, float rA, const Vector3& posB, float rB);
CollisionInfo CheckSphereAABBCollision(const Vector3& spherePos, float sphereRadius, const AABB& aabb);