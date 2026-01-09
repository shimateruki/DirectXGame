#include "GhostRecorder.h"
#include "imgui.h" 
#include "SceneManager.h" 
#include "BaseScene.h"
#include <fstream>
#include "json.hpp"

using json = nlohmann::json;

void GhostRecorder::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
    target_ = nullptr;
    frames_.clear();
    state_ = State::Idle;

    // 初期設定
    isLoop_ = false;
    isRelative_ = true;
}

// 外部呼び出し用の再生関数
void GhostRecorder::Play(const std::string& fileName, bool loop, bool isRelative) {
    // ファイルをロード
    Load(fileName);

    // データが無ければ再生しない
    if (frames_.empty()) return;

    // 設定を保存
    isLoop_ = loop;
    isRelative_ = isRelative;

    // 内部再生処理へ
    StartPlayingInternal();
}

// 停止
void GhostRecorder::Stop() {
    state_ = State::Idle;
    currentFrameIndex_ = 0;
}

void GhostRecorder::StartRecording() {
    if (target_ == nullptr) return;
    frames_.clear();
    state_ = State::Recording;
}

void GhostRecorder::StopRecording() {
    state_ = State::Idle;
}

// 再生開始（共通処理）
void GhostRecorder::StartPlayingInternal() {
    if (frames_.empty() || target_ == nullptr) return;

    state_ = State::Playing;
    currentFrameIndex_ = 0;

    // ★ここがミソ：相対座標モードのために基準点を記録する★

    // 1. 今、ターゲットがいる場所を「スタート地点」として覚える
    startPos_ = target_->GetTranslate();
    startRot_ = target_->GetRotation();

    // 2. 録画データの「1フレーム目」も覚えておく（これを基準に差分を計算するため）
    firstFramePos_ = frames_[0].position;
    firstFrameRot_ = frames_[0].rotation;
}


