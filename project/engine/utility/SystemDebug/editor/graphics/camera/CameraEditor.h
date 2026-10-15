#pragma once
#include "Camera.h"
#include "IEditable.h"
#include "engine/utility/math/Math.h"
#include "Object3d.h"
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace engine { class Object3d; }
class Object3dCommon;
class PrimitiveDrawer;
struct ID3D12GraphicsCommandList;

/// <summary>ゲーム・自由・演出カメラの切替、設定保存、プレビューを管理する。</summary>
class CameraEditor : public IEditable {
public:
    enum class Mode {
        Game,  // ゲーム用カメラ。
        Editor // WASD操作の自由カメラ。
    };

    // 編集画面のカメラモード別設定。各モードの値を保持し、切替時に使用する設定を選ぶ。
    struct Settings {
        Mode currentMode = Mode::Game;
        Camera::FollowMode gameFollowMode = Camera::FollowMode::kAimable;

        // Gameモード用パラメータ。
        float distance = 15.0f;
        float height = 5.0f;
        engine::Vector3 angle = { 25.0f, 0.0f, 0.0f };
        engine::Vector3 lockOnOffset = { 0.0f, 4.0f, -12.0f };

        // Orbitモード用パラメータ。
        float orbitRadius = 15.0f;
        engine::Vector3 orbitCenterOffset = { 0.0f, 0.0f, 0.0f };
        float orbitCenterHeight = 5.0f;
        float orbitHeight = 5.0f;
        float orbitSpeed = 0.005f;
        float orbitStartAngleDeg = 0.0f;
        bool orbitGuideVisible = true;
        bool orbitCenterGizmoVisible = true;
        float orbitGuideMarkerSize = 0.45f;
        int cameraSensitivity = 0;
        bool cameraGuideVisible = true;
        bool cameraBodyVisible = true;
        bool cameraPreviewVisible = false;
        int cameraPreviewFps = 15;
        float cameraPreviewResolutionScale = 0.5f;
        float cameraGuideSize = 0.65f;
        float cameraFrustumLength = 6.0f;
        float cameraPreviewHeight = 180.0f;

        // Editorモード用パラメータ。
        float moveSpeed = 0.5f;
        float boostSpeed = 2.0f;
        float mouseSensitivity = 0.003f;
        engine::Vector3 fixedPointPos = { 0.0f, 5.0f, -15.0f };
        engine::Vector3 fixedPointAngle = { 0.0f, 0.0f, 0.0f };

        // Editorカメラの保存用状態。
        engine::Vector3 editorCameraPos = { 0.0f, 5.0f, -15.0f };
        engine::Vector3 editorCameraAngle = { 0.0f, 0.0f, 0.0f };
    };

public:
    /// カメラ編集の共有インスタンスを返す。
    static CameraEditor* GetInstance();

    /// 設定と自由カメラの保存状態を読み込む。
    void Initialize();
    /// 現在のカメラモードに応じて追従または自由移動を更新する。
    void Update(engine::Object3d* player, bool isLockingOn);
    /// モード、視点、演出 Override の編集 UI を描画する。
    void DrawImGui() override;
    std::string GetName() override { return "Camera Editor"; }

    /// 現在のゲームカメラ設定と演出用OverrideをJSONへ保存する。
    void SaveSettings();
    /// 選択中のJSONからカメラ設定と演出用Overrideを復元する。
    void LoadSettings();
    /// ファイル名を切り替え、未作成なら既定値で新規保存する。
    void LoadFile(const std::string& fileName);
    /// カメラ設定フォルダ内のJSON一覧を再収集する。
    void RefreshFileList();

    /// 自由カメラの位置・角度だけをEditor専用ファイルへ保存する。
    void SaveEditorState();
    /// Editor専用ファイルから自由カメラの位置・角度を復元する。
    void LoadEditorState();

    bool IsEditorMode() const { return settings_.currentMode == Mode::Editor; }
    /// ゲーム視点と自由カメラを切り替える。
    void SetMode(Mode mode);
    Mode GetMode() const { return settings_.currentMode; }
    // 自由カメラのワールド位置と回転を設定する。変更時は保存を予約し、ブロック中は予約しない。
    void SetEditorCameraTransform(const engine::Vector3& position, const engine::Vector3& rotation);
    // 現在シーンにある Object の見た目、なければ当たり判定の範囲へ視点を合わせる。
    // 対象が無効または有効な範囲を持たなければ false。
    bool FocusSceneObject(engine::Object3d* object);
    /// 選択中の Camera Object に Editor 視点を合わせられたか返す。
    bool FocusSelectedCameraObject();
    void SetGameViewHovered(bool hovered) { isGameViewHovered_ = hovered; }
    // blocker のビットを有効・無効化し、編集中の自由カメラ状態の自動保存を抑制する。
    void SetEditorStateSaveBlocker(uint32_t blocker, bool enabled);
    bool IsEditorStateSaveBlocked() const { return editorStateSaveBlockers_ != 0; }

