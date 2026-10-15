#pragma once

#include "IEditable.h"
#include "Model.h"
#include "engine/animation/BodyAnimationClip.h"
#include "engine/utility/math/Math.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class DirectXCommon;
namespace engine { class Object3d; }
class SceneManager;
class BaseScene;

/// <summary>
/// モデルのポーズキー、イベントマーカー、プレビュー再生を編集する作業用ウィンドウ。
/// </summary>
/// 単体ObjectのBody Transformと、モデルのボーン姿勢を編集・プレビューするアニメーション作業台。
class AnimationWorkbench : public IEditable {
public:
    /// シーンと描画基盤への非所有参照を設定する。
    void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);
    /// タイムライン再生とプレビュー姿勢を時間に合わせて更新する。
    void Update(float deltaTime);
    /// プレビューオブジェクトとカメラの一時状態を片付ける。
    void Finalize();
    /// アニメーション編集ウィンドウを描画する。
    void DrawImGui() override;
    /// キー、イベント、再生位置を操作するタイムラインを描画する。
    void DrawTimelinePanel();
    /// 現在のタイムライン位置からプレビュー再生を始める。
    void StartPreview();
    /// プレビュー再生を止める。
    void StopPreview();
    std::string GetName() override { return "Animation Workbench"; }

    /// 作業対象を見やすい位置にカメラを一時的に配置する。
    void ApplyCameraOverride();
    /// Game View の位置と大きさをギズモ投影に使用する。
    void SetGameViewRegion(const engine::Vector2& offset, const engine::Vector2& size);
    void SetGameViewMousePos(const engine::Vector2& pos) { gameViewMousePos_ = pos; }
    void SetGameViewHovered(bool hovered) { isGameViewHovered_ = hovered; }
    bool IsEnabled() const { return enabled_; }
#ifdef USE_IMGUI
    bool* GetBoneOverlayPtr() { return &showBoneOverlay_; }
    bool* GetBoneNamesPtr() { return &showBoneNames_; }
    bool* GetBoneAxesPtr() { return &showBoneAxes_; }
#endif

private:
    /// 指定時刻の1ジョイント姿勢を保持し、タイムライン補間の素材にする。
    struct PoseKey {
        float time = 0.0f;
        int jointIndex = -1;
        std::string jointName;
        engine::Vector3 translate = { 0.0f, 0.0f, 0.0f };
        engine::Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        engine::Vector3 scale = { 1.0f, 1.0f, 1.0f };
    };

    /// 指定時刻で鳴らす効果音や演出確認などのイベント情報を保持する。
    struct EventMarker {
        float time = 0.0f;
        int type = 0;
        std::string name;
        engine::Vector3 offset = { 0.0f, 0.0f, 0.0f };
    };

