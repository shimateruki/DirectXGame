#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.Internal.h"

using namespace CameraEditorImplementation;

// カメラ設定画面とプレビューパネル。
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

            engine::Vector3 center = GetOrbitCenter();
            if (ImGui::Button(ICON_FA_REDO " 現在位置を周回開始位置にセット")) {
                CaptureOrbitStartFromCurrentCamera(CameraManager::GetInstance()->GetMainCamera());
            }
            ImGui::TextDisabled("周回中心: %.2f, %.2f, %.2f", center.x, center.y, center.z);

            if (ImGui::Button("現在の角度を開始角度にする")) {
                Camera* cam = CameraManager::GetInstance()->GetMainCamera();
                if (cam) {
                    engine::Vector3 diff = cam->GetEye() - center;
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
            engine::Vector3 pos = camera->GetEye();
            engine::Vector3 rot = camera->GetRotation();
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
        else if (engine::Object3d* selectedCameraObject = GetSelectedCameraObject()) {
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
                    const engine::Vector3 eye = ResolveOverrideEye(param);
                    const engine::Vector3 target = ResolveOverrideTarget(param);
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
    // カメラの描画負荷を調整できるよう、更新頻度と解像度をプレビュー設定にまとめる。
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
        // RenderTargetサイズの変更後は古いプレビューを無効化する。
        PostEffect::GetInstance()->SetCameraPreviewResolutionScale(settings_.cameraPreviewResolutionScale);
        InvalidateCameraPreviews();
        SaveSettings();
    }
    const PostEffect* postEffect = PostEffect::GetInstance();
    ImGui::TextDisabled(
        "実描画: %d x %d / 非表示・別タブ・折り畳み中は更新停止",
        postEffect->GetCameraPreviewWidth(),
        postEffect->GetCameraPreviewHeight());

    engine::Object3d* selectedCameraObject = GetSelectedCameraObject();
    if (selectedCameraObject) {
        ImGui::TextColored(
            ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
            "Camera Object: %s",
            selectedCameraObject->GetName().c_str());
        engine::Vector3 selectedEye{};
        engine::Vector3 selectedTarget{};
        if (ResolveSceneCameraPose(selectedCameraObject, selectedEye, selectedTarget)) {
            ImGui::TextDisabled("Object Eye: %.2f, %.2f, %.2f", selectedEye.x, selectedEye.y, selectedEye.z);
            ImGui::TextDisabled("Object Target: %.2f, %.2f, %.2f", selectedTarget.x, selectedTarget.y, selectedTarget.z);
        }
    }
    else {
        ImGui::TextDisabled("HierarchyでCamera Objectを選択すると、専用プレビューを表示します。");
    }

    const engine::Vector3 eye = GetPreviewCameraEye();
    const engine::Vector3 forward = GetPreviewCameraForward();
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

    if (!settings_.cameraPreviewVisible || !EditorToolSettings::Get().IsVisible("cameraPreview")) {
        // 折り畳み・非表示中は実描画側も更新を止められるよう、Imageを登録しない。
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
        // 縦横比を崩さずInspectorの幅に収める。
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
