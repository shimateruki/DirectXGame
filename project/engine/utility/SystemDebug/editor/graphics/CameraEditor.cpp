#include "CameraEditor.h"
#include "CameraManager.h"
#include "InputManager.h" // 入力取得に必要
#include "PrimitiveDrawer.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "json.hpp"
#include <fstream>
#include <cmath>
#include <filesystem> 
#include <DebugConsole.h>
#include "IconsFontAwesome5.h"
using json = nlohmann::json;
namespace fs = std::filesystem; // 短縮用

namespace {
    constexpr float kPi = 3.14159265f;

    float DegToRad(float degrees) {
        return degrees * kPi / 180.0f;
    }

    float RadToDeg(float radians) {
        return radians * 180.0f / kPi;
    }

    Matrix4x4 MakeLineBoxMatrix(const Vector3& start, const Vector3& end, float thickness) {
        Math math;
        Vector3 diff = end - start;
        float length = math.Length(diff);
        if (length < 0.001f) {
            length = 0.001f;
        }

        Vector3 center = (start + end) * 0.5f;
        float yaw = std::atan2(diff.x, diff.z);
        float horizontalLength = std::sqrt(diff.x * diff.x + diff.z * diff.z);
        float pitch = std::atan2(-diff.y, horizontalLength);

        Matrix4x4 scale = math.MakeScaleMatrix({ thickness, thickness, length });
        Matrix4x4 rotate = math.Multiply(math.MakeRotateXMatrix(pitch), math.MakeRotateYMatrix(yaw));
        Matrix4x4 translate = math.MakeTranslateMatrix(center);
        return math.Multiply(math.Multiply(scale, rotate), translate);
    }

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
    // 自由カメラ専用の状態を読み込む
    LoadEditorState();

