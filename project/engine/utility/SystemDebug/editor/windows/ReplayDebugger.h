#pragma once

#ifdef USE_IMGUI

#include "Object3d.h"
#include "Sprite.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

class BaseScene;
class DebugEditor;
class SceneManager;

// ReplayDebuggerは、実行中シーンの短時間履歴を保持して停止・巻き戻し・分岐再開を行います。
class ReplayDebugger {
public:
    enum class Mode {
        Idle,
        Recording,
        Paused,
        Playback,
    };

    void Initialize(SceneManager* sceneManager, DebugEditor* debugEditor);
    void Finalize();

    // ゲーム更新より前に、履歴再生とシーン変更検知を行います。
    void UpdateBeforeSimulation(float realDeltaTime, bool isPlaying);
    // ゲーム更新が完了した後に、現在状態を一定間隔で記録します。
    void CaptureAfterSimulation(float simulationDeltaTime, bool isPlaying);
    void Draw(bool* open);

    bool ShouldFreezeSimulation() const;
    bool IsRecording() const { return mode_ == Mode::Recording; }
    bool HasFrames() const;
    Mode GetMode() const { return mode_; }
    // Main Menu Barから、実行を停止または選択時点から分岐再開します。
    void ToggleSimulationPause();

    // 再生停止やシーン再読み込みの直前に、保持オブジェクトを通常管理へ戻します。
    void ResetForSceneChange();

private:
    struct ObjectSnapshot {
        uint64_t replayId = 0;
        std::string name;
        std::string className;
        Object3d::ReplayState state;
    };

    struct SpriteSnapshot {
        uint64_t replayId = 0;
        std::string name;
        Sprite::ReplayState state;
    };

    struct FrameDiagnostics {
        std::size_t activeObjects = 0;
        std::size_t changedObjects = 0;
        std::size_t spawnedObjects = 0;
        std::size_t removedObjects = 0;
        std::size_t hpChanges = 0;
        std::size_t deaths = 0;
        std::size_t activeSprites = 0;
        std::size_t changedSprites = 0;
    };

    struct CameraSnapshot {
        bool valid = false;
        Vector3 eye = { 0.0f, 0.0f, 0.0f };
        Vector3 target = { 0.0f, 0.0f, 1.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
        float fovY = 0.45f;
        float nearClip = 0.1f;
        float farClip = 1000.0f;
    };

    struct FrameSnapshot {
        double time = 0.0;
        std::vector<ObjectSnapshot> objects;
        std::vector<SpriteSnapshot> sprites;
        CameraSnapshot camera;
        json sceneState = json::object();
        FrameDiagnostics diagnostics;
        std::size_t estimatedBytes = 0;
    };

    void BeginRecording(BaseScene* scene);
    void CaptureFrame(double time);
    void ApplyFrame(std::size_t index);
    void PauseAt(std::size_t index);
    void StartPlayback();
    void ResumeFromCursor();
    void StepCursor(int direction);
    void ClearHistory(bool continueRecording);
    BaseScene* GetValidatedActiveScene(bool allowTransition = false) const;
    void ReleaseRetainedObjects();
    void ClearTransientRuntime();
    void TrimToCapacity();
    void BuildFrameDiagnostics(FrameSnapshot& frame) const;
    void RecalculateMemoryEstimate();
    std::size_t GetMaxFrameCount() const;
    const char* GetModeLabel() const;

    // ReplayDebugger.Ui.cpp
    void DrawToolbar();
    void DrawSummaryCards();
    void DrawTimelineEditor();
    void DrawObjectBrowser();
    void DrawObjectInspector();
    void DrawSpriteBrowser();
    void DrawSpriteInspector();
    void DrawSettingsPanel();
    void HandleEditorShortcuts();
    void SelectFrameFromTimeline(double time);
    void SelectSceneObject(uint64_t replayId);
    void JumpToObjectChange(int direction);
    bool HasMeaningfulChange(const ObjectSnapshot& lhs, const ObjectSnapshot& rhs) const;
    bool HasMeaningfulChange(const SpriteSnapshot& lhs, const SpriteSnapshot& rhs) const;
    const ObjectSnapshot* FindObjectSnapshot(const FrameSnapshot& frame, uint64_t replayId) const;
    const SpriteSnapshot* FindSpriteSnapshot(const FrameSnapshot& frame, uint64_t replayId) const;
    const ObjectSnapshot* FindPreviousObjectSnapshot(uint64_t replayId) const;
    const ObjectSnapshot* FindLatestObjectSnapshot(uint64_t replayId) const;
    static const char* FormatBytes(std::size_t bytes, char* buffer, std::size_t bufferSize);

private:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* debugEditor_ = nullptr;
    BaseScene* activeScene_ = nullptr;
    uint64_t activeSceneGeneration_ = 0;
    std::deque<FrameSnapshot> frames_;
    std::size_t cursor_ = 0;
    Mode mode_ = Mode::Idle;

    bool autoRecord_ = true;
    bool wasPlaying_ = false;
    float captureRate_ = 15.0f;
    float historySeconds_ = 20.0f;
    float playbackSpeed_ = 1.0f;
    float captureAccumulator_ = 0.0f;
    float playbackTime_ = 0.0f;
    double timelineTime_ = 0.0;
    std::size_t lastMissingObjectCount_ = 0;
    std::size_t lastMissingSpriteCount_ = 0;
    std::size_t estimatedMemoryBytes_ = 0;

    uint64_t selectedReplayId_ = 0;
    uint64_t selectedSpriteReplayId_ = 0;
    bool inspectSprites_ = false;
    float timelinePixelsPerSecond_ = 110.0f;
    bool autoScrollTimeline_ = true;
    bool showOnlyChangedObjects_ = false;
    bool showRemovedObjects_ = true;
    char objectFilter_[128] = {};
    std::string statusMessage_;
};

#endif
