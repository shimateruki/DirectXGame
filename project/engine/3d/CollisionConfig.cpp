#include "engine/3d/CollisionConfig.h"
#include "engine/base/Math.h"
#include <algorithm> // std::min, std::max
#include <cmath>     // std::abs, std::sqrt

// --- ヘルパー関数群 (実装) ---

// [1] AABB vs AABB 判定 (★★★ 再度、正しいロジックに修正 ★★★)
CollisionInfo CheckAABBCollision(const AABB& a, const AABB& b) {
    CollisionInfo info;
    info.isColliding = false; // 初期化

    // 各軸の中心座標
    Vector3 centerA = (a.min + a.max) * 0.5f;
    Vector3 centerB = (b.min + b.max) * 0.5f;

    // 各軸の(半分の)サイズ
    Vector3 sizeA = (a.max - a.min) * 0.5f;
    Vector3 sizeB = (b.max - b.min) * 0.5f; // 正しい計算

    // 中心間の距離ベクトル
    Vector3 distanceVec = centerA - centerB;

    // 各軸でのめり込み量を計算
    // (サイズの合計) - (中心間距離の絶対値) が正なら重なっている
    float overlapX = (sizeA.x + sizeB.x) - std::abs(distanceVec.x);
    float overlapY = (sizeA.y + sizeB.y) - std::abs(distanceVec.y);
    float overlapZ = (sizeA.z + sizeB.z) - std::abs(distanceVec.z);

    // 1つでも重なっていない軸があれば衝突していない
    if (overlapX < 0.0f || overlapY < 0.0f || overlapZ < 0.0f) {
        return info;
    }

    // 衝突している
    info.isColliding = true;

    // めり込み量が最小の軸を特定 (これが衝突した軸)
    if (overlapY < overlapX && overlapY < overlapZ) {
        // --- Y軸 (上下) が最小 ---
        info.penetration = overlapY;
        // A(Player)がB(Block)より上なら、上(+Y)方向の法線
        info.normal = (distanceVec.y > 0.0f) ? Vector3{ 0, 1, 0 } : Vector3{ 0, -1, 0 };
    } else if (overlapX < overlapZ) {
        // --- X軸 (左右) が最小 ---
        info.penetration = overlapX;
        // A(Player)がB(Block)より右(+X)なら、右(+X)方向の法線
        info.normal = (distanceVec.x > 0.0f) ? Vector3{ 1, 0, 0 } : Vector3{ -1, 0, 0 };
    } else {
        // --- Z軸 (前後) が最小 ---
        info.penetration = overlapZ;
        // A(Player)がB(Block)より手前(+Z)なら、手前(+Z)方向の法線
        info.normal = (distanceVec.z > 0.0f) ? Vector3{ 0, 0, 1 } : Vector3{ 0, 0, -1 };
    }

    return info;
}


// [2] Sphere vs Sphere 判定 (変更なし)
CollisionInfo CheckSphereCollision(
    const Vector3& posA, float rA, const Vector3& posB, float rB) {
    // (以前のコードのまま)
    CollisionInfo info;
    Math math;
    Vector3 vecAtoB = posB - posA;
    float distance = math.Length(vecAtoB);
    float totalRadius = rA + rB;
    float penetration = totalRadius - distance;

    if (penetration > 0) {
        info.isColliding = true;
        info.penetration = penetration;
        if (distance > 0.001f) {
            info.normal = (vecAtoB / distance) * -1.0f;
        } else {
            info.normal = { 1.0f, 0, 0 };
        }
    } else {
        info.isColliding = false;
    }
    return info;
}

// [3] Sphere vs AABB 判定 (変更なし)
CollisionInfo CheckSphereAABBCollision(
    const Vector3& spherePos, float sphereRadius, const AABB& aabb) {
    // (以前のコードのまま)
    CollisionInfo info;
    Math math;

    Vector3 closestPoint = {
        math.Clamp(spherePos.x, aabb.min.x, aabb.max.x),
        math.Clamp(spherePos.y, aabb.min.y, aabb.max.y),
        math.Clamp(spherePos.z, aabb.min.z, aabb.max.z)
    };
    float distanceSq = math.Length(spherePos - closestPoint); // LengthSqを使用

    if (distanceSq < (sphereRadius * sphereRadius)) {
        info.isColliding = true;
        if (distanceSq > 0.001f) {
            float distance = std::sqrt(distanceSq); // sqrtが必要
            info.penetration = sphereRadius - distance;
            info.normal = (spherePos - closestPoint) / distance;
        } else {
            Vector3 aabbCenter = (aabb.min + aabb.max) * 0.5f;
            Vector3 vecToCenter = aabbCenter - spherePos;
            if (math.Length(vecToCenter) < 0.001f) {
                info.normal = { 1, 0, 0 };
            } else {
                info.normal = math.Normalize(vecToCenter) * -1.0f;
            }
            info.penetration = sphereRadius;
        }
    } else {
        info.isColliding = false;
    }
    return info;
}