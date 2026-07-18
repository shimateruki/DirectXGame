#define NOMINMAX
#include "CinematicPlayer.h"

#include "AnimationInterpolation.h"
#include "BaseScene.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "GameAudioSettings.h"
#include "GhostRecorder.h"
#include "Object3d.h"
#include "SceneManager.h"

#include <algorithm>
#include <cmath>

namespace {
AnimationInterpolation::EasingType ToEasingType(int value) {
    value = std::clamp(value, 0, 4);
    return static_cast<AnimationInterpolation::EasingType>(value);
}

Vector3 AddVector3(const Vector3& a, const Vector3& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

Vector3 MultiplyVector3(const Vector3& a, const Vector3& b) {
    return { a.x * b.x, a.y * b.y, a.z * b.z };
}

void ApplyPose(Object3d* object, const Vector3& position, const Vector3& rotation, const Vector3& scale) {
    if (!object) {
        return;
    }
    object->SetTranslate(position);
    object->SetRotation(rotation);
    object->SetScale(scale);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
}
}

void CinematicPlayer::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
    sequence_ = nullptr;
    boundScene_ = nullptr;
    transformRuntime_.clear();
    vfxRuntime_.clear();
    audioRuntime_.clear();
    animationRuntime_.clear();
    currentTime_ = 0.0f;
    isPlaying_ = false;
    isPreviewing_ = false;
    isLooping_ = false;
    activeCameraObject_ = nullptr;
    activeCameraShotIndex_ = -1;
}

void CinematicPlayer::SetSequence(CinematicSequence* sequence) {
    if (isPlaying_ || isPreviewing_) {
        Stop(true);
    }
    sequence_ = sequence;
    currentTime_ = 0.0f;
    RefreshBindings();
}

void CinematicPlayer::RefreshBindings() {
    boundScene_ = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    transformRuntime_.clear();
    vfxRuntime_.clear();
    audioRuntime_.clear();
    animationRuntime_.clear();

    if (!sequence_) {
        return;
    }

    transformRuntime_.resize(sequence_->transformTracks.size());
    for (size_t index = 0; index < sequence_->transformTracks.size(); ++index) {
        const auto& track = sequence_->transformTracks[index];
        auto& runtime = transformRuntime_[index];
        runtime.target = ResolveTarget(track.binding);
        if (runtime.target && runtime.target->recorder_) {
            runtime.target->recorder_->SetSceneManager(sceneManager_);
            runtime.target->recorder_->SetTarget(runtime.target);
            if (!track.legacyPathFile.empty()) {
                runtime.target->recorder_->Load(track.legacyPathFile);
            }
        }
    }

    vfxRuntime_.resize(sequence_->vfxTracks.size());
    for (size_t index = 0; index < sequence_->vfxTracks.size(); ++index) {
        const auto& track = sequence_->vfxTracks[index];
        auto& runtime = vfxRuntime_[index];
        runtime.target = ResolveTarget(track.binding);
        runtime.sequencer.Initialize(runtime.target);
        if (!track.sequenceName.empty()) {
            runtime.sequencer.Load(track.sequenceName);
            sequence_->vfxTracks[index].duration = std::max(0.1f, runtime.sequencer.GetDuration());
        }
    }
    audioRuntime_.resize(sequence_->audioClips.size());
    animationRuntime_.resize(sequence_->animationClips.size());
    for (size_t index = 0; index < sequence_->animationClips.size(); ++index) {
        animationRuntime_[index].target = ResolveTarget(sequence_->animationClips[index].binding);
    }
}

void CinematicPlayer::Play(bool loop) {
    Stop(true);
    RefreshBindings();
    CaptureBasePoses();
    ResetRuntimeEvents();
    currentTime_ = 0.0f;
    isLooping_ = loop;
    isPlaying_ = true;
    isPreviewing_ = false;
    Evaluate(0.0f, -1.0f / 60.0f, true);
}

