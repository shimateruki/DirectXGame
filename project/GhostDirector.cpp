#define NOMINMAX
#include "GhostDirector.h"
#include "imgui.h"
#include "BaseScene.h"
#include <fstream>
#include "json.hpp"
#include <filesystem>
#include <DebugConsole.h>
#include "GhostRecorder.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

void GhostDirector::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
    tracks_.clear();
    isPlaying_ = false;
}

void GhostDirector::Update() {
    // 実際の移動処理は各オブジェクトの GhostRecorder::Update() が行うので、
    // ディレクター側は現状毎フレーム何もしなくても大丈夫です。
}

void GhostDirector::DrawImGui() {
#ifdef USE_IMGUI
  

    // 1. シナリオファイル管理
    if (ImGui::CollapsingHeader("シナリオファイル管理", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string dirPath = "Resources/json/scenario/";
        if (!fs::exists(dirPath)) fs::create_directories(dirPath);

        if (ImGui::BeginCombo("既存シナリオをロード", scenarioNameBuf_)) {
            for (const auto& entry : fs::directory_iterator(dirPath)) {
                if (entry.path().extension() == ".json") {
                    std::string fileName = entry.path().stem().string();
                    bool isSelected = (std::string(scenarioNameBuf_) == fileName);
                    if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                        strncpy_s(scenarioNameBuf_, sizeof(scenarioNameBuf_), fileName.c_str(), _TRUNCATE);
                        LoadScenario(scenarioNameBuf_);
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("シナリオ名", scenarioNameBuf_, sizeof(scenarioNameBuf_));
        if (ImGui::Button("Save Scenario")) SaveScenario(scenarioNameBuf_);
        ImGui::SameLine();
        if (ImGui::Button("Load Scenario")) LoadScenario(scenarioNameBuf_);
    }

    ImGui::Separator();

    // 2. タイムライン(トラック)管理
    if (ImGui::CollapsingHeader("トラック管理 (配役表)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("+ トラックを追加")) {
            tracks_.push_back(Track());
        }

        ImGui::Spacing();

        for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
            ImGui::PushID(i);
            ImGui::Text("Track %d", i + 1);
            ImGui::SameLine();
            if (ImGui::Button("Del")) {
                tracks_.erase(tracks_.begin() + i);
                ImGui::PopID();
                break;
            }

            auto& track = tracks_[i];

            // --- ターゲット選択 ---
            std::string currentTargetName = track.target ? track.target->GetName() : track.targetName.empty() ? "(未選択)" : track.targetName + " (見つかりません)";
            if (ImGui::BeginCombo("Target", currentTargetName.c_str())) {
                if (sceneManager_ && sceneManager_->GetCurrentScene()) {
                    for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
                        bool isSelected = (track.target == obj.get());
                        if (ImGui::Selectable(obj->GetName().c_str(), isSelected)) {
                            track.target = obj.get();
                            track.targetName = obj->GetName();
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            // --- パスデータ選択 ---
            std::string currentPath = track.pathFileName.empty() ? "(未選択)" : track.pathFileName;
            if (ImGui::BeginCombo("Path Data", currentPath.c_str())) {
                std::string animDirPath = "Resources/json/animation/";
                if (fs::exists(animDirPath)) {
                    for (const auto& entry : fs::directory_iterator(animDirPath)) {
                        if (entry.path().extension() == ".json") {
                            std::string fileName = entry.path().stem().string();
                            bool isSelected = (track.pathFileName == fileName);
                            if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                                track.pathFileName = fileName;

                                // =======================================================
                                // ★追加：選んだ瞬間にレコーダーに読み込ませてシーク可能にする
                                // =======================================================
                                if (track.target && track.target->recorder_) {
                                    track.target->recorder_->Load(fileName);
                                }
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::Checkbox("ループ", &track.isLoop);

            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    // =======================================================
        // ★タイムライン (シークバー) 操作UI
        // =======================================================
    if (ImGui::CollapsingHeader("タイムライン操作 (Timeline Scrub)", ImGuiTreeNodeFlags_DefaultOpen)) {
        // 一番長いトラックの時間を計算する
        int maxFrames = 0;
        for (const auto& track : tracks_) {
            if (track.target && track.target->recorder_) {
                maxFrames = std::max(maxFrames, track.target->recorder_->GetTotalFrames());
            }
        }
        float maxTime = maxFrames / 60.0f;

        ImGui::Text("全体の長さ: %.2f sec", maxTime);

        // =======================================================
        // ★大修正：処理の順番を「記憶 → 動かす → 戻す」に正しく直す！
        // =======================================================

        // シークバーの描画（ここではまだ動かさない。値が変わったかどうかのフラグだけ取る）
        bool isScrubbingChanged = ImGui::SliderFloat("シークバー", &currentScrubTime_, 0.0f, maxTime, "%.2f sec");

        // ① まず、掴んだ瞬間の処理（移動よりも絶対に先に行う！）
        if (ImGui::IsItemActivated()) {
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) {
                    track.target->recorder_->CaptureBasePose(); // 現在地を安全にロック
                }
            }
        }

        // ② 次に、スライダーが動いた時の処理（安全に記憶されたbasePositionを使える）
        if (isScrubbingChanged) {
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) {
                    track.target->recorder_->EvaluateAtFrame(static_cast<int>(currentScrubTime_ * 60.0f));
                }
            }
        }

        // ③ 最後に、離した瞬間の処理
        if (ImGui::IsItemDeactivated()) {
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) {
                    track.target->recorder_->RestoreBasePose(); // 元の位置にシュッと戻す
                }
            }
        }
        // =======================================================

        ImGui::SameLine();
        // ★修正：強制移動させると原点バグが起きるため、時間だけをリセットするように変更
        if (ImGui::Button("先頭に戻す (Rewind)")) {
            currentScrubTime_ = 0.0f;
        }
    }
    ImGui::Separator();

    // 3. 一斉再生コントロール
    if (isPlaying_) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "▶ シナリオ再生中...");
    } else {
        ImGui::TextColored(ImVec4(1, 1, 1, 1), "■ 待機中");
    }

    if (ImGui::Button("▶ 全体再生 (Play Scenario)", ImVec2(-1, 40))) {
        PlayScenario();
    }
    if (ImGui::Button("■ 停止 (Stop Scenario)", ImVec2(-1, 30))) {
        StopScenario();
    }

   
#endif
}
void GhostDirector::PlayScenario() {
    DebugConsole::GetInstance()->AddLog("GhostDirector: PlayScenario [" + std::string(scenarioNameBuf_) + "]");
    isPlaying_ = true;

    for (auto& track : tracks_) {
        // 名前だけあってポインタがnullの場合、シーンから探し直す (ロード直後対策)
        if (!track.target && !track.targetName.empty() && sceneManager_ && sceneManager_->GetCurrentScene()) {
            for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
                if (obj->GetName() == track.targetName) {
                    track.target = obj.get();
                    break;
                }
            }
        }

        // 役者と台本が揃っていたら一斉にPlay！
        if (track.target && track.target->recorder_ && !track.pathFileName.empty()) {
            bool isCinematic = (track.target->GetClassName() == "CinematicCamera");
            track.target->recorder_->Play(track.pathFileName, track.isLoop, track.isRelative, isCinematic);
        } else {
            DebugConsole::GetInstance()->AddLog(" -> Warning: Track のターゲットまたはパスが不正です。Target: " + track.targetName);
        }
    }
}

void GhostDirector::StopScenario() {
    isPlaying_ = false;
    for (auto& track : tracks_) {
        if (track.target && track.target->recorder_) {
            track.target->recorder_->Stop();
        }
    }
}

void GhostDirector::SaveScenario(const std::string& fileName) {
    if (tracks_.empty()) return;
    json root;
    root["tracks"] = json::array();
    for (const auto& track : tracks_) {
        json t;
        t["targetName"] = track.target ? track.target->GetName() : track.targetName;
        t["pathFileName"] = track.pathFileName;
        t["isRelative"] = track.isRelative;
        t["isLoop"] = track.isLoop;
        root["tracks"].push_back(t);
    }

    std::string dirPath = "Resources/json/scenario/";
    if (!fs::exists(dirPath)) fs::create_directories(dirPath);

    std::string path = dirPath + fileName + ".json";
    std::ofstream file(path);
    if (file.is_open()) {
        file << root.dump(4);
        file.close();
        DebugConsole::GetInstance()->AddLog("GhostDirector: Saved " + path);
    }
}

void GhostDirector::LoadScenario(const std::string& fileName) {
    std::string path = "Resources/json/scenario/" + fileName + ".json";
    std::ifstream file(path);
    if (!file.is_open()) return;

    json root; file >> root;
    tracks_.clear();

    if (root.contains("tracks")) {
        for (const auto& j : root["tracks"]) {
            Track t;
            t.targetName = j.value("targetName", "");
            t.pathFileName = j.value("pathFileName", "");
            t.isRelative = j.value("isRelative", true);
            t.isLoop = j.value("isLoop", false);
            t.target = nullptr;

            // ロード時にシーン内に一致する名前があれば結びつけておく
            if (sceneManager_ && sceneManager_->GetCurrentScene() && !t.targetName.empty()) {
                for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
                    if (obj->GetName() == t.targetName) {
                        t.target = obj.get();
                        break;
                    }
                }
            }
            if (t.target && t.target->recorder_ && !t.pathFileName.empty()) {
                t.target->recorder_->Load(t.pathFileName);
            }
            tracks_.push_back(t);
        }
        DebugConsole::GetInstance()->AddLog("GhostDirector: Loaded " + path);
    }
}