#include "game/actor/player/Player.Internal.h"

#include "VFXSequencer.h"

using namespace PlayerImplementation;

// 元の更新順序と判定を維持して、担当処理をまとめています。
// 敵の運搬・吸収からコピー開始までを扱います。途中キャンセルでも敵とプレイヤーの見た目を戻せる順序にします。

void Player::SetCarriedEnemy(Object3d* enemy) {
    if (carriedEnemy_ == enemy) {
        if (enemy && !hasCarriedEnemyBaseScale_) {
            carriedEnemyBaseScale_ = GetStableEnemyCarryScale(enemy);
            const Transform* enemyTransform = enemy->GetTransform();
            if (enemyTransform) {
                carriedEnemyBaseRotation_ = enemyTransform->rotate;
                carriedEnemyBaseQuaternion_ = enemyTransform->quaternion;
                carriedEnemyBaseQuaternionMaster_ = enemyTransform->isQuaternionMaster;
            }
            hasCarriedEnemyBaseScale_ = true;
        }
        return;
    }

    carriedEnemy_ = enemy;
    if (enemy) {
        carriedEnemyBaseScale_ = GetStableEnemyCarryScale(enemy);
        const Transform* enemyTransform = enemy->GetTransform();
        if (enemyTransform) {
            carriedEnemyBaseRotation_ = enemyTransform->rotate;
            carriedEnemyBaseQuaternion_ = enemyTransform->quaternion;
            carriedEnemyBaseQuaternionMaster_ = enemyTransform->isQuaternionMaster;
        }
        hasCarriedEnemyBaseScale_ = true;
    } else {
        carriedEnemyBaseScale_ = {1.0f, 1.0f, 1.0f};
        carriedEnemyBaseRotation_ = {0.0f, 0.0f, 0.0f};
        carriedEnemyBaseQuaternion_ = {0.0f, 0.0f, 0.0f, 1.0f};
        carriedEnemyBaseQuaternionMaster_ = true;
        hasCarriedEnemyBaseScale_ = false;
    }
}

void Player::ReleaseCarriedEnemy(bool restorePose) {
    if (absorbEffectActive_) {
        CancelAbsorbEffect(restorePose);
    }

    Object3d* enemy = carriedEnemy_;
    if (!enemy) {
        return;
    }

    if (auto* enemyBase = dynamic_cast<BaseEnemy*>(enemy)) {
        enemyBase->SetCarried(false);
    } else {
        enemy->SetCollisionAttribute(CollisionCategories::Get("enemy"));
        enemy->SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_bullet") | CollisionCategories::Get("player_attack"));
    }

    enemy->SetIsVisible(true);
    if (restorePose) {
        enemy->SetScale(hasCarriedEnemyBaseScale_ ? carriedEnemyBaseScale_ : enemy->GetScale());
        enemy->SetEulerAngles(carriedEnemyBaseRotation_);
        enemy->SetQuaternion(carriedEnemyBaseQuaternion_);
        enemy->SetQuaternionMaster(carriedEnemyBaseQuaternionMaster_);
        enemy->UpdateLocalMatrix();
        enemy->UpdateWorldMatrix();
    }

    SetCarriedEnemy(nullptr);
}

bool Player::ShouldPlayAbsorbEffect(BaseEnemy* enemy) const {
    if (!enemy) {
        return false;
    }

    const EnemyMorphType type = ResolveEnemyMorphType(enemy->GetEnemyType());
    return type == EnemyMorphType::Slime || type == EnemyMorphType::FireSlime;
}

void Player::BeginAbsorbEffect(BaseEnemy* enemy) {
    if (!enemy) {
        return;
    }

    pendingAbsorbType_ = ResolveEnemyMorphType(enemy->GetEnemyType());
    if (pendingAbsorbType_ != EnemyMorphType::Slime && pendingAbsorbType_ != EnemyMorphType::FireSlime) {
        StartEnemyMorph(enemy);
        return;
    }

    pendingAbsorbEnemy_ = enemy;
    pendingAbsorbEnemyBaseScale_ = hasCarriedEnemyBaseScale_ ? carriedEnemyBaseScale_ : GetStableEnemyCarryScale(enemy);
    pendingAbsorbEnemyStartPos_ = enemy->GetWorldPosition();
    pendingAbsorbTint_ = pendingAbsorbType_ == EnemyMorphType::Slime ? Vector4{1.0f, 0.42f, 0.82f, 1.0f}
                                                                     : GetEnemyMorphTint(pendingAbsorbType_);
    pendingAbsorbPlayerBaseColor_ = GetColor();
    absorbEffectTimer_ = 0.0f;
    absorbEffectEmitTimer_ = 0.0f;
    absorbEffectActive_ = true;

    enemy->SetCarried(true);
    enemy->SetCollisionAttribute(0);
    enemy->SetCollisionMask(0);
    enemy->SetIsVisible(true);
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetScale({2.35f, 1.65f, 2.35f});
}