void CinematicPlayer::Resume(bool loop) {
    if (!sequence_) {
        return;
    }
    if (!isPreviewing_) {
        Play(loop);
        return;
    }

    isLooping_ = loop;
    isPreviewing_ = false;
    isPlaying_ = true;
    ResetRuntimeEvents();
    for (size_t index = 0; index < sequence_->audioClips.size() && index < audioRuntime_.size(); ++index) {
        const auto& clip = sequence_->audioClips[index];
        const float endTime = clip.startTime + std::max(0.05f, clip.duration);
        if (clip.enabled && !clip.muted && clip.loop && !clip.audioId.empty() &&
            currentTime_ >= clip.startTime && currentTime_ < endTime) {
            GameAudioSettings::GetInstance()->PlaySE(clip.audioId, clip.volume, true);
            audioRuntime_[index].started = true;
        }
    }
}

void CinematicPlayer::Pause() {
    if (!isPlaying_) {
        return;
    }

    for (auto& runtime : vfxRuntime_) {
        runtime.sequencer.Stop();
        runtime.started = false;
    }
    StopAudioClips();
    isPlaying_ = false;
    isPreviewing_ = true;
}

void CinematicPlayer::Stop(bool restorePose) {
    for (auto& runtime : vfxRuntime_) {
        runtime.sequencer.Stop();
        runtime.started = false;
    }
    StopAudioClips();
    StopCameraTrack();
    if (restorePose) {
        RestoreBasePoses();
    }
    isPlaying_ = false;
    isPreviewing_ = false;
    currentTime_ = 0.0f;
    lastEventId_ = 0;
    lastEventTarget_ = nullptr;
}

void CinematicPlayer::Update(float deltaTime) {
    BaseScene* currentScene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (currentScene != boundScene_) {
        Stop(false);
        RefreshBindings();
        return;
    }
    if (!isPlaying_ || !sequence_ || deltaTime <= 0.0f) {
        return;
    }

    const float duration = GetDuration();
    const float previousTime = currentTime_;
    float nextTime = currentTime_ + deltaTime;
    if (nextTime >= duration) {
        if (isLooping_) {
            Evaluate(duration, previousTime, true);
            RestoreBasePoses();
            CaptureBasePoses();
            ResetRuntimeEvents();
            nextTime = std::fmod(nextTime, std::max(duration, 0.1f));
            currentTime_ = 0.0f;
            Evaluate(nextTime, -1.0f / 60.0f, true);
            currentTime_ = nextTime;
            return;
        }

        Evaluate(duration, previousTime, true);
        currentTime_ = duration;
        isPlaying_ = false;
        return;
    }

    Evaluate(nextTime, previousTime, true);
    currentTime_ = nextTime;
}

void CinematicPlayer::SetTime(float timeSeconds, bool dispatchEvents) {
    if (!sequence_) {
        return;
    }
    if (!isPlaying_ && !isPreviewing_) {
        BeginPreview();
    }

    const float nextTime = std::clamp(timeSeconds, 0.0f, GetDuration());
    const float previousTime = currentTime_;
    Evaluate(nextTime, previousTime, dispatchEvents);
    currentTime_ = nextTime;
}

void CinematicPlayer::BeginPreview() {
    if (isPlaying_ || isPreviewing_ || !sequence_) {
        return;
    }
    RefreshBindings();
    CaptureBasePoses();
    ResetRuntimeEvents();
    isPreviewing_ = true;
}

void CinematicPlayer::EndPreview(bool restorePose) {
    if (!isPreviewing_) {
        return;
    }
    StopCameraTrack();
    StopAudioClips();
    if (restorePose) {
        RestoreBasePoses();
    }
    isPreviewing_ = false;
    currentTime_ = 0.0f;
}

