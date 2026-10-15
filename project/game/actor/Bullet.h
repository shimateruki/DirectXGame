#pragma once
#include "Object3d.h"
#include "Event.h"

#include <string>

// 速度、寿命、衝突時の演出を持つゲーム用の汎用弾です。
class Bullet : public engine::Object3d {
  public:
    // Object3d を初期化します。衝突属性は Fire() で設定します。
    void Initialize(Object3dCommon* common) override;
    // 速度に沿って移動し、寿命タイマーを進めます。
    void Update(float deltaTime) override;
    // 衝突相手に応じて弾の消滅やダメージ通知を行います。
    bool OnCollision(engine::Object3d* other) override;

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

    void SetExplosionRadius(float radius) { explosionRadius_ = radius; }
    bool IsDead() const { return isDead_; }
    void Expire(bool playImpact = false);
    void SetGravity(float gravity) { gravity_ = gravity; }
    // 液面から跳ねるマグマ弾だけに、速度に応じた伸縮とカメラ方向への向きを適用します。
    void SetMagmaHopperPresentation(bool enabled) { magmaHopperPresentation_ = enabled; }
    float GetDamage() const { return damage_; }
    const engine::Vector3& GetVelocity() const { return velocity_; }
    DamageType GetDamageType() const { return damageType_; }
    const StatusEffectApplication& GetStatusEffect() const { return statusEffect_; }

  private:
    void Explode();
    float explosionRadius_ = 0.0f;
    engine::Vector3 velocity_ = {0.0f, 0.0f, 0.0f};
    float lifeTimer_ = 0.0f;
    float gravity_ = 0.0f;
    float damage_ = 1.0f;
    DamageType damageType_ = DamageType::Physical;
    StatusEffectApplication statusEffect_;
    std::string trailPreset_;
    std::string trailSecondaryPreset_;
    std::string impactPreset_;
    std::string impactSecondaryPreset_;
    float trailInterval_ = 0.05f;
    float trailTimer_ = 0.0f;
    float trailSpeedScale_ = 1.0f;
    bool trailReverseDirection_ = false;
    bool impactDirected_ = true;
    bool impactEmitted_ = false;
    bool isDead_ = false;
    bool magmaHopperPresentation_ = false;

    void EmitTrailVfx();
    void EmitImpactVfx();
};