void Player::UpdateAbsorbEffect(float deltaTime) {
    if (!absorbEffectActive_) {
        return;
    }
    if (!pendingAbsorbEnemy_) {
        CancelAbsorbEffect(false);
        return;
    }
    if (deltaTime <= 0.0f) {
        return;
    }

    absorbEffectTimer_ += deltaTime;
    absorbEffectEmitTimer_ -= deltaTime;

    const float t = std::clamp(absorbEffectTimer_ / kAbsorbEffectDuration, 0.0f, 1.0f);
    const float ease = t * t * (3.0f - 2.0f * t);
    const Vector3 playerPos = GetWorldPosition();
    const Vector3 targetPos = playerPos + Vector3{0.0f, 1.35f, 0.0f};

    Vector3 absorbPos{Math::Lerp(pendingAbsorbEnemyStartPos_.x, targetPos.x, ease),
                      Math::Lerp(pendingAbsorbEnemyStartPos_.y, targetPos.y, ease),
                      Math::Lerp(pendingAbsorbEnemyStartPos_.z, targetPos.z, ease)};
    absorbPos.y += std::sin(t * kPi) * 0.22f;

    const float scaleRate = Math::Lerp(1.0f, 0.06f, ease);
    const float squash = std::sin(t * kPi) * 0.22f;
    {
        pendingAbsorbEnemy_->SetTranslate(absorbPos);
        pendingAbsorbEnemy_->SetScale({pendingAbsorbEnemyBaseScale_.x * scaleRate * (1.0f + squash),
                                 pendingAbsorbEnemyBaseScale_.y * scaleRate * (1.0f - squash * 0.75f),
                                 pendingAbsorbEnemyBaseScale_.z * scaleRate * (1.0f + squash)});
        pendingAbsorbEnemy_->SetEulerX(pendingAbsorbEnemy_->GetRotation().x + 9.0f * deltaTime);
        pendingAbsorbEnemy_->SetEulerY(pendingAbsorbEnemy_->GetRotation().y + 18.0f * deltaTime);
        pendingAbsorbEnemy_->SetEulerZ(pendingAbsorbEnemy_->GetRotation().z + 7.0f * deltaTime);
        pendingAbsorbEnemy_->SetQuaternionMaster(false);
        pendingAbsorbEnemy_->UpdateLocalMatrix();
        pendingAbsorbEnemy_->UpdateWorldMatrix();
    }

    pendingAbsorbEnemy_->SetColor(pendingAbsorbTint_);
    pendingAbsorbEnemy_->SetCollisionAttribute(0);
    pendingAbsorbEnemy_->SetCollisionMask(0);

    const float flash = std::sin(t * kPi);
    const float colorRate = 0.35f + flash * 0.55f;
    Vector4 flashColor{std::clamp(pendingAbsorbPlayerBaseColor_.x * (1.0f - colorRate) +
                                      pendingAbsorbTint_.x * colorRate + flash * 0.18f,
                                  0.0f, 1.0f),
                       std::clamp(pendingAbsorbPlayerBaseColor_.y * (1.0f - colorRate) +
                                      pendingAbsorbTint_.y * colorRate + flash * 0.18f,
                                  0.0f, 1.0f),
                       std::clamp(pendingAbsorbPlayerBaseColor_.z * (1.0f - colorRate) +
                                      pendingAbsorbTint_.z * colorRate + flash * 0.18f,
                                  0.0f, 1.0f),
                       1.0f};
    SetColor(flashColor);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetColor(flashColor);
        }
    }

    const float bodyPulse = std::sin(t * kPi);
    SetScale({2.0f + bodyPulse * 0.55f, 2.0f - bodyPulse * 0.38f, 2.0f + bodyPulse * 0.55f});

    if (particleSystem_ && absorbEffectEmitTimer_ <= 0.0f) {
        Vector3 toPlayer = targetPos - absorbPos;
        const float length = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y + toPlayer.z * toPlayer.z);
        if (length > 0.001f) {
            toPlayer = toPlayer / length;
        } else {
            toPlayer = {0.0f, 1.0f, 0.0f};
        }

        const int count = pendingAbsorbType_ == EnemyMorphType::FireSlime ? 14 : 12;
        particleSystem_->SpawnParticles(absorbPos, count, 1.4f, &toPlayer, 26.0f, pendingAbsorbTint_,
                                        {pendingAbsorbTint_.x, pendingAbsorbTint_.y, pendingAbsorbTint_.z, 0.0f}, 0.08f,
                                        0.24f, 0.34f, 0.035f);
        absorbEffectEmitTimer_ = kAbsorbEffectParticleInterval;
    }

    if (absorbEffectTimer_ >= kAbsorbEffectDuration) {
        FinishAbsorbEffect();
    }
}

