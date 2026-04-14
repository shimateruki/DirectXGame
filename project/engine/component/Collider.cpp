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

CollisionInfo Collider::CheckCollision(const Collider* other) const {
    CollisionInfo collision;
    collision.isColliding = false;

    if (!other || !transform_ || !other->transform_) return collision;

    ColliderType myType = this->GetType();
    ColliderType otherType = other->GetType();

    // 自身のワールド座標 
    Vector3 myPos = transform_->translate;
    Vector3 otherPos = other->transform_->translate;

    // ==========================================
    // --- 同種形状 ---
    // ==========================================
    if (myType == ColliderType::kAABB && otherType == ColliderType::kAABB) {
        collision = CheckAABBCollision(this->GetAABB(), other->GetAABB());
    }
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kSphere) {
        collision = CheckSphereCollision(myPos, this->GetRadius(), otherPos, other->GetRadius());
    }
    else if (myType == ColliderType::kOBB && otherType == ColliderType::kOBB) {
        collision = CheckOBBCollision(this->GetOBB(), other->GetOBB());
    }
    else if (myType == ColliderType::kCylinder && otherType == ColliderType::kCylinder) {
        collision = CheckCylinderCollision(this->GetCylinder(), other->GetCylinder());
    }

    // ==========================================
    // --- 異種形状 ---
    // ==========================================
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

    else if (myType == ColliderType::kSphere && otherType == ColliderType::kCylinder) {
        collision = CheckSphereCylinderCollision(myPos, this->GetRadius(), other->GetCylinder());
    }
    else if (myType == ColliderType::kCylinder && otherType == ColliderType::kSphere) {
        collision = CheckSphereCylinderCollision(otherPos, other->GetRadius(), this->GetCylinder());
        collision.normal = collision.normal * -1.0f; // 押し出し方向を反転
    }
    else if (myType == ColliderType::kAABB && otherType == ColliderType::kCylinder) {
        collision = CheckAABBCylinderCollision(this->GetAABB(), other->GetCylinder());
        collision.normal = collision.normal * -1.0f; // 押し出し方向を反転
    }
    else if (myType == ColliderType::kCylinder && otherType == ColliderType::kAABB) {
        collision = CheckAABBCylinderCollision(other->GetAABB(), this->GetCylinder());
    }
    else if (myType == ColliderType::kOBB && otherType == ColliderType::kCylinder) {
        collision = CheckOBBCylinderCollision(this->GetOBB(), other->GetCylinder());
        collision.normal = collision.normal * -1.0f; // 押し出し方向を反転
    }
    else if (myType == ColliderType::kCylinder && otherType == ColliderType::kOBB) {
        collision = CheckOBBCylinderCollision(other->GetOBB(), this->GetCylinder());
    }

    return collision;
}
Cylinder Collider::GetCylinder() const {
    Cylinder cylinder;
    if (!transform_) return cylinder;

    Math math;
    Matrix4x4 matTrans = math.MakeTranslateMatrix(config_.center);
    Matrix4x4 matWorld = math.Multiply(matTrans, transform_->matWorld);

    cylinder.center = { matWorld.m[3][0], matWorld.m[3][1], matWorld.m[3][2] };

    // スケールを考慮 (XとZの大きい方を半径にかける)
    float scaleX = std::abs(transform_->scale.x);
    float scaleZ = std::abs(transform_->scale.z);
    cylinder.radius = config_.size.x * (std::max)(scaleX, scaleZ);

    // 高さはYスケールをかける
    cylinder.height = config_.size.y * std::abs(transform_->scale.y);

    return cylinder;
}