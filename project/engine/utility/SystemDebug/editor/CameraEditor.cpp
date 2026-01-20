#include "CameraEditor.h"
#include "CameraManager.h"
#include "InputManager.h" // 入力取得に必要
#include "imgui.h"
#include "json.hpp"
#include <fstream>
#include <cmath>
#include <filesystem> 
#include <DebugConsole.h>

using json = nlohmann::json;
namespace fs = std::filesystem; // 短縮用

namespace {
    // クォータニオンを使わず、オイラー角から前方ベクトルなどを求める簡易計算
    Vector3 CalculateForward(const Vector3& rotation) {
        // rotation.x = Pitch, rotation.y = Yaw
        float x = std::sin(rotation.y) * std::cos(rotation.x);
        float y = std::sin(rotation.x); // Y-Upの場合、Pitch上がプラスならsin(x)ではなく-sin(x)の場合も
        float z = std::cos(rotation.y) * std::cos(rotation.x);
        return { x, y, z };
    }
}

CameraEditor* CameraEditor::GetInstance() {
    static CameraEditor instance;
    return &instance;
}

void CameraEditor::Initialize() {
    // 起動時にフォルダ内のファイル一覧を取得
    RefreshFileList();

    // デフォルトファイル（camera_settings.json）を読み込む
    LoadSettings();
}

// フォルダ内の .json ファイルを探してリストを更新する
void CameraEditor::RefreshFileList() {
    fileList_.clear();

    // ディレクトリが存在しなければ作成しておく（親切設計）
    if (!fs::exists(kDirectoryPath_)) {
        fs::create_directories(kDirectoryPath_);
        return;
    }

    // ディレクトリ内を走査して .json だけリストに追加
    for (const auto& entry : fs::directory_iterator(kDirectoryPath_)) {
        if (entry.path().extension() == ".json") {
            fileList_.push_back(entry.path().filename().string());
        }
    }
}

void CameraEditor::Update(Object3d* player, bool isLockingOn) {
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return;

    // --- ロックオン中の特例処理 ---
    // ゲームモードかつロックオン中なら、エディタ設定を無視してゲームロジックに任せる
    if (settings_.currentMode == Mode::Game && isLockingOn) {
        return;
    }

    if (settings_.currentMode == Mode::Game) {
        // ==========================================
        // GAME MODE (プレイヤー追従)
        // ==========================================
        if (player) {
            camera->SetFollowTarget(player);
            camera->SetFollowMode(settings_.gameFollowMode);

            // 追従パラメータの反映 (kAimable / kFixed などの時のみ有効)
            camera->ConfigAimable(settings_.distance, settings_.height, settings_.angle);
            camera->SetLockOnOffset(settings_.lockOnOffset);
        }
    } else {
        // ==========================================
        // EDITOR MODE (自由移動)
        // ==========================================
        camera->SetFollowTarget(nullptr); // 追従解除
        UpdateFreeCamera(camera);         // 自由移動ロジック実行
    }
}

