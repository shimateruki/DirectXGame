#pragma once
#include "Camera.h"
#include "IEditable.h"
#include "engine/utility/math/Math.h"
#include "Object3d.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Object3d;
class Object3dCommon;
class PrimitiveDrawer;
struct ID3D12GraphicsCommandList;

/// <summary>
/// ゲームカメラとEditor用自由カメラの切り替え、設定保存、演出カメラ再生を行う。
/// </summary>
// CameraEditorは、編集用カメラ、プレビューカメラ、演出用カメラの設定をまとめて扱います。
class CameraEditor : public IEditable {
public:
    enum class Mode {
        Game,  // ゲーム用カメラ。
        Editor // WASD操作の自由カメラ。
    };

    struct Settings {
        Mode currentMode = Mode::Game;
        Camera::FollowMode gameFollowMode = Camera::FollowMode::kAimable;

        // Gameモード用パラメータ。
        float distance = 15.0f;
        float height = 5.0f;
        Vector3 angle = { 25.0f, 0.0f, 0.0f };
        Vector3 lockOnOffset = { 0.0f, 4.0f, -12.0f };

        // Orbitモード用パラメータ。
        float orbitRadius = 15.0f;
        Vector3 orbitCenterOffset = { 0.0f, 0.0f, 0.0f };
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
        bool savedOverrideGuideVisible = true;
        bool selectedOverridePreview = true;
        float cameraGuideSize = 0.65f;
        float cameraFrustumLength = 6.0f;
        float cameraPreviewHeight = 180.0f;

        // Editorモード用パラメータ。
        float moveSpeed = 0.5f;
        float boostSpeed = 2.0f;
        float mouseSensitivity = 0.003f;
        Vector3 fixedPointPos = { 0.0f, 5.0f, -15.0f };
        Vector3 fixedPointAngle = { 0.0f, 0.0f, 0.0f };

        // Editorカメラの保存用状態。
        Vector3 editorCameraPos = { 0.0f, 5.0f, -15.0f };
        Vector3 editorCameraAngle = { 0.0f, 0.0f, 0.0f };
    };

public:
    static CameraEditor* GetInstance();

    void Initialize();
    void Update(Object3d* player, bool isLockingOn);
    void DrawImGui() override;
    std::string GetName() override { return "Camera Editor"; }

    void SaveSettings();
    void LoadSettings();
    void LoadFile(const std::string& fileName);
    void RefreshFileList();

    void SaveEditorState();
    void LoadEditorState();

    bool IsEditorMode() const { return settings_.currentMode == Mode::Editor; }
    void SetMode(Mode mode);
    Mode GetMode() const { return settings_.currentMode; }
    void SetEditorCameraTransform(const Vector3& position, const Vector3& rotation);
    bool FocusSelectedOverrideCamera();
    void SetGameViewHovered(bool hovered) { isGameViewHovered_ = hovered; }
    void SetEditorStateSaveBlocker(uint32_t blocker, bool enabled);
    bool IsEditorStateSaveBlocked() const { return editorStateSaveBlockers_ != 0; }

    /// <summary>
    /// 保存済みの名前を指定して、カメラの一時上書き演出を再生する。
    /// </summary>
    bool PlayOverrideCamera(Camera* camera, const std::string& cameraName);

    int GetCameraSensitivity() const { return settings_.cameraSensitivity; }
    void SetCameraSensitivity(int val) {
        settings_.cameraSensitivity = val;
        SaveSettings();
    }

    void DrawOrbitGuide(PrimitiveDrawer& primitiveDrawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit);
    void DrawCameraGuide(PrimitiveDrawer& primitiveDrawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit);
    void SetObject3dCommon(Object3dCommon* common);
    void DrawCameraModelGizmos(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    void DrawOrbitCenterGizmo(const Vector2& gameViewOffset, const Vector2& gameViewSize, bool snapEnabled, float snapValue);
    void DrawSelectedOverrideCameraGizmo(const Vector2& gameViewOffset, const Vector2& gameViewSize, bool snapEnabled, float snapValue);
    bool ShouldRenderCameraPreview() const { return settings_.cameraPreviewVisible; }
    void SetCameraPreviewVisible(bool visible, bool save = true);
    bool ShouldShowSceneCameraPreviewOverlay() const;
    Camera* PreparePreviewCamera(float aspectRatio);
    Camera* PrepareCinematicPreviewCamera(float aspectRatio);

private:
    void UpdateFreeCamera(Camera* camera);
    void ApplyOrbitStartAngle(Camera* camera, bool resetSmoothing);
    void CaptureOrbitStartFromCurrentCamera(Camera* camera);
    void SetOrbitCenterFromWorld(const Vector3& center);
    void SetOrbitStartFromWorld(const Vector3& startEye);
    Vector3 GetOrbitCenter() const;
    Vector3 GetOrbitStartEye() const;
    Vector3 GetConfiguredCameraEye() const;
    Vector3 GetConfiguredCameraForward() const;
    Vector3 GetPreviewCameraEye() const;
    Vector3 GetPreviewCameraForward() const;
    const char* GetPreviewCameraLabel() const;
    const Camera::CameraOverrideParams* GetSelectedOverrideParams() const;
    Vector3 ResolveOverrideEye(const Camera::CameraOverrideParams& params) const;
    Vector3 ResolveOverrideTarget(const Camera::CameraOverrideParams& params) const;
    Vector3 ResolveOverrideForward(const Camera::CameraOverrideParams& params) const;
    Vector3 GetConfiguredCameraRight(const Vector3& forward) const;
    Vector3 GetConfiguredCameraUp(const Vector3& forward, const Vector3& right) const;
    Matrix4x4 MakeLineBoxMatrix(const Vector3& start, const Vector3& end, float thickness) const;
    void EnsureCameraModelGizmo(std::unique_ptr<Object3d>& gizmo, const std::string& name);
    void ApplyCameraModelGizmo(Object3d* gizmo, const Vector3& eye, const Vector3& forward, float sizeScale, const Vector4& color);
        // 選択中カメラのプレビュー表示と操作パネルを描画します。
void DrawCameraPreviewPanel();
    void ApplyConfiguredCameraPreview(Camera* camera) const;

private:
    CameraEditor() = default;
    ~CameraEditor() = default;
    CameraEditor(const CameraEditor&) = delete;
    const CameraEditor& operator=(const CameraEditor&) = delete;

    Settings settings_;
    char fileNameBuffer_[64] = "camera_settings.json";

    const std::string kDirectoryPath_ = "Resources/json/camera/";
    std::vector<std::string> fileList_;
    bool isGameViewHovered_ = false;
    bool isDraggingOrbitCenterGizmo_ = false;
    bool isDraggingOverrideCameraGizmo_ = false;
    int orbitEditGizmoTarget_ = 1;
    int overrideCameraGizmoMode_ = 2;
    std::map<std::string, Camera::CameraOverrideParams> overrideParamsMap_;
    uint32_t editorStateSaveBlockers_ = 0;
    std::string selectedOverrideName_ = "";
    char newOverrideNameBuffer_[64] = "";
    Object3d* targetPlayer_ = nullptr;
    Camera previewCamera_;
    bool previewCameraInitialized_ = false;
    Object3dCommon* object3dCommon_ = nullptr;
    std::unique_ptr<Object3d> gameCameraModelGizmo_;
    std::vector<std::unique_ptr<Object3d>> overrideCameraModelGizmos_;
};
