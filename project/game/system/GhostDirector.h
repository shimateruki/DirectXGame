#pragma once

#include "CinematicPlayer.h"
#include "CinematicSequence.h"
#include "IEditable.h"

#include <string>
#include <vector>

class DebugEditor;
class Object3d;
class SceneManager;
class BaseScene;

struct ActiveEvent {
    int id = 0;
    Object3d* targetObject = nullptr;
};

// 複数Object、Camera、VFXを1本のマスター時間で編集・再生します。
class GhostDirector : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor = nullptr);
    void Update(float deltaTime);
    void ApplyBodyPose() { player_.ApplyBodyPose(); }
    void DrawImGui() override;
    void DrawTimelinePanel();
    std::string GetName() override { return "演出タイムライン"; }
    void EndStudio();

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
#ifdef USE_IMGUI
    bool* GetTrackPreviewPtr() { return &showSelectedTrackPreview_; }
    bool* GetOrientationPreviewPtr() { return &showPreviewOrientation_; }
#endif

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
    void DrawInspectorContents();
    void DrawStudioControls();
    bool BeginStudio();
    void CreateStudioObjects();
    void PlaceStudioCamera();
    Object3d* FindStudioObject(int role) const;
    static int GetRoleIndex(const std::string& name);
    void DrawAuthoringGuide();
    void DrawAnchorTemplateEditor();
    void DrawTransformTrackEditor();
    void DrawVFXTrackEditor();
    void DrawCameraShotEditor();
    void DrawAnimationClipEditor();
    void DrawAudioClipEditor();
    void DrawSignalEditor();
    void DrawTimelineCanvas();
    bool DrawObjectBindingEditor(
        const char* label,
        CinematicObjectBinding& binding,
        bool cameraOnly,
        bool allowWorld);

    SceneManager* sceneManager_ = nullptr;
    BaseScene* studioScene_ = nullptr;
    bool studioEnabled_ = false;
    bool studioCameraSaved_ = false;
    Vector3 studioStoredEye_ = {}, studioStoredTarget_ = {}, studioStoredRotation_ = {};
    float studioStoredFov_ = 0.45f, studioStoredNear_ = 0.1f, studioStoredFar_ = 1000.0f;
    int studioStoredMode_ = 0, studioStoredFollowMode_ = 0;
    Object3d* studioStoredFollow_ = nullptr;
    DebugEditor* editor_ = nullptr;
    CinematicSequence sequence_;
    CinematicObjectBinding templateActor_;
    CinematicObjectBinding templateCamera_;
    float templateDuration_ = 2.7f;
    float templateDistance_ = 5.0f;
    float templateHeight_ = 2.0f;
    CinematicPlayer player_;
    char scenarioNameBuf_[64] = "boss_attack_1";

    float currentScrubTime_ = 0.0f;
    float timelinePixelsPerSecond_ = 100.0f;
    bool isLooping_ = false;
    bool autoKeyEnabled_ = false;
    bool showSelectedTrackPreview_ = true;
    bool showPreviewOrientation_ = true;
    std::string fileOperationStatus_;
    int selectedTrackKind_ = 0;
    int selectedTrackIndex_ = -1;
    int selectedKeyIndex_ = -1;
};
