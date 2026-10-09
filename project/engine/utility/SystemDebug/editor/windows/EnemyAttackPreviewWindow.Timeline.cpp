#include "EnemyAttackPreviewWindow.h"
#include "EnemyAttackPreviewWindow.Constants.h"
#include "game/system/CrownFinaleTuning.h"
#include "game/system/CrownDefeatTuning.h"

#include "BaseEnemy.h"
#include "BaseScene.h"
#include "EffectPreviewStage.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"
#include "Object3d.h"
#include "Player.h"
#include "SceneManager.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

using EnemyAttackPreviewConstants::kCopyActions;
using EnemyAttackPreviewConstants::kCopyLabels;
using EnemyAttackPreviewConstants::kFixedPreviewStep;

namespace {
#ifdef USE_IMGUI
ImU32 GetPhaseColor(const std::string& phase, int alpha = 230) {
    if (phase == "予兆") return IM_COL32(255, 158, 52, alpha);
    if (phase == "攻撃") return IM_COL32(255, 74, 28, alpha);
    if (phase == "硬直") return IM_COL32(212, 126, 92, alpha);
    if (phase == "溜め") return IM_COL32(255, 188, 64, alpha);
    if (phase == "近距離放電・溜め" || phase == "連続落雷・溜め") return IM_COL32(247, 210, 73, alpha);
    if (phase == "連続落雷・落雷中") return IM_COL32(103, 218, 255, alpha);
    if (phase == "三連風弾・風で上昇" || phase == "三連風弾・頭上に形成") return IM_COL32(128, 245, 211, alpha);
    if (phase == "三連風弾・順番に投射") return IM_COL32(105, 218, 255, alpha);
    if (phase == "三連風弾・着地") return IM_COL32(164, 226, 207, alpha);
    if (phase == "放電後・反発") return IM_COL32(167, 126, 255, alpha);
    if (phase == "再使用待ち") return IM_COL32(84, 126, 158, alpha);
    if (phase == "上昇") return IM_COL32(112, 205, 255, alpha);
    if (phase == "突進") return IM_COL32(255, 92, 116, alpha);
    if (phase == "着地反発") return IM_COL32(171, 116, 255, alpha);
    if (phase == "待機・移動") return IM_COL32(104, 190, 153, alpha);
    if (phase == "空中拘束・落下") return IM_COL32(82, 172, 235, alpha);
    if (phase == "着地・受け止め") return IM_COL32(242, 185, 69, alpha);
    if (phase == "踏ん張り・抵抗") return IM_COL32(83, 188, 139, alpha);
    if (phase == "引きずられ") return IM_COL32(241, 132, 66, alpha);
    if (phase == "限界・分裂寸前") return IM_COL32(238, 76, 118, alpha);
    if (phase == "分裂完了" || phase == "分裂後") return IM_COL32(187, 105, 244, alpha);
    return IM_COL32(128, 164, 196, alpha);
}

void DrawTimelineBlock(
    ImDrawList* drawList,
    const ImVec2& origin,
    float labelWidth,
    float pixelsPerSecond,
    float laneY,
    float laneHeight,
    float beginTime,
    float endTime,
    ImU32 color,
    const char* label) {
    if (!drawList || endTime <= beginTime) {
        return;
    }

    const float x0 = origin.x + labelWidth + beginTime * pixelsPerSecond;
    const float x1 = origin.x + labelWidth + endTime * pixelsPerSecond;
    drawList->AddRectFilled(
        ImVec2(x0 + 1.0f, laneY + 3.0f),
        ImVec2(x1 - 1.0f, laneY + laneHeight - 3.0f),
        color,
        3.0f);
    if (label && x1 - x0 >= 48.0f) {
        drawList->PushClipRect(
            ImVec2(x0 + 3.0f, laneY),
            ImVec2(x1 - 3.0f, laneY + laneHeight),
            true);
        drawList->AddText(
            ImVec2(x0 + 6.0f, laneY + 6.0f),
            IM_COL32(245, 248, 252, 255),
            label);
        drawList->PopClipRect();
    }
}
#endif
}

