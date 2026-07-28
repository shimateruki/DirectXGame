#pragma once
#include "Object3d.h"
#include "Event.h"

#include <string>

// 一定時間で消え、衝突時にも消滅する汎用弾オブジェクト
// Bulletは、速度と寿命を持って飛び、衝突時に消滅する基本弾オブジェクトです。
class Bullet : public Object3d {
public:
        // 弾用モデル、Collider、初期状態を設定します。
void Initialize(Object3dCommon* common) override;
        // 速度に沿って移動し、寿命タイマーを進めます。
void Update(float deltaTime) override;
        // 衝突相手に応じて弾の消滅やダメージ通知を行います。
bool OnCollision(Object3d* other) override;

    /// <summary>
    /// 弾の初期位置、速度、寿命、当たり判定属性を設定して発射する。
    /// </summary>
        // 発射位置、速度、寿命、衝突属性を設定して弾を有効化します。
void Fire(const Vector3& position, const Vector3& velocity,
       float life, uint32_t attribute, uint32_t mask, float damage = 1.0f,
       const StatusEffectApplication& statusEffect = {},
       DamageType damageType = DamageType::Physical);

    // 弾へ追従する軌跡と、衝突時に一度だけ再生する演出を設定します。
    void ConfigureVfx(const std::string& trailPreset, const std::string& impactPreset,
        float trailInterval, float trailSpeedScale);

    bool IsDead() const { return isDead_; }
    void Expire(bool playImpact = false);
    float GetDamage() const { return damage_; }
    const Vector3& GetVelocity() const { return velocity_; }
    DamageType GetDamageType() const { return damageType_; }
    const StatusEffectApplication& GetStatusEffect() const { return statusEffect_; }

private:
    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f };
    float lifeTimer_ = 0.0f;
    float damage_ = 1.0f;
    DamageType damageType_ = DamageType::Physical;
    StatusEffectApplication statusEffect_;
    std::string trailPreset_;
    std::string impactPreset_;
    float trailInterval_ = 0.05f;
    float trailTimer_ = 0.0f;
    float trailSpeedScale_ = 1.0f;
    bool impactEmitted_ = false;
    bool isDead_ = false;

    void EmitTrailVfx();
    void EmitImpactVfx();
};
