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
// JSON では列挙値を整数で保存します。未知の値は既知の範囲へ丸めて読み込みます。
int ToInt(AnimatorParameterType type) {
    // JSONにはenumの数値を保存し、読み込み時に同じ型へ戻せるようにする。
    return static_cast<int>(type);
}

// 遷移条件の比較種別を保存用の整数へ変換します。
int ToInt(AnimatorConditionMode mode) {
    // 遷移条件の比較種別をJSONで扱える整数へ変換する。
    return static_cast<int>(mode);
}

// 保存値を有効範囲に丸めてAnimatorのパラメータ型へ戻します。
AnimatorParameterType ToParameterType(int value) {
    // 保存値を定義済みのパラメータ型の範囲に収める。
    return static_cast<AnimatorParameterType>(std::clamp(value, 0, 3));
}

// 保存値を有効範囲に丸めて遷移条件の比較種別へ戻します。
AnimatorConditionMode ToConditionMode(int value) {
    // 未知の比較種別は定義済みの範囲へ丸めてから読み込む。
    return static_cast<AnimatorConditionMode>(std::clamp(value, 0, 6));
}

AnimationInterpolation::EasingType ToEasing(int value) {
    // 補間指定を有効なenum範囲へ収めてから再生器へ渡す。
    return static_cast<AnimationInterpolation::EasingType>(std::clamp(value, 0, 4));
}
}

void AnimatorControllerAsset::Clear() {
    // 再読込前に状態・条件・パラメータをまとめて破棄する。
    version = kCurrentVersion;
    name = "animator_controller";
    entryState.clear();
    parameters.clear();
    states.clear();
    transitions.clear();
}

int AnimatorControllerAsset::FindStateIndex(const std::string& stateName) const {
    // 遷移先の検索は名前を基準にし、配列順の変更に依存させない。
    for (int index = 0; index < static_cast<int>(states.size()); ++index) {
        if (states[index].name == stateName) {
            return index;
        }
    }
    return -1;
}

const AnimatorStateDefinition* AnimatorControllerAsset::FindState(const std::string& stateName) const {
    // 名前が一致しない場合はnullptrを返し、未定義状態の参照を防ぐ。
    const int index = FindStateIndex(stateName);
    return index >= 0 ? &states[index] : nullptr;
}

bool AnimatorControllerAsset::Save(const std::string& filePath) const {
    json root;
    // 読み込んだ旧版の番号は引き継がず、現在の形式として書き出します。
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
        json stateJson = {
            { "name", state.name },
            { "clipName", state.clipName },
            { "bodyClipName", state.bodyClipName },
            { "speed", state.speed },
            { "loop", state.loop },
            { "blendDuration", state.blendDuration },
            { "blendEasing", state.blendEasing },
            { "eventTimelineDuration", state.eventTimelineDuration },
            { "events", json::array() }
        };
        for (const auto& event : state.events) {
            stateJson["events"].push_back({
                { "name", event.name },
                { "payload", event.payload },
                { "normalizedTime", event.normalizedTime }
            });
        }
        root["states"].push_back(stateJson);
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
        // 再読み込みでは古い配列へ追記せず、JSON の内容で定義全体を置き換えます。
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
            state.bodyClipName = item.value("bodyClipName", "");
            state.speed = item.value("speed", 1.0f);
            state.loop = item.value("loop", true);
            state.blendDuration = item.value("blendDuration", 0.12f);
            state.blendEasing = item.value("blendEasing", 4);
            // 不正な時刻とイベント位置を読み込んでも、発火判定の時間軸を有効範囲に保ちます。
            state.eventTimelineDuration = (std::max)(item.value("eventTimelineDuration", 1.0f), 0.01f);
            for (const auto& eventItem : item.value("events", json::array())) {
                AnimatorEventDefinition event;
                event.name = eventItem.value("name", "FeedbackCue");
                event.payload = eventItem.value("payload", "");
                event.normalizedTime = std::clamp(eventItem.value("normalizedTime", 0.0f), 0.0f, 1.0f);
                state.events.push_back(event);
            }
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
        // 途中まで読み込んだ定義を Runtime から参照させないため、失敗時は空に戻します。
        Clear();
        return false;
    }

    if (entryState.empty() && !states.empty()) {
        entryState = states.front().name;
    }
    return true;
}

