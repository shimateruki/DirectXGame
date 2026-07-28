#include "VFXSequencerEditor.h"
#include "BaseScene.h"
#include "CameraManager.h"
#include "EditorManager.h"
#include "EffectPreviewStage.h"
#include "GPUParticleManager.h"
#include "IconsFontAwesome5.h"
#include "MeshEffectManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <imgui.h>

namespace fs = std::filesystem;

namespace {
    ImVec4 GetEventColor(VFXEventType type) {
        switch (type) {
        case VFXEventType::GPUParticle:
            return ImVec4(1.0f, 0.55f, 0.12f, 1.0f);
        case VFXEventType::MovingParticle:
            return ImVec4(1.0f, 0.32f, 0.08f, 1.0f);
        case VFXEventType::MeshEffect:
            return ImVec4(0.10f, 0.72f, 1.0f, 1.0f);
        case VFXEventType::SoundEffect:
            return ImVec4(0.32f, 1.0f, 0.45f, 1.0f);
        case VFXEventType::CameraShake:
            return ImVec4(0.92f, 0.92f, 0.30f, 1.0f);
        case VFXEventType::LightPulse:
            return ImVec4(1.0f, 0.82f, 0.28f, 1.0f);
        default:
            return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        }
    }

    ImU32 ToU32(const ImVec4& color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    float GetEventEndTime(const VFXEvent& event) {
        if (event.type == VFXEventType::MovingParticle ||
            event.type == VFXEventType::CameraShake ||
            event.type == VFXEventType::LightPulse) {
            return event.triggerTime + (std::max)(event.duration, 0.01f);
        }
        return event.triggerTime + 0.08f;
    }
}

void VFXSequencerEditor::Initialize() {
    previewSequencer_.Initialize(nullptr);
    RefreshFileList();
}

Vector3 VFXSequencerEditor::ResolvePreviewPosition() const {
    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    if (previewStage && previewStage->IsEnabled()) {
        return previewStage->GetPreviewPosition();
    }

    if (isPreviewMode_) {
        const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
        if (camera) {
            Vector3 camEye = camera->GetEye();
            Vector3 camTarget = camera->GetTargetPoint();
            Vector3 dir = {
                camTarget.x - camEye.x,
                camTarget.y - camEye.y,
                camTarget.z - camEye.z
            };
            float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            Vector3 forward = { 0.0f, 0.0f, 1.0f };
            if (length > 0.0001f) {
                forward = { dir.x / length, dir.y / length, dir.z / length };
            }
            return {
                camEye.x + forward.x * previewDistance_,
                camEye.y + forward.y * previewDistance_,
                camEye.z + forward.z * previewDistance_
            };
        }
    }

    return { 0.0f, 0.0f, 0.0f };
}

void VFXSequencerEditor::PlayPreview() {
    previewSequencer_.SetRootPosition(ResolvePreviewPosition());
    previewSequencer_.SetRootScale({ previewRootScale_, previewRootScale_, previewRootScale_ });
    previewSequencer_.Play();
}

void VFXSequencerEditor::Update(float deltaTime) {
    float timeStep = deltaTime;
    if (timeStep <= 0.0001f) {
        timeStep = 1.0f / 60.0f;
    }

    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    const bool isThisEditorSelected = EditorManager::GetInstance()->GetSelectedObject() == this;
    const bool usePreviewStage = previewStage && previewStage->IsEnabled() && isThisEditorSelected;
    GPUParticleManager* gpuManager = GPUParticleManager::GetInstance();
    MeshEffectManager* meshManager = MeshEffectManager::GetInstance();

    auto restartPreviewAt = [&](float seekTime) {
        previewSequencer_.Stop();
        meshManager->ClearActiveEffects();
        gpuManager->ResetSimulation();
        PlayPreview();

        const float targetTime = std::clamp(seekTime, 0.0f, (std::max)(previewSequencer_.GetDuration(), 0.0f));
        const float previousScale = gpuManager->GetTimeScale();
        gpuManager->SetTimeScale(1.0f);
        float simulatedTime = 0.0f;
        while (simulatedTime + (1.0f / 60.0f) < targetTime) {
            previewSequencer_.Update(1.0f / 60.0f);
            meshManager->UpdateEditorPreviewStep(1.0f / 60.0f);
            gpuManager->UpdateEditorPreviewStep(1.0f / 60.0f);
            simulatedTime += 1.0f / 60.0f;
        }
        if (targetTime > simulatedTime) {
            const float remainder = targetTime - simulatedTime;
            previewSequencer_.Update(remainder);
            meshManager->UpdateEditorPreviewStep(remainder);
            gpuManager->UpdateEditorPreviewStep(remainder);
        }
        gpuManager->SetTimeScale(previousScale);
    };

    float particlePreviewScale = previewPlaybackSpeed_;
    if (usePreviewStage) {
        particlePreviewScale *= previewStage->GetPlaybackSpeed();
        timeStep *= previewStage->GetPlaybackSpeed();
        if (previewStage->GetPlayRequestSerial() != lastStagePlayRequestSerial_) {
            lastStagePlayRequestSerial_ = previewStage->GetPlayRequestSerial();
            restartPreviewAt(0.0f);
        }
        if (previewStage->GetStopRequestSerial() != lastStageStopRequestSerial_) {
            lastStageStopRequestSerial_ = previewStage->GetStopRequestSerial();
            previewSequencer_.Stop();
            meshManager->ClearActiveEffects();
            gpuManager->ResetSimulation();
        }
        if (previewStage->GetSeekRequestSerial() != lastStageSeekRequestSerial_) {
            lastStageSeekRequestSerial_ = previewStage->GetSeekRequestSerial();
            restartPreviewAt(previewStage->GetSeekTargetTime());
        }
        if (previewStage->IsLoopEnabled() && previewStage->IsTransportPlaying() &&
            !previewSequencer_.IsPlaying() && !previewSequencer_.GetEvents().empty()) {
            restartPreviewAt(0.0f);
        }
    }
    timeStep *= previewPlaybackSpeed_;
    if (isThisEditorSelected) {
        gpuManager->SetTimeScale((std::max)(0.0f, particlePreviewScale));
    }

    const bool previewPaused = isThisEditorSelected && particlePreviewScale <= 0.0001f;
    if (!previewPaused) {
        previewSequencer_.Update(timeStep);
    }

    bool isGamePlaying = false;
    if (SceneManager::GetInstance()) {
        isGamePlaying = SceneManager::GetInstance()->IsPlaying();
    }

    if (!isGamePlaying && isThisEditorSelected) {
        meshManager->Update(previewPaused ? 0.0f : timeStep);
        gpuManager->Update(deltaTime);
    }

    if (usePreviewStage) {
        const float duration = (std::max)(previewSequencer_.GetDuration(), 0.1f);
        std::vector<EffectPreviewStage::TimelineEvent> timelineEvents;
        timelineEvents.reserve(previewSequencer_.GetEvents().size());
        for (const VFXEvent& event : previewSequencer_.GetEvents()) {
            const ImVec4 color = GetEventColor(event.type);
            timelineEvents.push_back({
                GetEventTypeName(event.type),
                event.triggerTime,
                (std::min)(GetEventEndTime(event), duration),
                Vector4{ color.x, color.y, color.z, color.w }
            });
        }
        previewStage->ReportToolState(
            EffectPreviewStage::ToolKind::VfxSequence,
            "VFX Sequence",
            std::clamp(previewSequencer_.GetCurrentTime(), 0.0f, duration),
            duration,
            previewStage->IsTransportPlaying(),
            static_cast<int>(meshManager->GetActiveEffects().size()) + gpuManager->GetActiveSystemCount(),
            timelineEvents);
    }
}

void VFXSequencerEditor::RefreshFileList() {
    particlePresetList_.clear();
    sequenceFileList_.clear();
    meshEffectList_.clear();
    seFileList_.clear();

    const std::string particleDir = "Resources/json/gpu_particles/";
    if (GPUParticleManager::GetInstance() && GPUParticleManager::GetInstance()->IsInitialized()) {
        GPUParticleManager::GetInstance()->ReloadAllPresets(particleDir);
    }
    if (fs::exists(particleDir)) {
        for (const auto& entry : fs::directory_iterator(particleDir)) {
            if (entry.path().extension() == ".json") {
                particlePresetList_.push_back(entry.path().stem().string());
            }
        }
    }

    const std::string meshDir = "Resources/json/effect/";
    if (fs::exists(meshDir)) {
        for (const auto& entry : fs::directory_iterator(meshDir)) {
            if (entry.path().extension() == ".json") {
                meshEffectList_.push_back(entry.path().stem().string());
            }
        }
    }

    const std::string sequenceDir = "Resources/json/vfx_sequence/";
    if (fs::exists(sequenceDir)) {
        for (const auto& entry : fs::directory_iterator(sequenceDir)) {
            if (entry.path().extension() == ".json") {
                sequenceFileList_.push_back(entry.path().stem().string());
            }
        }
    }

    const std::string seDir = "Resources/audio/se/";
    if (fs::exists(seDir)) {
        for (const auto& entry : fs::directory_iterator(seDir)) {
            const auto ext = entry.path().extension();
            if (ext == ".wav" || ext == ".mp3") {
                seFileList_.push_back(entry.path().filename().string());
            }
        }
    }

    std::sort(particlePresetList_.begin(), particlePresetList_.end());
    std::sort(sequenceFileList_.begin(), sequenceFileList_.end());
    std::sort(meshEffectList_.begin(), meshEffectList_.end());
    std::sort(seFileList_.begin(), seFileList_.end());
}

void VFXSequencerEditor::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_FILM " VFX Cue Editor");
    ImGui::TextDisabled("パーティクル、メッシュ、SE、カメラ揺れ、画面演出を時間差で組み合わせます。");

    if (ImGui::Button(ICON_FA_SYNC " リスト更新", ImVec2(140, 0))) {
        RefreshFileList();
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_FA_EYE " Preview Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (EffectPreviewStage::GetInstance()->IsEnabled()) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.7f, 1.0f), "Effect Preview Stage active");
        }
        ImGui::Checkbox("Spawn in front of Camera", &isPreviewMode_);
        if (isPreviewMode_) {
            ImGui::Indent();
            ImGui::DragFloat("Distance", &previewDistance_, 0.1f, 1.0f, 50.0f);
            ImGui::Unindent();
        }
        ImGui::DragFloat("Root Scale", &previewRootScale_, 0.05f, 0.1f, 5.0f, "%.2f");
        ImGui::DragFloat(ICON_FA_CLOCK " Preview Speed", &previewPlaybackSpeed_, 0.05f, 0.0f, 5.0f, "%.2f");
        if (ImGui::Button("1/4 Speed")) {
            previewPlaybackSpeed_ = 0.25f;
        }
        ImGui::SameLine();
        if (ImGui::Button("1/2 Speed")) {
            previewPlaybackSpeed_ = 0.5f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Normal Speed")) {
            previewPlaybackSpeed_ = 1.0f;
        }
    }

    ImGui::Separator();

    if (ImGui::Button(ICON_FA_PLAY " 再生", ImVec2(110, 30))) {
        PlayPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " 停止", ImVec2(110, 30))) {
        previewSequencer_.Stop();
    }
    ImGui::SameLine();
    if (previewSequencer_.IsPlaying()) {
        ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), ICON_FA_PLAY_CIRCLE " 再生中");
    }
    else {
        ImGui::TextDisabled(ICON_FA_STOP_CIRCLE " 停止中");
    }
    ImGui::TextDisabled("Active Mesh Effects: %d", static_cast<int>(MeshEffectManager::GetInstance()->GetActiveEffects().size()));
    if (ImGui::Button("メッシュ確認")) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            "Resources/json/effect/effect_hitfx_kickpunch_center_flash.json",
            ResolvePreviewPosition(),
            { 1.5707963f, 0.0f, 0.0f },
            { previewRootScale_, previewRootScale_, previewRootScale_ });
    }

    DrawTimelinePreview();

    ImGui::SeparatorText("イベント追加");
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth = (std::max)((ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f, 80.0f);

    if (ImGui::Button(ICON_FA_FIRE " パーティクル", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VFXEventType::GPUParticle);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROUTE " 軌跡", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VFXEventType::MovingParticle);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CUBE " メッシュ", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VFXEventType::MeshEffect);
    }

    if (ImGui::Button(ICON_FA_MUSIC " SE", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VFXEventType::SoundEffect);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CAMERA " カメラ揺れ", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VFXEventType::CameraShake);
    }
    if (ImGui::Button("Light", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VFXEventType::LightPulse);
    }

    ImGui::SeparatorText("イベント一覧");
    auto& events = previewSequencer_.GetEvents();
    for (int i = 0; i < static_cast<int>(events.size()); ++i) {
        DrawEventEditor(i, events[i]);
    }

    ImGui::SeparatorText("保存と読み込み");
    if (ImGui::BeginCombo(ICON_FA_FOLDER_OPEN " 既存ファイル", sequenceNameInput_)) {
        for (const auto& seqName : sequenceFileList_) {
            const bool isSelected = (std::string(sequenceNameInput_) == seqName);
            if (ImGui::Selectable(seqName.c_str(), isSelected)) {
                strcpy_s(sequenceNameInput_, sizeof(sequenceNameInput_), seqName.c_str());
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::InputText(ICON_FA_FILE_SIGNATURE " 保存名", sequenceNameInput_, sizeof(sequenceNameInput_));

    if (ImGui::Button(ICON_FA_DOWNLOAD " 保存", ImVec2(120, 0))) {
        previewSequencer_.Save(sequenceNameInput_);
        RefreshFileList();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UPLOAD " 読み込み", ImVec2(120, 0))) {
        previewSequencer_.Load(sequenceNameInput_);
    }
#endif
}

void VFXSequencerEditor::DrawTimelinePreview() {
#ifdef USE_IMGUI
    auto& events = previewSequencer_.GetEvents();
    float maxTime = 1.0f;
    for (const auto& event : events) {
        maxTime = (std::max)(maxTime, GetEventEndTime(event));
    }
    maxTime = (std::max)(maxTime, 3.0f);

    ImGui::SeparatorText("タイムライン");
    const float width = (std::max)(ImGui::GetContentRegionAvail().x, 240.0f);
    const float rowHeight = 20.0f;
    const float headerHeight = 22.0f;
    const float height = headerHeight + (std::max)(1, static_cast<int>(events.size())) * rowHeight + 10.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("##VFXCueTimeline", ImVec2(width, height));

    const ImU32 bgColor = IM_COL32(28, 31, 36, 255);
    const ImU32 lineColor = IM_COL32(95, 100, 110, 160);
    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), bgColor, 4.0f);
    drawList->AddRect(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(120, 130, 145, 180), 4.0f);

    for (int second = 0; second <= static_cast<int>(maxTime + 0.5f); ++second) {
        const float x = origin.x + (static_cast<float>(second) / maxTime) * width;
        drawList->AddLine(ImVec2(x, origin.y + headerHeight), ImVec2(x, origin.y + height - 4.0f), lineColor);
        char label[16];
        sprintf_s(label, "%.0fs", static_cast<float>(second));
        drawList->AddText(ImVec2(x + 4.0f, origin.y + 4.0f), IM_COL32(210, 215, 225, 255), label);
    }

    for (int i = 0; i < static_cast<int>(events.size()); ++i) {
        const VFXEvent& event = events[i];
        const float startX = origin.x + (event.triggerTime / maxTime) * width;
        const float endX = origin.x + (GetEventEndTime(event) / maxTime) * width;
        const float y = origin.y + headerHeight + i * rowHeight + 3.0f;
        const ImVec4 color = GetEventColor(event.type);
        drawList->AddRectFilled(ImVec2(startX, y), ImVec2((std::max)(endX, startX + 8.0f), y + 14.0f), ToU32(color), 3.0f);

        char label[64];
        sprintf_s(label, "%02d %s", i, GetEventTypeName(event.type));
        drawList->AddText(ImVec2(startX + 4.0f, y - 1.0f), IM_COL32(245, 245, 245, 255), label);
    }
