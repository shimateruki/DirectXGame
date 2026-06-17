#include "Collider.h"
#include <algorithm>
#include <cmath>

// コンストラクタ
Collider::Collider(Transform* ownerTransform) {
    // 持ち主のTransformを記憶しておく
    transform_ = ownerTransform;
}

float Collider::GetRadius() const {
    if (!transform_) return config_.size.x;

    // スケールの最大値を掛ける
    float maxScale = (std::max)({
        std::abs(transform_->scale.x),
        std::abs(transform_->scale.y),
        std::abs(transform_->scale.z)
        });
    return config_.size.x * maxScale;
}

OBB Collider::GetOBB() const {
    OBB obb;
    if (!transform_) return obb;

    Math math;

    // 1. コライダーのローカル行列
    Matrix4x4 matRotX = math.MakeRotateXMatrix(config_.rotation.x);
    Matrix4x4 matRotY = math.MakeRotateYMatrix(config_.rotation.y);
    Matrix4x4 matRotZ = math.MakeRotateZMatrix(config_.rotation.z);
    Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));

    Matrix4x4 matTrans = math.MakeTranslateMatrix(config_.center);
    Matrix4x4 matColliderLocal = math.Multiply(matRot, matTrans);

    // 2. 持ち主のワールド行列と合成
    Matrix4x4 matFinal = math.Multiply(matColliderLocal, transform_->matWorld);

    // 3. 情報抽出
    obb.center = { matFinal.m[3][0], matFinal.m[3][1], matFinal.m[3][2] };
    obb.orientations[0] = math.Normalize({ matFinal.m[0][0], matFinal.m[0][1], matFinal.m[0][2] });
    obb.orientations[1] = math.Normalize({ matFinal.m[1][0], matFinal.m[1][1], matFinal.m[1][2] });
    obb.orientations[2] = math.Normalize({ matFinal.m[2][0], matFinal.m[2][1], matFinal.m[2][2] });

    obb.size = {
        config_.size.x * transform_->scale.x,
        config_.size.y * transform_->scale.y,
        config_.size.z * transform_->scale.z
    };

    return obb;
}

AABB Collider::GetAABB() const {
    OBB obb = GetOBB();
    Vector3 axisX = obb.orientations[0] * obb.size.x;
    Vector3 axisY = obb.orientations[1] * obb.size.y;
    Vector3 axisZ = obb.orientations[2] * obb.size.z;

    Vector3 corners[8] = {
        obb.center - axisX - axisY - axisZ,
        obb.center + axisX - axisY - axisZ,
        obb.center - axisX + axisY - axisZ,
        obb.center + axisX + axisY - axisZ,
        obb.center - axisX - axisY + axisZ,
        obb.center + axisX - axisY + axisZ,
        obb.center - axisX + axisY + axisZ,
        obb.center + axisX + axisY + axisZ
    };

    Vector3 minPos = corners[0];
    Vector3 maxPos = corners[0];

    for (int i = 1; i < 8; ++i) {
        minPos.x = std::min(minPos.x, corners[i].x);
        minPos.y = std::min(minPos.y, corners[i].y);
        minPos.z = std::min(minPos.z, corners[i].z);

        maxPos.x = std::max(maxPos.x, corners[i].x);
        maxPos.y = std::max(maxPos.y, corners[i].y);
        maxPos.z = std::max(maxPos.z, corners[i].z);
    }

    return { minPos, maxPos };
}

