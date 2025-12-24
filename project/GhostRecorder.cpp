#include "GhostRecorder.h"
#include "imgui.h" 
#include "SceneManager.h" 
#include "BaseScene.h"
#include <fstream>
#include "json.hpp"


using json = nlohmann::json;

void GhostRecorder::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager; // 保存
    target_ = nullptr;
    frames_.clear();
    state_ = State::Idle;
}

void GhostRecorder::Update() {
    if (target_ == nullptr) return;

    // --- 録画中 ---
    if (state_ == State::Recording) {
        // 現在のターゲットの場所と回転を保存
        GhostFrame frame;
        frame.position = target_->GetTranslate();
        frame.rotation = target_->GetRotation();
        frames_.push_back(frame);
    }
    // --- 再生中 ---
    else if (state_ == State::Playing) {
        // データがあるかチェック
        if (currentFrameIndex_ < frames_.size()) {
            // 保存されたデータをターゲットに上書き（憑依！）
            GhostFrame& frame = frames_[currentFrameIndex_];
            target_->SetTranslate(frame.position);
            target_->SetRotation(frame.rotation);

            // 次のフレームへ
            currentFrameIndex_++;
        } else {
            // 最後まで再生したら終了
            state_ = State::Idle;
        }
    }
}

void GhostRecorder::DrawImGui() {
#ifdef USE_IMGUI


    // --- 1. ステータス表示 ---
    const char* stateStr = "Idle";
    if (state_ == State::Recording) stateStr = "Recording (REC)";
    if (state_ == State::Playing) stateStr = "Playing (PLAY)";

    // ステートとフレーム数を表示
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "State: %s", stateStr); // 黄色で強調
    ImGui::SameLine();
    ImGui::Text("Frames: %d", (int)frames_.size());

    ImGui::Separator();

    // --- 2. ターゲット選択 ---
    if (sceneManager_) {
        BaseScene* currentScene = sceneManager_->GetCurrentScene();
        if (currentScene) {
            std::string currentTargetName = (target_) ? target_->GetName() : "None";

            if (ImGui::BeginCombo("Target", currentTargetName.c_str())) {
                auto& objects = currentScene->GetObjects();
                for (auto& obj : objects) {
                    Object3d* rawObj = obj.get();
                    std::string name = rawObj->GetName();
                    bool isSelected = (target_ == rawObj);

                    if (ImGui::Selectable(name.c_str(), isSelected)) {
                        target_ = rawObj;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    ImGui::Separator();

    // --- 3. 録画・再生コントロール ---
    ImGui::Text("Control");

    // 録画開始ボタン (待機中 または 再生中 に押せる)
    if (state_ == State::Idle || state_ == State::Playing) {
        // 赤っぽい色にして録画ボタンっぽくする
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Record Start")) {
            StartRecording();
        }
        ImGui::PopStyleColor();
    }

    // 停止ボタン (録画中 または 再生中 に押せる)
    if (state_ == State::Recording || state_ == State::Playing) {
        if (ImGui::Button("Stop")) {
            StopRecording();
        }
    }

    // 再生ボタン (待機中 かつ データがある場合 に押せる)
    if (state_ == State::Idle && !frames_.empty()) {
        // Recordボタンが出ていれば横に並べる
        ImGui::SameLine();
        if (ImGui::Button("Play")) {
            StartPlaying();
        }
    }

    ImGui::Separator();

    // --- 4. ファイル保存・読み込み (新機能) ---
    ImGui::Text("File Operation (JSON)");

    // ファイル名入力欄
    ImGui::InputText("File Name", fileNameBuffer_, sizeof(fileNameBuffer_));

    // .json という拡張子をユーザーに意識させるためのヒント表示
    ImGui::TextDisabled("Save to: resouces/json/%s.json", fileNameBuffer_);

    // Saveボタン (データがあり、かつ待機中のときのみ)
    if (!frames_.empty() && state_ == State::Idle) {
        if (ImGui::Button("Save JSON")) {
            Save(fileNameBuffer_);
        }
    }

    ImGui::SameLine(); // ボタンを横並びに

    // Loadボタン (待機中のときのみ)
    if (state_ == State::Idle) {
        if (ImGui::Button("Load JSON")) {
            Load(fileNameBuffer_);
        }
    }

#endif
}

void GhostRecorder::StartRecording() {
    frames_.clear(); // 前のデータを消す
    state_ = State::Recording;
}

void GhostRecorder::StopRecording() {
    state_ = State::Idle;
}

void GhostRecorder::StartPlaying() {
    currentFrameIndex_ = 0; // 最初から
    state_ = State::Playing;
}
void GhostRecorder::Save(const std::string& fileName) {
    if (frames_.empty()) return;

    json root;
    root["frames"] = json::array();

    // 全フレームをJSON配列に変換
    for (const auto& frame : frames_) {
        json frameJson;
        // Vector3 は配列 [x, y, z] として保存すると容量が節約できます
        frameJson["pos"] = { frame.position.x, frame.position.y, frame.position.z };
        frameJson["rot"] = { frame.rotation.x, frame.rotation.y, frame.rotation.z };

        root["frames"].push_back(frameJson);
    }

    // Resourcesフォルダの中に保存（フォルダがないと失敗するので注意）
    std::string path = "resouces/json/" + fileName + ".json";

    std::ofstream file(path);
    if (file.is_open()) {
        file << root.dump(4); // インデント4で綺麗に書き出し
        file.close();
    }
}

// Load関数
void GhostRecorder::Load(const std::string& fileName) {
    std::string path = "resouces/json/" + fileName + ".json";

    std::ifstream file(path);
    if (!file.is_open()) return;

    json root;
    file >> root;

    frames_.clear(); // 既存のデータをクリア

    // JSONからデータを復元
    if (root.contains("frames")) {
        for (const auto& frameJson : root["frames"]) {
            GhostFrame frame;

            // 配列から取り出す
            auto& pos = frameJson["pos"];
            frame.position = { pos[0], pos[1], pos[2] };

            auto& rot = frameJson["rot"];
            frame.rotation = { rot[0], rot[1], rot[2] };

            frames_.push_back(frame);
        }
    }

    // 読み込み終わったら停止状態へ
    state_ = State::Idle;
    currentFrameIndex_ = 0;
}