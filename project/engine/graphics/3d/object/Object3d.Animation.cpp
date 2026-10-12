#define NOMINMAX
#include "Object3d.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

namespace {
std::string ResolveAnimatorControllerPath(const std::string& assetPath) {
    // 名前だけの指定は標準の Animator ディレクトリへ解決し、保存時の短い参照を許す。
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
    // 全 Body Clip の読み込み成功後にだけ現行 Controller を置き換える。
    if (assetPath.empty()) {
        ClearAnimatorController();
        return true;
    }
    const std::string resolvedPath = ResolveAnimatorControllerPath(assetPath);
    AnimatorControllerAsset loadedAsset;
    if (!loadedAsset.Load(resolvedPath)) {
        return false;
    }
    std::unordered_map<std::string, BodyAnimationClip> loadedBodyClips;
    for (const auto& state : loadedAsset.states) {
        if (state.bodyClipName.empty() || loadedBodyClips.contains(state.bodyClipName)) continue;
        BodyAnimationClip clip;
        if (!clip.Load(BodyAnimationClip::ResolveAssetPath(state.bodyClipName))) return false;
        loadedBodyClips.emplace(state.bodyClipName, std::move(clip));
    }
    animatorControllerPath_ = assetPath;
    animatorControllerAsset_ = std::move(loadedAsset);
    animatorBodyClips_ = std::move(loadedBodyClips);
    animatorControllerLoaded_ = true;
    animatorControllerRuntime_.SetController(&animatorControllerAsset_, true);
    return true;
}

void Object3d::ClearAnimatorController() {
    // 身体補正を外してからアセットを破棄し、次のモデルへ変形を持ち越さない。
    if (animatorBodyPoseApplied_ && meshRenderer_) meshRenderer_->ResetVisualTransform();
    animatorBodyPoseApplied_ = false;
    animatorBodyClips_.clear();
    animatorControllerRuntime_.SetController(nullptr, false);
    animatorControllerAsset_.Clear();
    animatorControllerPath_.clear();
    animatorControllerLoaded_ = false;
}

bool Object3d::PlayAnimatorState(const std::string& stateName, float blendDuration, int easing) {
    // Controller 未設定時に状態遷移だけを先行させない。
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
    // 編集プレビューは時刻を直接指定し、実戦では Controller の補間経路を使う。
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
    // Controller に状態がない間は空名を返し、呼び出し側の代替表示に任せる。
    const AnimatorStateDefinition* state = animatorControllerRuntime_.GetCurrentState();
    return state ? state->name : std::string{};
}

AnimatorControllerRuntime::Snapshot Object3d::CaptureAnimatorSnapshot() const {
    // プレビューを抜けた後に元の再生時刻へ戻せるよう、実行状態を保存する。
    return animatorControllerRuntime_.CaptureSnapshot();
}

void Object3d::RestoreAnimatorSnapshot(const AnimatorControllerRuntime::Snapshot& snapshot) {
    // 時刻だけでなく見た目の姿勢も同期して、プレビュー終了時の残像を防ぐ。
    if (!animatorControllerLoaded_ || !snapshot.valid) {
        return;
    }
    animatorControllerRuntime_.RestoreSnapshot(snapshot);
    ApplyAnimatorControllerPose(GetModel(), true);
}

void Object3d::RestoreAnimationPlayback(const std::string& animationName, float timeSeconds, bool loop) {
    // Controller を使わないモデルの再生時刻も、巻き戻し直後に姿勢へ反映する。
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
    // ボーンの局所姿勢と Object3d のワールド行列を合成して装着位置を得る。
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
    // ジョイントが見つからない場合は出力位置を変更しない。
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
    animatorControllerRuntime_.Update(deltaTime, [this, model](const AnimatorStateDefinition& state) {
        if (const auto body = animatorBodyClips_.find(state.bodyClipName); body != animatorBodyClips_.end())
            return body->second.GetDuration();
        const Model::Animation* animation = model->GetAnimation(state.clipName);
        return animation ? animation->duration : 0.0f;
    });

    for (const AnimatorEventInstance& event : animatorControllerRuntime_.ConsumeEvents()) {
        // FeedbackCueはVFX、SE、カメラ、振動、ヒットストップを一つのAssetから再生します。
        if (event.name == "FeedbackCue" && !event.payload.empty()) {
            VFXSequencer::PlayOneShotOnTarget(event.payload, this);
        }
        if (animatorEventCallback_) {
            animatorEventCallback_(event);
        }
    }
    return ApplyAnimatorControllerPose(model, false);
}

bool Object3d::ApplyAnimatorControllerPose(Model* model, bool forceModelUpdate) {
    // 前後状態を同じ時刻基準で標本化し、骨格と身体変形を一つの補間率で合成する。
    if (!animatorControllerLoaded_ || !model) {
        return false;
    }
    const AnimatorStateDefinition* currentState = animatorControllerRuntime_.GetCurrentState();
    if (!currentState) {
        return false;
    }
    const AnimatorStateDefinition* previousState = animatorControllerRuntime_.GetPreviousState();
    const bool transitioning = previousState && animatorControllerRuntime_.IsTransitioning();
    const auto sampleBody = [this](const AnimatorStateDefinition* state, float time) {
        BodyAnimationClip::Sample sample;
        if (state) {
            const auto clip = animatorBodyClips_.find(state->bodyClipName);
            if (clip != animatorBodyClips_.end()) clip->second.Evaluate(time, state->loop, sample);
        }
        return sample;
    };
    const bool hasBody = !currentState->bodyClipName.empty() ||
        (transitioning && !previousState->bodyClipName.empty());
    if (hasBody && meshRenderer_) {
        const auto current = sampleBody(currentState, animatorControllerRuntime_.GetStateTime());
        const auto previous = transitioning
            ? sampleBody(previousState, animatorControllerRuntime_.GetPreviousTime()) : current;
        const float weight = transitioning ? animatorControllerRuntime_.GetTransitionWeight() : 1.0f;
        const auto blendAngle = [weight](float from, float to) {
            return Math::LerpShortAngle(from, to, weight);
        };
        meshRenderer_->SetVisualTransform(Math::Lerp(previous.scale, current.scale, weight),
            {blendAngle(previous.rotate.x, current.rotate.x), blendAngle(previous.rotate.y, current.rotate.y),
             blendAngle(previous.rotate.z, current.rotate.z)},
            Math::Lerp(previous.translate, current.translate, weight));
        meshRenderer_->SetVisualShear(Math::Lerp(previous.shear, current.shear, weight),
            Math::Lerp(previous.shearPivotY, current.shearPivotY, weight));
        animatorBodyPoseApplied_ = true;
    } else if (animatorBodyPoseApplied_ && meshRenderer_) {
        meshRenderer_->ResetVisualTransform();
        animatorBodyPoseApplied_ = false;
    }

    const Model::Animation* currentAnimation = currentState->clipName.empty()
        ? nullptr : model->GetAnimation(currentState->clipName);
    const Model::Animation* previousAnimation = transitioning && !previousState->clipName.empty()
        ? model->GetAnimation(previousState->clipName) : nullptr;
    if (currentAnimation && previousAnimation) {
        model->ApplyBlendedAnimation(
            *previousAnimation,
            animatorControllerRuntime_.GetPreviousTime(),
            *currentAnimation,
            animatorControllerRuntime_.GetStateTime(),
            animatorControllerRuntime_.GetTransitionWeight());
    } else if (currentAnimation) {
        model->ApplyAnimation(*currentAnimation, animatorControllerRuntime_.GetStateTime());
    }
    if (!hasBody && !currentAnimation) return false;
    animName_ = currentState->clipName;
    animationTime_ = animatorControllerRuntime_.GetStateTime();
    isAnimLoop_ = currentState->loop;
    model->Update(forceModelUpdate || currentAnimation != nullptr);
    return true;
}
