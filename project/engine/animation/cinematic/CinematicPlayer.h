#pragma once

#include "engine/animation/cinematic/CinematicSequence.h"
#include "engine/animation/BodyAnimationClip.h"
#include "VfxSequencer.h"
#include "Camera.h"

#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include <unordered_set>

class BaseScene;
namespace engine { class Object3D; }
class SceneManager;

namespace engine {

// 担当：一本の再生時刻からカットシーンの全トラックを評価する。
// 関連：CinematicSequence（定義）、Object3D / Camera（対象）、GhostDirector（編集）
// 主な入口：Update / IsPlaying / GetCurrentTime
class CinematicPlayer {
  public:
    // ゲームの音声ID表は起動時に一度だけ接続する。再生器からゲーム層へ依存しない。
    // 関連：CinematicPlayer（この型を使う機能の本体）
    struct AudioEntry { std::string id, displayName; };
    static void SetAudioServices(std::function<void(const std::string&, float, bool)> play,
                                 std::function<void(const std::string&)> stop,
                                 std::function<std::vector<AudioEntry>()> entries);
    static std::vector<AudioEntry> GetAudioEntries();
    ~CinematicPlayer();
    using AnimationCallback =
        std::function<void(engine::Object3D*, const CinematicAnimationClipData&, float localTime, bool isPreview)>;
    using SignalCallback = std::function<void(engine::Object3D*, const CinematicSignalMarker&, bool isPreview)>;

    // SceneManager を借用し、再生・プレビュー状態を初期化する。
    void Initialize(SceneManager* sceneManager);

    // 再生中の系列を切り替える際は姿勢を戻し、新しい系列の対象を解決する。
    // sequence の所有権は受け取らず、再生中は呼び出し側が保持する。
    void SetSequence(CinematicSequence* sequence);

    // 制作空間では実シーンの対象ではなく、役割に対応する仮Objectへ解決する。
    void SetBindingResolver(std::function<engine::Object3D*(const CinematicObjectBinding&)> resolver) {
        bindingResolver_ = std::move(resolver);
        bindingsReady_ = false;
    }

    // Objectを持たない演出地点も、VFX開始時にワールド位置へ解決する。
    void SetPositionResolver(std::function<std::optional<engine::Vector3>(const CinematicObjectBinding&)> resolver) {
        positionResolver_ = std::move(resolver);
    }

    /// アニメーションクリップを適用する呼び出し側の処理を登録する。
    void SetAnimationCallback(AnimationCallback callback) { animationCallback_ = std::move(callback); }

    /// シグナル発火を受け取る呼び出し側の処理を登録する。
    void SetSignalCallback(SignalCallback callback) { signalCallback_ = std::move(callback); }

    // 現在のシーンから対象を再解決する。シーン切替後や対象の作り直し後に呼ぶ。
    void RefreshBindings();

    /// <summary>再生前に対象・キー・カメラの共通条件を調べ、問題点を返す。</summary>
    std::vector<std::string> ValidateBindings() const;

    /// <summary>JSONの対象指定を、現在のシーンのObjectへ解決する。</summary>
    engine::Object3D* ResolveBinding(const CinematicObjectBinding& binding) const { return ResolveTarget(binding); }

    // Directorの選択と本番再生で、ID優先・名前による対象探索を共有する。
    static engine::Object3D* ResolveSceneTarget(BaseScene* scene, const CinematicObjectBinding& binding);

    // 先頭から再生し、時刻0のイベントも一度評価する。
    void Play(bool loop);

    // プレビュー中なら現在時刻から再生し、それ以外は先頭から開始する。
    void Resume(bool loop);

    // 時刻と姿勢を保って再生を止め、音とVFXは停止する。
    void Pause();

    // restorePose が true なら再生開始前の姿勢に戻す。
    void Stop(bool restorePose = true);

    // 再生時間を秒で進め、通過したイベントを評価する。
    void Update(float deltaTime);

    // 時刻は秒で範囲内に丸める。通常のシークではイベントを再発火させない。
    void SetTime(float timeSeconds, bool dispatchEvents = false);

    // 現在姿勢を退避して編集用シークを開始する。再生中は開始しない。
    void BeginPreview();

    // プレビューを閉じる。restorePose=true なら退避した姿勢へ戻す。
    void EndPreview(bool restorePose = true);

    // 通常のキャラクター更新後、イベントを再発火せず身体姿勢だけを保持する。
    void ApplyBodyPose();