void AnimatorControllerRuntime::SetController(const AnimatorControllerAsset* controller, bool playEntryState) {
    // Assetの差し替え後は前の状態を使わず、必要なら入口状態から再生する。
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
    pendingEvents_.clear();
    InitializeParameters();
    if (playEntryState && controller_) {
        const int entryIndex = controller_->FindStateIndex(controller_->entryState);
        // 初期状態名が欠ける古い定義でも、先頭の状態から再生できるようにします。
        currentStateIndex_ = entryIndex >= 0 ? entryIndex : (controller_->states.empty() ? -1 : 0);
        QueueEntryEvents(GetCurrentState());
    }
}

void AnimatorControllerRuntime::Update(float deltaTime, const DurationResolver& durationResolver) {
    if (!controller_ || currentStateIndex_ < 0 || deltaTime <= 0.0f) {
        return;
    }

    const AnimatorStateDefinition* current = GetCurrentState();
    const float previousCurrentTime = currentTime_;
    const float speed = current ? std::max(0.0f, current->speed) : 0.0f;
    const float clipDuration = ResolveStateDuration(current, durationResolver);
    // クリップ長が取れない状態でも、イベントと exitTime は同じ代替時間軸で評価します。
    // 実際のアニメーション再生位置はクリップ長が分からないまま折り返しません。
    const float eventDuration = clipDuration > 0.0f
        ? clipDuration
        : (current ? (std::max)(current->eventTimelineDuration, 0.01f) : 0.0f);
    const float rawNewTime = previousCurrentTime + deltaTime * speed;
    // 時刻をループ位置へ折り返す前に通過イベントを調べ、1 更新で複数周した場合も拾います。
    CollectCrossedEvents(current, previousCurrentTime, rawNewTime, eventDuration);
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

    // 折り返した時刻だけを見ると、大きな deltaTime で exitTime をまたいだ遷移を取りこぼします。
    const float normalizedTime = GetNormalizedTime(current, rawNewTime, durationResolver);
    if (const AnimatorTransitionDefinition* transition = FindAutomaticTransition(normalizedTime)) {
        const int destination = controller_->FindStateIndex(transition->toState);
        if (destination >= 0 && destination != currentStateIndex_) {
            // 行き先が有効なときだけ Trigger を消費し、失敗した定義で入力を失わないようにします。
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
    pendingEvents_.clear();
    // 時刻 0 からの再生だけ、開始位置に配置されたイベントを即時発火させます。
    if (currentTime_ <= 0.0001f) QueueEntryEvents(GetCurrentState());
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

    // 引数の負値は「未指定」です。遷移定義、遷移先状態の順で既定値を探します。
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
    QueueEntryEvents(GetCurrentState());
    return true;
}

bool AnimatorControllerRuntime::IsPlayingState(const std::string& stateName) const {
    // 現在状態が有効な場合だけ名前を比較する。
    const AnimatorStateDefinition* state = GetCurrentState();
    return state && state->name == stateName;
}

const AnimatorStateDefinition* AnimatorControllerRuntime::GetCurrentState() const {
    // Assetが無いかインデックスが範囲外なら、状態なしとしてnullptrを返す。
    if (!controller_ || currentStateIndex_ < 0 || currentStateIndex_ >= static_cast<int>(controller_->states.size())) {
        return nullptr;
    }
    return &controller_->states[currentStateIndex_];
}

const AnimatorStateDefinition* AnimatorControllerRuntime::GetPreviousState() const {
    // 補間元が存在する場合だけ旧状態を返す。
    if (!controller_ || previousStateIndex_ < 0 || previousStateIndex_ >= static_cast<int>(controller_->states.size())) {
        return nullptr;
    }
    return &controller_->states[previousStateIndex_];
}

float AnimatorControllerRuntime::GetTransitionWeight() const {
    // 遷移中は設定されたイージングで重みを計算し、通常時は現状態を100%使う。
    if (!IsTransitioning()) {
        return 1.0f;
    }
    const float rate = std::clamp(transitionElapsed_ / std::max(transitionDuration_, 0.0001f), 0.0f, 1.0f);
    return AnimationInterpolation::ApplyEasing(rate, ToEasing(transitionEasing_));
}

void AnimatorControllerRuntime::SetCurrentTime(float timeSeconds) {
    // スクラブ後に過去のイベントを再発火させないよう、待機中イベントを捨てる。
    currentTime_ = std::max(0.0f, timeSeconds);
    pendingEvents_.clear();
}

std::vector<AnimatorEventInstance> AnimatorControllerRuntime::ConsumeEvents() {
    // イベントを呼び出し側へ移し、次フレームの二重通知を防ぐ。
    std::vector<AnimatorEventInstance> result;
    result.swap(pendingEvents_);
    return result;
}

void AnimatorControllerRuntime::SetFloat(const std::string& name, float value) {
    // Float条件の評価に使う現在値を名前で保持する。
    floatParameters_[name] = value;
}
void AnimatorControllerRuntime::SetInt(const std::string& name, int value) {
    // Int条件の評価に使う現在値を名前で保持する。
    intParameters_[name] = value;
}
void AnimatorControllerRuntime::SetBool(const std::string& name, bool value) {
    // Bool条件の評価に使う現在値を名前で保持する。
    boolParameters_[name] = value;
}
void AnimatorControllerRuntime::SetTrigger(const std::string& name) {
    // Triggerは発火済み状態にし、遷移が使うまで保持する。
    triggerParameters_[name] = true;
}
void AnimatorControllerRuntime::ResetTrigger(const std::string& name) {
    // Triggerの消費後に再遷移しないよう発火状態を解除する。
    triggerParameters_[name] = false;
}

float AnimatorControllerRuntime::GetFloat(const std::string& name) const {
    // 未設定のFloat条件は0として評価する。
    const auto it = floatParameters_.find(name);
    return it == floatParameters_.end() ? 0.0f : it->second;
}

int AnimatorControllerRuntime::GetInt(const std::string& name) const {
    // 未設定のInt条件は0として評価する。
    const auto it = intParameters_.find(name);
    return it == intParameters_.end() ? 0 : it->second;
}

bool AnimatorControllerRuntime::GetBool(const std::string& name) const {
    // 未設定のBool条件はfalseとして評価する。
    const auto it = boolParameters_.find(name);
    return it != boolParameters_.end() && it->second;
}

bool AnimatorControllerRuntime::IsTriggerSet(const std::string& name) const {
    // 未登録Triggerは発火していないものとして扱う。
    const auto it = triggerParameters_.find(name);
    return it != triggerParameters_.end() && it->second;
}

AnimatorControllerRuntime::Snapshot AnimatorControllerRuntime::CaptureSnapshot() const {
    // Assetの再読み込み後も状態名を使って再生位置を戻せるよう、時間と補間情報を保存する。
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
    // 配列位置ではなく状態名で復元し、Asset 内の並び替えに依存しないようにします。
    currentStateIndex_ = controller_->FindStateIndex(snapshot.currentState);
    previousStateIndex_ = controller_->FindStateIndex(snapshot.previousState);
    currentTime_ = snapshot.currentTime;
    previousTime_ = snapshot.previousTime;
    transitionElapsed_ = snapshot.transitionElapsed;
    transitionDuration_ = snapshot.transitionDuration;
    transitionEasing_ = snapshot.transitionEasing;
    pendingEvents_.clear();
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
        // Trigger は持続値ではないため、定義の初期値に関係なく未発火から始めます。
        case AnimatorParameterType::Trigger: triggerParameters_[parameter.name] = false; break;
        }
    }
}

