#pragma once
#include <cstdint>
#include "engine/utility/math/Math.h"

/// Objectが属する衝突カテゴリです。複数カテゴリはビットORで組み合わせます。
/// ゲーム側でカテゴリを追加する場合は、既存ビットと重複しない値を割り当ててください。
enum CollisionAttribute : uint32_t {
    kPlayer = 1 << 0,               // 操作キャラクター。
    kEnemy = 1 << 1,                // 敵キャラクター。
    kGround = 1 << 2,               // 移動を遮る地形。
    kAttributePlayerBullet = 1 << 3,// Playerが生成する飛び道具。
    kTrigger = 1 << 4,              // 押し戻さずイベントだけを通知する領域。
    kMapBlock = 1 << 5,             // ゲーム側で動かせる地形ブロック用の予約属性。
    kPlayerAttack = 1 << 6,         // Player側の攻撃判定。
    kEnemyAttack = 1 << 7,          // Enemy側の攻撃判定。
    kHookAnchor = 1 << 8,           // 接続・移動先として参照するアンカー。
};

/// 接地判定の対象となる属性をまとめたマスクです。
const uint32_t kAllGround = kGround;

/// CharacterMotorが物理的に押し戻す属性です。ソリッドを追加した場合はここへ含めます。
const uint32_t kAllSolid = kGround;

/// Colliderが使用する形状です。
enum class ColliderType {
    kNone,   // 当たり判定なし
    kSphere, // 球
    kAABB,   // AABB（回転しない箱）
    kOBB,    // 回転する箱。
    kCylinder, // 円柱。
    kRing,   // リング（ドーナツ型・衝撃波用）
    kTerrain // 高さ付き地形
};

/// 回転しない箱型の衝突形状です。
struct AABB {
    Vector3 min; // 箱の最小座標
    Vector3 max; // 箱の最大座標
};

/// 中心、向き、半サイズで表す回転可能な箱型衝突形状です。
struct OBB {
    Vector3 center;          // 中心点
    Vector3 orientations[3]; // 座標軸 (正規化された X, Y, Z 軸)
    Vector3 size;            // 中心からの半サイズ (width/2, height/2, depth/2)
};

/// 中心、面法線、内外半径、厚みで表すリング型衝突形状です。
struct Ring {
    Vector3 center;
    Vector3 normal = { 0, 1, 0 }; // リングが乗っている面の法線
    float innerRadius;
    float outerRadius;
    float height; // 厚み (中心から上下に height/2)
};

/// 中心、軸、半径、全長で表す円柱型衝突形状です。
struct Cylinder {
    Vector3 center;
    Vector3 axis = { 0, 1, 0 }; // 軸方向 (正規化)
    float radius;
    float height; // 全長 (中心から上下に height/2)
};

/// 形状判定から返す衝突有無、押し戻し法線、めり込み量です。
struct CollisionInfo {
    bool isColliding = false;      // 衝突しているか
    Vector3 normal = { 0,0,0 };   // 衝突法線 (押し戻す方向)
    float penetration = 0.0f;    // めり込み量
};
/// 衝突法線に最も近い主要方向です。
enum class CollisionFace {
    kTop,    // 上面 (Y+)
    kBottom, // 底面 (Y-)
    kRight,  // 右面 (X+)
    kLeft,   // 左面 (X-)
    kFront,  // 正面 (Z+)
    kBack,   // 背面 (Z-)
    kOther   // 斜め
};

/// 正規化済みの衝突法線から、最も近い主要方向を判定します。
/// <param name="normal">衝突法線 (正規化されていること)</param>
/// <param name="threshold">「斜め」と判断する閾値</param>
/// <returns>CollisionFace</returns>
CollisionFace GetCollisionFace(const Vector3& normal, float threshold = 0.8f);

// 形状ごとの衝突判定。法線は第1引数側を押し戻す方向で返します。
CollisionInfo CheckAABBCollision(const AABB& a, const AABB& b);
CollisionInfo CheckSphereCollision(const Vector3& posA, float rA, const Vector3& posB, float rB);
CollisionInfo CheckSphereAABBCollision(const Vector3& spherePos, float sphereRadius, const AABB& aabb);
CollisionInfo CheckSphereOBBCollision(const Vector3& spherePos, float sphereRadius, const OBB& obb);
CollisionInfo CheckOBBCollision(const OBB& obb1, const OBB& obb2);
/// <summary>
/// AABBとOBBの衝突判定 (AABBをOBBに変換して判定)
/// </summary>
CollisionInfo CheckAABBOBBCollision(const AABB& a, const OBB& b);

// --- リング関連の衝突判定 ---
CollisionInfo CheckRingSphereCollision(const Ring& ring, const Vector3& spherePos, float sphereRadius);
CollisionInfo CheckRingOBBCollision(const Ring& ring, const OBB& obb);

// --- 円柱関連の衝突判定 ---
CollisionInfo CheckCylinderSphereCollision(const Cylinder& cyl, const Vector3& spherePos, float sphereRadius);
CollisionInfo CheckCylinderOBBCollision(const Cylinder& cyl, const OBB& obb);