    // 自由カメラの位置を復元
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->SetEye(settings_.editorCameraPos);
        camera->SetRotation(settings_.editorCameraAngle);
        camera->Update();
    }
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
            std::string fileName = entry.path().filename().string();
            // エディタ状態保存用のファイルはリストに表示しない
            if (fileName == "editor_camera_state.json") continue;
            fileList_.push_back(fileName);
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
        camera->SetFollowMode(settings_.gameFollowMode);
        camera->SetLockOnOffset(settings_.lockOnOffset);
        camera->ConfigFixedPoint(settings_.fixedPointPos, settings_.fixedPointAngle);

        if (settings_.gameFollowMode == Camera::FollowMode::kOrbit) {
            camera->SetOrbitParams(settings_.orbitRadius, settings_.orbitHeight, settings_.orbitSpeed);
            camera->SetOrbitCenterOffset(settings_.orbitCenterOffset);
            camera->SetOrbitCenterHeight(settings_.orbitCenterHeight);
        }

        if (player) {
            camera->SetFollowTarget(player);
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
            bool isControllingCamera = input->IsMouseButtonPressed(1) || isMouseMoving ||
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
                // 操作していない時だけ、モードに必要な設定値をカメラに流し込む
                if (settings_.gameFollowMode == Camera::FollowMode::kAimable ||
                    settings_.gameFollowMode == Camera::FollowMode::kFixed ||
                    settings_.gameFollowMode == Camera::FollowMode::kFirstPerson) {
                    camera->ConfigAimable(settings_.distance, settings_.height, settings_.angle);
                }
            }
        }
        else {
            camera->SetFollowTarget(nullptr);
        }

    }
    else {
        camera->SetFollowTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::kAimable);
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

    // GameViewをホバーしているか、右クリック中のみ移動・ズームを許可
    if (isGameViewHovered_ || input->IsMouseButtonPressed(1)) {

        // Shiftキーで加速
        float speed = (input->IsKeyPressed(DIK_LSHIFT) || input->IsKeyPressed(DIK_RSHIFT))
            ? settings_.boostSpeed
            : settings_.moveSpeed;

        // W/S: カメラの視線方向へ前進・後退
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

        // A/D: カメラの左右へ平行移動
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

        // Q/E: 空間の上下へ移動
        if (input->IsKeyPressed(DIK_E)) {
            moveVelocity.y += speed; // Eで上昇
        }
        if (input->IsKeyPressed(DIK_Q)) {
            moveVelocity.y -= speed; // Qで下降
        }

        // ホイール回転: ズーム移動
        float wheelDelta = input->GetMouseWheelDelta();
        if (wheelDelta != 0.0f) {
            float wheelDir = (wheelDelta > 0.0f) ? 1.0f : -1.0f;
            float zoomSpeed = speed * 3.0f;

            moveVelocity.x += forward.x * zoomSpeed * wheelDir;
            moveVelocity.y += forward.y * zoomSpeed * wheelDir;
            moveVelocity.z += forward.z * zoomSpeed * wheelDir;
        }
    }

    // 中クリック (ホイール押し込み) : パン(平行)移動
    // GameViewホバー中、または中クリック押下中のみ許可
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
    if (!IsEditorStateSaveBlocked()) {
        settings_.editorCameraPos = eye;
        settings_.editorCameraAngle = rotation;
    }

    if (!IsEditorStateSaveBlocked()) {
        static float saveTimer = 0.0f;
        saveTimer += 0.016f; // おおよそ60fps
        if (saveTimer > 1.0f) { // 1秒ごとに自動保存
            SaveEditorState(); // 専用ファイルに保存
            saveTimer = 0.0f;
        }
    }

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
            if (settings_.gameFollowMode == Camera::FollowMode::kOrbit) {
                ApplyOrbitStartAngle(CameraManager::GetInstance()->GetMainCamera(), true);
            }
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

        if (settings_.gameFollowMode == Camera::FollowMode::kOrbit) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "周回ガイド");
            ImGui::Checkbox("ガイドを表示", &settings_.orbitGuideVisible);
            ImGui::Checkbox("自由カメラ編集ギズモを表示", &settings_.orbitCenterGizmoVisible);
            const char* editGizmoTargets[] = { "周回中心", "開始カメラ位置" };
            ImGui::Combo("自由カメラ時のギズモ対象", &orbitEditGizmoTarget_, editGizmoTargets, IM_ARRAYSIZE(editGizmoTargets));
            ImGui::TextDisabled("自由カメラで外から見ながら、周回カメラの中心や開始位置を調整します。");
            ImGui::DragFloat("注視点の高さ", &settings_.orbitCenterHeight, 0.1f, -10.0f, 50.0f);
            if (ImGui::DragFloat3("周回中心オフセット", &settings_.orbitCenterOffset.x, 0.1f, -80.0f, 80.0f)) {
                ApplyOrbitStartAngle(CameraManager::GetInstance()->GetMainCamera(), true);
            }
            if (ImGui::Button("周回中心オフセットをリセット")) {
                settings_.orbitCenterOffset = { 0.0f, 0.0f, 0.0f };
                ApplyOrbitStartAngle(CameraManager::GetInstance()->GetMainCamera(), true);
                SaveSettings();
            }
            if (ImGui::DragFloat("開始角度", &settings_.orbitStartAngleDeg, 0.5f, -360.0f, 360.0f, "%.1f deg")) {
                ApplyOrbitStartAngle(CameraManager::GetInstance()->GetMainCamera(), true);
            }
            ImGui::DragFloat("ガイド点の大きさ", &settings_.orbitGuideMarkerSize, 0.01f, 0.1f, 2.0f);

            Vector3 center = GetOrbitCenter();
            if (ImGui::Button(ICON_FA_REDO " 現在位置を周回開始位置にセット")) {
                CaptureOrbitStartFromCurrentCamera(CameraManager::GetInstance()->GetMainCamera());
            }
            ImGui::TextDisabled("周回中心: %.2f, %.2f, %.2f", center.x, center.y, center.z);

            if (ImGui::Button("現在の角度を開始角度にする")) {
                Camera* cam = CameraManager::GetInstance()->GetMainCamera();
                if (cam) {
                    Vector3 diff = cam->GetEye() - center;
                    settings_.orbitStartAngleDeg = RadToDeg(std::atan2(diff.z, diff.x));
                    ApplyOrbitStartAngle(cam, true);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("開始位置をプレビュー")) {
                ApplyOrbitStartAngle(CameraManager::GetInstance()->GetMainCamera(), true);
            }
        }

        if (settings_.gameFollowMode == Camera::FollowMode::kFixedPoint) {
            ImGui::Separator();
            ImGui::Text(ICON_FA_MAP_MARKER_ALT " 定点カメラ設定");
            ImGui::DragFloat3(" カメラ座標", &settings_.fixedPointPos.x, 0.1f);
            ImGui::DragFloat3(" カメラ角度", &settings_.fixedPointAngle.x, 0.05f);
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
            Vector3 rot = camera->GetRotation();
            ImGui::TextDisabled(ICON_FA_LOCATION_ARROW " 現在座標: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
            ImGui::TextDisabled(ICON_FA_SYNC " 現在角度: %.2f, %.2f, %.2f", rot.x, rot.y, rot.z);

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), ICON_FA_SAVE " 自由カメラ保存");
            ImGui::TextDisabled("保存済み座標: %.2f, %.2f, %.2f", settings_.editorCameraPos.x, settings_.editorCameraPos.y, settings_.editorCameraPos.z);
            ImGui::TextDisabled("保存済み角度: %.2f, %.2f, %.2f", settings_.editorCameraAngle.x, settings_.editorCameraAngle.y, settings_.editorCameraAngle.z);

            if (ImGui::Button(ICON_FA_SAVE " 現在の自由カメラ位置を保存")) {
                settings_.editorCameraPos = pos;
                settings_.editorCameraAngle = rot;
                SaveEditorState();
                DebugConsole::GetInstance()->AddLog("Editor free camera position saved.");
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_UNDO " 保存済み自由カメラへ戻す")) {
                LoadEditorState();
                SetEditorCameraTransform(settings_.editorCameraPos, settings_.editorCameraAngle);
                camera->Update();
                DebugConsole::GetInstance()->AddLog("Editor free camera position restored.");
            }

            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_REDO " 現在位置を「周回カメラ」の開始位置にセット")) {
                CaptureOrbitStartFromCurrentCamera(camera);
            }
            if (ImGui::Button(ICON_FA_MAP_MARKER_ALT " 現在位置を「定点カメラ」の座標・角度にセット")) {
                settings_.fixedPointPos = pos;
                settings_.fixedPointAngle = rot; // 角度も記録
                SaveSettings(); // セットしたら自動でセーブ
            }
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
                    p.fixedTargetPos = camera->GetTargetPoint();

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

            // エディタ上で動作を確認可能
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
    j["orbitCenterOffset"] = { settings_.orbitCenterOffset.x, settings_.orbitCenterOffset.y, settings_.orbitCenterOffset.z };
    j["orbitCenterHeight"] = settings_.orbitCenterHeight;
    j["orbitHeight"] = settings_.orbitHeight;
    j["orbitSpeed"] = settings_.orbitSpeed;
    j["orbitStartAngleDeg"] = settings_.orbitStartAngleDeg;
    j["orbitGuideVisible"] = settings_.orbitGuideVisible;
    j["orbitCenterGizmoVisible"] = settings_.orbitCenterGizmoVisible;
    j["orbitGuideMarkerSize"] = settings_.orbitGuideMarkerSize;
    j["cameraSensitivity"] = settings_.cameraSensitivity;
    // エディタ設定
    j["moveSpeed"] = settings_.moveSpeed;
    j["boostSpeed"] = settings_.boostSpeed;
    j["mouseSensitivity"] = settings_.mouseSensitivity;
    j["fixedPointPos"] = { settings_.fixedPointPos.x, settings_.fixedPointPos.y, settings_.fixedPointPos.z };
    j["fixedPointAngle"] = { settings_.fixedPointAngle.x, settings_.fixedPointAngle.y, settings_.fixedPointAngle.z };
    
    // editorCameraPos, editorCameraAngle はここでは保存しない (SaveEditorStateに移行)

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
            }
            else {
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
        if (j.contains("orbitCenterOffset") && j["orbitCenterOffset"].is_array() && j["orbitCenterOffset"].size() >= 3) {
            settings_.orbitCenterOffset.x = j["orbitCenterOffset"][0];
            settings_.orbitCenterOffset.y = j["orbitCenterOffset"][1];
            settings_.orbitCenterOffset.z = j["orbitCenterOffset"][2];
        }
        if (j.contains("orbitCenterHeight")) settings_.orbitCenterHeight = j["orbitCenterHeight"];
        if (j.contains("orbitHeight")) settings_.orbitHeight = j["orbitHeight"];
        if (j.contains("orbitSpeed"))  settings_.orbitSpeed = j["orbitSpeed"];
        if (j.contains("orbitStartAngleDeg")) settings_.orbitStartAngleDeg = j["orbitStartAngleDeg"];
        if (j.contains("orbitGuideVisible")) settings_.orbitGuideVisible = j["orbitGuideVisible"];
        if (j.contains("orbitCenterGizmoVisible")) settings_.orbitCenterGizmoVisible = j["orbitCenterGizmoVisible"];
        if (j.contains("orbitGuideMarkerSize")) settings_.orbitGuideMarkerSize = j["orbitGuideMarkerSize"];
        if (j.contains("cameraSensitivity")) settings_.cameraSensitivity = j["cameraSensitivity"];

        if (j.contains("moveSpeed")) settings_.moveSpeed = j["moveSpeed"];
        if (j.contains("boostSpeed")) settings_.boostSpeed = j["boostSpeed"];
        if (j.contains("mouseSensitivity")) settings_.mouseSensitivity = j["mouseSensitivity"];
        if (j.contains("fixedPointPos") && j["fixedPointPos"].is_array()) {
            settings_.fixedPointPos.x = j["fixedPointPos"][0];
            settings_.fixedPointPos.y = j["fixedPointPos"][1];
            settings_.fixedPointPos.z = j["fixedPointPos"][2];
        }
        if (j.contains("fixedPointAngle") && j["fixedPointAngle"].is_array()) {
            settings_.fixedPointAngle.x = j["fixedPointAngle"][0];
            settings_.fixedPointAngle.y = j["fixedPointAngle"][1];
            settings_.fixedPointAngle.z = j["fixedPointAngle"][2];
        }
        
        // editorCameraPos, editorCameraAngle はここでは読み込まない (LoadEditorStateに移行)
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

void CameraEditor::SetEditorStateSaveBlocker(uint32_t blocker, bool enabled) {
    if (enabled) {
        editorStateSaveBlockers_ |= blocker;
    }
    else {
        editorStateSaveBlockers_ &= ~blocker;
    }
}

void CameraEditor::SetEditorCameraTransform(const Vector3& position, const Vector3& rotation) {
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->SetEye(position);
        camera->SetRotation(rotation);
        Vector3 forward;
        forward.x = std::sin(rotation.y) * std::cos(rotation.x);
        forward.y = -std::sin(rotation.x);
        forward.z = std::cos(rotation.y) * std::cos(rotation.x);
        camera->SetTarget(position + forward * 10.0f);
        // ターゲット追従を切らないと動かない場合があるので念のため
        camera->SetFollowTarget(nullptr);
    }
}

