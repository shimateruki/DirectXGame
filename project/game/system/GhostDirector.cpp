#define NOMINMAX
#include "GhostDirector.h"
#include "BaseScene.h"
#include "DebugConsole.h"
#include "GhostRecorder.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "json.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
    constexpr const char* kScenarioDir = "Resources/json/scenario/";
    constexpr const char* kAnimationDir = "Resources/json/animation/";
    constexpr const char* kVFXDir = "Resources/json/vfx_sequence/";

    std::vector<std::string> CollectJsonStems(const std::string& directory) {
        std::vector<std::string> names;
        if (!fs::exists(directory)) {
            return names;
        }

        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".json") {
                names.push_back(entry.path().stem().string());
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    void AddDebugLog(const std::string& text) {
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog(text);
        }
    }

    std::string BuildTargetLabel(Object3d* target, const std::string& targetName, bool allowWorld) {
        if (target) {
            return target->GetName();
        }
        if (!targetName.empty()) {
            return targetName + " (未解決)";
        }
        return allowWorld ? "(ワールド座標)" : "(未選択)";
    }
}

void GhostDirector::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
    tracks_.clear();
    vfxTracks_.clear();
    isPlaying_ = false;
    playTimer_ = 0.0f;
    currentScrubTime_ = 0.0f;
}

void GhostDirector::Update(float deltaTime) {
    if (!isPlaying_) {
        return;
    }

    if (!useImguiTime_) {
        AdvanceTime(deltaTime);
    }
}