void EnemyAttackPreviewWindow::DrawTimelineWindow() {
    // 設定上の攻撃時間と実際の AI フェーズを同じ時間軸に重ねて比較します。
#ifdef USE_IMGUI
    if (crownMovieMode_) { DrawCrownMovieTimeline(); return; }
    constexpr const char* kWindowName = "攻撃・能力プレビュー - Timeline";
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin(kWindowName, nullptr, windowFlags)) {
        ImGui::End();
        return;
    }

    if (!IsCurrentSceneReady()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.28f, 1.0f), "Object3dを配置できるSceneが必要です。");
        ImGui::End();
        return;
    }
    if (sceneManager_->IsPlaying()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.20f, 1.0f), "編集モードへ戻すと敵攻撃タイムラインを使用できます。");
        ImGui::End();
        return;
    }

    DrawTrackTimeline();
    if (placementPreview_) {
        ImGui::TextWrapped("配置モード: 選択部品を単独表示しています。攻撃全体の再生はInspectorの「保存せず動作確認」を使います。");
        DrawPlacementControls();
        ImGui::End();
        return;
    }
    if (!enabled_) {
        ImGui::TextDisabled("右のInspectorまたは下のボタンからプレビューを有効にしてください。");
        ImGui::SameLine();
        if (ImGui::Button("プレビューを開始")) {
            enabled_ = true;
            CreatePreviewObjects();
        }
        ImGui::End();
        return;
    }

    const bool timelineFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (timelineFocused && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            playing_ = !playing_;
        }
        const int stepFrames = ImGui::GetIO().KeyShift ? 10 : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
            playing_ = false;
            SeekPreview(elapsedTime_ - kFixedPreviewStep * static_cast<float>(stepFrames));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
            playing_ = false;
            SeekPreview(elapsedTime_ + kFixedPreviewStep * static_cast<float>(stepFrames));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            RestartPreview();
        }
    }

    ImGui::PushID("EnemyAttackTimelineToolbar");
    if (playerCopyMode_) ImGui::Text("プレイヤー / %s / %s", kCopyLabels[copyTypeIndex_], kCopyActions[copyActionIndex_]);
    if (ImGui::Button(playing_ ? "■ 一時停止" : "▶ 再生", ImVec2(108.0f, 0.0f))) {
        playing_ = !playing_;
    }
    ImGui::SameLine();
    if (ImGui::Button("|<")) RestartPreview();
    ImGui::SameLine();
    if (ImGui::Button("-10F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ - kFixedPreviewStep * 10.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("-1F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ - kFixedPreviewStep);
    }
    ImGui::SameLine();
    if (ImGui::Button("+1F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ + kFixedPreviewStep);
    }
    ImGui::SameLine();
    if (ImGui::Button("+10F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ + kFixedPreviewStep * 10.0f);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    ImGui::SliderFloat("速度", &playbackSpeed_, 0.05f, 2.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::Checkbox("ループ", &loop_);
    ImGui::SameLine();
    if (ImGui::Button("カメラを初期位置へ")) {
        EffectPreviewStage::GetInstance()->RequestCameraRecenter();
    }
    ImGui::PopID();

    const TimelineSample* currentSample = timelineSamples_.empty() ? nullptr : &timelineSamples_.back();
    if (ImGui::BeginTable("EnemyAttackTimelineSummary", 4, ImGuiTableFlags_SizingStretchSame)) {
        const char* labels[] = { "時間 / Frame", playerCopyMode_ ? "入力フェーズ" : "AIフェーズ", "速度", "接地からの高さ" };
        char values[4][64]{};
        sprintf_s(values[0], "%.3f / %.2f 秒  |  %dF", elapsedTime_, loopDuration_, static_cast<int>(std::round(elapsedTime_ / kFixedPreviewStep)));
        // 状態名は日本語や実行件数を含み64バイトを超えます。固定長バッファへコピーしません。
        sprintf_s(values[2], "%.2f m/s", currentSample ? currentSample->speed : 0.0f);
        sprintf_s(values[3], "%.2f m", currentSample ? currentSample->height : 0.0f);
        for (int column = 0; column < 4; ++column) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", labels[column]);
            if (column == 1) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(GetCurrentPhaseName());
                ImGui::PopTextWrapPos();
            } else ImGui::TextUnformatted(values[column]);
        }
        ImGui::EndTable();
    }

    if (const auto* prismSlime = dynamic_cast<const EnemyPrismSlime*>(previewEnemy_)) {
        const int chantLayers = prismSlime->GetDebugChantVisibleLayerCount();
        if (chantLayers == -2) {
            ImGui::TextDisabled("詠唱エフェクト: 待機中");
        } else if (chantLayers < 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.28f, 1.0f),
                "詠唱エフェクト: 生成失敗（Debug Console を確認）");
        } else {
            ImGui::Text("詠唱エフェクト: 表示対象 %d / 7 レイヤー", chantLayers);
        }
    }

    ImGui::SetNextItemWidth(170.0f);
    ImGui::SliderFloat("タイムライン拡大", &timelinePixelsPerSecond_, 60.0f, 280.0f, "%.0f px/s");
    ImGui::SameLine();
    ImGui::Checkbox("再生位置を追従", &timelineFollowPlayhead_);
    ImGui::SameLine();
    ImGui::TextDisabled("Space: 再生/停止  ←/→: 1F  Shift+←/→: 10F  Home: 先頭");

    constexpr float kLabelWidth = 118.0f;
    constexpr float kHeaderHeight = 28.0f;
    constexpr float kLaneHeight = 30.0f;
    constexpr int kLaneCount = 10;
    constexpr float kBottomPadding = 8.0f;
    const float timelineHeight = kHeaderHeight + kLaneHeight * static_cast<float>(kLaneCount) + kBottomPadding;
    const float duration = (std::max)(loopDuration_, kFixedPreviewStep);
    const float visibleWidth = (std::max)(ImGui::GetContentRegionAvail().x, 320.0f);
    const float contentWidth = (std::max)(visibleWidth, kLabelWidth + duration * timelinePixelsPerSecond_ + 32.0f);

    ImGui::BeginChild(
        "##EnemyAttackTimelineScroll",
        ImVec2(0.0f, timelineHeight + ImGui::GetStyle().ScrollbarSize + 6.0f),
        true,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##EnemyAttackTimelineCanvas", ImVec2(contentWidth, timelineHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 canvasEnd(origin.x + contentWidth, origin.y + timelineHeight);

    drawList->AddRectFilled(origin, canvasEnd, IM_COL32(22, 25, 31, 255), 4.0f);
    drawList->AddRectFilled(origin, ImVec2(origin.x + kLabelWidth, canvasEnd.y), IM_COL32(34, 38, 47, 255), 4.0f);
    drawList->AddLine(
        ImVec2(origin.x + kLabelWidth, origin.y),
        ImVec2(origin.x + kLabelWidth, canvasEnd.y),
        IM_COL32(105, 115, 135, 255));

    const bool giantHookPreview = IsGiantHookSplitPreview();
    const char* laneNames[kLaneCount] = {
        giantHookPreview ? "専用モーション" : "設定タイミング",
        "実測AIフェーズ",
        "移動速度",
        "ジャンプ高さ",
        "変形量",
        "被弾リアクション",
        "GPU Particle系統",
        "通常Particle数",
        "Mesh Effect数",
        "Debris数"
    };
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const float y0 = origin.y + kHeaderHeight + kLaneHeight * static_cast<float>(lane);
        const float y1 = y0 + kLaneHeight;
        if ((lane % 2) == 0) {
            drawList->AddRectFilled(ImVec2(origin.x, y0), ImVec2(canvasEnd.x, y1), IM_COL32(255, 255, 255, 6));
        }
        drawList->AddLine(ImVec2(origin.x, y1), ImVec2(canvasEnd.x, y1), IM_COL32(62, 68, 80, 190));
        drawList->AddText(ImVec2(origin.x + 8.0f, y0 + 7.0f), IM_COL32(205, 213, 226, 255), laneNames[lane]);
    }

    float tickStep = 1.0f;
    if (timelinePixelsPerSecond_ >= 220.0f) tickStep = 0.25f;
    else if (timelinePixelsPerSecond_ >= 110.0f) tickStep = 0.5f;
    const int tickCount = static_cast<int>(std::ceil(duration / tickStep));
    for (int tick = 0; tick <= tickCount; ++tick) {
        const float time = static_cast<float>(tick) * tickStep;
        const float x = origin.x + kLabelWidth + time * timelinePixelsPerSecond_;
        const bool major = std::fmod(time + 0.001f, 1.0f) < 0.01f;
        drawList->AddLine(
            ImVec2(x, origin.y + (major ? 0.0f : 14.0f)),
            ImVec2(x, canvasEnd.y),
            major ? IM_COL32(80, 88, 105, 220) : IM_COL32(60, 66, 78, 130));
        if (major) {
            char label[24]{};
            sprintf_s(label, "%.0fs", time);
            drawList->AddText(ImVec2(x + 4.0f, origin.y + 5.0f), IM_COL32(185, 194, 210, 255), label);
        }
    }

    const EnemyAttackDefinition* sequenceAttack = GetSelectedAttackDefinition();
    if (sequenceAttack && sequenceAttack->sequence.enabled) {
        const auto& sequence = sequenceAttack->sequence;
        const float laneY = origin.y + kHeaderHeight;
        // 設定イベントはランタイム開始からの相対時刻。Prismは詠唱完了が原点で、AI待機は含みません。
        DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight,
            0, (std::min)(duration, sequence.duration), IM_COL32(50, 100, 125, 160), "共通命令 (命令開始=0)");
        if (sequence.actorControl.enabled) {
            DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight,
                0, sequence.actorControl.telegraphEnd, IM_COL32(170, 140, 65, 230), "予備動作");
            DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight,
                sequence.actorControl.telegraphEnd, sequence.duration, IM_COL32(100, 120, 170, 230), "発射後・後隙");
        }
        for (const auto& event : sequence.events) {
            const float x = origin.x + kLabelWidth + event.time * timelinePixelsPerSecond_;
            drawList->AddLine(ImVec2(x, laneY), ImVec2(x, laneY + kLaneHeight), IM_COL32(255, 200, 60, 255), 2);
            if (ImGui::IsMouseHoveringRect(ImVec2(x - 5, laneY), ImVec2(x + 5, laneY + kLaneHeight))) {
                ImGui::BeginTooltip();
                ImGui::Text("%.3f秒: %s / %s", event.time, event.operation.c_str(), event.condition.c_str());
                ImGui::TextUnformatted(event.stopOnFalse ? "条件不成立: 後続命令も中止" : "条件不成立: スキップ");
                ImGui::EndTooltip();
            }
        }
    } else if (giantHookPreview) {
        const float laneY = origin.y + kHeaderHeight;
        DrawTimelineBlock(
            drawList,
            origin,
            kLabelWidth,
            timelinePixelsPerSecond_,
            laneY,
            kLaneHeight,
            0.0f,
            duration,
            IM_COL32(70, 105, 132, 190),
            giantPreviewModeIndex_ == 2
                ? "空中拘束 → 着地 → 抵抗 → 分裂"
                : "踏ん張り → 引きずられ → 分裂");
    } else if (const EnemyAttackDefinition* attack = GetSelectedAttackDefinition()) {
        const float laneY = origin.y + kHeaderHeight;
        float cursorTime = 0.0f;
        const float windupEnd = (std::min)(duration, cursorTime + attack->windupDuration);
        DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight, cursorTime, windupEnd, IM_COL32(214, 155, 51, 210), "予備動作");
        cursorTime = windupEnd;
        const float activeEnd = (std::min)(duration, cursorTime + attack->activeDuration);
        DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight, cursorTime, activeEnd, IM_COL32(223, 72, 92, 220), "攻撃");
        cursorTime = activeEnd;
        const float recoveryEnd = (std::min)(duration, cursorTime + attack->recoveryDuration);
        DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight, cursorTime, recoveryEnd, IM_COL32(126, 91, 205, 215), "硬直");
        cursorTime = recoveryEnd;
        const float cooldownEnd = (std::min)(duration, cursorTime + attack->cooldown);
        DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight, cursorTime, cooldownEnd, IM_COL32(75, 120, 145, 190), "再使用待ち");
    }

    if (!timelineSamples_.empty()) {
        const float phaseLaneY = origin.y + kHeaderHeight + kLaneHeight;
        std::size_t segmentBegin = 0;
        for (std::size_t index = 1; index <= timelineSamples_.size(); ++index) {
            const bool segmentEnds = index == timelineSamples_.size() || timelineSamples_[index].phase != timelineSamples_[segmentBegin].phase;
            if (!segmentEnds) {
                continue;
            }
            const float startTime = std::clamp(timelineSamples_[segmentBegin].time, 0.0f, duration);
            const float rawEndTime = index < timelineSamples_.size()
                ? timelineSamples_[index].time
                : (std::max)(elapsedTime_, startTime + kFixedPreviewStep);
            const float endTime = std::clamp(rawEndTime, 0.0f, duration);
            DrawTimelineBlock(
                drawList,
                origin,
                kLabelWidth,
                timelinePixelsPerSecond_,
                phaseLaneY,
                kLaneHeight,
                startTime,
                endTime,
                GetPhaseColor(timelineSamples_[segmentBegin].phase),
                timelineSamples_[segmentBegin].phase.c_str());
            segmentBegin = index;
        }

        float maxSpeed = 1.0f;
        float maxHeight = 1.0f;
        float maxDeformation = 0.25f;
        float maxGpuParticleSystems = 1.0f;
        float maxCpuParticles = 1.0f;
        float maxMeshEffects = 1.0f;
        float maxDebrisPieces = 1.0f;
        for (const TimelineSample& sample : timelineSamples_) {
            maxSpeed = (std::max)(maxSpeed, sample.speed);
            maxHeight = (std::max)(maxHeight, sample.height);
            maxDeformation = (std::max)(maxDeformation, sample.deformation);
            maxGpuParticleSystems = (std::max)(maxGpuParticleSystems, sample.gpuParticleSystems);
            maxCpuParticles = (std::max)(maxCpuParticles, sample.cpuParticles);
            maxMeshEffects = (std::max)(maxMeshEffects, sample.meshEffects);
            maxDebrisPieces = (std::max)(maxDebrisPieces, sample.debrisPieces);
        }
        if (const EnemyAttackDefinition* attack = GetSelectedAttackDefinition()) {
            maxSpeed = (std::max)(maxSpeed, attack->maxSpeed);
        }

        const auto drawGraph = [&](int lane, float TimelineSample::* valueMember, float maxValue, ImU32 color) {
            const float laneTop = origin.y + kHeaderHeight + kLaneHeight * static_cast<float>(lane) + 4.0f;
            const float laneBottom = laneTop + kLaneHeight - 8.0f;
            for (std::size_t index = 1; index < timelineSamples_.size(); ++index) {
                const TimelineSample& previous = timelineSamples_[index - 1];
                const TimelineSample& current = timelineSamples_[index];
                const float x0 = origin.x + kLabelWidth + std::clamp(previous.time, 0.0f, duration) * timelinePixelsPerSecond_;
                const float x1 = origin.x + kLabelWidth + std::clamp(current.time, 0.0f, duration) * timelinePixelsPerSecond_;
                const float y0 = laneBottom - std::clamp((previous.*valueMember) / maxValue, 0.0f, 1.0f) * (laneBottom - laneTop);
                const float y1 = laneBottom - std::clamp((current.*valueMember) / maxValue, 0.0f, 1.0f) * (laneBottom - laneTop);
                drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 2.0f);
            }
        };
        drawGraph(2, &TimelineSample::speed, maxSpeed, IM_COL32(75, 210, 255, 255));
        drawGraph(3, &TimelineSample::height, maxHeight, IM_COL32(255, 196, 72, 255));
        drawGraph(4, &TimelineSample::deformation, maxDeformation, IM_COL32(241, 121, 196, 255));
        drawGraph(5, &TimelineSample::hitReaction, 1.0f, IM_COL32(255, 92, 92, 255));
        drawGraph(6, &TimelineSample::gpuParticleSystems, maxGpuParticleSystems, IM_COL32(255, 224, 74, 255));
        drawGraph(7, &TimelineSample::cpuParticles, maxCpuParticles, IM_COL32(90, 226, 255, 255));
        drawGraph(8, &TimelineSample::meshEffects, maxMeshEffects, IM_COL32(255, 139, 72, 255));
        drawGraph(9, &TimelineSample::debrisPieces, maxDebrisPieces, IM_COL32(190, 157, 112, 255));

        char scaleText[64]{};
        sprintf_s(scaleText, "max %.1f", maxSpeed);
        drawList->AddText(
            ImVec2(origin.x + kLabelWidth - 55.0f, origin.y + kHeaderHeight + kLaneHeight * 2.0f + 7.0f),
            IM_COL32(120, 194, 220, 255),
            scaleText);
        sprintf_s(scaleText, "max %.1f", maxHeight);
        drawList->AddText(
            ImVec2(origin.x + kLabelWidth - 55.0f, origin.y + kHeaderHeight + kLaneHeight * 3.0f + 7.0f),
            IM_COL32(224, 181, 102, 255),
            scaleText);
        sprintf_s(scaleText, "max %.2f", maxDeformation);
        drawList->AddText(
            ImVec2(origin.x + kLabelWidth - 62.0f, origin.y + kHeaderHeight + kLaneHeight * 4.0f + 7.0f),
            IM_COL32(224, 137, 201, 255),
            scaleText);
        const auto drawCountScale = [&](int lane, float maxValue, ImU32 color) {
            sprintf_s(scaleText, "max %.0f", maxValue);
            drawList->AddText(
                ImVec2(origin.x + kLabelWidth - 55.0f, origin.y + kHeaderHeight + kLaneHeight * static_cast<float>(lane) + 7.0f),
                color,
                scaleText);
        };
        drawCountScale(6, maxGpuParticleSystems, IM_COL32(226, 205, 98, 255));
        drawCountScale(7, maxCpuParticles, IM_COL32(112, 205, 224, 255));
        drawCountScale(8, maxMeshEffects, IM_COL32(224, 153, 112, 255));
        drawCountScale(9, maxDebrisPieces, IM_COL32(190, 166, 130, 255));
    }

    const float playheadTime = std::clamp(elapsedTime_, 0.0f, duration);
    const float playheadX = origin.x + kLabelWidth + playheadTime * timelinePixelsPerSecond_;
    drawList->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, canvasEnd.y), IM_COL32(255, 82, 82, 255), 2.0f);
    drawList->AddTriangleFilled(
        ImVec2(playheadX - 6.0f, origin.y),
        ImVec2(playheadX + 6.0f, origin.y),
        ImVec2(playheadX, origin.y + 9.0f),
        IM_COL32(255, 82, 82, 255));

    if (ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x >= origin.x + kLabelWidth) {
            const float hoverTime = std::clamp(
                (mouse.x - origin.x - kLabelWidth) / timelinePixelsPerSecond_,
                0.0f,
                duration);
            ImGui::BeginTooltip();
            ImGui::Text("%.3f 秒  |  %dF", hoverTime, static_cast<int>(std::round(hoverTime / kFixedPreviewStep)));
            if (!timelineSamples_.empty()) {
                const auto nearest = (std::min_element)(
                    timelineSamples_.begin(),
                    timelineSamples_.end(),
                    [hoverTime](const TimelineSample& lhs, const TimelineSample& rhs) {
                        return std::abs(lhs.time - hoverTime) < std::abs(rhs.time - hoverTime);
                    });
                ImGui::Text("フェーズ: %s", nearest->phase.c_str());
                ImGui::Text("速度 %.2f m/s  |  高さ %.2f m", nearest->speed, nearest->height);
                ImGui::Text("変形 %.2f  |  被弾 %.2f", nearest->deformation, nearest->hitReaction);
                ImGui::Text("GPU系統 %.0f  |  通常粒子 %.0f", nearest->gpuParticleSystems, nearest->cpuParticles);
                ImGui::Text("Mesh Effect %.0f  |  Debris %.0f", nearest->meshEffects, nearest->debrisPieces);
            }
            ImGui::EndTooltip();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                timelineFollowPlayhead_ = false;
                playing_ = false;
                SeekPreview(hoverTime);
            }
        }
        if (ImGui::GetIO().KeyCtrl && std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
            timelinePixelsPerSecond_ = std::clamp(
                timelinePixelsPerSecond_ + ImGui::GetIO().MouseWheel * 18.0f,
                60.0f,
                280.0f);
        }
    }

    if (playing_ && timelineFollowPlayhead_) {
        const float playheadLocalX = kLabelWidth + playheadTime * timelinePixelsPerSecond_;
        const float desiredScroll = (std::max)(0.0f, playheadLocalX - visibleWidth * 0.75f);
        ImGui::SetScrollX((std::min)(desiredScroll, ImGui::GetScrollMaxX()));
    }
    ImGui::EndChild();

    ImGui::End();