void Player::FinishAbsorbEffect() {
    BaseEnemy* enemy = pendingAbsorbEnemy_;
    if (!enemy) {
        CancelAbsorbEffect(false);
        return;
    }

    // 獲得バーストは共通VFXへ集約し、二重発生を避ける。

    SetColor(pendingAbsorbPlayerBaseColor_);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetColor(pendingAbsorbPlayerBaseColor_);
        }
    }

    SetScale(managedBaseScale_);
    absorbEffectActive_ = false;
    pendingAbsorbEnemy_ = nullptr;
    pendingAbsorbType_ = EnemyMorphType::None;
    absorbEffectTimer_ = 0.0f;
    absorbEffectEmitTimer_ = 0.0f;

    StartEnemyMorph(enemy);
}

void Player::CancelAbsorbEffect(bool restoreEnemy) {
    if (restoreEnemy && pendingAbsorbEnemy_) {
        {
            pendingAbsorbEnemy_->SetScale(pendingAbsorbEnemyBaseScale_);
            pendingAbsorbEnemy_->SetQuaternionMaster(true);
            pendingAbsorbEnemy_->UpdateLocalMatrix();
            pendingAbsorbEnemy_->UpdateWorldMatrix();
        }
        pendingAbsorbEnemy_->SetIsVisible(true);
    }

    SetColor(pendingAbsorbPlayerBaseColor_);
    if (!isEnemyMorphed_) {
        for (Object3d* child : GetChildren()) {
            if (child) {
                child->SetColor(pendingAbsorbPlayerBaseColor_);
            }
        }
    }

    absorbEffectActive_ = false;
    pendingAbsorbEnemy_ = nullptr;
    pendingAbsorbType_ = EnemyMorphType::None;
    absorbEffectTimer_ = 0.0f;
    absorbEffectEmitTimer_ = 0.0f;
}

void Player::StartEnemyMorph(BaseEnemy* enemy) {
    if (!enemy) {
        return;
    }

    const EnemyMorphType resolvedType = ResolveEnemyMorphType(enemy->GetEnemyType());
    if (resolvedType == EnemyMorphType::None) {
        DebugConsole::GetInstance()->AddLog("Enemy morph rejected: unsupported enemy type.");
        return;
    }

    if (baseCombatController_) {
        baseCombatController_->Cancel(*this, true);
    }
    if (isEnemyMorphed_ && copyAbilityController_) {
        copyAbilityController_->Cancel(*this);
    }

    const float currentMoveYaw = GetMoveYaw();
    SaveEnemyMorphBaseAppearance(enemy->GetIsVisible());

    enemyMorphType_ = resolvedType;
    enemyMorphSource_ = enemy;
    enemyMorphHasTimeLimit_ = !enemy->param_.has_value() || enemy->param_->morphLimited;
    enemyMorphDuration_ =
        enemy->param_.has_value() ? (std::max)(0.1f, enemy->param_->morphDuration) : kEnemyMorphDuration;
    enemyMorphTimer_ = enemyMorphHasTimeLimit_ ? enemyMorphDuration_ : 0.0f;
    enemyMorphEffectTimer_ = 0.0f;
    enemyMorphVisualTimer_ = 0.0f;
    if (copyAbilityController_) {
        copyAbilityController_->Activate(static_cast<int>(enemyMorphType_), enemy->GetAttackProfile());
    }
    enemyMorphTint_ = GetEnemyMorphTint(enemyMorphType_);
    isEnemyMorphed_ = true;
    CompleteEnemyMorphStart(currentMoveYaw);
    enemy->SetCarried(true);
    if (enemy->param_.has_value()) {
        enemy->param_->hp = 0.0f;
    }
    enemy->isDead = true;
    enemy->SetIsVisible(false);
    enemy->SetCollisionAttribute(0);
    enemy->SetCollisionMask(0);
    enemy->SetParticleName("");
    enemy->SetGPUParticleName("");
    enemy->SetMeshEffect1Name("");
    enemy->SetMeshEffect2Name("");

    if (!enemy->GetModelName().empty()) {
        SetModel(enemy->GetModelName());
    }
    const std::string enemyTexture = enemy->GetTexturePath();
    if (!enemyTexture.empty()) {
        SetTexture(enemyTexture);
    }
    SetMaterialType(enemy->GetMaterialType());
    SetEmissive(enemy->GetEmissive());
    animName_.clear();
    ResetAnimationTime();
    SetColor(enemy->GetColor());

    EmitEnemyMorphStartEffects();

    DebugConsole::GetInstance()->AddLog("Enemy morph start: " + enemy->GetEnemyType());
    if (carriedEnemy_ == enemy) {
        SetCarriedEnemy(nullptr);
    }

    // スライム系のコピー能力は元敵の寿命から完全に切り離します。
    // 以降の更新・解除はPlayerCopyAbilityControllerだけが担当します。
    if (copyAbilityController_ && copyAbilityController_->HandlesMorphType(static_cast<int>(enemyMorphType_))) {
        enemyMorphSource_ = nullptr;
    }
}

