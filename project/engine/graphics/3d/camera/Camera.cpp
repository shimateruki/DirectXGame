#define NOMINMAX
#include "Camera.h"
#include "WinApp.h" 
#include <algorithm> // std::min, std::max
#include "imgui.h"
#include "ImGuizmo.h"
#include "Object3d.h"
#include <cmath>
#include <CollisionManager.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const float PI = (float)M_PI;
static Math math;

void Camera::ConfigFixedPoint(const Vector3& position) {
    fixedPointPos_ = position;
}

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

    // デフォルトのカメラモードを kAimable に設定
    followMode_ = FollowMode::kAimable;

    // kAimable のデフォルト距離を設定
    distance_ = 10.0f;

    isInputEnabled_ = true;
}

void Camera::Update() {
    static Math math;

    // -----------------------------------------------------------------
    //  ゲーム内カメラ挙動
    // -----------------------------------------------------------------
    if (followObject_) {
        Vector3 playerPos = followObject_->GetWorldPosition();

        // (A) 注視点 (Target) の計算
        // 全モード共通： プレイヤーの足元 + 高さ(Height)
        Vector3 targetPos = playerPos;
        targetPos.y += aimHeight_;

        // ロックオン時は敵を見る
        if (followMode_ == FollowMode::kLockOn && targetObject_) {
            targetPos = targetObject_->GetWorldPosition();
        }
        // 一人称は目の高さ調整として Height を使う
        else if (followMode_ == FollowMode::kFirstPerson) {
            targetPos = playerPos + firstPersonOffset_;
        }

        target_ = targetPos; // Target決定！

        // (B) カメラ位置 (Eye) の理想位置を計算
        Vector3 desiredEye = eye_;
        float toRad = 3.14159265f / 180.0f;

        switch (followMode_) {
        case FollowMode::kLockOn:
            if (targetObject_) {
                Matrix4x4 rotateMat = math.MakeRotateYMatrix(followObject_->GetRotation().y);
                Vector3 rotatedOffset = math.TransformNormal(lockOnOffset_, rotateMat);
                desiredEye = playerPos + rotatedOffset;
            } else {
                followMode_ = FollowMode::kAimable;
            }
            break;

        case FollowMode::kAimable:
        {


            // Z軸回転も含めた回転行列を作成
            // Pitch(X), Yaw(Y), Roll(Z) をすべて反映
            Matrix4x4 rotateMat =
                math.MakeRotateZMatrix(rotation_.z) * math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);

            Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
            offset = math.TransformNormal(offset, rotateMat);
            desiredEye = target_ + offset;
        }
        break;

        case FollowMode::kFixed:
        {
            // ★修正点3: Fixedモードも aimAngle_.x (Pitch) を使うように修正
            float currentY = rotation_.y;
            float pitch = aimAngle_.x * toRad; // X成分をPitchとして使用

            Matrix4x4 rotateMat = math.MakeRotateXMatrix(pitch) * math.MakeRotateYMatrix(currentY);
            Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
            offset = math.TransformNormal(offset, rotateMat);
            desiredEye = target_ + offset;

            rotation_.x = pitch;
        }
        break;

        case FollowMode::kFirstPerson:
            desiredEye = targetPos;
            {
                // 一人称も3軸回転を反映させたい場合はここも修正可能だが、通常はX/Yのみ
                // 3軸反映させるなら kAimable と同様の回転行列を使う
                Matrix4x4 rotateMatFP = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
                Vector3 forward = math.TransformNormal({ 0, 0, 1 }, rotateMatFP);
                target_ = desiredEye + forward;
            }
            break;

        case FollowMode::kOrbit:
        {
            if (followObject_) {
                // 1. 角度を更新 (速度を加算)
                orbitAngle_ += orbitSpeed_;

                // 2. ターゲット座標（プレイヤー）を取得
                Vector3 tPos = followObject_->GetWorldPosition();

                // 3. ターゲットを中心に円運動する座標を計算
                desiredEye.x = tPos.x + orbitRadius_ * std::cos(orbitAngle_);
                desiredEye.z = tPos.z + orbitRadius_ * std::sin(orbitAngle_);
                desiredEye.y = tPos.y + orbitHeight_;

                // 4. 常にターゲットを見る
                target_ = tPos;
            }
        }
        break;

        case FollowMode::kFixedPoint:
            // カメラ位置は「指定された固定座標」にする
            desiredEye = fixedPointPos_;
            target_ = targetPos;
            break;
        }

        // -----------------------------------------------------------------
        // (C) 壁めり込み防止 & 位置の確定
        // -----------------------------------------------------------------
        if (isEyeFrozen_) {
            // フリーズ中は更新しない
        } else {
            // 通常時の処理 (Raycast & 位置更新)
            if (followMode_ != FollowMode::kFirstPerson) {
                Vector3 toEye = desiredEye - target_;
                float dist = math.Length(toEye);
                Vector3 direction = (dist > 0.001f) ? math.Normalize(toEye) : Vector3{ 0,0,1 };

                // 0.1以上離れているならレイを飛ばす
                if (dist > 0.1f) {
                    RaycastHit hit = CollisionManager::GetInstance()->Raycast(
                        target_, direction, dist, kGround
                    );
                    if (hit.isHit) {
                        const float kEpsilon = 0.2f;
                        eye_ = hit.hitPoint - (direction * kEpsilon);
                    } else {
                        eye_ = desiredEye;
                    }
                } else {
                    eye_ = desiredEye;
                }
            } else {
                // 一人称視点はRaycastしない
                eye_ = desiredEye;
            }
        }
    }

    // 行列更新
    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
}

