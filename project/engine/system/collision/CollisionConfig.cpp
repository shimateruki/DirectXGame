#include "CollisionConfig.h"
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cmath>
#include <limits>

// [1] AABB vs AABB 判定
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


// [2] Sphere vs Sphere 判定 
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

// [3] Sphere vs AABB 判定 
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


CollisionFace GetCollisionFace(const Vector3& normal, float threshold) {
    // Y軸 (上下) の判定を優先
    if (normal.y > threshold) { return CollisionFace::kTop; }
    if (normal.y < -threshold) { return CollisionFace::kBottom; }

    // X軸 (左右) の判定
    if (normal.x > threshold) { return CollisionFace::kRight; }
    if (normal.x < -threshold) { return CollisionFace::kLeft; }

    // Z軸 (前後) の判定
    if (normal.z > threshold) { return CollisionFace::kFront; }
    if (normal.z < -threshold) { return CollisionFace::kBack; }

    // どの軸にも強く当たっていない場合は「斜め」
    return CollisionFace::kOther;
}

// ------------------------------------------------------------
//  Sphere vs OBB
// ------------------------------------------------------------
CollisionInfo CheckSphereOBBCollision(const Vector3& spherePos, float sphereRadius, const OBB& obb) {
    CollisionInfo info = { false, {0,0,0}, 0.0f };

    // 球の中心をOBBのローカル空間に変換するイメージで、
    // OBB上の「球に一番近い点」を探す
    Vector3 centerToSphere = spherePos - obb.center;
    Vector3 closestPoint = obb.center;

    Math math; // Mathクラスのインスタンス (Dot計算用)

    // 各軸(X, Y, Z)について投影してクランプ
    for (int i = 0; i < 3; ++i) {
        // 距離を軸に投影
        float dist = math.Dot(centerToSphere, obb.orientations[i]);

        // OBBのサイズ(半サイズ)で制限
        float clamped = std::clamp(dist, -obb.size.x, obb.size.x);
        // ※ size.x, y, z を配列的にアクセスできない場合は以下のように分岐するか、sizeを配列にする
        if (i == 1) clamped = std::clamp(dist, -obb.size.y, obb.size.y);
        if (i == 2) clamped = std::clamp(dist, -obb.size.z, obb.size.z);

        // 最短点を加算
        closestPoint += obb.orientations[i] * clamped;
    }

    // 最短点と球の中心との距離をチェック
    Vector3 diff = spherePos - closestPoint;
    float distanceSq = math.Dot(diff, diff);

    if (distanceSq <= (sphereRadius * sphereRadius)) {
        info.isColliding = true;

        float distance = std::sqrt(distanceSq);

        // めり込み量と法線
        if (distance > 0.0001f) {
            info.penetration = sphereRadius - distance;
            info.normal = math.Normalize(diff);
        } else {
            // 中心が完全に埋まっている場合
            info.penetration = sphereRadius;
            info.normal = obb.orientations[1]; // とりあえずY軸などで代用
        }
    }

    return info;
}

