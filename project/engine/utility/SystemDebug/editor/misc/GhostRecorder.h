#pragma once
#include "Object3d.h"
#include "engine/utility/math/Math.h"
#include "IEditable.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class SceneManager;

// ゴースト再生用の1サンプル分のTransformとイベントを保持する。再生時は60サンプルを1秒として扱う。
struct GhostFrame {
    engine::Vector3 position;
    engine::Vector3 rotation;
    engine::Vector3 scale = { 1.0f, 1.0f, 1.0f };
    int eventID = 0; // 0はイベントなし。非0なら通過時に対象のOnRecordEventへ渡す。
};


// プレイ中の動きやカメラ軌跡を記録し、エディタ上で再生確認する。
// 対象とSceneManagerのポインタは借用し、シーンから対象が消えたときは参照を解除する。
class GhostRecorder : public IEditable {
public:
    enum class State {
        Idle,       // 待機中
        Recording,  // 録画中
        Playing     // 再生中
    };

    // 始終点と中継点から記録フレームを生成する設定。相対生成の基準と区間補間方式も保持する。
    struct GenerationParams {
        engine::Vector3 startPos = { 0,0,0 };
        engine::Vector3 startRot = { 0,0,0 };
        engine::Vector3 startScale = { 1.0f, 1.0f, 1.0f };
        int startEventID = 0;
        float startWaitTime = 0.0f;
        float startDurationToNext = 1.0f;
        int startEasingToNext = 0;
        engine::Vector3 endPos = { 0,0,0 };
        engine::Vector3 endRot = { 0,0,0 };
        engine::Vector3 endScale = { 1.0f, 1.0f, 1.0f };
        int endEventID = 0;
        float endWaitTime = 0.0f;
        engine::Vector3 anchorOffsetPos = { 0.0f, 0.0f, 0.0f };
        engine::Vector3 anchorOffsetRot = { 0.0f, 0.0f, 0.0f };
        // 経路生成の中継点。waitTime はその点での待機秒、durationToNext は次点までの移動秒。
        struct Waypoint {
            engine::Vector3 pos;
            engine::Vector3 rot;
            engine::Vector3 scale = { 1.0f, 1.0f, 1.0f };
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
    /// <summary>記録内容と再生状態を初期化する。既存のメモリ上の記録は破棄される。</summary>
    void Initialize(SceneManager* sceneManager);
    // 記録・再生を進め、参照中の対象がシーンから消えた場合は解除する。
    void Update();
    // 記録、再生、軌跡編集の操作画面を描画する。
    void DrawImGui() override;
    std::string GetName() override { return "Ghost Recorder (Cinematic/Path)"; }

    // 記録済み経路を指定した画面領域へ重ねて描画する。読み取り専用では編集操作を受け付けない。
    void DrawPreview(const engine::Matrix4x4& viewProjection, const engine::Vector2& offset, const engine::Vector2& size, bool isReadOnly = false);
    // 軌跡上にObjectの姿勢を複製表示する。
    void DrawObjectGhostPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>記録・再生するシーン内Objectを指定する。所有権は移さない。</summary>
    void SetTarget(engine::Object3d* target);
    // 対象への借用参照を解除する。
    void ClearTarget();
    engine::Object3d* GetTarget() const { return target_; }
    void SetSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }
    // 軌跡プレビューへ渡せる記録があるかを返す。
    bool HasPreviewData() const;
    State GetState() const { return state_; }

    /// <summary>Resources/json/animation内の記録を読み込み、対象へ再生する。</summary>
    /// <param name="fileName">拡張子を除いたJSONファイル名。</param>
    /// <param name="loop">終端の次に先頭へ戻す場合はtrue。</param>
    /// <param name="isRelative">開始時の対象位置を基準に軌跡を適用する場合はtrue。</param>
    /// <param name="isCinematic">対象を非表示にし、演出カメラとして再生する場合はtrue。</param>
    void Play(const std::string& fileName, bool loop, bool isRelative, bool isCinematic);

    /// <summary>再生を止める。相対再生中にautoResetがtrueなら開始時の姿勢へ戻す。</summary>
    void Stop(bool autoReset = true);

    /// <summary>メモリ上の記録と生成パラメータをResources/json/animationへ保存する。</summary>
    /// <remarks>記録が空、またはファイルを開けない場合は保存しない。</remarks>
    void Save(const std::string& fileName);

    /// <summary>同じ場所のJSONを読み込む。ファイルがなければ現在の記録を維持する。</summary>
    void Load(const std::string& fileName);

