#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

enum class AnimatorParameterType {
    Float,
    Int,
    Bool,
    Trigger
};

enum class AnimatorConditionMode {
    Greater,
    Less,
    Equals,
    NotEqual,
    If,
    IfNot,
    Trigger
};

struct AnimatorParameterDefinition {
    std::string name;
    AnimatorParameterType type = AnimatorParameterType::Float;
    float defaultFloat = 0.0f;
    int defaultInt = 0;
    bool defaultBool = false;
};

struct AnimatorStateDefinition {
    std::string name;
    // Model内のSkeletal Animation名です。
    std::string clipName;
    // Animation Workbenchで作成したBody Transform Clip Asset名です。
    std::string bodyClipName;
    float speed = 1.0f;
    bool loop = true;
    float blendDuration = 0.12f;
    int blendEasing = 4;
};

struct AnimatorConditionDefinition {
    std::string parameter;
    AnimatorConditionMode mode = AnimatorConditionMode::Greater;
    float threshold = 0.0f;
};

struct AnimatorTransitionDefinition {
    std::string fromState;
    std::string toState;
    float duration = 0.12f;
    int easing = 4;
    bool hasExitTime = false;
    float exitTime = 0.9f;
    std::vector<AnimatorConditionDefinition> conditions;
};

class AnimatorControllerAsset {
public:
    static constexpr int kCurrentVersion = 1;

    void Clear();
    int FindStateIndex(const std::string& stateName) const;
    const AnimatorStateDefinition* FindState(const std::string& stateName) const;
    bool Load(const std::string& filePath);
    bool Save(const std::string& filePath) const;

    int version = kCurrentVersion;
    std::string name = "animator_controller";
    std::string entryState;
    std::vector<AnimatorParameterDefinition> parameters;
    std::vector<AnimatorStateDefinition> states;
    std::vector<AnimatorTransitionDefinition> transitions;
};

class AnimatorControllerRuntime {
public:
    using DurationResolver = std::function<float(const std::string& clipName)>;

    struct Snapshot {
        std::string currentState;
        std::string previousState;
        float currentTime = 0.0f;
        float previousTime = 0.0f;
        float transitionElapsed = 0.0f;
        float transitionDuration = 0.0f;
        int transitionEasing = 4;
        bool valid = false;
    };

    void SetController(const AnimatorControllerAsset* controller, bool playEntryState = true);
    const AnimatorControllerAsset* GetController() const { return controller_; }
    void Reset(bool playEntryState = true);
    void Update(float deltaTime, const DurationResolver& durationResolver);

    bool Play(const std::string& stateName, float timeSeconds = 0.0f);
    bool CrossFade(const std::string& stateName, float durationOverride = -1.0f, int easingOverride = -1);
    bool IsPlayingState(const std::string& stateName) const;
    bool IsTransitioning() const { return previousStateIndex_ >= 0 && transitionDuration_ > 0.0f; }

    const AnimatorStateDefinition* GetCurrentState() const;
    const AnimatorStateDefinition* GetPreviousState() const;
    float GetStateTime() const { return currentTime_; }
    float GetPreviousTime() const { return previousTime_; }
    float GetTransitionWeight() const;
    void SetCurrentTime(float timeSeconds) { currentTime_ = timeSeconds < 0.0f ? 0.0f : timeSeconds; }

    void SetFloat(const std::string& name, float value);
    void SetInt(const std::string& name, int value);
    void SetBool(const std::string& name, bool value);
    void SetTrigger(const std::string& name);
    void ResetTrigger(const std::string& name);
    float GetFloat(const std::string& name) const;
    int GetInt(const std::string& name) const;
    bool GetBool(const std::string& name) const;
    bool IsTriggerSet(const std::string& name) const;

    Snapshot CaptureSnapshot() const;
    void RestoreSnapshot(const Snapshot& snapshot);

private:
    void InitializeParameters();
    void AdvanceStateTime(float& time, const AnimatorStateDefinition* state, float deltaTime, const DurationResolver& durationResolver) const;
    float GetNormalizedTime(const AnimatorStateDefinition* state, float time, const DurationResolver& durationResolver) const;
    const AnimatorTransitionDefinition* FindRequestedTransition(int fromIndex, int toIndex) const;
    const AnimatorTransitionDefinition* FindAutomaticTransition(float normalizedTime) const;
    bool AreConditionsMet(const AnimatorTransitionDefinition& transition) const;
    void ConsumeTriggers(const AnimatorTransitionDefinition& transition);

    const AnimatorControllerAsset* controller_ = nullptr;
    int currentStateIndex_ = -1;
    int previousStateIndex_ = -1;
    float currentTime_ = 0.0f;
    float previousTime_ = 0.0f;
    float transitionElapsed_ = 0.0f;
    float transitionDuration_ = 0.0f;
    int transitionEasing_ = 4;

    std::unordered_map<std::string, float> floatParameters_;
    std::unordered_map<std::string, int> intParameters_;
    std::unordered_map<std::string, bool> boolParameters_;
    std::unordered_map<std::string, bool> triggerParameters_;
};