void CameraEditor::UpdateFreeCamera(Camera* camera) {
    InputManager* input = InputManager::GetInstance();

    // 現在のカメラ情報を取得
    Vector3 eye = camera->GetEye();
    Vector3 rotation = camera->GetRotation();

    // ----------------------------------------------------------
    // 1. 回転処理 (右クリック中のみ)
    // ----------------------------------------------------------
    if (input->IsMouseButtonPressed(1)) { // 1 = Right Click
        Vector2 mouseDelta = input->GetMouseMoveDelta();

        // 感度をかけて加算
        rotation.y += mouseDelta.x * settings_.mouseSensitivity;
        rotation.x += mouseDelta.y * settings_.mouseSensitivity;

        // ピッチ制限 (真上・真下に行き過ぎないように)
        const float pitchLimit = 1.5f; // 約85度
        if (rotation.x > pitchLimit) rotation.x = pitchLimit;
        if (rotation.x < -pitchLimit) rotation.x = -pitchLimit;

        camera->SetRotation(rotation);
    }

    // ----------------------------------------------------------
    // 2. 移動処理 (WASD + QE)
    // ----------------------------------------------------------
    Vector3 forward, right;

    // 前方ベクトル
    forward.x = std::sin(rotation.y) * std::cos(rotation.x);
    forward.y = -std::sin(rotation.x);
    forward.z = std::cos(rotation.y) * std::cos(rotation.x);

    // 右ベクトル
    right.x = std::cos(rotation.y);
    right.y = 0.0f;
    right.z = -std::sin(rotation.y);

    // 実際の移動ベクトル
    Vector3 moveVelocity = { 0, 0, 0 };

    // Shiftキーで加速
    float speed = (input->IsKeyPressed(DIK_LSHIFT) || input->IsKeyPressed(DIK_RSHIFT))
        ? settings_.boostSpeed
        : settings_.moveSpeed;

    // 前後 (W/S)
    if (input->IsKeyPressed(DIK_W)) {
        moveVelocity.x += forward.x * speed;
        moveVelocity.y += forward.y * speed;
        moveVelocity.z += forward.z * speed;
    }
    if (input->IsKeyPressed(DIK_S)) {
        moveVelocity.x -= forward.x * speed;
        moveVelocity.y -= forward.y * speed;
        moveVelocity.z -= forward.z * speed;
    }

    // 左右 (A/D)
    if (input->IsKeyPressed(DIK_D)) {
        moveVelocity.x += right.x * speed;
        moveVelocity.y += right.y * speed;
        moveVelocity.z += right.z * speed;
    }
    if (input->IsKeyPressed(DIK_A)) {
        moveVelocity.x -= right.x * speed;
        moveVelocity.y -= right.y * speed;
        moveVelocity.z -= right.z * speed;
    }

    // 上下 (Q/E)
    if (input->IsKeyPressed(DIK_E)) {
        moveVelocity.y += speed;
    }
    if (input->IsKeyPressed(DIK_Q)) {
        moveVelocity.y -= speed;
    }

    // 座標更新
    eye.x += moveVelocity.x;
    eye.y += moveVelocity.y;
    eye.z += moveVelocity.z;

    camera->SetEye(eye);

    Vector3 newTarget;
    newTarget.x = eye.x + forward.x * 10.0f;
    newTarget.y = eye.y + forward.y * 10.0f;
    newTarget.z = eye.z + forward.z * 10.0f;

    camera->SetTarget(newTarget);
}

void CameraEditor::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Begin("カメラ");

    // ==========================================================
    //  ファイル管理セクション 
    // ==========================================================
    if (ImGui::CollapsingHeader("ファイルマネージャー", ImGuiTreeNodeFlags_DefaultOpen)) {

        // 1. ファイルリスト (コンボボックス)
        static int currentItem = -1;
        if (ImGui::BeginCombo("ファイル選択", "Choose from list...")) {
            for (int i = 0; i < fileList_.size(); i++) {
                bool isSelected = (currentItem == i);

                // リスト項目を描画
                if (ImGui::Selectable(fileList_[i].c_str(), isSelected)) {
                    currentItem = i;
                    // 選んだファイル名をバッファにコピー (手入力の手間を省略)
                    std::string selectedName = fileList_[i];
                    strcpy_s(fileNameBuffer_, sizeof(fileNameBuffer_), selectedName.c_str());
                }

                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // 2. ファイル名入力欄 (新規作成や名前変更用)
        ImGui::Text("ファイル名(.json)");
        ImGui::InputText("##ファイル名", fileNameBuffer_, sizeof(fileNameBuffer_));

        // 3. 操作ボタン
        if (ImGui::Button("ロード")) {
            LoadSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("セーブ")) {
            SaveSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("セーブファイルリスト")) {
            RefreshFileList();
        }
    }
    ImGui::Separator();
    // ==========================================================


    // --- モード選択 ---
    const char* modeNames[] = { "ゲームカメラ", "自由に動けるカメラ" };
    int currentModeInt = static_cast<int>(settings_.currentMode);
    if (ImGui::Combo("メインモード", &currentModeInt, modeNames, IM_ARRAYSIZE(modeNames))) {
        settings_.currentMode = static_cast<Mode>(currentModeInt);
    }

    ImGui::Separator();

    if (settings_.currentMode == Mode::Game) {
        // --- Game Mode 設定 ---
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "カメラモード設定");

        const char* followModeNames[] = { "デフォルト", "3人称", "1人称" };
        int currentFollow = static_cast<int>(settings_.gameFollowMode);
        if (ImGui::Combo("View Type", &currentFollow, followModeNames, IM_ARRAYSIZE(followModeNames))) {
            settings_.gameFollowMode = static_cast<Camera::FollowMode>(currentFollow);
        }

        // 調整可能なパラメータのみ表示
        if (settings_.gameFollowMode == Camera::FollowMode::kAimable ||
            settings_.gameFollowMode == Camera::FollowMode::kFixed) {

            ImGui::DragFloat("距離", &settings_.distance, 0.1f, 1.0f, 100.0f);
            ImGui::DragFloat("高さ", &settings_.height, 0.1f, 0.0f, 50.0f);
            ImGui::DragFloat("角度", &settings_.angle, 0.1f, -90.0f, 90.0f);
        }

    } else {
        // --- Editor Mode 設定 ---
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "自由に動けるカメラ");
        ImGui::TextWrapped("右クリックを押しながら WASD で移動。Q/E で上下移動。Shift でブースト");

        ImGui::Dummy(ImVec2(0, 5));
        ImGui::Text("設定");
        ImGui::SliderFloat("移動速度", &settings_.moveSpeed, 0.1f, 5.0f);
        ImGui::SliderFloat("加速速度", &settings_.boostSpeed, 1.0f, 10.0f);
        ImGui::SliderFloat("マウス感度", &settings_.mouseSensitivity, 0.001f, 0.05f);

        // 現在座標の表示
        Camera* camera = CameraManager::GetInstance()->GetMainCamera();
        if (camera) {
            Vector3 pos = camera->GetEye();
            ImGui::Text("Pos: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
        }
    }

    ImGui::End();
#endif
}

