#define NOMINMAX
#include "AnimatorController.h"

#include "engine/utility/math/AnimationInterpolation.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace {
int ToInt(AnimatorParameterType type) {
    return static_cast<int>(type);
}

int ToInt(AnimatorConditionMode mode) {
    return static_cast<int>(mode);
}

AnimatorParameterType ToParameterType(int value) {
    return static_cast<AnimatorParameterType>(std::clamp(value, 0, 3));
}

AnimatorConditionMode ToConditionMode(int value) {
    return static_cast<AnimatorConditionMode>(std::clamp(value, 0, 6));
}

AnimationInterpolation::EasingType ToEasing(int value) {
    return static_cast<AnimationInterpolation::EasingType>(std::clamp(value, 0, 4));
}
}

void AnimatorControllerAsset::Clear() {
    version = kCurrentVersion;
    name = "animator_controller";
    entryState.clear();
    parameters.clear();
    states.clear();
    transitions.clear();
}

int AnimatorControllerAsset::FindStateIndex(const std::string& stateName) const {
    for (int index = 0; index < static_cast<int>(states.size()); ++index) {
        if (states[index].name == stateName) {
            return index;
        }
    }
    return -1;
}

const AnimatorStateDefinition* AnimatorControllerAsset::FindState(const std::string& stateName) const {
    const int index = FindStateIndex(stateName);
    return index >= 0 ? &states[index] : nullptr;
}

