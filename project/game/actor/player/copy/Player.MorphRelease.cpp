#include "game/actor/player/Player.Internal.h"

#include "game/actor/item/DroppedCopyStar.h"
#include "game/actor/combat/CombatGroundQuery.h"
#include "BaseScene.h"

using namespace PlayerImplementation;

// 元の更新順序と判定を維持して、担当処理をまとめています。
float Player::GetEnemyMorphReleaseRate() const {
    // 解除演出が止まっている間は進行率を使わせない。
    if (!enemyMorphReleaseActive_ || enemyMorphReleaseDuration_ <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(enemyMorphReleaseTimer_ / enemyMorphReleaseDuration_, 0.0f, 1.0f);
}

bool Player::IsPinkSlimeMorphed() const {
    // 通常のピンクコピーだけに専用ジャンプを許可する。
    return isEnemyMorphed_ && enemyMorphType_ == EnemyMorphType::Slime;
}

void Player::ActivateCheckpoint(const Vector3& position, const std::string& checkpointObjectName) {
    // 復活位置と同時に現在のコピーを記録し、再開時の能力を再現する。
    respawnPosition_ = position;
    checkpointActivated_ = true;
    checkpointObjectName_ = checkpointObjectName;
    const std::string currentMorph = GetEnemyTypeForCurrentMorph();
    if (!currentMorph.empty()) {
        checkpointMorphEnemyType_ = currentMorph;
    }
    RefreshCheckpointMorphSnapshot();
}

bool Player::ApplyStoredCopy(const std::string& enemyType, bool unlimitedDuration) {
    const PlayerCopyTypeDescriptor* descriptor = PlayerCopyTypeCatalog::Find(enemyType);
    const EnemyMorphType resolvedType = ResolveEnemyMorphType(enemyType);
    if (!descriptor || resolvedType == EnemyMorphType::None || isDead ||
        (isCinematicLocked_ && resolvedType != EnemyMorphType::Crown) || absorbEffectActive_ ||
        enemyMorphReleaseActive_) {
        return false;
    }

    if (baseCombatController_) {
        baseCombatController_->Cancel(*this, true);
    }
    if (isEnemyMorphed_ && copyAbilityController_) {
        copyAbilityController_->Cancel(*this);
    }

    // 記憶台は敵を吸収する通常経路を通らないため、所持中の敵をここで正規解放します。
    // 参照だけを残すと変身解除後に敵が再び頭上へ追従し、カメラへ張り付いたように見えます。
    ReleaseCarriedEnemy(true);
    aimTargetObject_ = nullptr;
    if (hookMarker_) {
        hookMarker_->SetIsVisible(false);
    }

    const float currentMoveYaw = GetMoveYaw();
    SaveEnemyMorphBaseAppearance(true);

    enemyMorphType_ = resolvedType;
    enemyMorphSource_ = nullptr;
    enemyMorphHasTimeLimit_ = !unlimitedDuration;
    enemyMorphDuration_ = kEnemyMorphDuration;
    enemyMorphTimer_ = enemyMorphHasTimeLimit_ ? enemyMorphDuration_ : 0.0f;
    enemyMorphEffectTimer_ = 0.0f;
    enemyMorphVisualTimer_ = 0.0f;
    enemyMorphTint_ = GetEnemyMorphTint(enemyMorphType_);
    if (copyAbilityController_) {
        copyAbilityController_->ActivateDefault(static_cast<int>(enemyMorphType_));
    }
    isEnemyMorphed_ = true;
    CompleteEnemyMorphStart(currentMoveYaw);

    // 王冠とマントの装飾で被弾範囲が広がらないよう、体の判定を維持する。
    const ColliderConfig bodyCollider = GetColliderConfig();
    SetModel(std::string(descriptor->modelName));
    if (resolvedType == EnemyMorphType::Crown)
        SetColliderConfig(bodyCollider);
    SetTexture("");
    // 覇王モデルは顔・金属・布を別材質で持つため、全身ジェル化しない。
    SetMaterialType(resolvedType == EnemyMorphType::Crown ? 0 : 25);
    SetEmissive(1.0f);
    animName_.clear();
    ResetAnimationTime();
    SetColor(descriptor->displayColor);
    EmitEnemyMorphStartEffects();

    DebugConsole::GetInstance()->AddLog("Stored copy applied: " + enemyType);
    return true;
}

std::string Player::GetEnemyTypeForCurrentMorph() const {
    // セーブデータには列挙値でなく安定した敵種別名を記録する。
    if (!isEnemyMorphed_) {
        return {};
    }
    switch (enemyMorphType_) {
    case EnemyMorphType::Slime:
        return "Slime";
    case EnemyMorphType::Bomber:
        return "Bomber";
    case EnemyMorphType::FireSlime:
        return "FireSlime";
    case EnemyMorphType::ThunderSlime:
        return "ThunderSlime";
    case EnemyMorphType::WindSlime:
        return "WindSlime";
    default:
        return {};
    }
}

void Player::ClearCrownPower() {
    // 王冠以外のコピーを解除せず、最終戦専用の能力だけ片付ける。
    if (enemyMorphType_ == EnemyMorphType::Crown)
        CancelEnemyMorph();
}

void Player::RefreshCheckpointMorphSnapshot() {
    // 有効なチェックポイントだけ永続データへ反映する。
    if (!checkpointActivated_) {
        return;
    }
    const Vector3 position = respawnPosition_;
    GameDataManager::GetInstance()->RequestStageCheckpointRespawn(StageManager::GetInstance()->GetCurrentStageIndex(),
                                                                  position.x, position.y, position.z,
                                                                  checkpointMorphEnemyType_, checkpointObjectName_);
}

void Player::BeginEnemyMorphRelease(bool expired) {
    // 王冠の欠片は最終戦中の専用装備。任意解除による攻略不能を防ぎます。
    if (enemyMorphType_ == EnemyMorphType::Crown && !expired)
        return;
    if (!isEnemyMorphed_ || enemyMorphReleaseActive_) {
        return;
    }

    if (copyAbilityController_) {
        copyAbilityController_->Cancel(*this);
    }
    evasionInvincibleTimer_ = 0.0f;
    isEvasionInvincible_ = false;
    UpdateColor();

    enemyMorphReleaseActive_ = true;
    enemyMorphReleaseBurstStarted_ = false;
    enemyMorphReleaseExpired_ = expired;
    enemyMorphReleaseTimer_ = 0.0f;
    enemyMorphReleaseDuration_ = expired ? kEnemyMorphExpiredReleaseDuration : kEnemyMorphReleaseDuration;
    enemyMorphReleaseBurstTime_ = expired ? kEnemyMorphExpiredReleaseBurstTime : kEnemyMorphReleaseBurstTime;
    enemyMorphReleaseParticleTimer_ = 0.0f;
    enemyMorphReleasePlayerScale_ = GetScale();
    enemyMorphReleaseTint_ = GetEnemyMorphTint(enemyMorphType_);

    const float yaw = GetMoveYaw();
    enemyMorphReleaseDirection_ = {-std::sin(yaw), 0.0f, -std::cos(yaw)};
    const float playerScale =
        (std::max)({std::abs(savedMorphScale_.x), std::abs(savedMorphScale_.y), std::abs(savedMorphScale_.z), 0.1f});
    const float residueScale = std::clamp(playerScale * 0.32f, 0.38f, 0.82f);
    enemyMorphReleaseVisualScale_ = {residueScale, residueScale, residueScale};

    if (enemyMorphReleaseVisual_) {
        enemyMorphReleaseVisual_->SetIsVisible(false);
        enemyMorphReleaseVisual_->SetColor(enemyMorphReleaseTint_);
        enemyMorphReleaseVisual_->SetScale({0.0f, 0.0f, 0.0f});
    }

    pendingAttack2_ = false;
    attackInputBuffer_.ClearPendingInput();
    DebugConsole::GetInstance()->AddLog(expired ? "Enemy morph release: expired." : "Enemy morph release: manual.");
}

void Player::EmitEnemyMorphReleaseBurst() {
    // 解除の山場は一度だけ発火し、残像と回収可能な星を重複生成しない。
    if (enemyMorphReleaseBurstStarted_) {
        return;
    }

    enemyMorphReleaseBurstStarted_ = true;
    enemyMorphReleaseParticleTimer_ = 0.0f;
    enemyMorphReleaseStartPosition_ = GetWorldPosition() + Vector3{0.0f, 0.82f, 0.0f};

    if (enemyMorphReleaseVisual_) {
        enemyMorphReleaseVisual_->SetTranslate(enemyMorphReleaseStartPosition_);
        enemyMorphReleaseVisual_->SetRotation({0.0f, GetMoveYaw() + kPi, 0.0f});
        enemyMorphReleaseVisual_->SetScale({enemyMorphReleaseVisualScale_.x * 1.18f,
                                            enemyMorphReleaseVisualScale_.y * 0.58f,
                                            enemyMorphReleaseVisualScale_.z * 1.18f});
        enemyMorphReleaseVisual_->SetColor(enemyMorphReleaseTint_);
        enemyMorphReleaseVisual_->SetIsVisible(true);
        enemyMorphReleaseVisual_->UpdateLocalMatrix();
        enemyMorphReleaseVisual_->UpdateWorldMatrix();
    }

    Vector3 burstDirection = {enemyMorphReleaseDirection_.x, 0.42f, enemyMorphReleaseDirection_.z};
    const float burstLength = Math::Length(burstDirection);
    if (burstLength > 0.001f) {
        burstDirection = burstDirection / burstLength;
    }
    if (particleSystem_) {
        particleSystem_->SpawnParticles(
            enemyMorphReleaseStartPosition_, enemyMorphReleaseExpired_ ? 20 : 28, 1.45f, &burstDirection,
            enemyMorphReleaseExpired_ ? 20.0f : 25.0f, enemyMorphReleaseTint_,
            {enemyMorphReleaseTint_.x, enemyMorphReleaseTint_.y, enemyMorphReleaseTint_.z, 0.0f}, 0.10f, 0.34f, 0.32f,
            0.04f);
    }

    if (enemyMorphType_ == EnemyMorphType::ThunderSlime) {
        EmitThunderMorphBurst(enemyMorphReleaseStartPosition_, enemyMorphReleaseVisualScale_);
    } else if (enemyMorphType_ == EnemyMorphType::Slime) {
        EmitCopyMorphPreset(kPinkSlimeMorphBurstPreset, enemyMorphReleaseStartPosition_);
    } else if (enemyMorphType_ == EnemyMorphType::Bomber) {
        SpawnCopyMorphPulse(kBomberMorphPulseEffectPath, enemyMorphReleaseStartPosition_, {1.24f, 1.24f, 1.24f});
        EmitCopyMorphPreset(kBomberMorphAuraPreset, enemyMorphReleaseStartPosition_);
    } else if (enemyMorphType_ == EnemyMorphType::FireSlime) {
        SpawnCopyMorphPulse(kFireMorphPulseEffectPath, enemyMorphReleaseStartPosition_, {1.28f, 1.28f, 1.28f});
        EmitCopyMorphPreset(kFireMorphAuraPreset, enemyMorphReleaseStartPosition_);
    } else if (enemyMorphType_ == EnemyMorphType::WindSlime) {
        SpawnCopyMorphPulse(kWindMorphPulseEffectPath, enemyMorphReleaseStartPosition_, {1.32f, 1.08f, 1.32f});
        EmitCopyMorphPreset(kWindMorphAuraPreset, enemyMorphReleaseStartPosition_);
    }

    // 型情報を失う前に、任意解除分だけ回収可能な星へ移します。
    const std::string droppedType = GetEnemyTypeForCurrentMorph();
    auto* sceneManager = SceneManager::GetInstance();
    if (!enemyMorphReleaseExpired_ && !droppedType.empty() && common_ && sceneManager && sceneManager->IsPlaying() &&
        sceneManager->GetCurrentScene()) {
        Vector3 landing = GetWorldPosition() + enemyMorphReleaseDirection_ * 2.25f;
        landing = CombatGroundQuery::FindGroundPoint(landing, this, {16.0f, 34.0f}, GetWorldPosition().y);
        landing.y += 0.85f;
        auto star = std::make_unique<DroppedCopyStar>();
        star->InitializeDrop(common_, droppedType, !enemyMorphHasTimeLimit_, enemyMorphReleaseStartPosition_, landing);
        sceneManager->GetCurrentScene()->AddObject(std::move(star));
    }
    RestoreEnemyMorphAppearance();
}

void Player::UpdateEnemyMorphRelease(float deltaTime) {
    // 予備動作、成分の分離、復帰を同じ時計で進めて見た目と操作復帰を同期する。
    if (!enemyMorphReleaseActive_) {
        return;
    }

    if (deltaTime > 0.0f) {
        enemyMorphReleaseTimer_ += deltaTime;
        enemyMorphReleaseParticleTimer_ -= deltaTime;
    }

    if (!enemyMorphReleaseBurstStarted_ && enemyMorphReleaseTimer_ < enemyMorphReleaseBurstTime_) {
        const float t =
            std::clamp(enemyMorphReleaseTimer_ / (std::max)(enemyMorphReleaseBurstTime_, 0.001f), 0.0f, 1.0f);
        const float eased = t * t * (3.0f - 2.0f * t);
        SetScale({enemyMorphReleasePlayerScale_.x * (1.0f + eased * 0.18f),
                  enemyMorphReleasePlayerScale_.y * (1.0f - eased * 0.38f),
                  enemyMorphReleasePlayerScale_.z * (1.0f + eased * 0.18f)});
        SetColor(Math::Lerp(GetColor(), {1.0f, 1.0f, 1.0f, 1.0f}, eased * 0.22f));
        UpdateLocalMatrix();
        UpdateWorldMatrix();
        return;
    }

    if (!enemyMorphReleaseBurstStarted_) {
        EmitEnemyMorphReleaseBurst();
    }

    const float flightDuration = (std::max)(enemyMorphReleaseDuration_ - enemyMorphReleaseBurstTime_, 0.001f);
    const float flightTime = (std::max)(0.0f, enemyMorphReleaseTimer_ - enemyMorphReleaseBurstTime_);
    const float t = std::clamp(flightTime / flightDuration, 0.0f, 1.0f);
    const float eased = 1.0f - std::pow(1.0f - t, 3.0f);
    const float wobble = std::sin(t * kPi * 4.0f) * std::pow(1.0f - t, 1.35f);

    SetScale({savedMorphScale_.x * (1.0f + wobble * 0.11f), savedMorphScale_.y * (1.0f - wobble * 0.09f),
              savedMorphScale_.z * (1.0f + wobble * 0.11f)});
    UpdateLocalMatrix();
    UpdateWorldMatrix();

    if (enemyMorphReleaseVisual_) {
        Vector3 position = enemyMorphReleaseStartPosition_;
        position.x += enemyMorphReleaseDirection_.x * 2.25f * eased;
        position.z += enemyMorphReleaseDirection_.z * 2.25f * eased;
        position.y += std::sin(t * kPi) * 0.92f - t * 0.34f;

        float scaleRate = 1.0f;
        if (t < 0.20f) {
            scaleRate = 0.72f + (t / 0.20f) * 0.38f;
        } else if (t > 0.68f) {
            scaleRate = std::clamp((1.0f - t) / 0.32f, 0.0f, 1.0f);
        }
        const float residueWobble = std::sin(t * kPi * 5.0f) * (1.0f - t) * 0.18f;
        enemyMorphReleaseVisual_->SetTranslate(position);
        enemyMorphReleaseVisual_->SetScale(
            {enemyMorphReleaseVisualScale_.x * scaleRate * (1.0f + residueWobble),
             enemyMorphReleaseVisualScale_.y * scaleRate * (1.0f - residueWobble * 0.75f),
             enemyMorphReleaseVisualScale_.z * scaleRate * (1.0f + residueWobble)});
        enemyMorphReleaseVisual_->SetColor({enemyMorphReleaseTint_.x, enemyMorphReleaseTint_.y,
                                            enemyMorphReleaseTint_.z, std::clamp((1.0f - t) * 1.8f, 0.0f, 1.0f)});
        enemyMorphReleaseVisual_->Update(deltaTime);
        enemyMorphReleaseVisual_->UpdateLocalMatrix();
        enemyMorphReleaseVisual_->UpdateWorldMatrix();
    }

    if (enemyMorphReleaseParticleTimer_ <= 0.0f && t < 0.82f) {
        if (enemyMorphReleaseExpired_ || t > 0.18f) {
            if (particleSystem_) {
                Vector3 trailDirection = {-enemyMorphReleaseDirection_.x, 0.12f, -enemyMorphReleaseDirection_.z};
                Vector3 trailPosition = enemyMorphReleaseVisual_ ? enemyMorphReleaseVisual_->GetWorldPosition()
                                                                 : enemyMorphReleaseStartPosition_;
                particleSystem_->SpawnParticles(
                    trailPosition, 4, 0.55f, &trailDirection, 6.0f, enemyMorphReleaseTint_,
                    {enemyMorphReleaseTint_.x, enemyMorphReleaseTint_.y, enemyMorphReleaseTint_.z, 0.0f}, 0.04f, 0.14f,
                    0.16f, 0.01f);
            }
            enemyMorphReleaseParticleTimer_ = 0.055f;
        }
    }

    if (enemyMorphReleaseTimer_ >= enemyMorphReleaseDuration_) {
        const Vector3 reboundScale = {savedMorphScale_.x * 1.08f, savedMorphScale_.y * 0.92f,
                                      savedMorphScale_.z * 1.08f};
        ResetEnemyMorphRelease();
        TriggerSlimeImpulse(reboundScale, 0.16f);
    }
}

void Player::RestoreEnemyMorphAppearance() {
    // コピー前のモデル・材質・衝突範囲と向きをまとめて復元する。
    if (!isEnemyMorphed_) {
        return;
    }

    const float currentMoveYaw = GetMoveYaw();
    BaseEnemy* morphSource = enemyMorphSource_;

    if (copyAbilityController_) {
        copyAbilityController_->Cancel(*this);
    }

    if (!savedMorphModelName_.empty()) {
        SetModel(savedMorphModelName_);
    }
    if (!savedMorphTexturePath_.empty()) {
        SetTexture(savedMorphTexturePath_);
    } else {
        SetTexture("");
    }
    SetMaterialType(savedMorphMaterialType_);
    SetEmissive(savedMorphEmissive_);
    SetColor(savedMorphColor_);
    SetScale(savedMorphScale_);
    animName_ = savedMorphAnimName_;
    isAnimLoop_ = savedMorphAnimLoop_;
    ResetAnimationTime();

    const auto& children = GetChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        Object3d* child = children[i];
        if (child) {
            child->SetColor(savedMorphColor_);
            if (i < savedMorphChildVisible_.size()) {
                child->SetIsVisible(savedMorphChildVisible_[i]);
            } else {
                child->SetIsVisible(true);
            }
        }
    }

    if (morphSource && !morphSource->isDead && !morphSource->IsDefeatEffectPlaying()) {
        morphSource->SetIsVisible(savedMorphSourceVisible_);
    }
    if (carriedEnemy_ == morphSource) {
        SetCarriedEnemy(nullptr);
    }

    isEnemyMorphed_ = false;
    enemyMorphType_ = EnemyMorphType::None;
    enemyMorphSource_ = nullptr;
    enemyMorphHasTimeLimit_ = true;
    enemyMorphTimer_ = 0.0f;
    enemyMorphEffectTimer_ = 0.0f;
    enemyMorphVisualTimer_ = 0.0f;
    savedMorphChildVisible_.clear();
    savedMorphSourceVisible_ = true;
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera(); camera && !IsEditorInternal()) {
        camera->SetAimCameraSuppressed(false);
    }
    SetMoveYaw(currentMoveYaw);
    DebugConsole::GetInstance()->AddLog("Enemy morph end.");
}

