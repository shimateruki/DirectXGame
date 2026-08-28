#include "EffectPreviewStage.h"

#include "BaseScene.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "IconsFontAwesome5.h"
#include "LightManager.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr const char* kFloorName = "__Editor_EffectPreviewFloor";
constexpr const char* kLegacyBackWallName = "__Editor_EffectPreviewBackWall";
constexpr const char* kLegacyLeftRailName = "__Editor_EffectPreviewLeftRail";
constexpr const char* kLegacyRightRailName = "__Editor_EffectPreviewRightRail";
constexpr const char* kYAxisName = "__Editor_EffectPreviewAxisY";
constexpr const char* kOriginMarkerName = "__Editor_EffectPreviewOrigin";
constexpr const char* kEnvironmentPrefix = "__Editor_EffectPreview";
constexpr int kGridLineCount = 21;
constexpr int kGridCenterIndex = kGridLineCount / 2;
constexpr int kOuterMajorHalfCount = 10;

std::string MakeGridXName(int index) {
    return std::string(kEnvironmentPrefix) + "GridX_" + std::to_string(index);
}

std::string MakeGridZName(int index) {
    return std::string(kEnvironmentPrefix) + "GridZ_" + std::to_string(index);
}

std::string MakeOuterMajorXName(int index) {
    return std::string(kEnvironmentPrefix) + "OuterMajorX_" + std::to_string(index);
}

std::string MakeOuterMajorZName(int index) {
    return std::string(kEnvironmentPrefix) + "OuterMajorZ_" + std::to_string(index);
}

bool IsPreviewEnvironmentName(const std::string& name) {
    return name.rfind(kEnvironmentPrefix, 0) == 0;
}

const char* GetToolKindName(EffectPreviewStage::ToolKind kind) {
    switch (kind) {
    case EffectPreviewStage::ToolKind::CpuParticle:
        return "CPU Particle";
    case EffectPreviewStage::ToolKind::GpuParticle:
        return "GPU Particle";
    case EffectPreviewStage::ToolKind::MeshEffect:
        return "Mesh Effect";
    case EffectPreviewStage::ToolKind::VfxSequence:
        return "VFX Sequence";
    case EffectPreviewStage::ToolKind::Debris:
        return "Debris";
    case EffectPreviewStage::ToolKind::Trail:
        return "Trail";
    case EffectPreviewStage::ToolKind::None:
    default:
        return "None";
    }
}

ImU32 ToImU32(const Vector4& color) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w));
}
}

EffectPreviewStage* EffectPreviewStage::GetInstance() {
    static EffectPreviewStage instance;
    return &instance;
}

void EffectPreviewStage::Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon) {
    sceneManager_ = sceneManager;
    dxCommon_ = dxCommon;
}

void EffectPreviewStage::EnableForToolPreview() {
    const bool isEnteringPreview = !enabled_;
    enabled_ = true;
    isolatedSpace_ = true;
    showFloor_ = true;
    moveCameraOnEnter_ = true;
    stageRadius_ = (std::max)(stageRadius_, 35.0f);
    cameraDistance_ = (std::max)(cameraDistance_, 14.0f);
    cameraHeight_ = (std::max)(cameraHeight_, 5.5f);
    // 再生位置の変更やループ再生成では、ユーザーが動かしたカメラを維持する。
    // 自動配置はプレビュー空間へ入った最初の1回だけ行う。
    if (isEnteringPreview) {
        // UIから再入場したフレームはUpdateより先にCamera Overrideが走る場合があるため、
        // 入場要求を受けた時点で通常Sceneの視点を確保する。
        CaptureCameraState();
        recenterCameraRequested_ = true;
    }
}

void EffectPreviewStage::ReturnToScene() {
    enabled_ = false;
    RestoreStudioLighting();
    RestoreCameraState();
    hasPlacedCamera_ = false;
    recenterCameraRequested_ = false;
    CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 0, false);

    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
            if (object && IsPreviewEnvironmentName(object->GetName())) {
                object->SetIsVisible(false);
            }
        }
    }

    if (dxCommon_) {
        const Vector4& sceneClearColor = LightManager::GetInstance()->GetSceneClearColor();
        dxCommon_->SetRenderClearColor(
            sceneClearColor.x,
            sceneClearColor.y,
            sceneClearColor.z,
            sceneClearColor.w);
    }

    // 復元をこの場で完了したため、次フレームに同じ終了処理を繰り返さない。
    wasEnabled_ = false;
}

void EffectPreviewStage::ReportToolState(
    ToolKind toolKind,
    const std::string& title,
    float currentTime,
    float duration,
    bool isPlaying,
    int activeCount,
    const std::vector<TimelineEvent>& events,
    const PerformanceMetrics& performance) {
    const bool changedTool = activeToolKind_ != toolKind || activeToolTitle_ != title;
    if (changedTool) {
        ResetPerformanceMeasurement(true);
    }
    activeToolKind_ = toolKind;
    activeToolTitle_ = title.empty() ? GetToolKindName(toolKind) : title;
    reportedCurrentTime_ = (std::max)(0.0f, currentTime);
    reportedDuration_ = (std::max)(0.05f, duration);
    reportedPlaying_ = isPlaying;
    reportedActiveCount_ = (std::max)(0, activeCount);
    reportedEvents_ = events;
    reportedPerformance_ = performance;
    UpdatePerformanceMeasurement(performance);
    if (changedTool) {
        lastDispatchedSeekTarget_ = -1.0f;
        lastSeekDispatchTimestamp_ = -1.0;
    }
    if (changedTool || !timelineScrubbing_) {
        timelineScrubTime_ = std::clamp(reportedCurrentTime_, 0.0f, reportedDuration_);
    }
}