void Camera::SetFollowMode(FollowMode mode) {
    followMode_ = mode;
}

void Camera::ConfigFixed(const Vector3& offset) {
    fixedOffset_ = offset;
}

void Camera::ConfigAimable(float distance, float height, const Vector3& angle) {
    aimDistance_ = distance;
    aimHeight_ = height;
    aimAngle_ = angle;
    float toRad = 3.14159265f / 180.0f;
    rotation_.x = angle.x * toRad;
    rotation_.y = angle.y * toRad;
    rotation_.z = angle.z * toRad;
}

void Camera::ConfigFirstPerson(const Vector3& eyeOffset) {
    firstPersonOffset_ = eyeOffset;
}

void Camera::AddRotation(const Vector2& mouseDelta) {
    // kAimable と kFirstPerson で共用の回転処理
    const float rotateSpeed = 0.005f;
    rotation_.x += mouseDelta.y * rotateSpeed;
    rotation_.y += mouseDelta.x * rotateSpeed;

    // X軸の回転（ピッチ）に制限をかける
    const float pitchLimit = PI / 2.0f - 0.01f; // 90度手前
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));

    // Y軸の回転（ヨー）は 2PI でラップアラウンド
    if (rotation_.y > PI) { rotation_.y -= 2.0f * PI; }
    if (rotation_.y < -PI) { rotation_.y += 2.0f * PI; }
}

void Camera::SyncRotationToCurrentView() {
    static Math math;

    // (A) 注視点 (Target) を決定する
    Vector3 targetPos;
    if (targetObject_) {
        targetPos = targetObject_->GetWorldPosition();
    } else if (followObject_) {
        targetPos = followObject_->GetWorldPosition();
    } else {
        targetPos = target_;
    }

    // (B) Eye から Target への「前方ベクトル」を計算
    Vector3 forward = targetPos - eye_;
    if (math.Length(forward) < 0.001f) {
        forward = { 0.0f, 0.0f, 1.0f };
    } else {
        forward = math.Normalize(forward);
    }

    // (C) 前方ベクトルからヨー(Y軸回転)とピッチ(X軸回転)を逆算
    rotation_.y = std::atan2(forward.x, forward.z);
    rotation_.x = std::asin(-forward.y);

    const float pitchLimit = PI / 2.0f - 0.01f;
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));
}