#define NOMINMAX
#include "Camera.h"
#include "WinApp.h" 
#include "DirectXCommon.h"
#include <algorithm> // std::min, std::max
#include "imgui.h"
#include "ImGuizmo.h"
#include "Object3d.h"
#include "engine/utility/math/AnimationInterpolation.h"
#include <cmath>
#include <utility>
#include <CollisionManager.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 明示的に float にキャスト
const float PI = (float)M_PI;
static Math math;

namespace {
AnimationInterpolation::EasingType ToEasingType(Camera::OverrideEasing easing) {
    switch (easing) {
    case Camera::OverrideEasing::kEaseIn:
        return AnimationInterpolation::EasingType::EaseIn;
    case Camera::OverrideEasing::kEaseOut:
        return AnimationInterpolation::EasingType::EaseOut;
    case Camera::OverrideEasing::kEaseInOut:
        return AnimationInterpolation::EasingType::EaseInOut;
    case Camera::OverrideEasing::kSmootherStep:
        return AnimationInterpolation::EasingType::SmootherStep;
    case Camera::OverrideEasing::kLinear:
    default:
        return AnimationInterpolation::EasingType::Linear;
    }
}

Vector3 GetObjectForward(Object3d* object) {
    if (!object) {
        return { 0.0f, 0.0f, 1.0f };
    }

    Vector3 forward = Math::TransformNormal({ 0.0f, 0.0f, 1.0f }, object->GetWorldMatrix());
    const float length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (length <= 0.0001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { forward.x / length, forward.y / length, forward.z / length };
}
}

void Camera::ConfigFixedPoint(const Vector3& position, const Vector3& angle) {
    fixedPointPos_ = position;
    fixedPointAngle_ = angle;
}

void Camera::UpdateProjectionMatrix() {

    static Math math;

    // 現在のパラメータを使ってプロジェクション行列を再計算
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    RefreshDerivedMatrices();
}

void Camera::RefreshDerivedMatrices() {
    viewProjectionMatrix_ = math.Multiply(viewMatrix_, projectionMatrix_);
    inverseViewProjectionMatrix_ = math.Inverse(viewProjectionMatrix_);
    frustum_ = math.ExtractFrustumPlanes(viewProjectionMatrix_);

    if (constMap_) {
        constMap_->view = viewMatrix_;
        constMap_->projection = projectionMatrix_;
    }
}

void Camera::SetLookAtPreviewView(const Vector3& eye, const Vector3& target, float aspectRatio) {
    Math math;

    Vector3 safeTarget = target;
    Vector3 forward = safeTarget - eye;
    if (math.Length(forward) < 0.0001f) {
        safeTarget = eye + Vector3{ 0.0f, 0.0f, 1.0f };
        forward = safeTarget - eye;
    }

    eye_ = eye;
    target_ = safeTarget;
    up_ = { 0.0f, 1.0f, 0.0f };
    aspectRatio_ = aspectRatio;
    followObject_ = nullptr;
    targetObject_ = nullptr;
    followMode_ = FollowMode::kFixedPoint;
    smoothEye_ = eye_;
    smoothTarget_ = target_;
    fixedPointPos_ = eye_;

    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    RefreshDerivedMatrices();
}

void Camera::ApplyReplayView(
    const Vector3& eye,
    const Vector3& target,
    const Vector3& rotation,
    float fovY,
    float nearClip,
    float farClip) {
    Vector3 safeTarget = target;
    if (math.Length(safeTarget - eye) < 0.0001f) {
        safeTarget = eye + Vector3{ 0.0f, 0.0f, 1.0f };
    }

    eye_ = eye;
    target_ = safeTarget;
    rotation_ = rotation;
    fovY_ = fovY;
    nearClip_ = (std::max)(nearClip, 0.001f);
    farClip_ = (std::max)(farClip, nearClip_ + 0.01f);
    smoothEye_ = eye_;
    smoothTarget_ = target_;
    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    RefreshDerivedMatrices();
}

void Camera::SetFreezeEye(bool freeze) {
    isEyeFrozen_ = freeze;
    if (freeze) {
        smoothEye_ = eye_;
    }
}

void Camera::SetAimCameraSuppressed(bool suppressed) {
    if (isAimCameraSuppressed_ == suppressed) {
        return;
    }

    isAimCameraSuppressed_ = suppressed;
    // 右クリック視点の途中座標を次の状態へ持ち越さず、そのフレームの正しい視点へ戻します。
    isCameraInitialized_ = false;
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

    // 定数バッファ作成
    constBuffer_ = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(CameraVP));
    constBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constMap_));

    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    RefreshDerivedMatrices();
}


