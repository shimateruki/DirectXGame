#define NOMINMAX
#include "game/actor/combat/CombatGroundQuery.h"
#include "game/actor/combat/CombatMath.h"
#include "game/actor/enemy/boss/magma/EnemyMagmaSlime.Internal.h"
#include "game/actor/enemy/boss/core/BossChantVisual.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshRenderer.h"
#include "SceneManager.h"
#include "game/actor/enemy/animation/SlimeBounceAnimator.h"
#include "game/actor/enemy/animation/BossGroundHopVfx.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

using namespace EnemyMagmaSlimeImplementation;

// 遭遇制御中は通常AIに先行し、Editor復帰・待機・登場の状態を処理します。
bool EnemyMagmaSlime::UpdateEncounterState(float deltaTime) {
    if (!IsEncounterControlled()) {
        encounterState_ = EncounterState::Normal;
        encounterInitializedForPlay_ = false;
        return false;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->IsPlaying()) {
        ResetEncounterStateForEditor();
        return true;
    }

    if (!encounterInitializedForPlay_) {
        InitializeEncounterState();
    }
    if (encounterState_ == EncounterState::Dormant) {
        SetVelocity({0.0f, 0.0f, 0.0f});
        ClearTransientVisuals();
        HideAttackTelegraph();
        return true;
    }
    if (encounterState_ != EncounterState::Appearing) {
        return false;
    }

    encounterTimer_ += (std::max)(0.0f, deltaTime);
    idleTimer_ += (std::max)(0.0f, deltaTime);
    const float progress = std::clamp(encounterTimer_ / GetEncounterAppearanceDuration(), 0.0f, 1.0f);
    const float eased = SmoothStep01(progress);
    const float landingPulse = std::sin(progress * kPi) * 0.16f;
    const float horizontalScale = 0.24f + eased * 0.76f + landingPulse;
    const float verticalScale = 0.12f + eased * 0.88f + landingPulse * 0.55f;

    // 小さく浮いた姿から配置時の大きさへ戻す間、衝突判定はまだ有効にしません。
    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetTranslate({
        encounterBasePosition_.x,
        encounterBasePosition_.y + (1.0f - eased) * kEncounterAppearanceRise,
        encounterBasePosition_.z,
    });
    SetRotation({
        encounterBaseRotation_.x,
        encounterBaseRotation_.y + (1.0f - eased) * kPi * 0.36f,
        encounterBaseRotation_.z,
    });
    SetScale({
        encounterBaseScale_.x * horizontalScale,
        encounterBaseScale_.y * verticalScale,
        encounterBaseScale_.z * horizontalScale,
    });
    SetColor({
        encounterBaseColor_.x,
        encounterBaseColor_.y,
        encounterBaseColor_.z,
        (std::max)(0.01f, eased),
    });
    SetEmissive(encounterBaseEmissive_ + (1.0f - std::abs(progress * 2.0f - 1.0f)) * 1.75f);
    SyncCollisionRadius();

    if (progress >= 1.0f) {
        FinishEncounterAppearance();
    }
    return true;
}

// PlayとEditorの切り替えに備え、配置時の姿勢・マテリアル・衝突設定を保持します。
void EnemyMagmaSlime::CaptureEncounterAuthoredState(bool refreshVisualTransform) {
    if (encounterAuthoredStateCaptured_ && !refreshVisualTransform) {
        return;
    }

    encounterBasePosition_ = GetTransform()->translate;
    encounterBaseScale_ = GetScale();
    encounterBaseRotation_ = GetRotation();
    encounterBaseColor_ = GetColor();
    encounterBaseEmissive_ = GetEmissive();
    encounterMaterialType_ = GetMaterialType();
    const uint32_t collisionAttribute = GetCollisionAttribute();
    const uint32_t collisionMask = GetCollisionMask();
    // 待機中に保存された衝突無効値を、登場後へ持ち越さないよう補います。
    if (collisionAttribute != 0) {
        encounterCollisionAttribute_ = collisionAttribute;
    } else if (!encounterAuthoredStateCaptured_) {
        encounterCollisionAttribute_ = CollisionCategories::Get("enemy");
    }
    if (collisionMask != 0) {
        encounterCollisionMask_ = collisionMask;
    } else if (!encounterAuthoredStateCaptured_) {
        encounterCollisionMask_ = CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet");
    }
    baseScale_ = encounterBaseScale_;
    hasBaseScale_ = true;
    encounterAuthoredStateCaptured_ = true;
}