void Player::SaveEnemyMorphBaseAppearance(bool sourceVisible) {
    if (isEnemyMorphed_) {
        return;
    }

    savedMorphModelName_ = GetModelName();
    savedMorphTexturePath_ = GetTexturePath();
    savedMorphAnimName_ = animName_;
    savedMorphAnimLoop_ = isAnimLoop_;
    savedMorphColor_ = GetColor();
    savedMorphScale_ = GetScale();
    savedMorphMaterialType_ = GetMaterialType();
    savedMorphEmissive_ = GetEmissive();
    savedMorphSourceVisible_ = sourceVisible;
    savedMorphChildVisible_.clear();
    savedMorphChildVisible_.reserve(GetChildren().size());
    for (Object3d* child : GetChildren()) {
        savedMorphChildVisible_.push_back(child ? child->GetIsVisible() : false);
    }
}

void Player::CompleteEnemyMorphStart(float moveYaw) {
    if (checkpointActivated_ && enemyMorphType_ != EnemyMorphType::Crown) {
        const std::string currentMorph = GetEnemyTypeForCurrentMorph();
        if (!currentMorph.empty()) {
            checkpointMorphEnemyType_ = currentMorph;
            RefreshCheckpointMorphSnapshot();
        }
    }
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera(); camera && !IsEditorInternal()) {
        // コピー中の右クリックは固有能力専用なので、通常フックの一人称ズームを無効化します。
        camera->SetAimCameraSuppressed(true);
    }
    if (hookMarker_) {
        hookMarker_->SetIsVisible(false);
    }
    aimTargetObject_ = nullptr;
    SetMoveYaw(moveYaw);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetIsVisible(false);
        }
    }
}

void Player::EmitEnemyMorphStartEffects() {
    const char* cue = nullptr;
    switch (enemyMorphType_) {
    case EnemyMorphType::Slime:
        cue = "player_copy_pink";
        break;
    case EnemyMorphType::FireSlime:
        cue = "player_copy_fire";
        break;
    case EnemyMorphType::ThunderSlime:
        cue = "player_copy_thunder";
        break;
    case EnemyMorphType::WindSlime:
        cue = "player_copy_wind";
        break;
    case EnemyMorphType::Bomber:
        cue = "player_copy_bomb";
        break;
    default:
        break;
    }
    if (cue) {
        // 見た目だけの変形。速度・操作・当たり判定・コピー成立時刻は変更しない。
        // 攻撃は同じモーション枠を上書きでき、ジャンプ時はAnimator側で解除する。
        ClearSlimeAbilityMotion();
        PlaySlimeAbilityMotion("player_copy_acquire", false, 1.0f, 0.025f, 0.08f);
        VFXSequencer::PlayOneShotOnTarget(cue, this);
        // 獲得演出中に常時オーラを重ねない。
        enemyMorphEffectTimer_ = 0.55f;
        return;
    }
    // 王冠などの特殊コピーは既存演出を維持する。
    if (particleSystem_ && enemyMorphType_ != EnemyMorphType::Slime) {
        Vector3 up = {0.0f, 1.0f, 0.0f};
        particleSystem_->SpawnParticles(
            GetWorldPosition() + Vector3{0.0f, 1.0f, 0.0f}, 32, 2.2f, &up, 22.0f, enemyMorphTint_,
            {enemyMorphTint_.x, enemyMorphTint_.y, enemyMorphTint_.z, 0.0f}, 0.14f, 0.45f, 0.42f, 0.08f);
    }
}