void EffectPreviewStage::UpdatePerformanceMeasurement(const PerformanceMetrics& performance) {
    if (!performanceMeasurementRunning_ ||
        !performance.available ||
        performanceMeasurementTool_ != activeToolKind_) {
        return;
    }

    if (performanceWarmupProgress_ < performanceWarmupFrames_) {
        ++performanceWarmupProgress_;
        return;
    }

    performanceCpuTimeAccumMs_ += performance.cpuTimeMs;
    performanceGpuTimeAccumMs_ += performance.gpuTimeMs;
    performanceParticleCountAccum_ += static_cast<double>(performance.particleCount);
    if (performance.frameDeltaSeconds > 0.0f) {
        performanceElapsedSeconds_ += performance.frameDeltaSeconds;
    }
    ++performanceSampleProgress_;

    if (performanceSampleProgress_ < performanceSampleFrames_) {
        return;
    }

    const double sampleCount = static_cast<double>(performanceSampleProgress_);
    measuredPerformance_.available = true;
    measuredPerformance_.particleCount = static_cast<int>(
        std::lround(performanceParticleCountAccum_ / sampleCount));
    measuredPerformance_.cpuTimeMs = static_cast<float>(performanceCpuTimeAccumMs_ / sampleCount);
    measuredPerformance_.gpuTimeMs = static_cast<float>(performanceGpuTimeAccumMs_ / sampleCount);
    measuredPerformance_.fps = performanceElapsedSeconds_ > 0.0 ?
        static_cast<float>(sampleCount / performanceElapsedSeconds_) : 0.0f;
    measuredPerformance_.memoryBytes = performance.memoryBytes;
    performanceMeasurementRunning_ = false;
    performanceMeasurementValid_ = true;
}

void EffectPreviewStage::ResetPerformanceMeasurement(bool clearResult) {
    performanceMeasurementRunning_ = false;
    performanceMeasurementTool_ = ToolKind::None;
    performanceWarmupProgress_ = 0;
    performanceSampleProgress_ = 0;
    performanceCpuTimeAccumMs_ = 0.0;
    performanceGpuTimeAccumMs_ = 0.0;
    performanceElapsedSeconds_ = 0.0;
    performanceParticleCountAccum_ = 0.0;
    if (clearResult) {
        performanceMeasurementValid_ = false;
        measuredPerformance_ = {};
    }
}