Ring Collider::GetRing() const {
    Ring ring;
    if (!transform_) return ring;

    Math math;

    // 1. 回転を計算
    Matrix4x4 matRotX = math.MakeRotateXMatrix(config_.rotation.x);
    Matrix4x4 matRotY = math.MakeRotateYMatrix(config_.rotation.y);
    Matrix4x4 matRotZ = math.MakeRotateZMatrix(config_.rotation.z);
    Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));

    // 2. 持ち主のワールド回転と合成
    // matWorld から回転成分だけ抽出するのが理想だが、ここでは簡易的に normal を変換
    Vector3 localNormal = { 0, 1, 0 }; // ローカル空間でのリングの向き (上向き)
    
    // 回転行列を適用
    Matrix4x4 worldRot = math.Multiply(matRot, transform_->matWorld);
    // スケールを除去した回転成分だけ取り出すのが正しいが、
    // ここでは normal を変換して正規化する
    ring.normal = math.Normalize({
        localNormal.x * worldRot.m[0][0] + localNormal.y * worldRot.m[1][0] + localNormal.z * worldRot.m[2][0],
        localNormal.x * worldRot.m[0][1] + localNormal.y * worldRot.m[1][1] + localNormal.z * worldRot.m[2][1],
        localNormal.x * worldRot.m[0][2] + localNormal.y * worldRot.m[1][2] + localNormal.z * worldRot.m[2][2]
    });

    // 3. 中心座標 (オフセット加味)
    Matrix4x4 matTrans = math.MakeTranslateMatrix(config_.center);
    Matrix4x4 matFinal = math.Multiply(matTrans, transform_->matWorld);
    ring.center = { matFinal.m[3][0], matFinal.m[3][1], matFinal.m[3][2] };

    // 4. サイズ (スケールを反映)
    // X を外径、Z を内径、Y を厚みとして扱う
    ring.outerRadius = config_.size.x * std::abs(transform_->scale.x);
    ring.innerRadius = config_.size.z * std::abs(transform_->scale.z);
    ring.height = config_.size.y * 2.0f * std::abs(transform_->scale.y);

    // 外径が内径より小さくならないように調整
    if (ring.outerRadius < ring.innerRadius) {
        std::swap(ring.outerRadius, ring.innerRadius);
    }

    return ring;
}

Cylinder Collider::GetCylinder() const {
    Cylinder cyl;
    if (!transform_) return cyl;
    Math math;

    // 1. 回転
    Matrix4x4 matRotX = math.MakeRotateXMatrix(config_.rotation.x);
    Matrix4x4 matRotY = math.MakeRotateYMatrix(config_.rotation.y);
    Matrix4x4 matRotZ = math.MakeRotateZMatrix(config_.rotation.z);
    Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));

    // 2. 中心 (オフセット加味)
    Matrix4x4 matTrans = math.MakeTranslateMatrix(config_.center);
    Matrix4x4 matFinal = math.Multiply(math.Multiply(matRot, matTrans), transform_->matWorld);
    cyl.center = { matFinal.m[3][0], matFinal.m[3][1], matFinal.m[3][2] };

    // 3. 軸 (Y軸を想定)
    Vector3 localAxis = { 0, 1, 0 };
    Matrix4x4 worldRot = math.Multiply(matRot, transform_->matWorld);
    cyl.axis = math.Normalize({
        localAxis.x * worldRot.m[0][0] + localAxis.y * worldRot.m[1][0] + localAxis.z * worldRot.m[2][0],
        localAxis.x * worldRot.m[0][1] + localAxis.y * worldRot.m[1][1] + localAxis.z * worldRot.m[2][1],
        localAxis.x * worldRot.m[0][2] + localAxis.y * worldRot.m[1][2] + localAxis.z * worldRot.m[2][2]
    });

    // 4. サイズ
    cyl.radius = config_.size.x * std::abs(transform_->scale.x);
    cyl.height = config_.size.y * 2.0f * std::abs(transform_->scale.y);

    return cyl;
}