void GhostDirector::DrawImGui() {
#ifdef USE_IMGUI
    if (isPlaying_ && useImguiTime_) {
        AdvanceTime(ImGui::GetIO().DeltaTime);
    }

    auto drawTargetCombo = [this](const char* label, Object3d*& target, std::string& targetName, bool allowWorld) {
        std::string currentTargetName = BuildTargetLabel(target, targetName, allowWorld);
        if (ImGui::BeginCombo(label, currentTargetName.c_str())) {
            if (allowWorld && ImGui::Selectable("(ワールド座標)", target == nullptr && targetName.empty())) {
                target = nullptr;
                targetName.clear();
            }

            if (sceneManager_ && sceneManager_->GetCurrentScene()) {
                for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
                    bool isSelected = (target == obj.get());
                    if (ImGui::Selectable(obj->GetName().c_str(), isSelected)) {
                        target = obj.get();
                        targetName = obj->GetName();
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }
    };

    ImGui::Text(ICON_FA_FILM " Cinematic Director");
    ImGui::TextDisabled("GhostRecorderのパス演出とVFX Cueを同じタイムラインで再生します。");

    if (ImGui::CollapsingHeader(ICON_FA_SAVE " シナリオファイル", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!fs::exists(kScenarioDir)) {
            fs::create_directories(kScenarioDir);
        }

        const auto scenarioFiles = CollectJsonStems(kScenarioDir);
        if (ImGui::BeginCombo(ICON_FA_FOLDER_OPEN " 既存シナリオ", scenarioNameBuf_)) {
            for (const auto& fileName : scenarioFiles) {
                const bool isSelected = (std::string(scenarioNameBuf_) == fileName);
                if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                    strncpy_s(scenarioNameBuf_, sizeof(scenarioNameBuf_), fileName.c_str(), _TRUNCATE);
                    LoadScenario(scenarioNameBuf_);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText(ICON_FA_FILE_SIGNATURE " シナリオ名", scenarioNameBuf_, sizeof(scenarioNameBuf_));

        if (ImGui::Button(ICON_FA_DOWNLOAD " Save Scenario")) {
            SaveScenario(scenarioNameBuf_);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " Load Scenario")) {
            LoadScenario(scenarioNameBuf_);
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_FA_USERS " パストラック", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(ICON_FA_PLUS_CIRCLE " パストラックを追加")) {
            tracks_.push_back(Track());
        }

        const auto animationFiles = CollectJsonStems(kAnimationDir);
        for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
            ImGui::PushID(i);
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), ICON_FA_USER " Path Track %d", i + 1);
            ImGui::SameLine(ImGui::GetWindowWidth() - 50);
            if (ImGui::Button(ICON_FA_TRASH_ALT " Del")) {
                tracks_.erase(tracks_.begin() + i);
                ImGui::PopID();
                break;
            }

            auto& track = tracks_[i];
            drawTargetCombo(ICON_FA_CROSSHAIRS " Target", track.target, track.targetName, false);

            std::string currentPath = track.pathFileName.empty() ? "(未選択)" : track.pathFileName;
            if (ImGui::BeginCombo(ICON_FA_MAP_SIGNS " Path Data", currentPath.c_str())) {
                for (const auto& fileName : animationFiles) {
                    const bool isSelected = (track.pathFileName == fileName);
                    if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                        track.pathFileName = fileName;
                        if (track.target && track.target->recorder_) {
                            track.target->recorder_->Load(fileName);
                        }
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SliderFloat(ICON_FA_CLOCK " 開始ディレイ", &track.delayTime, 0.0f, 30.0f, "%.2f sec");
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_MAGIC " VFX Cueトラック", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(ICON_FA_PLUS_CIRCLE " VFX Cueを追加")) {
            VFXTrack track;
            track.sequencer.Initialize(nullptr);
            vfxTracks_.push_back(track);
        }

        const auto vfxFiles = CollectJsonStems(kVFXDir);
        for (int i = 0; i < static_cast<int>(vfxTracks_.size()); ++i) {
            ImGui::PushID(10000 + i);
            ImGui::TextColored(ImVec4(0.78f, 0.42f, 1.0f, 1.0f), ICON_FA_MAGIC " VFX Cue %d", i + 1);
            ImGui::SameLine(ImGui::GetWindowWidth() - 50);
            if (ImGui::Button(ICON_FA_TRASH_ALT " Del")) {
                vfxTracks_.erase(vfxTracks_.begin() + i);
                ImGui::PopID();
                break;
            }

            auto& track = vfxTracks_[i];
            drawTargetCombo(ICON_FA_CROSSHAIRS " Target", track.target, track.targetName, true);
            track.sequencer.SetTargetObject(track.target);

            std::string currentSequence = track.sequenceName.empty() ? "(未選択)" : track.sequenceName;
            if (ImGui::BeginCombo(ICON_FA_FILM " VFX Sequence", currentSequence.c_str())) {
                for (const auto& fileName : vfxFiles) {
                    const bool isSelected = (track.sequenceName == fileName);
                    if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                        track.sequenceName = fileName;
                        track.sequencer.Initialize(track.target);
                        track.sequencer.Load(fileName);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SliderFloat(ICON_FA_CLOCK " 発火ディレイ", &track.delayTime, 0.0f, 30.0f, "%.2f sec");
            ImGui::TextDisabled("Targetなしの場合はCue内の座標をワールド座標として使います。");
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_FA_STREAM " タイムライン", ImGuiTreeNodeFlags_DefaultOpen)) {
        const float maxTime = GetScenarioDuration();
        ImGui::Text(ICON_FA_STOPWATCH " 全体尺: %.2f sec", maxTime);
        ImGui::TextDisabled("VFX Cueは一瞬の発火演出なので、スクラブ中はパスだけを確認します。");

        const bool isScrubbingChanged = ImGui::SliderFloat("##Scrub", &currentScrubTime_, 0.0f, maxTime, "%.2f sec");

        if (ImGui::IsItemActivated()) {
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) {
                    track.target->recorder_->CaptureBasePose();
                }
            }
        }

        if (isScrubbingChanged) {
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) {
                    float localTime = currentScrubTime_ - track.delayTime;
                    if (localTime < 0.0f) {
                        localTime = 0.0f;
                    }
                    track.target->recorder_->EvaluateAtFrame(static_cast<int>(localTime * 60.0f));
                }
            }
        }

        if (ImGui::IsItemDeactivated()) {
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) {
                    track.target->recorder_->RestoreBasePose();
                }
            }
        }

        if (ImGui::Button(ICON_FA_BACKWARD " 先頭に戻す")) {
            currentScrubTime_ = 0.0f;
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) {
                    track.target->recorder_->EvaluateAtFrame(0);
                }
            }
        }
    }

    ImGui::Separator();

    if (isPlaying_) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FA_PLAY_CIRCLE " 再生中 (%s): %.2f sec",
            useImguiTime_ ? "Editor" : "Game", playTimer_);
    }
    else {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ICON_FA_STOP_CIRCLE " 待機中");
    }

    static bool editorLoopCheck = false;
    ImGui::Checkbox(ICON_FA_REDO " Loop Playback", &editorLoopCheck);

    if (ImGui::Button(ICON_FA_PLAY " Editor Preview", ImVec2(-1, 40))) {
        PlayScenario(editorLoopCheck, true);
    }

    if (ImGui::Button(ICON_FA_GAMEPAD " Game Play Test", ImVec2(-1, 30))) {
        PlayScenario(editorLoopCheck, false);
    }

    if (ImGui::Button(ICON_FA_STOP " Stop Scenario", ImVec2(-1, 30))) {
        StopScenario();
    }