void EffectPreviewStage::DrawTimelineWindow() {
#ifdef USE_IMGUI
    constexpr const char* kWindowName = "エフェクトプレビュー - Timeline";
    ImGui::Begin(kWindowName, nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::Text(ICON_FA_MAGIC " %s", activeToolTitle_.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", GetToolKindName(activeToolKind_));

    if (enabled_) {
        ImGui::TextColored(
            ImVec4(0.42f, 0.90f, 0.68f, 1.0f),
            "隔離プレビュー空間を表示中");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UNDO " 元のSceneへ戻る")) {
            ReturnToScene();
        }
    } else {
        ImGui::TextColored(
            ImVec4(1.0f, 0.78f, 0.32f, 1.0f),
            "通常Sceneへ復帰済み");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLAY " プレビュー空間へ入る")) {
            EnableForToolPreview();
        }
    }

    if (ImGui::Button(ICON_FA_PLAY " 先頭から")) {
        transportPlaying_ = true;
        timelineScrubTime_ = 0.0f;
        ++playRequestSerial_;
    }
    ImGui::SameLine();
    if (transportPlaying_) {
        if (ImGui::Button(ICON_FA_PAUSE " 一時停止")) {
            transportPlaying_ = false;
        }
    } else if (ImGui::Button(ICON_FA_PLAY " 再開")) {
        transportPlaying_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " 停止")) {
        transportPlaying_ = false;
        timelineScrubTime_ = 0.0f;
        seekTargetTime_ = 0.0f;
        ++stopRequestSerial_;
    }

    const auto requestStep = [this](float seconds) {
        transportPlaying_ = false;
        seekTargetTime_ = std::clamp(reportedCurrentTime_ + seconds, 0.0f, reportedDuration_);
        timelineScrubTime_ = seekTargetTime_;
        ++seekRequestSerial_;
    };
    ImGui::SameLine();
    if (ImGui::SmallButton("-10F")) requestStep(-10.0f / 60.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("-1F")) requestStep(-1.0f / 60.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("+1F")) requestStep(1.0f / 60.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("+10F")) requestStep(10.0f / 60.0f);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CROSSHAIRS " プレビュー中心へ")) {
        RequestCameraRecenter();
    }

    ImGui::SetNextItemWidth(150.0f);
    ImGui::DragFloat("再生速度", &playbackSpeed_, 0.05f, 0.05f, 4.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::Checkbox("ループ", &loopPreview_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    ImGui::DragFloat("表示倍率", &timelinePixelsPerSecond_, 5.0f, 80.0f, 600.0f, "%.0f px/s");

    if (ImGui::BeginTable("EffectTimelineSummary", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("時間 %.2f / %.2f s", reportedCurrentTime_, reportedDuration_);
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("状態 %s", reportedPlaying_ ? "再生中" : (transportPlaying_ ? "待機" : "停止中"));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("Active %d", reportedActiveCount_);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("イベント %d", static_cast<int>(reportedEvents_.size()));
        ImGui::EndTable();
    }

    const auto dispatchSeek = [this](float targetTime, bool force) {
        const float clampedTime = std::clamp(targetTime, 0.0f, reportedDuration_);
        const double now = ImGui::GetTime();
        const float minimumChange = (std::max)(1.0f / 240.0f, reportedDuration_ / 1200.0f);
        const bool changedEnough =
            lastDispatchedSeekTarget_ < 0.0f ||
            std::abs(clampedTime - lastDispatchedSeekTarget_) >= minimumChange;
        const bool intervalElapsed =
            lastSeekDispatchTimestamp_ < 0.0 ||
            now - lastSeekDispatchTimestamp_ >= (1.0 / 30.0);
        if (!force && (!changedEnough || !intervalElapsed)) {
            return;
        }

        transportPlaying_ = false;
        seekTargetTime_ = clampedTime;
        timelineScrubTime_ = clampedTime;
        lastDispatchedSeekTarget_ = clampedTime;
        lastSeekDispatchTimestamp_ = now;
        ++seekRequestSerial_;
    };

    if (ImGui::SliderFloat("再生位置", &timelineScrubTime_, 0.0f, reportedDuration_, "%.3f s")) {
        timelineScrubTime_ = std::clamp(timelineScrubTime_, 0.0f, reportedDuration_);
        timelineScrubbing_ = true;
        dispatchSeek(timelineScrubTime_, false);
    }
    if (ImGui::IsItemActivated()) {
        timelineScrubbing_ = true;
    }
    if (ImGui::IsItemDeactivated()) {
        timelineScrubbing_ = false;
        dispatchSeek(timelineScrubTime_, true);
    }

    constexpr float kLabelWidth = 132.0f;
    constexpr float kHeaderHeight = 24.0f;
    constexpr float kLaneHeight = 28.0f;
    const int laneCount = (std::max)(1, static_cast<int>(reportedEvents_.size()));
    const float contentWidth = (std::max)(
        ImGui::GetContentRegionAvail().x,
        kLabelWidth + reportedDuration_ * timelinePixelsPerSecond_ + 24.0f);
    const float canvasHeight = kHeaderHeight + laneCount * kLaneHeight + 10.0f;

    ImGui::BeginChild("##EffectTimelineScroll", ImVec2(0.0f, canvasHeight + 18.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("##EffectTimelineCanvas", ImVec2(contentWidth, canvasHeight));
    drawList->AddRectFilled(origin, ImVec2(origin.x + contentWidth, origin.y + canvasHeight), IM_COL32(31, 35, 42, 255));
    drawList->AddLine(
        ImVec2(origin.x + kLabelWidth, origin.y),
        ImVec2(origin.x + kLabelWidth, origin.y + canvasHeight),
        IM_COL32(120, 128, 142, 220));

    const int wholeSeconds = static_cast<int>(std::ceil(reportedDuration_));
    for (int second = 0; second <= wholeSeconds; ++second) {
        const float x = origin.x + kLabelWidth + static_cast<float>(second) * timelinePixelsPerSecond_;
        drawList->AddLine(
            ImVec2(x, origin.y + kHeaderHeight),
            ImVec2(x, origin.y + canvasHeight),
            IM_COL32(88, 96, 108, 150));
        char timeLabel[24];
        sprintf_s(timeLabel, "%ds", second);
        drawList->AddText(ImVec2(x + 4.0f, origin.y + 4.0f), IM_COL32(210, 216, 226, 255), timeLabel);
    }

    if (reportedEvents_.empty()) {
        const float laneY = origin.y + kHeaderHeight;
        drawList->AddText(ImVec2(origin.x + 8.0f, laneY + 7.0f), IM_COL32(220, 224, 232, 255), "Playback");
        drawList->AddRectFilled(
            ImVec2(origin.x + kLabelWidth, laneY + 5.0f),
            ImVec2(origin.x + kLabelWidth + reportedDuration_ * timelinePixelsPerSecond_, laneY + kLaneHeight - 5.0f),
            IM_COL32(70, 150, 205, 190),
            4.0f);
    } else {
        for (int index = 0; index < static_cast<int>(reportedEvents_.size()); ++index) {
            const TimelineEvent& event = reportedEvents_[index];
            const float laneY = origin.y + kHeaderHeight + static_cast<float>(index) * kLaneHeight;
            const float startX = origin.x + kLabelWidth + std::clamp(event.startTime, 0.0f, reportedDuration_) * timelinePixelsPerSecond_;
            const float endX = origin.x + kLabelWidth + std::clamp((std::max)(event.endTime, event.startTime + 0.02f), 0.0f, reportedDuration_) * timelinePixelsPerSecond_;
            drawList->AddText(ImVec2(origin.x + 8.0f, laneY + 7.0f), IM_COL32(220, 224, 232, 255), event.label.c_str());
            drawList->AddRectFilled(
                ImVec2(startX, laneY + 5.0f),
                ImVec2((std::max)(startX + 5.0f, endX), laneY + kLaneHeight - 5.0f),
                ToImU32(event.color),
                4.0f);
        }
    }

    const float playheadX = origin.x + kLabelWidth + std::clamp(reportedCurrentTime_, 0.0f, reportedDuration_) * timelinePixelsPerSecond_;
    drawList->AddLine(
        ImVec2(playheadX, origin.y),
        ImVec2(playheadX, origin.y + canvasHeight),
        IM_COL32(255, 88, 96, 255),
        2.0f);

    const bool timelinePointerActive =
        ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if ((ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || timelinePointerActive) {
        const float mouseX = ImGui::GetIO().MousePos.x;
        timelineScrubbing_ = true;
        dispatchSeek((mouseX - origin.x - kLabelWidth) / timelinePixelsPerSecond_, false);
    }
    if (ImGui::IsItemDeactivated() && timelineScrubbing_) {
        timelineScrubbing_ = false;
        dispatchSeek(timelineScrubTime_, true);
    }
    ImGui::EndChild();

    if (reportedPerformance_.available) {
        ImGui::SeparatorText(ICON_FA_CHART_BAR " Performance Measurement");
        ImGui::TextDisabled(
            "選択中の%sだけを計測します。CPU/GPUの自動比較や設定変更は行いません。",
            activeToolKind_ == ToolKind::CpuParticle ? "通常Particle" : "GPU Particle");

        const float liveFps = reportedPerformance_.frameDeltaSeconds > 0.0f ?
            1.0f / reportedPerformance_.frameDeltaSeconds : 0.0f;
        if (ImGui::BeginTable(
            "EffectPerformanceLive", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("表示");
            ImGui::TableSetupColumn("粒子数");
            ImGui::TableSetupColumn("CPU時間");
            ImGui::TableSetupColumn("GPU時間");
            ImGui::TableSetupColumn("FPS");
            ImGui::TableSetupColumn("使用メモリ");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted("現在値");
            ImGui::TableNextColumn(); ImGui::Text("%d", reportedPerformance_.particleCount);
            ImGui::TableNextColumn(); ImGui::Text("%.3f ms", reportedPerformance_.cpuTimeMs);
            ImGui::TableNextColumn(); ImGui::Text("%.3f ms", reportedPerformance_.gpuTimeMs);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", liveFps);
            ImGui::TableNextColumn(); ImGui::Text(
                "%.2f MB",
                static_cast<double>(reportedPerformance_.memoryBytes) / (1024.0 * 1024.0));
            ImGui::EndTable();
        }

        ImGui::BeginDisabled(performanceMeasurementRunning_);
        ImGui::SetNextItemWidth(150.0f);
        ImGui::DragInt("ウォームアップ", &performanceWarmupFrames_, 1.0f, 1, 300, "%d frames");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::DragInt("平均計測", &performanceSampleFrames_, 1.0f, 30, 600, "%d frames");
        performanceWarmupFrames_ = std::clamp(performanceWarmupFrames_, 1, 300);
        performanceSampleFrames_ = std::clamp(performanceSampleFrames_, 30, 600);
        if (ImGui::Button(ICON_FA_STOPWATCH " このParticleを計測", ImVec2(180.0f, 0.0f))) {
            ResetPerformanceMeasurement(true);
            performanceMeasurementRunning_ = true;
            performanceMeasurementTool_ = activeToolKind_;
        }
        ImGui::EndDisabled();

        if (performanceMeasurementRunning_) {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_STOP " 中止")) {
                ResetPerformanceMeasurement(false);
            }
            const int completedFrames = performanceWarmupProgress_ + performanceSampleProgress_;
            const int totalFrames = performanceWarmupFrames_ + performanceSampleFrames_;
            const float progress = totalFrames > 0 ?
                static_cast<float>(completedFrames) / static_cast<float>(totalFrames) : 0.0f;
            const char* phase = performanceWarmupProgress_ < performanceWarmupFrames_ ?
                "ウォームアップ中" : "平均計測中";
            ImGui::ProgressBar(
                std::clamp(progress, 0.0f, 1.0f),
                ImVec2(-1.0f, 0.0f),
                phase);
        }

        if (performanceMeasurementValid_) {
            if (ImGui::BeginTable(
                "EffectPerformanceMeasured", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableSetupColumn("計測結果");
                ImGui::TableSetupColumn("粒子数");
                ImGui::TableSetupColumn("CPU時間");
                ImGui::TableSetupColumn("GPU時間");
                ImGui::TableSetupColumn("FPS");
                ImGui::TableSetupColumn("使用メモリ");
                ImGui::TableHeadersRow();
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%dF平均", performanceSampleFrames_);
                ImGui::TableNextColumn(); ImGui::Text("%d", measuredPerformance_.particleCount);
                ImGui::TableNextColumn(); ImGui::Text("%.3f ms", measuredPerformance_.cpuTimeMs);
                ImGui::TableNextColumn(); ImGui::Text("%.3f ms", measuredPerformance_.gpuTimeMs);
                ImGui::TableNextColumn(); ImGui::Text("%.1f", measuredPerformance_.fps);
                ImGui::TableNextColumn(); ImGui::Text(
                    "%.2f MB",
                    static_cast<double>(measuredPerformance_.memoryBytes) / (1024.0 * 1024.0));
                ImGui::EndTable();
            }

            const char* methodLabel = activeToolKind_ == ToolKind::CpuParticle ? "CPU更新" : "GPU更新";
            const auto buildResultText = [&](bool markdown) {
                std::ostringstream stream;
                stream << std::fixed;
                if (markdown) {
                    stream << "| 方式 | 粒子数 | CPU時間 | GPU時間 | FPS | 使用メモリ |\n"
                        << "|---|---:|---:|---:|---:|---:|\n"
                        << "| " << methodLabel << " | " << measuredPerformance_.particleCount
                        << " | " << std::setprecision(3) << measuredPerformance_.cpuTimeMs << "ms"
                        << " | " << measuredPerformance_.gpuTimeMs << "ms"
                        << " | " << std::setprecision(1) << measuredPerformance_.fps
                        << " | " << std::setprecision(2)
                        << static_cast<double>(measuredPerformance_.memoryBytes) / (1024.0 * 1024.0)
                        << "MB |\n";
                }
                else {
                    stream << "方式\t粒子数\tCPU時間\tGPU時間\tFPS\t使用メモリ\n"
                        << methodLabel << '\t' << measuredPerformance_.particleCount << '\t'
                        << std::setprecision(3) << measuredPerformance_.cpuTimeMs << "ms\t"
                        << measuredPerformance_.gpuTimeMs << "ms\t"
                        << std::setprecision(1) << measuredPerformance_.fps << '\t'
                        << std::setprecision(2)
                        << static_cast<double>(measuredPerformance_.memoryBytes) / (1024.0 * 1024.0)
                        << "MB\n";
                }
                return stream.str();
            };

            if (ImGui::Button("Markdown行をコピー")) {
                ImGui::SetClipboardText(buildResultText(true).c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("PowerPoint用TSVをコピー")) {
                ImGui::SetClipboardText(buildResultText(false).c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TRASH_ALT " 結果を消去")) {
                ResetPerformanceMeasurement(true);
            }
        }
    }

    ImGui::End();
#endif
}

void EffectPreviewStage::Update() {
    CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 0, enabled_ && isolatedSpace_);

    if (enabled_ && !wasEnabled_) {
        CaptureCameraState();
        hasPlacedCamera_ = false;
    }
    if (!enabled_ && wasEnabled_) {
        RestoreStudioLighting();
        RestoreCameraState();
        hasPlacedCamera_ = false;
        recenterCameraRequested_ = false;
        const Vector4& sceneClearColor = LightManager::GetInstance()->GetSceneClearColor();
        dxCommon_->SetRenderClearColor(sceneClearColor.x, sceneClearColor.y, sceneClearColor.z, sceneClearColor.w);
    }
    wasEnabled_ = enabled_;

    if (enabled_) {
        ApplyBackgroundColor();
        if (studioLighting_) {
            ApplyStudioLighting();
        } else {
            RestoreStudioLighting();
        }
    }

    Object3d* floor = FindFloor();
    Vector3 previewPos = GetGroundPosition();
    if (enabled_ && (showFloor_ || showGrid_ || showAxes_)) {
        if (!floor) {
            CreateFloor();
            floor = FindFloor();
        }

        stageRadius_ = (std::max)(stageRadius_, gridSpacing_ * static_cast<float>(kGridCenterIndex) + 2.0f);
        floorSize_ = stageRadius_ * 2.0f;
        const float half = stageRadius_;
        const float floorY = previewPos.y - 0.08f;
        const float gridY = previewPos.y + 0.01f;

        if (showFloor_) {
            UpdateEnvironmentObject(kFloorName, { previewPos.x, floorY, previewPos.z }, { half, 0.05f, half }, floorColor_);
        } else {
            SetEnvironmentObjectVisible(kFloorName, false);
        }

        for (int i = 0; i < kGridLineCount; ++i) {
            if (!showGrid_ && !(showAxes_ && i == kGridCenterIndex)) {
                SetEnvironmentObjectVisible(MakeGridXName(i), false);
                SetEnvironmentObjectVisible(MakeGridZName(i), false);
                continue;
            }
            const float offset = (static_cast<float>(i) - static_cast<float>(kGridCenterIndex)) * gridSpacing_;
            const bool isCenter = i == kGridCenterIndex;
            const bool isMajor = (i - kGridCenterIndex) % 5 == 0;
            const Vector4 xLineColor = isCenter && showAxes_ ? xAxisColor_ : (isMajor ? majorGridColor_ : minorGridColor_);
            const Vector4 zLineColor = isCenter && showAxes_ ? zAxisColor_ : (isMajor ? majorGridColor_ : minorGridColor_);
            const float lineWidth = isCenter && showAxes_ ? 0.040f : (isMajor ? 0.025f : 0.014f);
            UpdateEnvironmentObject(MakeGridXName(i), { previewPos.x, gridY, previewPos.z + offset }, { half, 0.010f, lineWidth }, xLineColor);
            UpdateEnvironmentObject(MakeGridZName(i), { previewPos.x + offset, gridY + 0.006f, previewPos.z }, { lineWidth, 0.010f, half }, zLineColor);
        }

        for (int i = -kOuterMajorHalfCount; i <= kOuterMajorHalfCount; ++i) {
            const int nameIndex = i + kOuterMajorHalfCount;
            const float offset = static_cast<float>(i) * 5.0f * gridSpacing_;
            const bool overlapsFineGrid = std::abs(offset) <= gridSpacing_ * static_cast<float>(kGridCenterIndex) + 0.001f;
            const bool withinStage = std::abs(offset) <= stageRadius_ + 0.001f;
            if (!showGrid_ || overlapsFineGrid || !withinStage) {
                SetEnvironmentObjectVisible(MakeOuterMajorXName(nameIndex), false);
                SetEnvironmentObjectVisible(MakeOuterMajorZName(nameIndex), false);
                continue;
            }
            UpdateEnvironmentObject(MakeOuterMajorXName(nameIndex), { previewPos.x, gridY, previewPos.z + offset }, { half, 0.010f, 0.025f }, majorGridColor_);
            UpdateEnvironmentObject(MakeOuterMajorZName(nameIndex), { previewPos.x + offset, gridY + 0.006f, previewPos.z }, { 0.025f, 0.010f, half }, majorGridColor_);
        }

        if (showAxes_) {
            UpdateEnvironmentObject(kYAxisName, { previewPos.x, previewPos.y + 1.5f, previewPos.z }, { 0.018f, 1.5f, 0.018f }, yAxisColor_);
            UpdateEnvironmentObject(kOriginMarkerName, { previewPos.x, previewPos.y + 0.035f, previewPos.z }, { 0.07f, 0.07f, 0.07f }, { 0.92f, 0.92f, 0.95f, 1.0f });
        } else {
            SetEnvironmentObjectVisible(kYAxisName, false);
            SetEnvironmentObjectVisible(kOriginMarkerName, false);
        }

        // 旧プレビュー箱の壁とレールは互換Objectとして残っていても非表示にします。
        SetEnvironmentObjectVisible(kLegacyBackWallName, false);
        SetEnvironmentObjectVisible(kLegacyLeftRailName, false);
        SetEnvironmentObjectVisible(kLegacyRightRailName, false);
    }
    else if (floor) {
        for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
            if (object && IsPreviewEnvironmentName(object->GetName())) {
                object->SetIsVisible(false);
            }
        }
    }
}

void EffectPreviewStage::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_MAGIC " Effect Preview Stage");
    ImGui::Separator();

    if (enabled_) {
        if (ImGui::Button(ICON_FA_UNDO " 元のSceneへ戻る")) {
            ReturnToScene();
        }
    } else if (ImGui::Button(ICON_FA_PLAY " プレビュー空間へ入る")) {
        EnableForToolPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLAY " 1回再生")) {
        ++playRequestSerial_;
    }

    ImGui::Checkbox("ループ再生", &loopPreview_);
    ImGui::Checkbox("隔離空間で確認", &isolatedSpace_);
    ImGui::Checkbox("有効化時にカメラを移動", &moveCameraOnEnter_);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CROSSHAIRS " カメラをプレビューへ移動")) {
        RequestCameraRecenter();
    }
    ImGui::DragFloat("再生速度", &playbackSpeed_, 0.05f, 0.0f, 5.0f);
    ImGui::DragFloat3(isolatedSpace_ ? "隔離空間内の床原点" : "プレビュー床原点", &origin_.x, 0.1f);
    if (isolatedSpace_) {
        ImGui::DragFloat3("隔離空間ベース", &isolatedBase_.x, 100.0f);
    }
    ImGui::DragFloat("プレビュー中心高さ", &previewCenterHeight_, 0.05f, 0.0f, 10.0f, "%.2f m");
    ImGui::TextDisabled("中心原点のEffectは標準1m。接地型Effectは0mへ下げて確認できます。");
    if (moveCameraOnEnter_) {
        ImGui::DragFloat("カメラ距離", &cameraDistance_, 0.1f, 2.0f, 80.0f);
        ImGui::DragFloat("カメラ高さ", &cameraHeight_, 0.1f, -20.0f, 40.0f);
        ImGui::DragFloat("カメラ方位角", &cameraAzimuthDegrees_, 0.5f, -180.0f, 180.0f, "%.1f deg");
        ImGui::DragFloat("注視点の高さ", &cameraTargetHeight_, 0.05f, 0.0f, 20.0f);
    }

    ImGui::SeparatorText("Studio Viewport");
    const char* presetNames[] = { "Studio Dark", "Studio Light", "Emission Black" };
    int selectedPreset = studioPresetIndex_;
    if (ImGui::Combo("表示プリセット", &selectedPreset, presetNames, IM_ARRAYSIZE(presetNames))) {
        ApplyStudioPreset(selectedPreset);
    }
    ImGui::TextDisabled("Dark:標準 / Light:黒煙・暗色 / Black:発光・Bloom・Alpha確認");

    ImGui::Checkbox("床", &showFloor_);
    ImGui::SameLine();
    ImGui::Checkbox("1mグリッド", &showGrid_);
    ImGui::SameLine();
    ImGui::Checkbox("XYZ軸", &showAxes_);
    if (showFloor_ || showGrid_) {
        if (ImGui::DragFloat("グリッド間隔", &gridSpacing_, 0.05f, 0.1f, 6.0f, "%.2f m")) {
            gridSpacing_ = std::clamp(gridSpacing_, 0.1f, 6.0f);
        }
        const float minimumRadius = gridSpacing_ * static_cast<float>(kGridCenterIndex) + 2.0f;
        if (ImGui::DragFloat("空間半径", &stageRadius_, 0.5f, minimumRadius, 80.0f, "%.1f m")) {
            stageRadius_ = std::clamp(stageRadius_, minimumRadius, 80.0f);
        }
        ImGui::TextDisabled("中央は細かいGrid、外周は5区画ごとのMajor Line。標準直径70mです。");
    }

    ImGui::Checkbox("Studio Lighting", &studioLighting_);
    if (studioLighting_ && ImGui::TreeNode("Studio Lighting設定")) {
        ImGui::ColorEdit3("Key Light色", &studioLightColor_.x);
        ImGui::DragFloat3("Key Light方向", &studioLightDirection_.x, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("Key Light強度", &studioLightIntensity_, 0.05f, 0.0f, 8.0f);
        ImGui::DragFloat("Ambient強度", &studioAmbientIntensity_, 0.02f, 0.0f, 2.0f);
        ImGui::TextDisabled("Preview中だけSceneのDirectional Light・Fog・Skyboxを一時的に置き換えます。");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("表示色の微調整")) {
        ImGui::ColorEdit4("背景色", &backgroundColor_.x);
        ImGui::ColorEdit4("床色", &floorColor_.x);
        ImGui::ColorEdit4("Minor Grid", &minorGridColor_.x);
        ImGui::ColorEdit4("Major Grid", &majorGridColor_.x);
        ImGui::TreePop();
    }

    if (ImGui::Button(ICON_FA_UNDO " Studio標準へ戻す", ImVec2(-1, 0))) {
        ApplyStudioPreset(0);
        showFloor_ = true;
        showGrid_ = true;
        showAxes_ = true;
        studioLighting_ = true;
        gridSpacing_ = 1.0f;
        stageRadius_ = 35.0f;
        previewCenterHeight_ = 1.0f;
        cameraDistance_ = 14.0f;
        cameraHeight_ = 5.5f;
        cameraAzimuthDegrees_ = 35.0f;
        cameraTargetHeight_ = 1.5f;
        recenterCameraRequested_ = true;
    }
    if (ImGui::Button(ICON_FA_TRASH_ALT " Preview環境を削除", ImVec2(-1, 0))) {
        RemoveFloor();
    }

    ImGui::Separator();
    ImGui::TextWrapped("BlenderのStudio Viewportに近い中立的な検証空間です。Mesh EffectとGPU Particleの発生位置を隔離し、床・グリッド・軸・背景・照明を揃えて比較します。Preview環境はScene保存対象から除外され、終了時にCameraとLightingを復元します。");
#endif
}

