#pragma once
#include "Bullet.h"
#include <list>
#include <memory>
#include <string>

// 前方宣言
class Object3dCommon;
class CollisionManager;

// 弾の描画と追跡・着弾演出をまとめて指定します。衝突半径と寿命は Fire() の引数です。
struct BulletVisualConfig {
    std::string debugLabel; // 攻撃の構成名。生成元の弾部品をプレビューで識別します。
    bool collisionRadiusIsWorld = false; // 新しい弾部品では表示スケールと判定半径を独立させます。
    float explosionRadius = 0.0f;
    int32_t materialType = 0;
    BlendMode blendMode = BlendMode::kNormal;
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    float emissive = 1.0f;
    float visualScale = 1.0f;
    float waveSpeed = 2.0f;
    float waveHeight = 0.5f;
    float waveFrequency = 1.5f;
    float effectType = 0.0f;
    float effectScale = 1.0f;
    float effectSoftness = 0.55f;
    float effectIntensity = 1.0f;
    float billboardScale = 0.55f;
    std::string texturePath = "";
    std::string trailPreset = "";
    std::string trailSecondaryPreset = "";
    std::string impactPreset = "";
    std::string impactSecondaryPreset = "";
    float trailInterval = 0.05f;
    float trailSpeedScale = 1.0f;
    bool trailReverseDirection = false;
    bool impactDirected = true;
    // マグマ噴出弾の液面クリップ、専用の伸縮と影の扱いを有効にします。
    bool magmaHopperPresentation = false;
};

/// <summary>
/// ゲーム中の弾の生成、更新、描画を管理します。
/// </summary>
class BulletManager {
  public:
    static BulletManager* GetInstance();

    /// <summary>
    /// 描画と衝突管理への参照を受け取る。どちらも所有しない。
    /// </summary>
    void Initialize(Object3dCommon* common, CollisionManager* colManager);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    // リプレイ分岐など、時間軸を切り替える際に残存弾を安全に破棄します。
    void Clear();

    /// <summary>
    /// 全ての弾を更新し、死亡した弾を衝突管理から解除して破棄する。
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 全ての弾を描画
    /// </summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    bool HasSpecialMaterialBullets() const;
    void DrawSpecial(uint32_t depthSrvHandle, uint32_t grabSrvHandle);

    /// <summary>
    /// 指定した当たり判定と演出を持つ弾を生成し、衝突管理へ登録します。
    /// </summary>
    /// <param name="radius">ワールド座標での衝突半径</param>
    /// <param name="life">弾の寿命（秒）</param>
    /// <param name="gravity">下向きの加速度。負値は 0 に補正します。</param>
    /// <param name="attr">弾自身の衝突属性。</param>
    /// <param name="mask">衝突対象にする属性マスク。</param>
    void Fire(const Vector3& pos, const Vector3& vel, uint32_t attr, uint32_t mask,
              const std::string& model = "Primitives/sphere", float radius = 0.2f, float life = 120,
              const BulletVisualConfig& visualConfig = {}, float damage = 1.0f,
              const StatusEffectApplication& statusEffect = {}, DamageType damageType = DamageType::Physical,
              float gravity = 0.0f);

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