#endif
}

void GhostDirector::PlayScenario(bool isLoop, bool useImguiTime) {
    AddDebugLog("GhostDirector: PlayScenario [" + std::string(scenarioNameBuf_) +
        "] (Loop: " + (isLoop ? "On" : "Off") + ")");

    isPlaying_ = true;
    playTimer_ = 0.0f;
    currentScrubTime_ = 0.0f;
    isLooping_ = isLoop;
    useImguiTime_ = useImguiTime;

    for (auto& track : tracks_) {
        PreparePathTrack(track);
    }

    for (auto& track : vfxTracks_) {
        PrepareVFXTrack(track);
    }
}

void GhostDirector::StopScenario() {
    isPlaying_ = false;
    playTimer_ = 0.0f;
    currentScrubTime_ = 0.0f;

    for (auto& track : tracks_) {
        track.hasStarted = false;
        if (track.target && track.target->recorder_) {
            track.target->recorder_->Stop();
        }
    }

    for (auto& track : vfxTracks_) {
        track.hasStarted = false;
        track.sequencer.Stop();
    }
}

void GhostDirector::SaveScenario(const std::string& fileName) {
    json root;
    root["version"] = 2;
    root["tracks"] = json::array();
    root["vfxCues"] = json::array();

    for (const auto& track : tracks_) {
        json t;
        t["targetName"] = track.target ? track.target->GetName() : track.targetName;
        t["pathFileName"] = track.pathFileName;
        t["delayTime"] = track.delayTime;
        root["tracks"].push_back(t);
    }

    for (const auto& track : vfxTracks_) {
        json t;
        t["targetName"] = track.target ? track.target->GetName() : track.targetName;
        t["sequenceName"] = track.sequenceName;
        t["delayTime"] = track.delayTime;
        root["vfxCues"].push_back(t);
    }

    if (!fs::exists(kScenarioDir)) {
        fs::create_directories(kScenarioDir);
    }

    const std::string path = std::string(kScenarioDir) + fileName + ".json";
    std::ofstream file(path);
    if (file.is_open()) {
        file << root.dump(4);
        AddDebugLog("GhostDirector: Saved " + path);
    }
}

void GhostDirector::LoadScenario(const std::string& fileName) {
    const std::string path = std::string(kScenarioDir) + fileName + ".json";
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }

    json root;
    file >> root;
    tracks_.clear();
    vfxTracks_.clear();

    if (root.contains("tracks")) {
        for (const auto& j : root["tracks"]) {
            Track track;
            track.targetName = j.value("targetName", "");
            track.pathFileName = j.value("pathFileName", "");
            track.delayTime = j.value("delayTime", 0.0f);
            PreparePathTrack(track);
            tracks_.push_back(track);
        }
    }

    if (root.contains("vfxCues")) {
        for (const auto& j : root["vfxCues"]) {
            VFXTrack track;
            track.targetName = j.value("targetName", "");
            track.sequenceName = j.value("sequenceName", "");
            track.delayTime = j.value("delayTime", 0.0f);
            PrepareVFXTrack(track);
            vfxTracks_.push_back(track);
        }
    }

    AddDebugLog("GhostDirector: Loaded " + path);
}

bool GhostDirector::IsFinished() const {
    if (!isPlaying_) {
        return true;
    }

    for (const auto& track : tracks_) {
        if (!track.hasStarted) {
            return false;
        }

        if (track.target && track.target->recorder_ &&
            track.target->recorder_->GetState() == GhostRecorder::State::Playing) {
            return false;
        }
    }

    for (const auto& track : vfxTracks_) {
        if (!track.hasStarted) {
            return false;
        }

        if (track.sequencer.IsPlaying()) {
            return false;
        }
    }

    return true;
}

