
#include "engine/3d/Camera.h"
#include "engine/base/WinApp.h" 

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
    // (SetTargetが _DEBUG で無効化されていれば、 targetPosition_ は nullptr のまま)
    if (targetPosition_) {
        // --- ターゲット追従カメラの処理 (Releaseビルド時のみ) ---
        eye_ = *targetPosition_ + followOffset_;
        target_ = *targetPosition_;

    } else {
        // --- デバッグ用の自由移動カメラの処理 ---
        // ★★★ このロジック全体を _DEBUG マクロで囲む ★★★
#ifdef _DEBUG
        if (inputManager_) {

            // --- ★★★ 角度変更：左クリック（0）に変更 ★★★ ---
            if (inputManager_->IsMouseButtonPressed(0)) { // 1 -> 0 に変更
                Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
                const float rotateSpeed = 0.001f; // (感度はここで調整)
                rotation_.x += mouseDelta.y * rotateSpeed;
                rotation_.y += mouseDelta.x * rotateSpeed;
            }

            Vector3 move = { 0, 0, 0 };
            const float moveSpeed = 0.3f; // (感度はここで調整)

            // --- ★★★ キー移動：A/DでX軸、Q/EでY軸 ★★★ ---
            if (inputManager_->IsKeyPressed(DIK_A)) { move.x -= moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_D)) { move.x += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_E)) { move.y += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_Q)) { move.y -= moveSpeed; }

            // --- ★★★ Z軸移動：W/S と マウスホイール ★★★ ---
            // (InputManagerの修正が前提)
            float wheelDelta = inputManager_->GetMouseWheelDelta();
            const float wheelSpeed = 0.005f; // (ホイール感度はここで調整)

            if (inputManager_->IsKeyPressed(DIK_W)) { move.z += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_S)) { move.z -= moveSpeed; }
            move.z += wheelDelta * wheelSpeed; // ホイールでもZ軸移動


            Math math;
            Matrix4x4 rotateMatrix = math.Multiply(math.MakeRotateXMatrix(rotation_.x), math.MakeRotateYMatrix(rotation_.y));

            move = math.TransformNormal(move, rotateMatrix);
            eye_ = eye_ + move;

            // ★ デバッグカメラでは注視点(target)もカメラと一緒に動かす
            Vector3 targetForward = { 0.0f, 0.0f, 1.0f }; // カメラの前方
            targetForward = math.TransformNormal(targetForward, rotateMatrix);
            target_ = eye_ + targetForward; // 視点(eye)から1.0f前
        }
#endif // _DEBUG
    }

    // --- 共通の行列計算 ---
    Math math;
    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
}

void Camera::SetTarget(const Vector3* target) {
    // ★★★ デバッグビルド中はターゲット追従を無効にする ★★★
#ifdef _DEBUG
    // デバッグビルド中は追従を無効にする
    (void)target; // 引数を使わないことによる警告を抑制
    targetPosition_ = nullptr; // 念のため nullptr に
#else
    // リリースビルド中は従来通り
    targetPosition_ = target;
#endif
}