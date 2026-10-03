#include "game/actor/enemy/boss/prism/EnemyPrismSlime.Internal.h"

using namespace EnemyPrismSlimeImplementation;

// 元の更新順序と判定を維持して、担当処理をまとめています。
bool EnemyPrismSlime::UpdateEncounterState(float deltaTime) {
    if (!IsEncounterControlled()) {
        encounterState_ = EncounterState::Normal;
        encounterInitializedForPlay_ = false;
        return false;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    const bool isPlaying = sceneManager && sceneManager->IsPlaying();
    if (!isPlaying) {
        ResetEncounterStateForEditor();
        return true;
    }

    if (!encounterInitializedForPlay_) {
        InitializeEncounterState();
    }

    if (encounterState_ == EncounterState::Dormant) {
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        ClearPrismAttackVisuals();
        HideAttackTelegraph();
        return true;
    }

    if (encounterState_ != EncounterState::Appearing) {
        return false;
    }

    encounterTimer_ += (std::max)(0.0f, deltaTime);
    idleTimer_ += (std::max)(0.0f, deltaTime);
    const float duration = GetEncounterAppearanceDuration();
    const float progress = std::clamp(encounterTimer_ / duration, 0.0f, 1.0f);
    const float eased = SmoothStep01(progress);
    const float settlePulse = std::sin(progress * kPi) * 0.13f;

    SetIsVisible(true);
    // 登場演出中も地面との衝突だけは維持し、床抜けを防ぎます。
    SetCollisionAttribute(kPrismActiveCollisionAttribute);
    SetCollisionMask(kPrismAppearanceCollisionMask);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetMaterialType(4);
    SetTranslate({
        encounterBasePosition_.x,
        encounterBasePosition_.y + (1.0f - eased) * kEncounterAppearanceRise,
        encounterBasePosition_.z,
    });
    const float scaleRate = 0.28f + eased * 0.72f + settlePulse;
    SetScale({
        encounterBaseScale_.x * scaleRate,
        encounterBaseScale_.y * (0.18f + eased * 0.82f + settlePulse * 0.72f),
        encounterBaseScale_.z * scaleRate,
    });
    SetColor({
        encounterBaseColor_.x,
        encounterBaseColor_.y,
        encounterBaseColor_.z,
        (std::max)(0.015f, eased),
    });
    SetEmissive(1.35f + (1.0f - std::abs(progress * 2.0f - 1.0f)) * 2.7f);
    SyncCollisionRadius();

    if (progress >= 1.0f) {
        FinishEncounterAppearance();
    }
    return true;
}

void EnemyPrismSlime::CaptureEncounterAuthoredState(bool refreshVisualTransform) {
    if (encounterAuthoredStateCaptured_ && !refreshVisualTransform) {
        return;
    }

    encounterBasePosition_ = GetTransform()->translate;
    encounterBaseScale_ = GetScale();
    encounterBaseColor_ = GetColor();
    encounterBaseEmissive_ = GetEmissive();
    encounterMaterialType_ = GetMaterialType();

    const uint32_t collisionAttribute = GetCollisionAttribute();
    const uint32_t collisionMask = GetCollisionMask();
    // 待機状態を保存したシーンでは属性とマスクが0になっています。
    // その値を「通常時の設定」として記憶すると、登場後も床をすり抜けるため既定値へ戻します。
    if (collisionAttribute != 0) {
        encounterCollisionAttribute_ = collisionAttribute;
    } else if (!encounterAuthoredStateCaptured_) {
        encounterCollisionAttribute_ = kPrismActiveCollisionAttribute;
    }
    if (collisionMask != 0) {
        encounterCollisionMask_ = collisionMask;
    } else if (!encounterAuthoredStateCaptured_) {
        encounterCollisionMask_ = kPrismActiveCollisionMask;
    }

    baseScale_ = encounterBaseScale_;
    hasBaseScale_ = true;
    encounterAuthoredStateCaptured_ = true;
}

void EnemyPrismSlime::InitializeEncounterState() {
    encounterInitializedForPlay_ = true;
    CaptureEncounterAuthoredState();
    attackCycleCount_ = 0;
    attackCycleIndex_ = 0;
    lastAutomaticAttack_ = AttackKind::None;

    const bool startActive = param_.has_value() && param_->startActive;
    if (startActive || encounterRequestedActive_) {
        BeginEncounterAppearance();
    } else {
        ApplyDormantEncounterState();
    }
}

void EnemyPrismSlime::ResetEncounterStateForEditor() {
    if (encounterInitializedForPlay_) {
        SetTranslate(encounterBasePosition_);
        SetScale(encounterBaseScale_);
        SetColor(encounterBaseColor_);
        SetMaterialType(encounterMaterialType_);
        SetEmissive(encounterBaseEmissive_);
        SyncCollisionRadius();
    }

    // 非再生中も配置値は更新できるよう保持しつつ、ゲーム画面には待機中ボスを出しません。
    CaptureEncounterAuthoredState(true);
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    encounterInitializedForPlay_ = false;
    encounterRequestedActive_ = false;
    attackCycleCount_ = 0;
    attackCycleIndex_ = 0;
    lastAutomaticAttack_ = AttackKind::None;
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    ClearPrismAttackVisuals();
    HideAttackTelegraph();
}

void EnemyPrismSlime::BeginEncounterAppearance() {
    encounterState_ = EncounterState::Appearing;
    encounterTimer_ = 0.0f;
    attackCooldown_ = 1.0f;
    SetIsVisible(true);
    // 演出中は攻撃・プレイヤー接触を無効にし、地面との衝突だけ有効にします。
    SetCollisionAttribute(kPrismActiveCollisionAttribute);
    SetCollisionMask(kPrismAppearanceCollisionMask);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    ClearPrismAttackVisuals();
    HideAttackTelegraph();

    Vector3 effectPosition = encounterBasePosition_;
    effectPosition.y += 0.15f;
    VFXSequencer::PlayOneShot(
        kEncounterAppearanceSequence,
        effectPosition,
        { 1.65f, 1.65f, 1.65f });
}

void EnemyPrismSlime::ApplyDormantEncounterState() {
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    SetTranslate(encounterBasePosition_);
    SetScale(encounterBaseScale_);
    SetColor(encounterBaseColor_);
    SetMaterialType(encounterMaterialType_);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    repositionTimer_ = 0.0f;
    repositionDuration_ = 0.0f;
    repositionJumpPending_ = false;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    ClearPrismAttackVisuals();
    HideAttackTelegraph();
}

void EnemyPrismSlime::FinishEncounterAppearance() {
    encounterState_ = EncounterState::Active;
    encounterTimer_ = 0.0f;
    SetTranslate(encounterBasePosition_);
    SetScale(encounterBaseScale_);
    SetColor(encounterBaseColor_);
    SetMaterialType(encounterMaterialType_);
    SetEmissive(encounterBaseEmissive_);
    SetCollisionAttribute(encounterCollisionAttribute_);
    SetCollisionMask(encounterCollisionMask_);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetIsVisible(true);
    SyncCollisionRadius();
}

bool EnemyPrismSlime::IsEncounterControlled() const {
    return param_.has_value() && param_->actionMode == kEncounterControlledActionMode;
}

float EnemyPrismSlime::GetEncounterAppearanceDuration() const {
    return param_.has_value()
        ? (std::clamp)(param_->shakeDuration, 0.35f, 3.0f)
        : kDefaultEncounterAppearanceDuration;
}

bool EnemyPrismSlime::IsEncounterHudActive() const {
    if (!IsEncounterControlled() || isDead) {
        return false;
    }
    return encounterState_ == EncounterState::Appearing || encounterState_ == EncounterState::Active;
}

float EnemyPrismSlime::GetEncounterCurrentHp() const {
    return param_.has_value() ? (std::max)(0.0f, param_->hp) : 0.0f;
}

float EnemyPrismSlime::GetEncounterMaximumHp() const {
    return param_.has_value() ? (std::max)(1.0f, param_->maxHp) : 1.0f;
}

float EnemyPrismSlime::GetEncounterAppearanceProgress() const {
    if (encounterState_ == EncounterState::Active) {
        return 1.0f;
    }
    if (encounterState_ != EncounterState::Appearing) {
        return 0.0f;
    }
    return std::clamp(encounterTimer_ / GetEncounterAppearanceDuration(), 0.0f, 1.0f);
}

void EnemyPrismSlime::TriggerDebugDefeat() {
    if (!param_.has_value() || isDead || IsDefeatEffectPlaying()) {
        return;
    }

    param_->hp = 0.0f;
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    attackTimer_ = 0.0f;
    repositionTimer_ = 0.0f;
    repositionDuration_ = 0.0f;
    repositionJumpPending_ = false;
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    ClearPrismAttackVisuals();
    HideAttackTelegraph();
}

