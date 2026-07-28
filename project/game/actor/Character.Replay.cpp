#include "Character.h"

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

void Character::CaptureReplayCustomState(json& state) const {
    Object3d::CaptureReplayCustomState(state);
    state["characterVelocity"] = ToJson(velocity_);
    state["characterGrounded"] = isGrounded_;
    state["externalImpulseVelocity"] = ToJson(externalImpulseVelocity_);
    state["externalImpulseTimer"] = externalImpulseTimer_;
    state["externalImpulseDuration"] = externalImpulseDuration_;
    state["externalImpulseVerticalPending"] = externalImpulseVerticalPending_;
}

void Character::RestoreReplayCustomState(const json& state) {
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
