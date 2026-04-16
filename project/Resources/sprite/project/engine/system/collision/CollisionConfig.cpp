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

    const float aH[3] = { a.size.x, a.size.y, a.size.z };
    const float bH[3] = { b.size.x, b.size.y, b.size.z };

    float R[3][3];
    float absR[3][3];
    // ★ EPS は削除（外積の計算時にゼロ除算回避が入っているため不要）

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R[i][j] = math.Dot(a.orientations[i], b.orientations[j]);
            // ★ EPSを足さない純粋な絶対値にする（床が膨張するバグの修正）
            absR[i][j] = std::fabs(R[i][j]);
        }
    }

    Vector3 tVec = b.center - a.center;
    float tA[3] = {
        math.Dot(tVec, a.orientations[0]),
        math.Dot(tVec, a.orientations[1]),
        math.Dot(tVec, a.orientations[2])
    };

    float minOverlap = std::numeric_limits<float>::infinity();
    Vector3 minAxis = { 0,0,0 };

    // --- 1) Aの3軸をテスト ---
    for (int i = 0; i < 3; ++i) {
        float ra = aH[i];
        float rb = bH[0] * absR[i][0] + bH[1] * absR[i][1] + bH[2] * absR[i][2];
        float dist = std::fabs(tA[i]);
        float overlap = (ra + rb) - dist;
        if (overlap <= 0.0f) return info;
        if (overlap < minOverlap) {
            minOverlap = overlap;
            minAxis = a.orientations[i];
            if (math.Dot(a.center - b.center, minAxis) < 0.0f) minAxis = minAxis * -1.0f;
        }
    }

    // --- 2) Bの3軸をテスト ---
    for (int j = 0; j < 3; ++j) {
        float ra = aH[0] * absR[0][j] + aH[1] * absR[1][j] + aH[2] * absR[2][j];
        float rb = bH[j];
        float tB = std::fabs(tA[0] * R[0][j] + tA[1] * R[1][j] + tA[2] * R[2][j]);
        float overlap = (ra + rb) - tB;
        if (overlap <= 0.0f) return info;
        if (overlap < minOverlap) {
            minOverlap = overlap;
            minAxis = b.orientations[j];
            if (math.Dot(a.center - b.center, minAxis) < 0.0f) minAxis = minAxis * -1.0f;
        }
    }

    // --- 3) 9つの外積軸 (Ai x Bj) をテスト ---
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            Vector3 axis = math.Cross(a.orientations[i], b.orientations[j]);
            float axisLen = math.Length(axis);
            if (axisLen < 1e-6f) continue; // ここで弾いているので EPS 不要
            axis = axis / axisLen;

            int i1 = (i + 1) % 3;
            int i2 = (i + 2) % 3;
            int j1 = (j + 1) % 3;
            int j2 = (j + 2) % 3;

            float ra = aH[i1] * absR[i2][j] + aH[i2] * absR[i1][j];
            float rb = bH[j1] * absR[i][j2] + bH[j2] * absR[i][j1];

            float proj = std::fabs(tA[i2] * R[i1][j] - tA[i1] * R[i2][j]);
            float overlap = (ra + rb) - proj;

            if (overlap <= 0.0f) return info;

            overlap /= axisLen; // 実際の長さに補正

            if (overlap < minOverlap) {
                minOverlap = overlap;
                minAxis = axis;
                if (math.Dot(a.center - b.center, minAxis) < 0.0f) minAxis = minAxis * -1.0f;
            }
        }
    }

    info.isColliding = true;
    info.penetration = minOverlap;
    info.normal = minAxis;
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