    bool IsPlaying() const { return isPlaying_; }

    bool IsPreviewing() const { return isPreviewing_; }

    bool IsFinished() const { return finished_; }

    // 演出素材が所有する部品だけを描画する。生成・並び順・寿命は再生器側で管理する。
    void DrawPresentation(ID3D12Resource* pointLight, ID3D12Resource* spotLight);

    float GetCurrentTime() const { return currentTime_; }

    /// 現在シーケンスの再生尺を秒で返す。
    float GetDuration() const;

    // 再生開始時に記録した姿勢を返す。対象が記録されていなければ false。
    bool GetBasePose(engine::Object3D* object, engine::Vector3& position, engine::Vector3& rotation, engine::Vector3& scale) const;

    // アンカーのワールド位置と Y 回転（ラジアン）を返す。親付き・未解決なら false。
    bool GetAnchorPose(engine::Vector3& position, float& yaw) const;

    int GetLastEventId() const { return lastEventId_; }

    engine::Object3D* GetLastEventTarget() const { return lastEventTarget_; }

  private:
    // 演出で所有・借用するObjectと、停止時に戻す姿勢・材質。
    // 関連：CinematicPlayer（この型を使う機能の本体）
    struct PresentationObject {
        std::unique_ptr<engine::Object3D> owned; // 演出で生成した場合だけ所有するObject。
        engine::Object3D* target = nullptr; // 操作対象への借用参照。
        engine::Object3D* source = nullptr; // 演出の基準となる元Objectへの借用参照。
        engine::Vector3 origin{}, rotation{}, scale{1, 1, 1}, pivot{}, radial{};
        CinematicVisualKey before;
        engine::Object3D::MaterialState material{};
    };
    // 演出専用Objectを用意し、既存Objectを使う部品は借用先を解決する。
    void PreparePresentation();
    // 再生直前の姿勢・材質・可視状態と、カメラ／帯の復元用状態を退避する。
    void CapturePresentation();
    // 一時Objectを破棄する。restore 時は生存中の借用対象とカメラ状態を戻す。
    void ReleasePresentation(bool restore);
    // 指定秒の見た目・視点を評価する。対象が失われた場合は再生を止めて復元する。
    void EvaluatePresentation(float time, float previous, bool dispatch);
    // 現在シーンと自分が所有する部品から、参照してよいObjectの集合を作る。
    std::unordered_set<engine::Object3D*> GetLiveTargets() const;
    // 対象不足や名前重複、視点キーとCamera Shotの競合を診断結果へ追記する。
    void AddPresentationIssues(std::vector<std::string>& issues) const;
    // 外部の位置解決を優先し、解決できない名前は nullopt を返す。
    std::optional<engine::Vector3> PresentationPosition(const std::string& name,
        const std::unordered_set<engine::Object3D*>& live) const;
    std::vector<PresentationObject> presentationObjects_;
    std::vector<std::pair<engine::Object3D*, bool>> presentationHidden_;
    std::unique_ptr<Camera> presentationCamera_;
    Camera* presentationPreviousCamera_ = nullptr; // 演出開始前の有効カメラ。借用参照。
    engine::Vector3 presentationOrigin_{}, presentationLookAt_{}, presentationScale_{1, 1, 1};
    float presentationYaw_ = 0.0f, presentationBars_ = 0.0f;
    bool presentationCaptured_ = false, finished_ = false, bindingsReady_ = false;

    static std::function<void(const std::string&, float, bool)> playAudio_;
    static std::function<void(const std::string&)> stopAudio_;
    static std::function<std::vector<AudioEntry>()> audioEntries_;
    // 演出開始前の Transform と対象の借用参照。相対トラックの基準と停止時の復元に使う。
    // 関連：CinematicPlayer（この型を使う機能の本体）
    struct TransformRuntime {
        engine::Object3D* target = nullptr; // 操作対象への借用参照。
        engine::Vector3 basePosition = engine::Vector3::ZERO;
        engine::Vector3 baseRotation = engine::Vector3::ZERO;
        engine::Vector3 baseScale = {1.0f, 1.0f, 1.0f};
        bool captured = false;
    };

    // トラックが借用する対象と、トラック自身が持つ Sequencer。started は再発火の制御に使う。
    // 関連：CinematicPlayer（この型を使う機能の本体）
    struct VfxRuntime {
        engine::Object3D* target = nullptr; // 操作対象への借用参照。
        VfxSequencer sequencer;
        bool started = false;
    };

