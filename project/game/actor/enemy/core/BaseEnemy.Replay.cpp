#include "BaseEnemy.h"

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
}

void BaseEnemy::CaptureReplayCustomState(json& state) const {
    Character::CaptureReplayCustomState(state);
    state["enemyDamageCooldown"] = damageCooldownTimer_;
    state["enemyColorResetTimer"] = colorResetTimer_;
    state["enemyDamageReactionTimer"] = damageReactionTimer_;
    state["enemyDamageReactionDuration"] = damageReactionDuration_;
    state["enemyDamageReactionStrength"] = damageReactionStrength_;
    state["enemyDamageReactionDirection"] = ToJson(damageReactionLocalDirection_);
    state["enemyDefaultColor"] = ToJson(defaultColor_);
    state["enemyCarried"] = isCarried_;
    state["enemyDormant"] = isDormant_;
    state["enemyThrowRecoveryTimer"] = throwRecoveryTimer_;
    state["enemyThrownPhysics"] = isThrownPhysics_;
    state["enemyWanderOrigin"] = ToJson(wanderOrigin_);
    state["enemyWanderTarget"] = ToJson(wanderTarget_);
    state["enemyWanderWaitTimer"] = wanderWaitTimer_;
    state["enemyWanderRetargetTimer"] = wanderRetargetTimer_;
    state["enemyWanderSeed"] = wanderSeed_;
    state["enemyHasWanderOrigin"] = hasWanderOrigin_;
    state["enemyThrownAngularVelocity"] = ToJson(thrownAngularVelocity_);
    state["enemyThrownTimer"] = thrownTimer_;
    state["enemyThrownSettleTimer"] = thrownSettleTimer_;
    state["enemyThrowRecoveryRotateTimer"] = throwRecoveryRotateTimer_;
    state["enemySlamCooldown"] = slamImpactCooldownTimer_;
    state["enemySlamCount"] = slamImpactCount_;
    state["enemyRotationRecovering"] = isThrowRotationRecovering_;
    state["enemyDefeatPlaying"] = isDefeatEffectPlaying_;
    state["enemyDefeatFinished"] = isDefeatEffectFinished_;
    state["enemyCoinDropsSpawned"] = hasSpawnedDefeatCoinDrops_;
    state["enemyTargetDetected"] = wasTargetDetected_;
    state["enemyNoticeActive"] = isNoticeReactionActive_;
    state["enemyNoticeTimer"] = noticeReactionTimer_;
    state["enemyNoticeCooldown"] = noticeReactionCooldown_;
    state["enemyNoticeYaw"] = noticeMarkYaw_;
    state["enemyNoticeBaseScale"] = ToJson(noticeBaseScale_);
    state["enemyNoticeBaseColor"] = ToJson(noticeBaseColor_);
    state["enemyDefeatTimer"] = defeatEffectTimer_;
    state["enemyDefeatParticleTimer"] = defeatEffectParticleTimer_;
    state["enemyDefeatBasePosition"] = ToJson(defeatBasePosition_);
    state["enemyDefeatBaseScale"] = ToJson(defeatBaseScale_);
    state["enemyDefeatBaseColor"] = ToJson(defeatBaseColor_);
}