float AnimatorControllerRuntime::ResolveStateDuration(
    const AnimatorStateDefinition* state,
    const DurationResolver& durationResolver) const {
    // クリップ長が解決できない場合は0とし、時刻の折り返しを行わない。
    if (!state) {
        return 0.0f;
    }
    const float resolved = durationResolver ? durationResolver(*state) : 0.0f;
    return resolved > 0.0f ? resolved : 0.0f;
}

void AnimatorControllerRuntime::QueueEntryEvents(const AnimatorStateDefinition* state) {
    // 状態に入った瞬間の時刻0イベントだけを待機キューへ入れる。
    if (!state) {
        return;
    }
    for (const AnimatorEventDefinition& event : state->events) {
        if (event.normalizedTime <= 0.0001f) {
            pendingEvents_.push_back({ state->name, event.name, event.payload, event.normalizedTime });
        }
    }
}

void AnimatorControllerRuntime::CollectCrossedEvents(
    const AnimatorStateDefinition* state,
    float previousTime,
    float rawNewTime,
    float duration) {
    if (!state || state->events.empty() || duration <= 0.0f || rawNewTime <= previousTime) {
        return;
    }

    // Asset 側の配列順に依存しない発火順を保証するため、参照だけを時刻順に並べます。
    std::vector<const AnimatorEventDefinition*> sortedEvents;
    sortedEvents.reserve(state->events.size());
    for (const AnimatorEventDefinition& event : state->events) {
        sortedEvents.push_back(&event);
    }
    std::stable_sort(sortedEvents.begin(), sortedEvents.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->normalizedTime < rhs->normalizedTime;
    });

    // (前回時刻, 今回時刻] を対象にし、境界上のイベントを次フレームで重複発火させません。
    // rawNewTime はループ前の値なので、1 更新で複数周した場合も各周を調べられます。
    constexpr float kEventTimeEpsilon = 0.00001f;
    const int lastLoop = state->loop ? static_cast<int>(std::floor(rawNewTime / duration)) : 0;
    const int firstLoop = state->loop ? (std::max)(0, static_cast<int>(std::floor(previousTime / duration))) : 0;
    const float clampedEndTime = state->loop ? rawNewTime : (std::min)(rawNewTime, duration);
    for (int loopIndex = firstLoop; loopIndex <= lastLoop; ++loopIndex) {
        for (const AnimatorEventDefinition* event : sortedEvents) {
            const float eventTime =
                std::clamp(event->normalizedTime, 0.0f, 1.0f) * duration +
                static_cast<float>(loopIndex) * duration;
            if (eventTime > previousTime + kEventTimeEpsilon &&
                eventTime <= clampedEndTime + kEventTimeEpsilon) {
                pendingEvents_.push_back({ state->name, event->name, event->payload, event->normalizedTime });
            }
        }
        if (!state->loop) {
            break;
        }
    }
}

