#include "CameraEditor.h"
#include "CameraManager.h"
#include "InputManager.h" // 入力取得に必要
#include "imgui.h"
#include "json.hpp"
#include <fstream>
#include <cmath>
#include <filesystem> 
#include <DebugConsole.h>
#include "IconsFontAwesome5.h"
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

    if (settings_.currentMode == Mode::Game && isLockingOn) {
        return;
    }

    if (settings_.currentMode == Mode::Game) {
        if (player) {
            camera->SetFollowTarget(player);
            camera->SetFollowMode(settings_.gameFollowMode);
            InputManager* input = InputManager::GetInstance();

            // ★追加: ジャイロ入力があるかチェック
            Vector3 gyro = input->GetGyroscope();
            // 感度は 0.05f くらいでOK
            bool isGyroActive = (std::abs(gyro.x) > 0.05f || std::abs(gyro.y) > 0.05f || std::abs(gyro.z) > 0.05f);

            // 操作中判定 (ジャイロも含める！)
            bool isControllingCamera = input->IsMouseButtonPressed(1) ||
                (std::abs(input->GetRightStick().x) > 0.1f) ||
                (std::abs(input->GetRightStick().y) > 0.1f) ||
                isGyroActive;


            if (isControllingCamera) {
                // A. 操作中： カメラの値を Editor に逆反映 (Read)
                // カメラには書き込まない！
                if (settings_.gameFollowMode == Camera::FollowMode::kAimable ||
                    settings_.gameFollowMode == Camera::FollowMode::kFirstPerson) {

                    Vector3 currentRot = camera->GetRotation();
                    float toDeg = 180.0f / 3.14159265f;

                    settings_.angle.x = currentRot.x * toDeg;
                    settings_.angle.y = currentRot.y * toDeg;
                    settings_.angle.z = currentRot.z * toDeg;
                }

            } else {

                camera->ConfigAimable(settings_.distance, settings_.height, settings_.angle);
            }

            // これらは操作中でも反映してOK
            camera->SetLockOnOffset(settings_.lockOnOffset);
            camera->ConfigFixedPoint(settings_.fixedPointPos);
        }

        if (settings_.gameFollowMode == Camera::FollowMode::kOrbit) {
            camera->SetOrbitParams(settings_.orbitRadius, settings_.orbitHeight, settings_.orbitSpeed);
        }

    } else {
        camera->SetFollowTarget(nullptr);
        UpdateFreeCamera(camera);
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
    // 2. カメラの向き(ローカル軸)の計算
    // ----------------------------------------------------------
    Vector3 forward, right, up;

    // 前方ベクトル (カメラが向いている方向)
    forward.x = std::sin(rotation.y) * std::cos(rotation.x);
    forward.y = -std::sin(rotation.x);
    forward.z = std::cos(rotation.y) * std::cos(rotation.x);

    // 右ベクトル (カメラから見た水平の右方向)
    right.x = std::cos(rotation.y);
    right.y = 0.0f;
    right.z = -std::sin(rotation.y);

    // ★追加: 上ベクトル (前方と右の外積で、カメラから見た「本当の上」を計算)
    up.x = forward.y * right.z - forward.z * right.y;
    up.y = forward.z * right.x - forward.x * right.z;
    up.z = forward.x * right.y - forward.y * right.x;

    // ----------------------------------------------------------
    // 3. 移動処理
    // ----------------------------------------------------------
    Vector3 moveVelocity = { 0, 0, 0 };

    // Shiftキーで加速
    float speed = (input->IsKeyPressed(DIK_LSHIFT) || input->IsKeyPressed(DIK_RSHIFT))
        ? settings_.boostSpeed
        : settings_.moveSpeed;

    // ★ W/S: カメラの「向いている方向」へ前進・後退 (Unity/Unreal風)
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

    // ★ A/D: カメラの左右へ平行移動
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

    // ★ Q/E: 空間の上下へ移動 (Unity/Unreal共通のショートカット)
    if (input->IsKeyPressed(DIK_E)) {
        moveVelocity.y += speed; // Eで上昇
    }
    if (input->IsKeyPressed(DIK_Q)) {
        moveVelocity.y -= speed; // Qで下降
    }

    // ★ 中クリック (ホイール押し込み) : パン(平行)移動
    // 視点を変えずに、カメラをそのまま上下左右にスライドさせます
    if (input->IsMouseButtonPressed(2)) { // 2 = Middle Click
        Vector2 mouseDelta = input->GetMouseMoveDelta();
        float panSpeed = settings_.moveSpeed * 0.1f; // マウスの動きに対して少しマイルドに調整

        // X移動: マウスの動きと逆方向に右ベクトルを使って移動
        moveVelocity.x -= right.x * mouseDelta.x * panSpeed;
        moveVelocity.y -= right.y * mouseDelta.x * panSpeed;
        moveVelocity.z -= right.z * mouseDelta.x * panSpeed;

        // Y移動: マウスの動きに合わせて上ベクトルを使って移動
        moveVelocity.x += up.x * mouseDelta.y * panSpeed;
        moveVelocity.y += up.y * mouseDelta.y * panSpeed;
        moveVelocity.z += up.z * mouseDelta.y * panSpeed;
    }

    // ★ ホイール回転: ズーム移動 (維持)
    float wheelDelta = input->GetMouseWheelDelta();
    if (wheelDelta != 0.0f) {
        float wheelDir = (wheelDelta > 0.0f) ? 1.0f : -1.0f;
        float zoomSpeed = speed * 3.0f;

        moveVelocity.x += forward.x * zoomSpeed * wheelDir;
        moveVelocity.y += forward.y * zoomSpeed * wheelDir;
        moveVelocity.z += forward.z * zoomSpeed * wheelDir;
    }

    // 座標更新
    eye.x += moveVelocity.x;
    eye.y += moveVelocity.y;
    eye.z += moveVelocity.z;

    camera->SetEye(eye);

    // ----------------------------------------------------------
    // 4. ターゲットの更新
    // ----------------------------------------------------------
    Vector3 newTarget;
    newTarget.x = eye.x + forward.x * 10.0f;
    newTarget.y = eye.y + forward.y * 10.0f;
    newTarget.z = eye.z + forward.z * 10.0f;

    camera->SetTarget(newTarget);
}