Vector3 EffectPreviewStage::GetGroundPosition() const {
    if (!isolatedSpace_) return origin_;
    return {
        isolatedBase_.x + origin_.x,
        isolatedBase_.y + origin_.y,
        isolatedBase_.z + origin_.z
    };
}

Vector3 EffectPreviewStage::GetPreviewPosition() const {
    Vector3 position = GetGroundPosition();
    position.y += previewCenterHeight_;
    return position;
}

void EffectPreviewStage::ApplyCameraOverride() {
    if (!enabled_) return;

    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);

    if ((moveCameraOnEnter_ && !hasPlacedCamera_) || recenterCameraRequested_) {
        PlaceCameraAtPreview();
        hasPlacedCamera_ = true;
        recenterCameraRequested_ = false;
    }
}

void EffectPreviewStage::PlaceCameraAtPreview() {
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return;

    Vector3 target = GetGroundPosition();
    target.y += cameraTargetHeight_;
    constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
    const float azimuth = cameraAzimuthDegrees_ * kDegreesToRadians;
    Vector3 eye = {
        target.x + std::sin(azimuth) * cameraDistance_,
        target.y + cameraHeight_,
        target.z - std::cos(azimuth) * cameraDistance_
    };

    float pitch = std::atan2(cameraHeight_, cameraDistance_);
    camera->SetFollowTarget(nullptr);
    camera->SetEye(eye);
    camera->SetTarget(target);
    camera->SetRotation({ pitch, -azimuth, 0.0f });
    camera->Update();
    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
}