void CameraEditor::SaveSettings() {
    // ディレクトリパス + 入力されたファイル名 を結合
    std::string filePath = kDirectoryPath_ + std::string(fileNameBuffer_);

    json j;
    j["mode"] = static_cast<int>(settings_.currentMode);
    j["gameFollowMode"] = static_cast<int>(settings_.gameFollowMode);
    j["distance"] = settings_.distance;
    j["height"] = settings_.height;
    j["angle"] = settings_.angle;
    j["lockOnOffset"] = { settings_.lockOnOffset.x, settings_.lockOnOffset.y, settings_.lockOnOffset.z };

    // エディタ設定
    j["moveSpeed"] = settings_.moveSpeed;
    j["boostSpeed"] = settings_.boostSpeed;
    j["mouseSensitivity"] = settings_.mouseSensitivity;

    std::ofstream file(filePath);
    if (file.is_open()) {
        file << j.dump(4);
    }

    // ★保存したら新しいファイルが増えたかもしれないのでリストを更新
    RefreshFileList();
}

void CameraEditor::LoadSettings() {
    // ディレクトリパス + 入力されたファイル名 を結合
    std::string filePath = kDirectoryPath_ + std::string(fileNameBuffer_);

    std::ifstream file(filePath);
    if (!file.is_open()) return;

    try {
        json j;
        file >> j;
        if (j.contains("mode")) settings_.currentMode = static_cast<Mode>(j["mode"]);
        if (j.contains("gameFollowMode")) settings_.gameFollowMode = static_cast<Camera::FollowMode>(j["gameFollowMode"]);

        if (j.contains("distance")) settings_.distance = j["distance"];
        if (j.contains("height")) settings_.height = j["height"];
        if (j.contains("angle")) settings_.angle = j["angle"];

        if (j.contains("lockOnOffset") && j["lockOnOffset"].is_array()) {
            settings_.lockOnOffset.x = j["lockOnOffset"][0];
            settings_.lockOnOffset.y = j["lockOnOffset"][1];
            settings_.lockOnOffset.z = j["lockOnOffset"][2];
        }

        if (j.contains("moveSpeed")) settings_.moveSpeed = j["moveSpeed"];
        if (j.contains("boostSpeed")) settings_.boostSpeed = j["boostSpeed"];
        if (j.contains("mouseSensitivity")) settings_.mouseSensitivity = j["mouseSensitivity"];
    }
    catch (...) {
        // エラーハンドリング
    }
}

// CameraEditor.cpp

void CameraEditor::LoadFile(const std::string& fileName) {
    // 1. ファイル名バッファを更新
    strcpy_s(fileNameBuffer_, sizeof(fileNameBuffer_), fileName.c_str());

    // 2. パスを作成
    std::string filePath = kDirectoryPath_ + std::string(fileNameBuffer_);

    // 3. ファイルが存在するかチェック
    if (!fs::exists(filePath)) {
        // A. 存在しない場合 -> デフォルト値をセットして保存（新規作成）
        // (これをしないと、前のシーンの設定が残ってしまう)
        settings_ = Settings(); // デフォルトコンストラクタで初期化
        settings_.currentMode = Mode::Game; // 基本はゲームモード

        // ログ出し (任意)
        DebugConsole::GetInstance()->AddLog("New Camera Setting Created: " + fileName);

        // 新規保存
        SaveSettings();
    }

    // 4. 改めて読み込み
    LoadSettings();
}

void CameraEditor::SetMode(Mode mode) {
    settings_.currentMode = mode;
}

void CameraEditor::SetEditorCameraTransform(const Vector3& position, const Vector3& rotation) {
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->SetEye(position);
        camera->SetRotation(rotation);
        // ターゲット追従を切らないと動かない場合があるので念のため
        camera->SetFollowTarget(nullptr);
    }
}