bool AnimatorControllerAsset::Save(const std::string& filePath) const {
    json root;
    root["version"] = kCurrentVersion;
    root["name"] = name;
    root["entryState"] = entryState;
    root["parameters"] = json::array();
    root["states"] = json::array();
    root["transitions"] = json::array();

    for (const auto& parameter : parameters) {
        root["parameters"].push_back({
            { "name", parameter.name },
            { "type", ToInt(parameter.type) },
            { "defaultFloat", parameter.defaultFloat },
            { "defaultInt", parameter.defaultInt },
            { "defaultBool", parameter.defaultBool }
        });
    }
    for (const auto& state : states) {
        root["states"].push_back({
            { "name", state.name },
            { "clipName", state.clipName },
            { "speed", state.speed },
            { "loop", state.loop },
            { "blendDuration", state.blendDuration },
            { "blendEasing", state.blendEasing }
        });
    }
    for (const auto& transition : transitions) {
        json item = {
            { "fromState", transition.fromState },
            { "toState", transition.toState },
            { "duration", transition.duration },
            { "easing", transition.easing },
            { "hasExitTime", transition.hasExitTime },
            { "exitTime", transition.exitTime },
            { "conditions", json::array() }
        };
        for (const auto& condition : transition.conditions) {
            item["conditions"].push_back({
                { "parameter", condition.parameter },
                { "mode", ToInt(condition.mode) },
                { "threshold", condition.threshold }
            });
        }
        root["transitions"].push_back(item);
    }

    const std::filesystem::path path(filePath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file << root.dump(4);
    return file.good();
}

bool AnimatorControllerAsset::Load(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    json root;
    try {
        file >> root;
        Clear();
        version = root.value("version", 1);
        name = root.value("name", std::filesystem::path(filePath).stem().string());
        entryState = root.value("entryState", "");
        for (const auto& item : root.value("parameters", json::array())) {
            AnimatorParameterDefinition parameter;
            parameter.name = item.value("name", "Parameter");
            parameter.type = ToParameterType(item.value("type", 0));
            parameter.defaultFloat = item.value("defaultFloat", 0.0f);
            parameter.defaultInt = item.value("defaultInt", 0);
            parameter.defaultBool = item.value("defaultBool", false);
            parameters.push_back(parameter);
        }
        for (const auto& item : root.value("states", json::array())) {
            AnimatorStateDefinition state;
            state.name = item.value("name", "State");
            state.clipName = item.value("clipName", "");
            state.speed = item.value("speed", 1.0f);
            state.loop = item.value("loop", true);
            state.blendDuration = item.value("blendDuration", 0.12f);
            state.blendEasing = item.value("blendEasing", 4);
            states.push_back(state);
        }
        for (const auto& item : root.value("transitions", json::array())) {
            AnimatorTransitionDefinition transition;
            transition.fromState = item.value("fromState", "");
            transition.toState = item.value("toState", "");
            transition.duration = item.value("duration", 0.12f);
            transition.easing = item.value("easing", 4);
            transition.hasExitTime = item.value("hasExitTime", false);
            transition.exitTime = item.value("exitTime", 0.9f);
            for (const auto& conditionItem : item.value("conditions", json::array())) {
                AnimatorConditionDefinition condition;
                condition.parameter = conditionItem.value("parameter", "");
                condition.mode = ToConditionMode(conditionItem.value("mode", 0));
                condition.threshold = conditionItem.value("threshold", 0.0f);
                transition.conditions.push_back(condition);
            }
            transitions.push_back(transition);
        }
    } catch (...) {
        Clear();
        return false;
    }

    if (entryState.empty() && !states.empty()) {
        entryState = states.front().name;
    }
    return true;
}

void AnimatorControllerRuntime::SetController(const AnimatorControllerAsset* controller, bool playEntryState) {
    controller_ = controller;
    Reset(playEntryState);
}

void AnimatorControllerRuntime::Reset(bool playEntryState) {
    currentStateIndex_ = -1;
    previousStateIndex_ = -1;
    currentTime_ = 0.0f;
    previousTime_ = 0.0f;
    transitionElapsed_ = 0.0f;
    transitionDuration_ = 0.0f;
    transitionEasing_ = 4;
    InitializeParameters();
    if (playEntryState && controller_) {
        const int entryIndex = controller_->FindStateIndex(controller_->entryState);
        currentStateIndex_ = entryIndex >= 0 ? entryIndex : (controller_->states.empty() ? -1 : 0);
    }
}

void AnimatorControllerRuntime::Update(float deltaTime, const DurationResolver& durationResolver) {
    if (!controller_ || currentStateIndex_ < 0 || deltaTime <= 0.0f) {
        return;
    }

    const AnimatorStateDefinition* current = GetCurrentState();
    AdvanceStateTime(currentTime_, current, deltaTime, durationResolver);
    if (IsTransitioning()) {
        AdvanceStateTime(previousTime_, GetPreviousState(), deltaTime, durationResolver);
        transitionElapsed_ += deltaTime;
        if (transitionElapsed_ >= transitionDuration_) {
            previousStateIndex_ = -1;
            previousTime_ = 0.0f;
            transitionElapsed_ = transitionDuration_;
        }
    }

    const float normalizedTime = GetNormalizedTime(current, currentTime_, durationResolver);
    if (const AnimatorTransitionDefinition* transition = FindAutomaticTransition(normalizedTime)) {
        const int destination = controller_->FindStateIndex(transition->toState);
        if (destination >= 0 && destination != currentStateIndex_) {
            ConsumeTriggers(*transition);
            CrossFade(transition->toState, transition->duration, transition->easing);
        }
    }
}

bool AnimatorControllerRuntime::Play(const std::string& stateName, float timeSeconds) {
    if (!controller_) {
        return false;
    }
    const int stateIndex = controller_->FindStateIndex(stateName);
    if (stateIndex < 0) {
        return false;
    }
    currentStateIndex_ = stateIndex;
    previousStateIndex_ = -1;
    currentTime_ = std::max(0.0f, timeSeconds);
    previousTime_ = 0.0f;
    transitionElapsed_ = 0.0f;
    transitionDuration_ = 0.0f;
    return true;
}

bool AnimatorControllerRuntime::CrossFade(const std::string& stateName, float durationOverride, int easingOverride) {
    if (!controller_) {
        return false;
    }
    const int destination = controller_->FindStateIndex(stateName);
    if (destination < 0) {
        return false;
    }
    if (destination == currentStateIndex_ && !IsTransitioning()) {
        return true;
    }

    float duration = durationOverride;
    int easing = easingOverride;
    if (const AnimatorTransitionDefinition* transition = FindRequestedTransition(currentStateIndex_, destination)) {
        if (duration < 0.0f) duration = transition->duration;
        if (easing < 0) easing = transition->easing;
    }
    const AnimatorStateDefinition& destinationState = controller_->states[destination];
    if (duration < 0.0f) duration = destinationState.blendDuration;
    if (easing < 0) easing = destinationState.blendEasing;

    previousStateIndex_ = currentStateIndex_;
    previousTime_ = currentTime_;
    currentStateIndex_ = destination;
    currentTime_ = 0.0f;
    transitionElapsed_ = 0.0f;
    transitionDuration_ = std::max(0.0f, duration);
    transitionEasing_ = std::clamp(easing, 0, 4);
    if (previousStateIndex_ < 0 || transitionDuration_ <= 0.0f) {
        previousStateIndex_ = -1;
        previousTime_ = 0.0f;
    }
    return true;
}

bool AnimatorControllerRuntime::IsPlayingState(const std::string& stateName) const {
    const AnimatorStateDefinition* state = GetCurrentState();
    return state && state->name == stateName;
}

const AnimatorStateDefinition* AnimatorControllerRuntime::GetCurrentState() const {
    if (!controller_ || currentStateIndex_ < 0 || currentStateIndex_ >= static_cast<int>(controller_->states.size())) {
        return nullptr;
    }
    return &controller_->states[currentStateIndex_];
}

const AnimatorStateDefinition* AnimatorControllerRuntime::GetPreviousState() const {
    if (!controller_ || previousStateIndex_ < 0 || previousStateIndex_ >= static_cast<int>(controller_->states.size())) {
        return nullptr;
    }
    return &controller_->states[previousStateIndex_];
}

float AnimatorControllerRuntime::GetTransitionWeight() const {
    if (!IsTransitioning()) {
        return 1.0f;
    }
    const float rate = std::clamp(transitionElapsed_ / std::max(transitionDuration_, 0.0001f), 0.0f, 1.0f);
    return AnimationInterpolation::ApplyEasing(rate, ToEasing(transitionEasing_));
}

void AnimatorControllerRuntime::SetFloat(const std::string& name, float value) { floatParameters_[name] = value; }
void AnimatorControllerRuntime::SetInt(const std::string& name, int value) { intParameters_[name] = value; }
void AnimatorControllerRuntime::SetBool(const std::string& name, bool value) { boolParameters_[name] = value; }
void AnimatorControllerRuntime::SetTrigger(const std::string& name) { triggerParameters_[name] = true; }
void AnimatorControllerRuntime::ResetTrigger(const std::string& name) { triggerParameters_[name] = false; }

float AnimatorControllerRuntime::GetFloat(const std::string& name) const {
    const auto it = floatParameters_.find(name);
    return it == floatParameters_.end() ? 0.0f : it->second;
}

int AnimatorControllerRuntime::GetInt(const std::string& name) const {
    const auto it = intParameters_.find(name);
    return it == intParameters_.end() ? 0 : it->second;
}

bool AnimatorControllerRuntime::GetBool(const std::string& name) const {
    const auto it = boolParameters_.find(name);
    return it != boolParameters_.end() && it->second;
}

bool AnimatorControllerRuntime::IsTriggerSet(const std::string& name) const {
    const auto it = triggerParameters_.find(name);
    return it != triggerParameters_.end() && it->second;
}

AnimatorControllerRuntime::Snapshot AnimatorControllerRuntime::CaptureSnapshot() const {
    Snapshot snapshot;
    if (!controller_) {
        return snapshot;
    }
    if (const auto* state = GetCurrentState()) snapshot.currentState = state->name;
    if (const auto* state = GetPreviousState()) snapshot.previousState = state->name;
    snapshot.currentTime = currentTime_;
    snapshot.previousTime = previousTime_;
    snapshot.transitionElapsed = transitionElapsed_;
    snapshot.transitionDuration = transitionDuration_;
    snapshot.transitionEasing = transitionEasing_;
    snapshot.valid = !snapshot.currentState.empty();
    return snapshot;
}

void AnimatorControllerRuntime::RestoreSnapshot(const Snapshot& snapshot) {
    if (!controller_ || !snapshot.valid) {
        return;
    }
    currentStateIndex_ = controller_->FindStateIndex(snapshot.currentState);
    previousStateIndex_ = controller_->FindStateIndex(snapshot.previousState);
    currentTime_ = snapshot.currentTime;
    previousTime_ = snapshot.previousTime;
    transitionElapsed_ = snapshot.transitionElapsed;
    transitionDuration_ = snapshot.transitionDuration;
    transitionEasing_ = snapshot.transitionEasing;
}

void AnimatorControllerRuntime::InitializeParameters() {
    floatParameters_.clear();
    intParameters_.clear();
    boolParameters_.clear();
    triggerParameters_.clear();
    if (!controller_) {
        return;
    }
    for (const auto& parameter : controller_->parameters) {
        switch (parameter.type) {
        case AnimatorParameterType::Float: floatParameters_[parameter.name] = parameter.defaultFloat; break;
        case AnimatorParameterType::Int: intParameters_[parameter.name] = parameter.defaultInt; break;
        case AnimatorParameterType::Bool: boolParameters_[parameter.name] = parameter.defaultBool; break;
        case AnimatorParameterType::Trigger: triggerParameters_[parameter.name] = false; break;
        }
    }
}

void AnimatorControllerRuntime::AdvanceStateTime(
    float& time,
    const AnimatorStateDefinition* state,
    float deltaTime,
    const DurationResolver& durationResolver) const {
    if (!state) {
        return;
    }
    time += deltaTime * std::max(0.0f, state->speed);
    const float duration = durationResolver ? durationResolver(state->clipName) : 0.0f;
    if (duration <= 0.0f) {
        return;
    }
    if (state->loop) {
        time = std::fmod(time, duration);
    } else {
        time = std::min(time, duration);
    }
}

float AnimatorControllerRuntime::GetNormalizedTime(
    const AnimatorStateDefinition* state,
    float time,
    const DurationResolver& durationResolver) const {
    if (!state || !durationResolver) {
        return 0.0f;
    }
    const float duration = durationResolver(state->clipName);
    return duration > 0.0f ? time / duration : 0.0f;
}

const AnimatorTransitionDefinition* AnimatorControllerRuntime::FindRequestedTransition(int fromIndex, int toIndex) const {
    if (!controller_ || toIndex < 0 || toIndex >= static_cast<int>(controller_->states.size())) {
        return nullptr;
    }
    const std::string from = fromIndex >= 0 ? controller_->states[fromIndex].name : "";
    const std::string& to = controller_->states[toIndex].name;
    for (const auto& transition : controller_->transitions) {
        if (transition.fromState == from && transition.toState == to) {
            return &transition;
        }
    }
    for (const auto& transition : controller_->transitions) {
        if ((transition.fromState == "*" || transition.fromState.empty()) && transition.toState == to) {
            return &transition;
        }
    }
    return nullptr;
}

const AnimatorTransitionDefinition* AnimatorControllerRuntime::FindAutomaticTransition(float normalizedTime) const {
    if (!controller_ || currentStateIndex_ < 0) {
        return nullptr;
    }
    const std::string& current = controller_->states[currentStateIndex_].name;
    // State固有の遷移をAny Stateより優先し、JSONの並び順で挙動が変わるのを防ぎます。
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& transition : controller_->transitions) {
            if (transition.toState.empty() || transition.toState == current) {
                continue;
            }
            const bool isSpecific = transition.fromState == current;
            const bool isAnyState = transition.fromState == "*" || transition.fromState.empty();
            if ((pass == 0 && !isSpecific) || (pass == 1 && !isAnyState)) {
                continue;
            }
            if (transition.hasExitTime && normalizedTime < transition.exitTime) {
                continue;
            }
            if (AreConditionsMet(transition)) {
                return &transition;
            }
        }
    }
    return nullptr;
}

