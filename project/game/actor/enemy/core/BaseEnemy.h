#pragma once
#include "Character.h"
#include "AttackTelegraph.h"
#include "EnemyAttackProfile.h"
#include <cstdint>
#include <memory>

// 敵固有の移動・攻撃姿勢へ最後に重ねる、短い被弾リアクションの見た目です。
// scaleは倍率、rotationとoffsetはローカル空間の加算値です。
struct EnemyVisualReactionPose {
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    Vector3 rotation = { 0.0f, 0.0f, 0.0f };
    Vector3 offset = { 0.0f, 0.0f, 0.0f };
    float weight = 0.0f;
};

// 全ての敵が共有する移動・被ダメージ・投げ物理・撃破演出をまとめる基底クラス
// BaseEnemyは、敵共通の追跡対象、徘徊、被弾、投げ物理、撃破演出を持つ基底クラスです。
class BaseEnemy : public Character {
public:
    virtual ~BaseEnemy() = default;

    // モデルと敵用の基本当たり判定を初期化する
        // 敵共通のモデル、Collider、色、攻撃予兆などを初期化します。
virtual void Initialize(Object3dCommon* common, const std::string& modelName);

    // 共通の重力・投げ物理・撃破演出・被ダメージ表示を更新する
        // 被弾タイマー、投げ物理、撃破演出など敵共通の更新を行います。
virtual void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    // プレイヤー攻撃・地形・壁との共通衝突処理
        // 弾、地形、プレイヤーなどとの衝突に応じた共通処理を行います。
virtual bool OnCollision(Object3d* other) override;

    // 追跡や攻撃の基準になるターゲットを登録する
    void SetTarget(Object3d* target) { target_ = target; }

    // シーン演出などで敵のAI・重力・当たり判定を一時停止します。
    void SetDormant(bool dormant);
    bool IsDormant() const { return isDormant_; }

    void SetDetectionRange(float range) { detectionRange_ = range; }
    float GetDetectionRange() const { return detectionRange_; }
        // プレイヤーに持たれている状態を切り替えます。
virtual void SetCarried(bool isCarried);
    bool IsCarried() const { return isCarried_; }
        // 投げられたときの初速度を受け取り、投げ物理状態へ移行します。
virtual void BeginThrown(const Vector3& initialVelocity);
    bool IsThrownPhysics() const { return isThrownPhysics_; }
    bool IsDefeatEffectPlaying() const { return isDefeatEffectPlaying_; }
    // 中ボスなど、フック命中演出は受けても持ち運びへ移行しない敵が上書きします。
    virtual bool IsPullImmune() const { return false; }
    // DamageEventを受けた敵へ、攻撃方向に応じた潰れと反発を開始します。
    void PlayDamageReaction(Object3d* attacker, const Vector3& knockbackVelocity, float damage);
    float GetDamageReactionWeight() const;
    virtual void ExecuteAbility(class Player* player);
    virtual void UpdateCarriedAbility(class Player* player, float deltaTime);

    // 現在の敵タイプに対応する攻撃プロファイルを再読み込みします。
    bool ReloadAttackProfile(std::string* errorMessage = nullptr);
    const EnemyAttackProfile& GetAttackProfile() const { return attackProfile_; }


protected:
    void CaptureReplayCustomState(json& state) const override;
    void RestoreReplayCustomState(const json& state) override;

    // ターゲットが遠い時に使う、スポーン位置基準のランダム徘徊
        // 徘徊目標へ向かう速度を計算します。
Vector3 CalculateWanderVelocity(float deltaTime, float moveSpeed, float radiusScale = 0.65f, float verticalOffset = 0.0f, bool includeVertical = false);
    Vector3 GetWanderTargetPosition(float deltaTime, float radiusScale = 0.65f, float verticalOffset = 0.0f);
    void ResetWanderOrigin();

    // 投げられた直後の物理・回転復帰中は、敵固有AIを止めるために使う
    bool IsThrowRecovering() const { return throwRecoveryTimer_ > 0.0f || isThrownPhysics_ || isThrowRotationRecovering_; }