    // 音声クリップの発火済み状態。音声バッファや再生 Voice をここで所有するわけではない。
    // 関連：CinematicPlayer（この型を使う機能の本体）
    struct AudioRuntime {
        bool started = false;
    };

    // 演出開始前の見た目と Animator 再生位置。restoreOnStop に応じた復元に使い、対象は借用する。
    // 関連：CinematicPlayer（この型を使う機能の本体）
    struct AnimationRuntime {
        engine::Object3D* target = nullptr; // 操作対象への借用参照。
        BodyAnimationClip bodyClip;
        BodyAnimationClip::Sample baseVisual;
        bool bodyCaptured = false;
        bool restoreOnStop = true; // 停止時に開始前の姿勢を戻すか。
        AnimatorControllerRuntime::Snapshot animatorSnapshot;
        std::string legacyAnimationName;
        float legacyAnimationTime = 0.0f;
        bool legacyLoop = true;
        bool captured = false;
    };

    /// バインディングから現在シーンの対象を解決する。所有権は移さない。
    engine::Object3D* ResolveTarget(const CinematicObjectBinding& binding) const;

    /// 指定変形トラックの終端時刻を返す。
    float GetTransformTrackDuration(size_t index) const;

    /// 再生前の各対象の姿勢を退避する。
    void CaptureBasePoses();

    /// 退避した各対象の姿勢を戻す。
    void RestoreBasePoses();

    /// イベントの発火済み状態を再生開始時に戻す。
    void ResetRuntimeEvents();

    /// 指定時刻の全トラックを評価し、必要なイベントを発火する。
    void Evaluate(float timeSeconds, float previousTimeSeconds, bool dispatchEvents);

    /// 指定した変形トラックの姿勢を時刻から評価する。
    void EvaluateTransformTrack(size_t index, float timeSeconds, float previousTimeSeconds, bool dispatchEvents);

    /// アニメーションクリップを評価する。bodyOnly は身体姿勢だけを適用する。
    void EvaluateAnimationClips(float timeSeconds, bool bodyOnly = false);

    /// 前回時刻から通過したシグナルを発火する。
    void DispatchSignals(float timeSeconds, float previousTimeSeconds);

    /// VFX トラックの開始と進行を現在時刻へ同期する。
    void UpdateVFXTracks(float timeSeconds, float previousTimeSeconds);

    /// 音声クリップの開始位置を現在時刻から判定する。
    void UpdateAudioClips(float timeSeconds, float previousTimeSeconds);

    /// このシーケンスで開始した音声を止める。
    void StopAudioClips();

    /// 現在のカメラショットを実カメラへ反映する。
    void UpdateCameraTrack(float timeSeconds);

    /// シーケンスのカメラ制御を終了する。
    void StopCameraTrack(bool switchingToNextShot = false);

    SceneManager* sceneManager_ = nullptr; // シーン取得元。借用参照。
    BaseScene* boundScene_ = nullptr; // 対象を解決したシーン。切替後は再解決する。
    CinematicSequence* sequence_ = nullptr; // 再生する定義への借用参照。
    std::vector<TransformRuntime> transformRuntime_;
    std::vector<VfxRuntime> vfxRuntime_;
    std::vector<AudioRuntime> audioRuntime_;
    std::vector<AnimationRuntime> animationRuntime_;
    engine::Object3D* activeCameraObject_ = nullptr;
    int activeCameraShotIndex_ = -1; // 現在のカメラカット。-1は適用なし。

    AnimationCallback animationCallback_;
    std::function<engine::Object3D*(const CinematicObjectBinding&)> bindingResolver_;
    std::function<std::optional<engine::Vector3>(const CinematicObjectBinding&)> positionResolver_;
    SignalCallback signalCallback_;

    float currentTime_ = 0.0f; // 共通再生時刻（秒）。
    bool isPlaying_ = false;
    bool isPreviewing_ = false;
    bool isLooping_ = false;
    bool anchorCaptured_ = false;
    engine::Vector3 anchorPosition_ = {};
    float anchorYaw_ = 0.0f; // 相対演出の基準ヨー角（ラジアン）。
    int lastEventId_ = 0;
    engine::Object3D* lastEventTarget_ = nullptr; // 直近のイベント対象。借用参照。
};

} // namespace engine

// 既存呼び出しとの互換名。新規コードは engine:: の正式名を使用する。
using engine::CinematicPlayer;