void CameraEditor::ApplyOrbitStartAngle(Camera* camera, bool resetSmoothing) {
    if (!camera) return;

    float angle = DegToRad(settings_.orbitStartAngleDeg);
    camera->SetOrbitCenterOffset(settings_.orbitCenterOffset);
    camera->SetOrbitCenterHeight(settings_.orbitCenterHeight);
    camera->SetOrbitParams(settings_.orbitRadius, settings_.orbitHeight, settings_.orbitSpeed);
    camera->SetOrbitAngle(angle);
    if (resetSmoothing) {
        camera->ResetFollowSmoothing();
    }
}

void CameraEditor::CaptureOrbitStartFromCurrentCamera(Camera* camera) {
    if (!camera) return;

    const Vector3 center = GetOrbitCenter();
    const Vector3 diff = camera->GetEye() - center;
    const float horizontalDistance = std::sqrt(diff.x * diff.x + diff.z * diff.z);

    if (horizontalDistance > 0.001f) {
        settings_.orbitRadius = (horizontalDistance < 1.0f) ? 1.0f : horizontalDistance;
        settings_.orbitStartAngleDeg = RadToDeg(std::atan2(diff.z, diff.x));
    }
    else {
        settings_.orbitRadius = 1.0f;
    }

    settings_.orbitHeight = diff.y;
    ApplyOrbitStartAngle(camera, true);
    SaveSettings();
    DebugConsole::GetInstance()->AddLog("Orbit camera start position captured from current camera.");
}