void GhostRecorder::Update() {
    if (target_ == nullptr) return;

    // --- 録画中 ---
    if (state_ == State::Recording) {
        GhostFrame frame;
        frame.position = target_->GetTranslate();
        frame.rotation = target_->GetRotation();
        frames_.push_back(frame);
    }
    // --- 再生中 ---
    else if (state_ == State::Playing) {

        if (frames_.empty()) {
            state_ = State::Idle;
            return;
        }

        // 現在のフレームデータを取得
        const GhostFrame& frame = frames_[currentFrameIndex_];

        if (isRelative_) {
      

            // 計算式：
            // 「再生開始時の場所」 + (「今のフレーム座標」 - 「録画開始時の座標」)

            Vector3 diffPos = frame.position - firstFramePos_; // 移動量
            Vector3 diffRot = frame.rotation - firstFrameRot_; // 回転量

            target_->SetTranslate(startPos_ + diffPos);
            target_->SetRotation(startRot_ + diffRot);
        } else {
            // ★絶対座標モード：録画した場所にワープする（従来の挙動）
            target_->SetTranslate(frame.position);
            target_->SetRotation(frame.rotation);
        }

        // 次のフレームへ
        currentFrameIndex_++;

        // 最後まで再生したら？
        if (currentFrameIndex_ >= frames_.size()) {
            if (isLoop_) {
                // ループ：最初に戻る
                currentFrameIndex_ = 0;
            } else {
                // ループなし：終了（最後のポーズで止まるか、Idleに戻す）
                // ここでは最後のポーズで止めて、ステートをIdleに戻します
                currentFrameIndex_ = (int)frames_.size() - 1;
                state_ = State::Idle;
            }
        }
    }
}
void GhostRecorder::DrawImGui() {
    ImGui::Begin("Ghost Recorder");

    if (sceneManager_) {
        BaseScene* scene = sceneManager_->GetCurrentScene();
        if (scene) {
            // 現在のターゲット名を表示
            std::string currentTargetName = target_ ? target_->GetName() : "(なし)";

            // コンボボックス（プルダウン）で選択
            if (ImGui::BeginCombo("ターゲット (Target)", currentTargetName.c_str())) {

                // シーン内の全オブジェクトを取得してリスト化
                auto& objects = scene->GetObjects();
                for (auto& obj : objects) {
                    bool isSelected = (target_ == obj.get());

                    // 名前を表示して選択できるようにする
                    if (ImGui::Selectable(obj->GetName().c_str(), isSelected)) {
                        target_ = obj.get(); // ターゲット更新
                    }

                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    // ステート表示（デバッグ用に見やすく）
    const char* stateStr = "待機中 (Idle)";
    if (state_ == State::Recording) stateStr = "録画中 (Recording...)";
    if (state_ == State::Playing) stateStr = "再生中 (Playing)";
    ImGui::Text("状態: %s", stateStr);

    // --- 録画エリア ---
    if (state_ == State::Idle) {
        // ターゲットがいないと押せないようにグレーアウトする
        if (!target_) ImGui::BeginDisabled();

        // 緑色っぽくして「開始」感を出す
        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f));
        if (ImGui::Button("録画開始 (Start Rec)", ImVec2(-1, 0))) {
            StartRecording();
        }
        ImGui::PopStyleColor();

        if (!target_) ImGui::EndDisabled();

    } else if (state_ == State::Recording) {
        // 録画中は赤色にする（重要）
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        if (ImGui::Button("録画停止 (Stop Rec)", ImVec2(-1, 0))) {
            StopRecording();
        }
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // --- 保存・読み込みエリア ---
    static char fileNameBuf[64] = "anim_test";
    ImGui::InputText("ファイル名", fileNameBuf, 64);

    if (ImGui::Button("保存 (Save JSON)")) {
        Save(fileNameBuf);
    }
    ImGui::SameLine();
    if (ImGui::Button("読み込み (Load JSON)")) {
        Load(fileNameBuf);
    }

    ImGui::Separator();

    // --- 再生設定エリア ---
    ImGui::Text("再生設定 (Playback)");

    static bool loopCheck = false;
    static bool relativeCheck = true;

    ImGui::Checkbox("ループ再生 (Loop)", &loopCheck);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("アニメーションを繰り返します");

    ImGui::Checkbox("相対座標モード (Relative)", &relativeCheck);
    // ここは重要な概念なので、ツールチップで補足
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ON: 現在の座標を基準に動きます\nOFF: 録画時の絶対座標にワープして動きます");

    // 再生ボタン
    if (state_ != State::Recording) {
        // ターゲットがいないと再生も危険なので無効化
        if (!target_) ImGui::BeginDisabled();

        if (ImGui::Button("再生開始 (Play)", ImVec2(120, 0))) {
            isLoop_ = loopCheck;
            isRelative_ = relativeCheck;
            StartPlayingInternal();
        }

        if (!target_) ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("停止 (Stop)", ImVec2(120, 0))) {
        Stop();
    }

    ImGui::End();
}

// Save関数
void GhostRecorder::Save(const std::string& fileName) {
    if (frames_.empty()) return;
    json root;
    root["frames"] = json::array();
    for (const auto& frame : frames_) {
        json frameJson;
        frameJson["pos"] = { frame.position.x, frame.position.y, frame.position.z };
        frameJson["rot"] = { frame.rotation.x, frame.rotation.y, frame.rotation.z };
        root["frames"].push_back(frameJson);
    }
    std::string path = "resouces/json/animation/" + fileName + ".json"; // パス修正
    std::ofstream file(path);
    if (file.is_open()) {
        file << root.dump(4);
        file.close();
    }
}

// Load関数
void GhostRecorder::Load(const std::string& fileName) {
    std::string path = "resouces/json/animation/" + fileName + ".json"; 
    std::ifstream file(path);
    if (!file.is_open()) return;
    json root;
    file >> root;
    frames_.clear();
    if (root.contains("frames")) {
        for (const auto& frameJson : root["frames"]) {
            GhostFrame frame;
            if (frameJson.contains("pos")) {
                auto& p = frameJson["pos"];
                frame.position = { p[0], p[1], p[2] };
            }
            if (frameJson.contains("rot")) {
                auto& r = frameJson["rot"];
                frame.rotation = { r[0], r[1], r[2] };
            }
            frames_.push_back(frame);
        }
    }
}