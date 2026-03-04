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
    playTimer_ = 0.0f;
}

void GhostDirector::Update() {
    // ※エディタ上でUpdateが呼ばれない可能性があるため、
    // 実際のタイマー進行は確実に呼ばれる DrawImGui() 側で行います！
}

void GhostDirector::DrawImGui() {
#ifdef USE_IMGUI

    // =======================================================
    // ★大修正：確実に毎フレーム呼ばれるここでタイマーを進めてPlayする！
    // =======================================================
    if (isPlaying_) {
        // ImGuiの機能を使って正確なDeltaTime(フレーム間の経過時間)を足す
        playTimer_ += ImGui::GetIO().DeltaTime;

        for (auto& track : tracks_) {
            // 出番が来たらPlay！
            if (!track.hasStarted && playTimer_ >= track.delayTime) {
                if (track.target && track.target->recorder_ && !track.pathFileName.empty()) {
                    bool isCinematic = (track.target->GetClassName() == "CinematicCamera");

                    // ★修正：ターゲット本体が持っている Relative / Loop フラグをそのまま使う！
                    track.target->recorder_->Play(
                        track.pathFileName,
                        track.target->isRecordLoop_,
                        track.target->isRecordRelative_,
                        isCinematic
                    );
                }
                track.hasStarted = true; // スタート済み
            }
        }
    }

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

    // 2. トラック管理
    if (ImGui::CollapsingHeader("トラック管理 (配役表)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("+ トラックを追加")) {
            tracks_.push_back(Track());
        }

        ImGui::Spacing();

        for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
            ImGui::PushID(i);
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Track %d", i + 1);
            ImGui::SameLine(ImGui::GetWindowWidth() - 50);
            if (ImGui::Button("Del")) {
                tracks_.erase(tracks_.begin() + i);
                ImGui::PopID();
                break;
            }

            auto& track = tracks_[i];

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

            ImGui::SliderFloat("開始ディレイ (秒)", &track.delayTime, 0.0f, 10.0f, "%.2f sec");

            // ★修正: Director側の Relative/Loop のチェックボックスは削除しました！

            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::Separator();

    // 3. タイムライン操作 (Timeline Scrub)
    if (ImGui::CollapsingHeader("タイムライン操作 (Timeline Scrub)", ImGuiTreeNodeFlags_DefaultOpen)) {
        float maxTime = 0.0f;
        for (const auto& track : tracks_) {
            if (track.target && track.target->recorder_) {
                float duration = track.target->recorder_->GetTotalFrames() / 60.0f;
                maxTime = std::max(maxTime, track.delayTime + duration);
            }
        }

        ImGui::Text("全体の長さ: %.2f sec", maxTime);

        bool isScrubbingChanged = ImGui::SliderFloat("シークバー", &currentScrubTime_, 0.0f, maxTime, "%.2f sec");
        if (!isPlaying_ && !ImGui::IsItemActive()) {
            for (auto& track : tracks_) { if (track.target && track.target->recorder_) track.target->recorder_->CaptureBasePose(); }
        }
        if (ImGui::IsItemActivated()) {
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) track.target->recorder_->CaptureBasePose();
            }
        }

        if (isScrubbingChanged) {
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) {
                    float localTime = currentScrubTime_ - track.delayTime;
                    if (localTime < 0.0f) localTime = 0.0f;
                    track.target->recorder_->EvaluateAtFrame(static_cast<int>(localTime * 60.0f));
                }
            }
        }

        if (ImGui::IsItemDeactivated()) {
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) track.target->recorder_->RestoreBasePose();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("先頭に戻す (Rewind)")) {
            currentScrubTime_ = 0.0f;
        }
    }
    ImGui::Separator();

    // 4. 一斉再生コントロール
    if (isPlaying_) ImGui::TextColored(ImVec4(0, 1, 0, 1), "▶ シナリオ再生中... (%.2f sec)", playTimer_);
    else ImGui::TextColored(ImVec4(1, 1, 1, 1), "■ 待機中");

    if (ImGui::Button("▶ 全体再生 (Play Scenario)", ImVec2(-1, 40))) PlayScenario();
    if (ImGui::Button("■ 停止 (Stop Scenario)", ImVec2(-1, 30))) StopScenario();

#endif
}

void GhostDirector::PlayScenario() {
    DebugConsole::GetInstance()->AddLog("GhostDirector: PlayScenario [" + std::string(scenarioNameBuf_) + "]");
    isPlaying_ = true;
    playTimer_ = 0.0f;

    for (auto& track : tracks_) {
        track.hasStarted = false;

        if (!track.target && !track.targetName.empty() && sceneManager_ && sceneManager_->GetCurrentScene()) {
            for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
                if (obj->GetName() == track.targetName) {
                    track.target = obj.get();
                    break;
                }
            }
        }

        // ここではまだPlayを呼ばず、「0フレーム目の待機ポーズ」をとらせる！
        if (track.target && track.target->recorder_ && !track.pathFileName.empty()) {
            track.target->recorder_->Load(track.pathFileName);
            track.target->recorder_->EvaluateAtFrame(0);
        }
    }
}

void GhostDirector::StopScenario() {
    isPlaying_ = false;
    playTimer_ = 0.0f;
    for (auto& track : tracks_) {
        track.hasStarted = false;
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
        t["delayTime"] = track.delayTime;
        // ★修正: isRelativeとisLoopはもう保存しない
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
            t.delayTime = j.value("delayTime", 0.0f);
            t.target = nullptr;

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

bool GhostDirector::IsFinished() const
{
    if (!isPlaying_) return true; // 再生してなければ終了扱い

    // 全てのトラック（キューブ）の再生が終わったかチェック
    for (const auto& track : tracks_) {
        if (track.target && track.target->recorder_) {
            // もしどれか一つでも再生中なら、まだ終わっていない
            if (track.target->recorder_->GetState() == GhostRecorder::State::Playing) {
                return false;
            }
        }
    }
    return true; // 全員終わった！
}

int GhostDirector::GetActiveEventID() const
{
    {
        if (!isPlaying_) return 0;

        // 全トラック（キューブ）を調べて、イベントが発生していたらそれを返す
        for (const auto& track : tracks_) {
            if (track.target && track.target->recorder_) {
                int eID = track.target->recorder_->GetCurrentEventID();
                if (eID != 0) return eID; // イベントを見つけたらボスコアに報告！
            }
        }
        return 0;
    }
}

void GhostDirector::DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size) {
    for (auto& track : tracks_) {
        // ターゲットが存在し、レコーダーがあり、パスデータがセットされていれば描画
        if (track.target && track.target->recorder_ && !track.pathFileName.empty()) {

            // ★第4引数に true (isReadOnly) を渡すことで、誤操作を防ぎつつ線だけを描画！
            track.target->recorder_->DrawPreview(viewProjection, offset, size, true);
        }
    }
}


ActiveEvent GhostDirector::GetActiveEvent() const {
    ActiveEvent result;
    if (!isPlaying_) return result;

    // 全員（全トラック）を調べて、イベントを起こしている奴がいないかチェック
    for (const auto& track : tracks_) {
        if (track.target && track.target->recorder_) {
            int eID = track.target->recorder_->GetCurrentEventID();

            if (eID != 0) {
                // イベントを見つけたら、「誰が」「何の」イベントを起こしたか詰めて返す！
                result.id = eID;
                result.targetObject = track.target;
                return result;
            }
        }
    }
    return result; // 誰も起こしていなければ id=0 で返る
}