#endif
}

void VFXSequencerEditor::DrawEventEditor(int index, VFXEvent& event) {
#ifdef USE_IMGUI
    auto& events = previewSequencer_.GetEvents();
    ImGui::PushID(index);

    const ImVec4 color = GetEventColor(event.type);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const bool open = ImGui::TreeNodeEx("Event", ImGuiTreeNodeFlags_DefaultOpen, "%02d  %s", index, GetEventTypeName(event.type));
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_TRASH_ALT)) {
        events.erase(events.begin() + index);
        ImGui::PopID();
        return;
    }

    if (open) {
        ImGui::DragFloat(ICON_FA_STOPWATCH " 発火時間", &event.triggerTime, 0.02f, 0.0f, 120.0f, "%.2f s");

        if (event.type == VFXEventType::GPUParticle) {
            DrawPresetCombo("プリセット", event.presetName, particlePresetList_);
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " オフセット", &event.offset.x, 0.1f);
        }
        else if (event.type == VFXEventType::MovingParticle) {
            DrawPresetCombo("プリセット", event.presetName, particlePresetList_);
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 開始オフセット", &event.offset.x, 0.1f);
            ImGui::DragFloat3(ICON_FA_ROUTE " 中間点", &event.controlPoint.x, 0.1f);
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 終点オフセット", &event.endOffset.x, 0.1f);
            ImGui::DragFloat(ICON_FA_STOPWATCH " 移動時間", &event.duration, 0.02f, 0.05f, 30.0f, "%.2f s");
            ImGui::DragInt(ICON_FA_SLIDERS_H " イージング", &event.easingType, 1.0f, 0, 30);
        }
        else if (event.type == VFXEventType::MeshEffect) {
            DrawPresetCombo("エフェクト", event.presetName, meshEffectList_);
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " オフセット", &event.offset.x, 0.1f);
            ImGui::DragFloat3(ICON_FA_SYNC_ALT " 回転", &event.rotation.x, 0.05f);
            ImGui::DragFloat3(ICON_FA_EXPAND_ARROWS_ALT " スケール", &event.scale.x, 0.05f);
        }
        else if (event.type == VFXEventType::SoundEffect) {
            DrawPresetCombo("SE", event.presetName, seFileList_);
        }
        else if (event.type == VFXEventType::CameraShake) {
            ImGui::DragFloat(ICON_FA_STOPWATCH " 揺れ時間", &event.duration, 0.01f, 0.03f, 5.0f, "%.2f s");
            ImGui::DragFloat(ICON_FA_BOLT " 強さ", &event.intensity, 0.01f, 0.0f, 5.0f, "%.2f");
            ImGui::DragFloat(ICON_FA_WAVE_SQUARE " 周波数", &event.frequency, 0.5f, 1.0f, 80.0f, "%.1f");
            ImGui::DragFloat3(ICON_FA_SLIDERS_H " 軸ごとの重み", &event.scale.x, 0.02f, 0.0f, 2.0f);
            ImGui::TextDisabled("軸重みは X/Y/Z の揺れやすさです。Zを低めにすると画面酔いしにくくなります。");
        }
        else if (event.type == VFXEventType::LightPulse) {
            ImGui::DragFloat(ICON_FA_STOPWATCH " Duration", &event.duration, 0.01f, 0.03f, 5.0f, "%.2f s");
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " Offset", &event.offset.x, 0.1f);
            ImGui::DragFloat(ICON_FA_BOLT " Intensity", &event.intensity, 0.1f, 0.0f, 80.0f, "%.1f");
            ImGui::DragFloat("Radius", &event.lightRadius, 0.1f, 0.1f, 80.0f, "%.1f");
            ImGui::DragFloat("Decay", &event.lightDecay, 0.05f, 0.1f, 8.0f, "%.2f");
            ImGui::ColorEdit4("Color", &event.lightColor.x);
        }

        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::PopID();
