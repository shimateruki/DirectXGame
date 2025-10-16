#include "engine/3d/Camera.h"
#include "engine/base/WinApp.h" // ウィンドウサイズ取得のため

void Camera::Initialize() {
    // デフォルトの視点、注視点、上方向を設定
    eye_ = { 0.0f, 5.0f, -20.0f };
    target_ = { 0.0f, 0.0f, 0.0f };
    up_ = { 0.0f, 1.0f, 0.0f };
    rotation_ = { 0.0f, 0.0f, 0.0f };

    // アスペクト比をウィンドウサイズから計算
    aspectRatio_ = (float)WinApp::kClientWidth / WinApp::kClientHeight;

    // 行列の計算を初回実行
    Update();
}

void Camera::Update() {
    // ターゲットが設定されているか？
    if (targetPosition_) {
        // --- ターゲット追従カメラの処理 ---

        // ターゲットの座標にオフセットを加えたものが、カメラの位置(eye)になる
        eye_ = *targetPosition_ + followOffset_;

        // カメラの注視点(target)は、ターゲットの座標そのもの
        target_ = *targetPosition_;

    } else {
        // --- 従来の自由移動カメラの処理 ---
        if (inputManager_) {
            // (ここは既存のWASD移動とマウス回転のコードなので変更なし)
            if (inputManager_->IsMouseButtonPressed(1)) {
                Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
                const float rotateSpeed = 0.01f;
                rotation_.x += mouseDelta.y * rotateSpeed;
                rotation_.y += mouseDelta.x * rotateSpeed;
            }
            Vector3 move = { 0, 0, 0 };
            const float moveSpeed = 0.3f;
            if (inputManager_->IsKeyPressed(DIK_W)) { move.z += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_S)) { move.z -= moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_A)) { move.x -= moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_D)) { move.x += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_E)) { move.y += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_Q)) { move.y -= moveSpeed; }
            Math math;
            Matrix4x4 rotateMatrix = math.Multiply(math.MakeRotateXMatrix(rotation_.x), math.MakeRotateYMatrix(rotation_.y));
            move = math.TransformNormal(move, rotateMatrix);
            eye_ = eye_ + move;
        }
    }

    // --- 共通の行列計算 ---
    // (Update関数の最後にあるこの部分は、追従でも自由移動でも共通で使うので変更なし)
    Math math;
    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
}

void Camera::SetTarget(const Vector3* target) {
    targetPosition_ = target;
}