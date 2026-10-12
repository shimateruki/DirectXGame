#include "game/actor/enemy/combat/EnemyAttackProfile.h"

// 攻撃プロフィールの定義値と、実行中のクールダウン・発生段階を分離して保持します。

#include <algorithm>

bool AttackAbilityRuntime::Start(const AttackAbilityDefinition& definition) {
    // 実行中の技を上書きせず、JSON由来の負時間は段階遷移の前に0へ丸める。
    if (!IsReady()) {
        return false;
    }
    definition_ = definition;
    definition_.windupDuration = (std::max)(0.0f, definition_.windupDuration);
    definition_.activeDuration = (std::max)(0.0f, definition_.activeDuration);
    definition_.recoveryDuration = (std::max)(0.0f, definition_.recoveryDuration);
    definition_.cooldown = (std::max)(0.0f, definition_.cooldown);
    hasDefinition_ = true;
    phase_ = AttackAbilityPhase::Windup;
    phaseTime_ = 0.0f;
    return true;
}

AttackAbilityUpdateResult AttackAbilityRuntime::Update(float deltaTime) {
    // 1フレームで複数段階を越える場合も遷移通知を落とさないよう残り時間を持ち回る。
    AttackAbilityUpdateResult result;
    result.previousPhase = phase_;
    result.currentPhase = phase_;
    if (!hasDefinition_ || phase_ == AttackAbilityPhase::Ready) {
        return result;
    }

    float remainingTime = (std::max)(0.0f, deltaTime);
    for (int transitionCount = 0; transitionCount < 5; ++transitionCount) {
        const float duration = definition_.GetPhaseDuration(phase_);
        const float timeToBoundary = (std::max)(0.0f, duration - phaseTime_);
        if (duration > 0.0f && remainingTime < timeToBoundary) {
            phaseTime_ += remainingTime;
            remainingTime = 0.0f;
            break;
        }

        if (duration > 0.0f) {
            remainingTime = (std::max)(0.0f, remainingTime - timeToBoundary);
        }
        phase_ = NextPhase(phase_);
        phaseTime_ = 0.0f;

        result.enteredActive |= phase_ == AttackAbilityPhase::Active;
        result.enteredRecovery |= phase_ == AttackAbilityPhase::Recovery;
        result.enteredCooldown |= phase_ == AttackAbilityPhase::Cooldown;
        result.becameReady |= phase_ == AttackAbilityPhase::Ready;

        if (phase_ == AttackAbilityPhase::Ready ||
            (remainingTime <= 0.0f && definition_.GetPhaseDuration(phase_) > 0.0f)) {
            break;
        }
    }

    result.currentPhase = phase_;
    return result;
}

void AttackAbilityRuntime::Cancel(bool enterCooldown) {
    // 中断後に再発動を制限するかどうかは呼び出し側が選ぶ。
    if (!hasDefinition_) {
        Reset();
        return;
    }
    phaseTime_ = 0.0f;
    phase_ = enterCooldown && definition_.cooldown > 0.0f ? AttackAbilityPhase::Cooldown : AttackAbilityPhase::Ready;
}

void AttackAbilityRuntime::Reset() {
    // 定義ごと捨て、次の技を即座に開始できる初期状態へ戻す。
    definition_ = {};
    phase_ = AttackAbilityPhase::Ready;
    phaseTime_ = 0.0f;
    hasDefinition_ = false;
}

float AttackAbilityRuntime::GetPhaseProgress() const {
    // 未開始時に段階の長さを参照しないよう、進捗0を返す。
    return hasDefinition_ ? definition_.GetPhaseProgressFromElapsed(phase_, phaseTime_) : 0.0f;
}

float AttackAbilityRuntime::GetElapsedActionTime() const {
    // クールダウンは動作本体に含めず、攻撃タイムライン上の終端に固定する。
    if (!hasDefinition_) {
        return 0.0f;
    }
    switch (phase_) {
    case AttackAbilityPhase::Windup:
        return phaseTime_;
    case AttackAbilityPhase::Active:
        return definition_.windupDuration + phaseTime_;
    case AttackAbilityPhase::Recovery:
        return definition_.windupDuration + definition_.activeDuration + phaseTime_;
    case AttackAbilityPhase::Cooldown:
        return definition_.GetActionDuration();
    case AttackAbilityPhase::Ready:
    default:
        return 0.0f;
    }
}

bool AttackAbilityRuntime::CanCancel() const {
    // キャンセル窓は定義内の時刻で判定するため、現在段階を経過時刻へ変換する。
    return hasDefinition_ && definition_.IsCancelWindowOpen(GetElapsedActionTime());
}

AttackAbilityPhase AttackAbilityRuntime::NextPhase(AttackAbilityPhase phase) const {
    // 予備動作から硬直・再使用待ちまでの順序をここで一本化する。
    switch (phase) {
    case AttackAbilityPhase::Windup:
        return AttackAbilityPhase::Active;
    case AttackAbilityPhase::Active:
        return AttackAbilityPhase::Recovery;
    case AttackAbilityPhase::Recovery:
        return AttackAbilityPhase::Cooldown;
    case AttackAbilityPhase::Cooldown:
    case AttackAbilityPhase::Ready:
    default:
        return AttackAbilityPhase::Ready;
    }
}