void EffectPreviewStage::CreateFloor() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene->GetObject3dCommon()) return;

    ModelManager::GetInstance()->LoadModel("Primitives/cube");

    const bool wasMissing = FindFloor() == nullptr;
    CreateEnvironmentObject(kFloorName);
    for (int i = 0; i < kGridLineCount; ++i) {
        CreateEnvironmentObject(MakeGridXName(i));
        CreateEnvironmentObject(MakeGridZName(i));
    }
    for (int i = 0; i <= kOuterMajorHalfCount * 2; ++i) {
        CreateEnvironmentObject(MakeOuterMajorXName(i));
        CreateEnvironmentObject(MakeOuterMajorZName(i));
    }
    CreateEnvironmentObject(kYAxisName);
    CreateEnvironmentObject(kOriginMarkerName);
    if (wasMissing) {
        DebugConsole::GetInstance()->AddLog("Effect Preview Stage environment created.");
    }
}

void EffectPreviewStage::RemoveFloor() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    std::vector<Object3d*> targets;
    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && IsPreviewEnvironmentName(object->GetName())) {
            targets.push_back(object.get());
        }
    }

    for (Object3d* target : targets) {
        sceneManager_->GetCurrentScene()->RequestRemoveObject(target);
    }
    if (!targets.empty()) {
        DebugConsole::GetInstance()->AddLog("Effect Preview Stage environment removed.");
    }
}

