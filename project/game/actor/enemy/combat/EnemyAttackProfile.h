#pragma once

#include "engine/utility/math/Math.h"

#include <string>
#include <vector>
enum class AttackAbilityPhase {
    Ready,
    Windup,
    Active,
    Recovery,
    Cooldown,
};


// 敵の攻撃1種類分を表す、実行時とEditorで共有するデータです。
struct EnemyAttackDefinition {
    std::string id;
    std::string displayName;

    float minRange = 0.0f;
    float maxRange = 10.0f;
    std::string hitShape = "sphere";
    Vector3 hitOffset = { 0.0f, 0.0f, 0.0f };
    Vector3 hitSize = { 1.0f, 1.0f, 1.0f };
    float radius = 1.0f;

    float windupDuration = 0.3f;
    float activeDuration = 0.2f;
    float recoveryDuration = 0.3f;
    float cooldown = 1.0f;
    float cancelWindowStart = 0.0f;
    float cancelWindowEnd = 0.0f;
    float warningLeadTime = 0.15f;

    float damage = 1.0f;
    Vector3 knockbackVelocity = { 0.0f, 0.0f, 0.0f };
    float invincibilityDuration = 0.0f;
    std::string statusEffectType;
    float statusDuration = 0.0f;
    float statusTickInterval = 0.5f;
    float statusTickDamage = 0.0f;
    std::string statusVfx;
    float minSpeed = 0.0f;
    float maxSpeed = 0.0f;
    float lifetime = 0.0f;

    float recommendedTargetDistance = 5.0f;
    float previewDuration = 4.0f;

    std::string animation;
    std::string windupVfx;
    std::string activeVfx;
    std::string impactVfx;
    std::string audioCue;
    std::string feedbackCue;

    float GetPhaseDuration(AttackAbilityPhase phase) const;
    float GetPhaseProgressFromElapsed(AttackAbilityPhase phase, float elapsed) const;
    float GetPhaseProgressFromRemaining(AttackAbilityPhase phase, float remaining) const;
    float GetActionDuration() const;
    float GetTotalDuration() const;
    bool IsCancelWindowOpen(float elapsedActionTime) const;
    bool HasConfiguredKnockback() const;
    Vector3 ResolveKnockback(const Vector3& forward, const Vector3& legacyFallback) const;
    float ResolveInvincibilityDuration(float legacyFallback = 1.0f) const;
};

// 敵1種類に含まれる攻撃をまとめたJSONアセットです。

using AttackAbilityDefinition = EnemyAttackDefinition;

struct AttackAbilityUpdateResult {
    AttackAbilityPhase previousPhase = AttackAbilityPhase::Ready;
    AttackAbilityPhase currentPhase = AttackAbilityPhase::Ready;
    bool enteredActive = false;
    bool enteredRecovery = false;
    bool enteredCooldown = false;
    bool becameReady = false;
};

/// 攻撃ごとの重複タイマーを減らす、共通のフェーズ実行器です。
class AttackAbilityRuntime {
public:
    bool Start(const AttackAbilityDefinition& definition);
    AttackAbilityUpdateResult Update(float deltaTime);
    void Cancel(bool enterCooldown = true);
    void Reset();

    AttackAbilityPhase GetPhase() const { return phase_; }
    float GetPhaseProgress() const;
    float GetElapsedActionTime() const;
    bool IsReady() const { return phase_ == AttackAbilityPhase::Ready; }
    bool IsHitWindowOpen() const { return phase_ == AttackAbilityPhase::Active; }
    bool CanCancel() const;
    const AttackAbilityDefinition* GetDefinition() const { return hasDefinition_ ? &definition_ : nullptr; }

private:
    AttackAbilityPhase NextPhase(AttackAbilityPhase phase) const;

    AttackAbilityDefinition definition_{};
    AttackAbilityPhase phase_ = AttackAbilityPhase::Ready;
    float phaseTime_ = 0.0f;
    bool hasDefinition_ = false;
};
// 読み込みに失敗した場合も既定値を保持し、既存シーンの動作を継続します。
class EnemyAttackProfile {
public:
    static EnemyAttackProfile CreateDefault(const std::string& enemyType);
    static std::string GetDefaultPath(const std::string& enemyType);
    static bool LoadCachedForEnemy(const std::string& enemyType, EnemyAttackProfile& destination, std::string* errorMessage = nullptr);
    static void InvalidateCache(const std::string& enemyType = {});

    bool LoadForEnemy(const std::string& enemyType, std::string* errorMessage = nullptr);
    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);
    bool SaveToFile(const std::string& path, std::string* errorMessage = nullptr) const;

    const EnemyAttackDefinition* FindAttack(const std::string& attackId) const;
    EnemyAttackDefinition* FindAttack(const std::string& attackId);
    void Sanitize();

    int version = 1;
    std::string enemyType;
    std::string displayName;
    std::vector<EnemyAttackDefinition> attacks;
};

using AttackAbilityAsset = EnemyAttackProfile;