void CameraEditor::SetOrbitCenterFromWorld(const Vector3& center) {
    if (targetPlayer_) {
        Vector3 targetPos = targetPlayer_->GetWorldPosition();
        settings_.orbitCenterOffset = center - targetPos;
        settings_.orbitCenterOffset.y -= settings_.orbitCenterHeight;
        return;
    }

    settings_.fixedPointPos = center - settings_.orbitCenterOffset;
}

void CameraEditor::SetOrbitStartFromWorld(const Vector3& startEye) {
    const Vector3 center = GetOrbitCenter();
    const Vector3 diff = startEye - center;
    const float horizontalDistance = std::sqrt(diff.x * diff.x + diff.z * diff.z);

    if (horizontalDistance > 0.001f) {
        settings_.orbitRadius = (horizontalDistance < 1.0f) ? 1.0f : horizontalDistance;
        settings_.orbitStartAngleDeg = RadToDeg(std::atan2(diff.z, diff.x));
    }

    settings_.orbitHeight = diff.y;
}

Vector3 CameraEditor::GetOrbitCenter() const {
    if (targetPlayer_) {
        Vector3 center = targetPlayer_->GetWorldPosition();
        center.x += settings_.orbitCenterOffset.x;
        center.y += settings_.orbitCenterHeight + settings_.orbitCenterOffset.y;
        center.z += settings_.orbitCenterOffset.z;
        return center;
    }
    return settings_.fixedPointPos + settings_.orbitCenterOffset;
}

