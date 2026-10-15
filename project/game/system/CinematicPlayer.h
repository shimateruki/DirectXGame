#pragma once

#include "CinematicSequence.h"
#include "engine/animation/BodyAnimationClip.h"
#include "VFXSequencer.h"

#include <functional>
#include <utility>
#include <vector>

class BaseScene;
namespace engine { class Object3d; }
class SceneManager;

// 1本のマスター時刻から全トラックを評価するムービー再生器です。
class CinematicPlayer {
  public:
    using AnimationCallback =
        std::function<void(engine::Object3d*, const CinematicAnimationClipData&, float localTime, bool isPreview)>;
    using SignalCallback = std::function<void(engine::Object3d*, const CinematicSignalMarker&, bool isPreview)>;

    // SceneManager を借用し、再生・プレビュー状態を初期化する。
    void Initialize(SceneManager* sceneManager);
    // 再生中の系列を切り替える際は姿勢を戻し、新しい系列の対象を解決する。
    // sequence の所有権は受け取らず、再生中は呼び出し側が保持する。
    void SetSequence(CinematicSequence* sequence);
    // 制作空間では実シーンの対象ではなく、役割に対応する仮Objectへ解決する。
    void SetBindingResolver(std::function<engine::Object3d*(const CinematicObjectBinding&)> resolver) {
        bindingResolver_ = std::move(resolver);
    }
    /// アニメーションクリップを適用する呼び出し側の処理を登録する。
    void SetAnimationCallback(AnimationCallback callback) { animationCallback_ = std::move(callback); }
    /// シグナル発火を受け取る呼び出し側の処理を登録する。
    void SetSignalCallback(SignalCallback callback) { signalCallback_ = std::move(callback); }
    // 現在のシーンから対象を再解決する。シーン切替後や対象の作り直し後に呼ぶ。
    void RefreshBindings();

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
    bool IsFinished() const { return !isPlaying_; }
    float GetCurrentTime() const { return currentTime_; }
    /// 現在シーケンスの再生尺を秒で返す。
    float GetDuration() const;
    // 再生開始時に記録した姿勢を返す。対象が記録されていなければ false。
    bool GetBasePose(engine::Object3d* object, engine::Vector3& position, engine::Vector3& rotation, engine::Vector3& scale) const;
    // アンカーのワールド位置と Y 回転（ラジアン）を返す。親付き・未解決なら false。
    bool GetAnchorPose(engine::Vector3& position, float& yaw) const;
    int GetLastEventId() const { return lastEventId_; }
    engine::Object3d* GetLastEventTarget() const { return lastEventTarget_; }

  private:
    // 演出開始前の Transform と対象の借用参照。相対トラックの基準と停止時の復元に使う。
    struct TransformRuntime {
        engine::Object3d* target = nullptr;
        engine::Vector3 basePosition = {0.0f, 0.0f, 0.0f};
        engine::Vector3 baseRotation = {0.0f, 0.0f, 0.0f};
        engine::Vector3 baseScale = {1.0f, 1.0f, 1.0f};
        bool captured = false;
    };

    // トラックが借用する対象と、トラック自身が持つ Sequencer。started は再発火の制御に使う。
    struct VFXRuntime {
        engine::Object3d* target = nullptr;
        VFXSequencer sequencer;
        bool started = false;
    };

    // 音声クリップの発火済み状態。音声バッファや再生 Voice をここで所有するわけではない。
    struct AudioRuntime {
        bool started = false;
    };

    // 演出開始前の見た目と Animator 再生位置。restoreOnStop に応じた復元に使い、対象は借用する。
    struct AnimationRuntime {
        engine::Object3d* target = nullptr;
        BodyAnimationClip bodyClip;
        BodyAnimationClip::Sample baseVisual;
        bool bodyCaptured = false;
        bool restoreOnStop = true;
        AnimatorControllerRuntime::Snapshot animatorSnapshot;
        std::string legacyAnimationName;
        float legacyAnimationTime = 0.0f;
        bool legacyLoop = true;
        bool captured = false;
    };

    /// バインディングから現在シーンの対象を解決する。所有権は移さない。
    engine::Object3d* ResolveTarget(const CinematicObjectBinding& binding) const;
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
    void StopCameraTrack();

    SceneManager* sceneManager_ = nullptr;
    BaseScene* boundScene_ = nullptr;
    CinematicSequence* sequence_ = nullptr;
    std::vector<TransformRuntime> transformRuntime_;
    std::vector<VFXRuntime> vfxRuntime_;
    std::vector<AudioRuntime> audioRuntime_;
    std::vector<AnimationRuntime> animationRuntime_;
    engine::Object3d* activeCameraObject_ = nullptr;
    int activeCameraShotIndex_ = -1;

    AnimationCallback animationCallback_;
    std::function<engine::Object3d*(const CinematicObjectBinding&)> bindingResolver_;
    SignalCallback signalCallback_;

    float currentTime_ = 0.0f;
    bool isPlaying_ = false;
    bool isPreviewing_ = false;
    bool isLooping_ = false;
    bool anchorCaptured_ = false;
    engine::Vector3 anchorPosition_ = {};
    float anchorYaw_ = 0.0f;
    int lastEventId_ = 0;
    engine::Object3d* lastEventTarget_ = nullptr;
};