Object3d* EffectPreviewStage::FindFloor() const {
    return FindEnvironmentObject(kFloorName);
}

Object3d* EffectPreviewStage::FindEnvironmentObject(const std::string& name) const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;

    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && object->GetName() == name) {
            return object.get();
        }
    }
    return nullptr;
}

void EffectPreviewStage::CreateEnvironmentObject(const std::string& name) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene->GetObject3dCommon() || FindEnvironmentObject(name)) return;

    auto object = std::make_unique<Object3d>();
    object->Initialize(scene->GetObject3dCommon());
    object->SetName(name);
    object->SetClassName("EditorOnly_EffectPreviewStage");
    object->SetSaveCategory("Object");
    object->SetEditorInternal(true);
    object->SetModel("Primitives/cube");
    object->SetIsLocked(true);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);
    object->SetMaterialType(0);
    object->SetBlendMode(BlendMode::kNormal);
    object->SetTexture("Resources/sprite/common/white.png");
    object->SetEnableLighting(false);
    object->SetCastShadow(false);
    object->SetMetallic(0.0f);
    object->SetRoughness(1.0f);
    object->SetEmissive(1.0f);
    object->SetColor(floorColor_);
    object->SetIsVisible(false);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
    scene->AddObject(std::move(object));
}

void EffectPreviewStage::UpdateEnvironmentObject(const std::string& name, const Vector3& translate, const Vector3& scale, const Vector4& color) {
    Object3d* object = FindEnvironmentObject(name);
    if (!object) return;

    object->SetEditorInternal(true);
    object->SetIsVisible(true);
    object->SetTranslate(translate);
    object->SetScale(scale);
    object->SetColor(color);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
}