bool AnimatorControllerRuntime::AreConditionsMet(const AnimatorTransitionDefinition& transition) const {
    if (transition.conditions.empty()) {
        return transition.hasExitTime;
    }
    for (const auto& condition : transition.conditions) {
        switch (condition.mode) {
        case AnimatorConditionMode::Greater:
            if (!(GetFloat(condition.parameter) > condition.threshold)) return false;
            break;
        case AnimatorConditionMode::Less:
            if (!(GetFloat(condition.parameter) < condition.threshold)) return false;
            break;
        case AnimatorConditionMode::Equals:
            if (GetInt(condition.parameter) != static_cast<int>(condition.threshold)) return false;
            break;
        case AnimatorConditionMode::NotEqual:
            if (GetInt(condition.parameter) == static_cast<int>(condition.threshold)) return false;
            break;
        case AnimatorConditionMode::If:
            if (!GetBool(condition.parameter)) return false;
            break;
        case AnimatorConditionMode::IfNot:
            if (GetBool(condition.parameter)) return false;
            break;
        case AnimatorConditionMode::Trigger:
            if (!IsTriggerSet(condition.parameter)) return false;
            break;
        }
    }
    return true;
}

void AnimatorControllerRuntime::ConsumeTriggers(const AnimatorTransitionDefinition& transition) {
    for (const auto& condition : transition.conditions) {
        if (condition.mode == AnimatorConditionMode::Trigger) {
            ResetTrigger(condition.parameter);
        }
    }
}
