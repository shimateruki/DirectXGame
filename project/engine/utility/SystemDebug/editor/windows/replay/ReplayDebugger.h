#pragma once

#ifdef USE_IMGUI

#include "Object3d.h"
#include "InputManager.h"
#include "SceneLoadContext.h"
#include "Sprite.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_set>
#include <vector>

class BaseScene;
class CaptureToolWindow;
class DebugEditor;
class SceneManager;

// 実行中シーンの短時間履歴を保持し、停止・巻き戻し・分岐再開を行う。
// SceneManagerとDebugEditorは借用し、記録はシーンの世代が変わると破棄する。
class ReplayDebugger {
public:
    enum class Mode {
        Idle,
        Recording,
        Paused,
        Playback,
        Regression,
    };

    /// <summary>シーンとエディターへの参照を借用し、保存済みリプレイ一覧を更新する。</summary>
    void Initialize(SceneManager* sceneManager, DebugEditor* debugEditor);
    /// <summary>保持オブジェクトを通常管理へ戻し、参照と履歴を解放する。</summary>
    void Finalize();

    /// <summary>ゲーム更新より前に、履歴再生とシーン変更を処理する。</summary>
    /// <param name="realDeltaTime">時間倍率を掛ける前の経過秒数。</param>
    /// <param name="isPlaying">現在Play中ならtrue。</param>
    void UpdateBeforeSimulation(float realDeltaTime, bool isPlaying);
    /// <summary>ゲーム更新後に入力を記録し、設定した間隔でシーンの状態を記録する。</summary>
    /// <param name="simulationDeltaTime">実際にゲーム更新へ渡した経過秒数。</param>
    /// <param name="isPlaying">現在Play中ならtrue。</param>
    void CaptureAfterSimulation(float simulationDeltaTime, bool isPlaying);
    void Draw(bool* open);

    /// <summary>有効なシーンの履歴を停止・再生表示中で、通常のゲーム更新を止めるべきか返す。</summary>
    bool ShouldFreezeSimulation() const;
    bool IsRecording() const { return mode_ == Mode::Recording; }
    bool HasFrames() const;
    Mode GetMode() const { return mode_; }
    // Main Menu Barから、実行を停止または選択時点から分岐再開します。
    void ToggleSimulationPause();

    /// <summary>Play開始直前の編集状態を通常の巻き戻し履歴とは別に保持する。</summary>
    /// <returns>シーンが有効で、基準フレームを取得できた場合にtrue。</returns>
    bool BeginPlayInEditorSnapshot();
    /// <summary>Play中に生成したObjectやSpriteを除き、同じSceneへ編集状態を復元する。</summary>
    /// <returns>保存した基準フレームを同じシーンへ適用できた場合にtrue。</returns>
    bool RestorePlayInEditorSnapshot();
    bool HasPlayInEditorSnapshot() const { return playInEditorSnapshot_.has_value(); }

    // 再生停止やシーン再読み込みの直前に、保持オブジェクトを通常管理へ戻します。
    void ResetForSceneChange();

    // 回帰テスト中は記録時のフレーム時間をGame更新へ渡す。
    /// <param name="defaultDeltaTime">通常更新で使う経過秒数。</param>
    float ResolveSimulationDeltaTime(float defaultDeltaTime) const;
    // 描画完了後に、回帰テストで予約されたGame View画像を保存します。
    void CapturePendingRegressionScreenshot(CaptureToolWindow* captureTool);

private:
    // Object を再特定する ID と記録時の状態。実行中生成物は生成情報も使って復元する。
    struct ObjectSnapshot {
        uint64_t replayId = 0;
        std::string persistentGuid;
        std::string name;
        std::string className;
        bool runtimeSpawned = false;
        std::string saveCategory;
        std::string enemyType;
        std::string gimmickType;
        std::string itemType;
        Object3d::ReplayState state;
    };

    // Sprite を再特定するキーと再生状態の値コピー。Sprite 本体の所有権は持たない。
    struct SpriteSnapshot {
        uint64_t replayId = 0;
        std::string bindingKey;
        std::string textureName;
        std::string name;
        Sprite::ReplayState state;
    };