void EffectPreviewStage::SetEnvironmentObjectVisible(const std::string& name, bool visible) {
    if (Object3d* object = FindEnvironmentObject(name)) {
        object->SetIsVisible(visible);
    }
}

void EffectPreviewStage::ApplyStudioPreset(int presetIndex) {
    studioPresetIndex_ = std::clamp(presetIndex, 0, 2);
    switch (studioPresetIndex_) {
    case 1:
        backgroundColor_ = { 0.42f, 0.45f, 0.50f, 1.0f };
        floorColor_ = { 0.54f, 0.56f, 0.60f, 1.0f };
        minorGridColor_ = { 0.35f, 0.37f, 0.41f, 1.0f };
        majorGridColor_ = { 0.25f, 0.27f, 0.31f, 1.0f };
        break;
    case 2:
        backgroundColor_ = { 0.005f, 0.006f, 0.008f, 1.0f };
        floorColor_ = { 0.025f, 0.028f, 0.035f, 1.0f };
        minorGridColor_ = { 0.08f, 0.09f, 0.11f, 1.0f };
        majorGridColor_ = { 0.16f, 0.18f, 0.22f, 1.0f };
        break;
    case 0:
    default:
        backgroundColor_ = { 0.095f, 0.105f, 0.125f, 1.0f };
        floorColor_ = { 0.20f, 0.215f, 0.24f, 1.0f };
        minorGridColor_ = { 0.30f, 0.32f, 0.35f, 1.0f };
        majorGridColor_ = { 0.46f, 0.49f, 0.54f, 1.0f };
        break;
    }
}

