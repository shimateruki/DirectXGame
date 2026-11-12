#pragma once
#include "engine/utility/math/Math.h"
#include "Transform.h"
#include "CollisionConfig.h" 
#include <array>
#include <cstdint>
#include <utility>
#include <vector>


/// <summary>衝突形状と所有Transformからの相対位置・大きさ。</summary>
// 関連：Collider（この型を使う機能の本体）
struct ColliderConfig {
    ColliderType type = ColliderType::Aabb;
    engine::Vector3 center = engine::Vector3::ZERO; // 所有Transform基準のローカル中心。
    // sizeの解釈は形状別。箱は半幅、球はxを半径として使い、ObjectのScaleは別に適用する。
    engine::Vector3 size = { 1.0f, 1.0f, 1.0f };
    // 衝突形状のローカル回転をラジアンで指定する。Objectの回転とは別の設定。
    engine::Vector3 rotation = engine::Vector3::ZERO;

    // ローカルXZ平面の凸な上面輪郭。空なら従来の箱判定を使う。
    std::vector<engine::Vector2> footprint;

    // モデルの上向き面から接地高さを取る。側面の判定は箱と輪郭を使う。
    bool useModelSurface = false;

    // モデル全体の凸包を使う。穴や凹部を持つ形状は子Colliderへ分割する。
    bool useModelVolume = false;
};

/// <summary>接地面として使うモデルのローカル三角形。</summary>
// 関連：Collider（この型を使う機能の本体）
struct ModelSurfaceTriangle {
    engine::Vector3 a;
    engine::Vector3 b;
    engine::Vector3 c;
};

/// <summary>高さマップ地形との接触判定に使うサンプル情報。</summary>
// 関連：Collider（この型を使う機能の本体）
struct TerrainCollisionData {
    bool enabled = false;
    int resolution = 0; // 高さマップ一辺のサンプル数。
    float sizeX = 1.0f; // 高さマップのX方向の広さ。
    float sizeZ = 1.0f; // 高さマップのZ方向の広さ。
    float minHeight = 0.0f; // 高さ値の下限。
    float maxHeight = 0.0f; // 高さ値の上限。
    std::vector<float> heights; // 格子状に並べた高さサンプル。
};

// 担当：Transformを反映した衝突形状と属性を管理し、形状同士を判定する。
// 関連：Object3D（保持）、Transform（姿勢）、CollisionManager（組合せ・通知）
// 主な入口：SetConfig / GetAabb / GetObb / CheckCollision
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

    // 高さマップの設定を既定値へ戻す。
    void ClearTerrainData() { terrainData_ = TerrainCollisionData{}; }

    // モデルのローカル三角形を受け取り、上面判定と凸包の元データを更新する。
    void SetModelSurface(std::vector<ModelSurfaceTriangle> triangles);

    bool HasModelSurface() const { return config_.useModelSurface && !modelSurface_.empty(); }

    bool HasModelVolume() const { return config_.useModelVolume && !modelHull_.empty(); }

    // ワールド空間の凸包へレイを飛ばす。ヒット判定がtrueの場合に距離と法線を利用する。
    bool RaycastModelVolume(const engine::Vector3& start, const engine::Vector3& direction,
                           float maxDistance, float& distance, engine::Vector3& normal) const;

    /// <summary>実際の凸包をエディターの判定表示にも使用する。</summary>
    std::vector<std::array<engine::Vector3, 3>> GetWorldHull() const;

    /// <summary>描画アニメーションの拡縮に影響されない衝突判定用スケールを指定する。</summary>
    void SetScaleOverride(const engine::Vector3& scale) { scaleOverride_ = scale; useScaleOverride_ = true; }

    // 衝突専用スケールを解除し、所有Transformのスケールを使う。
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

    // ここで返すsizeとcenterは設定値のまま。ワールド形状が必要ならGetAABB/GetOBB等を使う。
    engine::Vector3 GetSize() const { return config_.size; }

    engine::Vector3 GetCenter() const { return config_.center; }

    /// <summary>所有Transformと上書きスケールを考慮した判定半径を返す。</summary>
    float GetRadius() const;

    // 所有Transformを反映したワールド空間の衝突形状。
    Aabb GetAabb() const;

    Obb GetObb() const;

    Ring GetRing() const;

    Cylinder GetCylinder() const;

    /// <summary>上面輪郭をワールド座標に変換する。デバッグ表示と判定で共用する。</summary>
    std::vector<engine::Vector3> GetFootprintWorldVertices(float localY) const;

    /// <summary>相手の水平断面が上面輪郭と重なるか調べる。</summary>
    bool OverlapsFootprint(const Collider* other, engine::Vector3* normal = nullptr, float* depth = nullptr) const;

    // ワールド点のXZだけを上面輪郭で判定し、Y方向の高さ条件は別途確認する。
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

    // モデル凸包を使い、相手の形状との接触情報を求める。
    CollisionInfo CheckModelVolume(const Collider* other) const;
    // 姿勢が変わった場合にワールド凸包と判定用の方向を更新する。
    void UpdateWorldHullCache() const;

    // 地形Colliderの高さマップから指定座標の高さと法線をサンプリングする。
    bool SampleTerrain(const Collider* terrain, const engine::Vector3& worldPosition, float& outHeight, engine::Vector3& outNormal) const;

    // 球と地形の接触判定に使うワールド座標と押し戻し情報を求める。
    engine::Vector3 GetSphereCenter() const;

    // 球と高さマップ地形の接触・押し戻しを計算する。
    CollisionInfo CheckSphereTerrainCollision(const engine::Vector3& spherePos, float radius, const Collider* terrain) const;

    // 軸に沿う箱と高さマップ地形の接触情報を求める。
    CollisionInfo CheckAabbTerrainCollision(const Aabb& aabb, const Collider* terrain) const;

    // このコライダーの持ち主（座標計算に使う）
    Transform* transform_ = nullptr;

    // 設定データ
    ColliderConfig config_;

    // フィルタリング用
    uint32_t attribute_ = 0;
    uint32_t mask_ = 0xFFFFFFFF; // 接触を許可する相手属性のビット集合。

    bool useScaleOverride_ = false; // Transformとは別の判定用スケールを使うか。
    engine::Vector3 scaleOverride_ = { 1.0f, 1.0f, 1.0f }; // 衝突形状だけに適用するスケール。

    TerrainCollisionData terrainData_; // 高さマップ地形の判定設定。
    std::vector<ModelSurfaceTriangle> modelSurface_; // モデルのローカル三角形。
    std::vector<std::array<engine::Vector3, 3>> modelHull_; // ローカル座標の凸包面。
    // 静的地形の変換・分離軸は再利用し、動く床だけ行列が変わった時に更新する。
    mutable bool hullCacheValid_ = false;
    mutable engine::Matrix4x4 hullCacheMatrix_{};
    mutable std::vector<std::array<engine::Vector3, 3>> worldHull_; // 姿勢を反映した凸包面のキャッシュ。
    mutable std::vector<engine::Vector3> hullPoints_, hullNormals_, hullEdges_;
};
