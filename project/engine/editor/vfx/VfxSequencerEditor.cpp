#include "VfxSequencerEditor.h"
#include "BaseScene.h"
#include "CameraManager.h"
#include "EditorManager.h"
#include "EditorCommandRegistry.h"
#include "EffectPreviewStage.h"
#include "GpuParticleManager.h"
#include "IconsFontAwesome5.h"
#include "MeshEffectManager.h"
#include "Object3D.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <stdexcept>
#include <imgui.h>

namespace fs = std::filesystem;

namespace {
    ImVec4 GetEventColor(VfxEventType type) {
        // タイムライン上のイベント種別を色で区別できるようにする。
        switch (type) {
        case VfxEventType::GpuParticle:
            return ImVec4(1.0f, 0.55f, 0.12f, 1.0f);

        case VfxEventType::MovingParticle:
            return ImVec4(1.0f, 0.32f, 0.08f, 1.0f);

        case VfxEventType::MeshEffect:
            return ImVec4(0.10f, 0.72f, 1.0f, 1.0f);

        case VfxEventType::SoundEffect:
            return ImVec4(0.32f, 1.0f, 0.45f, 1.0f);

        case VfxEventType::CameraShake:
            return ImVec4(0.92f, 0.92f, 0.30f, 1.0f);

        case VfxEventType::PostEffectPulse:
            return ImVec4(0.92f, 0.35f, 1.0f, 1.0f);

        case VfxEventType::LightPulse:
            return ImVec4(1.0f, 0.82f, 0.28f, 1.0f);

        case VfxEventType::HitStop:
            return ImVec4(1.0f, 0.30f, 0.30f, 1.0f);

        case VfxEventType::ControllerRumble:
            return ImVec4(0.35f, 0.85f, 1.0f, 1.0f);

        case VfxEventType::CameraFovPulse:
            return ImVec4(0.75f, 0.58f, 1.0f, 1.0f);

        default:
            return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        }
    }

