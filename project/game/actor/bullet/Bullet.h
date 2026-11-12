#pragma once
#include "Object3D.h"
#include "Event.h"

#include <string>

// 速度、寿命、衝突時の演出を持つゲーム用の汎用弾です。
// 関連：BulletManagerが生成・所有し、衝突時の通知はEventManagerへ渡す。
// 主な入口：Initialize / Update / OnCollision
class Bullet : public engine::Object3D {
  public:
    // Object3D を初期化します。衝突属性は Fire() で設定します。
    void Initialize(engine::Object3DCommon* common) override;

    // 速度に沿って移動し、寿命タイマーを進めます。
    void Update(float deltaTime) override;

    // 衝突相手に応じて弾の消滅やダメージ通知を行います。
    bool OnCollision(engine::Object3D* other) override;

    /// <summary>発射位置、速度、寿命、衝突属性とダメージを設定して弾を有効化します。</summary>
    /// <param name="life">消滅するまでの秒数</param>
    /// <param name="attribute">弾自身の衝突属性</param>
    /// <param name="mask">衝突対象の属性マスク</param>
    void Fire(const engine::Vector3& position, const engine::Vector3& velocity, float life, uint32_t attribute, uint32_t mask,
              float damage = 1.0f, const StatusEffectApplication& statusEffect = {},
              DamageType damageType = DamageType::Physical);

    // 弾へ追従する軌跡と、衝突時に一度だけ再生する演出を設定します。
    void ConfigureVfx(const std::string& trailPreset, const std::string& trailSecondaryPreset,
                      const std::string& impactPreset, const std::string& impactSecondaryPreset, float trailInterval,
                      float trailSpeedScale, bool trailReverseDirection, bool impactDirected);
    // 弾にまとわせる粒子のプリセットと基準半径を指定し、発生時計を先頭へ戻す。
    void ConfigureBurningVfx(const std::string& preset, float radius);
    void ConfigureDestructionVfx(const std::string& debrisPreset, const std::string& secondaryDebrisPreset,
                                 float debrisScale, bool impactOnExpiry, bool destroyOnSolidCastHit);

    void SetExplosionRadius(float radius) { explosionRadius_ = radius; }

    bool IsDead() const { return isDead_; }

    // 弾を死亡扱いにする。playImpact 時だけ着弾演出を出し、既に死亡済みなら何もしない。
    void Expire(bool playImpact = false);

    void SetGravity(float gravity) { gravity_ = gravity; }
    void SetPreviewOnly(const engine::Object3D* owner) { previewOnly_ = true; previewOwner_ = owner; }
    bool IsPreviewOnly() const { return previewOnly_; }
    const engine::Object3D* GetPreviewOwner() const { return previewOwner_; }

    // 液面から跳ねるマグマ弾だけに、速度に応じた伸縮とカメラ方向への向きを適用します。
    void SetMagmaHopperPresentation(bool enabled) { magmaHopperPresentation_ = enabled; }

    float GetDamage() const { return damage_; }

    const engine::Vector3& GetVelocity() const { return velocity_; }

    DamageType GetDamageType() const { return damageType_; }

    const StatusEffectApplication& GetStatusEffect() const { return statusEffect_; }

  private:
    // Castまたは接触判定で確定した直撃相手を、爆風の範囲と独立して処理する。
    void Explode(engine::Object3D* directHit = nullptr);

    float explosionRadius_ = 0.0f;

    engine::Vector3 velocity_ = engine::Vector3::ZERO;
    engine::Vector3 impactNormal_ = engine::Vector3::ZERO;
    float lifeTimer_ = 0.0f;
    float gravity_ = 0.0f;

    float damage_ = 1.0f;
    DamageType damageType_ = DamageType::Physical;
    StatusEffectApplication statusEffect_;

    std::string trailPreset_;
    std::string trailSecondaryPreset_;

    std::string impactPreset_;
    std::string impactSecondaryPreset_;
    std::string impactDebrisPreset_;
    std::string impactDebrisSecondaryPreset_;
    float impactDebrisScale_ = 1.0f;
    bool impactOnExpiry_ = false;
    bool destroyOnSolidCastHit_ = false;

    float trailInterval_ = 0.05f;
    float trailTimer_ = 0.0f;
    float trailSpeedScale_ = 1.0f;
    std::string burningPreset_;
    float burningRadius_ = 0.0f;
    float burningTimer_ = 0.0f;
    bool trailReverseDirection_ = false;

    bool impactDirected_ = true;
    bool impactEmitted_ = false;

    bool isDead_ = false;

    bool magmaHopperPresentation_ = false;
    bool previewOnly_ = false;
    const engine::Object3D* previewOwner_ = nullptr; // 比較用。所有も参照もしません。

    // 速度ゼロでも無効な方向ベクトルを GPU パーティクルへ渡さない。
    void EmitTrailVfx();
    void EmitBurningVfx();

    // 衝突と強制終了が重なっても着弾演出を重複発生させない。
    void EmitImpactVfx();
};
