#include "AnimationWorkbench.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

// シークでは未登録の姿勢を解除し、表示モデルとInspectorを同じ時刻へ揃える。
void AnimationWorkbench::SeekTimeline(float time) {
    currentTime_ = std::clamp(time, 0.0f, (std::max)(0.01f, duration_));
    play_ = false;
    bodyEditOverrideActive_ = false;
    ClearAllEditOverrides();
    ApplyTimelinePose();
    SyncBodyUiFromTimeline();
    if (!editBodyTransform_ && selectedJointIndex_ >= 0) SyncUiFromJoint(selectedJointIndex_);
}

void AnimationWorkbench::DrawTimelineControls() {
#ifdef USE_IMGUI
    if (ImGui::Button(play_ ? "停止" : "再生")) {
        if (!play_ && currentTime_ >= duration_) SeekTimeline(0.0f);
        bodyEditOverrideActive_ = false;
        ClearAllEditOverrides();
        play_ = !play_;
    }
    ImGui::SameLine();
    if (ImGui::Button("先頭")) SeekTimeline(0.0f);
    ImGui::SameLine();
    if (ImGui::Button("-1F")) SeekTimeline(currentTime_ - 1.0f / timelineFps_);
    ImGui::SameLine();
    if (ImGui::Button("+1F")) SeekTimeline(currentTime_ + 1.0f / timelineFps_);
    ImGui::SameLine();
    ImGui::Checkbox("ループ", &loop_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::DragFloat("速度", &playbackSpeed_, 0.05f, 0.05f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::Text("%.3f 秒 / %d F", currentTime_, static_cast<int>(std::round(currentTime_ * timelineFps_)));

    ImGui::SetNextItemWidth(110);
    if (ImGui::DragFloat("長さ（秒）", &duration_, 0.05f, 0.05f, 600.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        // 既存キーを範囲外へ隠す短縮は行わない。
        for (const auto& key : bodyKeys_) duration_ = (std::max)(duration_, key.time);
        for (const auto& key : keys_) duration_ = (std::max)(duration_, key.time);
        for (const auto& key : events_) duration_ = (std::max)(duration_, key.time);
        SeekTimeline(currentTime_);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::BeginCombo("FPS", timelineFps_ == 30 ? "30" : "60")) {
        if (ImGui::Selectable("30", timelineFps_ == 30)) timelineFps_ = 30;
        if (ImGui::Selectable("60", timelineFps_ == 60)) timelineFps_ = 60;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("フレーム吸着", &timelineSnap_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("拡大", &timelineZoom_, 30.0f, 1200.0f, "%.0f px/s");
    ImGui::SameLine();
    if (ImGui::Button("全体表示")) timelineZoom_ = std::clamp((ImGui::GetWindowWidth() - 220.0f) / (std::max)(0.05f, duration_), 1.0f, 1200.0f);
#endif
}

void AnimationWorkbench::DrawTimelinePanel() {
#ifdef USE_IMGUI
    if (!ImGui::Begin("アニメーション制作 - Timeline", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("時間軸で選択 → 右側で姿勢を編集 → キー登録 → 再生・保存");
    DrawTimelineControls();
    ImGui::BeginDisabled(!previewModel_ || (!editBodyTransform_ && selectedJointIndex_ < 0));
    if (ImGui::Button("現在の姿勢をキー登録")) {
        if (editBodyTransform_) AddOrUpdateBodyKey(); else AddOrUpdateKey();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("クリップ保存")) { SaveAuthoringJson(); RefreshAssetFiles(); }
    ImGui::SameLine();
    if (ImGui::Button("カメラを対象へ")) recenterCameraRequested_ = true;
    ImGui::TextDisabled("空白をドラッグ：時刻移動 / 菱形をドラッグ：キー移動 / Esc：移動取消 / Space：再生 / 左右：1F");
    if (!previewModel_) ImGui::TextWrapped("右側でモデル、または保存済みクリップを読み込んでください。グリッド1区画は1mです。");

    const bool keyboard = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
    if (keyboard && timelineDragKind_ < 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            if (!play_ && currentTime_ >= duration_) SeekTimeline(0);
            bodyEditOverrideActive_ = false;
            ClearAllEditOverrides();
            play_ = !play_;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) SeekTimeline(currentTime_ - 1.0f / timelineFps_);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) SeekTimeline(currentTime_ + 1.0f / timelineFps_);
    }

    // キーを持つボーンと現在編集中のボーンを独立した行で表示する。
    std::vector<int> joints;
    for (const auto& key : keys_) {
        if (std::find(joints.begin(), joints.end(), key.jointIndex) == joints.end()) joints.push_back(key.jointIndex);
    }
    if (selectedJointIndex_ >= 0 && std::find(joints.begin(), joints.end(), selectedJointIndex_) == joints.end()) joints.push_back(selectedJointIndex_);
    std::sort(joints.begin(), joints.end());
    constexpr float labelWidth = 180.0f, rowHeight = 32.0f, rulerHeight = 28.0f;
    const int rows = 2 + static_cast<int>(joints.size());
    ImGui::BeginChild("AnimationTracks", {0, 0}, true, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = (std::max)(ImGui::GetContentRegionAvail().x, labelWidth + duration_ * timelineZoom_ + 40);
    const float height = (std::max)(ImGui::GetContentRegionAvail().y, rulerHeight + rows * rowHeight);
    ImGui::InvisibleButton("TrackCanvas", {width, height});
    const bool clicked = ImGui::IsItemClicked();
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    const auto mouse = ImGui::GetIO().MousePos;
    auto* draw = ImGui::GetWindowDrawList();
    const float timeOrigin = origin.x + labelWidth;
    auto screenX = [&](float time) { return timeOrigin + time * timelineZoom_; };
    auto quantize = [&](float time) {
        if (timelineSnap_) time = std::round(time * timelineFps_) / timelineFps_;
        return std::clamp(time, 0.0f, duration_);
    };
    draw->AddRectFilled(origin, {origin.x + width, origin.y + height}, IM_COL32(26, 29, 36, 255));
    for (int row = 0; row < rows; ++row) {
        const float y = origin.y + rulerHeight + row * rowHeight;
        draw->AddRectFilled({origin.x, y}, {origin.x + width, y + rowHeight}, row % 2 ? IM_COL32(39,43,53,255) : IM_COL32(33,37,46,255));
        std::string label = row == 0 ? "身体全体" : "イベント";
        if (row > 0 && row < rows - 1) {
            const int joint = joints[row - 1];
            label = "ボーン " + std::to_string(joint);
            if (previewModel_ && joint >= 0 && joint < static_cast<int>(previewModel_->GetJoints().size())) label = previewModel_->GetJoints()[joint].name;
        }
        draw->PushClipRect({origin.x,y}, {timeOrigin-4,y+rowHeight}, true);
        draw->AddText({origin.x+8,y+8}, IM_COL32(215,221,235,255), label.c_str());
        draw->PopClipRect();
        if (clicked && mouse.x < timeOrigin && mouse.y >= y && mouse.y < y + rowHeight) {
            if (row == 0) editBodyTransform_ = true;
            else if (row < rows - 1) { editBodyTransform_ = false; selectedJointIndex_ = joints[row-1]; SyncUiFromJoint(selectedJointIndex_); }
        }
    }
    // 可視範囲だけ目盛りを描画し、長いクリップでも描画負荷を一定にする。
    const float step = std::pow(2.0f, std::ceil(std::log2(75.0f / (std::max)(1.0f,timelineZoom_))));
    const float visibleStart = (std::max)(0.0f, (ImGui::GetScrollX()-labelWidth) / timelineZoom_);
    const float visibleEnd = (std::min)(duration_, (ImGui::GetScrollX()+ImGui::GetWindowWidth()) / timelineZoom_);
    for (float t = std::floor(visibleStart / step) * step; t <= visibleEnd; t += step) {
        const float x = screenX(t);
        draw->AddLine({x,origin.y}, {x,origin.y+height}, IM_COL32(68,73,86,255));
        char text[48]; std::snprintf(text,sizeof(text),"%.2fs",t);
        draw->AddText({x+4,origin.y+4}, IM_COL32(181,189,204,255), text);
    }
    bool hit = false;
    auto point = [&](int kind, int index, float time, int row, ImU32 color) {
        const ImVec2 p{screenX(time), origin.y+rulerHeight+(row+0.5f)*rowHeight};
        const bool isNearKey = hovered && std::abs(mouse.x-p.x)<9 && std::abs(mouse.y-p.y)<12;
        const bool selected = std::abs(currentTime_-time)<0.001f &&
            (kind == 0 ? editBodyTransform_ : kind == 1 ? !editBodyTransform_ && selectedJointIndex_ == keys_[index].jointIndex : selectedEventIndex_ == index);
        draw->AddQuadFilled({p.x,p.y-6},{p.x+6,p.y},{p.x,p.y+6},{p.x-6,p.y},selected ? IM_COL32(255,255,255,255) : color);
        if (isNearKey) ImGui::SetTooltip("%.3f 秒 / %d F\nドラッグして時間を移動",time,static_cast<int>(std::round(time*timelineFps_)));
        if (clicked && isNearKey && !hit) {
            hit = true;
            if (kind == 0) { editBodyTransform_=true; selectedBodyKeyIndex_=index; }
            if (kind == 1) { editBodyTransform_=false; selectedJointIndex_=keys_[index].jointIndex; }
            if (kind == 2) { selectedEventIndex_=index; eventType_=events_[index].type; eventOffsetUi_=events_[index].offset; strncpy_s(eventNameBuffer_,events_[index].name.c_str(),_TRUNCATE); }
            SeekTimeline(time);
            timelineDragKind_=kind; timelineDragIndex_=index;
            timelineDragTime_=time; timelineDragMouseX_=mouse.x;
        }
    };
    for (int i=0;i<static_cast<int>(bodyKeys_.size());++i) point(0,i,bodyKeys_[i].time,0,IM_COL32(98,210,240,255));
    for (int i=0;i<static_cast<int>(keys_.size());++i) {
        const int row=1+static_cast<int>(std::find(joints.begin(),joints.end(),keys_[i].jointIndex)-joints.begin());
        point(1,i,keys_[i].time,row,IM_COL32(192,155,245,255));
    }
    for (int i=0;i<static_cast<int>(events_.size());++i) point(2,i,events_[i].time,rows-1,IM_COL32(244,193,93,255));
    if (timelineDragKind_ >= 0) {
        const float target = std::abs(mouse.x-timelineDragMouseX_) < 3.0f ? timelineDragTime_ :
            quantize(timelineDragTime_ + (mouse.x-timelineDragMouseX_)/timelineZoom_);
        draw->AddLine({screenX(target),origin.y},{screenX(target),origin.y+height},IM_COL32(255,218,120,255),2);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape,false)) { timelineDragKind_ = -2; timelineDragIndex_ = -1; }
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // ドラッグ中には配列を並べ替えない。既存キーへの重なりは拒否し、上書き消失を防ぐ。
            const int index=timelineDragIndex_, kind=timelineDragKind_;
            bool collision=false;
            if (kind == 0 && index < static_cast<int>(bodyKeys_.size())) {
                for (int i=0;i<static_cast<int>(bodyKeys_.size());++i) if (i!=index && std::abs(bodyKeys_[i].time-target)<0.001f) collision=true;
                if (!collision) {
                    bodyKeys_[index].time=target;
                    SortBodyKeys();
                    for (int i=0;i<static_cast<int>(bodyKeys_.size());++i)
                        if (std::abs(bodyKeys_[i].time-target)<0.001f) selectedBodyKeyIndex_=i;
                }
            } else if (kind == 1 && index < static_cast<int>(keys_.size())) {
                for (int i=0;i<static_cast<int>(keys_.size());++i) if (i!=index && keys_[i].jointIndex==keys_[index].jointIndex && std::abs(keys_[i].time-target)<0.001f) collision=true;
                if (!collision) { keys_[index].time=target; SortKeys(); }
            } else if (kind == 2 && index < static_cast<int>(events_.size())) {
                events_[index].time=target;
            }
            SeekTimeline(collision ? timelineDragTime_ : target);
            timelineDragKind_ = timelineDragIndex_ = -1;
        }
    } else if (timelineDragKind_ == -1 && active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && mouse.x >= timeOrigin && !hit) {
        SeekTimeline(quantize((mouse.x-timeOrigin)/timelineZoom_));
    }
    if (timelineDragKind_ == -2 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) timelineDragKind_ = -1;
    const float playhead=screenX(currentTime_);
    draw->AddLine({playhead,origin.y},{playhead,origin.y+height},IM_COL32(255,107,120,255),2);
    draw->AddTriangleFilled({playhead-6,origin.y},{playhead+6,origin.y},{playhead,origin.y+10},IM_COL32(255,107,120,255));
    ImGui::EndChild();
    ImGui::End();
#endif
}