    ImU32 ToU32(const ImVec4& color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    float GetEventEndTime(const VfxEvent& event) {
        // 持続型イベントは設定した長さを、瞬間イベントは編集しやすい表示幅を使う。
        if (event.type == VfxEventType::MovingParticle ||
            event.type == VfxEventType::CameraShake ||
            event.type == VfxEventType::PostEffectPulse ||
            event.type == VfxEventType::LightPulse ||
            event.type == VfxEventType::HitStop ||
            event.type == VfxEventType::ControllerRumble ||
            event.type == VfxEventType::CameraFovPulse) {
            return event.triggerTime + (std::max)(event.duration, 0.01f);
        }

        return event.triggerTime + 0.08f;
    }
}

void VfxSequencerEditor::Initialize() {
    // プレビュー用Sequencerを初期化し、参照できるSequenceを収集します。
    previewSequencer_.Initialize(nullptr);
    RefreshFileList();
}

void VfxSequencerEditor::RequestOpenSequence(const std::string& name) {
    // 他画面からの選択要求を保留し、UI更新時に開くSequenceを切り替えます。
    pendingSequence_ = name;
    openRequested_ = true;
}

engine::Vector3 VfxSequencerEditor::ResolvePreviewPosition() const {
    // 専用Stageを優先し、通常画面ではカメラ基準の再生位置へ戻します。
    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();

    if (previewStage && previewStage->IsEnabled()) {
        return previewStage->GetPreviewPosition();
    }

    if (isPreviewMode_) {
        const engine::Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

        if (camera) {
            engine::Vector3 camEye = camera->GetEye();
            engine::Vector3 camTarget = camera->GetTargetPoint();
            engine::Vector3 dir = {
                camTarget.x - camEye.x,
                camTarget.y - camEye.y,
                camTarget.z - camEye.z
            };

            // 視線の長さを除いて向きだけを使う。指定距離は注視点までの倍率ではなく、視点からのワールド距離。
            float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            engine::Vector3 forward = { 0.0f, 0.0f, 1.0f };

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

void VfxSequencerEditor::PlayPreview() {
    // 位置と一括倍率を再生直前に渡し、編集した演出を同じ原点で試します。
    previewSequencer_.SetRootPosition(ResolvePreviewPosition());
    previewSequencer_.SetRootScale({ previewRootScale_, previewRootScale_, previewRootScale_ });
    previewSequencer_.Play();
}

void VfxSequencerEditor::Update(float deltaTime) {
    // StageのTransport、Editor固有の再生速度、実ゲームの更新有無を分けて扱う。
    float timeStep = deltaTime;

    if (timeStep <= 0.0001f) {
        timeStep = 1.0f / 60.0f;
    }

    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    const bool isThisEditorSelected = EditorManager::GetInstance()->GetSelectedObject() == this;
    const bool usePreviewStage = previewStage && previewStage->IsEnabled() && isThisEditorSelected;
    GpuParticleManager* gpuManager = GpuParticleManager::GetInstance();
    MeshEffectManager* meshManager = MeshEffectManager::GetInstance();

    auto restartPreviewAt = [&](float seekTime) {
        // 時間を戻す場合は生存中のMesh・GPU演出を消してから先頭から再発火させる。
        previewSequencer_.Stop();
        meshManager->ClearActiveEffects();
        gpuManager->ResetSimulation();
        PlayPreview();

        const float targetTime = std::clamp(seekTime, 0.0f, (std::max)(previewSequencer_.GetDuration(), 0.0f));
        // シーク時は速度倍率の影響を外して秒数を合わせ、再評価を終えたら元の倍率へ戻す。
        const float previousScale = gpuManager->GetTimeScale();
        gpuManager->SetTimeScale(1.0f);
        float simulatedTime = 0.0f;

        // SequencerとMeshはここで順に更新するが、GPU粒子は要求・時刻の蓄積だけを行う。
        // GPU側の経路積分や過去の深度衝突まで固定刻みで復元できるわけではない。
        while (simulatedTime + (1.0f / 60.0f) < targetTime) {
            previewSequencer_.Update(1.0f / 60.0f);
            meshManager->UpdateEditorPreviewStep(1.0f / 60.0f);
            gpuManager->UpdateEditorPreviewStep(1.0f / 60.0f);
            simulatedTime += 1.0f / 60.0f;
        }

        if (targetTime > simulatedTime) {
            // 最後の1コマ未満だけ個別に進め、指定時刻を超えて次の発火を実行しない。
            const float remainder = targetTime - simulatedTime;
            previewSequencer_.Update(remainder);
            meshManager->UpdateEditorPreviewStep(remainder);
            gpuManager->UpdateEditorPreviewStep(remainder);
        }

        gpuManager->SetTimeScale(previousScale);
    };

    float particlePreviewScale = previewPlaybackSpeed_;

    if (usePreviewStage) {
        // Play/Stop/Seekの要求シリアルを消化し、他ツールの操作を二重適用しない。
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

    // Stage と専用パネルの倍率の積がほぼ 0 なら Sequencer の時計を進めない。
    const bool previewPaused = isThisEditorSelected && particlePreviewScale <= 0.0001f;

    if (!previewPaused) {
        previewSequencer_.Update(timeStep);
    }

    bool isGamePlaying = false;

    if (SceneManager::GetInstance()) {
        isGamePlaying = SceneManager::GetInstance()->IsPlaying();
    }

    if (!isGamePlaying && isThisEditorSelected) {
        // 停止中のEditorではゲーム更新がManagerを進めないため、ここでプレビューを更新する。
        meshManager->Update(previewPaused ? 0.0f : timeStep);
        gpuManager->Update(deltaTime);
    }

    if (usePreviewStage) {
        // 各イベントの種別と発火区間をStageの共通タイムラインへ報告する。
        const float duration = (std::max)(previewSequencer_.GetDuration(), 0.1f);
        std::vector<EffectPreviewStage::TimelineEvent> timelineEvents;
        timelineEvents.reserve(previewSequencer_.GetEvents().size());

        for (const VfxEvent& event : previewSequencer_.GetEvents()) {
            const ImVec4 color = GetEventColor(event.type);
            timelineEvents.push_back({
                GetEventTypeName(event.type),
                event.triggerTime,
                (std::min)(GetEventEndTime(event), duration),
                engine::Vector4{ color.x, color.y, color.z, color.w }
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

void VfxSequencerEditor::RefreshFileList() {
    // GPU Particle・Mesh Effect・SE・Sequenceを再走査し、選択候補を更新します。
    particlePresetList_.clear();
    meshEffectList_.clear();
    seFileList_.clear();

    const std::string particleDir = "Resources/json/gpu_particles/";

    if (GpuParticleManager::GetInstance() && GpuParticleManager::GetInstance()->IsInitialized()) {
        GpuParticleManager::GetInstance()->ReloadAllPresets(particleDir);
    }

    const fs::path particleRoot = fs::path(particleDir).parent_path();
    if (fs::exists(particleRoot)) {
        for (const auto& entry : fs::recursive_directory_iterator(particleRoot)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                // 粒子プリセットの参照キーには素材ルートからの相対階層を残し、同名の別フォルダーも選べるようにする。
                auto relative = entry.path().lexically_relative(particleRoot);
                const auto key = relative.replace_extension().generic_u8string();
                particlePresetList_.emplace_back(key.begin(), key.end());
            }
        }
    }

    meshEffectList_ = JsonAssetBrowser::ListKeys("Resources/json/effect");

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
    const std::string audioEventDir = "Resources/json/audio_events/";

    if (fs::exists(audioEventDir)) {
        for (const auto& entry : fs::directory_iterator(audioEventDir)) {
            if (entry.path().extension() == ".json") {
                seFileList_.push_back(entry.path().filename().string());
            }
        }
    }

    std::sort(seFileList_.begin(), seFileList_.end());
}

void VfxSequencerEditor::DrawImGui() {
#ifdef USE_IMGUI
    // Preview操作、Event編集、JSON読込を一画面で行う。再生中はEvent配列を編集させない。
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
    DrawTimelinePreview();

    // 再生中のイベント配列変更は、発火状態と実行中の演出を不整合にします。
    ImGui::BeginDisabled(previewSequencer_.IsPlaying());
    ImGui::SeparatorText("イベント追加");
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth = (std::max)((ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f, 80.0f);

    if (ImGui::Button(ICON_FA_FIRE " パーティクル", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::GpuParticle);
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_ROUTE " 軌跡", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::MovingParticle);
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_CUBE " メッシュ", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::MeshEffect);
    }

    if (ImGui::Button(ICON_FA_MUSIC " SE", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::SoundEffect);
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_CAMERA " カメラ揺れ", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::CameraShake);
    }

    ImGui::SameLine();

    if (ImGui::Button("Debris", ImVec2(buttonWidth, 32))) AddDefaultEvent(VfxEventType::Debris);
    ImGui::SameLine();
    if (ImGui::Button("FOV Pulse", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::CameraFovPulse);
    }

    if (ImGui::Button("Light", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::LightPulse);
    }

    ImGui::SameLine();

    if (ImGui::Button("Post FX", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::PostEffectPulse);
    }

    ImGui::SameLine();

    if (ImGui::Button("Hit Stop", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::HitStop);
    }

    if (ImGui::Button("Controller Rumble", ImVec2(buttonWidth, 32))) {
        AddDefaultEvent(VfxEventType::ControllerRumble);
    }

    ImGui::SeparatorText("イベント一覧");
    auto& events = previewSequencer_.GetEvents();

    if (selectedEvent_ >= 0 && selectedEvent_ < static_cast<int>(events.size())) {
        if (ImGui::Button("選択イベントを複製")) {
            VfxEvent copy = events[selectedEvent_];
            // 元のイベントと表示上見分けやすいよう、複製直後だけ発火時刻を 0.1 秒ずらす。
            copy.triggerTime += 0.1f;
            events.push_back(copy);
            selectedEvent_ = static_cast<int>(events.size()) - 1;
        }

        DrawEventEditor(selectedEvent_, events[selectedEvent_]);
    } else {
        ImGui::TextWrapped("タイムラインの行を選ぶと、ここで素材・時刻・位置を編集できます。");
    }

    ImGui::EndDisabled();

    files_.Draw("VFXシーケンスJSONファイル", "Resources/json/vfx_sequence",
        [this](const fs::path& path) { return LoadSequenceFile(path); },
        [this](const fs::path& path, const std::string&) { return previewSequencer_.SaveFile(path); },
        [this] {
            previewSequencer_.Stop();
            previewSequencer_.GetEvents().clear();
            selectedEvent_ = -1;
            timelineDuration_ = 3.0f;
        });

    ImGui::TextWrapped("保存は手動です。素材側を変更した後は、停止して先頭から再生してください。");
    if (!fileStatus_.empty())
        ImGui::TextWrapped("%s", fileStatus_.c_str());
    if (ImGui::Button("演出タイムラインへ（動作・カメラと組み合わせる）")) {
        previewSequencer_.Stop();
        EffectPreviewStage::GetInstance()->ReturnToScene();
        EditorCommandRegistry::GetInstance()->Execute("tool.director");
    }

    if (openRequested_) {
        ImGui::OpenPopup("VFXシーケンスの読込確認");
        openRequested_ = false;
    }

    if (ImGui::BeginPopupModal("VFXシーケンスの読込確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("読込先: %s", pendingSequence_.c_str());
        ImGui::TextUnformatted("現在の未保存の編集は置き換わります。先に保存する場合はキャンセルしてください。");

        if (ImGui::Button("置き換えて読み込む")) {
            const fs::path relative(std::u8string(pendingSequence_.begin(), pendingSequence_.end()) + u8".json");
            if (LoadSequenceFile(fs::path("Resources/json/vfx_sequence") / relative))
                fileStatus_ = "読込完了: " + pendingSequence_;
            else
                fileStatus_ = "読込失敗。現在の編集は保持しています。";

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("キャンセル"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
#endif
}

bool VfxSequencerEditor::LoadSequenceFile(const fs::path& path) {
    const fs::path root("Resources/json/vfx_sequence");
    try {
        const auto relative = path.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || relative.has_root_path() || path.extension() != ".json") return false;
        for (const auto& part : relative) if (part == ".." || part == ".") return false;

        std::ifstream input(path);
        const auto document = nlohmann::json::parse(input);
        if (!document.contains("events") || !document["events"].is_array()) return false;
        auto keyPath = relative;
        // 再生器が参照するキーは拡張子を除いた素材ルート相対パス。フォルダー階層は保持する。
        const auto key = keyPath.replace_extension().generic_u8string();

        // 既存の形式と互換処理を使い、解析完了後にだけプレビュー内容を置き換える。
        VfxSequencer loaded;
        loaded.Load(std::string(key.begin(), key.end()));
        previewSequencer_.Stop();
        previewSequencer_.GetEvents() = std::move(loaded.GetEvents());
        files_.SetDocument(relative);
        selectedEvent_ = previewSequencer_.GetEvents().empty() ? -1 : 0;
        timelineDuration_ = 3.0f;
        for (const auto& event : previewSequencer_.GetEvents())
            timelineDuration_ = (std::max)(timelineDuration_, GetEventEndTime(event) + 0.5f);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void VfxSequencerEditor::DrawTimelinePreview() {
#ifdef USE_IMGUI
    // 表示秒数を画素幅へ換算し、行選択と発火時刻のドラッグを同じ矩形で扱う。
    auto& events = previewSequencer_.GetEvents();
    ImGui::SeparatorText("タイムライン");
    ImGui::TextWrapped("行をクリックして選択 / バーを左右ドラッグして発火時刻を移動（再生中は編集できません）");
    ImGui::Checkbox("0.05秒刻みに吸着", &snapTimeline_);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("表示する秒数", &timelineDuration_, 0.2f, 1.0f, 180.0f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();

    if (ImGui::Button("全体に合わせる")) {
        timelineDuration_ = 3.0f;

        for (const auto& event : events)
            timelineDuration_ = (std::max)(timelineDuration_, GetEventEndTime(event) + 0.5f);
    }

    const float maxTime = draggingEvent_ >= 0 ? dragTimelineDuration_ : timelineDuration_;
    const float width = (std::max)(ImGui::GetContentRegionAvail().x, 240.0f);
    const float rowHeight = 20.0f;
    const float headerHeight = 22.0f;
    const float height = headerHeight + (std::max)(1, static_cast<int>(events.size())) * rowHeight + 10.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("##VFXCueTimeline", ImVec2(width, height));

    if (ImGui::IsItemActivated() && !previewSequencer_.IsPlaying()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int row = static_cast<int>(std::floor((mouse.y - origin.y - headerHeight) / rowHeight));

        if (row >= 0 && row < static_cast<int>(events.size())) {
            selectedEvent_ = row;
            const float start = origin.x + events[row].triggerTime / maxTime * width;
            const float end = (std::max)(origin.x + GetEventEndTime(events[row]) / maxTime * width, start + 8.0f);

            if (mouse.x >= start && mouse.x <= end) {
                draggingEvent_ = row;
                dragStartTime_ = events[row].triggerTime;

                // 編集中に表示秒数を変更しても、ドラッグの換算には開始時の表示秒数を使う。
                dragTimelineDuration_ = maxTime;
            }
        }
    }

    if (ImGui::IsItemActive() && draggingEvent_ >= 0 && draggingEvent_ < static_cast<int>(events.size())) {
        // MouseDragDeltaは押下開始からの総移動量。現在値へ毎フレーム足すと移動を重複計上する。
        float time = dragStartTime_ + ImGui::GetMouseDragDelta().x / width * maxTime;
        if (snapTimeline_)
            time = std::round(time / 0.05f) * 0.05f;
        // 画面に表示する範囲とは別に、保存できる開始時刻を0〜120秒に制限する。
        events[draggingEvent_].triggerTime = std::clamp(time, 0.0f, 120.0f);
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        draggingEvent_ = -1;

    const ImU32 bgColor = IM_COL32(28, 31, 36, 255);

    // 時間目盛り、Eventの色付きバー、現在時刻の順に重ねて描く。
    const ImU32 lineColor = IM_COL32(95, 100, 110, 160);
    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), bgColor, 4.0f);
    drawList->AddRect(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(120, 130, 145, 180), 4.0f);

    // 表示幅と全秒数からおよそ48ピクセル以上になる整数秒間隔を選び、縮小すると目盛りを間引く。
    const int tickStep = (std::max)(1, static_cast<int>(std::ceil(maxTime * 48.0f / width)));

    for (int second = 0; second <= static_cast<int>(maxTime); second += tickStep) {
        const float x = origin.x + (static_cast<float>(second) / maxTime) * width;
        drawList->AddLine(ImVec2(x, origin.y + headerHeight), ImVec2(x, origin.y + height - 4.0f), lineColor);
        char label[16];
        sprintf_s(label, "%.0fs", static_cast<float>(second));
        drawList->AddText(ImVec2(x + 4.0f, origin.y + 4.0f), IM_COL32(210, 215, 225, 255), label);
    }

    for (int i = 0; i < static_cast<int>(events.size()); ++i) {
        const VfxEvent& event = events[i];
        const float startX = origin.x + (event.triggerTime / maxTime) * width;
        const float endX = origin.x + (GetEventEndTime(event) / maxTime) * width;
        const float y = origin.y + headerHeight + i * rowHeight + 3.0f;
        if (i == selectedEvent_)
            drawList->AddRectFilled(ImVec2(origin.x, y - 2.0f), ImVec2(origin.x + width, y + 16.0f), IM_COL32(80, 100, 140, 100));
        const ImVec4 color = GetEventColor(event.type);
        drawList->PushClipRect(origin, ImVec2(origin.x + width, origin.y + height), true);
        drawList->AddRectFilled(ImVec2(startX, y), ImVec2((std::max)(endX, startX + 8.0f), y + 14.0f), ToU32(color), 3.0f);

        char label[64];
        sprintf_s(label, "%02d %s", i, GetEventTypeName(event.type));
        drawList->AddText(ImVec2(startX + 4.0f, y - 1.0f), IM_COL32(245, 245, 245, 255), label);
        drawList->PopClipRect();
    }

    const float playheadX = origin.x + std::clamp(previewSequencer_.GetCurrentTime() / maxTime, 0.0f, 1.0f) * width;
    drawList->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, origin.y + height), IM_COL32(255, 110, 130, 255), 2.0f);
#endif
}

void VfxSequencerEditor::DrawEventEditor(int index, VfxEvent& event) {
#ifdef USE_IMGUI
    // Event型ごとの入力を同じ時系列データへ書き込み、プレビューと保存で共有します。
    auto& events = previewSequencer_.GetEvents();
    ImGui::PushID(index);

    const ImVec4 color = GetEventColor(event.type);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const bool open = ImGui::TreeNodeEx("Event", ImGuiTreeNodeFlags_DefaultOpen, "%02d  %s", index, GetEventTypeName(event.type));
    ImGui::PopStyleColor();

    ImGui::SameLine();

    if (ImGui::SmallButton(ICON_FA_TRASH_ALT)) {
        // 削除後は参照中のeventが無効になるので、この描画を直ちに終える。
        events.erase(events.begin() + index);
        selectedEvent_ = -1;
        if (open)
            ImGui::TreePop();
        ImGui::PopID();
        return;
    }

    if (open) {
        // 素材Editorへ移る際は名前をクリップボードへ渡す。編集中データの自動保存はしない。
        const char* materialTool = event.type == VfxEventType::MeshEffect ? "tool.mesh" :
            (event.type == VfxEventType::GpuParticle || event.type == VfxEventType::MovingParticle ? "tool.gpu" : nullptr);
        if (materialTool) {
            ImGui::TextWrapped("素材: %s", event.presetName.empty() ? "未設定" : event.presetName.c_str());

            if (ImGui::Button("素材名をコピーして素材エディターへ")) {
                ImGui::SetClipboardText(event.presetName.c_str());
                previewSequencer_.Stop();
                EditorCommandRegistry::GetInstance()->Execute(materialTool);
            }

            ImGui::TextWrapped("素材エディターの未保存内容は維持します。コピーした名前のプリセットを選んで読み込んでください。");
        }

        ImGui::DragFloat(ICON_FA_STOPWATCH " 発火時間", &event.triggerTime, 0.02f, 0.0f, 120.0f, "%.2f s");

        // 通常の発生。毎秒発生数が 0 なら単発で、正の値を指定したときだけ継続と方向の設定を表示する。
        if (event.type == VfxEventType::GpuParticle) {
            DrawPresetCombo("プリセット", event.presetName, particlePresetList_);
            ImGui::DragFloat("毎秒発生数（0で単発）", &event.emissionRate, .1f, 0, 120);
            if (event.emissionRate > 0) {
                ImGui::DragFloat("発生継続時間", &event.duration, .01f, .01f, 120);
                ImGui::DragFloat3("発生方向", &event.emissionDirection.x, .01f);
                ImGui::Checkbox("対象の水平回転に方向を合わせる", &event.directionFollowsYaw);
                ImGui::DragFloat("速度倍率", &event.emissionScale, .01f, 0, 10);
                ImGui::TextDisabled("シーケンス先頭基準。遅延時も1フレーム最大1回。位置補正はワールド単位。");
            }
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " オフセット", &event.offset.x, 0.1f);
        }
        // 粒子の発生点を移動させる。開始・途中・終端の位置と到達までの秒数を設定する。
        else if (event.type == VfxEventType::MovingParticle) {
            DrawPresetCombo("プリセット", event.presetName, particlePresetList_);
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 開始オフセット", &event.offset.x, 0.1f);
            ImGui::DragFloat3(ICON_FA_ROUTE " 中間点", &event.controlPoint.x, 0.1f);
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 終点オフセット", &event.endOffset.x, 0.1f);
            ImGui::DragFloat(ICON_FA_STOPWATCH " 移動時間", &event.duration, 0.02f, 0.05f, 30.0f, "%.2f s");
            ImGui::DragInt(ICON_FA_SLIDERS_H " イージング", &event.easingType, 1.0f, 0, 30);
        }
        // 選んだメッシュ素材を配置する。素材の見た目は別エディターへ任せ、ここでは発火時の姿勢を調整する。
        else if (event.type == VfxEventType::MeshEffect) {
            DrawPresetCombo("エフェクト", event.presetName, meshEffectList_);
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " オフセット", &event.offset.x, 0.1f);
            ImGui::DragFloat3(ICON_FA_SYNC_ALT " 回転", &event.rotation.x, 0.05f);
            ImGui::DragFloat3(ICON_FA_EXPAND_ARROWS_ALT " スケール", &event.scale.x, 0.05f);
        }
        // 時刻に達したら鳴らす素材だけを選ぶ。位置や継続時間の入力は設けない。
        else if (event.type == VfxEventType::SoundEffect) {
            DrawPresetCombo("SE", event.presetName, seFileList_);
        }
        // 揺れの長さと速さを指定し、共通の強さを X/Y/Z の重みで配分する。
        else if (event.type == VfxEventType::CameraShake) {
            ImGui::DragFloat(ICON_FA_STOPWATCH " 揺れ時間", &event.duration, 0.01f, 0.03f, 5.0f, "%.2f s");
            ImGui::DragFloat(ICON_FA_BOLT " 強さ", &event.intensity, 0.01f, 0.0f, 5.0f, "%.2f");
            ImGui::DragFloat(ICON_FA_WAVE_SQUARE " 周波数", &event.frequency, 0.5f, 1.0f, 80.0f, "%.1f");
            ImGui::DragFloat3(ICON_FA_SLIDERS_H " 軸ごとの重み", &event.scale.x, 0.02f, 0.0f, 2.0f);
            ImGui::TextDisabled("軸重みは X/Y/Z の揺れやすさです。Zを低めにすると画面酔いしにくくなります。");
        }
        // 画面効果を発火直後に加え、指定した秒数とカーブで減衰させる。
        else if (event.type == VfxEventType::PostEffectPulse) {
            ImGui::DragFloat(ICON_FA_STOPWATCH " 継続時間", &event.duration, 0.01f, 0.03f, 5.0f, "%.2f s");
            ImGui::DragInt(ICON_FA_SLIDERS_H " 減衰カーブ", &event.easingType, 1.0f, 0, 4);
            ImGui::DragFloat("放射ブラー", &event.radialIntensity, 0.001f, 0.0f, 1.0f, "%.3f");
            ImGui::DragFloat("ダメージフラッシュ", &event.damageFlash, 0.01f, 0.0f, 5.0f, "%.2f");
            ImGui::DragFloat("色収差", &event.chromaticAberration, 0.001f, 0.0f, 0.2f, "%.3f");
            ImGui::DragFloat("波打ち", &event.wobbleIntensity, 0.001f, 0.0f, 1.0f, "%.3f");
            ImGui::DragFloat("ブルーム加算", &event.bloomIntensity, 0.01f, 0.0f, 10.0f, "%.2f");
        }
        // 一時ライトの位置と色、届く範囲と距離による減衰を調整する。
        else if (event.type == VfxEventType::LightPulse) {
            ImGui::DragFloat(ICON_FA_STOPWATCH " Duration", &event.duration, 0.01f, 0.03f, 5.0f, "%.2f s");
            ImGui::DragFloat3(ICON_FA_ARROWS_ALT " Offset", &event.offset.x, 0.1f);
            ImGui::DragFloat(ICON_FA_BOLT " Intensity", &event.intensity, 0.1f, 0.0f, 80.0f, "%.1f");
            ImGui::DragFloat("Radius", &event.lightRadius, 0.1f, 0.1f, 80.0f, "%.1f");
            ImGui::DragFloat("Decay", &event.lightDecay, 0.05f, 0.1f, 8.0f, "%.2f");
            ImGui::ColorEdit4("Color", &event.lightColor.x);
        }
        else if (event.type == VfxEventType::Debris) {
            // 一覧を開いた時だけディスクを調べ、追加した素材も再起動なしで選べるようにする。
            if (ImGui::BeginCombo("破片素材", event.presetName.empty() ? "(なし)" : event.presetName.c_str())) {
                const auto names = JsonAssetBrowser::ListKeys("Resources/json/debris");
                for (const auto& name : names) {
                    if (ImGui::Selectable(name.c_str(), event.presetName == name)) event.presetName = name;
                }
                if (names.empty()) ImGui::TextDisabled("破片素材が見つかりません。");
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Resources/json/debris の素材を使用。形・数・大きさ・速度は破片エディターで調整します。");
            ImGui::DragFloat3("発生位置補正", &event.offset.x, .01f);
            ImGui::Checkbox("発生位置から地面高さを指定", &event.debrisGroundRelative);
            if (event.debrisGroundRelative) ImGui::DragFloat("地面の相対高さ", &event.debrisGroundOffset, .01f);
            ImGui::Checkbox("素材色を上書き", &event.debrisOverrideColor);
            if (event.debrisOverrideColor) ImGui::ColorEdit4("破片の色", &event.debrisColor.x);
        }
        // 画角の変化量は度数法。継続時間のうち、立ち上がりに割く割合も設定する。
        else if (event.type == VfxEventType::CameraFovPulse) {
            ImGui::DragFloat(ICON_FA_STOPWATCH " 継続時間", &event.duration, 0.01f, 0.03f, 3.0f, "%.2f s");
            ImGui::DragFloat("FOV変化量", &event.intensity, 0.1f, -30.0f, 30.0f, "%.1f deg");
            ImGui::DragFloat("立ち上がり比率", &event.attackRatio, 0.01f, 0.01f, 0.95f, "%.2f");
            ImGui::TextDisabled("正の値で広角、負の値で望遠へ振ってから元へ戻ります。");
        }
        // 短時間だけ進行を減速する。倍率 0 は停止、1 は通常速度。
        else if (event.type == VfxEventType::HitStop) {
            ImGui::DragFloat(ICON_FA_STOPWATCH " 停止時間", &event.duration, 0.005f, 0.01f, 1.0f, "%.3f s");
            ImGui::DragFloat("時間倍率", &event.intensity, 0.01f, 0.0f, 1.0f, "%.2f");
        }
        // 低周波と高周波のモーターを別々の強さで駆動する。
        else if (event.type == VfxEventType::ControllerRumble) {
            ImGui::DragFloat(ICON_FA_STOPWATCH " 振動時間", &event.duration, 0.01f, 0.01f, 5.0f, "%.2f s");
            ImGui::DragFloat("低周波モーター", &event.intensity, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::DragFloat("高周波モーター", &event.secondaryIntensity, 0.01f, 0.0f, 1.0f, "%.2f");
        }

        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::PopID();
#endif
}

void VfxSequencerEditor::DrawPresetCombo(const char* label, std::string& value, const std::vector<std::string>& list) {
#ifdef USE_IMGUI
    // 表示名と保存値を同じPreset識別子に揃えます。
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

void VfxSequencerEditor::AddDefaultEvent(VfxEventType type) {
    // Event型に応じた既存Presetを初期値にし、空の選択を減らします。
    VfxEvent event;
    event.type = type;
    event.triggerTime = 0.0f;
    event.presetName = GetEventTypeName(type);

    if (type == VfxEventType::GpuParticle) {
        event.presetName = particlePresetList_.empty() ? "" : particlePresetList_.front();
    }
    else if (type == VfxEventType::MovingParticle) {
        event.presetName = particlePresetList_.empty() ? "" : particlePresetList_.front();
        event.duration = 1.0f;
        event.controlPoint = { 0.0f, 4.0f, 0.0f };
        event.endOffset = { 0.0f, 0.0f, 8.0f };
    }
    else if (type == VfxEventType::MeshEffect) {
        event.presetName = meshEffectList_.empty() ? "" : meshEffectList_.front();
    }
    else if (type == VfxEventType::SoundEffect) {
        event.presetName = seFileList_.empty() ? "" : seFileList_.front();
    }
    else if (type == VfxEventType::CameraShake) {
        event.duration = 0.25f;
        event.intensity = 0.25f;
        event.frequency = 28.0f;
        event.scale = { 1.0f, 1.0f, 0.35f };
    }
    else if (type == VfxEventType::LightPulse) {
        event.duration = 0.22f;
        event.intensity = 22.0f;
        event.lightRadius = 11.0f;
        event.lightDecay = 1.35f;
        event.lightColor = { 1.0f, 0.58f, 0.12f, 1.0f };
    }
    else if (type == VfxEventType::PostEffectPulse) {
        event.duration = 0.28f;
        event.easingType = 2;
        event.radialIntensity = 0.035f;
        event.chromaticAberration = 0.012f;
        event.bloomIntensity = 0.35f;
    }
    else if (type == VfxEventType::CameraFovPulse) {
        event.duration = 0.24f;
        event.intensity = 4.0f;
        event.attackRatio = 0.12f;
    }
    else if (type == VfxEventType::HitStop) {
        event.duration = 0.065f;
        event.intensity = 0.0f;
    }
    else if (type == VfxEventType::ControllerRumble) {
        event.duration = 0.18f;
        event.intensity = 0.45f;
        event.secondaryIntensity = 0.72f;
    }

    previewSequencer_.GetEvents().push_back(event);
    selectedEvent_ = static_cast<int>(previewSequencer_.GetEvents().size()) - 1;
}

const char* VfxSequencerEditor::GetEventTypeName(VfxEventType type) const {
    // タイムラインとPresetの表示で共通の型名を使います。
    switch (type) {
    case VfxEventType::GpuParticle:
        return "GPU Particle";

    case VfxEventType::MovingParticle:
        return "Moving Particle";

    case VfxEventType::MeshEffect:
        return "Mesh Effect";

    case VfxEventType::SoundEffect:
        return "Sound Effect";

    case VfxEventType::CameraShake:
        return "Camera Shake";

    case VfxEventType::PostEffectPulse:
        return "Post Effect Pulse";

    case VfxEventType::LightPulse:
        return "Light Pulse";

    case VfxEventType::HitStop:
        return "Hit Stop";

    case VfxEventType::ControllerRumble:
        return "Controller Rumble";

    case VfxEventType::Debris:
        return "Debris";

    case VfxEventType::CameraFovPulse:
        return "Camera FOV Pulse";

    default:
        return "Unknown";
    }
}
