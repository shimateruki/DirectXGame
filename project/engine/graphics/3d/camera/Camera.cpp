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

const float PI = static_cast<float>(M_PI);
const float kCameraPitchLimitMin = -1.20f;
const float kCameraPitchLimitMax = 1.20f;
static Math math;

namespace {
bool IsIgnoredForCameraCollision(Object3d* object) {
    for (Object3d* current = object; current; current = current->GetParent()) {
        const std::string className = current->GetClassName();
        const std::string enemyType = current->GetEnemyType();
        const std::string name = current->GetName();

        if (className == "Enemy" || className == "BossCore") {
            return true;
        }
        if (!enemyType.empty()) {
            return true;
        }
        if (name.find("Battle_Field_Collision_Box_") != std::string::npos) {
            return true;
        }
        if (name.find("Armor") != std::string::npos ||
            name.find("armor") != std::string::npos ||
            name.find("Boss") != std::string::npos) {
            return true;
        }
    }

    return false;
}

void ApplyAlphaToHierarchy(Object3d* object, float alpha) {
    if (!object) {
        return;
    }

    Vector4 color = object->GetColor();
    object->SetColor({ color.x, color.y, color.z, alpha });

    for (Object3d* child : object->GetChildren()) {
        ApplyAlphaToHierarchy(child, alpha);
    }
}
}

void Camera::ConfigFixedPoint(const Vector3& position, const Vector3& angle) {
    fixedPointPos_ = position;
    fixedPointAngle_ = angle; 
}

void Camera::UpdateProjectionMatrix() {
    // 現在のパラメータを使ってプロジェクション行列を再計算
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
}

void Camera::Initialize() {
    // デフォルトの視点、注視点、上方向を設定
    eye_ = { 0.0f, 5.0f, -20.0f };
    target_ = { 0.0f, 0.0f, 0.0f };
    up_ = { 0.0f, 1.0f, 0.0f };
    rotation_ = { 0.0f, 0.0f, 0.0f };

    // デフォルトのオフセットを初期位置から反映
    fixedOffset_ = eye_;

    // アスペクト比をウィンドウサイズから計算
    aspectRatio_ = static_cast<float>(WinApp::kClientWidth) / WinApp::kClientHeight;

    // デフォルトのカメラモード設定
    followMode_ = FollowMode::kAimable;
    distance_ = 10.0f;
    isInputEnabled_ = true;
}