bool Collider::SampleTerrain(const Collider* terrain, const Vector3& worldPosition, float& outHeight, Vector3& outNormal) const {
    if (!terrain || !terrain->transform_) return false;
    const TerrainCollisionData& data = terrain->terrainData_;
    if (!data.enabled || data.resolution <= 0) return false;

    const int sampleCount = data.resolution + 1;
    if (static_cast<int>(data.heights.size()) < sampleCount * sampleCount) return false;

    const Vector3 terrainPos = terrain->transform_->translate;
    const Vector3 terrainScale = terrain->transform_->scale;
    const float scaleX = std::max(0.0001f, std::abs(terrainScale.x));
    const float scaleY = std::max(0.0001f, std::abs(terrainScale.y));
    const float scaleZ = std::max(0.0001f, std::abs(terrainScale.z));
    const float localX = (worldPosition.x - terrainPos.x) / scaleX;
    const float localZ = (worldPosition.z - terrainPos.z) / scaleZ;
    const float u = localX / std::max(0.0001f, data.sizeX) + 0.5f;
    const float v = localZ / std::max(0.0001f, data.sizeZ) + 0.5f;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return false;

    const float fx = u * static_cast<float>(data.resolution);
    const float fz = v * static_cast<float>(data.resolution);
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, data.resolution);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, data.resolution);
    const int x1 = std::clamp(x0 + 1, 0, data.resolution);
    const int z1 = std::clamp(z0 + 1, 0, data.resolution);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    auto heightAt = [&](int x, int z) -> float {
        return data.heights[static_cast<size_t>(z * sampleCount + x)];
    };
    const float h00 = heightAt(x0, z0);
    const float h10 = heightAt(x1, z0);
    const float h01 = heightAt(x0, z1);
    const float h11 = heightAt(x1, z1);
    const float hx0 = h00 + (h10 - h00) * tx;
    const float hx1 = h01 + (h11 - h01) * tx;
    const float localHeight = hx0 + (hx1 - hx0) * tz;
    outHeight = terrainPos.y + localHeight * scaleY;

    const int xm = std::clamp(x0 - 1, 0, data.resolution);
    const int xp = std::clamp(x0 + 1, 0, data.resolution);
    const int zm = std::clamp(z0 - 1, 0, data.resolution);
    const int zp = std::clamp(z0 + 1, 0, data.resolution);
    const float cellX = std::max(0.0001f, data.sizeX / static_cast<float>(data.resolution) * scaleX);
    const float cellZ = std::max(0.0001f, data.sizeZ / static_cast<float>(data.resolution) * scaleZ);
    const float gradX = ((heightAt(xp, z0) - heightAt(xm, z0)) * scaleY) / cellX;
    const float gradZ = ((heightAt(x0, zp) - heightAt(x0, zm)) * scaleY) / cellZ;
    outNormal = { -gradX, 1.0f, -gradZ };
    const float len = std::sqrt(outNormal.x * outNormal.x + outNormal.y * outNormal.y + outNormal.z * outNormal.z);
    if (len > 0.0001f) {
        outNormal.x /= len;
        outNormal.y /= len;
        outNormal.z /= len;
    } else {
        outNormal = { 0.0f, 1.0f, 0.0f };
    }
    return true;
}

CollisionInfo Collider::CheckSphereTerrainCollision(const Vector3& spherePos, float radius, const Collider* terrain) const {
    CollisionInfo collision;
    float terrainHeight = 0.0f;
    Vector3 normal = { 0.0f, 1.0f, 0.0f };
    if (!SampleTerrain(terrain, spherePos, terrainHeight, normal)) return collision;

    const float bottom = spherePos.y - radius;
    const float top = spherePos.y + radius;
    if (bottom <= terrainHeight && top >= terrainHeight - radius * 0.25f) {
        collision.isColliding = true;
        collision.normal = normal;
        collision.penetration = terrainHeight - bottom;
    }
    return collision;
}

CollisionInfo Collider::CheckAABBTerrainCollision(const AABB& aabb, const Collider* terrain) const {
    CollisionInfo collision;
    const Vector3 points[5] = {
        { (aabb.min.x + aabb.max.x) * 0.5f, aabb.min.y, (aabb.min.z + aabb.max.z) * 0.5f },
        { aabb.min.x, aabb.min.y, aabb.min.z },
        { aabb.max.x, aabb.min.y, aabb.min.z },
        { aabb.min.x, aabb.min.y, aabb.max.z },
        { aabb.max.x, aabb.min.y, aabb.max.z },
    };

    float bestPenetration = 0.0f;
    Vector3 bestNormal = { 0.0f, 1.0f, 0.0f };
    for (const Vector3& point : points) {
        float terrainHeight = 0.0f;
        Vector3 normal = { 0.0f, 1.0f, 0.0f };
        if (!SampleTerrain(terrain, point, terrainHeight, normal)) continue;
        if (aabb.min.y <= terrainHeight && aabb.max.y >= terrainHeight - 0.05f) {
            const float penetration = terrainHeight - aabb.min.y;
            if (penetration > bestPenetration) {
                bestPenetration = penetration;
                bestNormal = normal;
            }
        }
    }

    if (bestPenetration > 0.0f) {
        collision.isColliding = true;
        collision.normal = bestNormal;
        collision.penetration = bestPenetration;
    }
    return collision;
}