void Player::UpdateEnemyMorph(float deltaTime) {
    if (enemyMorphReleaseActive_) {
        UpdateEnemyMorphRelease(deltaTime);
        return;
    }

    if (!isEnemyMorphed_) {
        return;
    }

    if (enemyMorphSource_ && enemyMorphSource_->IsDefeatEffectPlaying()) {
        BeginEnemyMorphRelease(true);
        return;
    }

    if (enemyMorphHasTimeLimit_ && deltaTime > 0.0f) {
        enemyMorphTimer_ -= deltaTime;
        enemyMorphEffectTimer_ -= deltaTime;
        enemyMorphVisualTimer_ += deltaTime;
    } else if (!enemyMorphHasTimeLimit_ && deltaTime > 0.0f) {
        enemyMorphEffectTimer_ -= deltaTime;
        enemyMorphVisualTimer_ += deltaTime;
    }

    if (enemyMorphSource_) {
        enemyMorphSource_->SetIsVisible(false);
    }

    if (enemyMorphHasTimeLimit_ && enemyMorphTimer_ <= 0.0f) {
        BeginEnemyMorphRelease(true);
        return;
    }

    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetIsVisible(false);
        }
    }

    if (enemyMorphEffectTimer_ <= 0.0f) {
        if (enemyMorphType_ == EnemyMorphType::ThunderSlime) {
            const float morphElapsed = enemyMorphVisualTimer_;
            EmitOuterThunderParticles(GetWorldPosition(), GetScale(), morphElapsed * 2.6f, 1);
            enemyMorphEffectTimer_ = 0.48f;
            return;
        } else if (enemyMorphType_ == EnemyMorphType::Slime) {
            EmitCopyMorphPreset(kPinkSlimeMorphIdlePreset,
                                GetWorldPosition() + Vector3{0.0f, GetScale().y * 0.82f, 0.0f});
            enemyMorphEffectTimer_ = kPinkSlimeMorphParticleInterval;
            return;
        } else if (enemyMorphType_ == EnemyMorphType::Bomber) {
            EmitCopyMorphPreset(kBomberMorphAuraPreset, GetWorldPosition() + Vector3{0.0f, GetScale().y * 0.96f, 0.0f});
            enemyMorphEffectTimer_ = 0.68f;
            return;
        } else if (enemyMorphType_ == EnemyMorphType::FireSlime) {
            EmitCopyMorphPreset(kFireMorphAuraPreset, GetWorldPosition() + Vector3{0.0f, GetScale().y * 0.88f, 0.0f});
            enemyMorphEffectTimer_ = 0.48f;
            return;
        } else if (enemyMorphType_ == EnemyMorphType::WindSlime) {
            EmitCopyMorphPreset(kWindMorphAuraPreset, GetWorldPosition() + Vector3{0.0f, GetScale().y * 0.82f, 0.0f});
            enemyMorphEffectTimer_ = 0.62f;
            return;
        } else if (particleSystem_) {
            Vector3 up = {0.0f, 1.0f, 0.0f};
            particleSystem_->SpawnParticles(
                GetWorldPosition() + Vector3{0.0f, 1.1f, 0.0f}, 6, 1.2f, &up, 12.0f, enemyMorphTint_,
                {enemyMorphTint_.x, enemyMorphTint_.y, enemyMorphTint_.z, 0.0f}, 0.08f, 0.22f, 0.26f, 0.03f);
        }
        enemyMorphEffectTimer_ = kEnemyMorphParticleInterval;
    }
}

float Player::GetEnemyMorphRate() const {
    if (!isEnemyMorphed_) {
        return 0.0f;
    }
    if (!enemyMorphHasTimeLimit_) {
        return 1.0f;
    }
    if (enemyMorphDuration_ <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(enemyMorphTimer_ / enemyMorphDuration_, 0.0f, 1.0f);
}