void Camera::Update(float deltaTime) {
    const float frameDelta = std::clamp(deltaTime, 0.0f, 0.1f);
    auto LerpVec3 = [](const Vector3& a, const Vector3& b, float t) {
        return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
        };

    // =================================================================
    // [1] 通常カメラの処理（プレイヤー追従および各モードの座標計算）
    // =================================================================
    if (followObject_) {
        Vector3 playerPos = followObject_->GetWorldPosition();

        // -----------------------------------------------------------------
        // (A) 注視点 (TargetPos) の基本計算
        // -----------------------------------------------------------------
        Vector3 targetPos = playerPos;
        if (followMode_ == FollowMode::kOrbit) {
            targetPos.x += orbitCenterOffset_.x;
            targetPos.y += orbitCenterHeight_ + orbitCenterOffset_.y;
            targetPos.z += orbitCenterOffset_.z;
        } else {
            targetPos.y += aimHeight_;
        }

        // -----------------------------------------------------------------
        // (B) 目標座標 (Desired Eye) の計算
        // -----------------------------------------------------------------
        Vector3 desiredEye = eye_;
        float toRad = PI / 180.0f;

        switch (followMode_) {
        case FollowMode::kLockOn:
        {
            if (targetObject_) {
                // ロックオン時の注視点計算
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
            Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
            offset = math.TransformNormal(offset, rotateMat);
            desiredEye = targetPos + offset;
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
            targetPos = desiredEye + forwardFP;
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

            targetPos = desiredEye + forward * 10.0f;
            break;
        }
        }

        // -----------------------------------------------------------------
        // 右クリック（エイム・フック構え）時の1人称オーバーライド
        // -----------------------------------------------------------------
        bool isAiming = (inputManager_ && inputManager_->IsMouseButtonPressed(1) && !isAimCameraSuppressed_);
        if (isAiming) {
            // カメラの目標位置をプレイヤーの頭の位置に設定
            desiredEye = playerPos + Vector3{ 0.0f, aimHeight_, 0.0f };
            Matrix4x4 rotateMatFP = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
            Vector3 forwardFP = math.TransformNormal({ 0, 0, 1 }, rotateMatFP);
            targetPos = desiredEye + forwardFP * 10.0f;
        }

        // -----------------------------------------------------------------
        // (C) 初期化と補間の実行 【ズームイン・アウトの滑らか対応版】
        // -----------------------------------------------------------------
        if (!isCameraInitialized_) {
            smoothTarget_ = targetPos;
            smoothEye_ = desiredEye;
            target_ = smoothTarget_;
            eye_ = smoothEye_;
            isCameraInitialized_ = true;
        }
        else {
            // 基本の補間スピード
            float targetLerpFactor = (followMode_ == FollowMode::kLockOn) ? 1.0f : 0.2f;
            float eyeLerpFactor = (followMode_ == FollowMode::kLockOn) ? 1.0f : 0.15f;

            // ★エイム中は強制固定（ワープ）を廃止し、滑らかにズームする係数に変更
            if (isAiming) {
                targetLerpFactor = 0.4f; // 視線の向きは早めに追従させてエイムしやすくする
                eyeLerpFactor = 0.2f;    // カメラ位置は少し時間をかけて頭に引き寄せる
            }

            smoothTarget_ = LerpVec3(smoothTarget_, targetPos, targetLerpFactor);
            target_ = smoothTarget_;

            smoothEye_ = LerpVec3(smoothEye_, desiredEye, eyeLerpFactor);
            if (isEyeFrozen_) {
                smoothEye_ = eye_;
            }
            eye_ = smoothEye_;
        }

        // -----------------------------------------------------------------
        // (D) 障害物判定 (Collision)
        // -----------------------------------------------------------------
        if (!isEyeFrozen_) {
            // エイム中などはコリジョンを通さず、引き寄せられている視点位置をダイレクトに適用
            if (followMode_ != FollowMode::kFirstPerson && followMode_ != FollowMode::kFixedPoint && !isAiming) {
                Vector3 rayStartPos = playerPos;
                rayStartPos.y += aimHeight_;

                Vector3 toSmoothEye = smoothEye_ - rayStartPos;
                float dist = math.Length(toSmoothEye);
                Vector3 direction = (dist > 0.001f) ? math.Normalize(toSmoothEye) : Vector3{ 0,0,1 };

                Vector3 targetEye = smoothEye_;
                float finalDist = dist;

                // ① 壁などのオブジェクトとの判定
                if (dist > 0.1f) {
                    RaycastHit hit = CollisionManager::GetInstance()->Raycast(
                        rayStartPos, direction, dist, kGround
                    );

                    if (hit.isHit) {
                        const float kEpsilon = 0.2f;
                        finalDist = std::max(0.1f, hit.distance - kEpsilon);
                        targetEye = rayStartPos + (direction * finalDist);
                    }
                }

                // ② 地面へのめり込み防止
                float groundLimitY = playerPos.y + 0.5f;
                if (targetEye.y < groundLimitY && direction.y < -0.001f) {
                    float distToGround = (groundLimitY - rayStartPos.y) / direction.y;
                    if (distToGround > 0.1f && distToGround < finalDist) {
                        targetEye = rayStartPos + (direction * distToGround);
                    }
                }

                eye_ = targetEye;
            }
            else {
                eye_ = smoothEye_; // desiredEye ではなく滑らかに移動中の smoothEye_ を適用！
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
    else if (followMode_ == FollowMode::kFixedPoint) {
        Vector3 desiredEye = fixedPointPos_;
        float pitch = fixedPointAngle_.x;
        float yaw = fixedPointAngle_.y;
        Vector3 forward;
        forward.x = std::sin(yaw) * std::cos(pitch);
        forward.y = -std::sin(pitch);
        forward.z = std::cos(yaw) * std::cos(pitch);

        eye_ = desiredEye;
        target_ = desiredEye + forward * 10.0f;
        smoothEye_ = eye_;
        smoothTarget_ = target_;
        isCameraInitialized_ = true;
    }
    else if (followMode_ == FollowMode::kOrbit) {
        Vector3 orbitTarget = fixedPointPos_;
        orbitAngle_ += orbitSpeed_;

        Vector3 desiredEye;
        desiredEye.x = orbitTarget.x + orbitRadius_ * std::cos(orbitAngle_);
        desiredEye.z = orbitTarget.z + orbitRadius_ * std::sin(orbitAngle_);
        desiredEye.y = orbitTarget.y + orbitHeight_;

        if (!isCameraInitialized_) {
            smoothTarget_ = orbitTarget;
            smoothEye_ = desiredEye;
            target_ = smoothTarget_;
            eye_ = smoothEye_;
            isCameraInitialized_ = true;
        }
        else {
            smoothTarget_ = LerpVec3(smoothTarget_, orbitTarget, 0.2f);
            target_ = smoothTarget_;

            smoothEye_ = LerpVec3(smoothEye_, desiredEye, 0.15f);
            if (isEyeFrozen_) {
                smoothEye_ = eye_;
            }
            eye_ = smoothEye_;
        }
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

    auto resolveOverridePose = [&]() {
        Vector3 desiredEye = overrideParams_.fixedEyePos;
        if (overrideParams_.eyeSource == OverrideEyeSource::kSceneObject && overrideParams_.eyeObject) {
            desiredEye = overrideParams_.eyeObject->GetWorldPosition() + overrideParams_.eyeObjectOffset;
        }
        else {
            if (overrideParams_.trackEyeX) {
                desiredEye.x = normalTarget.x + (overrideParams_.fixedEyePos.x - overrideParams_.fixedTargetPos.x);
            }
            if (overrideParams_.trackEyeY) {
                desiredEye.y = normalTarget.y + (overrideParams_.fixedEyePos.y - overrideParams_.fixedTargetPos.y);
            }
            if (overrideParams_.trackEyeZ) {
                desiredEye.z = normalTarget.z + (overrideParams_.fixedEyePos.z - overrideParams_.fixedTargetPos.z);
            }
        }

        Vector3 desiredTarget = overrideParams_.fixedTargetPos;
        if (overrideParams_.targetSource == OverrideTargetSource::kSceneObject && overrideParams_.targetFollowObject) {
            desiredTarget = overrideParams_.targetFollowObject->GetWorldPosition() + overrideParams_.targetObjectOffset;
        }
        else if (overrideParams_.targetSource == OverrideTargetSource::kEyeObjectForward && overrideParams_.eyeObject) {
            desiredTarget = desiredEye
                + GetObjectForward(overrideParams_.eyeObject) * (std::max)(overrideParams_.eyeForwardDistance, 0.01f)
                + overrideParams_.targetObjectOffset;
        }
        else {
            if (overrideParams_.trackTargetX) desiredTarget.x = normalTarget.x;
            if (overrideParams_.trackTargetY) desiredTarget.y = normalTarget.y;
            if (overrideParams_.trackTargetZ) desiredTarget.z = normalTarget.z;
        }

        if (!overrideFollowInitialized_) {
            overrideFollowEye_ = desiredEye;
            overrideFollowTarget_ = desiredTarget;
            overrideFollowInitialized_ = true;
        }

        if (overrideParams_.eyeFollowMode == OverrideFollowMode::kSmooth) {
            overrideFollowEye_ = AnimationInterpolation::Damp(
                overrideFollowEye_, desiredEye, (std::max)(overrideParams_.eyeFollowResponse, 0.01f), frameDelta);
        }
        else {
            overrideFollowEye_ = desiredEye;
        }

        if (overrideParams_.targetFollowMode == OverrideFollowMode::kSmooth) {
            overrideFollowTarget_ = AnimationInterpolation::Damp(
                overrideFollowTarget_, desiredTarget, (std::max)(overrideParams_.targetFollowResponse, 0.01f), frameDelta);
        }
        else {
            overrideFollowTarget_ = desiredTarget;
        }

        return std::pair<Vector3, Vector3>{ overrideFollowEye_, overrideFollowTarget_ };
    };

    if (overrideDuration_ > 0.0f) {
        if (isOverridden_) {
            overrideTimer_ += frameDelta;
        }
        else {
            overrideTimer_ -= frameDelta;
        }

        overrideTimer_ = std::max(0.0f, std::min(overrideTimer_, overrideDuration_));
        float t = overrideTimer_ / overrideDuration_;
        overrideWeight_ = AnimationInterpolation::ApplyEasing(t, ToEasingType(overrideParams_.easing));

        if (overrideWeight_ > 0.0f) {
            const auto [finalEndEye, finalEndTarget] = resolveOverridePose();

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
            const auto [finalEndEye, finalEndTarget] = resolveOverridePose();

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
    Vector3 viewEye = eye_;
    Vector3 viewTarget = target_;
    Vector3 mixedShakeOffset = { 0.0f, 0.0f, 0.0f };
    for (ShakeLayer& layer : shakeLayers_) {
        const float progress = std::clamp(layer.elapsed / layer.duration, 0.0f, 1.0f);
        const float envelope = (1.0f - progress) * (1.0f - progress);
        const float phase = layer.elapsed * layer.frequency * 2.0f * PI;
        const float amplitude = layer.amplitude * envelope;
        mixedShakeOffset.x += std::sin(phase * 1.13f) * amplitude * layer.axisWeight.x;
        mixedShakeOffset.y += std::cos(phase * 1.37f) * amplitude * layer.axisWeight.y;
        mixedShakeOffset.z += std::sin(phase * 0.79f + 0.7f) * amplitude * layer.axisWeight.z;
        layer.elapsed += frameDelta;
    }
    shakeLayers_.erase(
        std::remove_if(shakeLayers_.begin(), shakeLayers_.end(), [](const ShakeLayer& layer) {
            return layer.elapsed >= layer.duration;
        }),
        shakeLayers_.end());
    viewEye = viewEye + mixedShakeOffset;
    viewTarget = viewTarget + mixedShakeOffset * 0.25f;

    float mixedFovOffset = 0.0f;
    for (FovPulseLayer& layer : fovPulseLayers_) {
        const float progress = std::clamp(layer.elapsed / layer.duration, 0.0f, 1.0f);
        float envelope = 0.0f;
        if (progress < layer.attackRatio) {
            const float attack = progress / layer.attackRatio;
            envelope = attack * attack * (3.0f - 2.0f * attack);
        } else {
            const float release = (progress - layer.attackRatio) / (1.0f - layer.attackRatio);
            const float inverse = 1.0f - release;
            envelope = inverse * inverse;
        }
        mixedFovOffset += layer.amountRadians * envelope;
        layer.elapsed += frameDelta;
    }
    fovPulseLayers_.erase(
        std::remove_if(fovPulseLayers_.begin(), fovPulseLayers_.end(), [](const FovPulseLayer& layer) {
            return layer.elapsed >= layer.duration;
        }),
        fovPulseLayers_.end());

    Vector3 forward = { viewTarget.x - viewEye.x, viewTarget.y - viewEye.y, viewTarget.z - viewEye.z };
    Vector3 currentUp = up_;

    if (std::abs(forward.x) < 0.001f && std::abs(forward.z) < 0.001f) {
        if (forward.y < 0.0f) {
            currentUp = { 0.0f, 0.0f, 1.0f };
        }
        else {
            currentUp = { 0.0f, 0.0f, -1.0f };
        }
    }

    viewMatrix_ = math.MakeLookAtMatrix(viewEye, viewTarget, currentUp);
    const float renderFov = std::clamp(fovY_ + mixedFovOffset, 5.0f * PI / 180.0f, 170.0f * PI / 180.0f);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(renderFov, aspectRatio_, nearClip_, farClip_);
    RefreshDerivedMatrices();
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

void Camera::SnapToThirdPerson(float distance, float height, float pitch) {
    if (!followObject_) return;

    followMode_ = FollowMode::kAimable;
    aimDistance_ = distance;
    aimHeight_ = height;
    rotation_.x = pitch;

    Vector3 playerPos = followObject_->GetWorldPosition();
    Vector3 targetPos = playerPos;
    targetPos.y += aimHeight_;

    Matrix4x4 rotateMat = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
    Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
    offset = math.TransformNormal(offset, rotateMat);

    smoothTarget_ = targetPos;
    smoothEye_ = targetPos + offset;
    target_ = smoothTarget_;
    eye_ = smoothEye_;
    isEyeFrozen_ = false;
}

void Camera::AddRotation(const Vector2& mouseDelta) {
    // kAimable と kFirstPerson で共用の回転処理
    const float rotateSpeed = 0.005f * rotationSensitivity_;
    rotation_.x += mouseDelta.y * rotateSpeed;
    rotation_.y += mouseDelta.x * rotateSpeed;

    // X軸の回転（ピッチ）に制限をかける
    const float pitchLimit = PI / 2.0f - 0.01f; // 90度手前
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));

    // Y軸の回転（ヨー）は 2PI でラップアラウンド
    if (rotation_.y > PI) { rotation_.y -= 2.0f * PI; }
    if (rotation_.y < -PI) { rotation_.y += 2.0f * PI; }
}

void Camera::SetRotationSensitivity(float sensitivity) {
    rotationSensitivity_ = std::clamp(sensitivity, 0.5f, 2.0f);
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
void Camera::StartShake(float duration, float amplitude, float frequency, const Vector3& axisWeight) {
    ShakeLayer layer;
    layer.duration = std::max(duration, 0.01f);
    layer.amplitude = std::max(amplitude, 0.0f);
    layer.frequency = std::max(frequency, 1.0f);
    layer.axisWeight = axisWeight;
    shakeLayers_.push_back(layer);
}

void Camera::StartFovPulse(float duration, float amountRadians, float attackRatio) {
    FovPulseLayer layer;
    layer.duration = std::max(duration, 0.01f);
    layer.amountRadians = amountRadians;
    layer.attackRatio = std::clamp(attackRatio, 0.01f, 0.95f);
    fovPulseLayers_.push_back(layer);
}

void Camera::ClearPresentationLayers() {
    shakeLayers_.clear();
    fovPulseLayers_.clear();
}

void Camera::StartOverride(const CameraOverrideParams& params) {
    isOverridden_ = true;
    overrideParams_ = params;
    overrideDuration_ = (std::max)(params.duration, 0.0f);
    overrideStartEye_ = eye_;
    overrideStartTarget_ = target_;
    overrideFollowInitialized_ = false;

    if (overrideWeight_ <= 0.0f) {
        overrideTimer_ = 0.0f;
    }
    else {
        overrideTimer_ = overrideWeight_ * overrideDuration_;
    }
}

void Camera::EndOverride(float duration) {
    isOverridden_ = false;
    overrideDuration_ = (std::max)(duration, 0.0f);
    overrideStartEye_ = eye_;
    overrideStartTarget_ = target_;

    // 終了中に追従Objectが元の場所へ戻っても画面が引っ張られないよう、最後の構図を固定します。
    overrideParams_.eyeSource = OverrideEyeSource::kFixed;
    overrideParams_.targetSource = OverrideTargetSource::kFixed;
    overrideParams_.eyeObject = nullptr;
    overrideParams_.targetFollowObject = nullptr;
    overrideParams_.fixedEyePos = eye_;
    overrideParams_.fixedTargetPos = target_;
    overrideParams_.trackEyeX = false;
    overrideParams_.trackEyeY = false;
    overrideParams_.trackEyeZ = false;
    overrideParams_.trackTargetX = false;
    overrideParams_.trackTargetY = false;
    overrideParams_.trackTargetZ = false;
    overrideFollowInitialized_ = false;

    overrideTimer_ = overrideWeight_ * overrideDuration_;
}
