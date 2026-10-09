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

// 再生中に直近で発火したイベント ID と対象。対象 Object は所有しません。
struct ActiveEvent {
    int id = 0;
    Object3d* targetObject = nullptr;
};

// 複数の Object、Camera、VFX を一つの時間軸で編集・再生します。
class GhostDirector : public IEditable {
  public:
    /// <summary>シーンと編集 UI を接続し、再生状態を初期化します。</summary>
    /// <param name="editor">編集用オブジェクト。再生だけなら nullptr を指定できます。</param>
    void Initialize(SceneManager* sceneManager, DebugEditor* editor = nullptr);
    void Update(float deltaTime);
    void ApplyBodyPose() { player_.ApplyBodyPose(); }
    void DrawImGui() override;
    void DrawTimelinePanel();
    std::string GetName() override { return "演出タイムライン"; }
    void EndStudio();

    /// <summary>読み込み済みの演出を先頭から再生します。</summary>
    /// <param name="isLoop">末尾に達したら先頭へ戻すか</param>
    void PlayScenario(bool isLoop = false);
    void StopScenario();
    void DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size);

    // 指定名の演出 JSON を保存・読み込みます。
    void SaveScenario(const std::string& fileName);
    void LoadScenario(const std::string& fileName);
    bool IsFinished() const;
    int GetActiveEventID() const;
    ActiveEvent GetActiveEvent() const;
    void AdvanceTime(float deltaTime);

    // 現在時刻の位置・回転・拡縮をトラックへ記録します。
    void RecordTransformKey(Object3d* object, bool force = false);
    bool IsAutoKeyEnabled() const { return autoKeyEnabled_; }
    float GetCurrentTime() const { return currentScrubTime_; }
#ifdef USE_IMGUI
    bool* GetTrackPreviewPtr() { return &showSelectedTrackPreview_; }
    bool* GetOrientationPreviewPtr() { return &showPreviewOrientation_; }
#endif

  private:
    static constexpr float kKeyTimeEpsilon = 1.0f / 120.0f;

    // 対象 Object とトラックの対応を調べ、必要な場合だけ新規トラックを作ります。
    int FindTransformTrack(Object3d* object) const;
    int AddTransformTrack(Object3d* object);
    void AddSelectedObjectsAsTracks();
    void RemoveSelectedTrack();
    // 編集内容をプレイヤーへ反映し、プレビュー時刻を維持するか選びます。
    void RefreshPlayer(bool preservePreview);
    void EvaluatePreviewAtCurrentTime();
    void SelectTrackTarget(int trackIndex);
    Object3d* ResolveTrackTarget(const CinematicObjectBinding& binding) const;
    void UpsertTransformKey(CinematicTransformTrack& track, const CinematicTransformKey& key);
    float GetTransformTrackDuration(int trackIndex) const;
    float GetScenarioDuration() const;

    // 時間軸 UI を操作・トラック編集・キー描画の単位に分けます。
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
    bool DrawObjectBindingEditor(const char* label, CinematicObjectBinding& binding, bool cameraOnly, bool allowWorld);

    // Scene と Editor は非所有参照。studioScene_ も SceneManager 側が所有します。
    SceneManager* sceneManager_ = nullptr;
    BaseScene* studioScene_ = nullptr;
    bool studioEnabled_ = false;
    bool studioCameraSaved_ = false;
    Vector3 studioStoredEye_ = {}, studioStoredTarget_ = {}, studioStoredRotation_ = {};
    float studioStoredFov_ = 0.45f, studioStoredNear_ = 0.1f, studioStoredFar_ = 1000.0f;
    int studioStoredMode_ = 0, studioStoredFollowMode_ = 0;
    Object3d* studioStoredFollow_ = nullptr;
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
