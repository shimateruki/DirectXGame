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

// 明示的に float にキャスト
const float PI = (float)M_PI;
static Math math;

void Camera::ConfigFixedPoint(const Vector3& position, const Vector3& angle) {
    fixedPointPos_ = position;
    fixedPointAngle_ = angle; 
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

    // =================================================================
    // [1] 通常カメラの処理（プレイヤー追従など）
    // =================================================================
    if (followObject_) {
        Vector3 playerPos = followObject_->GetWorldPosition();

        // -----------------------------------------------------------------
         // (A) 注視点 (Target) の計算
         // -----------------------------------------------------------------
        Vector3 targetPos = playerPos;
        targetPos.y += aimHeight_; // 基本はプレイヤーの足元 + 設定された高さ

        if (followMode_ == FollowMode::kLockOn && targetObject_) {
            Vector3 enemyPos = targetObject_->GetWorldPosition();
            Vector3 playerFocus = playerPos;
            playerFocus.y += aimHeight_ * 0.5f;

            // XZは敵寄りを注視 (0.7f)
            targetPos.x = playerFocus.x + (enemyPos.x - playerFocus.x) * 0.7f;
            targetPos.z = playerFocus.z + (enemyPos.z - playerFocus.z) * 0.7f;

            float distXZ = std::sqrt((enemyPos.x - playerFocus.x) * (enemyPos.x - playerFocus.x) +
                (enemyPos.z - playerFocus.z) * (enemyPos.z - playerFocus.z));

            float maxY = playerFocus.y + std::min(3.0f, distXZ * 0.4f);
            float idealY = playerFocus.y + (enemyPos.y - playerFocus.y) * 0.5f;

            targetPos.y = std::min(idealY, maxY);
        }
        else if (followMode_ == FollowMode::kFirstPerson) {
            targetPos = playerPos + firstPersonOffset_;
        }

        // =========================================================
        // ★ 修正1: プレイヤーとカメラの1フレームのズレを完全に無くす！
        // =========================================================
        // TargetのLerpを完全に排除し、プレイヤーの位置に100%即座に追従させる。
        // これにより、アニメーションの揺れとカメラが喧嘩してカクつくのを防ぎます。
        smoothTarget_ = targetPos;
        target_ = smoothTarget_;

        // -----------------------------------------------------------------
        // (B) カメラ位置 (Eye) の計算
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
                float heightZoom = heightDiff * 0.8f;

                float closeZoomOut = 0.0f;
                if (distanceXZ < 8.0f) {
                    closeZoomOut = (8.0f - distanceXZ) * 0.7f;
                }

                float zoom = std::max(0.0f, distanceXZ - 10.0f) * 0.5f;
                zoom = std::min(zoom, 25.0f);

                Vector3 dynamicOffset = { 2.0f, 2.0f + (closeZoomOut * 0.3f), -6.5f - closeZoomOut - heightZoom };
                dynamicOffset.z -= zoom;
                dynamicOffset.y += zoom * 0.15f;

                float angleToEnemy = std::atan2(toEnemy.x, toEnemy.z);
                auto NormalizeAngle = [](float a) {
                    while (a > 3.1415926535f) a -= 6.2831853071f;
                    while (a < -3.1415926535f) a += 6.2831853071f;
                    return a;
                    };

                float diff = NormalizeAngle(angleToEnemy - rotation_.y);
                rotation_.y += diff * 0.08f;

                Matrix4x4 rotateMat = math.MakeRotateYMatrix(rotation_.y);
                Vector3 rotatedOffset = math.TransformNormal(dynamicOffset, rotateMat);

                Vector3 smoothBase = playerPos;
                desiredEye = smoothBase + rotatedOffset;
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
            desiredEye = target_;
            Matrix4x4 rotateMatFP = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
            Vector3 forwardFP = math.TransformNormal({ 0, 0, 1 }, rotateMatFP);
            target_ = desiredEye + forwardFP;
            break;
        }
        case FollowMode::kOrbit:
        {
            orbitAngle_ += orbitSpeed_;
            desiredEye.x = target_.x + orbitRadius_ * std::cos(orbitAngle_);
            desiredEye.z = target_.z + orbitRadius_ * std::sin(orbitAngle_);
            desiredEye.y = target_.y + orbitHeight_;
            break;
        }
        case FollowMode::kFixedPoint:
        {
            desiredEye = fixedPointPos_;
            float pitch = fixedPointAngle_.x;
            float yaw = fixedPointAngle_.y;
            Vector3 forward;
            forward.x = std::sin(yaw) * std::cos(pitch);
            forward.y = -std::sin(pitch);
            forward.z = std::cos(yaw) * std::cos(pitch);

            target_ = desiredEye + forward * 10.0f;
            break;
        }
        }

        eye_ = desiredEye;

        if (!isEyeFrozen_) {
            if (followMode_ != FollowMode::kFirstPerson && followMode_ != FollowMode::kFixedPoint) {


                Vector3 rayStartPos = playerPos;
                rayStartPos.y += aimHeight_ * 0.5f; // 胸の高さ

                Vector3 toEye = desiredEye - rayStartPos;
                float dist = math.Length(toEye);
                Vector3 direction = (dist > 0.001f) ? math.Normalize(toEye) : Vector3{ 0,0,1 };

                if (dist > 0.1f) {
                    RaycastHit hit = CollisionManager::GetInstance()->Raycast(
                        rayStartPos, direction, dist, kGround
                    );

                    if (hit.isHit) {
                        const float kEpsilon = 0.2f;
                        eye_ = hit.hitPoint - (direction * kEpsilon);
                    }
                    else {
                        eye_ = desiredEye;
                    }
                }
                else {
                    eye_ = desiredEye;
                }

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
        // (D) 近距離フェード
        // -----------------------------------------------------------------
        Vector3 playerPosForDist = followObject_->GetWorldPosition();
        playerPosForDist.y += aimHeight_ * 0.5f;

        Vector3 toPlayer = playerPosForDist - eye_;
        float camToPlayerDist = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y + toPlayer.z * toPlayer.z);

        float alpha = 1.0f;
        if (camToPlayerDist < 3.5f) {
            alpha = std::max(0.0f, (camToPlayerDist - 1.0f) / 2.5f);
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

    // =================================================================
    // [2] 動的カメラオーバーライドの補間処理
    // =================================================================
    Vector3 normalEye = eye_;
    Vector3 normalTarget = target_;
    if (followObject_ == nullptr && isOverridden_) {
        normalEye = overrideStartEye_;
        normalTarget = overrideStartTarget_;
    }
    if (overrideDuration_ > 0.0f) {
        float deltaTime = 1.0f / 60.0f;

        if (isOverridden_) {
            overrideTimer_ += deltaTime;
        }
        else {
            overrideTimer_ -= deltaTime;
        }

        overrideTimer_ = std::max(0.0f, std::min(overrideTimer_, overrideDuration_));
        float t = overrideTimer_ / overrideDuration_;
        overrideWeight_ = t * t * (3.0f - 2.0f * t);

        if (overrideWeight_ > 0.0f) {
            Vector3 finalEndTarget = overrideParams_.fixedTargetPos;
            if (overrideParams_.trackTargetX) finalEndTarget.x = normalTarget.x;
            if (overrideParams_.trackTargetY) finalEndTarget.y = normalTarget.y;
            if (overrideParams_.trackTargetZ) finalEndTarget.z = normalTarget.z;

            Vector3 finalEndEye = overrideParams_.fixedEyePos;
            if (overrideParams_.trackEyeX) {
                float offsetX = overrideParams_.fixedEyePos.x - overrideParams_.fixedTargetPos.x;
                finalEndEye.x = normalTarget.x + offsetX;
            }
            if (overrideParams_.trackEyeY) {
                float offsetY = overrideParams_.fixedEyePos.y - overrideParams_.fixedTargetPos.y;
                finalEndEye.y = normalTarget.y + offsetY;
            }
            if (overrideParams_.trackEyeZ) {
                float offsetZ = overrideParams_.fixedEyePos.z - overrideParams_.fixedTargetPos.z;
                finalEndEye.z = normalTarget.z + offsetZ;
            }

            if (isOverridden_) {
                eye_ = LerpVec3(overrideStartEye_, finalEndEye, overrideWeight_);
                target_ = LerpVec3(overrideStartTarget_, finalEndTarget, overrideWeight_);
            }
            else {
                eye_ = LerpVec3(normalEye, finalEndEye, overrideWeight_);
                target_ = LerpVec3(normalTarget, finalEndTarget, overrideWeight_);
            }
            SyncRotationToCurrentView();
        }
    }
    else {
        if (isOverridden_) {
            Vector3 finalEndTarget = overrideParams_.fixedTargetPos;
            if (overrideParams_.trackTargetX) finalEndTarget.x = normalTarget.x;
            if (overrideParams_.trackTargetY) finalEndTarget.y = normalTarget.y;
            if (overrideParams_.trackTargetZ) finalEndTarget.z = normalTarget.z;

            Vector3 finalEndEye = overrideParams_.fixedEyePos;
            if (overrideParams_.trackEyeX) {
                float offsetX = overrideParams_.fixedEyePos.x - overrideParams_.fixedTargetPos.x;
                finalEndEye.x = normalTarget.x + offsetX;
            }
            if (overrideParams_.trackEyeY) {
                float offsetY = overrideParams_.fixedEyePos.y - overrideParams_.fixedTargetPos.y;
                finalEndEye.y = normalTarget.y + offsetY;
            }
            if (overrideParams_.trackEyeZ) {
                float offsetZ = overrideParams_.fixedEyePos.z - overrideParams_.fixedTargetPos.z;
                finalEndEye.z = normalTarget.z + offsetZ;
            }

            eye_ = finalEndEye;
            target_ = finalEndTarget;
            overrideWeight_ = 1.0f;
            SyncRotationToCurrentView();
        }
        else {
            overrideWeight_ = 0.0f;
        }
    }

    // =================================================================
    // [3] 最終行列の更新
    // =================================================================
    Vector3 forward = { target_.x - eye_.x, target_.y - eye_.y, target_.z - eye_.z };
    Vector3 currentUp = up_;

    if (std::abs(forward.x) < 0.001f && std::abs(forward.z) < 0.001f) {
        if (forward.y < 0.0f) {
            currentUp = { 0.0f, 0.0f, 1.0f };
        }
        else {
            currentUp = { 0.0f, 0.0f, -1.0f };
        }
    }

    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, currentUp);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    Matrix4x4 vp = math.Multiply(viewMatrix_, projectionMatrix_);
    frustum_ = math.ExtractFrustumPlanes(vp);
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

    Vector3 currentTarget = target_;

    // 現在のカメラ位置(eye_)から、注視点(currentTarget)への方向ベクトル
    Vector3 forward = currentTarget - eye_;
    float dist = math.Length(forward);

    // 距離が近すぎる場合の安全対策
    if (dist < 0.001f) {
        forward = { 0.0f, 0.0f, 1.0f };
    }
    else {
        forward = math.Normalize(forward);
    }

    // 方向ベクトルから、ヨー(Y軸)とピッチ(X軸)の角度を逆算
    rotation_.y = std::atan2(forward.x, forward.z);
    rotation_.x = std::asin(-forward.y);

    // 真上・真下を向きすぎないように制限
    const float pitchLimit = PI / 2.0f - 0.01f;
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));

    // カメラの距離も現在の距離に同期する（一瞬で近づくのを防ぐ）
    aimDistance_ = dist;
}
void Camera::StartOverride(const CameraOverrideParams& params) {
    isOverridden_ = true;
    overrideParams_ = params;
    overrideDuration_ = params.duration;
    overrideStartEye_ = eye_;
    overrideStartTarget_ = target_;

    if (overrideWeight_ <= 0.0f) {
        overrideTimer_ = 0.0f;
    }
    else {
        overrideTimer_ = overrideWeight_ * params.duration;
    }
}

void Camera::EndOverride(float duration) {
    isOverridden_ = false;
    overrideDuration_ = duration;
    overrideStartEye_ = eye_;
    overrideStartTarget_ = target_;

    overrideTimer_ = overrideWeight_ * duration;
}