void CameraEditor::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_VIDEO " --- カメラエディタ (Camera Editor) ---");

    // ---------------------------------------------------------
    // 1. ファイル管理セクション
    // ---------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " ファイル管理 (File Manager)", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int currentItem = -1;
        if (ImGui::BeginCombo(ICON_FA_HISTORY " ファイル選択", "Choose from list...")) {
            for (int i = 0; i < (int)fileList_.size(); i++) {
                bool isSelected = (currentItem == i);
                if (ImGui::Selectable(fileList_[i].c_str(), isSelected)) {
                    currentItem = i;
                    strcpy_s(fileNameBuffer_, sizeof(fileNameBuffer_), fileList_[i].c_str());
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::InputText(ICON_FA_FILE_SIGNATURE " ファイル名(.json)", fileNameBuffer_, sizeof(fileNameBuffer_));

        if (ImGui::Button(ICON_FA_UPLOAD " ロード")) LoadSettings();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_DOWNLOAD " セーブ")) SaveSettings();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SYNC " 更新")) RefreshFileList();
    }

    ImGui::Separator();
    ImGui::Spacing();

    // ---------------------------------------------------------
    // 2. モード選択
    // ---------------------------------------------------------
    const char* modeNames[] = { "ゲームカメラ (Game)", "自由カメラ (Editor)" };
    int currentModeInt = static_cast<int>(settings_.currentMode);
    if (ImGui::Combo(ICON_FA_COGS " メインモード", &currentModeInt, modeNames, IM_ARRAYSIZE(modeNames))) {
        settings_.currentMode = static_cast<Mode>(currentModeInt);
    }

    ImGui::Separator();

    // ---------------------------------------------------------
    // 3. 各モードごとの詳細設定
    // ---------------------------------------------------------
    if (settings_.currentMode == Mode::Game) {
        // --- Game Mode 設定 ---
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ICON_FA_GAMEPAD " カメラ挙動設定 (Game)");

        const char* followModeNames[] = {
            "固定 (Fixed)",
            "3人称 (Aimable)",
            "1人称 (FPS)",
            "ロックオン",
            "周回 (Orbit)",
            "定点注視 (FixedPoint)"
        };
        int currentFollow = static_cast<int>(settings_.gameFollowMode);
        if (ImGui::Combo(ICON_FA_EYE " View Type", &currentFollow, followModeNames, IM_ARRAYSIZE(followModeNames))) {
            settings_.gameFollowMode = static_cast<Camera::FollowMode>(currentFollow);
        }

        if (settings_.gameFollowMode == Camera::FollowMode::kAimable ||
            settings_.gameFollowMode == Camera::FollowMode::kFixed) {
            ImGui::DragFloat(ICON_FA_ARROWS_ALT " 距離 (Distance)", &settings_.distance, 0.1f, 1.0f, 100.0f);
            ImGui::DragFloat(ICON_FA_ARROWS_ALT_V " 高さ (Height)", &settings_.height, 0.1f, 0.0f, 50.0f);
            ImGui::DragFloat3(ICON_FA_SYNC " 角度 (X/Y/Z)", &settings_.angle.x, 0.1f, -180.0f, 180.0f);
        }

        if (settings_.gameFollowMode == Camera::FollowMode::kOrbit) {
            ImGui::Separator();
            ImGui::Text(ICON_FA_REDO " 周回設定");
            ImGui::DragFloat(" 半径 (Radius)", &settings_.orbitRadius, 0.1f, 1.0f, 100.0f);
            ImGui::DragFloat(" 高さ (Height)", &settings_.orbitHeight, 0.1f, -10.0f, 50.0f);
            ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 回転速度 (Speed)", &settings_.orbitSpeed, 0.0001f, -0.1f, 0.1f, "%.4f");
        }

        if (settings_.gameFollowMode == Camera::FollowMode::kFixedPoint) {
            ImGui::Separator();
            ImGui::Text(ICON_FA_MAP_MARKER_ALT " 定点カメラ設定");
            ImGui::DragFloat3(" カメラ座標", &settings_.fixedPointPos.x, 0.1f);
            if (ImGui::Button(ICON_FA_CROSSHAIRS " 現在のカメラ位置をセット")) {
                Camera* cam = CameraManager::GetInstance()->GetMainCamera();
                if (cam) settings_.fixedPointPos = cam->GetEye();
            }
        }
    }
    else {
        // --- Editor Mode 設定 ---
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), ICON_FA_MOUSE_POINTER " 自由操作設定 (Editor)");
        ImGui::TextDisabled("右クリック押下 + WASD で移動\nQ/E で上下移動 | Shift でブースト");

        ImGui::Spacing();
        ImGui::SliderFloat(ICON_FA_TACHOMETER_ALT " 移動速度", &settings_.moveSpeed, 0.1f, 5.0f);
        ImGui::SliderFloat(ICON_FA_FAST_FORWARD " 加速速度", &settings_.boostSpeed, 1.0f, 10.0f);
        ImGui::SliderFloat(ICON_FA_MOUSE " マウス感度", &settings_.mouseSensitivity, 0.001f, 0.05f);

        Camera* camera = CameraManager::GetInstance()->GetMainCamera();
        if (camera) {
            Vector3 pos = camera->GetEye();
            ImGui::TextDisabled(ICON_FA_LOCATION_ARROW " 現在座標: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
        }
    }