    // HPが尽きた敵は共通のポップ演出を再生してから消す
    bool ShouldHandleDefeatEffect() const;
        // 円形攻撃予兆を敵共通のAttackTelegraphへ表示します。
void ShowAttackTelegraphCircle(const Vector3& center, float radius, float progress, const Vector4& color);
    void ShowAttackTelegraphDecalCircle(const Vector3& center, float radius, float progress, const Vector4& color, const std::string& texturePath);
    void ShowAttackTelegraphLine(const Vector3& center, const Vector3& direction, float length, float width, float progress, const Vector4& color);
    void TriggerAttackTelegraphCue(const Vector4& color = { 1.0f, 0.05f, 0.02f, 1.0f });
    void HideAttackTelegraph();
    bool UpdateNoticeReaction(float deltaTime, float targetDistance, float detectRange, const Vector3& targetDirection);
    void CancelNoticeReaction();
    void SpawnDefeatCoinDrops();
    const EnemyAttackDefinition& GetAttackDefinition(const std::string& attackId) const;
    EnemyVisualReactionPose GetDamageReactionPose() const;
    void ApplyDamageReactionPose(
        Vector3& scale,
        Vector3& rotation,
        Vector3* offset = nullptr,
        float intensity = 1.0f) const;
    void UpdateDamageFeedbackTimers(float deltaTime);
    virtual void OnSlamImpact(const Vector3& impactPosition, float impactSpeed);

    Object3d* target_ = nullptr; // 追いかける対象。基本的にはプレイヤー。
    float detectionRange_ = 20.0f; // ターゲットを検知して攻撃AIへ入る距離。
    float damageCooldownTimer_ = 0.0f; // 連続ヒットを防ぐ短い無敵時間。
    float colorResetTimer_ = 0.0f;     // 被弾時の赤色を元に戻すタイマー。
    EnemyAttackProfile attackProfile_;
    Vector4 defaultColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // 被弾演出後に戻す基準色。
    bool isCarried_ = false;
    bool isDormant_ = false;
    float throwRecoveryTimer_ = 0.0f;
    bool isThrownPhysics_ = false;

private:
    // 徘徊の基準位置と次の目標地点を管理する
    void CaptureWanderOrigin();
    void PickWanderTarget(float radiusScale, float verticalOffset);
    float NextWanderRandom();
    float GetWanderRadius(float radiusScale) const;

    // 持ち上げ後に投げられた敵の移動・衝突・回転復帰
    void UpdateThrownPhysics(float deltaTime);
    void HandleThrownCollision(const CollisionInfo& info, uint32_t attribute);
    void EndThrownPhysics();
    void StartThrowRecovery();
    void UpdateThrowRecovery(float deltaTime);

    // 高速で地面へ叩きつけられた時の範囲ダメージと演出
    void SpawnSlamImpactEffect(const Vector3& impactPosition, float impactSpeed);
    void DamageSlamTargets(const Vector3& impactPosition, float impactSpeed);
    void BeginNoticeReaction();
    void EndNoticeReaction(bool restoreVisual = true);
    void EnsureNoticeMarkObject();
    void UpdateNoticeMark(float deltaTime, float progress);

    // 撃破時の消滅アニメーションとパーティクル
        // 撃破時の縮小、色変化、パーティクル演出を開始します。
void BeginDefeatEffect();
    void UpdateDefeatEffect(float deltaTime);
    void SpawnDefeatStartParticles();
    void SpawnDefeatLoopParticles();
    class ParticleSystem* GetCurrentParticleSystem() const;

    Vector3 wanderOrigin_ = { 0.0f, 0.0f, 0.0f };
    Vector3 wanderTarget_ = { 0.0f, 0.0f, 0.0f };
    float wanderWaitTimer_ = 0.0f;
    float wanderRetargetTimer_ = 0.0f;
    uint32_t wanderSeed_ = 0x12345678u;
    bool hasWanderOrigin_ = false;
    Vector3 thrownAngularVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 throwRecoveryStartRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 throwRecoveryTargetRotation_ = { 0.0f, 0.0f, 0.0f };
    float thrownTimer_ = 0.0f;
    float thrownSettleTimer_ = 0.0f;
    float throwRecoveryRotateTimer_ = 0.0f;
    float slamImpactCooldownTimer_ = 0.0f;
    int slamImpactCount_ = 0;
    bool isThrowRotationRecovering_ = false;
    bool isDefeatEffectPlaying_ = false;
    bool isDefeatEffectFinished_ = false;
    bool hasSpawnedDefeatCoinDrops_ = false;
    std::unique_ptr<AttackTelegraph> attackTelegraph_;
    std::unique_ptr<Object3d> noticeMarkObject_;
    bool wasTargetDetected_ = false;
    bool isNoticeReactionActive_ = false;
    float noticeReactionTimer_ = 0.0f;
    float noticeReactionCooldown_ = 0.0f;
    float noticeMarkYaw_ = 0.0f;
    Vector3 noticeBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector4 noticeBaseColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float defeatEffectTimer_ = 0.0f;
    float defeatEffectParticleTimer_ = 0.0f;
    float damageReactionTimer_ = 0.0f;
    float damageReactionDuration_ = 0.0f;
    float damageReactionStrength_ = 0.0f;
    Vector3 damageReactionLocalDirection_ = { 0.0f, 0.0f, -1.0f };
    Vector3 defeatBasePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 defeatBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector4 defeatBaseColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
};