    // 記録フレームの差分件数。再生状態そのものではなく、変更箇所を探すための統計。
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

    // 記録した視点・注視点と投影設定。valid が false のときは復元可能なカメラ情報がない。
    struct CameraSnapshot {
        bool valid = false;
        Vector3 eye = { 0.0f, 0.0f, 0.0f };
        Vector3 target = { 0.0f, 0.0f, 1.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
        float fovY = 0.45f;
        float nearClip = 0.1f;
        float farClip = 1000.0f;
    };

    // 記録時刻（秒）の Object・Sprite・Camera・Scene 状態。外部リソース全部の複製ではない。
    struct FrameSnapshot {
        double time = 0.0;
        std::vector<ObjectSnapshot> objects;
        std::vector<SpriteSnapshot> sprites;
        CameraSnapshot camera;
        json sceneState = json::object();
        FrameDiagnostics diagnostics;
        std::size_t estimatedBytes = 0;
    };


    // 再シミュレーションに渡す入力と更新幅。time と deltaTime は秒で、描画用姿勢記録とは別。
    struct InputSample {
        double time = 0.0;
        float deltaTime = 0.0f;
        InputManager::ReplayState state;
    };

    // 回帰比較の許容値。位置はワールド単位、性能予算はミリ秒で、性能超過の失敗扱いは任意。
    struct RegressionSettings {
        float maxPositionError = 1.0f;
        float worldPositionLimit = 10000.0f;
        float cpuBudgetMs = 33.33f;
        float gpuBudgetMs = 33.33f;
        bool failOnPerformanceBudget = false;
        int screenshotCount = 4;
    };

    // 回帰実行の進行・比較値・成果物。available と running を確認してから passed を解釈する。
    struct RegressionResult {
        bool available = false;
        bool passed = false;
        bool running = false;
        std::string archiveName;
        std::string reportPath;
        double durationSeconds = 0.0;
        std::size_t simulatedFrames = 0;
        std::size_t comparedFrames = 0;
        float maxPositionError = 0.0f;
        float averageCpuMs = 0.0f;
        float maximumCpuMs = 0.0f;
        float averageGpuMs = 0.0f;
        float maximumGpuMs = 0.0f;
        uint64_t newErrorLogs = 0;
        bool expectedGoal = false;
        bool observedGoal = false;
        std::vector<std::string> failures;
        std::vector<std::string> screenshotPaths;
    };
    // 保存済み記録の一覧表示用メタ情報。valid と error はアーカイブの読込診断。
    struct ReplayArchiveEntry {

        std::string fileName;
        std::string filePath;
        std::string createdAt;
        std::string sceneLabel;
        std::size_t frameCount = 0;
        std::size_t inputSampleCount = 0;
        bool regressionReady = false;
        double durationSeconds = 0.0;
        bool valid = false;
        std::string error;
    };

    // シーン準備完了まで保留する記録内容。フレームと入力サンプルを別々に保持する。
    struct PendingReplayArchive {
        std::string filePath;
        std::string createdAt;
        std::string sceneName;
        SceneLoadContext sceneContext;
        float captureRate = 15.0f;
        std::deque<FrameSnapshot> frames;
        std::vector<InputSample> inputSamples;
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
    std::size_t EstimateFrameBytes(const FrameSnapshot& frame) const;
    std::size_t GetMaxFrameCount() const;
    const char* GetModeLabel() const;

    bool SaveCurrentReplayArchive();
    void RefreshReplayArchiveList();
    bool RequestLoadReplayArchive(std::size_t archiveIndex);
    bool RequestPendingArchiveScene();
    bool TryCompletePendingArchiveLoad(bool isPlaying);
    bool IsPendingArchiveSceneCurrent() const;
    void RebindPendingArchiveToCurrentScene(BaseScene* scene);
    bool CanResumeLoadedArchive(std::string* reason = nullptr) const;
    json SerializeFrame(const FrameSnapshot& frame, double firstFrameTime) const;
    bool DeserializeFrame(const json& data, FrameSnapshot& frame, std::string& error) const;