CollisionInfo Collider::CheckCollision(const Collider* other) const {
    CollisionInfo collision;
    collision.isColliding = false;

    if (!other || !transform_ || !other->transform_) return collision;

    ColliderType myType = this->GetType();
    ColliderType otherType = other->GetType();
    if (myType == ColliderType::kNone || otherType == ColliderType::kNone) {
        return collision;
    }

    // 自身のワールド座標 
    Vector3 myPos = transform_->translate; 

    Vector3 otherPos = other->transform_->translate;

    if (myType == ColliderType::kSphere && otherType == ColliderType::kTerrain) {
        collision = CheckSphereTerrainCollision(myPos, this->GetRadius(), other);
    } else if (myType == ColliderType::kTerrain && otherType == ColliderType::kSphere) {
        collision = other->CheckSphereTerrainCollision(otherPos, other->GetRadius(), this);
        collision.normal = collision.normal * -1.0f;
    } else if ((myType == ColliderType::kAABB || myType == ColliderType::kOBB || myType == ColliderType::kCylinder) && otherType == ColliderType::kTerrain) {
        collision = CheckAABBTerrainCollision(this->GetAABB(), other);
    } else if (myType == ColliderType::kTerrain && (otherType == ColliderType::kAABB || otherType == ColliderType::kOBB || otherType == ColliderType::kCylinder)) {
        collision = other->CheckAABBTerrainCollision(other->GetAABB(), this);
        collision.normal = collision.normal * -1.0f;
    }
    // --- 同種形状 ---
    else if (myType == ColliderType::kAABB && otherType == ColliderType::kAABB) {
        collision = CheckAABBCollision(this->GetAABB(), other->GetAABB());
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kSphere) {
        collision = CheckSphereCollision(
            myPos, this->GetRadius(),
            otherPos, other->GetRadius());
    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kOBB) {
        collision = CheckOBBCollision(this->GetOBB(), other->GetOBB());
    }
    // --- 異種形状 ---
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kAABB) {
        collision = CheckSphereAABBCollision(myPos, this->GetRadius(), other->GetAABB());
    } else if (myType == ColliderType::kAABB && otherType == ColliderType::kSphere) {
        collision = CheckSphereAABBCollision(otherPos, other->GetRadius(), this->GetAABB());
        collision.normal = collision.normal * -1.0f;
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kOBB) {
        collision = CheckSphereOBBCollision(myPos, this->GetRadius(), other->GetOBB());
    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kSphere) {
        collision = CheckSphereOBBCollision(otherPos, other->GetRadius(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    } else if (myType == ColliderType::kAABB && otherType == ColliderType::kOBB) {
        collision = CheckAABBOBBCollision(this->GetAABB(), other->GetOBB());

    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kAABB) {
        collision = CheckAABBOBBCollision(other->GetAABB(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }
    // --- Ring 関連 ---
    else if (myType == ColliderType::kRing && otherType == ColliderType::kSphere) {
        collision = CheckRingSphereCollision(this->GetRing(), otherPos, other->GetRadius());
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kRing) {
        collision = CheckRingSphereCollision(other->GetRing(), myPos, this->GetRadius());
        collision.normal = collision.normal * -1.0f;
    }
    else if (myType == ColliderType::kRing && (otherType == ColliderType::kOBB || otherType == ColliderType::kAABB)) {
        collision = CheckRingOBBCollision(this->GetRing(), other->GetOBB());
    } else if ((myType == ColliderType::kOBB || myType == ColliderType::kAABB) && otherType == ColliderType::kRing) {
        collision = CheckRingOBBCollision(other->GetRing(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }

    // --- Cylinder 関連 ---
    else if (myType == ColliderType::kCylinder && otherType == ColliderType::kSphere) {
        collision = CheckCylinderSphereCollision(this->GetCylinder(), otherPos, other->GetRadius());
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kCylinder) {
        collision = CheckCylinderSphereCollision(other->GetCylinder(), myPos, this->GetRadius());
        collision.normal = collision.normal * -1.0f;
    }
    else if (myType == ColliderType::kCylinder && (otherType == ColliderType::kOBB || otherType == ColliderType::kAABB)) {
        collision = CheckCylinderOBBCollision(this->GetCylinder(), other->GetOBB());
    } else if ((myType == ColliderType::kOBB || myType == ColliderType::kAABB) && otherType == ColliderType::kCylinder) {
        collision = CheckCylinderOBBCollision(other->GetCylinder(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }

    return collision;
}