Vector3 CameraEditor::GetOrbitStartEye() const {
    const Vector3 center = GetOrbitCenter();
    const float startAngle = DegToRad(settings_.orbitStartAngleDeg);
    return {
        center.x + settings_.orbitRadius * std::cos(startAngle),
        center.y + settings_.orbitHeight,
        center.z + settings_.orbitRadius * std::sin(startAngle)
    };
}

void CameraEditor::DrawOrbitCenterGizmo(const Vector2& gameViewOffset, const Vector2& gameViewSize, bool snapEnabled, float snapValue) {
#ifdef USE_IMGUI
    if (settings_.currentMode != Mode::Editor) return;
    if (settings_.gameFollowMode != Camera::FollowMode::kOrbit) return;
    if (!settings_.orbitGuideVisible || !settings_.orbitCenterGizmoVisible) return;
    if (gameViewSize.x <= 1.0f || gameViewSize.y <= 1.0f) return;
    if (!isGameViewHovered_ && !isDraggingOrbitCenterGizmo_) return;

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return;

    Math math;
    orbitEditGizmoTarget_ = (std::clamp)(orbitEditGizmoTarget_, 0, 1);
    const bool editStartEye = orbitEditGizmoTarget_ == 1;
    Vector3 gizmoPosition = editStartEye ? GetOrbitStartEye() : GetOrbitCenter();
    Matrix4x4 world = math.MakeTranslateMatrix(gizmoPosition);
    Matrix4x4 view = camera->GetViewMatrix();
    Matrix4x4 projection = camera->GetProjectionMatrix();
    float snap[3] = { snapValue, snapValue, snapValue };

    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(gameViewOffset.x, gameViewOffset.y, gameViewSize.x, gameViewSize.y);
    ImGuizmo::Manipulate(
        &view.m[0][0],
        &projection.m[0][0],
        ImGuizmo::TRANSLATE,
        ImGuizmo::WORLD,
        &world.m[0][0],
        nullptr,
        (snapEnabled && snapValue > 0.0f) ? snap : nullptr);

    if (ImGuizmo::IsUsing()) {
        isDraggingOrbitCenterGizmo_ = true;
        Vector3 editedCenter = { world.m[3][0], world.m[3][1], world.m[3][2] };
        if (editStartEye) {
            SetOrbitStartFromWorld(editedCenter);
        } else {
            SetOrbitCenterFromWorld(editedCenter);
        }
        camera->SetOrbitCenterOffset(settings_.orbitCenterOffset);
        camera->SetOrbitCenterHeight(settings_.orbitCenterHeight);
        camera->SetOrbitParams(settings_.orbitRadius, settings_.orbitHeight, settings_.orbitSpeed);
    }
    else if (isDraggingOrbitCenterGizmo_) {
        isDraggingOrbitCenterGizmo_ = false;
        SaveSettings();
        DebugConsole::GetInstance()->AddLog("Orbit camera center gizmo saved.");
    }
#else
    (void)gameViewOffset;
    (void)gameViewSize;
    (void)snapEnabled;
    (void)snapValue;
#endif
}