void Camera::Update() {
    auto LerpVec3 = [](const Vector3& a, const Vector3& b, float t) {
        return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    };

    // =================================================================
    // [1] 通常カメラの処理（プレイヤー追従および各モードの座標計算）
    // =================================================================
    if (followObject_) {
        Vector3 playerPos = followObject_->GetWorldPosition();
        Vector3 aimableOffset = { 0.0f, 0.0f, 0.0f };

        // -----------------------------------------------------------------
        // (A) 注視点 (TargetPos) の基本計算
        // -----------------------------------------------------------------
        Vector3 targetPos = playerPos;
        targetPos.y += aimHeight_; // デフォルトはプレイヤーの頭上

        // -----------------------------------------------------------------
        // (B) 目標座標 (Desired Eye) の計算
        // -----------------------------------------------------------------
        Vector3 desiredEye = eye_;
        float toRad = PI / 180.0f;

        switch (followMode_) {
        case FollowMode::kLockOn:
        {
            if (targetObject_) {
                // ロックオン時の注視点計算（ここで targetPos を更新）
                Vector3 enemyPos = targetObject_->GetWorldPosition();
                Vector3 playerFocus = playerPos;
                playerFocus.y += aimHeight_ * 0.5f;

                targetPos.x = playerFocus.x + (enemyPos.x - playerFocus.x) * 0.7f;
                targetPos.z = playerFocus.z + (enemyPos.z - playerFocus.z) * 0.7f;

                float distXZ_L = std::sqrt((enemyPos.x - playerFocus.x) * (enemyPos.x - playerFocus.x) +
                    (enemyPos.z - playerFocus.z) * (enemyPos.z - playerFocus.z));
                float maxY = playerFocus.y + std::min(3.0f, distXZ_L * 0.4f);
                float idealY = playerFocus.y + (enemyPos.y - playerFocus.y) * 0.5f;
                targetPos.y = std::min(idealY, maxY);

                Vector3 toEnemy = enemyPos - playerPos;

                float distanceXZ = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z);
                float heightDiff = std::max(0.0f, enemyPos.y - playerPos.y);
                float heightZoom = heightDiff * 0.8f;

                // 至近距離でのカメラ引き
                float closeZoomOut = 0.0f;
                if (distanceXZ < 8.0f) {
                    closeZoomOut = (8.0f - distanceXZ) * 0.7f;
                }

                // 遠距離での追従ズーム
                float zoom = std::max(0.0f, distanceXZ - 10.0f) * 0.5f;
                zoom = std::min(zoom, 25.0f);

                // 基本オフセットの適用
                Vector3 dynamicOffset = { 2.0f, 2.0f + (closeZoomOut * 0.3f), -6.5f - closeZoomOut - heightZoom };
                dynamicOffset.z -= zoom;
                dynamicOffset.y += zoom * 0.15f;

                // プレイヤーを中心とした回転（ヨー）の補間
                float angleToEnemy = std::atan2(toEnemy.x, toEnemy.z);
                auto NormalizeAngle = [](float a) {
                    while (a > PI) a -= 2.0f * PI;
                    while (a < -PI) a += 2.0f * PI;
                    return a;
                };

                float diff = NormalizeAngle(angleToEnemy - rotation_.y);
                rotation_.y += diff * 0.08f;

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
            aimableOffset = { 0.0f, 0.0f, -aimDistance_ };
            aimableOffset = math.TransformNormal(aimableOffset, rotateMat);
            desiredEye = targetPos + aimableOffset;
            break;
        }
        case FollowMode::kFixed:
        {
            float currentY = rotation_.y;
            float pitch = aimAngle_.x * toRad;
            Matrix4x4 rotateMat = math.MakeRotateXMatrix(pitch) * math.MakeRotateYMatrix(currentY);
            Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
            offset = math.TransformNormal(offset, rotateMat);
            desiredEye = targetPos + offset;
            rotation_.x = pitch;
            break;
        }
        case FollowMode::kFirstPerson:
        {
            desiredEye = playerPos + firstPersonOffset_;
            Matrix4x4 rotateMatFP = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
            Vector3 forwardFP = math.TransformNormal({ 0, 0, 1 }, rotateMatFP);
            targetPos = desiredEye + forwardFP; // 一人称は回転方向が注視点
            break;
        }
        case FollowMode::kOrbit:
        {
            orbitAngle_ += orbitSpeed_;
            desiredEye.x = targetPos.x + orbitRadius_ * std::cos(orbitAngle_);
            desiredEye.z = targetPos.z + orbitRadius_ * std::sin(orbitAngle_);
            desiredEye.y = targetPos.y + orbitHeight_;
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

            targetPos = desiredEye + forward * 10.0f; // 定点カメラも回転方向が注視点
            break;
        }
        }

        // -----------------------------------------------------------------
        // (C) 初期化と補間の実行
        // -----------------------------------------------------------------

        // シーン開始時の最初の1フレームは補間せずに即座に配置する
        if (!isCameraInitialized_) {
            smoothTarget_ = targetPos;
            smoothEye_ = desiredEye;
            target_ = smoothTarget_;
            eye_ = smoothEye_;
            isCameraInitialized_ = true;
        }

        // 注視点の補間更新（ロックオン中は追従を優先するため係数を 1.0 にする）
        float targetLerpFactor = (followMode_ == FollowMode::kLockOn) ? 1.0f : 0.2f;
        smoothTarget_ = LerpVec3(smoothTarget_, targetPos, targetLerpFactor);
        target_ = smoothTarget_;

        // カメラ位置の補間更新（ロックオン解除時にスムーズに戻すための smoothEye_ を活用）
        if (followMode_ == FollowMode::kAimable) {
            // エイム（自由回転）モードでは、補間された注視点（target_）に回転オフセットを直接加算する
            // これにより、回転操作時の一時的なショートカット（プレイヤー直上の通過）がなくなりジンバルロックを防ぎます。
            // また、回転操作時のタイムラグ（重い慣性）がなくなり、3D酔いを完全に解消します。
            smoothEye_ = target_ + aimableOffset;
            eye_ = smoothEye_;
        }
        else {
            float eyeLerpFactor = (followMode_ == FollowMode::kLockOn) ? 1.0f : 0.15f;
            smoothEye_ = LerpVec3(smoothEye_, desiredEye, eyeLerpFactor);
            eye_ = smoothEye_;
        }

        // -----------------------------------------------------------------
        // (D) 障害物判定 (Collision)
        // -----------------------------------------------------------------
        if (!isEyeFrozen_) {
            if (followMode_ != FollowMode::kFirstPerson && followMode_ != FollowMode::kFixedPoint) {
                // レイの開始点をプレイヤーの頭の高さに設定
                // (注視点そのものを使うとロックオン中に敵の内側から判定が始まってしまうため)
                Vector3 rayStartPos = playerPos;
                rayStartPos.y += aimHeight_;

                Vector3 toSmoothEye = smoothEye_ - rayStartPos;
                float dist = math.Length(toSmoothEye);
                Vector3 direction = (dist > 0.001f) ? math.Normalize(toSmoothEye) : Vector3{ 0,0,1 };

                if (dist > 0.1f) {
                    RaycastHit hit = CollisionManager::GetInstance()->RaycastFiltered(
                        rayStartPos, direction, dist, kGround | kMapBlock, IsIgnoredForCameraCollision
                    );

                    if (hit.isHit) {
                        const float kEpsilon = 0.2f;
                        eye_ = hit.hitPoint - (direction * kEpsilon);
                    }
                    else {
                        eye_ = smoothEye_;
                    }
                }
                else {
                    eye_ = smoothEye_;
                }

                // 地面へのめり込み防止
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
        // (E) プレイヤー透過処理（近距離フェード）
        // -----------------------------------------------------------------
        Vector3 playerPosForDist = followObject_->GetWorldPosition();
        playerPosForDist.y += aimHeight_ * 0.5f;

        Vector3 toPlayer = playerPosForDist - eye_;
        float camToPlayerDist = math.Length(toPlayer);

        float alpha = 1.0f;
        const bool isCinematicCameraActive = isOverridden_ || overrideWeight_ > 0.0f;
        if (!isCinematicCameraActive && camToPlayerDist < 3.5f) {
            alpha = std::max(0.0f, (camToPlayerDist - 1.0f) / 2.5f);
        }

        ApplyAlphaToHierarchy(followObject_, alpha);
    }

    // =================================================================
    // [2] シネマティックカメラ (Override) の補間処理
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
    // [3] 行列とプロジェクションの更新
    // =================================================================
    Vector3 forward = { target_.x - eye_.x, target_.y - eye_.y, target_.z - eye_.z };
    Vector3 currentUp = up_;

    // 通常のジンバルロック回避（しきい値を広げてより安全に）
    if (std::abs(forward.x) < 0.05f && std::abs(forward.z) < 0.05f) {
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
    float toRad = PI / 180.0f;
    rotation_.x = std::max(kCameraPitchLimitMin, std::min(kCameraPitchLimitMax, angle.x * toRad));
    rotation_.y = angle.y * toRad;
    rotation_.z = angle.z * toRad;
}

void Camera::ConfigFirstPerson(const Vector3& eyeOffset) {
    firstPersonOffset_ = eyeOffset;
}

void Camera::AddRotation(const Vector2& mouseDelta) {
    const float rotateSpeed = 0.005f;
    // 感度倍率を適用
    rotation_.x += mouseDelta.y * rotateSpeed * sensitivityMultiplier_;
    rotation_.y += mouseDelta.x * rotateSpeed * sensitivityMultiplier_;

    // X軸（ピッチ）の回転制限（真上・真下付近でのジンバルロックや画面反転を防ぐため、安全な範囲に制限）
    rotation_.x = std::max(kCameraPitchLimitMin, std::min(kCameraPitchLimitMax, rotation_.x));

    // Y軸（ヨー）のラップアラウンド
    if (rotation_.y > PI) { rotation_.y -= 2.0f * PI; }
    if (rotation_.y < -PI) { rotation_.y += 2.0f * PI; }
}

void Camera::SetSensitivity(int level) {
    // level: -5 ~ 5
    // -5 のときは 0.2倍, 0 のときは 1.0倍, 5 のときは 3.0倍 程度にして変化を分かりやすくする
    if (level < 0) {
        // -5 -> 0.2, 0 -> 1.0
        sensitivityMultiplier_ = 1.0f + (level * 0.16f); // -5 * 0.16 = -0.8 -> 0.2
    } else {
        // 0 -> 1.0, 5 -> 3.0
        sensitivityMultiplier_ = 1.0f + (level * 0.4f); // 5 * 0.4 = 2.0 -> 3.0
    }
}

void Camera::SyncRotationToCurrentView() {
    // 理想的なカメラ位置(smoothEye_)から注視点への方向ベクトルを算出
    // 壁に押し付けられた位置(eye_)を使うと角度が急激に変化するため、理想位置を参照する
    Vector3 forward = target_ - smoothEye_;
    float dist = math.Length(forward);

    if (dist < 0.001f) {
        forward = { 0.0f, 0.0f, 1.0f };
    }
    else {
        forward = math.Normalize(forward);
    }

    // 方向ベクトルから現在の角度（ヨー・ピッチ）を逆算して同期
    rotation_.y = std::atan2(forward.x, forward.z);
    rotation_.x = std::asin(-forward.y);

    rotation_.x = std::max(kCameraPitchLimitMin, std::min(kCameraPitchLimitMax, rotation_.x));

    smoothTarget_ = target_;
    smoothEye_ = eye_;
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
