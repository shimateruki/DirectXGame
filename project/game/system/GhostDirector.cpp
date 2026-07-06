#define NOMINMAX
#include "GhostDirector.h"
#include "imgui.h"
#include "BaseScene.h"
#include <fstream>
#include "json.hpp"
#include <filesystem>
#include <DebugConsole.h>
#include "GhostRecorder.h"
#include "IconsFontAwesome5.h"
#include "../../engine/utility/PathUtility.h"
using json = nlohmann::json;
namespace fs = std::filesystem;

void GhostDirector::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
    tracks_.clear();
    isPlaying_ = false;
    playTimer_ = 0.0f;
}

void GhostDirector::Update(float deltaTime) {
    if (!isPlaying_) return;

    // ゲーム再生中（useImguiTime_ == false）なら、ゲームの deltaTime で進める
    // これにより、ゲーム側の停止処理（deltaTime=0）でボスも止まります
    if (!useImguiTime_) {
        AdvanceTime(deltaTime);
    }
}

void GhostDirector::DrawImGui() {
#ifdef USE_IMGUI
    // =======================================================
    // 1. 時間の更新（エディタ再生モード時のみ）
    // =======================================================
    if (isPlaying_ && useImguiTime_) {
        AdvanceTime(ImGui::GetIO().DeltaTime);
    }

    ImGui::Text(ICON_FA_FILM " --- ゴーストディレクター (Cinematic Director) ---");

    // =======================================================
    // 2. シナリオファイル管理
    // =======================================================
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " シナリオファイル管理", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string dirPath = "Resources/json/scenario/";
        const auto scenarioDirPath = cg2::path::FromUtf8(dirPath);
        if (!cg2::path::Exists(scenarioDirPath)) cg2::path::CreateDirectories(scenarioDirPath);

        if (ImGui::BeginCombo(ICON_FA_FOLDER_OPEN " 既存シナリオをロード", scenarioNameBuf_)) {
            for (const auto& entry : fs::directory_iterator(scenarioDirPath, cg2::path::SafeDirectoryOptions())) {
                if (cg2::path::IsRegularFile(entry) && cg2::path::ExtensionLower(entry.path()) == ".json") {
                    std::string fileName = cg2::path::ToUtf8String(entry.path().stem());
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
        ImGui::InputText(ICON_FA_FILE_SIGNATURE " シナリオ名", scenarioNameBuf_, sizeof(scenarioNameBuf_));

        if (ImGui::Button(ICON_FA_DOWNLOAD " Save Scenario")) SaveScenario(scenarioNameBuf_);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " Load Scenario")) LoadScenario(scenarioNameBuf_);
    }

    ImGui::Separator();

    // =======================================================
    // 3. トラック管理 (配役表)
    // =======================================================
    if (ImGui::CollapsingHeader(ICON_FA_USERS " トラック管理 (配役表)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(ICON_FA_PLUS_CIRCLE " + トラックを追加")) {
            tracks_.push_back(Track());
        }

        ImGui::Spacing();

        for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
            ImGui::PushID(i);
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), ICON_FA_USER " Track %d", i + 1);
            ImGui::SameLine(ImGui::GetWindowWidth() - 50);
            if (ImGui::Button(ICON_FA_TRASH_ALT " Del")) {
                tracks_.erase(tracks_.begin() + i);
                ImGui::PopID();
                break;
            }

            auto& track = tracks_[i];

            // ターゲットオブジェクト選択
            std::string currentTargetName = track.target ? track.target->GetName() : track.targetName.empty() ? "(未選択)" : track.targetName + " (見つかりません)";
            if (ImGui::BeginCombo(ICON_FA_CROSSHAIRS " Target (役者)", currentTargetName.c_str())) {
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

            // 録画データ（パス）選択
            std::string currentPath = track.pathFileName.empty() ? "(未選択)" : track.pathFileName;
            if (ImGui::BeginCombo(ICON_FA_MAP_SIGNS " Path Data (演技)", currentPath.c_str())) {
                std::string animDirPath = "Resources/json/animation/";
                const auto animationDirPath = cg2::path::FromUtf8(animDirPath);
                if (cg2::path::Exists(animationDirPath)) {
                    for (const auto& entry : fs::directory_iterator(animationDirPath, cg2::path::SafeDirectoryOptions())) {
                        if (cg2::path::IsRegularFile(entry) && cg2::path::ExtensionLower(entry.path()) == ".json") {
                            std::string fileName = cg2::path::ToUtf8String(entry.path().stem());
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

            ImGui::SliderFloat(ICON_FA_CLOCK " 開始ディレイ (秒)", &track.delayTime, 0.0f, 10.0f, "%.2f sec");
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::Separator();

    // =======================================================
    // 4. タイムライン操作 (Scrub)
    // =======================================================
    if (ImGui::CollapsingHeader(ICON_FA_STREAM " タイムライン操作 (Timeline Scrub)", ImGuiTreeNodeFlags_DefaultOpen)) {
        float maxTime = 0.0f;
        for (const auto& track : tracks_) {
            if (track.target && track.target->recorder_) {
                float duration = track.target->recorder_->GetTotalFrames() / 60.0f;
                maxTime = std::max(maxTime, track.delayTime + duration);
            }
        }

        ImGui::Text(ICON_FA_STOPWATCH " 全体の長さ: %.2f sec", maxTime);

        bool isScrubbingChanged = ImGui::SliderFloat("##Scrub", &currentScrubTime_, 0.0f, maxTime, "%.2f sec");

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

        if (ImGui::Button(ICON_FA_BACKWARD " 先頭に戻す (Rewind)")) {
            currentScrubTime_ = 0.0f;
            for (auto& track : tracks_) {
                if (track.target && track.target->recorder_) track.target->recorder_->EvaluateAtFrame(0);
            }
        }
    }

    ImGui::Separator();

    // =======================================================
    // 5. 再生コントロール
    // =======================================================
    if (isPlaying_) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), ICON_FA_PLAY_CIRCLE " ▶ 再生中 (%sモード): %.2f sec",
            useImguiTime_ ? "エディタ" : "ゲーム", playTimer_);
    }
    else {
        ImGui::TextColored(ImVec4(1, 1, 1, 1), ICON_FA_STOP_CIRCLE " ■ 待機中");
    }

    static bool editorLoopCheck = false;
    ImGui::Checkbox(ICON_FA_REDO " Loop Playback", &editorLoopCheck);

    // エディタ用：ゲームが止まっていても動く
    if (ImGui::Button(ICON_FA_PLAY " ▶ エディタでプレビュー (Editor Preview)", ImVec2(-1, 40))) {
        PlayScenario(editorLoopCheck, true);
    }

    // ゲーム内挙動テスト
    if (ImGui::Button(ICON_FA_GAMEPAD " ▶ ゲーム内挙動テスト (Game Play Test)", ImVec2(-1, 30))) {
        PlayScenario(editorLoopCheck, false);
    }

    if (ImGui::Button(ICON_FA_STOP " ■ 停止 (Stop Scenario)", ImVec2(-1, 30))) {
        StopScenario();
    }
#endif
}
void GhostDirector::PlayScenario(bool isLoop, bool useImguiTime) {
    DebugConsole::GetInstance()->AddLog("GhostDirector: PlayScenario [" + std::string(scenarioNameBuf_) +
        "] (Loop: " + (isLoop ? "On" : "Off") + ")");

    // 1. 再生フラグとタイマーの初期化
    isPlaying_ = true;
    playTimer_ = 0.0f;
    isLooping_ = isLoop;           // シナリオ全体をループさせるか
    useImguiTime_ = useImguiTime;  // UpdateのdeltaTimeを無視してImGuiのdeltaを使うか

    // 2. 全トラックの状態をリセット
    for (auto& track : tracks_) {
        track.hasStarted = false; // ディレイ待ち状態に戻す

        // ターゲットが外れている（ロード直後など）場合は名前で再検索
        if (!track.target && !track.targetName.empty() && sceneManager_ && sceneManager_->GetCurrentScene()) {
            for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
                if (obj->GetName() == track.targetName) {
                    track.target = obj.get();
                    break;
                }
            }
        }

        // 3. 各オブジェクト（レコーダー）の準備
        if (track.target && track.target->recorder_ && !track.pathFileName.empty()) {
            // 一旦停止させてからデータを最新状態にロード
            track.target->recorder_->Stop();
            track.target->recorder_->Load(track.pathFileName);
            track.target->recorder_->CaptureBasePose();
            // 再生開始前に「0フレーム目」のポーズをとらせる
            // これにより、ディレイがあるトラックも開始地点で待機できる
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
        // isRelativeとisLoopはもう保存しない
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
                t.target->recorder_->CaptureBasePose();
                t.target->recorder_->EvaluateAtFrame(0);
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


        if (!track.hasStarted) {
            return false;
        }

        if (track.target && track.target->recorder_) {
            // もしどれか一つでも再生中なら、まだ終わっていない
            if (track.target->recorder_->GetState() == GhostRecorder::State::Playing) {
                return false;
            }
        }
    }
    return true; // 全員が出番を迎え、かつ全再生が終わった
}
int GhostDirector::GetActiveEventID() const
{
    {
        if (!isPlaying_) return 0;

        // 全トラック（キューブ）を調べて、イベントが発生していたらそれを返す
        for (const auto& track : tracks_) {
            if (track.target && track.target->recorder_) {
                int eID = track.target->recorder_->GetCurrentEventID();
                if (eID != 0) return eID; // イベントを見つけたらボスコアに報告
            }
        }
        return 0;
    }
}

void GhostDirector::DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size) {
    for (auto& track : tracks_) {
        // ターゲットが存在し、レコーダーがあり、パスデータがセットされていれば描画
        if (track.target && track.target->recorder_ && !track.pathFileName.empty()) {

            // 第4引数に true (isReadOnly) を渡すことで、誤操作を防ぎつつ線だけを描画
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
                // イベントを見つけたら、「誰が」「何の」イベントを起こしたか詰めて返す
                result.id = eID;
                result.targetObject = track.target;
                return result;
            }
        }
    }
    return result; // 誰も起こしていなければ id=0 で返る
}

void GhostDirector::AdvanceTime(float deltaTime) {
    playTimer_ += deltaTime;

    for (auto& track : tracks_) {
        if (!track.hasStarted && playTimer_ >= track.delayTime) {
            if (track.target && track.target->recorder_) {
                bool isCinematic = (track.target->GetClassName() == "CinematicCamera");
                track.target->recorder_->PlayFromMemory(false, isCinematic);
            }
            track.hasStarted = true;
        }
    }

    // 全員の再生が終わったかチェック
    if (IsFinished()) {
        if (isLooping_) {
            // ループありなら、タイマーと各トラックの開始フラグをリセットして最初から
            playTimer_ = 0.0f;
            for (auto& t : tracks_) {
                t.hasStarted = false;
                // 最初に戻すポーズをとらせる
                if (t.target && t.target->recorder_) t.target->recorder_->EvaluateAtFrame(0);
            }
        } else {
            StopScenario();
        }
    }
}