CollisionInfo CheckSphereCylinderCollision(const Vector3& sphereCenter, float sphereRadius, const Cylinder& cylinder) {
    CollisionInfo info;
    info.isColliding = false;

    // 円柱の中心から球の中心へのベクトル
    Vector3 d = sphereCenter - cylinder.center;

    // Y軸（高さ）方向の距離と、XZ平面（水平）での距離
    float distY = std::abs(d.y);
    float distXZ = std::sqrt(d.x * d.x + d.z * d.z);

    float halfHeight = cylinder.height * 0.5f;

    // 完全に離れているかチェック
    if (distY > halfHeight + sphereRadius) return info;
    if (distXZ > cylinder.radius + sphereRadius) return info;

    // 角（フチ）の判定
    if (distY > halfHeight && distXZ > cylinder.radius) {
        float cornerDistSq = (distXZ - cylinder.radius) * (distXZ - cylinder.radius) + (distY - halfHeight) * (distY - halfHeight);
        if (cornerDistSq > sphereRadius * sphereRadius) return info;
    }

    info.isColliding = true;

    // 押し出し計算（上下の面か、側面か、浅い方に押し出す）
    float penY = (halfHeight + sphereRadius) - distY;
    float penXZ = (cylinder.radius + sphereRadius) - distXZ;

    if (penY < penXZ && distXZ < cylinder.radius) {
        // 上下に押し出す
        info.penetration = penY;
        info.normal = { 0.0f, (d.y > 0.0f) ? 1.0f : -1.0f, 0.0f };
    }
    else {
        // 横に押し出す
        info.penetration = penXZ;
        if (distXZ > 0.0001f) {
            info.normal = { d.x / distXZ, 0.0f, d.z / distXZ };
        }
        else {
            info.normal = { 1.0f, 0.0f, 0.0f };
        }
    }
    return info;
}

// ========================================================================
// : 円柱 vs 円柱
// ========================================================================
CollisionInfo CheckCylinderCollision(const Cylinder& a, const Cylinder& b) {
    CollisionInfo info;
    info.isColliding = false;

    Vector3 d = a.center - b.center;
    float distY = std::abs(d.y);
    float distXZ = std::sqrt(d.x * d.x + d.z * d.z);

    float sumHalfHeight = (a.height + b.height) * 0.5f;
    float sumRadius = a.radius + b.radius;

    if (distY > sumHalfHeight) return info;
    if (distXZ > sumRadius) return info;

    info.isColliding = true;
    float penY = sumHalfHeight - distY;
    float penXZ = sumRadius - distXZ;

    if (penY < penXZ) {
        info.penetration = penY;
        info.normal = { 0.0f, (d.y > 0.0f) ? 1.0f : -1.0f, 0.0f };
    }
    else {
        info.penetration = penXZ;
        if (distXZ > 0.0001f) {
            info.normal = { d.x / distXZ, 0.0f, d.z / distXZ };
        }
        else {
            info.normal = { 1.0f, 0.0f, 0.0f };
        }
    }
    return info;
}