float CinematicPlayer::GetDuration() const {
    if (!sequence_) {
        return 0.1f;
    }
    float result = sequence_->GetAuthoredDuration();
    for (size_t index = 0; index < sequence_->transformTracks.size(); ++index) {
        result = std::max(result, sequence_->transformTracks[index].startTime + GetTransformTrackDuration(index));
    }
    for (size_t index = 0; index < sequence_->vfxTracks.size() && index < vfxRuntime_.size(); ++index) {
        result = std::max(result, sequence_->vfxTracks[index].startTime + vfxRuntime_[index].sequencer.GetDuration());
    }
    return std::max(result, 0.1f);
}

bool CinematicPlayer::GetBasePose(Object3d* object, Vector3& position, Vector3& rotation, Vector3& scale) const {
    if (!object) {
        return false;
    }
    for (const auto& runtime : transformRuntime_) {
        if (runtime.target == object && runtime.captured) {
            position = runtime.basePosition;
            rotation = runtime.baseRotation;
            scale = runtime.baseScale;
            return true;
        }
    }
    return false;
}

Object3d* CinematicPlayer::ResolveTarget(const CinematicObjectBinding& binding) const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return nullptr;
    }

    Object3d* nameMatch = nullptr;
    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (!object) {
            continue;
        }
        if (binding.targetEventId >= 0 && object->GetEventID() == binding.targetEventId) {
            return object.get();
        }
        if (!binding.targetName.empty() && object->GetName() == binding.targetName) {
            nameMatch = object.get();
        }
    }
    return nameMatch;
}

float CinematicPlayer::GetTransformTrackDuration(size_t index) const {
    if (!sequence_ || index >= sequence_->transformTracks.size()) {
        return 0.0f;
    }
    const auto& track = sequence_->transformTracks[index];
    if (!track.keys.empty()) {
        return std::max(0.0f, track.keys.back().time);
    }
    if (index < transformRuntime_.size() && transformRuntime_[index].target &&
        transformRuntime_[index].target->recorder_ && !track.legacyPathFile.empty()) {
        return transformRuntime_[index].target->recorder_->GetDurationSeconds();
    }
    return 0.0f;
}

void CinematicPlayer::CaptureBasePoses() {
    for (auto& runtime : transformRuntime_) {
        if (!runtime.target) {
            continue;
        }
        runtime.basePosition = runtime.target->GetTranslate();
        runtime.baseRotation = runtime.target->GetRotation();
        runtime.baseScale = runtime.target->GetScale();
        runtime.captured = true;
        if (runtime.target->recorder_) {
            runtime.target->recorder_->CaptureBasePose();
        }
    }
    for (auto& runtime : animationRuntime_) {
        if (!runtime.target) {
            continue;
        }
        runtime.animatorSnapshot = runtime.target->CaptureAnimatorSnapshot();
        runtime.legacyAnimationName = runtime.target->animName_;
        runtime.legacyAnimationTime = runtime.target->GetAnimationTime();
        runtime.legacyLoop = runtime.target->isAnimLoop_;
        runtime.captured = true;
    }
}

void CinematicPlayer::RestoreBasePoses() {
    for (auto& runtime : transformRuntime_) {
        if (!runtime.target || !runtime.captured) {
            continue;
        }
        ApplyPose(runtime.target, runtime.basePosition, runtime.baseRotation, runtime.baseScale);
        runtime.captured = false;
    }
    for (size_t index = 0; sequence_ && index < animationRuntime_.size() && index < sequence_->animationClips.size(); ++index) {
        auto& runtime = animationRuntime_[index];
        if (!runtime.target || !runtime.captured || !sequence_->animationClips[index].restoreOnStop) {
            continue;
        }
        if (runtime.animatorSnapshot.valid) {
            runtime.target->RestoreAnimatorSnapshot(runtime.animatorSnapshot);
        } else {
            runtime.target->RestoreAnimationPlayback(
                runtime.legacyAnimationName,
                runtime.legacyAnimationTime,
                runtime.legacyLoop);
        }
        runtime.captured = false;
    }
}

