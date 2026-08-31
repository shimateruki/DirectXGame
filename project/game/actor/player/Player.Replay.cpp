#include "Player.h"
#include "PlayerCopyAbilityController.h"

namespace {
json ToJson(const Vector3& value) {
    return json::array({ value.x, value.y, value.z });
}

json ToJson(const Vector4& value) {
    return json::array({ value.x, value.y, value.z, value.w });
}

Vector3 ReadVector3(const json& value, const Vector3& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
}


Vector4 ReadVector4(const json& value, const Vector4& fallback) {
    if (!value.is_array() || value.size() < 4) {
        return fallback;
    }
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() };
}

bool IsSupportedMorphTypeValue(int value) {
    switch (static_cast<Player::EnemyMorphType>(value)) {
    case Player::EnemyMorphType::None:
    case Player::EnemyMorphType::Slime:
    case Player::EnemyMorphType::Bomber:
    case Player::EnemyMorphType::Bat:
    case Player::EnemyMorphType::BeamDrone:
    case Player::EnemyMorphType::Mushroom:
    case Player::EnemyMorphType::FireSlime:
    case Player::EnemyMorphType::ThunderSlime:
    case Player::EnemyMorphType::WindSlime:
        return true;
    default:
        return false;
    }
}
}

void Player::CaptureReplayCustomState(json& state) const {
    Character::CaptureReplayCustomState(state);
    state["playerLockingOn"] = isLockingOn_;
    state["playerControlActive"] = isControlActive_;
    state["playerCinematicLocked"] = isCinematicLocked_;
    state["playerPendingAttack2"] = pendingAttack2_;
    state["playerComboWindowTimer"] = comboWindowTimer_;
    state["playerAttackInputBuffered"] = attackInputBuffered_;
    state["playerAttackBufferUsed"] = attackBufferUsedForStateStart_;
    state["playerAttackInputBufferTimer"] = attackInputBufferTimer_;
    state["playerDamageCooldownTimer"] = damageCooldownTimer_;
    state["playerDamageInvincible"] = isDamageInvincible_;
    state["playerDashInvincible"] = isDashInvincible_;
    state["playerGuardInvincible"] = isGuardInvincible_;
    state["playerEvasionInvincibleTimer"] = evasionInvincibleTimer_;
    state["playerEvasionInvincible"] = isEvasionInvincible_;
    state["playerInvincibleBlinkTimer"] = invincibleBlinkTimer_;
    state["playerDeathTimer"] = deathTimer_;
    state["playerRespawnPosition"] = ToJson(respawnPosition_);
    state["playerCheckpointActivated"] = checkpointActivated_;
    state["playerCheckpointMorphEnemyType"] = checkpointMorphEnemyType_;
    state["playerBaseRotation"] = ToJson(baseRotation_);
    state["playerFirstUpdate"] = isFirstUpdate_;
    state["playerJumpCount"] = jumpCount_;
    state["playerMoveYaw"] = GetMoveYaw();
    state["playerEnemyMorphType"] = static_cast<int>(enemyMorphType_);
    state["playerEnemyMorphed"] = isEnemyMorphed_;
    state["playerMorphHasTimeLimit"] = enemyMorphHasTimeLimit_;
    state["playerMorphTimer"] = enemyMorphTimer_;
    state["playerMorphDuration"] = enemyMorphDuration_;
    state["playerMorphEffectTimer"] = enemyMorphEffectTimer_;
    state["playerMorphVisualTimer"] = enemyMorphVisualTimer_;
    state["playerMorphReleaseActive"] = enemyMorphReleaseActive_;
    state["playerMorphReleaseBurstStarted"] = enemyMorphReleaseBurstStarted_;
    state["playerMorphReleaseExpired"] = enemyMorphReleaseExpired_;
    state["playerMorphReleaseTimer"] = enemyMorphReleaseTimer_;
    state["playerMorphReleaseDuration"] = enemyMorphReleaseDuration_;
    state["playerMorphReleaseBurstTime"] = enemyMorphReleaseBurstTime_;
    state["playerMorphReleaseParticleTimer"] = enemyMorphReleaseParticleTimer_;
    state["playerMorphReleasePlayerScale"] = ToJson(enemyMorphReleasePlayerScale_);
    state["playerMorphReleaseVisualScale"] = ToJson(enemyMorphReleaseVisualScale_);
    state["playerMorphReleaseStartPosition"] = ToJson(enemyMorphReleaseStartPosition_);
    state["playerMorphReleaseDirection"] = ToJson(enemyMorphReleaseDirection_);
    state["playerMorphReleaseTint"] = ToJson(enemyMorphReleaseTint_);
    state["playerElectricShockTimer"] = electricShockFeedbackTimer_;
    state["playerElectricShockLocked"] = electricShockControlLocked_;
    state["playerTutorialSafety"] = tutorialSafetyEnabled_;
    state["playerTutorialCarryThrowEnabled"] = tutorialCarryThrowEnabled_;
    state["playerTutorialCarryAbsorbEnabled"] = tutorialCarryAbsorbEnabled_;
    if (baseCombatController_) {
        const PlayerBaseCombatController::ReplayState combatState =
            baseCombatController_->CaptureReplayState();
        state["playerBaseCombatPhase"] = combatState.phase;
        state["playerBaseCombatDirection"] = ToJson(combatState.direction);
        state["playerBaseCombatTimer"] = combatState.timer;
        state["playerBaseCombatEffectTimer"] = combatState.effectTimer;
        state["playerBaseCombatInputBufferTimer"] = combatState.inputBufferTimer;
        state["playerBaseCombatPressHitEnemy"] = combatState.pressHitEnemy;
    }
}