    /// <summary>
    /// 保存済みの名前を指定して、カメラの一時上書き演出を再生する。
    /// </summary>
    bool PlayOverrideCamera(Camera* camera, const std::string& cameraName);
    // Camera ObjectをGhostRecorderなどから実カメラとして再生する。
    /// Camera Object の保存済み視点を実カメラで再生する。
    bool PlaySceneObjectCamera(Camera* camera, engine::Object3d* cameraObject);
    /// ブレンド時間と補間方式を指定して Camera Object を再生する。
    bool PlaySceneObjectCamera(
        Camera* camera,
        engine::Object3d* cameraObject,
        float blendInDuration,
        float blendOutDuration,
        int easing);
    /// Camera Object の再生を止め、元のカメラへ戻す。
    void StopSceneObjectCamera(Camera* camera);
    /// Inspector で選択中の Camera Object を設定する。所有権は移さない。
    void SetSelectedCameraObject(engine::Object3d* cameraObject);
    /// 現在選択中の Camera Object を返す。
    engine::Object3d* GetSelectedCameraObject() const;
    // Camera Objectの保存済み設定から、実際の視点と注視点を解決します。
    bool ResolveSceneCameraPose(const engine::Object3d* cameraObject, engine::Vector3& eye, engine::Vector3& target) const;

    int GetCameraSensitivity() const { return settings_.cameraSensitivity; }
#ifdef USE_IMGUI
    bool* GetCameraGuideVisiblePtr() { return &settings_.cameraGuideVisible; }
    bool* GetCameraPreviewVisiblePtr() { return &settings_.cameraPreviewVisible; }
    bool* GetCameraBodyVisiblePtr() { return &settings_.cameraBodyVisible; }
    bool* GetOrbitGuideVisiblePtr() { return &settings_.orbitGuideVisible; }
    bool* GetOrbitGizmoVisiblePtr() { return &settings_.orbitCenterGizmoVisible; }
#endif
    void SetCameraSensitivity(int val) {
        settings_.cameraSensitivity = val;
        SaveSettings();
    }

