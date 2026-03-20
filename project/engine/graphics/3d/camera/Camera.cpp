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

// 修正: 明示的に float にキャスト
const float PI = (float)M_PI;
static Math math;

void Camera::ConfigFixedPoint(const Vector3& position) {
    fixedPointPos_ = position;
}

void Camera::UpdateProjectionMatrix() {

    static Math math;

    // 現在のパラメータを使ってプロジェクション行列を再計算
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
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
    auto LerpVec3 = [](const Vector3& a, const Vector3& b, float t) {
        return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
        };

    // -----------------------------------------------------------------
    //  (A) 注視点 (Target) の計算
    // -----------------------------------------------------------------
    if (followObject_) {
        Vector3 playerPos = followObject_->GetWorldPosition();

        // 基本はプレイヤーの足元 + 設定された高さ
        Vector3 targetPos = playerPos;
        targetPos.y += aimHeight_;

        if (followMode_ == FollowMode::kLockOn && targetObject_) {
            Vector3 enemyPos = targetObject_->GetWorldPosition();

            // プレイヤー側の基準点を少し下げる（足元ではなく胸のあたり）
            Vector3 playerFocus = playerPos;
            playerFocus.y += aimHeight_ * 0.5f;

            // =======================================================
            // ★修正1：完全な「中間点注視」の復活！
            // プレイヤーと敵の「ちょうど真ん中」を真っ直ぐ見つめる最強のカメラ
            // =======================================================
            targetPos.x = playerFocus.x + (enemyPos.x - playerFocus.x) * 0.5f;
            targetPos.y = playerFocus.y + (enemyPos.y - playerFocus.y) * 0.5f;
            targetPos.z = playerFocus.z + (enemyPos.z - playerFocus.z) * 0.5f;
        }
        else if (followMode_ == FollowMode::kFirstPerson) {
            targetPos = playerPos + firstPersonOffset_;
        }

        target_ = targetPos; // Target決定！

        // -----------------------------------------------------------------
        //  (B) カメラ位置 (Eye) の理想位置を計算
        // -----------------------------------------------------------------
        Vector3 desiredEye = eye_;
        float toRad = 3.14159265f / 180.0f;

        switch (followMode_) {
        case FollowMode::kLockOn:
        {
            if (targetObject_) {
                Vector3 enemyPos = targetObject_->GetWorldPosition();
                Vector3 toEnemy = enemyPos - playerPos;

                float distanceXZ = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z);
                float heightDiff = std::max(0.0f, enemyPos.y - playerPos.y);

                // =======================================================
                // ★修正2：上空カメラ禁止！遠い時は「後ろ」に引くだけにする
                // =======================================================
                float zoom = std::max(0.0f, distanceXZ - 10.0f) * 0.5f; // 引き(Z)を少し強めに
                zoom = std::min(zoom, 25.0f); // 最大で25mまでしか引かない

                Vector3 dynamicOffset = lockOnOffset_;
                dynamicOffset.z -= zoom;
                dynamicOffset.y += zoom * 0.15f; // ★上には少ししか上げない！（上空カメラ化を防止）

                // 敵の方向（角度）を計算し、滑らかに追従させる
                float angleToEnemy = std::atan2(toEnemy.x, toEnemy.z);
                auto NormalizeAngle = [](float a) {
                    while (a > 3.1415926535f) a -= 6.2831853071f;
                    while (a < -3.1415926535f) a += 6.2831853071f;
                    return a;
                    };

                float diff = NormalizeAngle(angleToEnemy - rotation_.y);
                rotation_.y += diff * 0.08f; // 追従速度

                // 角度を使ってカメラの配置場所を計算
                Matrix4x4 rotateMat = math.MakeRotateYMatrix(rotation_.y);
                Vector3 rotatedOffset = math.TransformNormal(dynamicOffset, rotateMat);
                desiredEye = playerPos + rotatedOffset;
            }
            else {
                followMode_ = FollowMode::kAimable;
            }
            break;
        }

        case FollowMode::kAimable:
        {
            Matrix4x4 rotateMat = math.MakeRotateZMatrix(rotation_.z) * math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
            Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
            offset = math.TransformNormal(offset, rotateMat);
            desiredEye = target_ + offset;
            break;
        }

        case FollowMode::kFixed:
        {
            float currentY = rotation_.y;
            float pitch = aimAngle_.x * toRad;
            Matrix4x4 rotateMat = math.MakeRotateXMatrix(pitch) * math.MakeRotateYMatrix(currentY);
            Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
            offset = math.TransformNormal(offset, rotateMat);
            desiredEye = target_ + offset;
            rotation_.x = pitch;
            break;
        }

        case FollowMode::kFirstPerson:
        {
            desiredEye = targetPos;
            Matrix4x4 rotateMatFP = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
            Vector3 forward = math.TransformNormal({ 0, 0, 1 }, rotateMatFP);
            target_ = desiredEye + forward;
            break;
        }

        case FollowMode::kOrbit:
        {
            if (followObject_) {
                orbitAngle_ += orbitSpeed_;
                Vector3 tPos = followObject_->GetWorldPosition();
                desiredEye.x = tPos.x + orbitRadius_ * std::cos(orbitAngle_);
                desiredEye.z = tPos.z + orbitRadius_ * std::sin(orbitAngle_);
                desiredEye.y = tPos.y + orbitHeight_;
                target_ = tPos;
            }
            break;
        }

        case FollowMode::kFixedPoint:
        {
            desiredEye = fixedPointPos_;
            target_ = targetPos;
            break;
        }
        }

        // -----------------------------------------------------------------
        //  滑らかな補間処理
        // -----------------------------------------------------------------
        if (!isCameraInitialized_) {
            smoothTarget_ = target_;
            smoothEye_ = desiredEye;
            isCameraInitialized_ = true;
        }

        smoothTarget_ = LerpVec3(smoothTarget_, target_, 0.1f);
        smoothEye_ = LerpVec3(smoothEye_, desiredEye, 0.1f);

        target_ = smoothTarget_;
        desiredEye = smoothEye_;

        // -----------------------------------------------------------------
        //  (C) 壁めり込み防止 & 地面埋まり防止の確定
        // -----------------------------------------------------------------
        if (!isEyeFrozen_) {
            if (followMode_ != FollowMode::kFirstPerson) {
                // レイを飛ばす起点を少し高くして、地面の凹凸での誤爆を防ぐ
                Vector3 rayStart = target_;
                rayStart.y += 0.5f;

                Vector3 toEye = desiredEye - rayStart;
                float dist = math.Length(toEye);
                Vector3 direction = (dist > 0.001f) ? math.Normalize(toEye) : Vector3{ 0,0,1 };

                if (dist > 0.1f) {
                    RaycastHit hit = CollisionManager::GetInstance()->Raycast(rayStart, direction, dist, 1);
                    if (hit.isHit) {
                        const float kEpsilon = 0.8f;
                        eye_ = hit.hitPoint - (direction * kEpsilon);
                    }
                    else {
                        eye_ = desiredEye;
                    }
                }
                else {
                    eye_ = desiredEye;
                }

                // =======================================================
                // ★修正3：絶対に地面に埋まらない「最強の高さストッパー」！
                // プレイヤーの足元 + 0.5m より下にはカメラを絶対に行かせない
                // =======================================================
                float groundLimitY = playerPos.y + 0.5f;
                if (eye_.y < groundLimitY) {
                    eye_.y = groundLimitY;
                }
            }
            else {
                eye_ = desiredEye;
            }
        }

        // -----------------------------------------------------------------
        //  (D) プレイヤーが画面を埋め尽くす問題の解決（近距離フェード）
        // -----------------------------------------------------------------
        Vector3 playerPosForDist = followObject_->GetWorldPosition();
        Vector3 toPlayer = playerPosForDist - eye_;
        float camToPlayerDist = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y + toPlayer.z * toPlayer.z);

        float alpha = 1.0f;
        if (camToPlayerDist < 1.5f) {
            alpha = std::max(0.0f, (camToPlayerDist - 0.5f) / 1.0f);
        }

        Vector4 pColor = followObject_->GetColor();
        followObject_->SetColor({ pColor.x, pColor.y, pColor.z, alpha });

        for (Object3d* child : followObject_->GetChildren()) {
            if (child) {
                Vector4 cColor = child->GetColor();
                child->SetColor({ cColor.x, cColor.y, cColor.z, alpha });
            }
        }
    }

    // -----------------------------------------------------------------
    //  行列更新
    // -----------------------------------------------------------------
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
    }
    else if (followObject_) {
        targetPos = followObject_->GetWorldPosition();
    }
    else {
        targetPos = target_;
    }

    // (B) Eye から Target への「前方ベクトル」を計算
    Vector3 forward = targetPos - eye_;
    if (math.Length(forward) < 0.001f) {
        forward = { 0.0f, 0.0f, 1.0f };
    }
    else {
        forward = math.Normalize(forward);
    }

    // (C) 前方ベクトルからヨー(Y軸回転)とピッチ(X軸回転)を逆算
    rotation_.y = std::atan2(forward.x, forward.z);
    rotation_.x = std::asin(-forward.y);

    const float pitchLimit = PI / 2.0f - 0.01f;
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));
}