void CameraEditor::DrawOrbitGuide(PrimitiveDrawer& primitiveDrawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit) {
#ifdef USE_IMGUI
    if (settings_.currentMode != Mode::Editor) return;
    if (!settings_.orbitGuideVisible) return;
    if (settings_.gameFollowMode != Camera::FollowMode::kOrbit) return;
    if (instanceCount >= maxDrawLimit) return;

    Math math;
    const Vector3 center = GetOrbitCenter();
    const float radius = settings_.orbitRadius;
    const float markerSize = settings_.orbitGuideMarkerSize;
    const float startAngle = DegToRad(settings_.orbitStartAngleDeg);

    auto drawSphere = [&](const Vector3& pos, float size, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;
        Matrix4x4 world = math.Multiply(math.MakeScaleMatrix({ size, size, size }), math.MakeTranslateMatrix(pos));
        primitiveDrawer.DrawWireSphere(commandList, world, color, instanceCount++);
        };

    auto drawCubeLine = [&](const Vector3& start, const Vector3& end, float thickness, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;
        primitiveDrawer.DrawWireCube(commandList, MakeLineBoxMatrix(start, end, thickness), color, instanceCount++);
        };

    if (radius > 0.01f && instanceCount < maxDrawLimit) {
        Matrix4x4 ringWorld = math.Multiply(
            math.MakeScaleMatrix({ radius * 2.0f, 0.04f, radius * 2.0f }),
            math.MakeTranslateMatrix(center));
        primitiveDrawer.DrawWireCylinder(commandList, ringWorld, { 0.1f, 0.8f, 1.0f, 0.85f }, instanceCount++);
    }

    Vector3 startGround = {
        center.x + radius * std::cos(startAngle),
        center.y,
        center.z + radius * std::sin(startAngle)
    };
    Vector3 startEye = startGround;
    startEye.y += settings_.orbitHeight;

    drawSphere(center, markerSize, { 1.0f, 0.9f, 0.1f, 1.0f });
    drawSphere(startEye, markerSize, { 1.0f, 0.15f, 0.15f, 1.0f });
    drawCubeLine(center, startGround, 0.04f, { 0.1f, 0.9f, 1.0f, 0.9f });
    drawCubeLine(startGround, startEye, 0.04f, { 0.3f, 1.0f, 0.2f, 0.9f });

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        drawSphere(camera->GetEye(), markerSize * 0.75f, { 0.75f, 0.35f, 1.0f, 1.0f });
    }
#else
    (void)primitiveDrawer;
    (void)commandList;
    (void)instanceCount;
    (void)maxDrawLimit;
#endif
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

void CameraEditor::SaveEditorState() {
    if (IsEditorStateSaveBlocked()) return;

    std::string filePath = kDirectoryPath_ + "editor_camera_state.json";
    json j;
    j["editorCameraPos"] = { settings_.editorCameraPos.x, settings_.editorCameraPos.y, settings_.editorCameraPos.z };
    j["editorCameraAngle"] = { settings_.editorCameraAngle.x, settings_.editorCameraAngle.y, settings_.editorCameraAngle.z };
    std::ofstream file(filePath);
    if (file.is_open()) {
        file << j.dump(4);
    }
}

void CameraEditor::LoadEditorState() {
    std::string filePath = kDirectoryPath_ + "editor_camera_state.json";
    std::ifstream file(filePath);
    if (!file.is_open()) return;
    try {
        json j;
        file >> j;
        if (j.contains("editorCameraPos") && j["editorCameraPos"].is_array()) {
            settings_.editorCameraPos.x = j["editorCameraPos"][0];
            settings_.editorCameraPos.y = j["editorCameraPos"][1];
            settings_.editorCameraPos.z = j["editorCameraPos"][2];
        }
        if (j.contains("editorCameraAngle") && j["editorCameraAngle"].is_array()) {
            settings_.editorCameraAngle.x = j["editorCameraAngle"][0];
            settings_.editorCameraAngle.y = j["editorCameraAngle"][1];
            settings_.editorCameraAngle.z = j["editorCameraAngle"][2];
        }
    }
    catch (...) {}
}