void Player::ResetEnemyMorphRelease() {
    // 中断された解除演出も、残像と伸縮を残さず初期化する。
    if (enemyMorphReleaseVisual_) {
        enemyMorphReleaseVisual_->SetIsVisible(false);
        enemyMorphReleaseVisual_->SetScale({0.0f, 0.0f, 0.0f});
    }
    if (enemyMorphReleaseActive_ && !isEnemyMorphed_) {
        SetScale(savedMorphScale_);
        SetColor(savedMorphColor_);
        UpdateLocalMatrix();
        UpdateWorldMatrix();
    }
    enemyMorphReleaseActive_ = false;
    enemyMorphReleaseBurstStarted_ = false;
    enemyMorphReleaseExpired_ = false;
    enemyMorphReleaseTimer_ = 0.0f;
    enemyMorphReleaseParticleTimer_ = 0.0f;
}

void Player::CancelEnemyMorph() {
    // 吸収演出とコピー解除が重なっても、古い照準や無敵状態を持ち越さない。
    if (absorbEffectActive_) {
        CancelAbsorbEffect(true);
    }
    ResetEnemyMorphRelease();
    RestoreEnemyMorphAppearance();
    if (hookMarker_) {
        hookMarker_->SetIsVisible(false);
    }
    aimTargetObject_ = nullptr;
    evasionInvincibleTimer_ = 0.0f;
    isEvasionInvincible_ = false;
    UpdateColor();
}

