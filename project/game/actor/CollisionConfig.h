#pragma once
#include <cstdint>
#include "engine/utility/math/Math.h"

// 衝突判定属性 (ビットフラグで管理)
enum CollisionAttribute : uint32_t {
    kPlayer = 1 << 0,  // プレイヤー
    kEnemy = 1 << 1,  // 敵
    kGround = 1 << 2,  // 通常の地形
    kAttributePlayerBullet = 1 << 3,
    kHookAnchor = 1 << 8,
	kTrigger = 1 << 4, // トリガー 
	kMapBlock = 1 << 5, // ボスが吸収可能なブロック
    kPlayerAttack = 1 << 6, // プレイヤーの近接攻撃（剣など）
    kEnemyAttack = 1 << 7, // 敵の近接攻撃
};



// 地形属性をまとめたマスク 
const uint32_t kAllGround = kGround;

// 押し出し処理を適応させるやつを含ませる
const uint32_t kAllSolid = kGround;

/// <summary>
/// コライダー（あたり判定）の形状タイプ
/// </summary>
enum class ColliderType {
    kNone,   // 当たり判定なし
    kSphere, // 球
    kAABB,   // AABB（回転しない箱）
    kOBB,
    kCylinder,
    kRing    // リング（ドーナツ型・衝撃波用）
};

// AABB構造体
struct AABB {
    Vector3 min; // 箱の最小座標
    Vector3 max; // 箱の最大座標
};

// OBB構造体 (中心、各軸の向き、サイズ)
struct OBB {
    Vector3 center;          // 中心点
    Vector3 orientations[3]; // 座標軸 (正規化された X, Y, Z 軸)
    Vector3 size;            // 中心からの半サイズ (width/2, height/2, depth/2)
};

// リング構造体 (中心、向き、内径、外径)
struct Ring {
    Vector3 center;
    Vector3 normal = { 0, 1, 0 }; // リングが乗っている面の法線
    float innerRadius;
    float outerRadius;
    float height; // 厚み (中心から上下に height/2)
};

// 円柱構造体
struct Cylinder {
    Vector3 center;
    Vector3 axis = { 0, 1, 0 }; // 軸方向 (正規化)
    float radius;
    float height; // 全長 (中心から上下に height/2)
};

// 衝突情報（結果）を格納する構造体
struct CollisionInfo {
    bool isColliding = false;      // 衝突しているか
    Vector3 normal = { 0,0,0 };   // 衝突法線 (押し戻す方向)
    float penetration = 0.0f;    // めり込み量
};
/// <summary>
/// 衝突面（法線）がどの方向を向いているかを示す
/// </summary>
enum class CollisionFace {
    kTop,    // 上面 (Y+)
    kBottom, // 底面 (Y-)
    kRight,  // 右面 (X+)
    kLeft,   // 左面 (X-)
    kFront,  // 正面 (Z+)
    kBack,   // 背面 (Z-)
    kOther   // 斜め
};

/// <summary>
/// 衝突法線ベクトルから、最も近い衝突面 (CollisionFace) を判定する
/// </summary>
/// <param name="normal">衝突法線 (正規化されていること)</param>
/// <param name="threshold">「斜め」と判断する閾値</param>
/// <returns>CollisionFace</returns>
CollisionFace GetCollisionFace(const Vector3& normal, float threshold = 0.8f);

// --- 衝突判定ヘルパー関数群 (宣言) ---
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
