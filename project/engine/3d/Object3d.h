#pragma once
#include "engine/base/Math.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/3d/Model.h"
#include "engine/3d/CollisionConfig.h" // ★ 追加
#include <wrl.h>
#include <string>

class Object3d {
public:
    struct Transform {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
    };
    struct DirectionalLight {
        Vector4 color;
        Vector3 direction;
        float intensity;
    };

public:
    virtual void Initialize(Object3dCommon* common);
    virtual void Update();
    void Draw();

    void SetModel(Model* model) { model_ = model; }
    void SetModel(const std::string& modelName);
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }

    // ブレンドモード用のセッターとゲッター
    void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
    BlendMode GetBlendMode() const { return blendMode_; }

    Transform* GetTransform() { return &transform_; }
    Model::Material* GetMaterial() { return model_ ? model_->GetMaterial() : nullptr; }
    DirectionalLight* GetDirectionalLight() { return directionalLightData_; }

    // ▼▼▼ ここからあたり判定用の機能 ▼▼▼

    // --- 属性フラグ関連 ---
    void SetCollisionAttribute(uint32_t attribute) { collisionAttribute_ = attribute; }
    void SetCollisionMask(uint32_t mask) { collisionMask_ = mask; }
    uint32_t GetCollisionAttribute() const { return collisionAttribute_; }
    uint32_t GetCollisionMask() const { return collisionMask_; }

    // --- 形状設定関連 ---
    void SetColliderType(ColliderType type) { colliderType_ = type; }
    ColliderType GetColliderType() const { return colliderType_; }

    void SetCollisionRadius(float radius) { radius_ = radius; }
    float GetCollisionRadius() const { return radius_; }

    void SetCollisionSize(const Vector3& size) { aabbSize_ = size; }
    const Vector3& GetCollisionSize() const { return aabbSize_; }

    // --- ゲッター ---
    const Vector3& GetWorldPosition() const { return transform_.translate; }
    AABB GetAABB() const {
        Vector3 center = transform_.translate;
        return {
            {center.x - aabbSize_.x, center.y - aabbSize_.y, center.z - aabbSize_.z},
            {center.x + aabbSize_.x, center.y + aabbSize_.y, center.z + aabbSize_.z}
        };
    }

    /// <summary>
    /// 衝突時に呼び出される仮想関数 (中身は空)
    /// </summary>
    virtual void OnCollision(Object3d* other) {
        (void)other; // 未使用引数の警告避け
    }


protected: // ★★★ privateからprotectedに変更 ★★★
    Object3dCommon* common_ = nullptr;
    Model* model_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    BlendMode blendMode_ = BlendMode::kNormal;

    // ▼▼▼ あたり判定用のメンバ変数 ▼▼▼
    ColliderType colliderType_ = ColliderType::kNone;
    uint32_t collisionAttribute_ = 0;
    uint32_t collisionMask_ = 0xFFFFFFFF;
    float radius_ = 1.0f;
    Vector3 aabbSize_ = { 1.0f, 1.0f, 1.0f };
};