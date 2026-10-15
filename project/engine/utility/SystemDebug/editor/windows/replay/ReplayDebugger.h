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
    /// <summary>Replay Debuggerのウィンドウを描画する。</summary>
    void Draw(bool* open);

    /// <summary>有効なシーンの履歴を停止・再生表示中で、通常のゲーム更新を止めるべきか返す。</summary>
    bool ShouldFreezeSimulation() const;
    /// <summary>記録モード中か返す。</summary>
    bool IsRecording() const { return mode_ == Mode::Recording; }
    /// <summary>再生できる履歴フレームがあるか返す。</summary>
    bool HasFrames() const;
    /// <summary>現在の記録・停止・再生モードを返す。</summary>
    Mode GetMode() const { return mode_; }
    /// <summary>実行を停止するか、選択時点から分岐して再開する。</summary>
    void ToggleSimulationPause();

    /// <summary>Play開始直前の編集状態を通常の巻き戻し履歴とは別に保持する。</summary>
    /// <returns>シーンが有効で、基準フレームを取得できた場合にtrue。</returns>
    bool BeginPlayInEditorSnapshot();
    /// <summary>Play中に生成したObjectやSpriteを除き、同じSceneへ編集状態を復元する。</summary>
    /// <returns>保存した基準フレームを同じシーンへ適用できた場合にtrue。</returns>
    bool RestorePlayInEditorSnapshot();
    /// <summary>Play開始前の編集状態を保持しているか返す。</summary>
    bool HasPlayInEditorSnapshot() const { return playInEditorSnapshot_.has_value(); }

    /// <summary>シーン変更前に保持オブジェクトを通常管理へ戻し、履歴を解除する。</summary>
    void ResetForSceneChange();

    /// <summary>回帰再生中は記録時のフレーム時間をゲーム更新へ渡す。</summary>
    /// <param name="defaultDeltaTime">通常更新で使う経過秒数。</param>
    float ResolveSimulationDeltaTime(float defaultDeltaTime) const;
    /// <summary>描画後に、回帰テストで予約されたGame View画像を保存する。</summary>
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
        engine::Object3d::ReplayState state;
    };

    // Sprite を再特定するキーと再生状態の値コピー。Sprite 本体の所有権は持たない。
    struct SpriteSnapshot {
        uint64_t replayId = 0;
        std::string bindingKey;
        std::string textureName;
        std::string name;
        engine::Sprite::ReplayState state;
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
        engine::Vector3 eye = { 0.0f, 0.0f, 0.0f };
        engine::Vector3 target = { 0.0f, 0.0f, 1.0f };
        engine::Vector3 rotation = { 0.0f, 0.0f, 0.0f };
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

    /// <summary>指定シーンを対象に入力と状態の記録を始める。</summary>
    void BeginRecording(BaseScene* scene);
    /// <summary>指定時刻のシーン・Object・Sprite・Camera状態を保存する。</summary>
    void CaptureFrame(double time);
    /// <summary>指定番号の記録フレームを現在シーンへ適用する。</summary>
    void ApplyFrame(std::size_t index);
    /// <summary>指定フレームを適用してシミュレーションを停止する。</summary>
    void PauseAt(std::size_t index);
    /// <summary>現在のカーソルから履歴再生を開始する。</summary>
    void StartPlayback();
    /// <summary>選択時点の状態からシミュレーションを再開する。</summary>
    void ResumeFromCursor();
    /// <summary>カーソルを指定方向へ一フレーム動かす。</summary>
    void StepCursor(int direction);
    /// <summary>履歴を破棄し、必要なら記録を続ける。</summary>
    void ClearHistory(bool continueRecording);
    /// <summary>世代と遷移状態を確認して有効な対象シーンを返す。</summary>
    BaseScene* GetValidatedActiveScene(bool allowTransition = false) const;
    /// <summary>Replay用に保持したオブジェクトを通常管理へ戻す。</summary>
    void ReleaseRetainedObjects();
    /// <summary>再生に使う一時的な実行状態を解除する。</summary>
    void ClearTransientRuntime();
    /// <summary>時間と件数の上限を超えた古い履歴を削除する。</summary>
    void TrimToCapacity();
    /// <summary>フレーム間の変化と生成・削除件数を集計する。</summary>
    void BuildFrameDiagnostics(FrameSnapshot& frame) const;
    /// <summary>保持中の履歴が使用する概算メモリ量を再計算する。</summary>
    void RecalculateMemoryEstimate();
    /// <summary>指定フレームの概算メモリ使用量を返す。</summary>
    std::size_t EstimateFrameBytes(const FrameSnapshot& frame) const;
    /// <summary>設定された履歴秒数と記録頻度から最大フレーム数を返す。</summary>
    std::size_t GetMaxFrameCount() const;
    /// <summary>現在のモードをUI表示用の文字列で返す。</summary>
    const char* GetModeLabel() const;

    /// <summary>現在の履歴を再利用できるReplayアーカイブへ保存する。</summary>
    /// <returns>保存に成功した場合はtrue。</returns>
    bool SaveCurrentReplayArchive();
    /// <summary>保存済みReplayアーカイブの一覧と診断情報を更新する。</summary>
    void RefreshReplayArchiveList();
    /// <summary>指定アーカイブの読み込みを予約する。</summary>
    bool RequestLoadReplayArchive(std::size_t archiveIndex);
    /// <summary>アーカイブに記録されたシーンへの遷移を要求する。</summary>
    bool RequestPendingArchiveScene();
    /// <summary>遷移先の準備ができたら保留中のアーカイブを読み込む。</summary>
    bool TryCompletePendingArchiveLoad(bool isPlaying);
    /// <summary>現在シーンが保留中アーカイブの対象シーンか返す。</summary>
    bool IsPendingArchiveSceneCurrent() const;
    /// <summary>保留中アーカイブのObject・Sprite参照を現在シーンへ結び直す。</summary>
    void RebindPendingArchiveToCurrentScene(BaseScene* scene);
    /// <summary>読み込んだアーカイブの選択時点から再開できるか返す。</summary>
    /// <param name="reason">再開できない場合の理由を受け取る任意の出力先。</param>
    bool CanResumeLoadedArchive(std::string* reason = nullptr) const;
    /// <summary>フレーム時刻を先頭からの相対値にしてJSONへ保存する。</summary>
    json SerializeFrame(const FrameSnapshot& frame, double firstFrameTime) const;
    /// <summary>JSONからフレームを復元し、不正データなら理由を返す。</summary>
    bool DeserializeFrame(const json& data, FrameSnapshot& frame, std::string& error) const;

    /// <summary>ゲーム入力のReplay状態をJSONへ変換する。</summary>
    json SerializeInputState(const InputManager::ReplayState& state) const;
    /// <summary>JSONからゲーム入力のReplay状態を復元する。</summary>
    bool DeserializeInputState(const json& data, InputManager::ReplayState& state, std::string& error) const;
    /// <summary>入力サンプルを先頭からの相対時刻でJSONへ保存する。</summary>
    json SerializeInputSample(const InputSample& sample, double firstFrameTime) const;
    /// <summary>JSONから時刻と入力を持つサンプルを復元する。</summary>
    bool DeserializeInputSample(const json& data, InputSample& sample, std::string& error) const;

    /// <summary>指定アーカイブによる回帰再生を予約する。</summary>
    bool RequestStartRegression(std::size_t archiveIndex);
    /// <summary>予約した回帰再生を開始し、比較結果を初期化する。</summary>
    bool StartRegression();
    /// <summary>次の記録入力をゲーム更新前に適用する。</summary>
    void UpdateRegressionBeforeSimulation();
    /// <summary>ゲーム更新後の実行状態と性能計測値を取得する。</summary>
    void CaptureRegressionAfterSimulation();
    /// <summary>実行結果を記録済みフレームと比較し、不一致を収集する。</summary>
    void CompareRegressionFrame(const FrameSnapshot& expected);
    /// <summary>回帰再生を止め、比較結果を確定する。</summary>
    void FinishRegression();
    /// <summary>回帰比較の診断と画像パスをレポートへ書き出す。</summary>
    void FinalizeRegressionReport();
    /// <summary>回帰再生中に撮るGame View画像の時刻を予約する。</summary>
    void QueueRegressionScreenshots();
    /// <summary>フレーム内のプレイヤー記録を探す。見つからなければnullptr。</summary>
    const ObjectSnapshot* FindPlayerSnapshot(const FrameSnapshot& frame) const;
    /// <summary>記録されたシーン状態がゴール到達を示すか返す。</summary>
    static bool IsGoalReached(const json& sceneState);
    // ReplayDebugger.Ui.cpp
    /// <summary>記録・停止・再生の操作列を描画する。</summary>
    void DrawToolbar();
    /// <summary>履歴件数やメモリ量などの概要カードを描画する。</summary>
    void DrawSummaryCards();
    /// <summary>記録時刻の選択と移動を行うタイムラインを描画する。</summary>
    void DrawTimelineEditor();
    /// <summary>記録済みObjectの一覧と検索UIを描画する。</summary>
    void DrawObjectBrowser();
    /// <summary>選択中Objectの記録状態を表示する。</summary>
    void DrawObjectInspector();
    /// <summary>記録済みSpriteの一覧を描画する。</summary>
    void DrawSpriteBrowser();
    /// <summary>選択中Spriteの記録状態を表示する。</summary>
    void DrawSpriteInspector();
    /// <summary>記録頻度、履歴長、再生速度などの設定を描画する。</summary>
    void DrawSettingsPanel();
    /// <summary>保存済みアーカイブの一覧と読み込み操作を描画する。</summary>
    void DrawArchivePanel();
    /// <summary>Replay画面のキーボード操作を処理する。</summary>
    void HandleEditorShortcuts();
    /// <summary>指定時刻に最も近い記録フレームへカーソルを移す。</summary>
    void SelectFrameFromTimeline(double time);
    /// <summary>回帰再生の設定、結果、失敗一覧を描画する。</summary>
    void DrawRegressionPanel();
    /// <summary>Replay IDに対応するシーンObjectを選択する。</summary>
    void SelectSceneObject(uint64_t replayId);
    /// <summary>現在の選択Objectが変化した前後のフレームへ移動する。</summary>
    void JumpToObjectChange(int direction);
    /// <summary>前後のObject記録に表示すべき差があるか返す。</summary>
    bool HasMeaningfulChange(const ObjectSnapshot& lhs, const ObjectSnapshot& rhs) const;
    /// <summary>前後のSprite記録に表示すべき差があるか返す。</summary>
    bool HasMeaningfulChange(const SpriteSnapshot& lhs, const SpriteSnapshot& rhs) const;
    /// <summary>指定Replay IDのObjectをフレームから探す。</summary>
    const ObjectSnapshot* FindObjectSnapshot(const FrameSnapshot& frame, uint64_t replayId) const;
    /// <summary>指定Replay IDのSpriteをフレームから探す。</summary>
    const SpriteSnapshot* FindSpriteSnapshot(const FrameSnapshot& frame, uint64_t replayId) const;
    /// <summary>選択フレームより前にある指定Objectの直近状態を探す。</summary>
    const ObjectSnapshot* FindPreviousObjectSnapshot(uint64_t replayId) const;
    /// <summary>履歴内で指定Objectが最後に記録された状態を探す。</summary>
    const ObjectSnapshot* FindLatestObjectSnapshot(uint64_t replayId) const;
    /// <summary>バイト数を読みやすい単位へ整形し、渡されたバッファーへ書く。</summary>
    /// <returns>整形後の文字列を指すポインター。</returns>
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
