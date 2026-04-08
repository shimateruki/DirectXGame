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
    targetPlayer_ = player;
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

            // =========================================================
            // 入力状態のチェック
            // =========================================================
            // マウスの移動量チェック
            Vector2 mouseDelta = input->GetMouseMoveDelta();
            bool isMouseMoving = (std::abs(mouseDelta.x) > 0.0f || std::abs(mouseDelta.y) > 0.0f);

            // ジャイロ入力チェック (感度 0.05f)
            Vector3 gyro = input->GetGyroscope();
            bool isGyroActive = (std::abs(gyro.x) > 0.05f || std::abs(gyro.y) > 0.05f || std::abs(gyro.z) > 0.05f);

            // =========================================================
            // 操作中判定（環境による分岐）
            // =========================================================
#ifdef USE_IMGUI
            // ★ Debug/Develop環境：エディタ操作の誤爆を防ぐため、右クリック中のみ操作中とみなす
            bool isControllingCamera = input->IsMouseButtonPressed(1) ||
#else
            // ★ Release環境：右クリック不要！マウスを動かしただけでも操作中とみなす
            bool isControllingCamera = input->IsMouseButtonPressed(1) || isMouseMoving ||
#endif
                (std::abs(input->GetRightStick().x) > 0.1f) ||
                (std::abs(input->GetRightStick().y) > 0.1f) ||
                isGyroActive;

            // =========================================================
            // カメラの値の反映処理
            // =========================================================
            if (isControllingCamera) {
                // 操作中： カメラの値を Editor に逆反映 (Read)
                // カメラには書き込まない！
                if (settings_.gameFollowMode == Camera::FollowMode::kAimable ||
                    settings_.gameFollowMode == Camera::FollowMode::kFirstPerson) {

                    Vector3 currentRot = camera->GetRotation();
                    float toDeg = 180.0f / 3.14159265f;

                    settings_.angle.x = currentRot.x * toDeg;
                    settings_.angle.y = currentRot.y * toDeg;
                    settings_.angle.z = currentRot.z * toDeg;
                }
            }
            else {
                // 操作していない時だけ、設定値をカメラに流し込む
                camera->ConfigAimable(settings_.distance, settings_.height, settings_.angle);
            }

            // これらは操作中でも反映してOK
            camera->SetLockOnOffset(settings_.lockOnOffset);
            camera->ConfigFixedPoint(settings_.fixedPointPos);
        }

        if (settings_.gameFollowMode == Camera::FollowMode::kOrbit) {
            camera->SetOrbitParams(settings_.orbitRadius, settings_.orbitHeight, settings_.orbitSpeed);
        }

    }
    else {
        camera->SetFollowTarget(nullptr);
        if (!camera->IsOverridden()) {
            UpdateFreeCamera(camera);
        }
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

    // 上ベクトル (前方と右の外積で、カメラから見た「本当の上」を計算)
    up.x = forward.y * right.z - forward.z * right.y;
    up.y = forward.z * right.x - forward.x * right.z;
    up.z = forward.x * right.y - forward.y * right.x;

    // ----------------------------------------------------------
    // 3. 移動・ズーム処理
    // ----------------------------------------------------------
    Vector3 moveVelocity = { 0, 0, 0 };

    // ==========================================================
    // ★ GameViewをホバーしているか、右クリック中のみ移動・ズームを許可
    // ==========================================================
    if (isGameViewHovered_ || input->IsMouseButtonPressed(1)) {

        // Shiftキーで加速
        float speed = (input->IsKeyPressed(DIK_LSHIFT) || input->IsKeyPressed(DIK_RSHIFT))
            ? settings_.boostSpeed
            : settings_.moveSpeed;

        // ★ W/S: カメラの「向いている方向」へ前進・後退
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

        // ★ Q/E: 空間の上下へ移動
        if (input->IsKeyPressed(DIK_E)) {
            moveVelocity.y += speed; // Eで上昇
        }
        if (input->IsKeyPressed(DIK_Q)) {
            moveVelocity.y -= speed; // Qで下降
        }

        // ★ ホイール回転: ズーム移動
        float wheelDelta = input->GetMouseWheelDelta();
        if (wheelDelta != 0.0f) {
            float wheelDir = (wheelDelta > 0.0f) ? 1.0f : -1.0f;
            float zoomSpeed = speed * 3.0f;

            moveVelocity.x += forward.x * zoomSpeed * wheelDir;
            moveVelocity.y += forward.y * zoomSpeed * wheelDir;
            moveVelocity.z += forward.z * zoomSpeed * wheelDir;
        }
    }

    // ★ 中クリック (ホイール押し込み) : パン(平行)移動
    // これもGameViewホバー中か、すでに中クリックを押している時だけ許可
    if (isGameViewHovered_ || input->IsMouseButtonPressed(2)) {
        if (input->IsMouseButtonPressed(2)) { // 2 = Middle Click
            Vector2 mouseDelta = input->GetMouseMoveDelta();
            float panSpeed = settings_.moveSpeed * 0.1f;

            // X移動: マウスの動きと逆方向に右ベクトルを使って移動
            moveVelocity.x -= right.x * mouseDelta.x * panSpeed;
            moveVelocity.y -= right.y * mouseDelta.x * panSpeed;
            moveVelocity.z -= right.z * mouseDelta.x * panSpeed;

            // Y移動: マウスの動きに合わせて上ベクトルを使って移動
            moveVelocity.x += up.x * mouseDelta.y * panSpeed;
            moveVelocity.y += up.y * mouseDelta.y * panSpeed;
            moveVelocity.z += up.z * mouseDelta.y * panSpeed;
        }
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

    // =========================================================
    // 1. ファイル管理セクション
    // =========================================================
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

    // =========================================================
    // 2. モード選択
    // =========================================================
    const char* modeNames[] = { "ゲームカメラ (Game)", "自由カメラ (Editor)" };
    int currentModeInt = static_cast<int>(settings_.currentMode);
    if (ImGui::Combo(ICON_FA_COGS " メインモード", &currentModeInt, modeNames, IM_ARRAYSIZE(modeNames))) {
        settings_.currentMode = static_cast<Mode>(currentModeInt);
    }

    ImGui::Separator();

    // =========================================================
    // 3. 各モードごとの詳細設定
    // =========================================================
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

    ImGui::Spacing();
    ImGui::Separator();

    // =========================================================
    // 4. シネマティックカメラ（オーバーライド）設定
    // =========================================================
    if (ImGui::CollapsingHeader(ICON_FA_VIDEO " 演出カメラ設定 (Cinematic Camera)")) {

        // --- 新規カメラの作成 ---
        ImGui::InputText("新規カメラ名", newOverrideNameBuffer_, sizeof(newOverrideNameBuffer_));
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS " 追加")) {
            std::string nName = newOverrideNameBuffer_;
            if (!nName.empty() && overrideParamsMap_.find(nName) == overrideParamsMap_.end()) {
                overrideParamsMap_[nName] = Camera::CameraOverrideParams(); // 新規作成
                selectedOverrideName_ = nName;
                newOverrideNameBuffer_[0] = '\0'; // 入力欄クリア
                SaveSettings(); // 追加したら即保存
            }
        }

        // --- 登録済みカメラのリスト ---
        ImGui::Text(ICON_FA_LIST " 保存済みカメラ一覧:");
        if (ImGui::BeginListBox("##CameraList", ImVec2(-FLT_MIN, 100))) {
            for (auto& [name, param] : overrideParamsMap_) {
                bool isSelected = (selectedOverrideName_ == name);
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    selectedOverrideName_ = name;
                }
            }
            ImGui::EndListBox();
        }

        // --- 選択中のカメラの詳細設定 ---
        if (!selectedOverrideName_.empty() && overrideParamsMap_.find(selectedOverrideName_) != overrideParamsMap_.end()) {
            auto& p = overrideParamsMap_[selectedOverrideName_];
            bool changed = false;

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "編集中: [%s]", selectedOverrideName_.c_str());
            ImGui::Indent();
            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_CAMERA " 現在のカメラ視点をここにコピー (Copy Current View)")) {
                Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                if (camera) {
                    // 位置をそのままコピー
                    p.fixedEyePos = camera->GetEye();

                    if (targetPlayer_) {
                        Vector3 pPos = targetPlayer_->GetWorldPosition();
                        pPos.y += 5.0f; // プレイヤーの少し上（注視点）を基準にする
                        p.fixedTargetPos = pPos;
                    }
                    else {
                        p.fixedTargetPos = camera->GetTargetPoint();
                    }

                    // その場所・その角度に完全に固定するため、追従フラグはすべてOFFにする
                    p.trackEyeX = false; p.trackEyeY = false; p.trackEyeZ = false;
                    p.trackTargetX = false; p.trackTargetY = false; p.trackTargetZ = false;

                    changed = true; // セーブフラグを立てる
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::DragFloat("移行時間 (秒)", &p.duration, 0.1f, 0.0f, 10.0f)) changed = true;

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ICON_FA_EYE " カメラ位置 (Eye)");
            if (ImGui::Checkbox("X軸追従##Eye", &p.trackEyeX)) changed = true; ImGui::SameLine();
            if (ImGui::Checkbox("Y軸追従##Eye", &p.trackEyeY)) changed = true; ImGui::SameLine();
            if (ImGui::Checkbox("Z軸追従##Eye", &p.trackEyeZ)) changed = true;
            if (ImGui::DragFloat3("固定座標##Eye", &p.fixedEyePos.x, 0.1f)) changed = true;

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), ICON_FA_CROSSHAIRS " 注視点 (Target)");
            if (ImGui::Checkbox("X軸追従##Tgt", &p.trackTargetX)) changed = true; ImGui::SameLine();
            if (ImGui::Checkbox("Y軸追従##Tgt", &p.trackTargetY)) changed = true; ImGui::SameLine();
            if (ImGui::Checkbox("Z軸追従##Tgt", &p.trackTargetZ)) changed = true;
            if (ImGui::DragFloat3("固定座標##Tgt", &p.fixedTargetPos.x, 0.1f)) changed = true;

            ImGui::Unindent();

            // ★ エディタ上で動きを確認できる神機能！
            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_PLAY " テスト再生 (Test Play)")) {
                Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                if (camera) PlayOverrideCamera(camera, selectedOverrideName_);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_STOP " 停止 (Stop)")) {
                Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                if (camera) camera->EndOverride(1.0f);
            }

            if (changed) {
                SaveSettings(); // いじったら即保存
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button(ICON_FA_TRASH " 選択中のカメラを削除")) {
                overrideParamsMap_.erase(selectedOverrideName_);
                selectedOverrideName_ = "";
                SaveSettings();
            }
            ImGui::PopStyleColor();
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
    json overridesJson = json::object();
    for (const auto& [name, param] : overrideParamsMap_) {
        json p;
        p["duration"] = param.duration;
        p["trackEyeX"] = param.trackEyeX;
        p["trackEyeY"] = param.trackEyeY;
        p["trackEyeZ"] = param.trackEyeZ;
        p["fixedEyePos"] = { param.fixedEyePos.x, param.fixedEyePos.y, param.fixedEyePos.z };

        p["trackTargetX"] = param.trackTargetX;
        p["trackTargetY"] = param.trackTargetY;
        p["trackTargetZ"] = param.trackTargetZ;
        p["fixedTargetPos"] = { param.fixedTargetPos.x, param.fixedTargetPos.y, param.fixedTargetPos.z };

        overridesJson[name] = p; // JSONオブジェクトに追加
    }
    j["Overrides"] = overridesJson; // 大元のJSONに「Overrides」という項目でまとめる
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
        overrideParamsMap_.clear();
        if (j.contains("Overrides")) {
            for (auto& [key, val] : j["Overrides"].items()) {
                Camera::CameraOverrideParams p;
                p.duration = val.value("duration", 1.0f);
                p.trackEyeX = val.value("trackEyeX", false);
                p.trackEyeY = val.value("trackEyeY", false);
                p.trackEyeZ = val.value("trackEyeZ", false);
                if (val.contains("fixedEyePos")) {
                    p.fixedEyePos = { val["fixedEyePos"][0], val["fixedEyePos"][1], val["fixedEyePos"][2] };
                }
                p.trackTargetX = val.value("trackTargetX", true);
                p.trackTargetY = val.value("trackTargetY", true);
                p.trackTargetZ = val.value("trackTargetZ", true);
                if (val.contains("fixedTargetPos")) {
                    p.fixedTargetPos = { val["fixedTargetPos"][0], val["fixedTargetPos"][1], val["fixedTargetPos"][2] };
                }
                overrideParamsMap_[key] = p;
            }
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

bool CameraEditor::PlayOverrideCamera(Camera* camera, const std::string& cameraName) {
    if (!camera) return false;

    // 登録されているカメラ名を探す
    auto it = overrideParamsMap_.find(cameraName);
    if (it != overrideParamsMap_.end()) {
        // 見つかったら、その設定をCameraクラスに渡して実行！
        camera->StartOverride(it->second);
        return true;
    }

    // 見つからなかった場合はエラーを出す
    DebugConsole::GetInstance()->AddLog("Error: Camera Override '" + cameraName + "' not found!");
    return false;
}