    /// Orbit 視点の軌道ガイドを描画し、描画数を上限内で加算する。
    void DrawOrbitGuide(PrimitiveDrawer& primitiveDrawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit);
    /// カメラ本体と視錐台のガイドを描画する。
    void DrawCameraGuide(PrimitiveDrawer& primitiveDrawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit);
    /// カメラ表示モデルの生成に使う共通描画情報を設定する。
    void SetObject3dCommon(Object3dCommon* common);
    /// シーン上のカメラ位置にモデルギズモを描画する。
    void DrawCameraModelGizmos(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    /// Game View 内で Orbit 中心を編集するギズモを描画する。
    void DrawOrbitCenterGizmo(const engine::Vector2& gameViewOffset, const engine::Vector2& gameViewSize, bool snapEnabled, float snapValue);
    // ImGui上で実際に見えているプレビューだけを、指定FPSで更新します。
    void BeginPreviewUiFrame();
    /// Scene Camera プレビューウィンドウの表示状態を通知する。
    void SetSceneCameraPreviewWindowVisible(bool visible);
    /// 選択中の Scene Camera がプレビュー可能か返す。
    bool HasSceneCameraPreviewTarget() const;
    /// フレームレート制限を踏まえ、通常カメラの再描画が必要か返す。
    bool ShouldRenderCameraPreview() const;
    /// フレームレート制限を踏まえ、Scene Camera の再描画が必要か返す。
    bool ShouldRenderSceneCameraPreview() const;
    /// プレビュー描画の時刻と対象種別を記録する。
    void NotifyCameraPreviewRendered(bool sceneCameraPreview);
    // Scene差し替え時に、破棄済みObjectへの選択とPreview更新履歴を無効化します。
    void InvalidatePreviewForSceneChange();
    /// 現在設定を用いたプレビューカメラを指定アスペクト比で準備する。
    Camera* PreparePreviewCamera(float aspectRatio);
    /// 演出用 Camera Object のプレビューカメラを準備する。
    Camera* PrepareCinematicPreviewCamera(float aspectRatio);

private:
    /// WASD とマウス入力で自由カメラを移動する。
    void UpdateFreeCamera(Camera* camera);
    /// 保存した開始角を Orbit カメラへ適用する。
    void ApplyOrbitStartAngle(Camera* camera, bool resetSmoothing);
    /// 現在視点から Orbit の開始角を取得する。
    void CaptureOrbitStartFromCurrentCamera(Camera* camera);
    /// ワールド座標から Orbit 中心設定を更新する。
    void SetOrbitCenterFromWorld(const engine::Vector3& center);
    /// ワールド座標から Orbit 開始視点の設定を更新する。
    void SetOrbitStartFromWorld(const engine::Vector3& startEye);
    /// Orbit 設定から中心と開始視点を復元する。
    engine::Vector3 GetOrbitCenter() const;
    engine::Vector3 GetOrbitStartEye() const;
    /// 編集中のカメラ設定から視点と向きを解決する。
    engine::Vector3 GetConfiguredCameraEye() const;
    engine::Vector3 GetConfiguredCameraForward() const;
    /// 表示中のプレビューで用いる視点と向きを返す。
    engine::Vector3 GetPreviewCameraEye() const;
    engine::Vector3 GetPreviewCameraForward() const;
    const char* GetPreviewCameraLabel() const;
    engine::Object3d* FindSceneObjectByName(const std::string& name) const;
    /// 保存済み Override の参照先 Object を実行時参照へ解決する。
    Camera::CameraOverrideParams MakeRuntimeOverrideParams(
        const Camera::CameraOverrideParams& params,
        engine::Object3d* fallbackEyeObject = nullptr) const;
    /// 演出用視点の位置と注視点をプレビュー向けに求める。
    bool ResolveCinematicPreviewPose(engine::Vector3& eye, engine::Vector3& target) const;
    /// Camera Object の設定から実行時 Override を構築する。
    Camera::CameraOverrideParams MakeRuntimeOverrideParams(const engine::Object3d* cameraObject) const;
    /// Override の参照先を含めて視点と注視点を解決する。
    engine::Vector3 ResolveOverrideEye(const Camera::CameraOverrideParams& params) const;
    engine::Vector3 ResolveOverrideTarget(const Camera::CameraOverrideParams& params) const;
    engine::Vector3 GetConfiguredCameraRight(const engine::Vector3& forward) const;
    engine::Vector3 GetConfiguredCameraUp(const engine::Vector3& forward, const engine::Vector3& right) const;
    /// 線ガイドを細い箱として描くための変換行列を作る。
    engine::Matrix4x4 MakeLineBoxMatrix(const engine::Vector3& start, const engine::Vector3& end, float thickness) const;
    /// カメラのモデルギズモがなければ生成する。
    void EnsureCameraModelGizmo(std::unique_ptr<engine::Object3d>& gizmo, const std::string& name);
    /// ギズモへ視点、向き、大きさ、色を反映する。
    void ApplyCameraModelGizmo(engine::Object3d* gizmo, const engine::Vector3& eye, const engine::Vector3& forward, float sizeScale, const engine::Vector4& color);
    /// 視野角とクリップ距離を指定してプレビューカメラを構成する。
    Camera* ConfigurePreviewCamera(
        const engine::Vector3& eye,
        const engine::Vector3& target,
        float aspectRatio,
        float fovY,
        float nearClip,
        float farClip);
    // 選択中カメラのプレビュー表示と操作パネルを描画する。
    void DrawCameraPreviewPanel();
    /// 保存済み設定の位置と向きをプレビューカメラへ反映する。
    void ApplyConfiguredCameraPreview(Camera* camera) const;
    /// 指定時刻からプレビューの更新周期が経過したか返す。
    bool IsPreviewUpdateDue(
        const std::chrono::steady_clock::time_point& lastRenderTime,
        bool hasRenderedFrame) const;
    /// 通常・演出用のプレビュー描画履歴を無効にする。
    void InvalidateCameraPreviews();

private:
    CameraEditor() = default;
    ~CameraEditor() = default;
    CameraEditor(const CameraEditor&) = delete;
    const CameraEditor& operator=(const CameraEditor&) = delete;

    Settings settings_;
    char fileNameBuffer_[64] = "camera_settings.json";

    const std::string kDirectoryPath_ = "Resources/json/camera/";
    const std::string kEditorStatePath_ = "output/editor_state/editor_camera_state.json";
    std::vector<std::string> fileList_;
    bool isGameViewHovered_ = false;
    bool isDraggingOrbitCenterGizmo_ = false;
    int orbitEditGizmoTarget_ = 1;
    std::map<std::string, Camera::CameraOverrideParams> overrideParamsMap_;
    uint32_t editorStateSaveBlockers_ = 0;
    bool editorStateDirty_ = false;
    std::chrono::steady_clock::time_point editorStateSaveReadyAt_{};
    std::string selectedOverrideName_ = "";
    engine::Object3d* selectedCameraObject_ = nullptr;
    float activeSceneCameraExitDuration_ = 0.30f;
    bool hasSceneCameraProjectionBackup_ = false;
    float sceneCameraBackupFovY_ = 0.45f;
    float sceneCameraBackupNearClip_ = 0.1f;
    float sceneCameraBackupFarClip_ = 1000.0f;
    char newOverrideNameBuffer_[64] = "";
    engine::Object3d* targetPlayer_ = nullptr;
    Camera previewCamera_;
    bool previewCameraInitialized_ = false;
    bool cameraPreviewPanelVisibleThisFrame_ = false;
    bool sceneCameraPreviewWindowVisibleThisFrame_ = false;
    bool hasRenderedCameraPreview_ = false;
    bool hasRenderedSceneCameraPreview_ = false;
    std::chrono::steady_clock::time_point lastCameraPreviewRenderTime_{};
    std::chrono::steady_clock::time_point lastSceneCameraPreviewRenderTime_{};
    Object3dCommon* object3dCommon_ = nullptr;
    std::unique_ptr<engine::Object3d> gameCameraModelGizmo_;
};