    int GetTotalFrames() const { return static_cast<int>(frames_.size()); }
    /// <summary>60サンプルを1秒として、最初から最後のサンプルまでの秒数を返す。</summary>
    float GetDurationSeconds() const;

    // 指定サンプルの姿勢を対象に反映する。
    void EvaluateAtFrame(int frameIndex);
    /// <summary>指定時刻の姿勢を補間して適用する。時刻は記録範囲に丸める。</summary>
    /// <param name="dispatchEvents">trueならpreviousTimeSecondsからtimeSecondsまでに通過したイベントを通知する。</param>
    /// <param name="previousTimeSeconds">イベント検出区間の始点。逆方向の評価では通知しない。</param>
    void EvaluateAtTime(float timeSeconds, bool dispatchEvents = false, float previousTimeSeconds = 0.0f);

    // 対象または相対生成アンカーから再生の基準姿勢を取得する。
    void CaptureBasePose();
    // 保存した対象姿勢を復元する。
    void RestoreBasePose();
    // 経路編集の履歴を一段戻す／進める。
    void PerformUndo();
    void PerformRedo();
    // 現在選択しているピンのイベントIDを返す。
    int GetCurrentEventID() const;

    bool IsPinSelected() const { return selectedPinType_ != SelectedPinType::None; }
    // 選択中の経路ピンを削除する。
    void DeleteSelectedPin();
    // ピンの選択状態だけを解除する。
    void DeselectPin();
    // ファイルを読み直さず、現在の記録を再生する。
    void PlayFromMemory(bool loop, bool isCinematic);

private:
    // 対象の動きのサンプル採取を開始／終了する。
    void StartRecording();
    void StopRecording();
    // 読み込み済みフレームを再生状態へ移す。
    void StartPlayingInternal();
    // フレームのTransformを対象へ適用する。
    void ApplyFrameTransform(const GhostFrame& frame);
    // 演出カメラ用の対象表示と視点を切り替える。
    void StartCinematicPlayback();
    void StopCinematicPlayback();
    // 編集パラメータの履歴を記録し、Undo/Redo へ渡す。
    void SaveHistory();
    void CommitHistory(const std::string& label);
    void RegisterHistory(const GenerationParams& before, const GenerationParams& after, const std::string& label);

    // 経路の曲線補間とプレビュー用サンプル生成。
    engine::Vector3 CatmullRom(const engine::Vector3& p0, const engine::Vector3& p1, const engine::Vector3& p2, const engine::Vector3& p3, float t);
    engine::Vector3 GetSplinePoint(const std::vector<engine::Vector3>& points, float t);
    engine::Vector3 TransformCoord(const engine::Vector3& vec, const engine::Matrix4x4& mat);
    std::vector<GhostFrame> BuildPreviewSamples(int sampleCount);
    // 借用中の対象が現在のシーンに残っているか検査する。
    bool IsTargetInCurrentScene() const;
    void ClearTargetIfMissingFromScene();

private:
    SceneManager* sceneManager_ = nullptr;
    engine::Object3d* target_ = nullptr;

    std::vector<GhostFrame> frames_;
    State state_ = State::Idle;

    size_t currentFrameIndex_ = 0;
    bool isLoop_ = false;
    bool isRelative_ = true;
    bool wasEditorPaused_ = false;


    GenerationParams genParams_;
    bool isShowPreview_ = true;
    bool isShowObjectPreview_ = true;
    int objectPreviewSampleCount_ = 6;
    float objectPreviewAlpha_ = 0.24f;

    bool isOverrideCamera_ = false;
    engine::Vector3 basePosition_ = { 0, 0, 0 };
    engine::Vector3 baseRotation_ = { 0, 0, 0 };
    engine::Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    bool isScrubbing_ = false; // Scrubbing中かどうかのフラグ
    engine::Object3d* anchor_ = nullptr;
    std::string anchorName_ = "";
    // 相対生成の基準に使うアンカーObjectをシーンから探す。
    void FindAnchor();
    enum class SelectedPinType {
        None,
        Start,
        Waypoint,
        End
    };
    SelectedPinType selectedPinType_ = SelectedPinType::None;
    int selectedWaypointIndex_ = -1;
    std::optional<GenerationParams> pendingHistoryState_;
    GenerationParams uiEditStartState_;
    bool hasUiEditStart_ = false;
    bool historyRegisteredThisFrame_ = false;
    std::shared_ptr<int> historyLifetime_ = std::make_shared<int>(0);
    bool isDraggingGizmo_ = false;
};