void BaseEnemy::RestoreReplayCustomState(const json& state) {
    Character::RestoreReplayCustomState(state);
    damageCooldownTimer_ = state.value("enemyDamageCooldown", damageCooldownTimer_);
    colorResetTimer_ = state.value("enemyColorResetTimer", colorResetTimer_);
    damageReactionTimer_ = state.value("enemyDamageReactionTimer", damageReactionTimer_);
    damageReactionDuration_ = state.value("enemyDamageReactionDuration", damageReactionDuration_);
    damageReactionStrength_ = state.value("enemyDamageReactionStrength", damageReactionStrength_);
    if (state.contains("enemyDamageReactionDirection")) {
        damageReactionLocalDirection_ = ReadVector3(state["enemyDamageReactionDirection"], damageReactionLocalDirection_);
    }
    if (state.contains("enemyDefaultColor")) defaultColor_ = ReadVector4(state["enemyDefaultColor"], defaultColor_);
    isCarried_ = state.value("enemyCarried", isCarried_);
    isDormant_ = state.value("enemyDormant", isDormant_);
    throwRecoveryTimer_ = state.value("enemyThrowRecoveryTimer", throwRecoveryTimer_);
    isThrownPhysics_ = state.value("enemyThrownPhysics", isThrownPhysics_);
    if (state.contains("enemyWanderOrigin")) wanderOrigin_ = ReadVector3(state["enemyWanderOrigin"], wanderOrigin_);
    if (state.contains("enemyWanderTarget")) wanderTarget_ = ReadVector3(state["enemyWanderTarget"], wanderTarget_);
    wanderWaitTimer_ = state.value("enemyWanderWaitTimer", wanderWaitTimer_);
    wanderRetargetTimer_ = state.value("enemyWanderRetargetTimer", wanderRetargetTimer_);
    wanderSeed_ = state.value("enemyWanderSeed", wanderSeed_);
    hasWanderOrigin_ = state.value("enemyHasWanderOrigin", hasWanderOrigin_);
    if (state.contains("enemyThrownAngularVelocity")) thrownAngularVelocity_ = ReadVector3(state["enemyThrownAngularVelocity"], thrownAngularVelocity_);
    thrownTimer_ = state.value("enemyThrownTimer", thrownTimer_);
    thrownSettleTimer_ = state.value("enemyThrownSettleTimer", thrownSettleTimer_);
    throwRecoveryRotateTimer_ = state.value("enemyThrowRecoveryRotateTimer", throwRecoveryRotateTimer_);
    slamImpactCooldownTimer_ = state.value("enemySlamCooldown", slamImpactCooldownTimer_);
    slamImpactCount_ = state.value("enemySlamCount", slamImpactCount_);
    isThrowRotationRecovering_ = state.value("enemyRotationRecovering", isThrowRotationRecovering_);
    isDefeatEffectPlaying_ = state.value("enemyDefeatPlaying", isDefeatEffectPlaying_);
    isDefeatEffectFinished_ = state.value("enemyDefeatFinished", isDefeatEffectFinished_);
    hasSpawnedDefeatCoinDrops_ = state.value("enemyCoinDropsSpawned", hasSpawnedDefeatCoinDrops_);
    wasTargetDetected_ = state.value("enemyTargetDetected", wasTargetDetected_);
    isNoticeReactionActive_ = state.value("enemyNoticeActive", isNoticeReactionActive_);
    noticeReactionTimer_ = state.value("enemyNoticeTimer", noticeReactionTimer_);
    noticeReactionCooldown_ = state.value("enemyNoticeCooldown", noticeReactionCooldown_);
    noticeMarkYaw_ = state.value("enemyNoticeYaw", noticeMarkYaw_);
    if (state.contains("enemyNoticeBaseScale")) noticeBaseScale_ = ReadVector3(state["enemyNoticeBaseScale"], noticeBaseScale_);
    if (state.contains("enemyNoticeBaseColor")) noticeBaseColor_ = ReadVector4(state["enemyNoticeBaseColor"], noticeBaseColor_);
    defeatEffectTimer_ = state.value("enemyDefeatTimer", defeatEffectTimer_);
    defeatEffectParticleTimer_ = state.value("enemyDefeatParticleTimer", defeatEffectParticleTimer_);
    if (state.contains("enemyDefeatBasePosition")) defeatBasePosition_ = ReadVector3(state["enemyDefeatBasePosition"], defeatBasePosition_);
    if (state.contains("enemyDefeatBaseScale")) defeatBaseScale_ = ReadVector3(state["enemyDefeatBaseScale"], defeatBaseScale_);
    if (state.contains("enemyDefeatBaseColor")) defeatBaseColor_ = ReadVector4(state["enemyDefeatBaseColor"], defeatBaseColor_);

    if (noticeMarkObject_) {
        noticeMarkObject_->SetIsVisible(isNoticeReactionActive_ && !IsReplayRemoved());
    }
}
