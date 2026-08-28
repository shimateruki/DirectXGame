#pragma once

#include "IEditable.h"
#include "engine/utility/math/Math.h"
#include <cstddef>
#include <string>
#include <vector>

class DirectXCommon;
class Object3d;
class SceneManager;

/// パーティクルやメッシュエフェクトを安全に確認するための隔離プレビュー空間を管理する。
class EffectPreviewStage : public IEditable {
public:
    enum class ToolKind {
        None,
        CpuParticle,
        GpuParticle,
        MeshEffect,
        VfxSequence,
        Debris,
        Trail
    };

    struct TimelineEvent {
        std::string label;
        float startTime = 0.0f;
        float endTime = 0.0f;
        Vector4 color = { 0.35f, 0.75f, 1.0f, 1.0f };
    };

    struct PerformanceMetrics {
        bool available = false;
        int particleCount = 0;
        float cpuTimeMs = 0.0f;
        float gpuTimeMs = 0.0f;
        float frameDeltaSeconds = 0.0f;
        float fps = 0.0f;
        size_t memoryBytes = 0;
    };

    /// 各エフェクトツールから共有して使うプレビューStageのシングルトンを返す。
    static EffectPreviewStage* GetInstance();

    /// プレビュー用Object生成に必要なシーンと描画共通参照を保持する。
    void Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon);
    void Update();
    void DrawImGui() override;
    void DrawTimelineWindow();
    std::string GetName() override { return "Effect Preview Stage"; }

    bool IsEnabled() const { return enabled_; }
    bool IsLoopEnabled() const { return loopPreview_; }
    float GetPlaybackSpeed() const { return transportPlaying_ ? playbackSpeed_ : 0.0f; }
    float GetConfiguredPlaybackSpeed() const { return playbackSpeed_; }
    bool IsTransportPlaying() const { return transportPlaying_; }
    /// 床、グリッド、接地型プレビューが共有する基準位置を返す。
    Vector3 GetGroundPosition() const;
    /// Mesh EffectやParticleを半分埋めずに確認するため、床から中心高さを加えた位置を返す。
    Vector3 GetPreviewPosition() const;
    int GetPlayRequestSerial() const { return playRequestSerial_; }
    int GetStopRequestSerial() const { return stopRequestSerial_; }
    int GetSeekRequestSerial() const { return seekRequestSerial_; }
    float GetSeekTargetTime() const { return seekTargetTime_; }
    void ReportToolState(
        ToolKind toolKind,
        const std::string& title,
        float currentTime,
        float duration,
        bool isPlaying,
        int activeCount,
        const std::vector<TimelineEvent>& events = {},
        const PerformanceMetrics& performance = {});
    /// プレビュー中だけカメラを見やすい位置へ移動し、終了時に戻せるようにする。
    void ApplyCameraOverride();
    void RequestCameraRecenter() { recenterCameraRequested_ = true; }
    void EnableForToolPreview();
    /// 隔離プレビューを終了し、開始前のカメラ・照明・Skyboxへ直ちに戻す。
    void ReturnToScene();

private:
    EffectPreviewStage() = default;
    ~EffectPreviewStage() override = default;
    EffectPreviewStage(const EffectPreviewStage&) = delete;
    EffectPreviewStage& operator=(const EffectPreviewStage&) = delete;

private:
    /// エフェクトの接地感や大きさを確認するための床Objectを作成する。
    void CreateFloor();
    void RemoveFloor();
    Object3d* FindFloor() const;
    Object3d* FindEnvironmentObject(const std::string& name) const;
    void CreateEnvironmentObject(const std::string& name);
    void UpdateEnvironmentObject(const std::string& name, const Vector3& translate, const Vector3& scale, const Vector4& color);
    void SetEnvironmentObjectVisible(const std::string& name, bool visible);
    void ApplyStudioPreset(int presetIndex);
    void ApplyStudioLighting();
    void RestoreStudioLighting();
    void ApplyBackgroundColor();
    void CaptureCameraState();
    /// プレビュー開始前に保存したカメラ状態へ戻す。
    void RestoreCameraState();
    void PlaceCameraAtPreview();
    void UpdatePerformanceMeasurement(const PerformanceMetrics& performance);
    void ResetPerformanceMeasurement(bool clearResult);