// ========================================================================
//  AABB vs 円柱 (Cylinder)
// ========================================================================
CollisionInfo CheckAABBCylinderCollision(const AABB& aabb, const Cylinder& cylinder) {
    CollisionInfo info;
    info.isColliding = false;

    // Y軸（高さ）の判定
    float aabbCenterY = (aabb.max.y + aabb.min.y) * 0.5f;
    float aabbHalfHeight = (aabb.max.y - aabb.min.y) * 0.5f;
    float cylHalfHeight = cylinder.height * 0.5f;
    float distY = std::abs(cylinder.center.y - aabbCenterY);
    float overlapY = (aabbHalfHeight + cylHalfHeight) - distY;

    if (overlapY <= 0.0f) return info; // 高さが重なっていない

    // XZ平面の判定 (AABBに対する円柱中心の最近接点を求める)
    float closestX = std::clamp(cylinder.center.x, aabb.min.x, aabb.max.x);
    float closestZ = std::clamp(cylinder.center.z, aabb.min.z, aabb.max.z);

    Vector3 closestPoint = { closestX, cylinder.center.y, closestZ };
    Vector3 diff = cylinder.center - closestPoint;
    diff.y = 0.0f; // XZ平面のみの距離

    float distSq = diff.x * diff.x + diff.z * diff.z;
    if (distSq > cylinder.radius * cylinder.radius) return info; // XZ平面で重なっていない

    info.isColliding = true;

    // 押し出し方向と量の計算
    float overlapXZ = cylinder.radius;
    if (distSq > 0.0001f) {
        float dist = std::sqrt(distSq);
        overlapXZ = cylinder.radius - dist;
        info.normal = { diff.x / dist, 0.0f, diff.z / dist };
    }
    else {
        // 円柱の中心がAABBの中にある場合の例外処理
        float centerDistX = cylinder.center.x - ((aabb.max.x + aabb.min.x) * 0.5f);
        float centerDistZ = cylinder.center.z - ((aabb.max.z + aabb.min.z) * 0.5f);
        float overlapX = ((aabb.max.x - aabb.min.x) * 0.5f) - std::abs(centerDistX);
        float overlapZ = ((aabb.max.z - aabb.min.z) * 0.5f) - std::abs(centerDistZ);

        if (overlapX < overlapZ) {
            overlapXZ = overlapX + cylinder.radius;
            info.normal = { (centerDistX > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f };
        }
        else {
            overlapXZ = overlapZ + cylinder.radius;
            info.normal = { 0.0f, 0.0f, (centerDistZ > 0.0f) ? 1.0f : -1.0f };
        }
    }

    // 上下に押し出すか、横に押し出すかを決定
    if (overlapY < overlapXZ) {
        info.penetration = overlapY;
        info.normal = { 0.0f, (cylinder.center.y > aabbCenterY) ? 1.0f : -1.0f, 0.0f };
    }
    else {
        info.penetration = overlapXZ;
    }

    return info;
}

// ========================================================================
//  OBB vs 円柱 (Cylinder)
// ========================================================================
CollisionInfo CheckOBBCylinderCollision(const OBB& obb, const Cylinder& cylinder) {
    CollisionInfo info;
    info.isColliding = false;

    // 円柱の中心をOBBのローカル空間に変換
    Vector3 diff = cylinder.center - obb.center;
    Math math; // Mathのインスタンスを作成
    Vector3 localCenter;
    localCenter.x = math.Dot(diff, obb.orientations[0]);
    localCenter.y = math.Dot(diff, obb.orientations[1]);
    localCenter.z = math.Dot(diff, obb.orientations[2]);

    // Y軸（高さ）の判定
    float cylHalfHeight = cylinder.height * 0.5f;
    float distY = std::abs(localCenter.y);
    float overlapY = (obb.size.y + cylHalfHeight) - distY;

    if (overlapY <= 0.0f) return info;

    // XZ平面の判定 (OBBローカル内での最近接点)
    float closestX = std::clamp(localCenter.x, -obb.size.x, obb.size.x);
    float closestZ = std::clamp(localCenter.z, -obb.size.z, obb.size.z);

    Vector3 closestLocal = { closestX, localCenter.y, closestZ };
    Vector3 diffLocal = localCenter - closestLocal;
    diffLocal.y = 0.0f;

    float distSq = diffLocal.x * diffLocal.x + diffLocal.z * diffLocal.z;
    if (distSq > cylinder.radius * cylinder.radius) return info;

    info.isColliding = true;

    // 押し出し計算（AABBと同じロジックをローカル空間で実行）
    float overlapXZ = cylinder.radius;
    Vector3 localNormal = { 1.0f, 0.0f, 0.0f };
    if (distSq > 0.0001f) {
        float dist = std::sqrt(distSq);
        overlapXZ = cylinder.radius - dist;
        localNormal = { diffLocal.x / dist, 0.0f, diffLocal.z / dist };
    }
    else {
        float overlapX = obb.size.x - std::abs(localCenter.x);
        float overlapZ = obb.size.z - std::abs(localCenter.z);
        if (overlapX < overlapZ) {
            overlapXZ = overlapX + cylinder.radius;
            localNormal = { (localCenter.x > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f };
        }
        else {
            overlapXZ = overlapZ + cylinder.radius;
            localNormal = { 0.0f, 0.0f, (localCenter.z > 0.0f) ? 1.0f : -1.0f };
        }
    }

    if (overlapY < overlapXZ) {
        info.penetration = overlapY;
        localNormal = { 0.0f, (localCenter.y > 0.0f) ? 1.0f : -1.0f, 0.0f };
    }
    else {
        info.penetration = overlapXZ;
    }

    // ローカルの押し出し方向をワールド方向に変換して返す
    info.normal = {
        obb.orientations[0].x * localNormal.x + obb.orientations[1].x * localNormal.y + obb.orientations[2].x * localNormal.z,
        obb.orientations[0].y * localNormal.x + obb.orientations[1].y * localNormal.y + obb.orientations[2].y * localNormal.z,
        obb.orientations[0].z * localNormal.x + obb.orientations[1].z * localNormal.y + obb.orientations[2].z * localNormal.z
    };

    return info;
}