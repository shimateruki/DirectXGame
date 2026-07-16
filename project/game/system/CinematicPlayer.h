#pragma once

#include "CinematicSequence.h"
#include "VFXSequencer.h"

#include <vector>

class BaseScene;
class Object3d;
class SceneManager;

// 1本のマスター時刻から全トラックを評価するムービー再生器です。
class CinematicPlayer {
public:
    void Initialize(SceneManager* sceneManager);
    void SetSequence(CinematicSequence* sequence);
    void RefreshBindings();

    void Play(bool loop);
    void Resume(bool loop);
    void Pause();
    void Stop(bool restorePose = true);
    void Update(float deltaTime);
    void SetTime(float timeSeconds, bool dispatchEvents = false);

    void BeginPreview();
    void EndPreview(bool restorePose = true);

    bool IsPlaying() const { return isPlaying_; }
    bool IsPreviewing() const { return isPreviewing_; }
    bool IsFinished() const { return !isPlaying_; }
    float GetCurrentTime() const { return currentTime_; }
    float GetDuration() const;
    bool GetBasePose(Object3d* object, Vector3& position, Vector3& rotation, Vector3& scale) const;
    int GetLastEventId() const { return lastEventId_; }
    Object3d* GetLastEventTarget() const { return lastEventTarget_; }

private:
    struct TransformRuntime {
        Object3d* target = nullptr;
        Vector3 basePosition = { 0.0f, 0.0f, 0.0f };
        Vector3 baseRotation = { 0.0f, 0.0f, 0.0f };
        Vector3 baseScale = { 1.0f, 1.0f, 1.0f };
        bool captured = false;
    };

    struct VFXRuntime {
        Object3d* target = nullptr;
        VFXSequencer sequencer;
        bool started = false;
    };

    Object3d* ResolveTarget(const CinematicObjectBinding& binding) const;
    float GetTransformTrackDuration(size_t index) const;
    void CaptureBasePoses();
    void RestoreBasePoses();
    void ResetRuntimeEvents();
    void Evaluate(float timeSeconds, float previousTimeSeconds, bool dispatchEvents);
    void EvaluateTransformTrack(size_t index, float timeSeconds, float previousTimeSeconds, bool dispatchEvents);
    void UpdateVFXTracks(float timeSeconds, float previousTimeSeconds);
    void UpdateCameraTrack(float timeSeconds);
    void StopCameraTrack();

    SceneManager* sceneManager_ = nullptr;
    BaseScene* boundScene_ = nullptr;
    CinematicSequence* sequence_ = nullptr;
    std::vector<TransformRuntime> transformRuntime_;
    std::vector<VFXRuntime> vfxRuntime_;
    Object3d* activeCameraObject_ = nullptr;

    float currentTime_ = 0.0f;
    bool isPlaying_ = false;
    bool isPreviewing_ = false;
    bool isLooping_ = false;
    int lastEventId_ = 0;
    Object3d* lastEventTarget_ = nullptr;
};
