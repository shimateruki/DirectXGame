#pragma once

#include "IEditable.h"
#include "Model.h"
#include "engine/utility/math/Math.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class DirectXCommon;
class Object3d;
class SceneManager;

class AnimationWorkbench : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);
    void Update(float deltaTime);
    void Finalize();
    void DrawImGui() override;
    std::string GetName() override { return "Animation Workbench"; }

    void ApplyCameraOverride();
    void SetGameViewRegion(const Vector2& offset, const Vector2& size);
    void SetGameViewMousePos(const Vector2& pos) { gameViewMousePos_ = pos; }
    void SetGameViewHovered(bool hovered) { isGameViewHovered_ = hovered; }
    bool IsEnabled() const { return enabled_; }

private:
    struct PoseKey {
        float time = 0.0f;
        int jointIndex = -1;
        std::string jointName;
        Vector3 translate = { 0.0f, 0.0f, 0.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
    };

    struct EventMarker {
        float time = 0.0f;
        int type = 0;
        std::string name;
        Vector3 offset = { 0.0f, 0.0f, 0.0f };
    };

private:
    void DrawPreviewControls();
    void DrawTimelineControls();
    void DrawJointControls();
    void DrawKeyframeControls();
    void DrawEventControls();
    bool LoadPreviewModel(const std::string& modelName);
    void CreatePreviewObject();
    void RemovePreviewObject();
    Object3d* FindPreviewObject() const;
    void ApplyTimelinePose();
    void ApplyPoseKey(const PoseKey& key);
    PoseKey BuildPoseKeyFromUi() const;
    void AddOrUpdateKey();
    void DeleteSelectedJointKeyAtCurrentTime();
    bool TryGetInterpolatedKey(int jointIndex, float time, PoseKey& keyOut) const;
    void SyncUiFromJoint(int jointIndex);
    void SortKeys();
    void SaveAuthoringJson();
    void LoadAuthoringJson();
    std::string GetSavePath() const;
    void StoreEditOverrideFromUi();
    void ClearEditOverride(int jointIndex);
    void ClearAllEditOverrides();
    void UpdateEventPreview(float previousTime, float currentTime);
    void FireEventPreview(const EventMarker& marker);
    std::string FormatEventMarker(const EventMarker& marker) const;
    void DrawBoneOverlayAndGizmo();
    bool ProjectWorldToGameView(const Vector3& world, Vector2& screenOut) const;
    Matrix4x4 GetJointWorldMatrix(int jointIndex) const;
    Vector3 GetMatrixTranslation(const Matrix4x4& matrix) const;
    Vector3 ToDegrees(const Vector3& radians) const;
    Vector3 ToRadians(const Vector3& degrees) const;

private:
    SceneManager* sceneManager_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    Model* previewModel_ = nullptr;
    Object3d* previewObject_ = nullptr;

    bool enabled_ = false;
    bool wasEnabled_ = false;
    bool autoApply_ = true;
    bool play_ = false;
    bool loop_ = true;
    bool useBaseAnimation_ = true;
    bool showBoneOverlay_ = true;
    bool showBoneNames_ = false;
    bool enableBoneGizmo_ = true;
    bool autoKeyOnGizmo_ = false;
    bool previewEvents_ = true;
    bool cameraMoveOnEnter_ = true;
    bool recenterCameraRequested_ = false;
    bool hasPlacedCamera_ = false;
    float currentTime_ = 0.0f;
    float duration_ = 2.0f;
    float playbackSpeed_ = 1.0f;
    float bonePointRadius_ = 5.0f;
    float eventPreviewTimer_ = 0.0f;
    float cameraDistance_ = 8.0f;
    float cameraHeight_ = 3.0f;
    Vector3 previewOrigin_ = { 12000.0f, 10000.0f, 12000.0f };
    Vector3 previewScale_ = { 1.0f, 1.0f, 1.0f };

    char modelNameBuffer_[256] = "";
    char animationNameBuffer_[128] = "";
    char saveFileBuffer_[128] = "enemy_animation.json";
    char jointSearchBuffer_[128] = "";
    char eventNameBuffer_[128] = "attack";
    int selectedJointIndex_ = -1;
    int selectedEventIndex_ = -1;
    int eventType_ = 0;
    std::string lastEventPreviewText_;
    Vector3 jointTranslateUi_ = { 0.0f, 0.0f, 0.0f };
    Vector3 jointRotateDegUi_ = { 0.0f, 0.0f, 0.0f };
    Vector3 jointScaleUi_ = { 1.0f, 1.0f, 1.0f };
    Vector3 eventOffsetUi_ = { 0.0f, 0.0f, 0.0f };
    Vector2 gameViewOffset_ = { 0.0f, 0.0f };
    Vector2 gameViewSize_ = { 1280.0f, 720.0f };
    Vector2 gameViewMousePos_ = { 0.0f, 0.0f };
    bool isGameViewHovered_ = false;
    int gizmoOperation_ = 0;

    std::vector<PoseKey> keys_;
    std::vector<EventMarker> events_;
    std::map<int, PoseKey> editPoseOverrides_;
};