// -----------------------------------------------------------------
// OBB vs OBB（改良版） - 標準的な回転行列 + absR を使った SAT 実装
// -----------------------------------------------------------------
CollisionInfo CheckOBBCollision(const OBB& a, const OBB& b) {
    CollisionInfo info = { false, {0,0,0}, 0.0f };
    Math math;

    // 半サイズ（既に obb.size は半サイズである前提）
    const float aH[3] = { a.size.x, a.size.y, a.size.z };
    const float bH[3] = { b.size.x, b.size.y, b.size.z };

    // 回転行列 R[i][j] = Ai dot Bj
    float R[3][3];
    float absR[3][3];
    const float EPS = 1e-6f;

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R[i][j] = math.Dot(a.orientations[i], b.orientations[j]);
            absR[i][j] = std::fabs(R[i][j]) + EPS; // 数値安定化
        }
    }

    // T = b.center - a.center 表示（Aの座標系での成分 tA）
    Vector3 tVec = b.center - a.center;
    float tA[3] = {
        math.Dot(tVec, a.orientations[0]),
        math.Dot(tVec, a.orientations[1]),
        math.Dot(tVec, a.orientations[2])
    };

    // 最小の重なり（押し戻し量）を探すための記録
    float minOverlap = std::numeric_limits<float>::infinity();
    Vector3 minAxis = { 0,0,0 };
    bool foundSeparating = false;

    // --- 1) Aの3軸をテスト ---
    for (int i = 0; i < 3; ++i) {
        float ra = aH[i];
        float rb = bH[0] * absR[i][0] + bH[1] * absR[i][1] + bH[2] * absR[i][2];
        float dist = std::fabs(tA[i]);
        float overlap = (ra + rb) - dist;
        if (overlap <= 0.0f) {
            return info; // 分離軸あり -> 衝突なし
        }
        if (overlap < minOverlap) {
            minOverlap = overlap;
            minAxis = a.orientations[i];
            // 方向を a->b に合わせる
            if (math.Dot(b.center - a.center, minAxis) < 0.0f) minAxis = minAxis * -1.0f;
        }
    }

    // --- 2) Bの3軸をテスト ---
    for (int j = 0; j < 3; ++j) {
        float ra = aH[0] * absR[0][j] + aH[1] * absR[1][j] + aH[2] * absR[2][j];
        float rb = bH[j];
        // t in B frame = dot(T, Bj) = sum_k tA[k] * R[k][j]
        float tB = std::fabs(tA[0] * R[0][j] + tA[1] * R[1][j] + tA[2] * R[2][j]);
        float overlap = (ra + rb) - tB;
        if (overlap <= 0.0f) {
            return info;
        }
        if (overlap < minOverlap) {
            minOverlap = overlap;
            minAxis = b.orientations[j];
            if (math.Dot(b.center - a.center, minAxis) < 0.0f) minAxis = minAxis * -1.0f;
        }
    }

    // --- 3) 9つの外積軸 (Ai x Bj) をテスト ---
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            // axis = Ai x Bj
            Vector3 axis = math.Cross(a.orientations[i], b.orientations[j]);
            float axisLen = math.Length(axis);
            if (axisLen < 1e-6f) {
                continue; // 平行に近い -> スキップ（既に A/B 軸で判定済）
            }
            axis = axis / axisLen; // 正規化

            // ra, rb の計算（公式）
            int i1 = (i + 1) % 3;
            int i2 = (i + 2) % 3;
            int j1 = (j + 1) % 3;
            int j2 = (j + 2) % 3;

            float ra = aH[i1] * absR[i2][j] + aH[i2] * absR[i1][j];
            float rb = bH[j1] * absR[i][j2] + bH[j2] * absR[i][j1];

            // 投影距離 t = | tA[i2]*R[i1][j] - tA[i1]*R[i2][j] |
            float proj = std::fabs(tA[i2] * R[i1][j] - tA[i1] * R[i2][j]);

            float overlap = (ra + rb) - proj;
            if (overlap <= 0.0f) {
                return info;
            }
            if (overlap < minOverlap) {
                minOverlap = overlap;
                // 法線は cross(Ai, Bj) の向き（a->b に合わせる）
                minAxis = axis;
                if (math.Dot(b.center - a.center, minAxis) < 0.0f) minAxis = minAxis * -1.0f;
            }
        }
    }

    // ここまで来たら衝突
    info.isColliding = true;
    info.penetration = minOverlap;
    info.normal = minAxis; // 既に a->b 方向に合わせている
    return info;
}

// ========================================================================
// AABB vs OBB 衝突判定
// ========================================================================
CollisionInfo CheckAABBOBBCollision(const AABB& a, const OBB& b) {
    OBB obbA;
    // AABBの中心とサイズを算出
    obbA.center = (a.min + a.max) * 0.5f;
    obbA.size = (a.max - a.min) * 0.5f;

    // AABBなので回転は単位行列（XYZ軸そのまま）
    obbA.orientations[0] = { 1.0f, 0.0f, 0.0f };
    obbA.orientations[1] = { 0.0f, 1.0f, 0.0f };
    obbA.orientations[2] = { 0.0f, 0.0f, 1.0f };

    // OBB同士として判定
    return CheckOBBCollision(obbA, b);
}