int GhostDirector::GetActiveEventID() const {
    if (!isPlaying_) {
        return 0;
    }

    for (const auto& track : tracks_) {
        if (track.target && track.target->recorder_) {
            int eventID = track.target->recorder_->GetCurrentEventID();
            if (eventID != 0) {
                return eventID;
            }
        }
    }

    return 0;
}

void GhostDirector::DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size) {
    for (auto& track : tracks_) {
        if (track.target && track.target->recorder_ && !track.pathFileName.empty()) {
            track.target->recorder_->DrawPreview(viewProjection, offset, size, true);
        }
    }
}

ActiveEvent GhostDirector::GetActiveEvent() const {
    ActiveEvent result;
    if (!isPlaying_) {
        return result;
    }

    for (const auto& track : tracks_) {
        if (track.target && track.target->recorder_) {
            int eventID = track.target->recorder_->GetCurrentEventID();
            if (eventID != 0) {
                result.id = eventID;
                result.targetObject = track.target;
                return result;
            }
        }
    }

    return result;
}

void GhostDirector::AdvanceTime(float deltaTime) {
    const float timeStep = deltaTime > 0.0001f ? deltaTime : 1.0f / 60.0f;
    playTimer_ += timeStep;
    currentScrubTime_ = playTimer_;

    for (auto& track : tracks_) {
        if (!track.hasStarted && playTimer_ >= track.delayTime) {
            if (track.target && track.target->recorder_) {
                const bool isCinematic = (track.target->GetClassName() == "CinematicCamera");
                track.target->recorder_->PlayFromMemory(false, isCinematic);
            }
            track.hasStarted = true;
        }
    }

    for (auto& track : vfxTracks_) {
        if (!track.hasStarted && playTimer_ >= track.delayTime) {
            if (!track.sequenceName.empty()) {
                track.sequencer.SetTargetObject(track.target);
                track.sequencer.Play();
                AddDebugLog("GhostDirector: Play VFX Cue [" + track.sequenceName + "]");
            }
            track.hasStarted = true;
        }

        if (track.hasStarted && track.sequencer.IsPlaying()) {
            track.sequencer.Update(timeStep);
        }
    }

    if (IsFinished()) {
        if (isLooping_) {
            playTimer_ = 0.0f;
            currentScrubTime_ = 0.0f;

            for (auto& track : tracks_) {
                track.hasStarted = false;
                if (track.target && track.target->recorder_) {
                    track.target->recorder_->EvaluateAtFrame(0);
                }
            }

            for (auto& track : vfxTracks_) {
                PrepareVFXTrack(track);
            }
        }
        else {
            StopScenario();
        }
    }
}

Object3d* GhostDirector::ResolveObjectByName(const std::string& name) const {
    if (name.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return nullptr;
    }

    for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (obj && obj->GetName() == name) {
            return obj.get();
        }
    }

    return nullptr;
}

void GhostDirector::PreparePathTrack(Track& track) {
    track.hasStarted = false;

    if (!track.target && !track.targetName.empty()) {
        track.target = ResolveObjectByName(track.targetName);
    }

    if (track.target && track.target->recorder_ && !track.pathFileName.empty()) {
        track.target->recorder_->Stop();
        track.target->recorder_->Load(track.pathFileName);
        track.target->recorder_->CaptureBasePose();
        track.target->recorder_->EvaluateAtFrame(0);
    }
}

void GhostDirector::PrepareVFXTrack(VFXTrack& track) {
    track.hasStarted = false;

    if (!track.target && !track.targetName.empty()) {
        track.target = ResolveObjectByName(track.targetName);
    }

    track.sequencer.Initialize(track.target);
    if (!track.sequenceName.empty()) {
        track.sequencer.Load(track.sequenceName);
    }
}

float GhostDirector::GetScenarioDuration() const {
    float duration = 0.1f;

    for (const auto& track : tracks_) {
        float trackDuration = 0.0f;
        if (track.target && track.target->recorder_) {
            trackDuration = static_cast<float>(track.target->recorder_->GetTotalFrames()) / 60.0f;
        }
        duration = (std::max)(duration, track.delayTime + trackDuration);
    }

    for (const auto& track : vfxTracks_) {
        float cueDuration = track.sequencer.GetDuration();
        if (!track.sequenceName.empty() && cueDuration <= 0.0f) {
            cueDuration = 0.1f;
        }
        duration = (std::max)(duration, track.delayTime + cueDuration);
    }

    return duration;
}