void Player::RestoreReplayCustomState(const json& state) {
    Character::RestoreReplayCustomState(state);
    isLockingOn_ = state.value("playerLockingOn", isLockingOn_);
    isControlActive_ = state.value("playerControlActive", isControlActive_);
    isCinematicLocked_ = state.value("playerCinematicLocked", isCinematicLocked_);
    pendingAttack2_ = state.value("playerPendingAttack2", pendingAttack2_);
    comboWindowTimer_ = state.value("playerComboWindowTimer", comboWindowTimer_);
    attackInputBuffered_ = state.value("playerAttackInputBuffered", attackInputBuffered_);
    attackBufferUsedForStateStart_ = state.value("playerAttackBufferUsed", attackBufferUsedForStateStart_);
    attackInputBufferTimer_ = state.value("playerAttackInputBufferTimer", attackInputBufferTimer_);
    damageCooldownTimer_ = state.value("playerDamageCooldownTimer", damageCooldownTimer_);
    isDamageInvincible_ = state.value("playerDamageInvincible", isDamageInvincible_);
    isDashInvincible_ = state.value("playerDashInvincible", isDashInvincible_);
    isGuardInvincible_ = state.value("playerGuardInvincible", isGuardInvincible_);
    evasionInvincibleTimer_ = state.value("playerEvasionInvincibleTimer", evasionInvincibleTimer_);
    isEvasionInvincible_ = state.value("playerEvasionInvincible", isEvasionInvincible_);
    invincibleBlinkTimer_ = state.value("playerInvincibleBlinkTimer", invincibleBlinkTimer_);
    deathTimer_ = state.value("playerDeathTimer", deathTimer_);
    if (state.contains("playerRespawnPosition")) {
        respawnPosition_ = ReadVector3(state["playerRespawnPosition"], respawnPosition_);
    }
    checkpointActivated_ =
        state.value("playerCheckpointActivated", checkpointActivated_);
    checkpointMorphEnemyType_ =
        state.value("playerCheckpointMorphEnemyType", checkpointMorphEnemyType_);
    if (state.contains("playerBaseRotation")) {
        baseRotation_ = ReadVector3(state["playerBaseRotation"], baseRotation_);
    }
    isFirstUpdate_ = state.value("playerFirstUpdate", isFirstUpdate_);
    jumpCount_ = state.value("playerJumpCount", jumpCount_);
    if (state.contains("playerMoveYaw")) {
        SetMoveYaw(state["playerMoveYaw"].get<float>());
    }

    const int morphType = state.value("playerEnemyMorphType", static_cast<int>(enemyMorphType_));
    const bool supportedMorphType = IsSupportedMorphTypeValue(morphType);
    if (supportedMorphType) {
        enemyMorphType_ = static_cast<EnemyMorphType>(morphType);
    } else {
        // 値6は廃止済みの巨大コピーです。古いリプレイは通常形態へ安全に戻します。
        enemyMorphType_ = EnemyMorphType::None;
    }
    isEnemyMorphed_ =
        supportedMorphType && state.value("playerEnemyMorphed", isEnemyMorphed_);
    enemyMorphHasTimeLimit_ = state.value("playerMorphHasTimeLimit", enemyMorphHasTimeLimit_);
    enemyMorphTimer_ = state.value("playerMorphTimer", enemyMorphTimer_);
    enemyMorphDuration_ = state.value("playerMorphDuration", enemyMorphDuration_);
    enemyMorphEffectTimer_ = state.value("playerMorphEffectTimer", enemyMorphEffectTimer_);
    enemyMorphVisualTimer_ = state.value("playerMorphVisualTimer", enemyMorphVisualTimer_);
    if (copyAbilityController_) {
        if (isEnemyMorphed_) {
            // リプレイの時間軸では元敵参照を復元せず、能力セッションを設定値から再構築します。
            copyAbilityController_->ActivateDefault(static_cast<int>(enemyMorphType_));
            if (copyAbilityController_->HandlesMorphType(static_cast<int>(enemyMorphType_))) {
                enemyMorphSource_ = nullptr;
            }
        }
        else {
            copyAbilityController_->Cancel(*this);
        }
    }
    enemyMorphReleaseActive_ = state.value("playerMorphReleaseActive", false);
    enemyMorphReleaseBurstStarted_ = state.value("playerMorphReleaseBurstStarted", false);
    enemyMorphReleaseExpired_ = state.value("playerMorphReleaseExpired", false);
    enemyMorphReleaseTimer_ = state.value("playerMorphReleaseTimer", 0.0f);
    enemyMorphReleaseDuration_ = state.value("playerMorphReleaseDuration", enemyMorphReleaseDuration_);
    enemyMorphReleaseBurstTime_ = state.value("playerMorphReleaseBurstTime", enemyMorphReleaseBurstTime_);
    enemyMorphReleaseParticleTimer_ = state.value("playerMorphReleaseParticleTimer", 0.0f);
    if (state.contains("playerMorphReleasePlayerScale")) {
        enemyMorphReleasePlayerScale_ = ReadVector3(state["playerMorphReleasePlayerScale"], enemyMorphReleasePlayerScale_);
    }
    if (state.contains("playerMorphReleaseVisualScale")) {
        enemyMorphReleaseVisualScale_ = ReadVector3(state["playerMorphReleaseVisualScale"], enemyMorphReleaseVisualScale_);
    }
    if (state.contains("playerMorphReleaseStartPosition")) {
        enemyMorphReleaseStartPosition_ = ReadVector3(state["playerMorphReleaseStartPosition"], enemyMorphReleaseStartPosition_);
    }
    if (state.contains("playerMorphReleaseDirection")) {
        enemyMorphReleaseDirection_ = ReadVector3(state["playerMorphReleaseDirection"], enemyMorphReleaseDirection_);
    }
    if (state.contains("playerMorphReleaseTint")) {
        enemyMorphReleaseTint_ = ReadVector4(state["playerMorphReleaseTint"], enemyMorphReleaseTint_);
    }
    if (enemyMorphReleaseActive_) {
        UpdateEnemyMorphRelease(0.0f);
    }
    else if (enemyMorphReleaseVisual_) {
        enemyMorphReleaseVisual_->SetIsVisible(false);
    }
    electricShockFeedbackTimer_ = state.value("playerElectricShockTimer", electricShockFeedbackTimer_);
    electricShockControlLocked_ = state.value("playerElectricShockLocked", electricShockControlLocked_);
    tutorialSafetyEnabled_ = state.value("playerTutorialSafety", tutorialSafetyEnabled_);
    tutorialCarryThrowEnabled_ = state.value("playerTutorialCarryThrowEnabled", tutorialCarryThrowEnabled_);
    tutorialCarryAbsorbEnabled_ = state.value("playerTutorialCarryAbsorbEnabled", tutorialCarryAbsorbEnabled_);
    if (baseCombatController_) {
        PlayerBaseCombatController::ReplayState combatState;
        combatState.phase = state.value("playerBaseCombatPhase", 0);
        if (state.contains("playerBaseCombatDirection")) {
            combatState.direction =
                ReadVector3(state["playerBaseCombatDirection"], combatState.direction);
        }
        combatState.timer = state.value("playerBaseCombatTimer", 0.0f);
        combatState.effectTimer = state.value("playerBaseCombatEffectTimer", 0.0f);
        combatState.inputBufferTimer =
            state.value("playerBaseCombatInputBufferTimer", 0.0f);
        combatState.pressHitEnemy =
            state.value("playerBaseCombatPressHitEnemy", false);
        baseCombatController_->RestoreReplayState(combatState);
    }
}