void EffectPreviewStage::ApplyStudioLighting() {
    LightManager* lightManager = LightManager::GetInstance();
    if (!lightManager) return;

    DirectionalLight& light = lightManager->GetDirectionalLight();
    if (!studioLightingApplied_) {
        storedDirectionalColor_ = light.color;
        storedDirectionalDirection_ = light.direction;
        storedDirectionalIntensity_ = light.intensity;
        storedAmbientColor_ = light.ambientColor;
        storedEnableFog_ = light.enableFog;
        storedVolumetricIntensity_ = light.volumetricIntensity;
        storedSkyboxEnabled_ = lightManager->IsSkyboxEnabled();
        hasCapturedLighting_ = true;
        studioLightingApplied_ = true;
    }

    const float directionLength = std::sqrt(
        studioLightDirection_.x * studioLightDirection_.x +
        studioLightDirection_.y * studioLightDirection_.y +
        studioLightDirection_.z * studioLightDirection_.z);
    if (directionLength > 0.0001f) {
        light.direction = {
            studioLightDirection_.x / directionLength,
            studioLightDirection_.y / directionLength,
            studioLightDirection_.z / directionLength
        };
    }
    light.color = studioLightColor_;
    light.intensity = studioLightIntensity_;
    light.ambientColor = {
        studioAmbientIntensity_ * 0.92f,
        studioAmbientIntensity_ * 0.96f,
        studioAmbientIntensity_
    };
    light.enableFog = 0;
    light.volumetricIntensity = 0.0f;
    lightManager->SetSkyboxEnabled(false);
}

void EffectPreviewStage::RestoreStudioLighting() {
    if (!studioLightingApplied_ || !hasCapturedLighting_) return;

    LightManager* lightManager = LightManager::GetInstance();
    if (lightManager) {
        DirectionalLight& light = lightManager->GetDirectionalLight();
        light.color = storedDirectionalColor_;
        light.direction = storedDirectionalDirection_;
        light.intensity = storedDirectionalIntensity_;
        light.ambientColor = storedAmbientColor_;
        light.enableFog = storedEnableFog_;
        light.volumetricIntensity = storedVolumetricIntensity_;
        lightManager->SetSkyboxEnabled(storedSkyboxEnabled_);
    }
    studioLightingApplied_ = false;
    hasCapturedLighting_ = false;
}

void EffectPreviewStage::ApplyBackgroundColor() {
    if (!dxCommon_) return;
    dxCommon_->SetRenderClearColor(backgroundColor_.x, backgroundColor_.y, backgroundColor_.z, backgroundColor_.w);
}

void EffectPreviewStage::CaptureCameraState() {
    if (hasCapturedCamera_) return;

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return;

    storedEye_ = camera->GetEye();
    storedTarget_ = camera->GetTargetPoint();
    storedRotation_ = camera->GetRotation();
    storedCameraMode_ = static_cast<int>(CameraEditor::GetInstance()->GetMode());
    hasCapturedCamera_ = true;
}

void EffectPreviewStage::RestoreCameraState() {
    if (!hasCapturedCamera_) return;

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->SetFollowTarget(nullptr);
        camera->SetEye(storedEye_);
        camera->SetTarget(storedTarget_);
        camera->SetRotation(storedRotation_);
        camera->Update();
    }

    CameraEditor::GetInstance()->SetMode(static_cast<CameraEditor::Mode>(storedCameraMode_));
    hasCapturedCamera_ = false;
}
