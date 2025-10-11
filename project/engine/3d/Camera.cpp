#include "engine/3d/Camera.h"
#include "engine/base/WinApp.h" // ウィンドウサイズ取得のため

void Camera::Initialize() {
    // デフォルトの視点、注視点、上方向を設定
    eye_ = { 0.0f, 5.0f, -30.0f };
    target_ = { 0.0f, 0.0f, 0.0f };
    up_ = { 0.0f, 1.0f, 0.0f };
    rotation_ = { 0.0f, 0.0f, 0.0f };

    // アスペクト比をウィンドウサイズから計算
    aspectRatio_ = (float)WinApp::kClientWidth / WinApp::kClientHeight;

    // 行列の計算を初回実行
    Update();
}

void Camera::Update() {
    // --- デバッグ用のカメラ操作 ---
    if (inputManager_) {
        // マウス右クリックで回転
        if (inputManager_->IsMouseButtonPressed(1)) {
            Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
            const float rotateSpeed = 0.01f;
            rotation_.x += mouseDelta.y * rotateSpeed;
            rotation_.y += mouseDelta.x * rotateSpeed;
        }

        // キーボードで移動
        Vector3 move = { 0, 0, 0 };
        const float moveSpeed = 0.3f;
        if (inputManager_->IsKeyPressed(DIK_W)) { move.z += moveSpeed; }
        if (inputManager_->IsKeyPressed(DIK_S)) { move.z -= moveSpeed; }
        if (inputManager_->IsKeyPressed(DIK_A)) { move.x -= moveSpeed; }
        if (inputManager_->IsKeyPressed(DIK_D)) { move.x += moveSpeed; }
        if (inputManager_->IsKeyPressed(DIK_E)) { move.y += moveSpeed; }
        if (inputManager_->IsKeyPressed(DIK_Q)) { move.y -= moveSpeed; }

        // 回転行列を計算
        Math math;
        Matrix4x4 rotateMatrix = math.Multiply(math.MakeRotateXMatrix(rotation_.x), math.MakeRotateYMatrix(rotation_.y));

        // 移動ベクトルをカメラのローカル座標系に変換
        move = math.TransformNormal(move, rotateMatrix);

        // 視点と注視点を移動
        eye_ = eye_ + move;
        target_ = target_ + move;
    }

    // --- 行列の計算 ---
    Math math;
    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
}