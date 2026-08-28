#include "ReplayDebugger.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "DebugEditor.h"
#include "EditorManager.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
constexpr float kLabelWidth = 126.0f;
constexpr float kTimelineHeaderHeight = 30.0f;
constexpr float kTimelineLaneHeight = 25.0f;
constexpr float kTimelineBottomPadding = 10.0f;

float DistanceSquared(const Vector3& lhs, const Vector3& rhs) {
    const float x = lhs.x - rhs.x;
    const float y = lhs.y - rhs.y;
    const float z = lhs.z - rhs.z;
    return x * x + y * y + z * z;
}

float DistanceSquared(const Vector2& lhs, const Vector2& rhs) {
    const float x = lhs.x - rhs.x;
    const float y = lhs.y - rhs.y;
    return x * x + y * y;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ReadCustomVector3(const json& custom, const char* key, Vector3& result) {
    if (!custom.contains(key)) {
        return false;
    }
    const json& value = custom[key];
    if (!value.is_array() || value.size() < 3) {
        return false;
    }
    result = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
    return true;
}

void DrawVector3Row(const char* label, const Vector3& value, const Vector3* previous) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%.3f, %.3f, %.3f", value.x, value.y, value.z);
    ImGui::TableSetColumnIndex(2);
    if (previous) {
        ImGui::TextDisabled(
            "%+.3f, %+.3f, %+.3f",
            value.x - previous->x,
            value.y - previous->y,
            value.z - previous->z);
    } else {
        ImGui::TextDisabled("-");
    }
}

void DrawVector2Row(const char* label, const Vector2& value, const Vector2* previous) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%.3f, %.3f", value.x, value.y);
    ImGui::TableSetColumnIndex(2);
    if (previous) {
        ImGui::TextDisabled("%+.3f, %+.3f", value.x - previous->x, value.y - previous->y);
    } else {
        ImGui::TextDisabled("-");
    }
}

const char* GetObjectStateLabel(const Object3d::ReplayState& state) {
    if (state.replayRemoved) return "削除済み";
    if (state.dead) return "死亡";
    if (!state.visible) return "非表示";
    return "有効";
}

ImVec4 GetObjectStateColor(const Object3d::ReplayState& state) {
    if (state.replayRemoved) return ImVec4(0.95f, 0.42f, 0.24f, 1.0f);
    if (state.dead) return ImVec4(0.95f, 0.28f, 0.38f, 1.0f);
    if (!state.visible) return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
    return ImVec4(0.38f, 0.88f, 0.52f, 1.0f);
}

const char* GetSpriteStateLabel(const Sprite::ReplayState& state) {
    if (state.replayRemoved) return "削除済み";
    if (!state.visible) return "非表示";
    return "表示中";
}

ImVec4 GetSpriteStateColor(const Sprite::ReplayState& state) {
    if (state.replayRemoved) return ImVec4(0.95f, 0.42f, 0.24f, 1.0f);
    if (!state.visible) return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
    return ImVec4(0.38f, 0.78f, 1.0f, 1.0f);
}
}