// Play開始時に攻撃候補をリセットし、配置設定で登場開始の有無を決めます。
void EnemyMagmaSlime::InitializeEncounterState() {
    encounterInitializedForPlay_ = true;
    CaptureEncounterAuthoredState();
    attackCycleIndex_ = attackCycle_.size();
    lastAutomaticAttack_ = AttackKind::None;
    const bool startActive = param_.has_value() && param_->startActive;
    if (startActive || encounterRequestedActive_) {
        BeginEncounterAppearance();
    } else {
        ApplyDormantEncounterState();
    }
}

// 再生中の演出を片付け、Editorで調整した配置値へ戻します。
void EnemyMagmaSlime::ResetEncounterStateForEditor() {
    if (encounterInitializedForPlay_) {
        SetTranslate(encounterBasePosition_);
        SetScale(encounterBaseScale_);
        SetRotation(encounterBaseRotation_);
        SetColor(encounterBaseColor_);
        SetMaterialType(encounterMaterialType_);
        SetEmissive(encounterBaseEmissive_);
        SyncCollisionRadius();
    }
    CaptureEncounterAuthoredState(true);
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    encounterInitializedForPlay_ = false;
    encounterRequestedActive_ = false;
    attackCycleIndex_ = attackCycle_.size();
    lastAutomaticAttack_ = AttackKind::None;
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    ClearTransientVisuals();
    HideAttackTelegraph();
}

// 登場中の衝突を止め、開始位置に出現VFXを再生します。
void EnemyMagmaSlime::BeginEncounterAppearance() {
    encounterState_ = EncounterState::Appearing;
    encounterTimer_ = 0.0f;
    attackCooldown_ = 1.1f;
    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({0.0f, 0.0f, 0.0f});
    ClearTransientVisuals();
    HideAttackTelegraph();
    VFXSequencer::PlayOneShot(kEncounterAppearanceSequence, encounterBasePosition_ + Vector3{0.0f, 0.18f, 0.0f},
                              {1.55f, 1.55f, 1.55f});
}

void EnemyMagmaSlime::ApplyDormantEncounterState() {
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    SetTranslate(encounterBasePosition_);
    SetScale(encounterBaseScale_);
    SetRotation(encounterBaseRotation_);
    SetColor(encounterBaseColor_);
    SetMaterialType(encounterMaterialType_);
    SetEmissive(encounterBaseEmissive_);
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    ClearTransientVisuals();
    HideAttackTelegraph();
}

// 登場完了時に配置時の衝突設定を戻し、戦闘状態へ切り替えます。
void EnemyMagmaSlime::FinishEncounterAppearance() {
    encounterState_ = EncounterState::Active;
    encounterTimer_ = 0.0f;
    SetTranslate(encounterBasePosition_);
    SetScale(encounterBaseScale_);
    SetRotation(encounterBaseRotation_);
    SetColor(encounterBaseColor_);
    SetMaterialType(encounterMaterialType_);
    SetEmissive(encounterBaseEmissive_);
    SetCollisionAttribute(encounterCollisionAttribute_);
    SetCollisionMask(encounterCollisionMask_);
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetIsVisible(true);
    SyncCollisionRadius();
}

bool EnemyMagmaSlime::IsEncounterControlled() const {
    return param_.has_value() && param_->actionMode == kEncounterControlledActionMode;
}

float EnemyMagmaSlime::GetEncounterAppearanceDuration() const {
    return param_.has_value() ? (std::clamp)(param_->shakeDuration, 0.45f, 3.0f) : kDefaultEncounterAppearanceDuration;
}

bool EnemyMagmaSlime::IsEncounterHudActive() const {
    if (!IsEncounterControlled() || isDead) {
        return false;
    }
    return encounterState_ == EncounterState::Appearing || encounterState_ == EncounterState::Active;
}

float EnemyMagmaSlime::GetEncounterCurrentHp() const {
    return param_.has_value() ? (std::max)(0.0f, param_->hp) : 0.0f;
}

float EnemyMagmaSlime::GetEncounterMaximumHp() const {
    return param_.has_value() ? (std::max)(1.0f, param_->maxHp) : 1.0f;
}

float EnemyMagmaSlime::GetEncounterAppearanceProgress() const {
    if (encounterState_ == EncounterState::Active) {
        return 1.0f;
    }
    if (encounterState_ != EncounterState::Appearing) {
        return 0.0f;
    }
    return std::clamp(encounterTimer_ / GetEncounterAppearanceDuration(), 0.0f, 1.0f);
}