void CinematicPlayer::ResetRuntimeEvents() {
    for (auto& runtime : vfxRuntime_) {
        runtime.sequencer.Stop();
        runtime.started = false;
    }
    for (auto& runtime : audioRuntime_) {
        runtime.started = false;
    }
    lastEventId_ = 0;
    lastEventTarget_ = nullptr;
}

void CinematicPlayer::Evaluate(float timeSeconds, float previousTimeSeconds, bool dispatchEvents) {
    lastEventId_ = 0;
    lastEventTarget_ = nullptr;

    for (size_t index = 0; sequence_ && index < sequence_->transformTracks.size(); ++index) {
        EvaluateTransformTrack(index, timeSeconds, previousTimeSeconds, dispatchEvents);
    }

    EvaluateAnimationClips(timeSeconds);

    if (dispatchEvents) {
        for (const auto& marker : sequence_->events) {
            if (marker.time > previousTimeSeconds && marker.time <= timeSeconds) {
                Object3d* target = ResolveTarget(marker.binding);
                if (target && marker.eventId != 0) {
                    target->OnRecordEvent(marker.eventId);
                }
                lastEventId_ = marker.eventId;
                lastEventTarget_ = target;
            }
        }
        DispatchSignals(timeSeconds, previousTimeSeconds);
    }

    if (isPlaying_) {
        UpdateVFXTracks(timeSeconds, previousTimeSeconds);
        UpdateAudioClips(timeSeconds, previousTimeSeconds);
    }
    UpdateCameraTrack(timeSeconds);
}

void CinematicPlayer::EvaluateTransformTrack(size_t index, float timeSeconds, float previousTimeSeconds, bool dispatchEvents) {
    if (!sequence_ || index >= sequence_->transformTracks.size() || index >= transformRuntime_.size()) {
        return;
    }
    const auto& track = sequence_->transformTracks[index];
    auto& runtime = transformRuntime_[index];
    if (!runtime.target) {
        return;
    }

    if (!track.enabled || track.muted) {
        if (runtime.captured) {
            ApplyPose(runtime.target, runtime.basePosition, runtime.baseRotation, runtime.baseScale);
        }
        return;
    }

    const float localTime = timeSeconds - track.startTime;
    const float previousLocalTime = previousTimeSeconds - track.startTime;
    const float trackDuration = GetTransformTrackDuration(index);
    if (localTime < 0.0f) {
        if (runtime.captured) {
            ApplyPose(runtime.target, runtime.basePosition, runtime.baseRotation, runtime.baseScale);
        }
        return;
    }

    if (localTime > trackDuration && !track.holdLast) {
        if (runtime.captured) {
            ApplyPose(runtime.target, runtime.basePosition, runtime.baseRotation, runtime.baseScale);
        }
        return;
    }
    const float evaluateTime = std::clamp(localTime, 0.0f, trackDuration);

    if (track.keys.empty()) {
        if (runtime.target->recorder_ && !track.legacyPathFile.empty()) {
            runtime.target->recorder_->EvaluateAtTime(evaluateTime, dispatchEvents, previousLocalTime);
        }
        return;
    }

    if (evaluateTime < track.keys.front().time) {
        if (runtime.captured) {
            ApplyPose(runtime.target, runtime.basePosition, runtime.baseRotation, runtime.baseScale);
        }
        return;
    }

    CinematicTransformKey value = track.keys.front();
    if (evaluateTime >= track.keys.back().time) {
        value = track.keys.back();
    } else {
        for (size_t keyIndex = 0; keyIndex + 1 < track.keys.size(); ++keyIndex) {
            const auto& a = track.keys[keyIndex];
            const auto& b = track.keys[keyIndex + 1];
            if (evaluateTime < a.time || evaluateTime > b.time) {
                continue;
            }
            const float raw = AnimationInterpolation::SegmentRate(evaluateTime, a.time, b.time);
            const float eased = AnimationInterpolation::ApplyEasing(raw, ToEasingType(a.easingToNext));
            value.time = evaluateTime;
            value.position = AnimationInterpolation::Lerp(a.position, b.position, eased);
            value.rotation = AnimationInterpolation::SlerpEuler(a.rotation, b.rotation, eased);
            value.scale = AnimationInterpolation::Lerp(a.scale, b.scale, eased);
            break;
        }
    }

    ApplyPose(
        runtime.target,
        track.relative ? AddVector3(runtime.basePosition, value.position) : value.position,
        track.relative ? AddVector3(runtime.baseRotation, value.rotation) : value.rotation,
        track.relative ? MultiplyVector3(runtime.baseScale, value.scale) : value.scale);
}