#endif
}
void CameraEditor::SaveSettings() {
    // ディレクトリパス + 入力されたファイル名 を結合
    std::string filePath = kDirectoryPath_ + std::string(fileNameBuffer_);

    json j;
    //j["mode"] = static_cast<int>(settings_.currentMode);
    j["gameFollowMode"] = static_cast<int>(settings_.gameFollowMode);
    j["distance"] = settings_.distance;
    j["height"] = settings_.height;
    j["angle"] = { settings_.angle.x, settings_.angle.y, settings_.angle.z };
    j["lockOnOffset"] = { settings_.lockOnOffset.x, settings_.lockOnOffset.y, settings_.lockOnOffset.z };

    //  周回(Orbit)モード用パラメータの保存
    j["orbitRadius"] = settings_.orbitRadius;
    j["orbitHeight"] = settings_.orbitHeight;
    j["orbitSpeed"] = settings_.orbitSpeed;

    // エディタ設定
    j["moveSpeed"] = settings_.moveSpeed;
    j["boostSpeed"] = settings_.boostSpeed;
    j["mouseSensitivity"] = settings_.mouseSensitivity;
    j["fixedPointPos"] = { settings_.fixedPointPos.x, settings_.fixedPointPos.y, settings_.fixedPointPos.z };
    std::ofstream file(filePath);
    if (file.is_open()) {
        file << j.dump(4);
    }

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
      /*  if (j.contains("mode")) settings_.currentMode = static_cast<Mode>(j["mode"]);*/
        if (j.contains("gameFollowMode")) settings_.gameFollowMode = static_cast<Camera::FollowMode>(j["gameFollowMode"]);

        if (j.contains("distance")) settings_.distance = j["distance"];
        if (j.contains("height")) settings_.height = j["height"];
        if (j.contains("angle")) {
            if (j["angle"].is_array()) {
                settings_.angle.x = j["angle"][0];
                settings_.angle.y = j["angle"][1];
                settings_.angle.z = j["angle"][2];
            } else {
                // 古いデータ(float)の場合はX(Pitch)にだけ入れる
                settings_.angle.x = j["angle"];
                settings_.angle.y = 0.0f;
                settings_.angle.z = 0.0f;
            }
        }
        if (j.contains("lockOnOffset") && j["lockOnOffset"].is_array()) {
            settings_.lockOnOffset.x = j["lockOnOffset"][0];
            settings_.lockOnOffset.y = j["lockOnOffset"][1];
            settings_.lockOnOffset.z = j["lockOnOffset"][2];
        }

        //  周回(Orbit)モード用パラメータの読み込み
        if (j.contains("orbitRadius")) settings_.orbitRadius = j["orbitRadius"];
        if (j.contains("orbitHeight")) settings_.orbitHeight = j["orbitHeight"];
        if (j.contains("orbitSpeed"))  settings_.orbitSpeed = j["orbitSpeed"];

        if (j.contains("moveSpeed")) settings_.moveSpeed = j["moveSpeed"];
        if (j.contains("boostSpeed")) settings_.boostSpeed = j["boostSpeed"];
        if (j.contains("mouseSensitivity")) settings_.mouseSensitivity = j["mouseSensitivity"];
        if (j.contains("fixedPointPos") && j["fixedPointPos"].is_array()) {
            settings_.fixedPointPos.x = j["fixedPointPos"][0];
            settings_.fixedPointPos.y = j["fixedPointPos"][1];
            settings_.fixedPointPos.z = j["fixedPointPos"][2];
        }
    }
    catch (...) {
        // エラーハンドリング
    }
}


void CameraEditor::LoadFile(const std::string& fileName) {
    // 1. ファイル名バッファを更新
    strcpy_s(fileNameBuffer_, sizeof(fileNameBuffer_), fileName.c_str());

    // 2. パスを作成
    std::string filePath = kDirectoryPath_ + std::string(fileNameBuffer_);

    // 3. ファイルが存在するかチェック
    if (!fs::exists(filePath)) {
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