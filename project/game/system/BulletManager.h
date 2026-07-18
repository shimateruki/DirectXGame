#pragma once
#include "Bullet.h" 
#include <list>
#include <memory>
#include <string>

// 前方宣言
class Object3dCommon;
class CollisionManager;

struct BulletVisualConfig {
    int32_t materialType = 0;
    BlendMode blendMode = BlendMode::kNormal;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float emissive = 1.0f;
    float visualScale = 1.0f;
    float effectType = 0.0f;
    float effectScale = 1.0f;
    float effectSoftness = 0.55f;
    float effectIntensity = 1.0f;
    float billboardScale = 0.55f;
    std::string texturePath = "";
};

/// <summary>
/// 弾を管理する「エンジン側」のシステム
/// </summary>
class BulletManager {
public:
    static BulletManager* GetInstance();

    /// <summary>
    /// 初期化 (Object3dCommon と CollisionManager が必要)
    /// </summary>
    void Initialize(Object3dCommon* common, CollisionManager* colManager);

    /// <summary>
    /// 終了処理 
    /// </summary>
    void Finalize();

    // リプレイ分岐など、時間軸を切り替える際に残存弾を安全に破棄します。
    void Clear();

    /// <summary>
    /// 全ての弾を更新 (移動、衝突による削除)
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 全ての弾を描画
    /// </summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    bool HasSpecialMaterialBullets() const;
    void DrawSpecial(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>
    /// 弾を発射
    /// </summary>
    void Fire(const Vector3& pos, const Vector3& vel,
        uint32_t attr, uint32_t mask,
        const std::string& model = "Primitives/sphere", float radius = 0.2f, float life = 120,
        const BulletVisualConfig& visualConfig = {});

    const std::list<std::unique_ptr<Bullet>>& GetBullets() const { return bullets_; }

private:
    BulletManager() = default;
    ~BulletManager() = default;
    BulletManager(const BulletManager&) = delete;
    BulletManager& operator=(const BulletManager&) = delete;

    std::list<std::unique_ptr<Bullet>> bullets_;
    Object3dCommon* common_ = nullptr;
    CollisionManager* colManager_ = nullptr;
};
