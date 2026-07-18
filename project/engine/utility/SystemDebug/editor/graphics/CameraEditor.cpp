#include "CameraEditor.h"
#include "CameraManager.h"
#include "InputManager.h" // 入力取得に必要
#include "PrimitiveDrawer.h"
#include "PostEffect.h"
#include "SRVManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <cmath>
#include <filesystem> 
#include <DebugConsole.h>
#include "IconsFontAwesome5.h"
using json = nlohmann::json;
namespace fs = std::filesystem; // 短縮用

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr const char* kCameraModelGizmoName = "Editor/camera_gizmo";
    constexpr bool kShowLegacyCameraEditor = false;

    // 再生中は通常の3人称カメラ表示だけを隠し、演出用カメラの確認を邪魔しないようにします。
    bool ShouldHideGameCameraGuideDuringPlay() {
        SceneManager* sceneManager = SceneManager::GetInstance();
        return sceneManager && sceneManager->IsPlaying();
    }

    BaseScene* GetCurrentScene() {
        SceneManager* sceneManager = SceneManager::GetInstance();
        return sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    }

    bool IsObjectInCurrentScene(const Object3d* target) {
        BaseScene* scene = GetCurrentScene();
        if (!scene || !target) {
            return false;
        }
        return std::any_of(scene->GetObjects().begin(), scene->GetObjects().end(), [target](const std::unique_ptr<Object3d>& object) {
            return object.get() == target;
        });
    }

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

    Vector3 CalculateEditorForward(const Vector3& rotation) {
        float x = std::sin(rotation.y) * std::cos(rotation.x);
        float y = -std::sin(rotation.x);
        float z = std::cos(rotation.y) * std::cos(rotation.x);
        return { x, y, z };
    }

    Vector3 Cross(const Vector3& a, const Vector3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    float Length(const Vector3& v) {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    Vector3 NormalizeSafe(const Vector3& v, const Vector3& fallback) {
        float length = Length(v);
        if (length < 0.0001f) {
            return fallback;
        }
        return { v.x / length, v.y / length, v.z / length };
    }

    Vector3 MakeRotationFromForward(const Vector3& forward) {
        Vector3 normalized = NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
        float pitch = std::asin(std::clamp(-normalized.y, -1.0f, 1.0f));
        float yaw = std::atan2(normalized.x, normalized.z);
        return { pitch, yaw, 0.0f };
    }

    Vector3 GetObjectWorldForward(const Object3d* object) {
        if (!object) {
            return { 0.0f, 0.0f, 1.0f };
        }
        Vector3 forward = Math::TransformNormal({ 0.0f, 0.0f, 1.0f }, object->GetWorldMatrix());
        return NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
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

void CameraEditor::SetObject3dCommon(Object3dCommon* common) {
    if (object3dCommon_ == common) {
        return;
    }

    object3dCommon_ = common;
    gameCameraModelGizmo_.reset();
}

void CameraEditor::EnsureCameraModelGizmo(std::unique_ptr<Object3d>& gizmo, const std::string& name) {
    if (!object3dCommon_) {
        return;
    }

    if (gizmo) {
        return;
    }

    gizmo = std::make_unique<Object3d>();
    gizmo->Initialize(object3dCommon_);
    gizmo->SetModel(kCameraModelGizmoName);
    gizmo->SetName(name);
    gizmo->SetClassName("Model");
    gizmo->SetMaterialType(0);
    gizmo->SetBlendMode(BlendMode::kNormal);
    gizmo->SetIsVisible(true);
}

void CameraEditor::ApplyCameraModelGizmo(Object3d* gizmo, const Vector3& eye, const Vector3& forward, float sizeScale, const Vector4& color) {
    if (!gizmo) {
        return;
    }

    const float baseSize = (std::max)(0.05f, settings_.cameraGuideSize);
    const Vector3 safeForward = NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
    const Vector3 rotation = MakeRotationFromForward(safeForward);
    const float modelScale = baseSize * sizeScale;

    // CameraEditorの可視化も、演出用カメラObjectと同じモデルを使って位置と向きを表示します。
    gizmo->SetTranslate(eye);
    gizmo->SetRotation(rotation);
    gizmo->SetScale({ modelScale, modelScale, modelScale });
    gizmo->SetColor(color);
    gizmo->SetEmissive(1.8f);
    gizmo->SetIsVisible(true);
    gizmo->UpdateLocalMatrix();
    gizmo->UpdateWorldMatrix();
}

void CameraEditor::DrawCameraModelGizmos(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (!settings_.cameraGuideVisible || !settings_.cameraBodyVisible || !object3dCommon_) {
        return;
    }

    if (!ShouldHideGameCameraGuideDuringPlay()) {
        const Vector3 gameEye = GetConfiguredCameraEye();
        const Vector3 gameForward = GetConfiguredCameraForward();
        EnsureCameraModelGizmo(gameCameraModelGizmo_, "CameraEditor_GameCameraGizmo");
        ApplyCameraModelGizmo(gameCameraModelGizmo_.get(), gameEye, gameForward, 0.85f, { 1.0f, 0.82f, 0.18f, 1.0f });
        if (gameCameraModelGizmo_) {
            gameCameraModelGizmo_->Draw(pointLightResource, spotLightResource);
        }
    } else if (gameCameraModelGizmo_) {
        gameCameraModelGizmo_->SetIsVisible(false);
    }

}

// フォルダ内の .json ファイルを探してリストを更新する
void CameraEditor::RefreshFileList() {
    fileList_.clear();

    // 保存先が存在しない場合は先に作成する。
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
                // 操作中はカメラの値をEditorへ逆反映し、設定値の上書きは行わない。
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
    DrawCameraPreviewPanel();
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

    // Camera Objectが演出カメラ設定の唯一の編集・保存単位です。
    if (ImGui::CollapsingHeader(ICON_FA_VIDEO " Camera Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        SceneManager* sceneManager = SceneManager::GetInstance();
        BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
        int cameraCount = 0;
        if (scene && ImGui::BeginListBox("##SceneCameraObjectList", ImVec2(-FLT_MIN, 120.0f))) {
            for (const auto& object : scene->GetObjects()) {
                if (!object || !object->IsCameraObject()) {
                    continue;
                }
                ++cameraCount;
                const bool selected = object.get() == selectedCameraObject_;
                const SceneCameraSettings& cameraSettings = object->GetSceneCameraSettings();
                const char* roleLabel = cameraSettings.role == SceneCameraRole::kMain ? "Main" : "Cinematic";
                const std::string label = object->GetName() + "  [" + roleLabel + "]";
                if (ImGui::Selectable(label.c_str(), selected)) {
                    SetSelectedCameraObject(object.get());
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndListBox();
        }

        if (cameraCount == 0) {
            ImGui::TextDisabled("Hierarchyの作成メニューからCamera Objectを追加してください。");
        }
        else if (Object3d* selectedCameraObject = GetSelectedCameraObject()) {
            const SceneCameraSettings& cameraSettings = selectedCameraObject->GetSceneCameraSettings();
            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "選択中: %s", selectedCameraObject->GetName().c_str());
            ImGui::TextDisabled("位置・注視・追従・EasingはCamera ObjectのInspectorで編集します。");
            ImGui::TextDisabled("Blend In %.2fs / Out %.2fs", cameraSettings.blendInDuration, cameraSettings.blendOutDuration);
            if (ImGui::Button(ICON_FA_PLAY " Camera Objectをテスト再生")) {
                PlaySceneObjectCamera(CameraManager::GetInstance()->GetMainCamera(), selectedCameraObject);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_STOP " 停止")) {
                StopSceneObjectCamera(CameraManager::GetInstance()->GetMainCamera());
            }
        }

        if (!overrideParamsMap_.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("旧形式の演出カメラ設定を%zu件読み込みました。新規編集には使用しません。", overrideParamsMap_.size());
        }
    }

    // 旧形式は既存JSONの読み込み互換だけを残し、編集UIには表示しません。
    if (kShowLegacyCameraEditor && ImGui::CollapsingHeader(ICON_FA_VIDEO " Legacy Cinematic Camera")) {

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
        ImGui::TextDisabled("Scene上の番号タグと同じ順番です。緑が選択中、青が未選択です。");
        std::string deleteOverrideName;
        if (ImGui::BeginListBox("##CameraList", ImVec2(-FLT_MIN, 100))) {
            int cameraIndex = 1;
            for (auto& [name, param] : overrideParamsMap_) {
                bool isSelected = (selectedOverrideName_ == name);
                ImGui::PushID(name.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? ImVec4(0.2f, 1.0f, 0.35f, 1.0f) : ImVec4(0.45f, 0.70f, 1.0f, 1.0f));
                const std::string listLabel = "#" + std::to_string(cameraIndex) + "  " + name + (isSelected ? "  [選択中]" : "");
                const float deleteButtonWidth = 28.0f;
                const float selectableWidth = (std::max)(80.0f, ImGui::GetContentRegionAvail().x - deleteButtonWidth - ImGui::GetStyle().ItemSpacing.x);
                if (ImGui::Selectable(listLabel.c_str(), isSelected, 0, ImVec2(selectableWidth, 0.0f))) {
                    selectedOverrideName_ = name;
                }
                const bool rowHovered = ImGui::IsItemHovered();
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                if (ImGui::SmallButton("X")) {
                    deleteOverrideName = name;
                }
                ImGui::PopStyleColor();

                if (rowHovered) {
                    const Vector3 eye = ResolveOverrideEye(param);
                    const Vector3 target = ResolveOverrideTarget(param);
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", name.c_str());
                    ImGui::Text("Eye: %.2f, %.2f, %.2f", eye.x, eye.y, eye.z);
                    ImGui::Text("Target: %.2f, %.2f, %.2f", target.x, target.y, target.z);
                    ImGui::Text("移行時間: %.2f 秒", param.duration);
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
                ++cameraIndex;
            }
            ImGui::EndListBox();
        }
        if (!deleteOverrideName.empty()) {
            overrideParamsMap_.erase(deleteOverrideName);
            if (selectedOverrideName_ == deleteOverrideName) {
                selectedOverrideName_.clear();
            }
            SaveSettings();
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
                    p.eyeSource = Camera::OverrideEyeSource::kFixed;
                    p.targetSource = Camera::OverrideTargetSource::kFixed;

                    // その場所・その角度に完全に固定するため、追従フラグはすべてOFFにする
                    p.trackEyeX = false; p.trackEyeY = false; p.trackEyeZ = false;
                    p.trackTargetX = false; p.trackTargetY = false; p.trackTargetZ = false;

                    changed = true; // セーブフラグを立てる
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::DragFloat("開始ブレンド時間 (秒)", &p.duration, 0.05f, 0.0f, 10.0f)) changed = true;
            if (ImGui::DragFloat("終了ブレンド時間 (秒)", &p.exitDuration, 0.05f, 0.0f, 10.0f)) changed = true;
            const char* easingNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out", "Smoother Step" };
            int easing = static_cast<int>(p.easing);
            if (ImGui::Combo("切替Easing", &easing, easingNames, IM_ARRAYSIZE(easingNames))) {
                p.easing = static_cast<Camera::OverrideEasing>(easing);
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ICON_FA_EYE " カメラ位置 (Eye)");
            const char* eyeSourceNames[] = { "固定座標", "Scene Camera Object" };
            int eyeSource = static_cast<int>(p.eyeSource);
            if (ImGui::Combo("Eye取得元", &eyeSource, eyeSourceNames, IM_ARRAYSIZE(eyeSourceNames))) {
                p.eyeSource = static_cast<Camera::OverrideEyeSource>(eyeSource);
                changed = true;
            }

            if (p.eyeSource == Camera::OverrideEyeSource::kFixed) {
                if (ImGui::Checkbox("X軸追従##Eye", &p.trackEyeX)) changed = true; ImGui::SameLine();
                if (ImGui::Checkbox("Y軸追従##Eye", &p.trackEyeY)) changed = true; ImGui::SameLine();
                if (ImGui::Checkbox("Z軸追従##Eye", &p.trackEyeZ)) changed = true;
                if (ImGui::DragFloat3("固定座標##Eye", &p.fixedEyePos.x, 0.1f)) changed = true;
            }
            else {
                const char* eyeObjectLabel = p.eyeObjectName.empty() ? "(未選択)" : p.eyeObjectName.c_str();
                if (ImGui::BeginCombo("Camera Object##Eye", eyeObjectLabel)) {
                    SceneManager* sceneManager = SceneManager::GetInstance();
                    if (sceneManager && sceneManager->GetCurrentScene()) {
                        for (const auto& object : sceneManager->GetCurrentScene()->GetObjects()) {
                            if (!object || !object->IsCameraObject()) continue;
                            const bool selected = p.eyeObjectName == object->GetName();
                            if (ImGui::Selectable(object->GetName().c_str(), selected)) {
                                p.eyeObjectName = object->GetName();
                                changed = true;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::DragFloat3("Objectオフセット##Eye", &p.eyeObjectOffset.x, 0.05f)) changed = true;
                const char* followModeNames[] = { "完全追従", "遅延追従" };
                int followMode = static_cast<int>(p.eyeFollowMode);
                if (ImGui::Combo("Eye追従", &followMode, followModeNames, IM_ARRAYSIZE(followModeNames))) {
                    p.eyeFollowMode = static_cast<Camera::OverrideFollowMode>(followMode);
                    changed = true;
                }
                if (p.eyeFollowMode == Camera::OverrideFollowMode::kSmooth &&
                    ImGui::DragFloat("Eye追従レスポンス", &p.eyeFollowResponse, 0.1f, 0.1f, 40.0f, "%.2f")) {
                    changed = true;
                }
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), ICON_FA_CROSSHAIRS " 注視点 (Target)");
            const char* targetSourceNames[] = { "固定座標", "Scene Objectを注視", "Camera Objectの前方" };
            int targetSource = static_cast<int>(p.targetSource);
            if (ImGui::Combo("Target取得元", &targetSource, targetSourceNames, IM_ARRAYSIZE(targetSourceNames))) {
                p.targetSource = static_cast<Camera::OverrideTargetSource>(targetSource);
                changed = true;
            }

            if (p.targetSource == Camera::OverrideTargetSource::kFixed) {
                if (ImGui::Checkbox("X軸追従##Tgt", &p.trackTargetX)) changed = true; ImGui::SameLine();
                if (ImGui::Checkbox("Y軸追従##Tgt", &p.trackTargetY)) changed = true; ImGui::SameLine();
                if (ImGui::Checkbox("Z軸追従##Tgt", &p.trackTargetZ)) changed = true;
                if (ImGui::DragFloat3("固定座標##Tgt", &p.fixedTargetPos.x, 0.1f)) changed = true;
            }
            else if (p.targetSource == Camera::OverrideTargetSource::kSceneObject) {
                const char* targetObjectLabel = p.targetObjectName.empty() ? "(未選択)" : p.targetObjectName.c_str();
                if (ImGui::BeginCombo("追従Object##Target", targetObjectLabel)) {
                    SceneManager* sceneManager = SceneManager::GetInstance();
                    if (sceneManager && sceneManager->GetCurrentScene()) {
                        for (const auto& object : sceneManager->GetCurrentScene()->GetObjects()) {
                            if (!object || object->IsEditorInternal() || object->GetName().empty()) continue;
                            const bool selected = p.targetObjectName == object->GetName();
                            if (ImGui::Selectable(object->GetName().c_str(), selected)) {
                                p.targetObjectName = object->GetName();
                                changed = true;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::DragFloat3("注視オフセット##Target", &p.targetObjectOffset.x, 0.05f)) changed = true;
            }
            else {
                if (ImGui::DragFloat("前方距離##Target", &p.eyeForwardDistance, 0.1f, 0.1f, 100.0f)) changed = true;
                if (ImGui::DragFloat3("前方注視オフセット##Target", &p.targetObjectOffset.x, 0.05f)) changed = true;
            }

            if (p.targetSource != Camera::OverrideTargetSource::kFixed) {
                const char* followModeNames[] = { "完全追従", "遅延追従" };
                int followMode = static_cast<int>(p.targetFollowMode);
                if (ImGui::Combo("Target追従", &followMode, followModeNames, IM_ARRAYSIZE(followModeNames))) {
                    p.targetFollowMode = static_cast<Camera::OverrideFollowMode>(followMode);
                    changed = true;
                }
                if (p.targetFollowMode == Camera::OverrideFollowMode::kSmooth &&
                    ImGui::DragFloat("Target追従レスポンス", &p.targetFollowResponse, 0.1f, 0.1f, 40.0f, "%.2f")) {
                    changed = true;
                }
            }

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
                if (camera) camera->EndOverride(p.exitDuration);
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

void CameraEditor::DrawCameraPreviewPanel() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(ICON_FA_CAMERA " カメラ可視化 / プレビュー", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Checkbox("編集時にカメラガイドを表示", &settings_.cameraGuideVisible);
    ImGui::SameLine();
    ImGui::Checkbox("カメラ本体モデルを表示", &settings_.cameraBodyVisible);
    ImGui::SameLine();
    if (ImGui::Checkbox("プレビュー枠を表示", &settings_.cameraPreviewVisible)) {
        InvalidateCameraPreviews();
    }
    ImGui::DragFloat("ガイド本体の大きさ", &settings_.cameraGuideSize, 0.01f, 0.1f, 3.0f, "%.2f");
    ImGui::DragFloat("視錐台の長さ", &settings_.cameraFrustumLength, 0.1f, 1.0f, 50.0f, "%.1f");
    ImGui::DragFloat("プレビュー高さ", &settings_.cameraPreviewHeight, 1.0f, 80.0f, 480.0f, "%.0f");

    constexpr int kPreviewFpsValues[] = { 10, 15, 30, 60 };
    const char* previewFpsLabels[] = { "10 FPS (軽量)", "15 FPS (推奨)", "30 FPS", "60 FPS" };
    int previewFpsIndex = 1;
    for (int i = 0; i < IM_ARRAYSIZE(kPreviewFpsValues); ++i) {
        if (settings_.cameraPreviewFps == kPreviewFpsValues[i]) {
            previewFpsIndex = i;
            break;
        }
    }
    if (ImGui::Combo("プレビュー更新頻度", &previewFpsIndex, previewFpsLabels, IM_ARRAYSIZE(previewFpsLabels))) {
        settings_.cameraPreviewFps = kPreviewFpsValues[previewFpsIndex];
        InvalidateCameraPreviews();
        SaveSettings();
    }

    constexpr float kPreviewResolutionScales[] = { 0.25f, 0.5f, 1.0f };
    const char* previewResolutionLabels[] = { "25% (軽量)", "50% (推奨)", "100% (高精細)" };
    int previewResolutionIndex = 1;
    float nearestScaleDistance = 10.0f;
    for (int i = 0; i < IM_ARRAYSIZE(kPreviewResolutionScales); ++i) {
        const float distance = std::abs(settings_.cameraPreviewResolutionScale - kPreviewResolutionScales[i]);
        if (distance < nearestScaleDistance) {
            nearestScaleDistance = distance;
            previewResolutionIndex = i;
        }
    }
    if (ImGui::Combo(
        "プレビュー解像度",
        &previewResolutionIndex,
        previewResolutionLabels,
        IM_ARRAYSIZE(previewResolutionLabels))) {
        settings_.cameraPreviewResolutionScale = kPreviewResolutionScales[previewResolutionIndex];
        PostEffect::GetInstance()->SetCameraPreviewResolutionScale(settings_.cameraPreviewResolutionScale);
        InvalidateCameraPreviews();
        SaveSettings();
    }
    const PostEffect* postEffect = PostEffect::GetInstance();
    ImGui::TextDisabled(
        "実描画: %d x %d / 非表示・別タブ・折り畳み中は更新停止",
        postEffect->GetCameraPreviewWidth(),
        postEffect->GetCameraPreviewHeight());

    Object3d* selectedCameraObject = GetSelectedCameraObject();
    if (selectedCameraObject) {
        ImGui::TextColored(
            ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
            "Camera Object: %s",
            selectedCameraObject->GetName().c_str());
        Vector3 selectedEye{};
        Vector3 selectedTarget{};
        if (ResolveSceneCameraPose(selectedCameraObject, selectedEye, selectedTarget)) {
            ImGui::TextDisabled("Object Eye: %.2f, %.2f, %.2f", selectedEye.x, selectedEye.y, selectedEye.z);
            ImGui::TextDisabled("Object Target: %.2f, %.2f, %.2f", selectedTarget.x, selectedTarget.y, selectedTarget.z);
        }
    }
    else {
        ImGui::TextDisabled("HierarchyでCamera Objectを選択すると、専用プレビューを表示します。");
    }

    const Vector3 eye = GetPreviewCameraEye();
    const Vector3 forward = GetPreviewCameraForward();
    ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "プレビュー対象: %s", GetPreviewCameraLabel());
    ImGui::TextDisabled("カメラ位置: %.2f, %.2f, %.2f", eye.x, eye.y, eye.z);
    ImGui::TextDisabled("視線方向: %.2f, %.2f, %.2f", forward.x, forward.y, forward.z);

    ImGui::Spacing();
    ImGui::TextDisabled("Scene表示の見方:");
    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.18f, 1.0f), "  黄色: ゲーム/3人称カメラ");
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.35f, 1.0f), "  緑色: 選択中のCamera Object");

    if (ImGui::Button(ICON_FA_SAVE " 可視化設定を保存")) {
        SaveSettings();
    }

    if (!settings_.cameraPreviewVisible) {
        return;
    }

    ImGui::Spacing();
    if (settings_.currentMode == Mode::Game) {
        ImGui::TextDisabled("下の画像は自由カメラ視点です。ゲームカメラ調整中に、外側から構図を確認できます。");
    } else {
        ImGui::TextDisabled("下の画像はゲームカメラ視点です。自由カメラ中に、3人称などの実際の見え方を確認できます。");
    }

    const float availableWidth = (std::max)(120.0f, ImGui::GetContentRegionAvail().x);
    const float aspect = 16.0f / 9.0f;
    float height = std::clamp(settings_.cameraPreviewHeight, 80.0f, 480.0f);
    float width = height * aspect;
    if (width > availableWidth) {
        width = availableWidth;
        height = width / aspect;
    }
    const float indent = (std::max)(0.0f, (availableWidth - width) * 0.5f);
    uint32_t textureHandle = PostEffect::GetInstance()->GetSRVHandle(PostEffect::kCameraPreviewTextureIndex);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(textureHandle);
    if (indent > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    }
    ImGui::Image((ImTextureID)gpuHandle.ptr, ImVec2(width, height));
    cameraPreviewPanelVisibleThisFrame_ = ImGui::IsItemVisible();
#endif
}

void CameraEditor::BeginPreviewUiFrame() {
    cameraPreviewPanelVisibleThisFrame_ = false;
    sceneCameraPreviewWindowVisibleThisFrame_ = false;
}

void CameraEditor::SetSceneCameraPreviewWindowVisible(bool visible) {
    sceneCameraPreviewWindowVisibleThisFrame_ = visible;
}

bool CameraEditor::HasSceneCameraPreviewTarget() const {
    Vector3 eye{};
    Vector3 target{};
    return GetSelectedCameraObject() != nullptr && ResolveCinematicPreviewPose(eye, target);
}

bool CameraEditor::ShouldRenderCameraPreview() const {
    return settings_.cameraPreviewVisible &&
        cameraPreviewPanelVisibleThisFrame_ &&
        IsPreviewUpdateDue(lastCameraPreviewRenderTime_, hasRenderedCameraPreview_);
}

bool CameraEditor::ShouldRenderSceneCameraPreview() const {
    return sceneCameraPreviewWindowVisibleThisFrame_ &&
        HasSceneCameraPreviewTarget() &&
        IsPreviewUpdateDue(lastSceneCameraPreviewRenderTime_, hasRenderedSceneCameraPreview_);
}

void CameraEditor::NotifyCameraPreviewRendered(bool sceneCameraPreview) {
    const auto now = std::chrono::steady_clock::now();
    if (sceneCameraPreview) {
        hasRenderedSceneCameraPreview_ = true;
        lastSceneCameraPreviewRenderTime_ = now;
    }
    else {
        hasRenderedCameraPreview_ = true;
        lastCameraPreviewRenderTime_ = now;
    }
}

bool CameraEditor::IsPreviewUpdateDue(
    const std::chrono::steady_clock::time_point& lastRenderTime,
    bool hasRenderedFrame) const {
    if (!hasRenderedFrame) {
        return true;
    }

    const int previewFps = std::clamp(settings_.cameraPreviewFps, 1, 60);
    const float updateInterval = 1.0f / static_cast<float>(previewFps);
    const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - lastRenderTime;
    return elapsed.count() >= updateInterval;
}

void CameraEditor::InvalidateCameraPreviews() {
    hasRenderedCameraPreview_ = false;
    hasRenderedSceneCameraPreview_ = false;
}

Camera* CameraEditor::PreparePreviewCamera(float aspectRatio) {
    const Vector3 eye = GetPreviewCameraEye();
    const Vector3 target = eye + GetPreviewCameraForward() * 10.0f;
    float fovY = 0.45f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    if (Camera* mainCamera = CameraManager::GetInstance()->GetMainCamera()) {
        fovY = mainCamera->GetFovY();
        nearClip = mainCamera->GetNearClip();
        farClip = mainCamera->GetFarClip();
    }
    return ConfigurePreviewCamera(eye, target, aspectRatio, fovY, nearClip, farClip);
}

Camera* CameraEditor::PrepareCinematicPreviewCamera(float aspectRatio) {
    Vector3 eye{};
    Vector3 target{};
    if (!ResolveCinematicPreviewPose(eye, target)) {
        return nullptr;
    }

    float fovY = 0.45f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    if (Object3d* selectedCamera = GetSelectedCameraObject()) {
        const SceneCameraSettings& settings = selectedCamera->GetSceneCameraSettings();
        fovY = settings.fovY;
        nearClip = settings.nearClip;
        farClip = settings.farClip;
    }
    else if (Camera* mainCamera = CameraManager::GetInstance()->GetMainCamera()) {
        fovY = mainCamera->GetFovY();
        nearClip = mainCamera->GetNearClip();
        farClip = mainCamera->GetFarClip();
    }

    return ConfigurePreviewCamera(eye, target, aspectRatio, fovY, nearClip, farClip);
}

Camera* CameraEditor::ConfigurePreviewCamera(
    const Vector3& eye,
    const Vector3& target,
    float aspectRatio,
    float fovY,
    float nearClip,
    float farClip) {
    if (!previewCameraInitialized_) {
        previewCamera_.Initialize();
        previewCameraInitialized_ = true;
    }

    const Vector3 forward = NormalizeSafe(target - eye, { 0.0f, 0.0f, 1.0f });
    const Vector3 rotation = MakeRotationFromForward(forward);
    previewCamera_.SetInputEnabled(false);
    previewCamera_.SetFollowTarget(nullptr);
    previewCamera_.SetFollowMode(Camera::FollowMode::kFixedPoint);
    previewCamera_.ConfigFixedPoint(eye, rotation);
    previewCamera_.SetEye(eye);
    previewCamera_.SetTarget(target);
    previewCamera_.SetRotation(rotation);
    previewCamera_.SetFovY(fovY);
    previewCamera_.SetClipRange(nearClip, farClip);
    previewCamera_.SetLookAtPreviewView(eye, target, aspectRatio);
    return &previewCamera_;
}

void CameraEditor::ApplyConfiguredCameraPreview(Camera* camera) const {
    if (!camera) {
        return;
    }

    const Vector3 eye = GetConfiguredCameraEye();
    const Vector3 forward = GetConfiguredCameraForward();
    const Vector3 rotation = MakeRotationFromForward(forward);

    camera->SetFollowTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->ConfigFixedPoint(eye, rotation);
    camera->SetEye(eye);
    camera->SetTarget(eye + forward * 10.0f);
    camera->SetRotation(rotation);
    camera->Update();
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
    j["cameraGuideVisible"] = settings_.cameraGuideVisible;
    j["cameraBodyVisible"] = settings_.cameraBodyVisible;
    j["cameraPreviewVisible"] = settings_.cameraPreviewVisible;
    j["cameraPreviewFps"] = settings_.cameraPreviewFps;
    j["cameraPreviewResolutionScale"] = settings_.cameraPreviewResolutionScale;
    j["cameraGuideSize"] = settings_.cameraGuideSize;
    j["cameraFrustumLength"] = settings_.cameraFrustumLength;
    j["cameraPreviewHeight"] = settings_.cameraPreviewHeight;
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
        p["exitDuration"] = param.exitDuration;
        p["easing"] = static_cast<int>(param.easing);
        p["eyeSource"] = static_cast<int>(param.eyeSource);
        p["eyeObjectName"] = param.eyeObjectName;
        p["eyeObjectOffset"] = { param.eyeObjectOffset.x, param.eyeObjectOffset.y, param.eyeObjectOffset.z };
        p["eyeFollowMode"] = static_cast<int>(param.eyeFollowMode);
        p["eyeFollowResponse"] = param.eyeFollowResponse;
        p["trackEyeX"] = param.trackEyeX;
        p["trackEyeY"] = param.trackEyeY;
        p["trackEyeZ"] = param.trackEyeZ;
        p["fixedEyePos"] = { param.fixedEyePos.x, param.fixedEyePos.y, param.fixedEyePos.z };

        p["targetSource"] = static_cast<int>(param.targetSource);
        p["targetObjectName"] = param.targetObjectName;
        p["targetObjectOffset"] = { param.targetObjectOffset.x, param.targetObjectOffset.y, param.targetObjectOffset.z };
        p["eyeForwardDistance"] = param.eyeForwardDistance;
        p["targetFollowMode"] = static_cast<int>(param.targetFollowMode);
        p["targetFollowResponse"] = param.targetFollowResponse;
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
        if (j.contains("cameraGuideVisible")) settings_.cameraGuideVisible = j["cameraGuideVisible"];
        if (j.contains("cameraBodyVisible")) settings_.cameraBodyVisible = j["cameraBodyVisible"];
        if (j.contains("cameraPreviewVisible")) settings_.cameraPreviewVisible = j["cameraPreviewVisible"];
        if (j.contains("cameraPreviewFps")) settings_.cameraPreviewFps = j["cameraPreviewFps"];
        if (j.contains("cameraPreviewResolutionScale")) {
            settings_.cameraPreviewResolutionScale = j["cameraPreviewResolutionScale"];
        }
        settings_.cameraPreviewFps = std::clamp(settings_.cameraPreviewFps, 1, 60);
        settings_.cameraPreviewResolutionScale = std::clamp(settings_.cameraPreviewResolutionScale, 0.25f, 1.0f);
        PostEffect::GetInstance()->SetCameraPreviewResolutionScale(settings_.cameraPreviewResolutionScale);
        InvalidateCameraPreviews();
        if (j.contains("cameraGuideSize")) settings_.cameraGuideSize = j["cameraGuideSize"];
        if (j.contains("cameraFrustumLength")) settings_.cameraFrustumLength = j["cameraFrustumLength"];
        if (j.contains("cameraPreviewHeight")) settings_.cameraPreviewHeight = j["cameraPreviewHeight"];

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
                p.exitDuration = val.value("exitDuration", 0.35f);
                p.easing = static_cast<Camera::OverrideEasing>(std::clamp(val.value("easing", 4), 0, 4));
                p.eyeSource = static_cast<Camera::OverrideEyeSource>(std::clamp(val.value("eyeSource", 0), 0, 1));
                p.eyeObjectName = val.value("eyeObjectName", "");
                if (val.contains("eyeObjectOffset") && val["eyeObjectOffset"].is_array() && val["eyeObjectOffset"].size() >= 3) {
                    p.eyeObjectOffset = { val["eyeObjectOffset"][0], val["eyeObjectOffset"][1], val["eyeObjectOffset"][2] };
                }
                p.eyeFollowMode = static_cast<Camera::OverrideFollowMode>(std::clamp(val.value("eyeFollowMode", 0), 0, 1));
                p.eyeFollowResponse = val.value("eyeFollowResponse", 12.0f);
                p.trackEyeX = val.value("trackEyeX", false);
                p.trackEyeY = val.value("trackEyeY", false);
                p.trackEyeZ = val.value("trackEyeZ", false);
                if (val.contains("fixedEyePos")) {
                    p.fixedEyePos = { val["fixedEyePos"][0], val["fixedEyePos"][1], val["fixedEyePos"][2] };
                }
                p.targetSource = static_cast<Camera::OverrideTargetSource>(std::clamp(val.value("targetSource", 0), 0, 2));
                p.targetObjectName = val.value("targetObjectName", "");
                if (val.contains("targetObjectOffset") && val["targetObjectOffset"].is_array() && val["targetObjectOffset"].size() >= 3) {
                    p.targetObjectOffset = { val["targetObjectOffset"][0], val["targetObjectOffset"][1], val["targetObjectOffset"][2] };
                }
                p.eyeForwardDistance = val.value("eyeForwardDistance", 10.0f);
                p.targetFollowMode = static_cast<Camera::OverrideFollowMode>(std::clamp(val.value("targetFollowMode", 0), 0, 1));
                p.targetFollowResponse = val.value("targetFollowResponse", 14.0f);
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

bool CameraEditor::FocusSelectedCameraObject() {
    Object3d* cameraObject = GetSelectedCameraObject();
    Vector3 eye{};
    Vector3 target{};
    if (!cameraObject || !ResolveSceneCameraPose(cameraObject, eye, target)) {
        return false;
    }

    const Vector3 forward = NormalizeSafe(target - eye, { 0.0f, 0.0f, 1.0f });
    const Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
    const float focusDistance = (std::max)(4.0f, settings_.cameraGuideSize * 8.0f);
    const float focusHeight = (std::max)(2.0f, settings_.cameraGuideSize * 4.0f);
    const Vector3 focusTarget = eye + worldUp * (settings_.cameraGuideSize * 0.6f);
    const Vector3 focusEye = eye - forward * focusDistance + worldUp * focusHeight;
    const Vector3 focusForward = NormalizeSafe(focusTarget - focusEye, forward);
    const Vector3 rotation = MakeRotationFromForward(focusForward);

    // Camera Object本体を確認できるように、実カメラ位置より少し後ろ上へ寄せる。
    settings_.editorCameraPos = focusEye;
    settings_.editorCameraAngle = rotation;
    SetMode(Mode::Editor);
    SetEditorCameraTransform(focusEye, rotation);

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->SetTarget(focusTarget);
        camera->Update();
    }

    DebugConsole::GetInstance()->AddLog("Focused Camera Object: " + cameraObject->GetName());
    return true;
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

Vector3 CameraEditor::GetConfiguredCameraEye() const {
    if (settings_.gameFollowMode == Camera::FollowMode::kFixedPoint) {
        return settings_.fixedPointPos;
    }

    if (settings_.gameFollowMode == Camera::FollowMode::kOrbit) {
        return GetOrbitStartEye();
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    const Vector3 fallback = camera ? camera->GetEye() : settings_.fixedPointPos;
    if (!targetPlayer_) {
        return fallback;
    }

    Vector3 target = targetPlayer_->GetWorldPosition();
    target.y += settings_.height;
    Vector3 forward = GetConfiguredCameraForward();
    return target - forward * settings_.distance;
}

Vector3 CameraEditor::GetConfiguredCameraForward() const {
    if (settings_.gameFollowMode == Camera::FollowMode::kFixedPoint) {
        return NormalizeSafe(CalculateEditorForward(settings_.fixedPointAngle), { 0.0f, 0.0f, 1.0f });
    }

    if (settings_.gameFollowMode == Camera::FollowMode::kOrbit) {
        return NormalizeSafe(GetOrbitCenter() - GetOrbitStartEye(), { 0.0f, 0.0f, 1.0f });
    }

    Vector3 rotation = {
        DegToRad(settings_.angle.x),
        DegToRad(settings_.angle.y),
        DegToRad(settings_.angle.z)
    };
    return NormalizeSafe(CalculateEditorForward(rotation), { 0.0f, 0.0f, 1.0f });
}

Vector3 CameraEditor::GetPreviewCameraEye() const {
    if (settings_.currentMode == Mode::Game) {
        return settings_.editorCameraPos;
    }
    return GetConfiguredCameraEye();
}

Vector3 CameraEditor::GetPreviewCameraForward() const {
    if (settings_.currentMode == Mode::Game) {
        return NormalizeSafe(CalculateEditorForward(settings_.editorCameraAngle), { 0.0f, 0.0f, 1.0f });
    }
    return GetConfiguredCameraForward();
}

const char* CameraEditor::GetPreviewCameraLabel() const {
    return settings_.currentMode == Mode::Game ? "自由カメラ" : "ゲームカメラ";
}

Object3d* CameraEditor::FindSceneObjectByName(const std::string& name) const {
    if (name.empty()) {
        return nullptr;
    }

    BaseScene* scene = GetCurrentScene();
    if (!scene) {
        return nullptr;
    }

    for (const auto& object : scene->GetObjects()) {
        if (object && object->GetName() == name) {
            return object.get();
        }
    }
    return nullptr;
}

void CameraEditor::SetSelectedCameraObject(Object3d* cameraObject) {
    Object3d* newSelection = cameraObject &&
        cameraObject->IsCameraObject() &&
        IsObjectInCurrentScene(cameraObject)
        ? cameraObject
        : nullptr;
    if (selectedCameraObject_ != newSelection) {
        selectedCameraObject_ = newSelection;
        hasRenderedSceneCameraPreview_ = false;
    }
}

Object3d* CameraEditor::GetSelectedCameraObject() const {
    if (!selectedCameraObject_ || !IsObjectInCurrentScene(selectedCameraObject_)) {
        return nullptr;
    }
    return selectedCameraObject_->IsCameraObject() ? selectedCameraObject_ : nullptr;
}

bool CameraEditor::ResolveSceneCameraPose(const Object3d* cameraObject, Vector3& eye, Vector3& target) const {
    if (!cameraObject || !cameraObject->IsCameraObject()) {
        return false;
    }

    const SceneCameraSettings& settings = cameraObject->GetSceneCameraSettings();
    const Object3d* eyeObject = cameraObject;
    if (settings.eyeSource == SceneCameraEyeSource::kSceneObject) {
        if (Object3d* resolvedEyeObject = FindSceneObjectByName(settings.eyeObjectName)) {
            eyeObject = resolvedEyeObject;
        }
    }

    eye = eyeObject->GetWorldPosition() + settings.eyeOffset;
    switch (settings.targetMode) {
    case SceneCameraTargetMode::kFixedPoint:
        target = settings.fixedTarget;
        break;
    case SceneCameraTargetMode::kSceneObject:
        if (Object3d* targetObject = FindSceneObjectByName(settings.targetObjectName)) {
            target = targetObject->GetWorldPosition() + settings.targetOffset;
        }
        else {
            target = eye + GetObjectWorldForward(eyeObject) * settings.forwardDistance + settings.targetOffset;
        }
        break;
    case SceneCameraTargetMode::kCameraForward:
    default:
        target = eye + GetObjectWorldForward(eyeObject) * settings.forwardDistance + settings.targetOffset;
        break;
    }
    return true;
}

Camera::CameraOverrideParams CameraEditor::MakeRuntimeOverrideParams(const Object3d* cameraObject) const {
    Camera::CameraOverrideParams runtime;
    if (!cameraObject) {
        return runtime;
    }

    const SceneCameraSettings& settings = cameraObject->GetSceneCameraSettings();
    runtime.duration = settings.blendInDuration;
    runtime.exitDuration = settings.blendOutDuration;
    runtime.easing = static_cast<Camera::OverrideEasing>(settings.easing);
    runtime.eyeSource = Camera::OverrideEyeSource::kSceneObject;
    runtime.eyeObject = const_cast<Object3d*>(cameraObject);
    runtime.eyeObjectName = cameraObject->GetName();
    if (settings.eyeSource == SceneCameraEyeSource::kSceneObject) {
        runtime.eyeObjectName = settings.eyeObjectName;
        if (Object3d* eyeObject = FindSceneObjectByName(settings.eyeObjectName)) {
            runtime.eyeObject = eyeObject;
        }
    }
    runtime.eyeObjectOffset = settings.eyeOffset;
    runtime.eyeFollowMode = static_cast<Camera::OverrideFollowMode>(settings.eyeFollowMode);
    runtime.eyeFollowResponse = settings.eyeFollowResponse;

    switch (settings.targetMode) {
    case SceneCameraTargetMode::kFixedPoint:
        runtime.targetSource = Camera::OverrideTargetSource::kFixed;
        runtime.fixedTargetPos = settings.fixedTarget;
        runtime.trackTargetX = false;
        runtime.trackTargetY = false;
        runtime.trackTargetZ = false;
        break;
    case SceneCameraTargetMode::kSceneObject:
        runtime.targetSource = Camera::OverrideTargetSource::kSceneObject;
        runtime.targetObjectName = settings.targetObjectName;
        runtime.targetFollowObject = FindSceneObjectByName(settings.targetObjectName);
        runtime.targetObjectOffset = settings.targetOffset;
        break;
    case SceneCameraTargetMode::kCameraForward:
    default:
        runtime.targetSource = Camera::OverrideTargetSource::kEyeObjectForward;
        runtime.eyeForwardDistance = settings.forwardDistance;
        runtime.targetObjectOffset = settings.targetOffset;
        break;
    }
    runtime.targetFollowMode = static_cast<Camera::OverrideFollowMode>(settings.targetFollowMode);
    runtime.targetFollowResponse = settings.targetFollowResponse;
    return runtime;
}

Camera::CameraOverrideParams CameraEditor::MakeRuntimeOverrideParams(
    const Camera::CameraOverrideParams& params,
    Object3d* fallbackEyeObject) const {
    Camera::CameraOverrideParams runtime = params;
    runtime.eyeObject = nullptr;
    runtime.targetFollowObject = nullptr;

    if (runtime.eyeSource == Camera::OverrideEyeSource::kSceneObject) {
        runtime.eyeObject = FindSceneObjectByName(runtime.eyeObjectName);
        if (!runtime.eyeObject) {
            runtime.eyeObject = fallbackEyeObject;
        }
    }
    if (runtime.targetSource == Camera::OverrideTargetSource::kSceneObject) {
        runtime.targetFollowObject = FindSceneObjectByName(runtime.targetObjectName);
    }
    return runtime;
}

Vector3 CameraEditor::ResolveOverrideEye(const Camera::CameraOverrideParams& params) const {
    const Camera::CameraOverrideParams runtime = MakeRuntimeOverrideParams(params);
    if (runtime.eyeSource == Camera::OverrideEyeSource::kSceneObject && runtime.eyeObject) {
        return runtime.eyeObject->GetWorldPosition() + runtime.eyeObjectOffset;
    }
    return params.fixedEyePos;
}

Vector3 CameraEditor::ResolveOverrideTarget(const Camera::CameraOverrideParams& params) const {
    const Camera::CameraOverrideParams runtime = MakeRuntimeOverrideParams(params);
    if (runtime.targetSource == Camera::OverrideTargetSource::kSceneObject && runtime.targetFollowObject) {
        return runtime.targetFollowObject->GetWorldPosition() + runtime.targetObjectOffset;
    }
    if (runtime.targetSource == Camera::OverrideTargetSource::kEyeObjectForward && runtime.eyeObject) {
        const Vector3 eye = runtime.eyeObject->GetWorldPosition() + runtime.eyeObjectOffset;
        return eye
            + GetObjectWorldForward(runtime.eyeObject) * (std::max)(runtime.eyeForwardDistance, 0.01f)
            + runtime.targetObjectOffset;
    }
    return params.fixedTargetPos;
}

bool CameraEditor::ResolveCinematicPreviewPose(Vector3& eye, Vector3& target) const {
    Object3d* selectedCamera = GetSelectedCameraObject();
    return selectedCamera && ResolveSceneCameraPose(selectedCamera, eye, target);
}

Vector3 CameraEditor::GetConfiguredCameraRight(const Vector3& forward) const {
    const Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
    Vector3 right = Cross(worldUp, forward);
    if (Length(right) < 0.0001f) {
        right = { 1.0f, 0.0f, 0.0f };
    }
    return NormalizeSafe(right, { 1.0f, 0.0f, 0.0f });
}

Vector3 CameraEditor::GetConfiguredCameraUp(const Vector3& forward, const Vector3& right) const {
    return NormalizeSafe(Cross(forward, right), { 0.0f, 1.0f, 0.0f });
}

Matrix4x4 CameraEditor::MakeLineBoxMatrix(const Vector3& start, const Vector3& end, float thickness) const {
    Math math;
    Vector3 diff = end - start;
    float length = Length(diff);
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

void CameraEditor::DrawCameraGuide(PrimitiveDrawer& primitiveDrawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit) {
#ifdef USE_IMGUI
    if (!settings_.cameraGuideVisible) return;
    if (instanceCount >= maxDrawLimit) return;

    Math math;
    const float markerSize = (std::max)(0.05f, settings_.cameraGuideSize);
    const float frustumLength = (std::max)(0.5f, settings_.cameraFrustumLength);

    auto drawSphere = [&](const Vector3& pos, float size, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;
        Matrix4x4 world = math.Multiply(math.MakeScaleMatrix({ size, size, size }), math.MakeTranslateMatrix(pos));
        primitiveDrawer.DrawWireSphere(commandList, world, color, instanceCount++);
    };

    auto drawCubeLine = [&](const Vector3& start, const Vector3& end, float thickness, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;
        primitiveDrawer.DrawWireCube(commandList, MakeLineBoxMatrix(start, end, thickness), color, instanceCount++);
    };

    auto drawWireBox = [&](const Vector3& center, const Vector3& right, const Vector3& up, const Vector3& forward,
        float halfRight, float halfUp, float halfForward, float thickness, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;

        auto corner = [&](float sx, float sy, float sz) {
            return center + right * (halfRight * sx) + up * (halfUp * sy) + forward * (halfForward * sz);
        };

        const Vector3 lbf = corner(-1.0f, -1.0f, 1.0f);
        const Vector3 rbf = corner(1.0f, -1.0f, 1.0f);
        const Vector3 ltf = corner(-1.0f, 1.0f, 1.0f);
        const Vector3 rtf = corner(1.0f, 1.0f, 1.0f);
        const Vector3 lbb = corner(-1.0f, -1.0f, -1.0f);
        const Vector3 rbb = corner(1.0f, -1.0f, -1.0f);
        const Vector3 ltb = corner(-1.0f, 1.0f, -1.0f);
        const Vector3 rtb = corner(1.0f, 1.0f, -1.0f);

        drawCubeLine(lbf, rbf, thickness, color);
        drawCubeLine(rbf, rtf, thickness, color);
        drawCubeLine(rtf, ltf, thickness, color);
        drawCubeLine(ltf, lbf, thickness, color);

        drawCubeLine(lbb, rbb, thickness, color);
        drawCubeLine(rbb, rtb, thickness, color);
        drawCubeLine(rtb, ltb, thickness, color);
        drawCubeLine(ltb, lbb, thickness, color);

        drawCubeLine(lbf, lbb, thickness, color);
        drawCubeLine(rbf, rbb, thickness, color);
        drawCubeLine(ltf, ltb, thickness, color);
        drawCubeLine(rtf, rtb, thickness, color);
    };

    auto drawCameraBody = [&](const Vector3& eye, const Vector3& forward, float sizeScale, const Vector4& bodyColor, const Vector4& accentColor) {
        if (!settings_.cameraBodyVisible || instanceCount >= maxDrawLimit) return;
        if (object3dCommon_) return;

        const Vector3 safeForward = NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
        const Vector3 right = GetConfiguredCameraRight(safeForward);
        const Vector3 up = GetConfiguredCameraUp(safeForward, right);
        const float size = markerSize * sizeScale;
        const float line = (std::max)(0.018f, size * 0.045f);

        // UnityのScene Viewに近い「カメラ筐体 + レンズ + 取っ手」を、Editor専用ワイヤーモデルとして生成します。
        const Vector3 bodyCenter = eye - safeForward * (size * 0.16f);
        drawWireBox(bodyCenter, right, up, safeForward, size * 0.82f, size * 0.48f, size * 0.42f, line, bodyColor);

        const Vector3 lensCenter = eye + safeForward * (size * 0.45f);
        drawWireBox(lensCenter, right, up, safeForward, size * 0.36f, size * 0.28f, size * 0.24f, line * 0.9f, accentColor);

        const Vector3 hoodCenter = eye + safeForward * (size * 0.78f);
        drawWireBox(hoodCenter, right, up, safeForward, size * 0.52f, size * 0.38f, size * 0.11f, line * 0.8f, accentColor);

        const Vector3 handleCenter = bodyCenter + up * (size * 0.66f) - safeForward * (size * 0.05f);
        drawWireBox(handleCenter, right, up, safeForward, size * 0.42f, size * 0.13f, size * 0.22f, line * 0.75f, bodyColor);

        const Vector3 tripodRoot = bodyCenter - up * (size * 0.52f);
        const Vector3 tripodCenter = tripodRoot - up * (size * 0.55f);
        drawCubeLine(tripodRoot, tripodCenter, line * 0.75f, bodyColor);
        drawCubeLine(tripodCenter, tripodCenter + right * (size * 0.58f) - up * (size * 0.22f), line * 0.65f, bodyColor);
        drawCubeLine(tripodCenter, tripodCenter - right * (size * 0.58f) - up * (size * 0.22f), line * 0.65f, bodyColor);
        drawCubeLine(tripodCenter, tripodCenter - safeForward * (size * 0.50f) - up * (size * 0.18f), line * 0.65f, bodyColor);
    };

    auto drawCameraBadge = [&](const Vector3& eye, const Vector3& forward, int badgeNumber, float sizeScale, const Vector4& color, bool selected) {
        if (instanceCount >= maxDrawLimit) return;

        const Vector3 safeForward = NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
        const Vector3 right = GetConfiguredCameraRight(safeForward);
        const Vector3 up = GetConfiguredCameraUp(safeForward, right);
        const float size = markerSize * sizeScale;
        const float line = (std::max)(0.014f, size * 0.035f);
        const Vector3 tagCenter = eye + up * (selected ? size * 1.45f : size * 1.12f) - safeForward * (size * 0.28f);

        // リスト上の番号とScene上のカメラ位置を対応させるため、小さなタグを描きます。
        drawWireBox(tagCenter, right, up, safeForward, size * 0.44f, size * 0.20f, size * 0.06f, line, color);

        if (badgeNumber <= 0) {
            drawCubeLine(tagCenter - right * (size * 0.22f), tagCenter + right * (size * 0.22f), line * 1.2f, color);
            drawCubeLine(tagCenter, tagCenter + up * (size * 0.28f), line * 0.9f, color);
            return;
        }

        const int dotCount = (std::min)(badgeNumber, 5);
        const float start = -0.24f;
        const float step = dotCount > 1 ? 0.12f : 0.0f;
        for (int i = 0; i < dotCount; ++i) {
            const Vector3 dotPos = tagCenter + right * (size * (start + step * i));
            drawSphere(dotPos, size * (selected ? 0.075f : 0.060f), color);
        }
        if (badgeNumber > 5) {
            drawCubeLine(tagCenter + right * (size * 0.28f) - up * (size * 0.08f), tagCenter + right * (size * 0.34f) + up * (size * 0.08f), line * 0.75f, color);
        }
    };

    auto drawFrustum = [&](const Vector3& eye, const Vector3& forward, float fovY, float sizeScale, const Vector4& eyeColor, const Vector4& rayColor, const Vector4& frameColor) {
        if (instanceCount >= maxDrawLimit) return;

        const Vector3 safeForward = NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
        const Vector3 right = GetConfiguredCameraRight(safeForward);
        const Vector3 up = GetConfiguredCameraUp(safeForward, right);
        const Vector3 target = eye + safeForward * frustumLength;
        const float safeFovY = std::clamp(fovY, DegToRad(1.0f), DegToRad(179.0f));
        const float halfHeight = frustumLength * std::tan(safeFovY * 0.5f);
        const float halfWidth = halfHeight * (16.0f / 9.0f);
        const float localMarkerSize = markerSize * sizeScale;
        const Vector3 topLeft = target + up * halfHeight - right * halfWidth;
        const Vector3 topRight = target + up * halfHeight + right * halfWidth;
        const Vector3 bottomLeft = target - up * halfHeight - right * halfWidth;
        const Vector3 bottomRight = target - up * halfHeight + right * halfWidth;

        drawCameraBody(eye, safeForward, sizeScale, eyeColor, frameColor);
        drawSphere(eye, settings_.cameraBodyVisible ? localMarkerSize * 0.24f : localMarkerSize, eyeColor);
        drawSphere(target, localMarkerSize * 0.45f, frameColor);
        drawCubeLine(eye, target, 0.035f * sizeScale, rayColor);
        drawCubeLine(eye, topLeft, 0.025f * sizeScale, frameColor);
        drawCubeLine(eye, topRight, 0.025f * sizeScale, frameColor);
        drawCubeLine(eye, bottomLeft, 0.025f * sizeScale, frameColor);
        drawCubeLine(eye, bottomRight, 0.025f * sizeScale, frameColor);
        drawCubeLine(topLeft, topRight, 0.03f * sizeScale, frameColor);
        drawCubeLine(topRight, bottomRight, 0.03f * sizeScale, frameColor);
        drawCubeLine(bottomRight, bottomLeft, 0.03f * sizeScale, frameColor);
        drawCubeLine(bottomLeft, topLeft, 0.03f * sizeScale, frameColor);
    };

    if (!ShouldHideGameCameraGuideDuringPlay()) {
        // ゲーム/3人称カメラの位置をScene上に表示します。
        const Vector3 gameEye = GetConfiguredCameraEye();
        const Vector3 gameForward = GetConfiguredCameraForward();
        const Vector4 gameEyeColor = { 1.0f, 0.82f, 0.18f, 1.0f };
        const Vector4 gameRayColor = { 1.0f, 0.72f, 0.18f, 0.95f };
        const Vector4 gameFrameColor = { 1.0f, 0.92f, 0.35f, 0.88f };
        drawFrustum(gameEye, gameForward, 0.45f, 0.85f, gameEyeColor, gameRayColor, gameFrameColor);
        drawCameraBadge(gameEye, gameForward, 0, 0.85f, gameFrameColor, true);
        if (targetPlayer_) {
            Vector3 lookTarget = targetPlayer_->GetWorldPosition();
            lookTarget.y += settings_.height;
            drawSphere(lookTarget, markerSize * 0.38f, gameFrameColor);
            drawCubeLine(gameEye, lookTarget, 0.024f, gameRayColor);
        }
    }

    // Camera Object選択中は、そのObject自身のFOV・位置・注視先でCamera Editorと同じガイドを描きます。
    if (Object3d* cameraObject = GetSelectedCameraObject()) {
        Vector3 eye{};
        Vector3 target{};
        if (ResolveSceneCameraPose(cameraObject, eye, target)) {
            const SceneCameraSettings& cameraSettings = cameraObject->GetSceneCameraSettings();
            const Vector3 forward = NormalizeSafe(target - eye, GetObjectWorldForward(cameraObject));
            const Vector4 eyeColor = { 0.2f, 1.0f, 0.35f, 1.0f };
            const Vector4 rayColor = { 0.2f, 1.0f, 0.35f, 0.95f };
            const Vector4 frameColor = { 0.75f, 1.0f, 0.35f, 0.95f };

            drawFrustum(eye, forward, cameraSettings.fovY, 0.95f, eyeColor, rayColor, frameColor);
            drawCameraBadge(eye, forward, 1, 0.95f, frameColor, true);
            drawSphere(target, markerSize * 0.32f, frameColor);
            drawCubeLine(eye, target, 0.032f, rayColor);
        }
    }

#else
    (void)primitiveDrawer;
    (void)commandList;
    (void)instanceCount;
    (void)maxDrawLimit;
#endif
}

void CameraEditor::DrawOrbitGuide(PrimitiveDrawer& primitiveDrawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit) {
#ifdef USE_IMGUI
    DrawCameraGuide(primitiveDrawer, commandList, instanceCount, maxDrawLimit);
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
        // Object参照はシーン切替で無効になるため、再生開始時に名前から解決します。
        camera->StartOverride(MakeRuntimeOverrideParams(it->second));
        return true;
    }

    // 見つからなかった場合はエラーを出す
    DebugConsole::GetInstance()->AddLog("Error: Camera Override '" + cameraName + "' not found!");
    return false;
}

bool CameraEditor::PlaySceneObjectCamera(Camera* camera, Object3d* cameraObject) {
    if (!cameraObject) {
        return false;
    }
    const SceneCameraSettings& settings = cameraObject->GetSceneCameraSettings();
    return PlaySceneObjectCamera(
        camera,
        cameraObject,
        settings.blendInDuration,
        settings.blendOutDuration,
        static_cast<int>(settings.easing));
}

bool CameraEditor::PlaySceneObjectCamera(
    Camera* camera,
    Object3d* cameraObject,
    float blendInDuration,
    float blendOutDuration,
    int easing) {
    if (!camera || !cameraObject || !cameraObject->IsCameraObject()) {
        return false;
    }

    const SceneCameraSettings& settings = cameraObject->GetSceneCameraSettings();
    if (!settings.enabled) {
        return false;
    }
    Camera::CameraOverrideParams runtime = MakeRuntimeOverrideParams(cameraObject);
    runtime.duration = std::max(0.0f, blendInDuration);
    runtime.exitDuration = std::max(0.0f, blendOutDuration);
    runtime.easing = static_cast<Camera::OverrideEasing>(std::clamp(easing, 0, 4));

    SetSelectedCameraObject(cameraObject);
    activeSceneCameraExitDuration_ = runtime.exitDuration;
    if (!hasSceneCameraProjectionBackup_) {
        sceneCameraBackupFovY_ = camera->GetFovY();
        sceneCameraBackupNearClip_ = camera->GetNearClip();
        sceneCameraBackupFarClip_ = camera->GetFarClip();
        hasSceneCameraProjectionBackup_ = true;
    }
    camera->SetFovY(settings.fovY);
    camera->SetClipRange(settings.nearClip, settings.farClip);
    camera->UpdateProjectionMatrix();
    camera->StartOverride(runtime);
    return true;
}

void CameraEditor::StopSceneObjectCamera(Camera* camera) {
    if (!camera) {
        return;
    }
    camera->EndOverride(activeSceneCameraExitDuration_);
    if (hasSceneCameraProjectionBackup_) {
        camera->SetFovY(sceneCameraBackupFovY_);
        camera->SetClipRange(sceneCameraBackupNearClip_, sceneCameraBackupFarClip_);
        camera->UpdateProjectionMatrix();
        hasSceneCameraProjectionBackup_ = false;
    }
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
