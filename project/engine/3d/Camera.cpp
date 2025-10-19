#define NOMINMAX
#include "engine/3d/Camera.h"
#include "engine/base/WinApp.h" 
#include <algorithm> // std::min, std::max のために追加
// --- 角度変換ヘルパー ---
#include <cmath> // M_PI が定義されていない場合のため
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const float PI = (float)M_PI;

void Camera::Initialize() {
    // デフォルトの視点、注視点、上方向を設定
    eye_ = { 0.0f, 5.0f, -20.0f };
    target_ = { 0.0f, 0.0f, 0.0f };
    up_ = { 0.0f, 1.0f, 0.0f };
    rotation_ = { 0.0f, 0.0f, 0.0f }; // 回転も初期化

    // Releaseモードのデフォルト設定を eye_ から反映
    fixedOffset_ = eye_;

    // アスペクト比をウィンドウサイズから計算
    aspectRatio_ = (float)WinApp::kClientWidth / WinApp::kClientHeight;

    // ★ デバッグビルドかどうかに応じて、最初のモードを決める
#ifdef _DEBUG
    // デバッグビルドなら、追従対象を nullptr にして自由視点カメラから開始
    targetPosition_ = nullptr;
#else
    // リリースビルドなら、kFixed モードから開始 (targetPosition_ は SetTarget で設定待ち)
    followMode_ = FollowMode::kFixed;
#endif

    // 行列の計算を初回実行
    Update();
}

void Camera::Update() {
    Math math;

    // --- 1. 追従対象がいる (主にReleaseビルド) ---
    if (targetPosition_) {

        // ★ Release用モードに応じてカメラの座標を計算
        switch (followMode_) {
        case FollowMode::kFixed:
        {
            // 従来の固定追従
            eye_ = *targetPosition_ + fixedOffset_;
            target_ = *targetPosition_;
            break;
        }

        case FollowMode::kAimable:
        {
            // --- ズーム可能な第三者視点 ---
            // 1. ターゲット(注視点)は対象の座標
            target_ = *targetPosition_;

            // 2. 回転行列の計算 (rotation_ は AddRotation で更新される)
            Matrix4x4 rotateMatrix = math.Multiply(math.MakeRotateXMatrix(rotation_.x), math.MakeRotateYMatrix(rotation_.y));

            // 3. オフセットベクトルの計算 (Z軸マイナス（後ろ）が基準)
            Vector3 offset = { 0.0f, 0.0f, -distance_ };
            offset = math.TransformNormal(offset, rotateMatrix);

            // 4. 視点(eye) = 注視点 + オフセット
            eye_ = target_ + offset;
            break;
        }

        case FollowMode::kFirstPerson:
        {
            // --- 一人称視点 ---
            // 1. 視点(eye) = ターゲットの座標 + FPS用オフセット
            eye_ = *targetPosition_ + firstPersonOffset_;

            // 2. 回転行列の計算 (rotation_ は AddRotation で更新される)
            Matrix4x4 rotateMatrix = math.Multiply(math.MakeRotateXMatrix(rotation_.x), math.MakeRotateYMatrix(rotation_.y));
            Vector3 forward = { 0.0f, 0.0f, 1.0f }; // Z軸プラス（前）
            forward = math.TransformNormal(forward, rotateMatrix);

            // 3. 注視点(target) = 視点 + 前方ベクトル
            target_ = eye_ + forward;
            break;
        }
        }
    }
    // --- 2. 追従対象がいない (主にDebugビルド) ---
    else {
#ifdef _DEBUG
        if (inputManager_) {
            // 左クリック + マウス移動での回転
            if (inputManager_->IsMouseButtonPressed(0)) {
                Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
                const float rotateSpeed = 0.01f;
                rotation_.x += mouseDelta.y * rotateSpeed;
                rotation_.y += mouseDelta.x * rotateSpeed;
            }
            // A/D/Q/E/W/S/Wheel での移動
            Vector3 move = { 0, 0, 0 };
            const float moveSpeed = 0.3f;
            if (inputManager_->IsKeyPressed(DIK_A)) { move.x -= moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_D)) { move.x += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_E)) { move.y += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_Q)) { move.y -= moveSpeed; }

            float wheelDelta = inputManager_->GetMouseWheelDelta();
            const float wheelSpeed = 0.005f;

            if (inputManager_->IsKeyPressed(DIK_W)) { move.z += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_S)) { move.z -= moveSpeed; }
            move.z += wheelDelta * wheelSpeed;

            Matrix4x4 rotateMatrix = math.Multiply(math.MakeRotateXMatrix(rotation_.x), math.MakeRotateYMatrix(rotation_.y));
            move = math.TransformNormal(move, rotateMatrix);
            eye_ = eye_ + move;

            // 注視点も一緒に動かす
            Vector3 targetForward = { 0.0f, 0.0f, 1.0f };
            targetForward = math.TransformNormal(targetForward, rotateMatrix);
            target_ = eye_ + targetForward;
        }
#endif // _DEBUG
    }

    // --- 3. 共通の行列計算 ---
    // (追従でもデバッグでも、計算された eye_, target_, up_ をもとに行列を生成)
    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
}



void Camera::SetTarget(const Vector3* target) {
    // (前回実装した、デバッグビルド中は無効化する処理)
#ifdef _DEBUG
    (void)target; // 警告抑制
    targetPosition_ = nullptr;
#else
    targetPosition_ = target;
#endif
}

void Camera::SetFollowMode(FollowMode mode) {
    followMode_ = mode;
}

void Camera::ConfigFixed(const Vector3& offset) {
    fixedOffset_ = offset;
}

void Camera::ConfigAimable(float distance, float minDistance, float maxDistance) {
    distance_ = distance;
    minDistance_ = minDistance;
    maxDistance_ = maxDistance;
}

void Camera::ConfigFirstPerson(const Vector3& eyeOffset) {
    firstPersonOffset_ = eyeOffset;
}

void Camera::AddRotation(const Vector2& mouseDelta) {
    // kAimable と kFirstPerson で共用の回転処理
    // (感度はデバッグカメラより少し抑えめにするなど、調整してください)
    const float rotateSpeed = 0.005f;
    rotation_.x += mouseDelta.y * rotateSpeed;
    rotation_.y += mouseDelta.x * rotateSpeed;

    // X軸の回転（ピッチ）に制限をかける (見上げすぎ、見下ろしすぎ防止)
    const float pitchLimit = (float)M_PI / 2.0f - 0.01f; // 90度少し手前
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));
}

void Camera::AddZoom(float wheelDelta) {
    // kAimable モードでのみ使用
    const float zoomSpeed = 0.005f; // (ホイール感度)
    distance_ -= wheelDelta * zoomSpeed;
    // 距離を min/max の範囲にクランプする
    distance_ = std::max(minDistance_, std::min(maxDistance_, distance_));
}