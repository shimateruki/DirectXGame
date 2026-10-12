#include "Character.h"

namespace {
json ToJson(const Vector3& value) {
    // リプレイのベクトルはシーン JSON と同じ xyz 順の配列に揃える。
    return json::array({value.x, value.y, value.z});
}

Vector3 ReadVector3(const json& value, const Vector3& fallback) {
    // 旧記録に値がない場合は再生中の現値を保ち、互換性を維持する。
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}
} // namespace

// 位置だけでなく外力の残り時間も保存し、巻き戻し後の移動を再現します。
void Character::CaptureReplayCustomState(json& state) const {
    // 位置だけでは再生後の動きが一致しないため、接地と外力の残り時間も記録する。
    Object3d::CaptureReplayCustomState(state);
    state["characterVelocity"] = ToJson(velocity_);
    state["characterGrounded"] = isGrounded_;
    state["externalImpulseVelocity"] = ToJson(externalImpulseVelocity_);
    state["externalImpulseTimer"] = externalImpulseTimer_;
    state["externalImpulseDuration"] = externalImpulseDuration_;
    state["externalImpulseVerticalPending"] = externalImpulseVerticalPending_;
}

void Character::RestoreReplayCustomState(const json& state) {
    // 記録に存在する項目だけ戻し、古いリプレイ形式でも再生を継続できるようにする。
    Object3d::RestoreReplayCustomState(state);
    if (state.contains("characterVelocity")) {
        velocity_ = ReadVector3(state["characterVelocity"], velocity_);
    }
    isGrounded_ = state.value("characterGrounded", isGrounded_);
    if (state.contains("externalImpulseVelocity")) {
        externalImpulseVelocity_ = ReadVector3(state["externalImpulseVelocity"], externalImpulseVelocity_);
    }
    externalImpulseTimer_ = state.value("externalImpulseTimer", externalImpulseTimer_);
    externalImpulseDuration_ = state.value("externalImpulseDuration", externalImpulseDuration_);
    externalImpulseVerticalPending_ = state.value("externalImpulseVerticalPending", externalImpulseVerticalPending_);
}
