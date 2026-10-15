#pragma once

#include "CinematicPlayer.h"
#include "CinematicSequence.h"
#include "IEditable.h"

#include <string>
#include <vector>

class DebugEditor;
namespace engine { class Object3d; }
class SceneManager;
class BaseScene;

// 再生中に直近で発火したイベント ID と対象。対象 Object は所有しません。
struct ActiveEvent {
    int id = 0;
    engine::Object3d* targetObject = nullptr;
};

// 複数の Object、Camera、VFX を一つの時間軸で編集・再生します。
class GhostDirector : public IEditable {
  public:
    /// <summary>シーンと編集 UI を接続し、再生状態を初期化します。</summary>
    /// <param name="editor">編集用オブジェクト。再生だけなら nullptr を指定できます。</param>
    void Initialize(SceneManager* sceneManager, DebugEditor* editor = nullptr);
    /// 演出の再生時刻と編集プレビューを進める。
    void Update(float deltaTime);
    /// 現在時刻の Body 姿勢を対象へ反映する。
    void ApplyBodyPose() { player_.ApplyBodyPose(); }
    /// 演出トラックの編集 UI を描画する。
    void DrawImGui() override;
    /// 再生位置と各トラックのキーをタイムライン上に描く。
    void DrawTimelinePanel();
    std::string GetName() override { return "演出タイムライン"; }
    /// 制作空間を閉じ、カメラと一時オブジェクトを元に戻す。
    void EndStudio();

    /// <summary>読み込み済みの演出を先頭から再生します。</summary>
    /// <param name="isLoop">末尾に達したら先頭へ戻すか</param>
    void PlayScenario(bool isLoop = false);
    /// 演出の再生を止め、必要な姿勢を復元する。
    void StopScenario();
    /// 選択トラックの演出ガイドを Game View に描画する。
    void DrawPreview(const engine::Matrix4x4& viewProjection, const engine::Vector2& offset, const engine::Vector2& size);

    // 指定名の演出 JSON を保存・読み込みます。
    void SaveScenario(const std::string& fileName);
    void LoadScenario(const std::string& fileName);
    /// 読み込み済みシナリオの再生が終わったか返す。
    bool IsFinished() const;
    /// 直近に発火したイベントの ID を返す。
    int GetActiveEventID() const;
    /// 直近に発火したイベントと非所有の対象参照を返す。
    ActiveEvent GetActiveEvent() const;
    /// 再生時刻だけを指定秒数進める。
    void AdvanceTime(float deltaTime);

    // 現在時刻の位置・回転・拡縮をトラックへ記録します。
    void RecordTransformKey(engine::Object3d* object, bool force = false);
    bool IsAutoKeyEnabled() const { return autoKeyEnabled_; }
    float GetCurrentTime() const { return currentScrubTime_; }
#ifdef USE_IMGUI
    bool* GetTrackPreviewPtr() { return &showSelectedTrackPreview_; }
    bool* GetOrientationPreviewPtr() { return &showPreviewOrientation_; }
#endif

  private:
    static constexpr float kKeyTimeEpsilon = 1.0f / 120.0f;

    // 対象 Object とトラックの対応を調べ、必要な場合だけ新規トラックを作ります。
    int FindTransformTrack(engine::Object3d* object) const;
    int AddTransformTrack(engine::Object3d* object);
    /// Editor で選択中の Object を変形トラックへ追加する。
    void AddSelectedObjectsAsTracks();
    /// 選択中の演出トラックを削除する。
    void RemoveSelectedTrack();
    // 編集内容をプレイヤーへ反映し、プレビュー時刻を維持するか選びます。
    void RefreshPlayer(bool preservePreview);
    /// 現在のスクラブ時刻で演出を評価する。
    void EvaluatePreviewAtCurrentTime();
    /// 選択したトラックの対象 Object を Editor でも選択する。
    void SelectTrackTarget(int trackIndex);
    /// 保存済みの対象識別子から現在シーンの Object を探す。
    engine::Object3d* ResolveTrackTarget(const CinematicObjectBinding& binding) const;
    /// 同じ時刻のキーを更新し、なければ追加する。
    void UpsertTransformKey(CinematicTransformTrack& track, const CinematicTransformKey& key);
    /// 指定トラックの最後のキー時刻を返す。
    float GetTransformTrackDuration(int trackIndex) const;
    /// シナリオ全体の再生尺を返す。
    float GetScenarioDuration() const;

    // 時間軸 UI を操作・トラック編集・キー描画の単位に分けます。
    /// 再生、停止、ループ操作を描画する。
    void DrawTransportControls();
    /// 選択中トラックとキーの編集欄を描画する。
    void DrawInspectorContents();
    /// 制作空間を開始・終了する操作を描画する。
    void DrawStudioControls();
    /// 演出編集用の専用空間を開けたか返す。
    bool BeginStudio();
    /// 制作空間に演出確認用の Object を配置する。
    void CreateStudioObjects();
    /// 制作空間に合わせてカメラを移す。
    void PlaceStudioCamera();
    /// 制作空間で指定役割の Object を探す。
    engine::Object3d* FindStudioObject(int role) const;
    /// Object 名から制作空間内の役割番号を求める。
    static int GetRoleIndex(const std::string& name);
    /// 演出制作の操作案内を描画する。
    void DrawAuthoringGuide();
    /// 演出の基準となる Actor と Camera の割当てを編集する。
    void DrawAnchorTemplateEditor();
    /// Object の位置・回転・拡縮トラックを編集する。
    void DrawTransformTrackEditor();
    /// VFX の時刻付き再生トラックを編集する。
    void DrawVFXTrackEditor();
    /// カメラショットの切替トラックを編集する。
    void DrawCameraShotEditor();
    /// ボディアニメーションの再生トラックを編集する。
    void DrawAnimationClipEditor();
    /// 音声クリップの再生トラックを編集する。
    void DrawAudioClipEditor();
    /// イベント通知トラックを編集する。
    void DrawSignalEditor();
    /// トラック、キー、現在時刻を編集するキャンバスを描画する。
    void DrawTimelineCanvas();
    /// 演出対象の参照先を選ぶ UI を描画し、変更があれば true を返す。
    bool DrawObjectBindingEditor(const char* label, CinematicObjectBinding& binding, bool cameraOnly, bool allowWorld);

    // Scene と Editor は非所有参照。studioScene_ も SceneManager 側が所有します。
    SceneManager* sceneManager_ = nullptr;
    BaseScene* studioScene_ = nullptr;
    bool studioEnabled_ = false;
    bool studioCameraSaved_ = false;
    engine::Vector3 studioStoredEye_ = {}, studioStoredTarget_ = {}, studioStoredRotation_ = {};
    float studioStoredFov_ = 0.45f, studioStoredNear_ = 0.1f, studioStoredFar_ = 1000.0f;
    int studioStoredMode_ = 0, studioStoredFollowMode_ = 0;
    engine::Object3d* studioStoredFollow_ = nullptr;
    DebugEditor* editor_ = nullptr;
    // 編集中の演出データとその再生インスタンスです。
    CinematicSequence sequence_;
    CinematicObjectBinding templateActor_;
    CinematicObjectBinding templateCamera_;
    float templateDuration_ = 2.7f;
    float templateDistance_ = 5.0f;
    float templateHeight_ = 2.0f;
    CinematicPlayer player_;
    char scenarioNameBuf_[64] = "boss_attack_1";

    // プレビュー時刻とタイムラインの UI 状態は、演出データには保存しません。
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