private:
    /// 再生、ループ、速度と現在時刻の操作 UI を描画する。
    void DrawPreviewControls();
    /// タイムラインの尺、スナップ、ズームを操作する。
    void DrawTimelineControls();
    /// 指定時刻へ移動し、プレビュー姿勢を同期する。
    void SeekTimeline(float time);
    /// モデルと編集対象クリップの選択 UI を描画する。
    void DrawAssetControls();
    /// Body Transform のキー値を編集する。
    void DrawBodyControls();
    /// 検索、選択したジョイントと編集値を表示する。
    void DrawJointControls();
    /// 選択ジョイントの姿勢キーを追加・編集・削除する。
    void DrawKeyframeControls();
    /// 時刻付きイベントマーカーを編集する。
    void DrawEventControls();
    /// 指定モデルをプレビューへ読み込めたか返す。
    bool LoadPreviewModel(const std::string& modelName);
    /// 現在のモデルから編集用オブジェクトを作成する。
    void CreatePreviewObject();
    /// 編集用オブジェクトをシーンから取り除く。
    void RemovePreviewObject();
    /// 現在の編集用オブジェクトを探す。
    engine::Object3d* FindPreviewObject() const;
    /// 現在時刻に対応する補間姿勢をプレビューObjectへ適用する。
    void ApplyTimelinePose();
    /// UI の Body Transform 値から保存用キーを組み立てる。
    BodyAnimationClip::Key BuildBodyKeyFromUi() const;
    /// 現在時刻の Body キーを追加または上書きする。
    void AddOrUpdateBodyKey();
    /// 選択中の Body キーを削除する。
    void DeleteSelectedBodyKey();
    /// 指定時刻の前後キーから Body 姿勢を補間する。
    bool TryGetInterpolatedBodyKey(float time, BodyAnimationClip::Key& keyOut) const;
    /// Body キーの値を UI へ反映する。
    void SyncBodyUiFromKey(const BodyAnimationClip::Key& key);
    /// 現在時刻の Body 姿勢を UI に表示する。
    void SyncBodyUiFromTimeline();
    /// Body キーを再生時刻順へ並べる。
    void SortBodyKeys();
    /// ジョイント姿勢キーをプレビューへ適用する。
    void ApplyPoseKey(const PoseKey& key);
    /// UI のジョイント値から保存用キーを組み立てる。
    PoseKey BuildPoseKeyFromUi() const;
    /// 現在時刻のジョイントキーを追加または上書きする。
    void AddOrUpdateKey();
    /// 選択中ジョイントの現在時刻のキーを削除する。
    void DeleteSelectedJointKeyAtCurrentTime();
    /// 指定ジョイントの前後キーを探し、現在時刻の補間結果を取得する。
    bool TryGetInterpolatedKey(int jointIndex, float time, PoseKey& keyOut) const;
    /// 選択ジョイントの現在姿勢を編集 UI へ反映する。
    void SyncUiFromJoint(int jointIndex);
    /// ジョイント姿勢キーを再生時刻順へ並べる。
    void SortKeys();
    /// 読み込み可能な制作データの一覧を更新する。
    void RefreshAssetFiles();
    /// 新規クリップの編集状態へ切り替える。
    void NewAuthoringClip();
    /// 編集中のキーやイベントをAuthoring用JSONへ保存する。
    void SaveAuthoringJson();
    /// Authoring 用 JSON を読み込み、キーとイベントを復元する。
    void LoadAuthoringJson();
    /// 現在のクリップ名から保存先を組み立てる。
    std::string GetSavePath() const;
    /// UI の姿勢値を一時的な編集上書きとして保持する。
    void StoreEditOverrideFromUi();
    /// 指定ジョイントの編集上書きを外す。
    void ClearEditOverride(int jointIndex);
    /// すべてのジョイント編集上書きを外す。
    void ClearAllEditOverrides();
    /// 前回時刻から現在時刻までに通過したイベントを探す。
    void UpdateEventPreview(float previousTime, float currentTime);
    /// 指定イベントをプレビュー再生する。
    void FireEventPreview(const EventMarker& marker);
    /// イベントをタイムライン上の表示文字列へ変換する。
    std::string FormatEventMarker(const EventMarker& marker) const;
    /// Body Transform 用のギズモを描画する。
    void DrawBodyGizmo();
    /// ボーンの表示と姿勢編集ギズモを描画する。
    void DrawBoneOverlayAndGizmo();
    /// ワールド座標を Game View 内の画面座標へ投影する。
    bool ProjectWorldToGameView(const engine::Vector3& world, engine::Vector2& screenOut) const;
    /// 指定ジョイントのワールド変換行列を返す。
    engine::Matrix4x4 GetJointWorldMatrix(int jointIndex) const;
    /// 行列の平行移動成分を取り出す。
    engine::Vector3 GetMatrixTranslation(const engine::Matrix4x4& matrix) const;
    /// ラジアンの三軸回転を度へ換算する。
    engine::Vector3 ToDegrees(const engine::Vector3& radians) const;
    /// 度の三軸回転をラジアンへ換算する。
    engine::Vector3 ToRadians(const engine::Vector3& degrees) const;

