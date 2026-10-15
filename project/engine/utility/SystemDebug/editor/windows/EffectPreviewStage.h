#pragma once

#include "IEditable.h"
#include "engine/utility/math/Math.h"
#include <cstddef>
#include <string>
#include <vector>

class DirectXCommon;
namespace engine { class Object3d; }
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

    // プレビュー時間軸へ表示する区間と色。これ自体はゲームのイベントを発火しない。
    struct TimelineEvent {
        std::string label;
        // プレビュー開始からの秒数。終了時刻は開始時刻以上を想定する。
        float startTime = 0.0f;
        float endTime = 0.0f;
        engine::Vector4 color = { 0.35f, 0.75f, 1.0f, 1.0f };
    };

    // 呼出元が提供したプレビュー計測値。CPU/GPU はミリ秒、frameDeltaSeconds は秒。
    struct PerformanceMetrics {
        // falseのとき測定値は呼び出し元から提供されていない。
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
    // プレビュー空間と再生状態を更新する。
    void Update();
    // 床、カメラ、照明などのプレビュー設定画面を描画する。
    void DrawImGui() override;
    // 各ツールが報告したイベントと再生操作を時間軸に表示する。
    void DrawTimelineWindow();
    std::string GetName() override { return "Effect Preview Stage"; }

    bool IsEnabled() const { return enabled_; }
#ifdef USE_IMGUI
    bool* GetGridVisiblePtr() { return &showGrid_; }
    bool* GetAxesVisiblePtr() { return &showAxes_; }
#endif
    bool IsLoopEnabled() const { return loopPreview_; }
    float GetPlaybackSpeed() const { return transportPlaying_ ? playbackSpeed_ : 0.0f; }
    float GetConfiguredPlaybackSpeed() const { return playbackSpeed_; }
    bool IsTransportPlaying() const { return transportPlaying_; }
    /// 床、グリッド、接地型プレビューが共有する基準位置を返す。
    engine::Vector3 GetGroundPosition() const;
    /// Mesh EffectやParticleを半分埋めずに確認するため、床から中心高さを加えた位置を返す。
    engine::Vector3 GetPreviewPosition() const;
    // 各番号は対応する操作要求のたびに増える。ツール側は前回見た番号との差で新しい要求を検出する。
    int GetPlayRequestSerial() const { return playRequestSerial_; }
    int GetStopRequestSerial() const { return stopRequestSerial_; }
    int GetSeekRequestSerial() const { return seekRequestSerial_; }
    /// シーク要求先の時刻。単位は秒。
    float GetSeekTargetTime() const { return seekTargetTime_; }
    /// <summary>ツールの現在状態をタイムラインへ報告する。時刻・長さは秒、性能時間はミリ秒。</summary>
    /// <remarks>ツールまたはタイトルが変わると計測結果と重複シーク判定をリセットする。</remarks>
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
    /// <summary>隔離プレビューを開始する。初回入場時だけシーンのカメラ状態を退避する。</summary>
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
    // プレビュー空間の終了時に専用床を除去する。
    void RemoveFloor();
    // 専用床と環境Objectを名前から探す。見つからない場合は nullptr。
    engine::Object3d* FindFloor() const;
    engine::Object3d* FindEnvironmentObject(const std::string& name) const;
    // 背景用Objectの生成、姿勢・色・表示状態の反映。
    void CreateEnvironmentObject(const std::string& name);
    void UpdateEnvironmentObject(const std::string& name, const engine::Vector3& translate, const engine::Vector3& scale, const engine::Vector4& color);
    void SetEnvironmentObjectVisible(const std::string& name, bool visible);
    // 選択したスタジオ背景と照明のプリセットを反映する。
    void ApplyStudioPreset(int presetIndex);
    // プレビュー用照明を適用し、終了時には退避した照明へ戻す。
    void ApplyStudioLighting();
    void RestoreStudioLighting();
    // プレビュー背景色を描画基盤へ反映する。
    void ApplyBackgroundColor();
    // シーンのカメラ状態をプレビュー入場時に保存する。
    void CaptureCameraState();
    /// プレビュー開始前に保存したカメラ状態へ戻す。
    void RestoreCameraState();
    // プレビュー対象が見える位置と方向へカメラを置く。
    void PlaceCameraAtPreview();
    // ツールが報告した計測値を蓄積／リセットする。
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
    engine::Vector3 origin_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 isolatedBase_ = { 2048.0f, 2048.0f, 2048.0f };
    engine::Vector3 storedEye_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 storedTarget_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 storedRotation_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector4 backgroundColor_ = { 0.095f, 0.105f, 0.125f, 1.0f };
    engine::Vector4 floorColor_ = { 0.20f, 0.215f, 0.24f, 1.0f };
    engine::Vector4 minorGridColor_ = { 0.30f, 0.32f, 0.35f, 1.0f };
    engine::Vector4 majorGridColor_ = { 0.46f, 0.49f, 0.54f, 1.0f };
    engine::Vector4 xAxisColor_ = { 0.82f, 0.22f, 0.20f, 1.0f };
    engine::Vector4 yAxisColor_ = { 0.32f, 0.75f, 0.32f, 1.0f };
    engine::Vector4 zAxisColor_ = { 0.22f, 0.42f, 0.88f, 1.0f };
    engine::Vector4 studioLightColor_ = { 1.0f, 0.965f, 0.90f, 1.0f };
    engine::Vector3 studioLightDirection_ = { -0.45f, -1.0f, 0.35f };
    engine::Vector4 storedDirectionalColor_ = {};
    engine::Vector3 storedDirectionalDirection_ = {};
    engine::Vector3 storedAmbientColor_ = {};
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