#endif
}

void EnemyAttackPreviewWindow::DrawCrownMovieTimeline() {
#ifdef USE_IMGUI
    // 同じ下段ドックを使い、InspectorとGame Viewを残して確認します。
    if (!ImGui::Begin("攻撃・能力プレビュー - Timeline",nullptr,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove)) {
        ImGui::End(); return;
    }
    ImGui::PushID("CrownMovieTimeline");
    ImGui::TextUnformatted(crownDefeatMode_?"最終撃破ムービー / 60 fps":"王冠継承・崩壊ムービー / 60 fps");
    if (!enabled_) {
        ImGui::TextDisabled("右のInspectorから「ムービープレビューを開始」を押してください。");
        ImGui::PopID(); ImGui::End(); return;
    }
    const auto& t=CrownFinaleTuning::Get();
    if (ImGui::Button(playing_?"一時停止":"再生")) {
        if (!playing_ && elapsedTime_>=loopDuration_-.001f) SeekPreview(0);
        playing_=!playing_;
    }
    ImGui::SameLine(); if (ImGui::Button("先頭")) SeekPreview(0);
    const int frames[]={-10,-1,1,10};
    for (int frame:frames) {
        ImGui::SameLine(); char label[16]; std::snprintf(label,sizeof(label),"%+dF",frame);
        if (ImGui::Button(label)) SeekPreview(elapsedTime_+frame*kFixedPreviewStep);
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(95); ImGui::SliderFloat("速度",&playbackSpeed_,.1f,2.0f,"%.2fx");
    ImGui::SameLine(); ImGui::Checkbox("ループ",&loop_);
    float seek=elapsedTime_; ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##Time",&seek,0,loopDuration_,"%.3f 秒")) SeekPreview(seek);
    ImGui::Text("%.3f / %.2f 秒 | %dF",elapsedTime_,loopDuration_,static_cast<int>(std::round(elapsedTime_*60)));
    if(crownDefeatMode_) {
        const auto& d=CrownDefeatTuning::Get();
        const char* names[]={"最後の一撃","王冠の亀裂","王冠破砕","力の喪失","小スライム","勝利","終了"};
        const float points[]={0,.14f,d.rupture+.015f,d.shrink,d.settle+.025f,d.victory+.035f,1};
        for(int i=0;i<7;++i) {if(i)ImGui::SameLine();if(ImGui::Button(names[i]))SeekPreview(points[i]*loopDuration_);}
        ImGui::Separator();
        ImGui::TextWrapped("最後の一撃 → 王冠の亀裂と再集束 → 破砕・放電 → 力が抜けて小スライムへ → 王の勝利");
        ImGui::TextDisabled("本番と同じ時間で進行します。設定は右のInspectorから保存できます。");
        ImGui::PopID();ImGui::End();return;
    }
    const char* names[]={"破断","欠片着地","暴走","吸い込み","体内模様","金色の体","王冠","翼","脱出","最終ステージ"};
    const float points[]={t.movieFragmentBreakTime,t.movieFragmentLandTime,t.movieRageTime,t.movieFragmentPullTime,
        t.movieFragmentAbsorbTime,t.movieBodyTime,t.movieCrownTime,t.movieWingsTime,t.movieEscapeTime,t.movieArriveTime};
    for (int i=0;i<10;++i) {
        if (i && i!=5) ImGui::SameLine();
        if (ImGui::Button(names[i])) SeekPreview(points[i]*loopDuration_);
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space,false)) playing_=!playing_;
        const int step=ImGui::GetIO().KeyShift?10:1;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow,false)) SeekPreview(elapsedTime_-step*kFixedPreviewStep);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow,false)) SeekPreview(elapsedTime_+step*kFixedPreviewStep);
        if (ImGui::IsKeyPressed(ImGuiKey_Home,false)) SeekPreview(0);
    }
    const ImVec2 origin=ImGui::GetCursorScreenPos(); const float width=ImGui::GetContentRegionAvail().x;
    const float labelWidth=90, pixels=(std::max)(10.0f,(width-labelWidth)/loopDuration_);
    ImDrawList* draw=ImGui::GetWindowDrawList();
    for (int second=0;second<=static_cast<int>(loopDuration_);++second) {
        float x=origin.x+labelWidth+second*pixels; char label[16]; std::snprintf(label,sizeof(label),"%ds",second);
        draw->AddLine({x,origin.y+19},{x,origin.y+158},IM_COL32(115,123,140,90));
        draw->AddText({x+2,origin.y},IM_COL32(225,230,240,255),label);
    }
    const char* rows[]={"王冠の欠片","プレイヤー","ステージ","カメラ"};
    for(int row=0;row<4;++row) draw->AddText({origin.x,origin.y+28+row*32},IM_COL32(230,233,239,255),rows[row]);
    auto block=[&](int row,float start,float end,ImU32 color,const char* label) {
        DrawTimelineBlock(draw,origin,labelWidth,pixels,origin.y+23+row*32,30,start*loopDuration_,end*loopDuration_,color,label);
    };
    block(0,0,t.movieFragmentBreakTime,IM_COL32(192,153,70,240),"亀裂");
    block(0,t.movieFragmentBreakTime,t.movieFragmentLandTime,IM_COL32(245,169,75,240),"落下");
    block(0,t.movieFragmentLandTime,t.movieFragmentPullTime,IM_COL32(185,172,100,240),"着地");
    block(0,t.movieFragmentPullTime,t.movieFragmentAbsorbTime,IM_COL32(85,191,221,240),"吸収");
    block(1,t.movieFragmentAbsorbTime,t.movieBodyTime,IM_COL32(114,218,223,240),"体内模様");
    block(1,t.movieBodyTime,t.movieCrownTime,IM_COL32(239,190,56,240),"金色の体");
    block(1,t.movieCrownTime,t.movieWingsTime,IM_COL32(239,164,56,240),"王冠");
    block(1,t.movieWingsTime,t.movieEscapeTime,IM_COL32(235,213,156,240),"翼");
    block(1,t.movieEscapeTime,t.movieArriveTime,IM_COL32(125,177,236,240),"飛行脱出");
    block(1,t.movieArriveTime,1,IM_COL32(112,205,168,240),"着地");
    block(2,t.movieRageTime,t.movieRageTime+.07f,IM_COL32(236,190,116,240),"衝撃");
    block(2,t.movieRageTime+.02f,t.movieArriveTime,IM_COL32(218,95,91,240),"床・壁の崩壊");
    block(3,0,t.movieFragmentLandTime,IM_COL32(134,145,222,240),"装飾を追う");
    block(3,t.movieFragmentLandTime,t.movieEscapeTime,IM_COL32(152,135,206,240),"吸収・変身");
    block(3,t.movieEscapeTime,1,IM_COL32(109,159,204,240),"脱出・最終戦");
    float x=origin.x+labelWidth+elapsedTime_*pixels;
    draw->AddLine({x,origin.y+18},{x,origin.y+158},IM_COL32(255,117,116,255),2);
    ImGui::Dummy({width,162});
    ImGui::TextDisabled("Space: 再生/停止 / ←→: 1F / Shift+←→: 10F / Home: 先頭");
    ImGui::PopID(); ImGui::End();
#endif
}
