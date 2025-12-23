#include "GhostRecorder.h"
#include "imgui.h" 
#include "SceneManager.h" 
#include "BaseScene.h"
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


    // ステータス表示
    const char* stateStr = "Idle";
    if (state_ == State::Recording) stateStr = "Recording...";
    if (state_ == State::Playing) stateStr = "Playing...";
    ImGui::Text("State: %s", stateStr);
    ImGui::Text("Frames: %d", (int)frames_.size());

    ImGui::Separator(); // 見やすく区切り線を入れる
    if (sceneManager_) {
        BaseScene* currentScene = sceneManager_->GetCurrentScene();
        if (currentScene) {

            // 現在ターゲットになっているものの名前を表示 (なければ "None")
            std::string currentTargetName = (target_) ? target_->GetName() : "None";

            // プルダウンメニュー (Combo Box)
            if (ImGui::BeginCombo("Target Object", currentTargetName.c_str())) {

                // シーン内の全オブジェクトを取得
                // ※ GetObjects() の戻り値の型に合わせて auto& にしています
                auto& objects = currentScene->GetObjects();

                for (auto& obj : objects) {
                    // スマートポインタの場合は .get() で生ポインタを取り出す
                    Object3d* rawObj = obj.get();

                    // 名前を取得
                    std::string name = rawObj->GetName();

                    // 「今のターゲットと同じか？」を判定
                    bool isSelected = (target_ == rawObj);

                    // リスト項目を作成
                    if (ImGui::Selectable(name.c_str(), isSelected)) {
                        target_ = rawObj; // クリックされたらターゲット変更
                    }

                    // 初期選択位置をセット
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    ImGui::Separator();
    // 録画ボタン
    if (state_ == State::Idle || state_ == State::Playing) {
        // ボタンを横に並べたいなら ImGui::SameLine(); を使う
        if (ImGui::Button("Record Start")) {
            StartRecording();
        }
    }

    // 停止ボタン
    if (state_ == State::Recording || state_ == State::Playing) {
        if (ImGui::Button("Stop")) {
            StopRecording();
        }
    }

    // 再生ボタン
    if (state_ == State::Idle && !frames_.empty()) {
        if (ImGui::Button("Play")) {
            StartPlaying();
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