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

CollisionInfo Collider::CheckCollision(const Collider* other) const {
    CollisionInfo collision;
    collision.isColliding = false;

    if (!other || !transform_ || !other->transform_) return collision;

    ColliderType myType = this->GetType();
    ColliderType otherType = other->GetType();

    // 自身のワールド座標 
    Vector3 myPos = transform_->translate;

    Vector3 otherPos = other->transform_->translate;

    // --- 同種形状 ---
    if (myType == ColliderType::kAABB && otherType == ColliderType::kAABB) {
        collision = CheckAABBCollision(this->GetAABB(), other->GetAABB());
    }
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kSphere) {
        collision = CheckSphereCollision(
            myPos, this->GetRadius(),
            otherPos, other->GetRadius());
    }
    else if (myType == ColliderType::kOBB && otherType == ColliderType::kOBB) {
        collision = CheckOBBCollision(this->GetOBB(), other->GetOBB());
    }
    // --- 異種形状 ---
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kAABB) {
        collision = CheckSphereAABBCollision(myPos, this->GetRadius(), other->GetAABB());
    }
    else if (myType == ColliderType::kAABB && otherType == ColliderType::kSphere) {
        collision = CheckSphereAABBCollision(otherPos, other->GetRadius(), this->GetAABB());
        collision.normal = collision.normal * -1.0f;
    }
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kOBB) {
        collision = CheckSphereOBBCollision(myPos, this->GetRadius(), other->GetOBB());
    }
    else if (myType == ColliderType::kOBB && otherType == ColliderType::kSphere) {
        collision = CheckSphereOBBCollision(otherPos, other->GetRadius(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }
    else if (myType == ColliderType::kAABB && otherType == ColliderType::kOBB) {
        collision = CheckAABBOBBCollision(this->GetAABB(), other->GetOBB());

    }
    else if (myType == ColliderType::kOBB && otherType == ColliderType::kAABB) {
        collision = CheckAABBOBBCollision(other->GetAABB(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }
    // --- Ring 関連 ---
    else if (myType == ColliderType::kRing && otherType == ColliderType::kSphere) {
        collision = CheckRingSphereCollision(this->GetRing(), otherPos, other->GetRadius());
    }
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kRing) {
        collision = CheckRingSphereCollision(other->GetRing(), myPos, this->GetRadius());
        collision.normal = collision.normal * -1.0f;
    }
    else if (myType == ColliderType::kRing && (otherType == ColliderType::kOBB || otherType == ColliderType::kAABB)) {
        collision = CheckRingOBBCollision(this->GetRing(), other->GetOBB());
    }
    else if ((myType == ColliderType::kOBB || myType == ColliderType::kAABB) && otherType == ColliderType::kRing) {
        collision = CheckRingOBBCollision(other->GetRing(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }

    // --- Cylinder 関連 ---
    else if (myType == ColliderType::kCylinder && otherType == ColliderType::kSphere) {
        collision = CheckCylinderSphereCollision(this->GetCylinder(), otherPos, other->GetRadius());
    }
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kCylinder) {
        collision = CheckCylinderSphereCollision(other->GetCylinder(), myPos, this->GetRadius());
        collision.normal = collision.normal * -1.0f;
    }
    else if (myType == ColliderType::kCylinder && (otherType == ColliderType::kOBB || otherType == ColliderType::kAABB)) {
        collision = CheckCylinderOBBCollision(this->GetCylinder(), other->GetOBB());
    }
    else if ((myType == ColliderType::kOBB || myType == ColliderType::kAABB) && otherType == ColliderType::kCylinder) {
        collision = CheckCylinderOBBCollision(other->GetCylinder(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }

    return collision;
}