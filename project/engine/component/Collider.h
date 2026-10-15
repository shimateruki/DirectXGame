#pragma once
#include "engine/utility/math/Math.h"
#include "Transform.h"
#include "CollisionConfig.h" 
#include <cstdint>
#include <utility>
#include <vector>


/// <summary>衝突形状と所有Transformからの相対位置・大きさ。</summary>
struct ColliderConfig {
    ColliderType type = ColliderType::kAABB;
    engine::Vector3 center = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 size = { 1.0f, 1.0f, 1.0f };
    engine::Vector3 rotation = { 0.0f, 0.0f, 0.0f };
    // ローカルXZ平面の凸な上面輪郭。空なら従来の箱判定を使う。
    std::vector<engine::Vector2> footprint;
    // モデルの上向き面から接地高さを取る。側面の判定は箱と輪郭を使う。
    bool useModelSurface = false;
};

/// <summary>接地面として使うモデルのローカル三角形。</summary>
struct ModelSurfaceTriangle {
    engine::Vector3 a;
    engine::Vector3 b;
    engine::Vector3 c;
};

/// <summary>高さマップ地形との接触判定に使うサンプル情報。</summary>
struct TerrainCollisionData {
    bool enabled = false;
    int resolution = 0;
    float sizeX = 1.0f;
    float sizeZ = 1.0f;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    std::vector<float> heights;
};

/// <summary>Transformに紐づく衝突形状と属性マスクを保持し、形状間を判定する。</summary>
class Collider {
public:
    /// <summary>所有オブジェクトのTransformを参照してColliderを作る。</summary>
    /// <param name="ownerTransform">衝突判定中に有効なTransform。Colliderは所有しない。</param>
    Collider(Transform* ownerTransform);
    ~Collider() = default;

    /// <summary>形状設定をまとめて差し替える。</summary>
    void SetConfig(const ColliderConfig& config) { config_ = config; }
    void SetAttribute(uint32_t attribute) { attribute_ = attribute; }
    void SetMask(uint32_t mask) { mask_ = mask; }
    void SetTerrainData(const TerrainCollisionData& data) { terrainData_ = data; }
    void ClearTerrainData() { terrainData_ = TerrainCollisionData{}; }
    void SetModelSurface(std::vector<ModelSurfaceTriangle> triangles) { modelSurface_ = std::move(triangles); }
    bool HasModelSurface() const { return config_.useModelSurface && !modelSurface_.empty(); }

    /// <summary>描画アニメーションの拡縮に影響されない衝突判定用スケールを指定する。</summary>
    void SetScaleOverride(const engine::Vector3& scale) { scaleOverride_ = scale; useScaleOverride_ = true; }
    void ClearScaleOverride() { useScaleOverride_ = false; }

    // --- 取得 (Getters) ---
    const ColliderConfig& GetConfig() const { return config_; }
    uint32_t GetAttribute() const { return attribute_; }
    uint32_t GetMask() const { return mask_; }
    const TerrainCollisionData& GetTerrainData() const { return terrainData_; }
    bool HasScaleOverride() const { return useScaleOverride_; }
    const engine::Vector3& GetScaleOverride() const { return scaleOverride_; }

    // タイプやサイズのショートカット
    ColliderType GetType() const { return config_.type; }
    engine::Vector3 GetSize() const { return config_.size; }
    engine::Vector3 GetCenter() const { return config_.center; }

    /// <summary>所有Transformと上書きスケールを考慮した判定半径を返す。</summary>
    float GetRadius() const;

    // 所有Transformを反映したワールド空間の衝突形状。
    AABB GetAABB() const;
    OBB GetOBB() const;
    Ring GetRing() const;
    Cylinder GetCylinder() const;

    /// <summary>上面輪郭をワールド座標に変換する。デバッグ表示と判定で共用する。</summary>
    std::vector<engine::Vector3> GetFootprintWorldVertices(float localY) const;
    /// <summary>相手の水平断面が上面輪郭と重なるか調べる。</summary>
    bool OverlapsFootprint(const Collider* other, engine::Vector3* normal = nullptr, float* depth = nullptr) const;
    bool ContainsFootprintPoint(const engine::Vector3& worldPoint) const;
    /// <summary>モデル上面の高さと法線をワールド座標で求める。</summary>
    bool SampleModelSurface(const engine::Vector3& worldPoint, float& outHeight, engine::Vector3& outNormal) const;
    /// <summary>上面三角形に対するレイ交差を返す。</summary>
    bool RaycastModelSurface(const engine::Vector3& start, const engine::Vector3& direction,
                             float maxDistance, float& outDistance, engine::Vector3& outNormal) const;

    /// <summary>指定Colliderとの接触情報を返す。</summary>
    /// <param name="other">判定相手のCollider。</param>
    CollisionInfo CheckCollision(const Collider* other) const;

private:
    // 判定用の上書きスケールを優先して、衝突形状のワールド変換を組み立てる。
    engine::Vector3 GetCollisionScale() const;
    engine::Matrix4x4 GetCollisionWorldMatrix() const;

    // 地形Colliderの高さマップから指定座標の高さと法線をサンプリングする。
    bool SampleTerrain(const Collider* terrain, const engine::Vector3& worldPosition, float& outHeight, engine::Vector3& outNormal) const;

    // 球と地形の接触判定に使うワールド座標と押し戻し情報を求める。
    engine::Vector3 GetSphereCenter() const;
    CollisionInfo CheckSphereTerrainCollision(const engine::Vector3& spherePos, float radius, const Collider* terrain) const;
    CollisionInfo CheckAABBTerrainCollision(const AABB& aabb, const Collider* terrain) const;

    // このコライダーの持ち主（座標計算に使う）
    Transform* transform_ = nullptr;

    // 設定データ
    ColliderConfig config_;

    // フィルタリング用
    uint32_t attribute_ = 0;
    uint32_t mask_ = 0xFFFFFFFF;

    bool useScaleOverride_ = false;
    engine::Vector3 scaleOverride_ = { 1.0f, 1.0f, 1.0f };

    TerrainCollisionData terrainData_;
    std::vector<ModelSurfaceTriangle> modelSurface_;
};