void AnimatorControllerRuntime::AdvanceStateTime(
    float& time,
    const AnimatorStateDefinition* state,
    float deltaTime,
    const DurationResolver& durationResolver) const {
    // ループ状態はクリップ長で折り返し、単発状態は終了時刻で止める。
    if (!state) {
        return;
    }
    // speed の負値は逆再生ではなく停止として扱う仕様です。
    time += deltaTime * std::max(0.0f, state->speed);
    const float duration = ResolveStateDuration(state, durationResolver);
    if (duration <= 0.0f) {
        // クリップ長が不明な場合は折り返し先を決められないため、時刻をそのまま進めます。
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
    // クリップ長が不明ならイベント用時間を代用し、正規化時刻のゼロ除算を避ける。
    if (!state) {
        return 0.0f;
    }
    const float clipDuration = ResolveStateDuration(state, durationResolver);
    const float duration = clipDuration > 0.0f
        ? clipDuration
        : (std::max)(state->eventTimelineDuration, 0.01f);
    return time / duration;
}

const AnimatorTransitionDefinition* AnimatorControllerRuntime::FindRequestedTransition(int fromIndex, int toIndex) const {
    if (!controller_ || toIndex < 0 || toIndex >= static_cast<int>(controller_->states.size())) {
        return nullptr;
    }
    const std::string from = fromIndex >= 0 ? controller_->states[fromIndex].name : "";
    const std::string& to = controller_->states[toIndex].name;
    // 明示的な切り替えでも、同じ宛先に複数定義があれば状態固有を優先します。
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
    // State 固有の遷移を Any State より優先します。同じ優先度内では定義順で選びます。
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& transition : controller_->transitions) {
            // 自分自身への自動遷移を除外し、毎フレームの再開始を防ぎます。
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
        // 条件も exitTime もない定義が毎フレーム発火し続けるのを防ぎます。
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
    // 成立した遷移で使用したTriggerだけを解除し、他の条件は維持する。
    for (const auto& condition : transition.conditions) {
        if (condition.mode == AnimatorConditionMode::Trigger) {
            ResetTrigger(condition.parameter);
        }
    }
}
