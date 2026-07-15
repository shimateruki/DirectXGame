#pragma once
#include "Object3d.h"
#include "engine/utility/math/Math.h"
#include "IEditable.h"

#include <deque>
#include <string>
#include <vector>

class SceneManager;

// ゴースト再生用の1フレーム分のTransformとイベントを保持する。
struct GhostFrame {
    Vector3 position;
    Vector3 rotation;
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    int eventID = 0;
};


// GhostRecorderは、プレイ中の動きやカメラ軌跡を記録し、エディタ上で再生確認するためのツールです。
class GhostRecorder : public IEditable {
public:
    enum class State {
        Idle,       // 待機中
        Recording,  // 録画中
        Playing     // 再生中
    };

    struct GenerationParams {
        Vector3 startPos = { 0,0,0 };
        Vector3 startRot = { 0,0,0 };
        Vector3 startScale = { 1.0f, 1.0f, 1.0f };
        int startEventID = 0;
        float startWaitTime = 0.0f;
        float startDurationToNext = 1.0f;
        int startEasingToNext = 0;
        Vector3 endPos = { 0,0,0 };
        Vector3 endRot = { 0,0,0 };
        Vector3 endScale = { 1.0f, 1.0f, 1.0f };
        int endEventID = 0;
        float endWaitTime = 0.0f;
        Vector3 anchorOffsetPos = { 0.0f, 0.0f, 0.0f };
        Vector3 anchorOffsetRot = { 0.0f, 0.0f, 0.0f };
        struct Waypoint {
            Vector3 pos;
            Vector3 rot;
            Vector3 scale = { 1.0f, 1.0f, 1.0f };
            int eventID = 0;
            float waitTime = 0.0f;
            float durationToNext = 1.0f;
            int easingToNext = 0;
        };
        std::vector<Waypoint> waypoints;

        bool generateRelative = false;
        bool useSpline = true;
    };

public:
    void Initialize(SceneManager* sceneManager);
    void Update();
    void DrawImGui() override;
    std::string GetName() override { return "Ghost Recorder (Cinematic/Path)"; }

    void DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size, bool isReadOnly = false);
    void DrawObjectGhostPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    void SetTarget(Object3d* target);
    void ClearTarget();
    Object3d* GetTarget() const { return target_; }
    void SetSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }
    bool HasPreviewData() const;
    State GetState() const { return state_; }

    void Play(const std::string& fileName, bool loop, bool isRelative, bool isCinematic);
    void Stop(bool autoReset = true);

    void Save(const std::string& fileName);
    void Load(const std::string& fileName);

    int GetTotalFrames() const { return static_cast<int>(frames_.size()); }
    void EvaluateAtFrame(int frameIndex);
    void CaptureBasePose();
    void RestoreBasePose();
    void PerformUndo();
    void PerformRedo();
    int GetCurrentEventID() const;

    bool IsPinSelected() const { return selectedPinType_ != SelectedPinType::None; }
    void DeleteSelectedPin();
    void DeselectPin();
    void PlayFromMemory(bool loop, bool isCinematic);

private:
    void StartRecording();
    void StopRecording();
    void StartPlayingInternal();
    void ApplyFrameTransform(const GhostFrame& frame);
    void StartCinematicPlayback();
    void StopCinematicPlayback();
    void SaveHistory();

    Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t);
    Vector3 GetSplinePoint(const std::vector<Vector3>& points, float t);
    Vector3 TransformCoord(const Vector3& vec, const Matrix4x4& mat);
    std::vector<GhostFrame> BuildPreviewSamples(int sampleCount);
    bool IsTargetInCurrentScene() const;
    void ClearTargetIfMissingFromScene();

private:
    SceneManager* sceneManager_ = nullptr;
    Object3d* target_ = nullptr;

    std::vector<GhostFrame> frames_;
    State state_ = State::Idle;

    size_t currentFrameIndex_ = 0;
    bool isLoop_ = false;
    bool isRelative_ = true;


    GenerationParams genParams_;
    bool isShowPreview_ = true;
    bool isShowObjectPreview_ = true;
    int objectPreviewSampleCount_ = 6;
    float objectPreviewAlpha_ = 0.24f;

    bool isOverrideCamera_ = false;
    Vector3 basePosition_ = { 0, 0, 0 };
    Vector3 baseRotation_ = { 0, 0, 0 };
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    bool isScrubbing_ = false; // Scrubbing中かどうかのフラグ
    Object3d* anchor_ = nullptr;
    std::string anchorName_ = "";
    void FindAnchor();
    enum class SelectedPinType {
        None,
        Start,
        Waypoint,
        End
    };
    SelectedPinType selectedPinType_ = SelectedPinType::None;
    int selectedWaypointIndex_ = -1;
    std::deque<GenerationParams> undoStack_;
    std::deque<GenerationParams> redoStack_;
    bool isDraggingGizmo_ = false;
};