void ReplayDebugger::Draw(bool* open) {
    if (!open || !*open) {
        return;
    }

    const ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("リプレイデバッガー - Time Machine", open, windowFlags)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("リプレイ")) {
            const bool hasFrames = HasFrames();
            if (ImGui::MenuItem("一時停止", "Space", false, mode_ == Mode::Recording && hasFrames)) {
                PauseAt(frames_.size() - 1);
            }
            if (ImGui::MenuItem("履歴を再生", "Space", false, mode_ == Mode::Paused && hasFrames)) {
                StartPlayback();
            }
            const bool canBranchFromMenu =
                mode_ == Mode::Paused && hasFrames &&
                (!loadedArchiveReadOnly_ || CanResumeLoadedArchive());
            if (ImGui::MenuItem("この時点から分岐再開", "Ctrl+Enter", false,
                canBranchFromMenu)) {
                ResumeFromCursor();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("履歴をクリア", nullptr, false, hasFrames)) {
                ClearHistory(mode_ == Mode::Recording);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("表示")) {
            ImGui::MenuItem("変化したObjectのみ", nullptr, &showOnlyChangedObjects_);
            ImGui::MenuItem("削除済みObjectを表示", nullptr, &showRemovedObjects_);
            ImGui::MenuItem("記録中は末尾へ追従", nullptr, &autoScrollTimeline_);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    HandleEditorShortcuts();
    DrawToolbar();
    DrawArchivePanel();
    DrawRegressionPanel();
    DrawSummaryCards();
    DrawTimelineEditor();

    ImGui::SeparatorText("フレーム内要素");
    if (ImGui::RadioButton("Object3D", !inspectSprites_)) {
        inspectSprites_ = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Sprite / UI", inspectSprites_)) {
        inspectSprites_ = true;
    }
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float browserWidth = (std::max)(500.0f, availableWidth * 0.62f);
    ImGui::BeginChild("##ReplayObjectBrowserPane", ImVec2(browserWidth, 0.0f), true);
    if (inspectSprites_) DrawSpriteBrowser();
    else DrawObjectBrowser();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##ReplayObjectInspectorPane", ImVec2(0.0f, 0.0f), true);
    if (inspectSprites_) DrawSpriteInspector();
    else DrawObjectInspector();
    DrawSettingsPanel();
    ImGui::EndChild();

    ImGui::End();
}

void ReplayDebugger::DrawToolbar() {
    const ImVec4 modeColor = mode_ == Mode::Recording
        ? ImVec4(1.0f, 0.28f, 0.24f, 1.0f)
        : (mode_ == Mode::Regression
            ? ImVec4(0.72f, 0.48f, 1.0f, 1.0f)
            : (mode_ == Mode::Playback ? ImVec4(0.30f, 0.78f, 1.0f, 1.0f) : ImVec4(1.0f, 0.78f, 0.25f, 1.0f)));
    ImGui::TextColored(modeColor, "● %s", GetModeLabel());
    ImGui::SameLine();
    ImGui::TextDisabled("| Space: 再生/停止  ←/→: 1 frame  Shift+←/→: 10 frames");

    BaseScene* activeScene = GetValidatedActiveScene();
    const bool hasFrames = HasFrames();
    if (mode_ == Mode::Idle) {
        ImGui::BeginDisabled(!activeScene);
        if (ImGui::Button("● 記録開始", ImVec2(116.0f, 0.0f))) {
            BeginRecording(activeScene);
            statusMessage_ = "リプレイ記録を開始しました。";
        }
        ImGui::EndDisabled();
    } else if (mode_ == Mode::Recording) {
        if (ImGui::Button("■ 一時停止", ImVec2(116.0f, 0.0f))) {
            PauseAt(frames_.size() - 1);
            statusMessage_ = "シーンを停止し、最新フレームを選択しました。";
        }
    } else if (mode_ == Mode::Regression) {
        if (ImGui::Button("■ テスト中止", ImVec2(116.0f, 0.0f))) {
            regressionResult_.failures.push_back("ユーザー操作で自動回帰テストを中止しました。");
            FinishRegression();
        }
    } else if (mode_ == Mode::Playback) {
        if (ImGui::Button("■ 履歴停止", ImVec2(116.0f, 0.0f))) {
            PauseAt(cursor_);
        }
    } else {
        if (ImGui::Button("▶ 履歴再生", ImVec2(116.0f, 0.0f))) {
            StartPlayback();
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasFrames || mode_ == Mode::Regression);
    if (ImGui::Button("|<")) PauseAt(0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("記録先頭へ (Home)");
    ImGui::SameLine();
    if (ImGui::Button("<<")) StepCursor(-10);
    ImGui::SameLine();
    if (ImGui::Button("<")) StepCursor(-1);
    ImGui::SameLine();
    if (ImGui::Button(">")) StepCursor(1);
    ImGui::SameLine();
    if (ImGui::Button(">>")) StepCursor(10);
    ImGui::SameLine();
    if (ImGui::Button(">|")) PauseAt(frames_.size() - 1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("記録末尾へ (End)");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    ImGui::SliderFloat("速度", &playbackSpeed_, 0.1f, 2.0f, "%.1fx");

    ImGui::SameLine();
    const std::size_t futureFrames = cursor_ < frames_.size() ? frames_.size() - cursor_ - 1 : 0;
    std::string archiveResumeBlockReason;
    const bool archiveResumeReady =
        !loadedArchiveReadOnly_ || CanResumeLoadedArchive(&archiveResumeBlockReason);
    ImGui::BeginDisabled(mode_ == Mode::Recording || mode_ == Mode::Regression || !archiveResumeReady);
    if (ImGui::Button("この時点から分岐再開", ImVec2(188.0f, 0.0f))) {
        ResumeFromCursor();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (loadedArchiveReadOnly_) {
            if (archiveResumeReady) {
                ImGui::SetTooltip("保存リプレイの選択位置からゲームを再開し、以後を新しい履歴として記録します。");
            } else {
                ImGui::SetTooltip("分岐再開できません: %s", archiveResumeBlockReason.c_str());
            }
        } else {
            ImGui::SetTooltip("選択位置より後ろの %zu フレームを破棄してゲームを再開します。", futureFrames);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("履歴クリア")) {
        ClearHistory(mode_ == Mode::Recording);
    }
    ImGui::EndDisabled();

    if (!statusMessage_.empty()) {
        ImGui::TextColored(ImVec4(0.48f, 0.82f, 1.0f, 1.0f), "%s", statusMessage_.c_str());
    }
}

void ReplayDebugger::DrawArchivePanel() {
    if (!ImGui::CollapsingHeader("保存リプレイ", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const bool canSave = HasFrames() && !loadedArchiveReadOnly_;
    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("現在の履歴を保存", ImVec2(164.0f, 0.0f))) {
        SaveCurrentReplayArchive();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(canSave
            ? "現在保持している履歴を Saved/Replays に保存します。"
            : "実行中に記録した履歴だけ保存できます。");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("一覧を更新")) {
        RefreshReplayArchiveList();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("保存先: Saved/Replays");

    if (replayArchiveEntries_.empty()) {
        ImGui::TextDisabled("保存済みのリプレイはありません。");
    } else {
        if (selectedArchiveIndex_ < 0 || selectedArchiveIndex_ >= static_cast<int>(replayArchiveEntries_.size())) {
            selectedArchiveIndex_ = 0;
        }
        const ReplayArchiveEntry& selected = replayArchiveEntries_[selectedArchiveIndex_];
        ImGui::SetNextItemWidth((std::max)(360.0f, ImGui::GetContentRegionAvail().x * 0.48f));
        if (ImGui::BeginCombo("保存データ", selected.fileName.c_str())) {
            for (int index = 0; index < static_cast<int>(replayArchiveEntries_.size()); ++index) {
                const ReplayArchiveEntry& entry = replayArchiveEntries_[index];
                const bool isSelected = index == selectedArchiveIndex_;
                const std::string label = entry.valid
                    ? entry.fileName + "  |  " + entry.sceneLabel
                    : entry.fileName + "  |  読込不可";
                if (ImGui::Selectable(label.c_str(), isSelected)) selectedArchiveIndex_ = index;
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        const ReplayArchiveEntry& current = replayArchiveEntries_[selectedArchiveIndex_];
        if (current.valid) {
            ImGui::TextDisabled("%s  |  %s  |  %.2f秒 / %zu状態 / %zu入力",
                current.createdAt.c_str(), current.sceneLabel.c_str(),
                current.durationSeconds, current.frameCount, current.inputSampleCount);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "読込不可: %s", current.error.c_str());
        }

        ImGui::BeginDisabled(!current.valid || pendingReplayArchive_.has_value());
        if (ImGui::Button("元シーンを開いて読込", ImVec2(188.0f, 0.0f))) {
            if (HasFrames() && !loadedArchiveReadOnly_) {
                ImGui::OpenPopup("現行リプレイ履歴を置き換えますか？");
            } else {
                RequestLoadReplayArchive(static_cast<std::size_t>(selectedArchiveIndex_));
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        const bool canStartRegression = current.valid && current.regressionReady &&
            !pendingReplayArchive_.has_value() && mode_ != Mode::Regression;
        ImGui::BeginDisabled(!canStartRegression);
        if (ImGui::Button("自動回帰テスト", ImVec2(164.0f, 0.0f))) {
            RequestStartRegression(static_cast<std::size_t>(selectedArchiveIndex_));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(current.regressionReady
                ? "元シーンを開き、記録入力で実シミュレーションを再実行します。"
                : "旧形式または入力列なしのReplayです。新しく記録・保存すると実行できます。");
        }
        ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("現行リプレイ履歴を置き換えますか？", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("現在のメモリ内リプレイ履歴は破棄されます。必要なら先に保存してください。");
            if (ImGui::Button("読込を続行", ImVec2(132.0f, 0.0f))) {
                RequestLoadReplayArchive(static_cast<std::size_t>(selectedArchiveIndex_));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル", ImVec2(112.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    if (pendingReplayArchive_) {
        ImGui::TextColored(ImVec4(1.0f, 0.76f, 0.28f, 1.0f),
            "元シーンの準備中です。Scene読込後、Play中に履歴を適用します。");
    }
    if (loadedArchiveReadOnly_) {
        std::string resumeBlockReason;
        if (CanResumeLoadedArchive(&resumeBlockReason)) {
            ImGui::TextColored(ImVec4(0.36f, 0.92f, 0.58f, 1.0f),
                "分岐再開可能: %s（任意フレームでゲームへ復帰できます）", loadedArchiveName_.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f),
                "閲覧中: %s（分岐停止理由: %s）",
                loadedArchiveName_.c_str(), resumeBlockReason.c_str());
        }
        if (recreatedArchiveObjectCount_ > 0) {
            ImGui::TextDisabled("Archiveから動的Objectを %zu 件再生成しました。",
                recreatedArchiveObjectCount_);
        }
    }
}


void ReplayDebugger::DrawRegressionPanel() {
    if (!ImGui::CollapsingHeader("自動回帰テスト", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const bool settingsLocked = mode_ == Mode::Regression || pendingRegressionStart_ || pendingReplayArchive_.has_value();
    ImGui::BeginDisabled(settingsLocked);
    ImGui::SetNextItemWidth(150.0f);
    ImGui::DragFloat("軌跡の許容誤差", &regressionSettings_.maxPositionError, 0.05f, 0.01f, 100.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::DragFloat("座標上限", &regressionSettings_.worldPositionLimit, 10.0f, 10.0f, 100000.0f, "%.0f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("撮影枚数", &regressionSettings_.screenshotCount, 0, 8);

    ImGui::Checkbox("性能Budget超過を失敗にする", &regressionSettings_.failOnPerformanceBudget);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::DragFloat("CPU ms", &regressionSettings_.cpuBudgetMs, 0.25f, 0.1f, 1000.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::DragFloat("GPU ms", &regressionSettings_.gpuBudgetMs, 0.25f, 0.1f, 1000.0f, "%.2f");
    ImGui::EndDisabled();

    ImGui::TextDisabled(
        "新形式Replayの入力を先頭状態から再実行し、プレイヤー軌跡・ゴール・Errorログ・CPU/GPUを検証します。");
    if (!regressionResult_.available) {
        ImGui::TextDisabled("保存リプレイを選び、上の『自動回帰テスト』から開始してください。");
        return;
    }

    if (regressionResult_.running) {
        const float progress = inputSamples_.empty()
            ? 0.0f
            : static_cast<float>(regressionInputIndex_) / static_cast<float>(inputSamples_.size());
        ImGui::ProgressBar((std::clamp)(progress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "実シミュレーション再実行中");
    } else {
        const ImVec4 resultColor = regressionResult_.passed
            ? ImVec4(0.36f, 0.92f, 0.58f, 1.0f)
            : ImVec4(1.0f, 0.38f, 0.30f, 1.0f);
        ImGui::TextColored(resultColor, "%s", regressionResult_.passed ? "PASS" : "FAIL");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", regressionResult_.archiveName.c_str());
    }

    if (ImGui::BeginTable("ReplayRegressionMetrics", 6,
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        const char* labels[] = { "再実行", "比較", "最大軌跡誤差", "CPU 平均/最大", "GPU 平均/最大", "Error" };
        for (const char* label : labels) {
            ImGui::TableSetupColumn(label);
        }
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("%zu", regressionResult_.simulatedFrames);
        ImGui::TableNextColumn(); ImGui::Text("%zu", regressionResult_.comparedFrames);
        ImGui::TableNextColumn(); ImGui::Text("%.3f", regressionResult_.maxPositionError);
        ImGui::TableNextColumn(); ImGui::Text("%.2f / %.2f", regressionResult_.averageCpuMs, regressionResult_.maximumCpuMs);
        ImGui::TableNextColumn(); ImGui::Text("%.2f / %.2f", regressionResult_.averageGpuMs, regressionResult_.maximumGpuMs);
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(regressionResult_.newErrorLogs));
        ImGui::EndTable();
    }

    ImGui::Text("ゴール: 基準 %s / 再実行 %s",
        regressionResult_.expectedGoal ? "到達" : "未到達",
        regressionResult_.observedGoal ? "到達" : "未到達");
    for (const std::string& failure : regressionResult_.failures) {
        ImGui::BulletText("%s", failure.c_str());
    }
    if (!regressionResult_.reportPath.empty()) {
        ImGui::TextWrapped("Report: %s", regressionResult_.reportPath.c_str());
    }
}
void ReplayDebugger::DrawSummaryCards() {
    if (!ImGui::BeginTable("ReplaySummaryCards", 4, ImGuiTableFlags_SizingStretchSame)) {
        return;
    }

    const double duration = frames_.size() >= 2 ? frames_.back().time - frames_.front().time : 0.0;
    const std::size_t objectCount = frames_.empty() ? 0 : frames_[cursor_].diagnostics.activeObjects;
    const std::size_t spriteCount = frames_.empty() ? 0 : frames_[cursor_].diagnostics.activeSprites;
    char memoryText[32];
    FormatBytes(estimatedMemoryBytes_, memoryText, sizeof(memoryText));

    const char* labels[] = { "記録時間", "フレーム", "Object / Sprite", "推定メモリ" };
    char values[4][48] = {};
    sprintf_s(values[0], "%.2f / %.0f 秒", duration, historySeconds_);
    sprintf_s(values[1], "%zu / %zu", frames_.size(), GetMaxFrameCount());
    sprintf_s(values[2], "%zu / %zu", objectCount, spriteCount);
    sprintf_s(values[3], "%s", memoryText);

    for (int column = 0; column < 4; ++column) {
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.18f, 0.88f));
        ImGui::BeginChild(column + 100, ImVec2(0.0f, 54.0f), true);
        ImGui::TextDisabled("%s", labels[column]);
        ImGui::Text("%s", values[column]);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

void ReplayDebugger::DrawTimelineEditor() {
    ImGui::SeparatorText("タイムライン");
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("ズーム", &timelinePixelsPerSecond_, 45.0f, 300.0f, "%.0f px/s");
    ImGui::SameLine();
    ImGui::Checkbox("記録末尾へ追従", &autoScrollTimeline_);

    const float timelineHeight = kTimelineHeaderHeight + kTimelineLaneHeight * 3.0f + kTimelineBottomPadding;
    if (frames_.empty()) {
        ImGui::BeginChild("##ReplayEmptyTimeline", ImVec2(0.0f, timelineHeight), true);
        ImGui::TextDisabled("ゲームを再生すると、ここにリプレイ履歴が記録されます。");
        ImGui::EndChild();
        return;
    }

    const double startTime = frames_.front().time;
    const double endTime = frames_.back().time;
    const double duration = (std::max)(0.1, endTime - startTime);
    const float visibleWidth = (std::max)(ImGui::GetContentRegionAvail().x, 300.0f);
    const float contentWidth = (std::max)(visibleWidth, kLabelWidth + static_cast<float>(duration) * timelinePixelsPerSecond_ + 80.0f);

    ImGui::BeginChild(
        "##ReplayTimelineScroll",
        ImVec2(0.0f, timelineHeight + ImGui::GetStyle().ScrollbarSize + 4.0f),
        true,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##ReplayTimelineCanvas", ImVec2(contentWidth, timelineHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + contentWidth, origin.y + timelineHeight);

    drawList->AddRectFilled(origin, end, IM_COL32(22, 25, 31, 255), 4.0f);
    drawList->AddRectFilled(origin, ImVec2(origin.x + kLabelWidth, end.y), IM_COL32(34, 38, 47, 255), 4.0f);
    drawList->AddLine(
        ImVec2(origin.x + kLabelWidth, origin.y),
        ImVec2(origin.x + kLabelWidth, end.y),
        IM_COL32(105, 115, 135, 255));

    const char* laneNames[] = { "Frames", "Spawn / Remove", "HP / Death" };
    for (int lane = 0; lane < 3; ++lane) {
        const float y0 = origin.y + kTimelineHeaderHeight + lane * kTimelineLaneHeight;
        const float y1 = y0 + kTimelineLaneHeight;
        if ((lane % 2) == 0) {
            drawList->AddRectFilled(ImVec2(origin.x, y0), ImVec2(end.x, y1), IM_COL32(255, 255, 255, 6));
        }
        drawList->AddLine(ImVec2(origin.x, y1), ImVec2(end.x, y1), IM_COL32(62, 68, 80, 190));
        drawList->AddText(ImVec2(origin.x + 8.0f, y0 + 5.0f), IM_COL32(205, 213, 226, 255), laneNames[lane]);
    }

    float tickStep = 1.0f;
    if (timelinePixelsPerSecond_ >= 220.0f) tickStep = 0.25f;
    else if (timelinePixelsPerSecond_ >= 110.0f) tickStep = 0.5f;
    const int tickCount = static_cast<int>(std::ceil(duration / tickStep));
    for (int tick = 0; tick <= tickCount; ++tick) {
        const float elapsed = tick * tickStep;
        const float x = origin.x + kLabelWidth + elapsed * timelinePixelsPerSecond_;
        const bool major = std::fmod(elapsed + 0.001f, 1.0f) < 0.01f;
        drawList->AddLine(
            ImVec2(x, origin.y + (major ? 0.0f : 14.0f)),
            ImVec2(x, end.y),
            major ? IM_COL32(80, 88, 105, 220) : IM_COL32(60, 66, 78, 130));
        if (major) {
            char label[24];
            sprintf_s(label, "%.0fs", elapsed);
            drawList->AddText(ImVec2(x + 4.0f, origin.y + 5.0f), IM_COL32(185, 194, 210, 255), label);
        }
    }

    for (std::size_t index = 0; index < frames_.size(); ++index) {
        const FrameSnapshot& frame = frames_[index];
        const float elapsed = static_cast<float>(frame.time - startTime);
        const float x = origin.x + kLabelWidth + elapsed * timelinePixelsPerSecond_;
        const float frameY0 = origin.y + kTimelineHeaderHeight + 4.0f;
        const float frameY1 = frameY0 + kTimelineLaneHeight - 8.0f;
        const std::size_t totalChanges = frame.diagnostics.changedObjects + frame.diagnostics.changedSprites;
        const int changeAlpha = (std::min)(220, 55 + static_cast<int>(totalChanges) * 20);
        drawList->AddLine(ImVec2(x, frameY0), ImVec2(x, frameY1), IM_COL32(90, 175, 235, changeAlpha), 1.0f);

        const float presenceY = origin.y + kTimelineHeaderHeight + kTimelineLaneHeight * 1.5f;
        if (frame.diagnostics.spawnedObjects > 0) {
            drawList->AddTriangleFilled(
                ImVec2(x, presenceY - 7.0f),
                ImVec2(x - 5.0f, presenceY + 5.0f),
                ImVec2(x + 5.0f, presenceY + 5.0f),
                IM_COL32(80, 225, 130, 255));
        }
        if (frame.diagnostics.removedObjects > 0) {
            drawList->AddRectFilled(
                ImVec2(x - 4.0f, presenceY - 4.0f),
                ImVec2(x + 4.0f, presenceY + 4.0f),
                IM_COL32(245, 125, 65, 255),
                1.0f);
        }

        const float hpY = origin.y + kTimelineHeaderHeight + kTimelineLaneHeight * 2.5f;
        if (frame.diagnostics.hpChanges > 0) {
            drawList->AddCircleFilled(ImVec2(x, hpY), 4.0f, IM_COL32(255, 205, 65, 255));
        }
        if (frame.diagnostics.deaths > 0) {
            drawList->AddCircle(ImVec2(x, hpY), 7.0f, IM_COL32(255, 70, 100, 255), 0, 2.0f);
        }
    }

    const float playheadElapsed = static_cast<float>(frames_[cursor_].time - startTime);
    const float playheadX = origin.x + kLabelWidth + playheadElapsed * timelinePixelsPerSecond_;
    drawList->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, end.y), IM_COL32(255, 82, 82, 255), 2.0f);
    drawList->AddTriangleFilled(
        ImVec2(playheadX - 6.0f, origin.y),
        ImVec2(playheadX + 6.0f, origin.y),
        ImVec2(playheadX, origin.y + 9.0f),
        IM_COL32(255, 82, 82, 255));

    if (ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x >= origin.x + kLabelWidth) {
            const double hoverTime = startTime + (mouse.x - origin.x - kLabelWidth) / timelinePixelsPerSecond_;
            std::size_t nearest = 0;
            double bestDistance = std::abs(frames_.front().time - hoverTime);
            for (std::size_t index = 1; index < frames_.size(); ++index) {
                const double distance = std::abs(frames_[index].time - hoverTime);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    nearest = index;
                }
            }
            const FrameSnapshot& hovered = frames_[nearest];
            ImGui::BeginTooltip();
            ImGui::Text("Frame %zu  |  %.3f 秒", nearest, hovered.time - startTime);
            ImGui::Text("変化 %zu / 生成 %zu / 削除 %zu", hovered.diagnostics.changedObjects, hovered.diagnostics.spawnedObjects, hovered.diagnostics.removedObjects);
            ImGui::Text("Sprite %zu / 変化 %zu", hovered.diagnostics.activeSprites, hovered.diagnostics.changedSprites);
            ImGui::Text("HP変化 %zu / 撃破 %zu", hovered.diagnostics.hpChanges, hovered.diagnostics.deaths);
            ImGui::EndTooltip();
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            if (mouse.x >= origin.x + kLabelWidth) {
                const double selectedTime = startTime + (mouse.x - origin.x - kLabelWidth) / timelinePixelsPerSecond_;
                SelectFrameFromTimeline(selectedTime);
            }
        }
        if (ImGui::GetIO().KeyCtrl && std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
            timelinePixelsPerSecond_ = (std::clamp)(
                timelinePixelsPerSecond_ + ImGui::GetIO().MouseWheel * 18.0f,
                45.0f,
                300.0f);
        }
    }

    if (mode_ == Mode::Recording && autoScrollTimeline_) {
        ImGui::SetScrollX(ImGui::GetScrollMaxX());
    }
    ImGui::EndChild();
}

void ReplayDebugger::DrawObjectBrowser() {
    if (frames_.empty()) {
        ImGui::TextDisabled("記録フレームがありません。");
        return;
    }

    ImGui::SetNextItemWidth(250.0f);
    ImGui::InputTextWithHint("##ReplayObjectFilter", "Object名 / Classで検索", objectFilter_, sizeof(objectFilter_));
    ImGui::SameLine();
    ImGui::Checkbox("変化のみ", &showOnlyChangedObjects_);
    ImGui::SameLine();
    ImGui::Checkbox("削除済み", &showRemovedObjects_);

    const FrameSnapshot& frame = frames_[cursor_];
    const std::string filter = ToLower(objectFilter_);
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("ReplayObjectTable", 7, flags, ImVec2(0.0f, -4.0f))) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("状態", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 66.0f);
    ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("変化", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableHeadersRow();

    const FrameSnapshot* previousFrame = cursor_ > 0 ? &frames_[cursor_ - 1] : nullptr;
    for (const ObjectSnapshot& snapshot : frame.objects) {
        const ObjectSnapshot* previous = previousFrame ? FindObjectSnapshot(*previousFrame, snapshot.replayId) : nullptr;
        const bool changed = !previous || HasMeaningfulChange(*previous, snapshot);
        if (showOnlyChangedObjects_ && !changed) continue;
        if (!showRemovedObjects_ && snapshot.state.replayRemoved) continue;
        if (!filter.empty()) {
            const std::string searchable = ToLower(snapshot.name + " " + snapshot.className);
            if (searchable.find(filter) == std::string::npos) continue;
        }

        ImGui::PushID(static_cast<int>(snapshot.replayId & 0x7fffffff));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool selected = selectedReplayId_ == snapshot.replayId;
        if (ImGui::Selectable(snapshot.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
            selectedReplayId_ = snapshot.replayId;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                SelectSceneObject(snapshot.replayId);
            }
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", snapshot.className.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(GetObjectStateColor(snapshot.state), "%s", GetObjectStateLabel(snapshot.state));
        ImGui::TableSetColumnIndex(3);
        if (snapshot.state.hasParameter) ImGui::Text("%.1f", snapshot.state.parameter.hp);
        else ImGui::TextDisabled("-");
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.2f", snapshot.state.translation.x);
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%.2f", snapshot.state.translation.y);
        ImGui::TableSetColumnIndex(6);
        if (!previous) {
            ImGui::TextColored(ImVec4(0.40f, 0.88f, 0.52f, 1.0f), "新規");
        } else if (!changed) {
            ImGui::TextDisabled("-");
        } else if (!previous->state.replayRemoved && snapshot.state.replayRemoved) {
            ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.24f, 1.0f), "削除");
        } else if (previous->state.hasParameter && snapshot.state.hasParameter &&
            std::abs(previous->state.parameter.hp - snapshot.state.parameter.hp) > 0.001f) {
            ImGui::Text("HP %+.1f", snapshot.state.parameter.hp - previous->state.parameter.hp);
        } else {
            ImGui::Text("Transform");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void ReplayDebugger::DrawSpriteBrowser() {
    if (frames_.empty()) {
        ImGui::TextDisabled("記録フレームがありません。");
        return;
    }

    ImGui::SetNextItemWidth(250.0f);
    ImGui::InputTextWithHint("##ReplaySpriteFilter", "Sprite名で検索", objectFilter_, sizeof(objectFilter_));
    ImGui::SameLine();
    ImGui::Checkbox("変化のみ", &showOnlyChangedObjects_);
    ImGui::SameLine();
    ImGui::Checkbox("削除済み", &showRemovedObjects_);

    const FrameSnapshot& frame = frames_[cursor_];
    const FrameSnapshot* previousFrame = cursor_ > 0 ? &frames_[cursor_ - 1] : nullptr;
    const std::string filter = ToLower(objectFilter_);
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("ReplaySpriteTable", 7, flags, ImVec2(0.0f, -4.0f))) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Sprite", ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("状態", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("H", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("変化", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableHeadersRow();

    for (const SpriteSnapshot& snapshot : frame.sprites) {
        const SpriteSnapshot* previous = previousFrame ? FindSpriteSnapshot(*previousFrame, snapshot.replayId) : nullptr;
        const bool changed = !previous || HasMeaningfulChange(*previous, snapshot);
        if (showOnlyChangedObjects_ && !changed) continue;
        if (!showRemovedObjects_ && snapshot.state.replayRemoved) continue;
        if (!filter.empty() && ToLower(snapshot.name).find(filter) == std::string::npos) continue;

        ImGui::PushID(static_cast<int>(snapshot.replayId & 0x7fffffff));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool selected = selectedSpriteReplayId_ == snapshot.replayId;
        if (ImGui::Selectable(snapshot.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
            selectedSpriteReplayId_ = snapshot.replayId;
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(GetSpriteStateColor(snapshot.state), "%s", GetSpriteStateLabel(snapshot.state));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.1f", snapshot.state.position.x);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.1f", snapshot.state.position.y);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.1f", snapshot.state.size.x);
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%.1f", snapshot.state.size.y);
        ImGui::TableSetColumnIndex(6);
        if (!previous) {
            ImGui::TextColored(ImVec4(0.40f, 0.88f, 0.52f, 1.0f), "新規");
        } else if (!changed) {
            ImGui::TextDisabled("-");
        } else if (!previous->state.replayRemoved && snapshot.state.replayRemoved) {
            ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.24f, 1.0f), "削除");
        } else if (previous->state.visible != snapshot.state.visible) {
            ImGui::Text("表示");
        } else {
            ImGui::Text("見た目");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void ReplayDebugger::DrawObjectInspector() {
    ImGui::SeparatorText("Object差分Inspector");
    if (frames_.empty() || selectedReplayId_ == 0) {
        ImGui::TextDisabled("左の一覧からObjectを選択してください。");
        return;
    }

    const ObjectSnapshot* current = FindObjectSnapshot(frames_[cursor_], selectedReplayId_);
    if (!current) {
        ImGui::TextDisabled("このフレームには選択Objectが存在しません。");
        ImGui::TextWrapped("生成前または削除後の区間です。変化ナビゲーションで出現・消滅フレームへ移動できます。");
        if (ImGui::Button("< 前の変化")) JumpToObjectChange(-1);
        ImGui::SameLine();
        if (ImGui::Button("次の変化 >")) JumpToObjectChange(1);
        return;
    }
    const ObjectSnapshot* previous = FindPreviousObjectSnapshot(selectedReplayId_);
    const ObjectSnapshot* latest = FindLatestObjectSnapshot(selectedReplayId_);

    ImGui::Text("%s", current->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s / ID %llu)", current->className.c_str(), static_cast<unsigned long long>(current->replayId));
    ImGui::TextColored(GetObjectStateColor(current->state), "● %s", GetObjectStateLabel(current->state));

    if (ImGui::Button("Scene上で選択", ImVec2(-1.0f, 0.0f))) {
        SelectSceneObject(selectedReplayId_);
    }
    if (ImGui::Button("< 前の変化")) JumpToObjectChange(-1);
    ImGui::SameLine();
    if (ImGui::Button("次の変化 >")) JumpToObjectChange(1);

    if (ImGui::BeginTable("ReplayTransformInspector", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("記録値");
        ImGui::TableSetupColumn("前Frame差分");
        ImGui::TableHeadersRow();
        DrawVector3Row("位置", current->state.translation, previous ? &previous->state.translation : nullptr);
        DrawVector3Row("回転", current->state.rotation, previous ? &previous->state.rotation : nullptr);
        DrawVector3Row("スケール", current->state.scale, previous ? &previous->state.scale : nullptr);
        ImGui::EndTable();
    }

    if (current->state.hasParameter) {
        const float previousHp = previous && previous->state.hasParameter ? previous->state.parameter.hp : current->state.parameter.hp;
        const float latestHp = latest && latest->state.hasParameter ? latest->state.parameter.hp : current->state.parameter.hp;
        ImGui::Text("HP: %.1f / %.1f", current->state.parameter.hp, current->state.parameter.maxHp);
        ImGui::SameLine();
        ImGui::TextDisabled("前Frame %+.1f / 最新との差 %+.1f", current->state.parameter.hp - previousHp, current->state.parameter.hp - latestHp);
    }

    Vector3 velocity;
    if (ReadCustomVector3(current->state.custom, "characterVelocity", velocity)) {
        ImGui::Text("速度: %.2f, %.2f, %.2f", velocity.x, velocity.y, velocity.z);
        ImGui::TextDisabled("速さ: %.2f", std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z));
    }

    ImGui::TextDisabled("Animation: %s  t=%.3f", current->state.animationName.empty() ? "(なし)" : current->state.animationName.c_str(), current->state.animationTime);
    ImGui::TextDisabled("Model: %s", current->state.modelName.empty() ? "(なし)" : current->state.modelName.c_str());
    if (lastMissingObjectCount_ > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "復元不能Object: %zu", lastMissingObjectCount_);
    }
}

void ReplayDebugger::DrawSpriteInspector() {
    ImGui::SeparatorText("Sprite差分Inspector");
    if (frames_.empty() || selectedSpriteReplayId_ == 0) {
        ImGui::TextDisabled("左の一覧からSpriteを選択してください。");
        return;
    }

    const SpriteSnapshot* current = FindSpriteSnapshot(frames_[cursor_], selectedSpriteReplayId_);
    if (!current) {
        ImGui::TextDisabled("このフレームには選択Spriteが存在しません。");
        return;
    }
    const SpriteSnapshot* previous = cursor_ > 0
        ? FindSpriteSnapshot(frames_[cursor_ - 1], selectedSpriteReplayId_)
        : nullptr;

    ImGui::Text("%s", current->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(ID %llu)", static_cast<unsigned long long>(current->replayId));
    ImGui::TextColored(GetSpriteStateColor(current->state), "● %s", GetSpriteStateLabel(current->state));

    if (ImGui::BeginTable("ReplaySpriteTransformInspector", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("記録値");
        ImGui::TableSetupColumn("前Frame差分");
        ImGui::TableHeadersRow();
        DrawVector2Row("位置", current->state.position, previous ? &previous->state.position : nullptr);
        DrawVector2Row("サイズ", current->state.size, previous ? &previous->state.size : nullptr);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("回転");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f", current->state.rotation);
        ImGui::TableSetColumnIndex(2);
        if (previous) ImGui::TextDisabled("%+.3f", current->state.rotation - previous->state.rotation);
        else ImGui::TextDisabled("-");
        ImGui::EndTable();
    }

    ImGui::Text("Color: %.2f, %.2f, %.2f, %.2f", current->state.color.x, current->state.color.y, current->state.color.z, current->state.color.w);
    ImGui::TextDisabled("Texture Handle: %u / Emissive: %.2f", current->state.textureHandle, current->state.emissive);
    ImGui::TextDisabled(
        "Animation: frame %d / %d  timer %.3f  %s",
        current->state.currentFrame,
        current->state.totalFrames,
        current->state.animationTimer,
        current->state.animationPlaying ? "再生中" : "停止");
    ImGui::TextDisabled("Parent Replay ID: %llu", static_cast<unsigned long long>(current->state.parentReplayId));
    if (lastMissingSpriteCount_ > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "復元不能Sprite: %zu", lastMissingSpriteCount_);
    }
}

void ReplayDebugger::DrawSettingsPanel() {
    if (!ImGui::CollapsingHeader("記録設定 / 対応範囲")) {
        return;
    }

    ImGui::Checkbox("プレイ開始時に自動記録", &autoRecord_);
    ImGui::BeginDisabled(frames_.size() > 1);
    ImGui::SliderFloat("記録頻度 (fps)", &captureRate_, 5.0f, 60.0f, "%.0f");
    ImGui::SliderFloat("履歴の長さ (秒)", &historySeconds_, 3.0f, 60.0f, "%.0f");
    ImGui::EndDisabled();
    if (frames_.size() > 1) {
        ImGui::TextDisabled("記録頻度と履歴長は、履歴クリア後に変更できます。");
    }

    ImGui::Separator();
    ImGui::TextWrapped("停止中と履歴再生中はScene Updateを完全停止します。分岐再開時は選択位置より未来の履歴を捨て、Collisionを復元して新しい時間軸を記録します。");
    ImGui::TextWrapped("保存リプレイは元のScene Assetを開き直し、永続GUIDで対応付けます。復元が完全なフレームは、その時点からゲームへ戻って新しい時間軸を記録できます。");
    ImGui::TextDisabled("対応: Scene Object / 動的Enemy・Gimmick・Item / Transform / HP / Player主要状態 / Camera / Sprite・UI / UI制御タイマー");
    ImGui::TextDisabled("分岐時に破棄: 一時Particle / Bullet / Audio。敵固有AI状態は各クラスのReplay state対応範囲で復元します。");
}

void ReplayDebugger::HandleEditorShortcuts() {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) {
        return;
    }
    if (mode_ == Mode::Regression) {
        return;
    }

    const bool hasFrames = HasFrames();
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        if (mode_ == Mode::Recording && hasFrames) PauseAt(frames_.size() - 1);
        else if (mode_ == Mode::Playback && hasFrames) PauseAt(cursor_);
        else if (mode_ == Mode::Paused && hasFrames) StartPlayback();
    }
    if (!hasFrames) return;

    const int step = ImGui::GetIO().KeyShift ? 10 : 1;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) StepCursor(-step);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) StepCursor(step);
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) PauseAt(0);
    if (ImGui::IsKeyPressed(ImGuiKey_End)) PauseAt(frames_.size() - 1);
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter) &&
        mode_ == Mode::Paused &&
        (!loadedArchiveReadOnly_ || CanResumeLoadedArchive())) {
        ResumeFromCursor();
    }
}

void ReplayDebugger::SelectFrameFromTimeline(double time) {
    if (frames_.empty()) return;
    std::size_t nearest = 0;
    double bestDistance = std::abs(frames_.front().time - time);
    for (std::size_t index = 1; index < frames_.size(); ++index) {
        const double distance = std::abs(frames_[index].time - time);
        if (distance < bestDistance) {
            bestDistance = distance;
            nearest = index;
        }
    }
    PauseAt(nearest);
}

void ReplayDebugger::SelectSceneObject(uint64_t replayId) {
    BaseScene* scene = GetValidatedActiveScene();
    if (!scene || !debugEditor_) return;
    for (auto& object : scene->GetObjects()) {
        if (object && object->GetReplayId() == replayId) {
            debugEditor_->SetSelectedObject(object.get());
            EditorManager::GetInstance()->SetSelectedObject(debugEditor_);
            statusMessage_ = object->GetName() + " をScene上で選択しました。";
            return;
        }
    }
    statusMessage_ = "対象Objectは現在のSceneに存在しません。";
}

void ReplayDebugger::JumpToObjectChange(int direction) {
    if (frames_.empty() || selectedReplayId_ == 0 || direction == 0) return;

    if (direction < 0) {
        for (std::size_t index = cursor_; index > 0; --index) {
            const ObjectSnapshot* current = FindObjectSnapshot(frames_[index], selectedReplayId_);
            const ObjectSnapshot* before = FindObjectSnapshot(frames_[index - 1], selectedReplayId_);
            const bool presenceChanged = (current == nullptr) != (before == nullptr);
            const bool stateChanged = current && before && HasMeaningfulChange(*before, *current);
            if (presenceChanged || stateChanged) {
                PauseAt(index - 1);
                return;
            }
        }
    } else {
        for (std::size_t index = cursor_ + 1; index < frames_.size(); ++index) {
            const ObjectSnapshot* current = FindObjectSnapshot(frames_[index], selectedReplayId_);
            const ObjectSnapshot* before = FindObjectSnapshot(frames_[index - 1], selectedReplayId_);
            const bool presenceChanged = (current == nullptr) != (before == nullptr);
            const bool stateChanged = current && before && HasMeaningfulChange(*before, *current);
            if (presenceChanged || stateChanged) {
                PauseAt(index);
                return;
            }
        }
    }
    statusMessage_ = "この方向には選択Objectの変化がありません。";
}

bool ReplayDebugger::HasMeaningfulChange(const ObjectSnapshot& lhs, const ObjectSnapshot& rhs) const {
    constexpr float kTransformEpsilonSquared = 0.000001f;
    if (lhs.state.replayRemoved != rhs.state.replayRemoved ||
        lhs.state.dead != rhs.state.dead ||
        lhs.state.visible != rhs.state.visible ||
        lhs.state.animationName != rhs.state.animationName) {
        return true;
    }
    if (DistanceSquared(lhs.state.translation, rhs.state.translation) > kTransformEpsilonSquared ||
        DistanceSquared(lhs.state.rotation, rhs.state.rotation) > kTransformEpsilonSquared ||
        DistanceSquared(lhs.state.scale, rhs.state.scale) > kTransformEpsilonSquared) {
        return true;
    }
    if (lhs.state.hasParameter != rhs.state.hasParameter) return true;
    if (lhs.state.hasParameter &&
        std::abs(lhs.state.parameter.hp - rhs.state.parameter.hp) > 0.001f) {
        return true;
    }
    return false;
}

bool ReplayDebugger::HasMeaningfulChange(const SpriteSnapshot& lhs, const SpriteSnapshot& rhs) const {
    constexpr float kEpsilonSquared = 0.000001f;
    if (lhs.state.replayRemoved != rhs.state.replayRemoved ||
        lhs.state.visible != rhs.state.visible ||
        lhs.state.textureHandle != rhs.state.textureHandle ||
        lhs.state.parentReplayId != rhs.state.parentReplayId ||
        lhs.state.animationPlaying != rhs.state.animationPlaying ||
        lhs.state.currentFrame != rhs.state.currentFrame) {
        return true;
    }
    if (DistanceSquared(lhs.state.position, rhs.state.position) > kEpsilonSquared ||
        DistanceSquared(lhs.state.size, rhs.state.size) > kEpsilonSquared ||
        std::abs(lhs.state.rotation - rhs.state.rotation) > 0.001f ||
        std::abs(lhs.state.animationTimer - rhs.state.animationTimer) > 0.001f ||
        std::abs(lhs.state.emissive - rhs.state.emissive) > 0.001f) {
        return true;
    }
    const Vector4& a = lhs.state.color;
    const Vector4& b = rhs.state.color;
    return std::abs(a.x - b.x) > 0.001f ||
        std::abs(a.y - b.y) > 0.001f ||
        std::abs(a.z - b.z) > 0.001f ||
        std::abs(a.w - b.w) > 0.001f;
}

const ReplayDebugger::ObjectSnapshot* ReplayDebugger::FindObjectSnapshot(const FrameSnapshot& frame, uint64_t replayId) const {
    const auto found = std::find_if(frame.objects.begin(), frame.objects.end(), [replayId](const ObjectSnapshot& snapshot) {
        return snapshot.replayId == replayId;
    });
    return found != frame.objects.end() ? &*found : nullptr;
}

const ReplayDebugger::SpriteSnapshot* ReplayDebugger::FindSpriteSnapshot(const FrameSnapshot& frame, uint64_t replayId) const {
    const auto found = std::find_if(frame.sprites.begin(), frame.sprites.end(), [replayId](const SpriteSnapshot& snapshot) {
        return snapshot.replayId == replayId;
    });
    return found != frame.sprites.end() ? &*found : nullptr;
}

const ReplayDebugger::ObjectSnapshot* ReplayDebugger::FindPreviousObjectSnapshot(uint64_t replayId) const {
    if (cursor_ == 0 || cursor_ >= frames_.size()) return nullptr;
    return FindObjectSnapshot(frames_[cursor_ - 1], replayId);
}

const ReplayDebugger::ObjectSnapshot* ReplayDebugger::FindLatestObjectSnapshot(uint64_t replayId) const {
    if (frames_.empty()) return nullptr;
    return FindObjectSnapshot(frames_.back(), replayId);
}

const char* ReplayDebugger::FormatBytes(std::size_t bytes, char* buffer, std::size_t bufferSize) {
    if (!buffer || bufferSize == 0) return "";
    if (bytes >= 1024ull * 1024ull) {
        sprintf_s(buffer, bufferSize, "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ull) {
        sprintf_s(buffer, bufferSize, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        sprintf_s(buffer, bufferSize, "%zu B", bytes);
    }
    return buffer;
}

#endif
