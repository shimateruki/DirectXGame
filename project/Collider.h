#pragma once
#include "engine/utility/math/Math.h"
#include "Transform.h" // Transformを使うため
#include "CollisionConfig.h" 
#include <cstdint>


struct ColliderConfig {
    ColliderType type = ColliderType::kAABB;
    Vector3 center = { 0.0f, 0.0f, 0.0f };
    Vector3 size = { 1.0f, 1.0f, 1.0f };
    Vector3 rotation = { 0.0f, 0.0f, 0.0f };
};

// 新しい Collider クラス
class Collider {
public:
    // コンストラクタ：誰（どのTransform）にくっつくかを指定して作る
    Collider(Transform* ownerTransform);
    ~Collider() = default;

    // --- 設定 (Setters) ---
    void SetConfig(const ColliderConfig& config) { config_ = config; }
    void SetAttribute(uint32_t attribute) { attribute_ = attribute; }
    void SetMask(uint32_t mask) { mask_ = mask; }

    // --- 取得 (Getters) ---
    const ColliderConfig& GetConfig() const { return config_; }
    uint32_t GetAttribute() const { return attribute_; }
    uint32_t GetMask() const { return mask_; }

    // タイプやサイズのショートカット
    ColliderType GetType() const { return config_.type; }
    Vector3 GetSize() const { return config_.size; }
    Vector3 GetCenter() const { return config_.center; }

    // 半径の取得 (スケールを加味)
    float GetRadius() const;

    // --- 形状計算 (Object3dから移動) ---
    AABB GetAABB() const;
    OBB GetOBB() const;

    // --- 衝突判定 (Object3dから移動) ---
    // 相手も Object3d ではなく Collider として受け取るように変更
    CollisionInfo CheckCollision(const Collider* other) const;

private:
    // このコライダーの持ち主（座標計算に使う）
    Transform* transform_ = nullptr;

    // 設定データ
    ColliderConfig config_;

    // フィルタリング用
    uint32_t attribute_ = 0;
    uint32_t mask_ = 0xFFFFFFFF;
};