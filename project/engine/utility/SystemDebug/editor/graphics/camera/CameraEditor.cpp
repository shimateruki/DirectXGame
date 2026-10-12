#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.Internal.h"

using namespace CameraEditorImplementation;

CameraEditor* CameraEditor::GetInstance() {
    // エディターのカメラ設定とプレビュー状態を一箇所で共有する。
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
    // 描画基盤が切り替わったら、旧基盤で作ったカメラ模型を作り直す。
    if (object3dCommon_ == common) {
        return;
    }

    object3dCommon_ = common;
    gameCameraModelGizmo_.reset();
}

void CameraEditor::EnsureCameraModelGizmo(std::unique_ptr<Object3d>& gizmo, const std::string& name) {
    // ガイド用模型は必要になったときだけ生成し、同じインスタンスを使い回す。
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
    // カメラ編集対象がないときは模型を隠し、ゲーム画面にガイドを残さない。
#ifdef USE_IMGUI
    if (!EditorToolSettings::Get().IsVisible("cameraBody")) return;
#endif
    if (!settings_.cameraGuideVisible || !settings_.cameraBodyVisible || !object3dCommon_) {
        return;
    }

    const bool isCameraEditorSelected =
        EditorManager::GetInstance()->GetSelectedObject() == this;
    if (!isCameraEditorSelected && GetSelectedCameraObject() == nullptr) {
        if (gameCameraModelGizmo_) {
            gameCameraModelGizmo_->SetIsVisible(false);
        }
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

void CameraEditor::Update(Object3d* player, bool isLockingOn) {
    // ロックオン中のカメラはゲーム側が制御するので、エディター設定で上書きしない。
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
        const bool stateChanged =
            eye.x != settings_.editorCameraPos.x ||
            eye.y != settings_.editorCameraPos.y ||
            eye.z != settings_.editorCameraPos.z ||
            rotation.x != settings_.editorCameraAngle.x ||
            rotation.y != settings_.editorCameraAngle.y ||
            rotation.z != settings_.editorCameraAngle.z;
        settings_.editorCameraPos = eye;
        settings_.editorCameraAngle = rotation;
        if (stateChanged) {
            editorStateDirty_ = true;
            editorStateSaveReadyAt_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        }
        if (editorStateDirty_ && std::chrono::steady_clock::now() >= editorStateSaveReadyAt_) {
            SaveEditorState();
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

void CameraEditor::BeginPreviewUiFrame() {
    // 各ウィンドウがこのフレームで開いたかを再判定し、隠れたプレビューの描画を止める。
    cameraPreviewPanelVisibleThisFrame_ = false;
    sceneCameraPreviewWindowVisibleThisFrame_ = false;
}

void CameraEditor::SetSceneCameraPreviewWindowVisible(bool visible) {
    // シーンカメラのプレビュー描画を UI の可視状態に連動させる。
    sceneCameraPreviewWindowVisibleThisFrame_ = visible;
}

bool CameraEditor::HasSceneCameraPreviewTarget() const {
    // 選択中の Camera Object が現シーンに存在し、姿勢を解決できる場合だけ描画する。
    Vector3 eye{};
    Vector3 target{};
    return GetSelectedCameraObject() != nullptr && ResolveCinematicPreviewPose(eye, target);
}

bool CameraEditor::ShouldRenderCameraPreview() const {
    // 表示中でも設定 FPS に達していなければオフスクリーン描画を省く。
#ifdef USE_IMGUI
    if (!EditorToolSettings::Get().IsVisible("cameraPreview")) return false;
#endif
    return settings_.cameraPreviewVisible &&
        cameraPreviewPanelVisibleThisFrame_ &&
        IsPreviewUpdateDue(lastCameraPreviewRenderTime_, hasRenderedCameraPreview_);
}

bool CameraEditor::ShouldRenderSceneCameraPreview() const {
    // 専用ウィンドウ、対象オブジェクト、更新間隔の三条件を満たした場合だけ描く。
#ifdef USE_IMGUI
    if (!EditorToolSettings::Get().IsVisible("sceneCameraPreview")) return false;
#endif
    return sceneCameraPreviewWindowVisibleThisFrame_ &&
        HasSceneCameraPreviewTarget() &&
        IsPreviewUpdateDue(lastSceneCameraPreviewRenderTime_, hasRenderedSceneCameraPreview_);
}

void CameraEditor::NotifyCameraPreviewRendered(bool sceneCameraPreview) {
    // 通常プレビューとシーンカメラプレビューの時刻を別々に記録する。
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
    // 初回は即時描画し、以降は設定 FPS から求めた間隔で更新する。
    if (!hasRenderedFrame) {
        return true;
    }

    const int previewFps = std::clamp(settings_.cameraPreviewFps, 1, 60);
    const float updateInterval = 1.0f / static_cast<float>(previewFps);
    const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - lastRenderTime;
    return elapsed.count() >= updateInterval;
}

void CameraEditor::InvalidateCameraPreviews() {
    // 次の表示時に古いテクスチャを使わず、両プレビューを描き直す。
    hasRenderedCameraPreview_ = false;
    hasRenderedSceneCameraPreview_ = false;
}

Camera* CameraEditor::PreparePreviewCamera(float aspectRatio) {
    // ゲームカメラの投影条件を借り、エディターの視点だけを専用カメラに設定する。
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
    // 選択した Camera Object の画角とクリップ距離で独立したプレビューを作る。
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
    // 本番カメラへの入力と追従を切り、プレビュー用の視点と投影だけを更新する。
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
    // 設定値を渡されたカメラへ反映する際は追従を解除し、固定視点にする。
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

void CameraEditor::SetMode(Mode mode) {
    settings_.currentMode = mode;
}

void CameraEditor::SetEditorStateSaveBlocker(uint32_t blocker, bool enabled) {
    // ブックマーク復元など保存すべきでない操作をビット単位で重ねて抑止する。
    if (enabled) {
        editorStateSaveBlockers_ |= blocker;
    }
    else {
        editorStateSaveBlockers_ &= ~blocker;
    }
}

void CameraEditor::SetEditorCameraTransform(const Vector3& position, const Vector3& rotation) {
    const bool stateChanged =
        position.x != settings_.editorCameraPos.x ||
        position.y != settings_.editorCameraPos.y ||
        position.z != settings_.editorCameraPos.z ||
        rotation.x != settings_.editorCameraAngle.x ||
        rotation.y != settings_.editorCameraAngle.y ||
        rotation.z != settings_.editorCameraAngle.z;
    settings_.editorCameraPos = position;
    settings_.editorCameraAngle = rotation;
    if (stateChanged && !IsEditorStateSaveBlocked()) {
        editorStateDirty_ = true;
        editorStateSaveReadyAt_ =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    }

    // ブックマーク復帰後も設定値と実カメラのTransformを一致させます。

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

bool CameraEditor::FocusSceneObject(Object3d* object) {
    // モデルの外接範囲を基準に画角を決め、表示モデルのない物体は判定範囲を使う。
    if (!object || !IsObjectInCurrentScene(object)) {
        return false;
    }

    object->UpdateWorldMatrix();

    AABB focusBounds = object->GetModelWorldAABB();
    const AABB collisionBounds = object->GetAABB();
	if (object->GetModelName().empty() && object->GetCollider() && IsValidFocusBounds(collisionBounds)) {
        // 見た目を持たない当たり判定Objectは、実際の判定範囲を画角の基準にします。
        focusBounds = collisionBounds;
    }

    if (!IsValidFocusBounds(focusBounds)) {
        return false;
    }

    const Vector3 focusTarget = {
        (focusBounds.min.x + focusBounds.max.x) * 0.5f,
        (focusBounds.min.y + focusBounds.max.y) * 0.5f,
        (focusBounds.min.z + focusBounds.max.z) * 0.5f,
    };
    const Vector3 halfExtent = {
        (focusBounds.max.x - focusBounds.min.x) * 0.5f,
        (focusBounds.max.y - focusBounds.min.y) * 0.5f,
        (focusBounds.max.z - focusBounds.min.z) * 0.5f,
    };
    const float objectRadius = (std::max)({ halfExtent.x, halfExtent.y, halfExtent.z, 0.5f });

    const Vector3 worldForward = GetObjectWorldForward(object);
    const Vector3 frontDirection = NormalizeSafe(
        { worldForward.x, 0.0f, worldForward.z },
        { 0.0f, 0.0f, 1.0f });
    const float focusDistance = std::clamp(objectRadius * 2.8f + 2.0f, 8.0f, 120.0f);
    const float focusHeight = std::clamp(objectRadius * 0.3f, 2.0f, 24.0f);
    const Vector3 focusEye = focusTarget + frontDirection * focusDistance + Vector3{ 0.0f, focusHeight, 0.0f };
    const Vector3 rotation = MakeRotationFromForward(
        NormalizeSafe(focusTarget - focusEye, { 0.0f, 0.0f, -1.0f }));

    settings_.editorCameraPos = focusEye;
    settings_.editorCameraAngle = rotation;
    SetMode(Mode::Editor);
    SetEditorCameraTransform(focusEye, rotation);

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->SetTarget(focusTarget);
        camera->Update();
    }

    DebugConsole::GetInstance()->AddLog("Focused Scene Object: " + object->GetName());
    return true;
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
    // 開始角度は設定ファイル上の度から Camera API が使うラジアンへ変換する。
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
    // 現在位置を中心との差分に変換して、再現可能な半径・高さ・開始角として保存する。
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
    // プレイヤー追従時はワールド位置をプレイヤー相対オフセットに変換する。
    if (targetPlayer_) {
        Vector3 targetPos = targetPlayer_->GetWorldPosition();
        settings_.orbitCenterOffset = center - targetPos;
        settings_.orbitCenterOffset.y -= settings_.orbitCenterHeight;
        return;
    }

    settings_.fixedPointPos = center - settings_.orbitCenterOffset;
}

void CameraEditor::SetOrbitStartFromWorld(const Vector3& startEye) {
    // ワールド座標の開始視点から周回半径と角度を逆算する。
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
    // プレイヤーがある場合だけ追従中心を使い、編集画面では固定点に戻す。
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
    // 保存した半径・高さ・角度から周回開始時のワールド視点を復元する。
    const Vector3 center = GetOrbitCenter();
    const float startAngle = DegToRad(settings_.orbitStartAngleDeg);
    return {
        center.x + settings_.orbitRadius * std::cos(startAngle),
        center.y + settings_.orbitHeight,
        center.z + settings_.orbitRadius * std::sin(startAngle)
    };
}

Vector3 CameraEditor::GetConfiguredCameraEye() const {
    // 固定・周回・追従で視点の算出方法が異なり、対象不在時は現在視点を維持する。
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
    // 固定・周回・通常追従の各設定からプレビュー用の前方を揃えて返す。
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
    // ゲーム実行中はエディター自由カメラを映し、編集時はゲームカメラ設定を映す。
    if (settings_.currentMode == Mode::Game) {
        return settings_.editorCameraPos;
    }
    return GetConfiguredCameraEye();
}

Vector3 CameraEditor::GetPreviewCameraForward() const {
    // 視点と同じモード切り替えで向きを選び、混在したプレビューを防ぐ。
    if (settings_.currentMode == Mode::Game) {
        return NormalizeSafe(CalculateEditorForward(settings_.editorCameraAngle), { 0.0f, 0.0f, 1.0f });
    }
    return GetConfiguredCameraForward();
}

const char* CameraEditor::GetPreviewCameraLabel() const {
    // 現在の操作モードとプレビュー側の視点名を対応させる。
    return settings_.currentMode == Mode::Game ? "自由カメラ" : "ゲームカメラ";
}

Object3d* CameraEditor::FindSceneObjectByName(const std::string& name) const {
    // 保存済みの名前から現シーンの実体を探し、シーン切り替え後の古いポインタを使わない。
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
    // 現シーンの Camera Object だけを保持し、選択変更時はプレビューを無効化する。
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
    // 以前のシーンの選択ポインタは返さない。
    if (!selectedCameraObject_ || !IsObjectInCurrentScene(selectedCameraObject_)) {
        return nullptr;
    }
    return selectedCameraObject_->IsCameraObject() ? selectedCameraObject_ : nullptr;
}

bool CameraEditor::ResolveSceneCameraPose(const Object3d* cameraObject, Vector3& eye, Vector3& target) const {
    // eye と target の参照名を毎回現シーンで解決し、対象が消えた場合は前方へ退避する。
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
    // 編集用設定を Camera の実行用形式へ変換し、追従対象の名前を現シーンで解決する。
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
    // 保存されたポインタを破棄して名前から再解決し、視点だけは予備対象を使えるようにする。
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
    // 動的な視点対象が見つからなければ保存された固定座標を使う。
    const Camera::CameraOverrideParams runtime = MakeRuntimeOverrideParams(params);
    if (runtime.eyeSource == Camera::OverrideEyeSource::kSceneObject && runtime.eyeObject) {
        return runtime.eyeObject->GetWorldPosition() + runtime.eyeObjectOffset;
    }
    return params.fixedEyePos;
}

Vector3 CameraEditor::ResolveOverrideTarget(const Camera::CameraOverrideParams& params) const {
    // ターゲットの種類に応じて追従対象・視点前方・固定点を選ぶ。
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
    // 演出プレビューは現シーンで選択中の Camera Object の姿勢だけを使用する。
    Object3d* selectedCamera = GetSelectedCameraObject();
    return selectedCamera && ResolveSceneCameraPose(selectedCamera, eye, target);
}

Vector3 CameraEditor::GetConfiguredCameraRight(const Vector3& forward) const {
    // 真上を向いて外積がゼロに近い場合はワールド X を代替軸とする。
    const Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
    Vector3 right = Cross(worldUp, forward);
    if (Length(right) < 0.0001f) {
        right = { 1.0f, 0.0f, 0.0f };
    }
    return NormalizeSafe(right, { 1.0f, 0.0f, 0.0f });
}

Vector3 CameraEditor::GetConfiguredCameraUp(const Vector3& forward, const Vector3& right) const {
    // 前方と右方向から直交する上方向を求める。
    return NormalizeSafe(Cross(forward, right), { 0.0f, 1.0f, 0.0f });
}

Matrix4x4 CameraEditor::MakeLineBoxMatrix(const Vector3& start, const Vector3& end, float thickness) const {
    // 線分を細い箱で描くため、中点・長さ・向きからワールド行列を組む。
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
    // 個別指定がない場合は Camera Object に保存された補間条件で再生する。
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
    // 投影行列はオーバーライド外のため、切り替え前に退避して終了時に戻す。
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
    // 視点補間を終了し、開始前の画角・クリップ距離を復元する。
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

void CameraEditor::InvalidatePreviewForSceneChange() {
    // シーンをまたぐ Camera Object と描画基盤への参照をまとめて無効化する。
    selectedCameraObject_ = nullptr;
    hasRenderedCameraPreview_ = false;
    hasRenderedSceneCameraPreview_ = false;
    cameraPreviewPanelVisibleThisFrame_ = false;
    sceneCameraPreviewWindowVisibleThisFrame_ = false;
    lastCameraPreviewRenderTime_ = {};
    lastSceneCameraPreviewRenderTime_ = {};
    SetObject3dCommon(nullptr);
}