#ifdef USE_IMGUI
void Player::DebugForceEnemyMorph(BaseEnemy* enemy) {
    if (!enemy || isDead || isCinematicLocked_) {
        return;
    }

    // 通常の吸収・運搬状態を先に解消し、能力側に古い敵参照を残さないようにします。
    CancelEnemyMorph();
    ReleaseCarriedEnemy(true);
    StartEnemyMorph(enemy);

    // デバッグ中は時間切れで解除されないようにし、任意の能力を継続して確認できます。
    if (isEnemyMorphed_) {
        enemyMorphHasTimeLimit_ = false;
        enemyMorphTimer_ = 0.0f;
    }
}

void Player::DebugClearEnemyMorph() {
    // デバッグ操作も通常の解除経路を通し、後片付けを共通化する。
    CancelEnemyMorph();
}

void Player::DebugPrepareForTeleport() {
    // 移動系演出を先に止め、保存されていた衝突設定を通常状態へ戻します。
    if (mover_) {
        mover_->ResetTransientState();
    }
    if (gateReturnAnimation_.IsActive()) {
        gateReturnAnimation_.Stop(this, false);
    }
    if (isCinematicLocked_) {
        EndCinematicLock(false);
    }

    // 攻撃・コピー能力の更新が次フレームに残らないよう、各セッションを明示的に破棄します。
    if (baseCombatController_) {
        baseCombatController_->Cancel(*this, false);
    }
    if (copyAbilityController_) {
        copyAbilityController_->Cancel(*this);
    }
    CancelEnemyMorph();
    ReleaseCarriedEnemy(true);

    // 感電は固定座標を毎フレーム再適用するため、ワープ座標を設定する前に必ず解除します。
    if (electricShockFeedbackTimer_ > 0.0f || electricShockControlLocked_) {
        electricShockPendingInvincibleDuration_ = 0.0f;
        EndElectricShockFeedback();
    }

    RestoreDamageBlinkVisibility();
    damageCooldownTimer_ = 0.0f;
    evasionInvincibleTimer_ = 0.0f;
    invincibleBlinkTimer_ = 0.0f;
    isEvasionInvincible_ = false;
    SetDamageInvincible(false);
    SetDashInvincible(false);
    SetGuardInvincible(false);

    pendingAttack2_ = false;
    attackInputBuffer_.ClearPendingInput();
    forcedSlimeAnimationModeActive_ = false;
    aimTargetObject_ = nullptr;
    if (hookMarker_) {
        hookMarker_->SetIsVisible(false);
    }

    ClearSlimeAbilityMotion();
    ChangeState(std::make_unique<PlayerStateIdle>());
    slimeAnimator_.Reset(managedBaseScale_, this);
    slimeAnimator_.SetMode(PlayerSlimeAnimator::Mode::Idle);

    Vector3 stableRotation = GetRotation();
    stableRotation.x = baseRotation_.x;
    stableRotation.z = baseRotation_.z;
    SetScale(managedBaseScale_);
    SetRotation(stableRotation);
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetIsControlActive(false);
    SetIsVisible(true);
    UpdateColor();
}
#endif
