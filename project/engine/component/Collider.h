#pragma once
#include "engine/utility/math/Math.h"
#include "Transform.h" // Transformを使うため
#include "CollisionConfig.h" 
#include <cstdint>
#include <vector>


// ColliderConfigは、Colliderの形状、中心、サイズ、回転をまとめた設定です。
struct ColliderConfig {
    ColliderType type = ColliderType::kAABB;
    Vector3 center = { 0.0f, 0.0f, 0.0f };
    Vector3 size = { 1.0f, 1.0f, 1.0f };
    Vector3 rotation = { 0.0f, 0.0f, 0.0f };
};

// TerrainCollisionDataは、高さマップ地形との衝突判定に使うサンプル情報です。
struct TerrainCollisionData {
    bool enabled = false;
    int resolution = 0;
    float sizeX = 1.0f;
    float sizeZ = 1.0f;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    std::vector<float> heights;
};

// 新しい Collider クラス
// Colliderは、Object3dに紐づく衝突形状と属性マスクを保持し、形状間判定を行います。
class Collider {
public:
    // コンストラクタ：誰（どのTransform）にくっつくかを指定して作る
        // 所有Object3dのTransformを参照し、ワールド座標で判定できるようにします。
Collider(Transform* ownerTransform);
    ~Collider() = default;

    // --- 設定 (Setters) ---
        // Colliderの形状設定をまとめて差し替えます。
void SetConfig(const ColliderConfig& config) { config_ = config; }
    void SetAttribute(uint32_t attribute) { attribute_ = attribute; }
    void SetMask(uint32_t mask) { mask_ = mask; }
    void SetTerrainData(const TerrainCollisionData& data) { terrainData_ = data; }
    void ClearTerrainData() { terrainData_ = TerrainCollisionData{}; }

    // --- 取得 (Getters) ---
    const ColliderConfig& GetConfig() const { return config_; }
    uint32_t GetAttribute() const { return attribute_; }
    uint32_t GetMask() const { return mask_; }
    const TerrainCollisionData& GetTerrainData() const { return terrainData_; }

    // タイプやサイズのショートカット
    ColliderType GetType() const { return config_.type; }
    Vector3 GetSize() const { return config_.size; }
    Vector3 GetCenter() const { return config_.center; }

    // 半径の取得 (スケールを加味)
        // 球判定や近似判定に使う半径を取得します。
float GetRadius() const;

    // --- 形状計算 (Object3dから移動) ---
    AABB GetAABB() const;
    OBB GetOBB() const;
    Ring GetRing() const;
    Cylinder GetCylinder() const;

    // --- 衝突判定 (Object3dから移動) ---
    // 相手も Object3d ではなく Collider として受け取るように変更
    CollisionInfo CheckCollision(const Collider* other) const;

private:
        // 地形Colliderから指定座標の高さと法線をサンプリングします。
bool SampleTerrain(const Collider* terrain, const Vector3& worldPosition, float& outHeight, Vector3& outNormal) const;
    Vector3 GetSphereCenter() const;
    CollisionInfo CheckSphereTerrainCollision(const Vector3& spherePos, float radius, const Collider* terrain) const;
    CollisionInfo CheckAABBTerrainCollision(const AABB& aabb, const Collider* terrain) const;

    // このコライダーの持ち主（座標計算に使う）
    Transform* transform_ = nullptr;

    // 設定データ
    ColliderConfig config_;

    // フィルタリング用
    uint32_t attribute_ = 0;
    uint32_t mask_ = 0xFFFFFFFF;

    TerrainCollisionData terrainData_;
};