#endif
}

void VFXSequencerEditor::DrawPresetCombo(const char* label, std::string& value, const std::vector<std::string>& list) {
#ifdef USE_IMGUI
    if (ImGui::BeginCombo(label, value.empty() ? "(なし)" : value.c_str())) {
        for (const auto& itemName : list) {
            const bool isSelected = (value == itemName);
            if (ImGui::Selectable(itemName.c_str(), isSelected)) {
                value = itemName;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
#endif
}

void VFXSequencerEditor::AddDefaultEvent(VFXEventType type) {
    VFXEvent event;
    event.type = type;
    event.triggerTime = 0.0f;
    event.presetName = GetEventTypeName(type);

    if (type == VFXEventType::GPUParticle) {
        event.presetName = particlePresetList_.empty() ? "" : particlePresetList_.front();
    }
    else if (type == VFXEventType::MovingParticle) {
        event.presetName = particlePresetList_.empty() ? "" : particlePresetList_.front();
        event.duration = 1.0f;
        event.controlPoint = { 0.0f, 4.0f, 0.0f };
        event.endOffset = { 0.0f, 0.0f, 8.0f };
    }
    else if (type == VFXEventType::MeshEffect) {
        event.presetName = meshEffectList_.empty() ? "" : meshEffectList_.front();
    }
    else if (type == VFXEventType::SoundEffect) {
        event.presetName = seFileList_.empty() ? "" : seFileList_.front();
    }
    else if (type == VFXEventType::CameraShake) {
        event.duration = 0.25f;
        event.intensity = 0.25f;
        event.frequency = 28.0f;
        event.scale = { 1.0f, 1.0f, 0.35f };
    }
    else if (type == VFXEventType::LightPulse) {
        event.duration = 0.22f;
        event.intensity = 22.0f;
        event.lightRadius = 11.0f;
        event.lightDecay = 1.35f;
        event.lightColor = { 1.0f, 0.58f, 0.12f, 1.0f };
    }

    previewSequencer_.GetEvents().push_back(event);
}

const char* VFXSequencerEditor::GetEventTypeName(VFXEventType type) const {
    switch (type) {
    case VFXEventType::GPUParticle:
        return "GPU Particle";
    case VFXEventType::MovingParticle:
        return "Moving Particle";
    case VFXEventType::MeshEffect:
        return "Mesh Effect";
    case VFXEventType::SoundEffect:
        return "Sound Effect";
    case VFXEventType::CameraShake:
        return "Camera Shake";
    case VFXEventType::PostEffectPulse:
        return "Post Effect Pulse";
    case VFXEventType::LightPulse:
        return "Light Pulse";
    default:
        return "Unknown";
    }
}
