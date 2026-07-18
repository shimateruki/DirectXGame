#include "Player.h"

namespace {
json ToJson(const Vector3& value) {
    return json::array({ value.x, value.y, value.z });
}

Vector3 ReadVector3(const json& value, const Vector3& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
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
    state["playerInvincibleBlinkTimer"] = invincibleBlinkTimer_;
    state["playerDeathTimer"] = deathTimer_;
    state["playerRespawnPosition"] = ToJson(respawnPosition_);
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
    state["playerElectricShockTimer"] = electricShockFeedbackTimer_;
    state["playerElectricShockLocked"] = electricShockControlLocked_;
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
    invincibleBlinkTimer_ = state.value("playerInvincibleBlinkTimer", invincibleBlinkTimer_);
    deathTimer_ = state.value("playerDeathTimer", deathTimer_);
    if (state.contains("playerRespawnPosition")) {
        respawnPosition_ = ReadVector3(state["playerRespawnPosition"], respawnPosition_);
    }
    if (state.contains("playerBaseRotation")) {
        baseRotation_ = ReadVector3(state["playerBaseRotation"], baseRotation_);
    }
    isFirstUpdate_ = state.value("playerFirstUpdate", isFirstUpdate_);
    jumpCount_ = state.value("playerJumpCount", jumpCount_);
    if (state.contains("playerMoveYaw")) {
        SetMoveYaw(state["playerMoveYaw"].get<float>());
    }

    const int morphType = state.value("playerEnemyMorphType", static_cast<int>(enemyMorphType_));
    if (morphType >= static_cast<int>(EnemyMorphType::None) &&
        morphType <= static_cast<int>(EnemyMorphType::ThunderSlime)) {
        enemyMorphType_ = static_cast<EnemyMorphType>(morphType);
    }
    isEnemyMorphed_ = state.value("playerEnemyMorphed", isEnemyMorphed_);
    enemyMorphHasTimeLimit_ = state.value("playerMorphHasTimeLimit", enemyMorphHasTimeLimit_);
    enemyMorphTimer_ = state.value("playerMorphTimer", enemyMorphTimer_);
    enemyMorphDuration_ = state.value("playerMorphDuration", enemyMorphDuration_);
    enemyMorphEffectTimer_ = state.value("playerMorphEffectTimer", enemyMorphEffectTimer_);
    enemyMorphVisualTimer_ = state.value("playerMorphVisualTimer", enemyMorphVisualTimer_);
    electricShockFeedbackTimer_ = state.value("playerElectricShockTimer", electricShockFeedbackTimer_);
    electricShockControlLocked_ = state.value("playerElectricShockLocked", electricShockControlLocked_);
}