private:
    SceneManager* sceneManager_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;

    bool enabled_ = false;
    bool isolatedSpace_ = true;
    bool moveCameraOnEnter_ = true;
    bool showFloor_ = true;
    bool showGrid_ = true;
    bool showAxes_ = true;
    bool studioLighting_ = true;
    bool loopPreview_ = true;
    bool wasEnabled_ = false;
    bool hasCapturedCamera_ = false;
    bool hasPlacedCamera_ = false;
    bool recenterCameraRequested_ = false;
    bool studioLightingApplied_ = false;
    bool hasCapturedLighting_ = false;
    bool transportPlaying_ = true;
    float playbackSpeed_ = 1.0f;
    float floorSize_ = 20.0f;
    float gridSpacing_ = 1.0f;
    float stageRadius_ = 35.0f;
    float previewCenterHeight_ = 1.0f;
    float cameraDistance_ = 14.0f;
    float cameraHeight_ = 5.5f;
    float cameraAzimuthDegrees_ = 35.0f;
    float cameraTargetHeight_ = 1.5f;
    float studioLightIntensity_ = 1.35f;
    float studioAmbientIntensity_ = 0.42f;
    Vector3 origin_ = { 0.0f, 0.0f, 0.0f };
    Vector3 isolatedBase_ = { 2048.0f, 2048.0f, 2048.0f };
    Vector3 storedEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 storedTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 storedRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector4 backgroundColor_ = { 0.095f, 0.105f, 0.125f, 1.0f };
    Vector4 floorColor_ = { 0.20f, 0.215f, 0.24f, 1.0f };
    Vector4 minorGridColor_ = { 0.30f, 0.32f, 0.35f, 1.0f };
    Vector4 majorGridColor_ = { 0.46f, 0.49f, 0.54f, 1.0f };
    Vector4 xAxisColor_ = { 0.82f, 0.22f, 0.20f, 1.0f };
    Vector4 yAxisColor_ = { 0.32f, 0.75f, 0.32f, 1.0f };
    Vector4 zAxisColor_ = { 0.22f, 0.42f, 0.88f, 1.0f };
    Vector4 studioLightColor_ = { 1.0f, 0.965f, 0.90f, 1.0f };
    Vector3 studioLightDirection_ = { -0.45f, -1.0f, 0.35f };
    Vector4 storedDirectionalColor_ = {};
    Vector3 storedDirectionalDirection_ = {};
    Vector3 storedAmbientColor_ = {};
    float storedDirectionalIntensity_ = 0.0f;
    float storedVolumetricIntensity_ = 0.0f;
    int storedEnableFog_ = 0;
    bool storedSkyboxEnabled_ = false;
    int studioPresetIndex_ = 0;
    int playRequestSerial_ = 0;
    int stopRequestSerial_ = 0;
    int seekRequestSerial_ = 0;
    int storedCameraMode_ = 0;
    ToolKind activeToolKind_ = ToolKind::None;
    std::string activeToolTitle_ = "プレビュー未選択";
    float reportedCurrentTime_ = 0.0f;
    float reportedDuration_ = 1.0f;
    float seekTargetTime_ = 0.0f;
    float timelineScrubTime_ = 0.0f;
    bool reportedPlaying_ = false;
    bool timelineScrubbing_ = false;
    float lastDispatchedSeekTarget_ = -1.0f;
    double lastSeekDispatchTimestamp_ = -1.0;
    int reportedActiveCount_ = 0;
    float timelinePixelsPerSecond_ = 220.0f;
    std::vector<TimelineEvent> reportedEvents_;
    PerformanceMetrics reportedPerformance_{};
    PerformanceMetrics measuredPerformance_{};
    bool performanceMeasurementRunning_ = false;
    bool performanceMeasurementValid_ = false;
    int performanceWarmupFrames_ = 30;
    int performanceSampleFrames_ = 120;
    int performanceWarmupProgress_ = 0;
    int performanceSampleProgress_ = 0;
    double performanceCpuTimeAccumMs_ = 0.0;
    double performanceGpuTimeAccumMs_ = 0.0;
    double performanceElapsedSeconds_ = 0.0;
    double performanceParticleCountAccum_ = 0.0;
    ToolKind performanceMeasurementTool_ = ToolKind::None;
};