void CinematicPlayer::EvaluateAnimationClips(float timeSeconds) {
    if (!sequence_) {
        return;
    }
    for (size_t index = 0; index < sequence_->animationClips.size(); ++index) {
        const auto& clip = sequence_->animationClips[index];
        if (!clip.enabled || clip.muted) {
            continue;
        }
        const float duration = std::max(0.05f, clip.duration);
        const float elapsed = timeSeconds - clip.startTime;
        if (elapsed < 0.0f || elapsed > duration) {
            continue;
        }
        float localTime = elapsed * std::max(0.0f, clip.playbackSpeed);
        if (clip.loop && duration > 0.0f) {
            localTime = std::fmod(localTime, duration);
        }
        Object3d* target = index < animationRuntime_.size() ? animationRuntime_[index].target : ResolveTarget(clip.binding);
        if (target && (clip.driver.empty() || clip.driver == "Animator" || clip.driver == "Model" || clip.driver == "Skeletal")) {
            target->EvaluateAnimatorState(
                clip.clipName,
                localTime,
                clip.loop,
                clip.blendInDuration,
                clip.easing,
                isPreviewing_);
        }
        if (animationCallback_) {
            animationCallback_(target, clip, localTime, isPreviewing_);
        }
    }
}

void CinematicPlayer::DispatchSignals(float timeSeconds, float previousTimeSeconds) {
    if (!sequence_ || !signalCallback_ || timeSeconds < previousTimeSeconds) {
        return;
    }
    for (const auto& signal : sequence_->signals) {
        if (!signal.enabled || signal.time <= previousTimeSeconds || signal.time > timeSeconds) {
            continue;
        }
        signalCallback_(ResolveTarget(signal.binding), signal, isPreviewing_);
    }
}

void CinematicPlayer::UpdateVFXTracks(float timeSeconds, float previousTimeSeconds) {
    if (!sequence_) {
        return;
    }
    for (size_t index = 0; index < sequence_->vfxTracks.size() && index < vfxRuntime_.size(); ++index) {
        const auto& track = sequence_->vfxTracks[index];
        auto& runtime = vfxRuntime_[index];
        if (!track.enabled || track.muted || track.sequenceName.empty()) {
            continue;
        }

        if (!runtime.started && timeSeconds >= track.startTime) {
            runtime.sequencer.SetTargetObject(runtime.target);
            runtime.sequencer.Play();
            runtime.started = true;
            const float catchUp = std::max(0.0f, timeSeconds - track.startTime);
            if (catchUp > 0.0f) {
                runtime.sequencer.Update(catchUp);
            }
        } else if (runtime.started) {
            runtime.sequencer.Update(std::max(0.0f, timeSeconds - previousTimeSeconds));
        }
    }
}

void CinematicPlayer::UpdateAudioClips(float timeSeconds, float previousTimeSeconds) {
    if (!sequence_) {
        return;
    }
    for (size_t index = 0; index < sequence_->audioClips.size() && index < audioRuntime_.size(); ++index) {
        const auto& clip = sequence_->audioClips[index];
        auto& runtime = audioRuntime_[index];
        if (!clip.enabled || clip.muted || clip.audioId.empty()) {
            continue;
        }

        const float endTime = clip.startTime + std::max(0.05f, clip.duration);
        const bool crossedStart = clip.startTime > previousTimeSeconds && clip.startTime <= timeSeconds;
        if (!runtime.started && crossedStart && (!clip.loop || timeSeconds < endTime)) {
            GameAudioSettings::GetInstance()->PlaySE(clip.audioId, clip.volume, clip.loop);
            runtime.started = true;
        }
        if (runtime.started && clip.loop && timeSeconds >= endTime) {
            GameAudioSettings::GetInstance()->Stop(clip.audioId);
            runtime.started = false;
        }
    }
}