private:
    SceneManager* sceneManager_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    Model* previewModel_ = nullptr;
    engine::Object3d* previewObject_ = nullptr;

    // プレビュー再生と表示オプション。
    bool enabled_ = false;
    // 制作空間の所有権と時間軸の表示状態。ゲーム用クリップには保存しない。
    bool ownsStudio_ = false;
    int pendingClipAction_ = 0;
    std::string pendingClipPath_;
    bool timelineSnap_ = true;
    float timelineZoom_ = 220.0f;
    int timelineFps_ = 30;
    int timelineDragKind_ = -1;
    int timelineDragIndex_ = -1;
    float timelineDragTime_ = 0.0f;
    float timelineDragMouseX_ = 0.0f;
    bool wasEnabled_ = false;
    bool autoApply_ = true;
    bool play_ = false;
    bool loop_ = true;
    bool editBodyTransform_ = true;
    bool useBaseAnimation_ = true;
    bool showBoneOverlay_ = true;
    bool showBoneNames_ = false;
    bool showBoneAxes_ = true;
    bool enableBoneGizmo_ = true;
    bool autoKeyOnGizmo_ = false;
    bool enableBodyGizmo_ = true;
    bool autoKeyBodyOnGizmo_ = false;
    bool bodyEditOverrideActive_ = false;
    bool previewEvents_ = true;
    bool cameraMoveOnEnter_ = true;
    bool recenterCameraRequested_ = false;
    bool hasPlacedCamera_ = false;
    BaseScene* previewScene_ = nullptr;
    bool storedCameraValid_ = false;
    engine::Vector3 storedEye_{}, storedTarget_{}, storedRotation_{};
    engine::Object3d* storedFollow_ = nullptr;
    int storedCameraMode_ = 0;

    // タイムラインとプレビュー配置。
    float currentTime_ = 0.0f;
    float duration_ = 2.0f;
    float playbackSpeed_ = 1.0f;
    float bonePointRadius_ = 5.0f;
    float boneLineThickness_ = 3.0f;
    float eventPreviewTimer_ = 0.0f;
    float cameraDistance_ = 8.0f;
    float cameraHeight_ = 3.0f;
    engine::Vector3 previewOrigin_ = { 12000.0f, 10000.0f, 12000.0f };
    engine::Vector3 previewScale_ = { 1.0f, 1.0f, 1.0f };

    // UI入力。
    char modelNameBuffer_[256] = "";
    char animationNameBuffer_[128] = "";
    char saveFileBuffer_[128] = "new_body_animation.json";
    char jointSearchBuffer_[128] = "";
    char eventNameBuffer_[128] = "attack";
    int selectedJointIndex_ = -1;
    int selectedBodyKeyIndex_ = -1;
    int selectedEventIndex_ = -1;
    int eventType_ = 0;
    std::string lastEventPreviewText_;
    engine::Vector3 jointTranslateUi_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 jointRotateDegUi_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 jointScaleUi_ = { 1.0f, 1.0f, 1.0f };
    engine::Vector3 bodyTranslateUi_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 bodyShearUi_ = {};
    float bodyShearPivotY_ = 0.0f;
    engine::Vector3 bodyRotateDegUi_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 bodyScaleUi_ = { 1.0f, 1.0f, 1.0f };
    int bodyEasingToNext_ = 4;
    engine::Vector3 eventOffsetUi_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector2 gameViewOffset_ = { 0.0f, 0.0f };
    engine::Vector2 gameViewSize_ = { 1280.0f, 720.0f };
    engine::Vector2 gameViewMousePos_ = { 0.0f, 0.0f };
    bool isGameViewHovered_ = false;
    int gizmoOperation_ = 0;

    std::vector<BodyAnimationClip::Key> bodyKeys_;
    std::vector<PoseKey> keys_;
    std::vector<EventMarker> events_;
    std::vector<std::string> assetFiles_;
    std::map<int, PoseKey> editPoseOverrides_;
};
