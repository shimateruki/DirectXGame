#pragma once

#include "IEditable.h"
#include "engine/utility/math/Math.h"
#include <string>

class DirectXCommon;
class Object3d;
class SceneManager;

class EffectPreviewStage : public IEditable {
public:
    static EffectPreviewStage* GetInstance();

    void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);
    void Update();
    void DrawImGui() override;
    std::string GetName() override { return "Effect Preview Stage"; }

    bool IsEnabled() const { return enabled_; }
    bool IsLoopEnabled() const { return loopPreview_; }
    float GetPlaybackSpeed() const { return playbackSpeed_; }
    Vector3 GetPreviewPosition() const;
    int GetPlayRequestSerial() const { return playRequestSerial_; }
    void ApplyCameraOverride();
    void RequestCameraRecenter() { recenterCameraRequested_ = true; }

private:
    EffectPreviewStage() = default;
    ~EffectPreviewStage() override = default;
    EffectPreviewStage(const EffectPreviewStage&) = delete;
    EffectPreviewStage& operator=(const EffectPreviewStage&) = delete;

private:
    void CreateFloor();
    void RemoveFloor();
    Object3d* FindFloor() const;
    void ApplyBackgroundColor();
    void CaptureCameraState();
    void RestoreCameraState();
    void PlaceCameraAtPreview();

private:
    SceneManager* sceneManager_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;

    bool enabled_ = false;
    bool isolatedSpace_ = true;
    bool moveCameraOnEnter_ = true;
    bool showFloor_ = true;
    bool loopPreview_ = true;
    bool wasEnabled_ = false;
    bool hasCapturedCamera_ = false;
    bool hasPlacedCamera_ = false;
    bool recenterCameraRequested_ = false;
    float playbackSpeed_ = 1.0f;
    float floorSize_ = 12.0f;
    float cameraDistance_ = 14.0f;
    float cameraHeight_ = 5.5f;
    Vector3 origin_ = { 0.0f, 2.0f, 0.0f };
    Vector3 isolatedBase_ = { 10000.0f, 10000.0f, 10000.0f };
    Vector3 storedEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 storedTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 storedRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector4 backgroundColor_ = { 0.06f, 0.07f, 0.09f, 1.0f };
    Vector4 floorColor_ = { 0.18f, 0.18f, 0.20f, 1.0f };
    int playRequestSerial_ = 0;
    int storedCameraMode_ = 0;
};