void CinematicPlayer::StopAudioClips() {
    if (!sequence_) {
        return;
    }
    for (size_t index = 0; index < sequence_->audioClips.size() && index < audioRuntime_.size(); ++index) {
        if (!audioRuntime_[index].started) {
            continue;
        }
        const auto& clip = sequence_->audioClips[index];
        if (!clip.audioId.empty()) {
            GameAudioSettings::GetInstance()->Stop(clip.audioId);
        }
        audioRuntime_[index].started = false;
    }
}

void CinematicPlayer::UpdateCameraTrack(float timeSeconds) {
    if (!sequence_) {
        return;
    }

    Object3d* requestedCamera = nullptr;
    float requestedStartTime = -1.0f;
    int requestedShotIndex = -1;
    if (!sequence_->cameraShots.empty()) {
        for (size_t index = 0; index < sequence_->cameraShots.size(); ++index) {
            const auto& shot = sequence_->cameraShots[index];
            if (!shot.enabled || shot.muted) {
                continue;
            }
            const float endTime = shot.startTime + std::max(0.05f, shot.duration);
            if (timeSeconds >= shot.startTime && timeSeconds <= endTime && shot.startTime >= requestedStartTime) {
                Object3d* target = ResolveTarget(shot.binding);
                if (target && target->IsCameraObject()) {
                    requestedCamera = target;
                    requestedStartTime = shot.startTime;
                    requestedShotIndex = static_cast<int>(index);
                }
            }
        }
    } else {
        // v3以前のSequenceはCamera ObjectのTransform Trackをカメラカットとして扱います。
        for (size_t index = 0; index < sequence_->transformTracks.size() && index < transformRuntime_.size(); ++index) {
            const auto& track = sequence_->transformTracks[index];
            Object3d* target = transformRuntime_[index].target;
            if (!track.enabled || track.muted || !target || !target->IsCameraObject()) {
                continue;
            }
            const float endTime = track.startTime + GetTransformTrackDuration(index);
            if (timeSeconds >= track.startTime && (track.holdLast || timeSeconds <= endTime) && track.startTime >= requestedStartTime) {
                requestedCamera = target;
                requestedStartTime = track.startTime;
            }
        }
    }

    if (requestedCamera == activeCameraObject_ && requestedShotIndex == activeCameraShotIndex_) {
        return;
    }
    StopCameraTrack();
    if (requestedCamera) {
        bool started = false;
        if (requestedShotIndex >= 0) {
            const auto& shot = sequence_->cameraShots[requestedShotIndex];
            started = CameraEditor::GetInstance()->PlaySceneObjectCamera(
                CameraManager::GetInstance()->GetMainCamera(), requestedCamera,
                shot.blendInDuration, shot.blendOutDuration, shot.easing);
        } else {
            started = CameraEditor::GetInstance()->PlaySceneObjectCamera(
                CameraManager::GetInstance()->GetMainCamera(), requestedCamera);
        }
        if (started) {
            activeCameraObject_ = requestedCamera;
            activeCameraShotIndex_ = requestedShotIndex;
        }
    }
}

void CinematicPlayer::StopCameraTrack() {
    if (!activeCameraObject_) {
        return;
    }
    CameraEditor::GetInstance()->StopSceneObjectCamera(CameraManager::GetInstance()->GetMainCamera());
    activeCameraObject_ = nullptr;
    activeCameraShotIndex_ = -1;
}