    json SerializeInputState(const InputManager::ReplayState& state) const;
    bool DeserializeInputState(const json& data, InputManager::ReplayState& state, std::string& error) const;
    json SerializeInputSample(const InputSample& sample, double firstFrameTime) const;
    bool DeserializeInputSample(const json& data, InputSample& sample, std::string& error) const;

    bool RequestStartRegression(std::size_t archiveIndex);
    bool StartRegression();
    void UpdateRegressionBeforeSimulation();
    void CaptureRegressionAfterSimulation();
    void CompareRegressionFrame(const FrameSnapshot& expected);
    void FinishRegression();
    void FinalizeRegressionReport();
    void QueueRegressionScreenshots();
    const ObjectSnapshot* FindPlayerSnapshot(const FrameSnapshot& frame) const;
    static bool IsGoalReached(const json& sceneState);
    // ReplayDebugger.Ui.cpp
    void DrawToolbar();
    void DrawSummaryCards();
    void DrawTimelineEditor();
    void DrawObjectBrowser();
    void DrawObjectInspector();
    void DrawSpriteBrowser();
    void DrawSpriteInspector();
    void DrawSettingsPanel();
    void DrawArchivePanel();
    void HandleEditorShortcuts();
    void SelectFrameFromTimeline(double time);
    void DrawRegressionPanel();
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
    std::optional<FrameSnapshot> playInEditorSnapshot_;
    std::unordered_set<std::string> playBaselinePersistentGuids_;
    std::vector<InputSample> inputSamples_;
    std::size_t cursor_ = 0;
    Mode mode_ = Mode::Idle;

    bool autoRecord_ = true;
    bool wasPlaying_ = false;
    // 1秒あたりの履歴フレーム数。入力サンプルはゲーム更新ごとに別途保存する。
    float captureRate_ = 15.0f;
    // メモリ上で保持する履歴の長さ。単位は秒。
    float historySeconds_ = 20.0f;
    float playbackSpeed_ = 1.0f;
    float captureAccumulator_ = 0.0f;
    float playbackTime_ = 0.0f;
    double timelineTime_ = 0.0;
    std::size_t lastMissingObjectCount_ = 0;
    std::size_t lastMissingSpriteCount_ = 0;
    std::size_t estimatedMemoryBytes_ = 0;
    std::size_t recreatedArchiveObjectCount_ = 0;
    bool loadedArchiveReadOnly_ = false;
    std::string loadedArchiveName_;
    std::optional<PendingReplayArchive> pendingReplayArchive_;
    std::vector<ReplayArchiveEntry> replayArchiveEntries_;
    int selectedArchiveIndex_ = -1;

    uint64_t selectedReplayId_ = 0;

    uint64_t selectedSpriteReplayId_ = 0;
    bool inspectSprites_ = false;
    float timelinePixelsPerSecond_ = 110.0f;
    bool autoScrollTimeline_ = true;
    bool showOnlyChangedObjects_ = false;
    bool showRemovedObjects_ = true;
    char objectFilter_[128] = {};
    std::string statusMessage_;

    RegressionSettings regressionSettings_;
    RegressionResult regressionResult_;
    bool pendingRegressionStart_ = false;
    bool regressionStepPrepared_ = false;
    bool regressionReportPending_ = false;
    std::size_t regressionInputIndex_ = 0;
    std::size_t regressionExpectedFrameIndex_ = 0;
    std::size_t regressionNextScreenshotIndex_ = 0;
    float regressionStepDeltaTime_ = 0.0f;
    double regressionElapsedTime_ = 0.0;
    double regressionCpuTotalMs_ = 0.0;
    double regressionGpuTotalMs_ = 0.0;
    std::size_t regressionPerformanceSamples_ = 0;
    uint64_t regressionErrorBaseline_ = 0;
    std::string regressionOutputDirectory_;
    std::vector<double> regressionScreenshotTimes_;
    std::deque<std::string> pendingRegressionScreenshotPaths_;
};

#endif
