#pragma once

#include "CinematicPlayer.h"
#include "CinematicSequence.h"
#include "IEditable.h"

#include <string>
#include <vector>

class DebugEditor;
class Object3d;
class SceneManager;

struct ActiveEvent {
    int id = 0;
    Object3d* targetObject = nullptr;
};

// 複数Object、Camera、VFXを1本のマスター時間で編集・再生します。
class GhostDirector : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor = nullptr);
    void Update(float deltaTime);
    void DrawImGui() override;
    std::string GetName() override { return "Cinematic Director (Multi Object)"; }

    void PlayScenario(bool isLoop = false, bool useImguiTime = false);
    void StopScenario();
    void DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size);

    void SaveScenario(const std::string& fileName);
    void LoadScenario(const std::string& fileName);
    bool IsFinished() const;
    int GetActiveEventID() const;
    ActiveEvent GetActiveEvent() const;
    void AdvanceTime(float deltaTime);

    void RecordTransformKey(Object3d* object, bool force = false);
    bool IsAutoKeyEnabled() const { return autoKeyEnabled_; }
    float GetCurrentTime() const { return currentScrubTime_; }

private:
    static constexpr float kKeyTimeEpsilon = 1.0f / 120.0f;

    int FindTransformTrack(Object3d* object) const;
    int AddTransformTrack(Object3d* object);
    void AddSelectedObjectsAsTracks();
    void RemoveSelectedTrack();
    void RefreshPlayer(bool preservePreview);
    void EvaluatePreviewAtCurrentTime();
    void SelectTrackTarget(int trackIndex);
    Object3d* ResolveTrackTarget(const CinematicObjectBinding& binding) const;
    void UpsertTransformKey(CinematicTransformTrack& track, const CinematicTransformKey& key);
    float GetTransformTrackDuration(int trackIndex) const;
    float GetScenarioDuration() const;

    void DrawTransportControls();
    void DrawTransformTrackEditor();
    void DrawVFXTrackEditor();
    void DrawCameraShotEditor();
    void DrawAnimationClipEditor();
    void DrawAudioClipEditor();
    void DrawSignalEditor();
    void DrawTimelineWindow();
    void DrawTimelineCanvas();
    bool DrawObjectBindingEditor(
        const char* label,
        CinematicObjectBinding& binding,
        bool cameraOnly,
        bool allowWorld);

    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    CinematicSequence sequence_;
    CinematicPlayer player_;
    char scenarioNameBuf_[64] = "boss_attack_1";

    float currentScrubTime_ = 0.0f;
    float timelinePixelsPerSecond_ = 100.0f;
    bool isLooping_ = false;
    bool autoKeyEnabled_ = true;
    bool showSelectedTrackPreview_ = true;
    bool showPreviewOrientation_ = true;
    bool timelineWindowOpen_ = true;
    int selectedTrackKind_ = 0;
    int selectedTrackIndex_ = -1;
    int selectedKeyIndex_ = -1;
};
