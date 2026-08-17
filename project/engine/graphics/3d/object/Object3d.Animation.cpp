#define NOMINMAX
#include "Object3d.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

namespace {
std::string ResolveAnimatorControllerPath(const std::string& assetPath) {
    if (assetPath.empty()) {
        return {};
    }
    std::filesystem::path path(assetPath);
    if (!path.has_parent_path()) {
        path = std::filesystem::path("Resources/json/animator") / path;
    }
    if (!path.has_extension()) {
        path += ".json";
    }
    return path.generic_string();
}
}

bool Object3d::SetAnimatorController(const std::string& assetPath) {
    if (assetPath.empty()) {
        ClearAnimatorController();
        return true;
    }
    const std::string resolvedPath = ResolveAnimatorControllerPath(assetPath);
    AnimatorControllerAsset loadedAsset;
    if (!loadedAsset.Load(resolvedPath)) {
        return false;
    }
    animatorControllerPath_ = assetPath;
    animatorControllerAsset_ = std::move(loadedAsset);
    animatorControllerLoaded_ = true;
    animatorControllerRuntime_.SetController(&animatorControllerAsset_, true);
    return true;
}

void Object3d::ClearAnimatorController() {
    animatorControllerRuntime_.SetController(nullptr, false);
    animatorControllerAsset_.Clear();
    animatorControllerPath_.clear();
    animatorControllerLoaded_ = false;
}

bool Object3d::PlayAnimatorState(const std::string& stateName, float blendDuration, int easing) {
    if (!animatorControllerLoaded_) {
        return false;
    }
    return animatorControllerRuntime_.CrossFade(stateName, blendDuration, easing);
}

bool Object3d::EvaluateAnimatorState(
    const std::string& stateName,
    float timeSeconds,
    bool loop,
    float blendDuration,
    int easing,
    bool exactPreview) {
    Model* model = GetModel();
    if (!model) {
        return false;
    }

    if (!animatorControllerLoaded_) {
        const Model::Animation* animation = model->GetAnimation(stateName);
        if (!animation) {
            return false;
        }
        animName_ = stateName;
        isAnimLoop_ = loop;
        animationTime_ = std::max(0.0f, timeSeconds);
        float sampleTime = animationTime_;
        if (loop && animation->duration > 0.0f) {
            sampleTime = std::fmod(sampleTime, animation->duration);
        } else {
            sampleTime = std::min(sampleTime, animation->duration);
        }
        model->ApplyAnimation(*animation, sampleTime);
        model->Update(true);
        return true;
    }

    if (!animatorControllerRuntime_.IsPlayingState(stateName)) {
        if (exactPreview) {
            animatorControllerRuntime_.Play(stateName, 0.0f);
        } else {
            animatorControllerRuntime_.CrossFade(stateName, blendDuration, easing);
        }
    }
    animatorControllerRuntime_.SetCurrentTime(std::max(0.0f, timeSeconds));
    return ApplyAnimatorControllerPose(model, true);
}

std::string Object3d::GetAnimatorCurrentStateName() const {
    const AnimatorStateDefinition* state = animatorControllerRuntime_.GetCurrentState();
    return state ? state->name : std::string{};
}

AnimatorControllerRuntime::Snapshot Object3d::CaptureAnimatorSnapshot() const {
    return animatorControllerRuntime_.CaptureSnapshot();
}

void Object3d::RestoreAnimatorSnapshot(const AnimatorControllerRuntime::Snapshot& snapshot) {
    if (!animatorControllerLoaded_ || !snapshot.valid) {
        return;
    }
    animatorControllerRuntime_.RestoreSnapshot(snapshot);
    ApplyAnimatorControllerPose(GetModel(), true);
}

void Object3d::RestoreAnimationPlayback(const std::string& animationName, float timeSeconds, bool loop) {
    animName_ = animationName;
    animationTime_ = std::max(0.0f, timeSeconds);
    isAnimLoop_ = loop;
    Model* model = GetModel();
    if (!model || animationName.empty()) {
        return;
    }
    if (const Model::Animation* animation = model->GetAnimation(animationName)) {
        float sampleTime = animationTime_;
        if (loop && animation->duration > 0.0f) {
            sampleTime = std::fmod(sampleTime, animation->duration);
        } else {
            sampleTime = std::min(sampleTime, animation->duration);
        }
        model->ApplyAnimation(*animation, sampleTime);
        model->Update(true);
    }
}

bool Object3d::TryGetJointWorldMatrix(const std::string& jointName, Matrix4x4& outMatrix) const {
    Model* model = GetModel();
    if (!model || jointName.empty()) {
        return false;
    }

    const int jointIndex = model->FindJointIndex(jointName);
    const auto& joints = model->GetJoints();
    if (jointIndex < 0 || jointIndex >= static_cast<int>(joints.size())) {
        return false;
    }

    outMatrix = Math::Multiply(joints[jointIndex].skeletonSpaceMatrix, GetWorldMatrix());
    return true;
}

bool Object3d::TryGetJointWorldPosition(const std::string& jointName, Vector3& outPosition) const {
    Matrix4x4 jointWorld = Math::MakeIdentity4x4();
    if (!TryGetJointWorldMatrix(jointName, jointWorld)) {
        return false;
    }
    outPosition = { jointWorld.m[3][0], jointWorld.m[3][1], jointWorld.m[3][2] };
    return true;
}

bool Object3d::UpdateAnimatorController(float deltaTime, Model* model) {
    if (!animatorControllerLoaded_ || !model) {
        return false;
    }
    animatorControllerRuntime_.Update(deltaTime, [model](const std::string& clipName) {
        const Model::Animation* animation = model->GetAnimation(clipName);
        return animation ? animation->duration : 0.0f;
    });
    return ApplyAnimatorControllerPose(model, false);
}

bool Object3d::ApplyAnimatorControllerPose(Model* model, bool forceModelUpdate) {
    if (!animatorControllerLoaded_ || !model) {
        return false;
    }
    const AnimatorStateDefinition* currentState = animatorControllerRuntime_.GetCurrentState();
    if (!currentState || currentState->clipName.empty()) {
        return false;
    }
    const Model::Animation* currentAnimation = model->GetAnimation(currentState->clipName);
    if (!currentAnimation) {
        return false;
    }

    animName_ = currentState->clipName;
    animationTime_ = animatorControllerRuntime_.GetStateTime();
    isAnimLoop_ = currentState->loop;
    const AnimatorStateDefinition* previousState = animatorControllerRuntime_.GetPreviousState();
    const Model::Animation* previousAnimation = previousState ? model->GetAnimation(previousState->clipName) : nullptr;
    if (previousAnimation && animatorControllerRuntime_.IsTransitioning()) {
        model->ApplyBlendedAnimation(
            *previousAnimation,
            animatorControllerRuntime_.GetPreviousTime(),
            *currentAnimation,
            animatorControllerRuntime_.GetStateTime(),
            animatorControllerRuntime_.GetTransitionWeight());
    } else {
        model->ApplyAnimation(*currentAnimation, animatorControllerRuntime_.GetStateTime());
    }
    model->Update(forceModelUpdate);
    return true;
}
