#include <unordered_set>
#define NOMINMAX
#include "Camera.h"
#include "Easing.h"
#include <numbers>
#include "game/system/settings/GameSettingsManager.h"
#include "WinApp.h" 
#include "DirectXCommon.h"
#include <algorithm> // std::min, std::max
#include "imgui.h"
#include "ImGuizmo.h"
#include "Object3D.h"
#include "engine/utility/math/AnimationInterpolation.h"
#include <cmath>
#include <utility>
#include <CollisionManager.h>
#include "SceneManager.h"


// 明示的に float にキャスト

static engine::Math math;

namespace {
AnimationInterpolation::EasingType ToEasingType(Camera::OverrideEasing easing) {
    // カメラ専用の設定値を共通の補間種別へ写す。
    switch (easing) {
    case Camera::OverrideEasing::EaseIn:
        return AnimationInterpolation::EasingType::EaseIn;

    case Camera::OverrideEasing::EaseOut:
        return AnimationInterpolation::EasingType::EaseOut;

    case Camera::OverrideEasing::EaseInOut:
        return AnimationInterpolation::EasingType::EaseInOut;

    case Camera::OverrideEasing::SmootherStep:
        return AnimationInterpolation::EasingType::SmootherStep;

    case Camera::OverrideEasing::Linear:
    default:
        return AnimationInterpolation::EasingType::Linear;
    }
}

engine::Vector3 GetObjectForward(engine::Object3D* object) {
    // ワールド変換から前方向を取り出し、縮退した場合は基準方向へ戻す。
    if (!object) {
        return { 0.0f, 0.0f, 1.0f };
    }

    engine::Vector3 forward = engine::Math::TransformNormal({ 0.0f, 0.0f, 1.0f }, object->GetWorldMatrix());
    const float length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);

    if (length <= 0.0001f) {
        return { 0.0f, 0.0f, 1.0f };
    }

    return { forward.x / length, forward.y / length, forward.z / length };
}

engine::Vector3 GetCurrentObjectPosition(const engine::Object3D* object) {
    if (!object) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return object->GetParent() ? object->GetWorldPosition() : object->GetTranslate();
}
// 球を細かく進める前に移動範囲の候補を一度だけ絞る。装飾の多いステージでも
// 各サンプルで全地形の形状計算と文字列判定を繰り返さない。
RaycastHit CameraSphereCast(const engine::Vector3& start, float radius,
    const engine::Vector3& direction, float distance, PhysicsQueryFilter filter) {
    const auto end = start + direction * distance;
    const engine::Vector3 low{(std::min)(start.x, end.x) - radius,
        (std::min)(start.y, end.y) - radius, (std::min)(start.z, end.z) - radius};
    const engine::Vector3 high{(std::max)(start.x, end.x) + radius,
        (std::max)(start.y, end.y) + radius, (std::max)(start.z, end.z) + radius};
    std::unordered_set<const engine::Object3D*> candidates;
    auto* collisions = CollisionManager::GetInstance();
    for (const auto* object : collisions->GetObjects()) {
        if (!object || object == filter.ignoredObject || !object->GetCollider() ||
            !(object->GetCollisionAttribute() & filter.mask) ||
            (filter.acceptsObject && !filter.acceptsObject(object))) continue;
        const auto box = object->GetAabb();
        if (box.max.x >= low.x && box.min.x <= high.x && box.max.y >= low.y &&
            box.min.y <= high.y && box.max.z >= low.z && box.min.z <= high.z)
            candidates.insert(object);
    }
    if (candidates.empty()) { RaycastHit hit; hit.distance = distance; return hit; }
    filter.acceptsObject = [&candidates](const engine::Object3D* object) { return candidates.contains(object); };
    return collisions->SphereCast(start, radius, direction, distance, filter);
}

PhysicsQueryFilter MakeCameraObstacleFilter(engine::Object3D* subject) {
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("ground");
    filter.ignoredObject = subject;
    filter.includeTriggers = true;
    const auto* sceneManager = SceneManager::GetInstance();
    // エディターで別の実行クラスからstageSelectを開いた場合も同じ条件にする。
    const bool isStageSelect = sceneManager->GetCurrentSceneName() == "SELECT" ||
        sceneManager->GetActiveSceneLoadContext().sceneAssetId == "stageSelect";
    filter.acceptsObject = [isStageSelect, subject](const engine::Object3D* object) {
        // 従来の無効マスクとプレイヤー階層の除外条件も維持する。
        if (object->GetCollisionMask() == 0)
            return false;
        for (const auto* current = object; current; current = current->GetParent()) {
            if (current == subject || current->GetClassName() == "Player" ||
                current->GetName().find("Player") != std::string::npos)
                return false;
        }
        // 通行止め用の障壁はカメラだけ通し、プレイヤーや弾の判定を変えない。
        if (object->GetGimmickType() == "PrismBarrier")
            return false;
        return !(isStageSelect && object->GetClassName() == "InvisibleBox");
    };
    return filter;
}

}

float Camera::RecoverCollisionDistance(float previous, float safeDistance, float frameDeltaTime, float seconds) {
    if (previous < 0.0f || safeDistance <= previous) return safeDistance;
    const float alpha = 1.0f - std::exp(-std::clamp(frameDeltaTime, 0.0f, 0.1f) / (std::max)(seconds, 0.01f));
    return previous + (safeDistance - previous) * alpha;
}

void Camera::ConfigFixedPoint(const engine::Vector3& position, const engine::Vector3& angle) {
    fixedPointPos_ = position;
    fixedPointAngle_ = angle;
}

void Camera::SetFovY(float fov) {
    fovY_ = fov;
    fovBlendActive_ = false;
}

void Camera::BlendFovY(float targetFov, float duration, OverrideEasing easing) {
    // 進行中の補間を含む現在値からつなぎ、Camera Objectの切替時に画角が跳ねないようにする。
    if (duration <= 0.0f) {
        SetFovY(targetFov);
        return;
    }
    fovBlendStart_ = regionProjectionActive_ ? renderedProjection_.x : fovY_;
    fovBlendTarget_ = targetFov;
    fovBlendElapsed_ = 0.0f;
    fovBlendDuration_ = duration;
    fovBlendEasing_ = easing;
    fovBlendActive_ = true;
}

engine::Vector3 Camera::ResolveRelativeEye(const engine::Object3D& origin, const engine::Object3D& reference,
                                           const engine::Vector3& offset) {
    const engine::Vector3 base = GetCurrentObjectPosition(&origin);
    const engine::Vector3 toward = GetCurrentObjectPosition(&reference) - base;
    const float horizontal = std::sqrt(toward.x * toward.x + toward.z * toward.z);
    const engine::Vector3 forward = horizontal > 0.0001f
        ? engine::Vector3{ toward.x / horizontal, 0.0f, toward.z / horizontal }
        : engine::Vector3{ 0.0f, 0.0f, 1.0f };
    const engine::Vector3 right = { forward.z, 0.0f, -forward.x };
    return base + right * offset.x + engine::Vector3{ 0.0f, offset.y, 0.0f } + forward * offset.z;
}

void Camera::UpdateProjectionMatrix() {

    static engine::Math math;

    // 現在のパラメータを使ってプロジェクション行列を再計算
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    RefreshDerivedMatrices();
}

void Camera::RefreshDerivedMatrices() {
    // 可視判定と逆投影に使う派生値を、定数バッファへ書く元の行列と同時に更新する。
    viewProjectionMatrix_ = math.Multiply(viewMatrix_, projectionMatrix_);
    inverseViewProjectionMatrix_ = math.Inverse(viewProjectionMatrix_);
    frustum_ = math.ExtractFrustumPlanes(viewProjectionMatrix_);

    if (constMap_) {
        constMap_->view = viewMatrix_;
        constMap_->projection = projectionMatrix_;
    }
}

void Camera::SetLookAtPreviewView(const engine::Vector3& eye, const engine::Vector3& target, float aspectRatio) {
    // EyeとTargetが重なる場合は正面を仮の注視方向にし、逆行列の計算を安定させる。
    engine::Math math;

    engine::Vector3 safeTarget = target;
    engine::Vector3 forward = safeTarget - eye;

    if (math.Length(forward) < 0.0001f) {
        safeTarget = eye + engine::Vector3{ 0.0f, 0.0f, 1.0f };
        forward = safeTarget - eye;
    }

    eye_ = eye;
    target_ = safeTarget;
    up_ = { 0.0f, 1.0f, 0.0f };
    aspectRatio_ = aspectRatio;
    followObject_ = nullptr;
    hasFollowHeight_ = false;
    followTargetGrounded_ = true;
    targetObject_ = nullptr;
    followMode_ = FollowMode::FixedPoint;
    smoothEye_ = eye_;
    smoothTarget_ = target_;
    fixedPointPos_ = eye_;

    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    RefreshDerivedMatrices();
}

void Camera::ApplyReplayView(
    const engine::Vector3& eye,
    const engine::Vector3& target,
    const engine::Vector3& rotation,
    float fovY,
    float nearClip,
    float farClip) {
    engine::Vector3 safeTarget = target;

    if (math.Length(safeTarget - eye) < 0.0001f) {
        safeTarget = eye + engine::Vector3{ 0.0f, 0.0f, 1.0f };
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
    aspectRatio_ = (float)WinApp::GetClientWidth() / WinApp::GetClientHeight();

    // デフォルトのカメラモードを Aimable に設定
    followMode_ = FollowMode::Aimable;

    // Aimable のデフォルト距離を設定
    distance_ = 10.0f;

    isInputEnabled_ = true;

    // 定数バッファ作成
    constBuffer_ = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(CameraVP));
    constBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constMap_));

    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
    RefreshDerivedMatrices();
}


void Camera::ConfigureGameplayStyle(GameplayStyle style, ObstacleResponse obstacles,
    float yawLimitDegrees, float pitchLimitDegrees, float returnResponse, float fov) {
    const auto finiteClamp = [](float v, float fallback, float lo, float hi) {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    };
    if (style != gameplayStyle_) {
        if (style == GameplayStyle::Guided) freeStyleFov_ = fovY_;
        else SetFovY(freeStyleFov_);
        guidedLookOffset_ = {}; guidedInputThisFrame_ = false; guidedFocusInitialized_ = false;
        collisionDistance_ = -1.0f; hasRegionSafeEye_ = false;
    }
    gameplayStyle_ = style; obstacleResponse_ = obstacles;
    const float radians = std::numbers::pi_v<float> / 180.0f;
    guidedYawLimit_ = finiteClamp(yawLimitDegrees, 10, 0, 30) * radians;
    guidedPitchLimit_ = finiteClamp(pitchLimitDegrees, 5, 0, 15) * radians;
    guidedReturnResponse_ = finiteClamp(returnResponse, 5, 0.5f, 20);
    guidedLookOffset_.x = std::clamp(guidedLookOffset_.x, -guidedYawLimit_, guidedYawLimit_);
    guidedLookOffset_.y = std::clamp(guidedLookOffset_.y, -guidedPitchLimit_, guidedPitchLimit_);
    // 演出中や復帰中の画角補間は、通常構図の設定で中断しない。
    if (IsGuidedGameplay() && !isOverridden_ && !fovBlendActive_)
        SetFovY(finiteClamp(fov, 0.65f, 0.35f, 1.2f));
}

bool Camera::IsFollowAimInputActive() const {
    return followObject_ && inputManager_ && inputManager_->IsActionPressed("SecondaryAction") &&
        !isAimCameraSuppressed_ && followObject_->GetClassName() != "Player" && followMode_ != FollowMode::LockOn;
}

engine::Vector3 Camera::UpdateFollowReferencePosition() {
    if (!followObject_) {
        hasFollowHeight_ = false;
        return engine::Vector3::ZERO;
    }
    auto position = followObject_->GetWorldPosition();
    const bool stabilize = stabilizeJumpHeight_ && !IsFollowAimInputActive() &&
        (followMode_ == FollowMode::Aimable || followMode_ == FollowMode::Fixed || followMode_ == FollowMode::LockOn);
    if (!hasFollowHeight_ || !stabilize || followTargetGrounded_) {
        // 接地した足場の高さは、区画を判定する前に更新する。
        followHeight_ = position.y;
    } else {
        // 落下と特殊な高い上昇は従来どおり追い、通常のジャンプだけ高さを保つ。
        followHeight_ = std::clamp(followHeight_, position.y - jumpHeightAllowance_, position.y);
    }
    hasFollowHeight_ = true;
    if (stabilize) position.y = followHeight_;
    return position;
}

void Camera::ConfigureGuidedFraming(float lookAhead, float width, float height, float response) {
    auto valid = [](float v, float fallback, float lo, float hi) {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    };
    guidedLookAhead_ = valid(lookAhead, 6, 0, 15);
    guidedDeadZoneWidth_ = valid(width, .1f, 0, .3f);
    guidedDeadZoneHeight_ = valid(height, .1f, 0, .3f);
    guidedFramingResponse_ = valid(response, 3, .5f, 15);
}

engine::Vector3 Camera::UpdateGuidedFocus(const engine::Vector3& subject, float frameDeltaTime) {
    const float yaw = aimAngle_.y * std::numbers::pi_v<float> / 180.0f;
    const engine::Vector3 forward{std::sin(yaw),0,std::cos(yaw)};
    const engine::Vector3 right{forward.z,0,-forward.x};
    auto movement = subject - guidedPreviousSubject_; movement.y = 0;
    // 対象変更やワープでは、古い位置からの巨大な先読みを持ち越さない。
    if (!guidedFocusInitialized_ || engine::Math::Length(movement) > 30.0f) {
        guidedFocusInitialized_ = true; guidedFocusAnchor_ = subject;
        guidedVelocity_ = {}; guidedLeadOffset_ = {}; guidedLastDirection_ = forward;
        movement = {};
    }
    guidedPreviousSubject_ = subject;
    const float alpha = 1.0f - std::exp(-guidedFramingResponse_ * frameDeltaTime);
    // 画角と距離から画面内の余白を地面上の距離へ換算する。
    const float halfHeight = (std::max)(std::abs(aimDistance_),1.0f) * std::tan(std::clamp(fovY_,.1f,1.5f) * .5f);
    const float width = halfHeight * (std::max)(std::abs(aspectRatio_),.1f) * guidedDeadZoneWidth_;
    const float pitch = std::clamp(aimAngle_.x,20.0f,65.0f) * std::numbers::pi_v<float> / 180.0f;
    const float depth = halfHeight / (std::max)(std::sin(pitch),.2f) * guidedDeadZoneHeight_;
    const auto previousAnchor = guidedFocusAnchor_;
    const auto delta = subject - guidedFocusAnchor_;
    const float x = delta.x*right.x + delta.z*right.z;
    const float z = delta.x*forward.x + delta.z*forward.z;
    // 余白の内側は固定し、越えた分だけ基準点を移す。
    guidedFocusAnchor_ = guidedFocusAnchor_ + right*(x-std::clamp(x,-width,width)) + forward*(z-std::clamp(z,-depth,depth));
    guidedFocusAnchor_.y = subject.y;
    // 余白内の足踏みは先読みの向きにも伝えず、画面の小刻みな往復を抑える。
    movement = guidedFocusAnchor_ - previousAnchor; movement.y = 0;
    auto velocity = frameDeltaTime > .00001f ? movement*(1.0f/frameDeltaTime) : engine::Vector3{};
    const float measuredSpeed = engine::Math::Length(velocity);
    if (measuredSpeed > 20) velocity = velocity*(20.0f/measuredSpeed);
    guidedVelocity_ = guidedVelocity_ + (velocity-guidedVelocity_)*alpha;
    const float speed = engine::Math::Length(guidedVelocity_);
    // 顔の向きや短い足踏みで反転させず、実際の移動から進む方向を求める。
    if (speed > 1.5f) guidedLastDirection_ = guidedVelocity_*(1.0f/speed);
    const auto desiredLead = guidedLastDirection_ * (guidedLookAhead_*(.5f+.5f*std::clamp(speed/6.0f,0.0f,1.0f)));
    guidedLeadOffset_ = guidedLeadOffset_ + (desiredLead-guidedLeadOffset_)*alpha;
    return guidedFocusAnchor_ + guidedLeadOffset_;
}

void Camera::Update(float deltaTime) {
    // 演出レイヤーの進行だけは大きなフレーム落ちを抑え、通常追従の補間は実経過時間で計算する。
    const float frameDelta = std::clamp(deltaTime, 0.0f, 0.1f);
    // 入力を離した時だけ補助角度を減衰させる。基準角度自体を回し続けない。
    if (IsGuidedGameplay()) {
        if (!guidedInputThisFrame_ || !isInputEnabled_) {
            const float retention = std::exp(-guidedReturnResponse_ * frameDelta);
            guidedLookOffset_.x *= retention; guidedLookOffset_.y *= retention;
        }
        const float radians = std::numbers::pi_v<float> / 180.0f;
        rotation_ = {std::clamp(aimAngle_.x, 20.0f, 65.0f) * radians + guidedLookOffset_.y,
            aimAngle_.y * radians + guidedLookOffset_.x, 0.0f};
    }
    guidedInputThisFrame_ = false;
    if (fovBlendActive_) {
        fovBlendElapsed_ = (std::min)(fovBlendElapsed_ + frameDelta, fovBlendDuration_);
        const float rate = AnimationInterpolation::ApplyEasing(
            fovBlendElapsed_ / fovBlendDuration_, ToEasingType(fovBlendEasing_));
        fovY_ = fovBlendStart_ + (fovBlendTarget_ - fovBlendStart_) * rate;
        if (fovBlendElapsed_ >= fovBlendDuration_) {
            fovBlendActive_ = false;
        }
    }
    auto lerpVec3 = [](const engine::Vector3& a, const engine::Vector3& b, float t) {
        return engine::Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
        };

    if (!followObject_ || followMode_ != FollowMode::LockOn || !targetObject_) {
        hasLockOnPlayerPosition_ = false;
    }

    // まず追従対象から通常の視点と注視点を求め、その後に演出用Overrideを重ねる。
    if (followObject_) {
        // 区画選択と同じ基準を使い、ジャンプの途中だけ別の高さへ切り替わらないようにする。
        engine::Vector3 playerPos = UpdateFollowReferencePosition();
        const bool isAiming = IsFollowAimInputActive();

        // 回転モードは軌道中心を、その他の追従モードはプレイヤーの上方を基準に見る。
        engine::Vector3 targetPos = playerPos;
        if (IsGuidedGameplay() && followMode_ != FollowMode::LockOn && !isAiming)
            targetPos = UpdateGuidedFocus(playerPos, frameDelta);
        else guidedFocusInitialized_ = false;

        if (followMode_ == FollowMode::Orbit) {
            targetPos.x += orbitCenterOffset_.x;
            targetPos.y += orbitCenterHeight_ + orbitCenterOffset_.y;
            targetPos.z += orbitCenterOffset_.z;
        } else {
            targetPos.y += aimHeight_;
        }

        // 各モードで目標視点を作る。以下の補間と衝突回避はこの結果に共通で適用する。
        engine::Vector3 desiredEye = eye_;
        float toRad = std::numbers::pi_v<float> / 180.0f;

        switch (followMode_) {
        case FollowMode::LockOn:
        {
            if (targetObject_) {
                // 敵そのものではなくプレイヤー寄りを注視し、両者の足元を画面内に残す。
                engine::Vector3 enemyPos = targetObject_->GetWorldPosition();
                engine::Vector3 playerFocus = playerPos;
                playerFocus.y += aimHeight_ * 0.5f;

                // 敵中心へ寄せすぎず、足元と進行方向を画面内へ残します。
                targetPos.x = playerFocus.x + (enemyPos.x - playerFocus.x) * 0.35f;
                targetPos.z = playerFocus.z + (enemyPos.z - playerFocus.z) * 0.35f;

                float horizontalDistance = std::sqrt((enemyPos.x - playerFocus.x) * (enemyPos.x - playerFocus.x) +
                    (enemyPos.z - playerFocus.z) * (enemyPos.z - playerFocus.z));
                float maxY = playerFocus.y + std::min(3.0f, horizontalDistance * 0.4f);
                float idealY = playerFocus.y + (enemyPos.y - playerFocus.y) * 0.25f;
                targetPos.y = std::min(idealY, maxY);

                if (IsGuidedGameplay()) {
                    // 敵へ自動旋回せず、基準方向と見下ろしを保って二体を収める。
                    const float separation = math.Length(enemyPos - playerFocus);
                    const float fitDistance = separation * 0.7f / std::tan(fovY_ * 0.5f) + 4.0f;
                    const float distance = std::clamp((std::max)(aimDistance_, fitDistance), aimDistance_, aimDistance_ + 35.0f);
                    const auto rotate = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
                    desiredEye = targetPos + math.TransformNormal({0,0,-distance}, rotate);
                    break;
                }
                engine::Vector3 toEnemy = enemyPos - playerPos;

                float distanceXZ = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z);
                float heightDiff = std::max(0.0f, enemyPos.y - playerPos.y);
                float heightZoom = std::min(heightDiff * 0.3f, 4.0f);

                // 近距離では二体が切れないよう後退し、遠距離でも引き幅に上限を設ける。
                float closeZoomOut = 0.0f;

                if (distanceXZ < 8.0f) {
                    closeZoomOut = (8.0f - distanceXZ) * 0.7f;
                }

                float zoom = std::max(0.0f, distanceXZ - 10.0f) * 0.5f;
                zoom = std::min(zoom, 25.0f);

                // 基準位置はカメラエディターのロックオン設定を使う。
                engine::Vector3 dynamicOffset = {
                    lockOnOffset_.x, lockOnOffset_.y + closeZoomOut * 0.3f,
                    lockOnOffset_.z - closeZoomOut - heightZoom };
                dynamicOffset.z -= zoom;
                dynamicOffset.y += zoom * 0.15f;

                float angleToEnemy = std::atan2(toEnemy.x, toEnemy.z);
                auto normalizeAngle = [](float a) {
                    while (a > std::numbers::pi_v<float>)
                        a -= 2.0f * std::numbers::pi_v<float>;
                    while (a < -std::numbers::pi_v<float>)
                        a += 2.0f * std::numbers::pi_v<float>;
                    return a;
                    };

                // 角度差を[-π, π]へ折り返し、回転方向が境界で反転しないようにする。
                float diff = normalizeAngle(angleToEnemy - rotation_.y);
                rotation_.y += diff * (1.0f - std::exp(-5.0f * std::max(0.0f, deltaTime)));

                engine::Matrix4x4 rotateMat = math.MakeRotateYMatrix(rotation_.y);
                engine::Vector3 rotatedOffset = math.TransformNormal(dynamicOffset, rotateMat);

                desiredEye = playerPos + rotatedOffset;
            }
            else {
                followMode_ = FollowMode::Aimable;
            }

            break;
        }

        case FollowMode::Aimable:
        {
            engine::Matrix4x4 rotateMat = math.MakeRotateZMatrix(rotation_.z) * math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
            engine::Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
            offset = math.TransformNormal(offset, rotateMat);
            desiredEye = targetPos + offset;
            break;
        }

        case FollowMode::Fixed:
        {
            float currentY = rotation_.y;
            float pitch = aimAngle_.x * toRad;
            engine::Matrix4x4 rotateMat = math.MakeRotateXMatrix(pitch) * math.MakeRotateYMatrix(currentY);
            engine::Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
            offset = math.TransformNormal(offset, rotateMat);
            desiredEye = targetPos + offset;
            rotation_.x = pitch;
            break;
        }

        case FollowMode::FirstPerson:
        {
            desiredEye = playerPos + firstPersonOffset_;
            engine::Matrix4x4 rotateMatFP = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
            engine::Vector3 forwardFP = math.TransformNormal({ 0, 0, 1 }, rotateMatFP);
            targetPos = desiredEye + forwardFP;
            break;
        }

        case FollowMode::Orbit:
        {
            orbitAngle_ += orbitSpeed_;
            desiredEye.x = targetPos.x + orbitRadius_ * std::cos(orbitAngle_);
            desiredEye.z = targetPos.z + orbitRadius_ * std::sin(orbitAngle_);
            desiredEye.y = targetPos.y + orbitHeight_;
            break;
        }

        case FollowMode::FixedPoint:
        {
            desiredEye = fixedPointPos_;
            float pitch = fixedPointAngle_.x;
            float yaw = fixedPointAngle_.y;
            engine::Vector3 forward;
            forward.x = std::sin(yaw) * std::cos(pitch);
            forward.y = -std::sin(pitch);
            forward.z = std::cos(yaw) * std::cos(pitch);

            targetPos = desiredEye + forward * 10.0f;
            break;
        }
        }

        // エイム中はモードごとの視点を一時的に頭の位置へ寄せ、視線の向きは維持する。
        if (isAiming) {
            // ここで視点と注視点の両方を置き換え、後段の補間へ渡す。
            desiredEye = playerPos + engine::Vector3{ 0.0f, aimHeight_, 0.0f };
            engine::Matrix4x4 rotateMatFP = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
            engine::Vector3 forwardFP = math.TransformNormal({ 0, 0, 1 }, rotateMatFP);
            targetPos = desiredEye + forwardFP * 10.0f;
        }

        // 急なダッシュでもプレイヤーの画面位置が遅れないよう、移動分を補間の基点へ先に加える。
        if (followMode_ == FollowMode::LockOn && targetObject_) {
            if (hasLockOnPlayerPosition_ && isCameraInitialized_) {
                engine::Vector3 displacement = playerPos - previousLockOnPlayerPosition_;
                // 横方向の高速追従は保ち、異なる高さへ着地した時の上下移動だけは補間する。
                if (stabilizeJumpHeight_)
                    displacement.y = 0.0f;
                smoothEye_ = smoothEye_ + displacement;
                smoothTarget_ = smoothTarget_ + displacement;
            }
            previousLockOnPlayerPosition_ = playerPos;
            hasLockOnPlayerPosition_ = true;
        } else {
            hasLockOnPlayerPosition_ = false;
        }

        // 初回だけ補間せず即時配置し、それ以降は注視点と視点を別の速さで追従させる。
        if (!isCameraInitialized_) {
            smoothTarget_ = targetPos;
            smoothEye_ = desiredEye;
            target_ = smoothTarget_;
            eye_ = smoothEye_;
            isCameraInitialized_ = true;
        }
        else {
            // 60fps 時の追従率を経過秒数へ換算し、異なるフレームレートでも同じ時間で追いつく。
            const auto frameIndependentAlpha = [deltaTime](float alphaAt60Fps) {
                return 1.0f - std::pow(1.0f - alphaAt60Fps, (std::max)(0.0f, deltaTime) * 60.0f);
            };

            float targetLerpFactor = (followMode_ == FollowMode::LockOn)
                ? 1.0f - std::exp(-10.0f * (std::max)(0.0f, deltaTime)) : frameIndependentAlpha(0.2f);
            float eyeLerpFactor = (followMode_ == FollowMode::LockOn)
                ? 1.0f - std::exp(-7.0f * (std::max)(0.0f, deltaTime)) : frameIndependentAlpha(0.15f);

            // エイムへの切替中も位置を飛ばさず、視線を視点より速く追従させる。
            if (isAiming) {
                targetLerpFactor = frameIndependentAlpha(0.4f); // 視線の向きは早めに追従させる。
                eyeLerpFactor = frameIndependentAlpha(0.2f);    // カメラ位置は少し遅れて引き寄せる。
            }

            smoothTarget_ = lerpVec3(smoothTarget_, targetPos, targetLerpFactor);
            target_ = smoothTarget_;

            smoothEye_ = lerpVec3(smoothEye_, desiredEye, eyeLerpFactor);

            if (isEyeFrozen_) {
                smoothEye_ = eye_;
            }

            eye_ = smoothEye_;
        }

        // 追従視点とプレイヤーの間に地形がある場合だけ、手前へ視点を移動する。
        if (!isEyeFrozen_) {
            // 一人称・固定視点・エイム中はこの地形補正を適用しない。
            if (followMode_ != FollowMode::FirstPerson && followMode_ != FollowMode::FixedPoint && !isAiming && !PreservesComposition()) {
                engine::Vector3 rayStartPos = playerPos;
                rayStartPos.y += aimHeight_;

                engine::Vector3 toSmoothEye = smoothEye_ - rayStartPos;
                float distanceValue = math.Length(toSmoothEye);
                engine::Vector3 direction = (distanceValue > 0.001f) ? math.Normalize(toSmoothEye) : engine::Vector3{ 0,0,1 };

                engine::Vector3 targetEye = smoothEye_;
                float finalDist = distanceValue;

                // 地形レイの衝突点より少し手前で止め、壁への視点のめり込みを防ぐ。
                if (distanceValue > 0.1f) {
                    PhysicsQueryFilter filter = MakeCameraObstacleFilter(followObject_);
                    filter.refineSphereCastHit = true;
                    RaycastHit hit = CameraSphereCast(
                        rayStartPos, visibilitySettings_.collisionRadius, direction, distanceValue, filter
                    );

                    if (hit.isHit) {
                        const float kEpsilon = 0.2f;
                        finalDist = std::max(0.1f, hit.distance - kEpsilon);
                        targetEye = rayStartPos + (direction * finalDist);
                    }
                }

                // 視線が下向きなら、足元より低い視点を地面限界まで引き上げる。
                float groundLimitY = playerPos.y + 0.5f;

                if (targetEye.y < groundLimitY && direction.y < -0.001f) {
                    float distToGround = (groundLimitY - rayStartPos.y) / direction.y;

                    if (distToGround > 0.1f && distToGround < finalDist) {
                        targetEye = rayStartPos + (direction * distToGround);
                    }
                }

                // 遠ざかる側だけ遅らせる。障害物へ入る方向の補間は貫通の原因になるため行わない。
                const float safeDistance = math.Length(targetEye - rayStartPos);
                collisionDistance_ = RecoverCollisionDistance(collisionDistance_, safeDistance,
                    frameDelta, visibilitySettings_.recoveryTime);
                eye_ = rayStartPos + direction * collisionDistance_;
            }
            else {
                collisionDistance_ = -1.0f;
                eye_ = smoothEye_; // 補間済み視点を使い、エイム切替時の位置飛びを避ける。
            }
        }

        // カメラがプレイヤーに近付いたときは本体と子オブジェクトの透明度を揃える。
        engine::Vector3 playerPosForDist = followObject_->GetWorldPosition();
        playerPosForDist.y += aimHeight_ * 0.5f;

        engine::Vector3 toPlayer = playerPosForDist - eye_;
        float camToPlayerDist = math.Length(toPlayer);

        float alpha = 1.0f;

        if (camToPlayerDist < 3.5f) {
            alpha = std::max(0.0f, (camToPlayerDist - 1.0f) / 2.5f);
        }
        if (followMode_ == FollowMode::LockOn) {
            // 壁際で視点が押し戻されても、操作中のプレイヤーを見失わない透明度を保つ。
            alpha = (std::max)(alpha, 0.7f);
        }

        engine::Vector4 pColor = followObject_->GetColor();
        followObject_->SetColor({ pColor.x, pColor.y, pColor.z, alpha });

        for (engine::Object3D* child : followObject_->GetChildren()) {
            if (child) {
                engine::Vector4 cColor = child->GetColor();
                child->SetColor({ cColor.x, cColor.y, cColor.z, alpha });
            }
        }
    }
    else if (followMode_ == FollowMode::FixedPoint) {
        // 追従対象がない固定視点は、前回の平滑化位置も同時に更新して復帰時の跳びを防ぐ。
        engine::Vector3 desiredEye = fixedPointPos_;
        float pitch = fixedPointAngle_.x;
        float yaw = fixedPointAngle_.y;
        engine::Vector3 forward;
        forward.x = std::sin(yaw) * std::cos(pitch);
        forward.y = -std::sin(pitch);
        forward.z = std::cos(yaw) * std::cos(pitch);

        eye_ = desiredEye;
        target_ = desiredEye + forward * 10.0f;
        smoothEye_ = eye_;
        smoothTarget_ = target_;
        isCameraInitialized_ = true;
    }
    else if (followMode_ == FollowMode::Orbit) {
        // 対象なしの軌道カメラは保存済み中心を使い、視点と注視点を別々に補間する。
        engine::Vector3 orbitTarget = fixedPointPos_;
        orbitAngle_ += orbitSpeed_;

        engine::Vector3 desiredEye;
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
            smoothTarget_ = lerpVec3(smoothTarget_, orbitTarget, 0.2f);
            target_ = smoothTarget_;

            smoothEye_ = lerpVec3(smoothEye_, desiredEye, 0.15f);

            if (isEyeFrozen_) {
                smoothEye_ = eye_;
            }

            eye_ = smoothEye_;
        }
    }

    // 演出カメラの位置は、通常カメラの計算が終わってから加重して重ねる。
    engine::Vector3 normalEye = eye_;
    engine::Vector3 normalTarget = target_;

    if (followObject_ == nullptr && isOverridden_) {
        normalEye = overrideStartEye_;
        normalTarget = overrideStartTarget_;
    }

    auto resolveOverridePose = [&]() {
        // 目の位置と注視点は固定座標、シーンオブジェクト追従、通常注視点への軸追従を組み合わせられる。
        engine::Vector3 desiredEye = overrideParams_.fixedEyePos;

        if (overrideParams_.eyeSource == OverrideEyeSource::SceneObjectRelativeToReference &&
            overrideParams_.eyeObject && overrideParams_.eyeReferenceObject) {
            desiredEye = ResolveRelativeEye(*overrideParams_.eyeObject, *overrideParams_.eyeReferenceObject,
                                            overrideParams_.eyeObjectOffset);
        }
        else if (overrideParams_.eyeSource == OverrideEyeSource::SceneObject && overrideParams_.eyeObject) {
            desiredEye = GetCurrentObjectPosition(overrideParams_.eyeObject) + overrideParams_.eyeObjectOffset;
        }
        else {
            if (overrideParams_.trackEyeX) {
                desiredEye.x = normalTarget.x + (overrideParams_.fixedEyePos.x - overrideParams_.fixedTargetPos.x);
            }

            if (overrideParams_.trackEyeY) {
                if (regionProjectionActive_ && regionSafetySubject_ == followObject_)
                    desiredEye.y += normalTarget.y - aimHeight_ - regionSafetySubject_->GetWorldPosition().y;
                else desiredEye.y = normalTarget.y + (overrideParams_.fixedEyePos.y - overrideParams_.fixedTargetPos.y);
            }

            if (overrideParams_.trackEyeZ) {
                desiredEye.z = normalTarget.z + (overrideParams_.fixedEyePos.z - overrideParams_.fixedTargetPos.z);
            }
        }

        engine::Vector3 desiredTarget = overrideParams_.fixedTargetPos;

        if (overrideParams_.targetSource == OverrideTargetSource::SceneObject && overrideParams_.targetFollowObject) {
            desiredTarget = GetCurrentObjectPosition(overrideParams_.targetFollowObject) + overrideParams_.targetObjectOffset;
        }
        else if (overrideParams_.targetSource == OverrideTargetSource::EyeObjectForward && overrideParams_.eyeObject) {
            desiredTarget = desiredEye
                + GetObjectForward(overrideParams_.eyeObject) * (std::max)(overrideParams_.eyeForwardDistance, 0.01f)
                + overrideParams_.targetObjectOffset;
        }
        else {
            if (overrideParams_.trackTargetX)
                desiredTarget.x = normalTarget.x;
            if (overrideParams_.trackTargetY) {
                if (regionProjectionActive_ && regionSafetySubject_ == followObject_)
                    desiredTarget.y += normalTarget.y - aimHeight_ - regionSafetySubject_->GetWorldPosition().y;
                else desiredTarget.y = normalTarget.y;
            }
            if (overrideParams_.trackTargetZ)
                desiredTarget.z = normalTarget.z;
        }

        // Override先も必要なら独立して平滑化し、切替直後だけ現在の目標位置で初期化する。
        if (!overrideFollowInitialized_) {
            overrideFollowEye_ = desiredEye;
            overrideFollowTarget_ = desiredTarget;
            overrideFollowInitialized_ = true;
        }

        if (overrideParams_.eyeFollowMode == OverrideFollowMode::Smooth) {
            overrideFollowEye_ = AnimationInterpolation::Damp(
                overrideFollowEye_, desiredEye, (std::max)(overrideParams_.eyeFollowResponse, 0.01f), frameDelta);
        }
        else {
            overrideFollowEye_ = desiredEye;
        }

        if (overrideParams_.targetFollowMode == OverrideFollowMode::Smooth) {
            overrideFollowTarget_ = AnimationInterpolation::Damp(
                overrideFollowTarget_, desiredTarget, (std::max)(overrideParams_.targetFollowResponse, 0.01f), frameDelta);
        }
        else {
            overrideFollowTarget_ = desiredTarget;
        }

        return std::pair<engine::Vector3, engine::Vector3>{ overrideFollowEye_, overrideFollowTarget_ };
    };

    // 重みを増減して演出へ入る・戻る。継続時間0なら目標姿勢を直ちに適用する。
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
                eye_ = lerpVec3(overrideStartEye_, finalEndEye, overrideWeight_);
                target_ = lerpVec3(overrideStartTarget_, finalEndTarget, overrideWeight_);
            }
            else {
                eye_ = lerpVec3(normalEye, finalEndEye, overrideWeight_);
                target_ = lerpVec3(normalTarget, finalEndTarget, overrideWeight_);
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

    // 範囲カメラでも基準ショットを保持し、手動操作は小さな見回しだけを合成する。
    if (IsGuidedGameplay() && regionProjectionActive_ && overrideWeight_ > 0.0f) {
        const auto offset = eye_ - target_;
        const auto rotate = math.MakeRotateXMatrix(guidedLookOffset_.y * overrideWeight_) *
            math.MakeRotateYMatrix(guidedLookOffset_.x * overrideWeight_);
        eye_ = target_ + math.TransformNormal(offset, rotate);
        SyncRotationToCurrentView();
    }

    // 範囲ショットはOverride合成後も検査する。従来の演出はこの補正の対象にしない。
    if (regionProjectionActive_ && regionSafetySubject_ && overrideWeight_ > 0.0f && !PreservesComposition()) {
        PhysicsQueryFilter filter = MakeCameraObstacleFilter(regionSafetySubject_);
        filter.refineSphereCastHit = true;
        auto safeEnd = [&](const engine::Vector3& start, const engine::Vector3& end) {
            const engine::Vector3 delta = end - start;
            const float distance = math.Length(delta);
            if (distance <= 0.001f) return end;
            const auto direction = delta * (1.0f / distance);
            const auto hit = CameraSphereCast(start, visibilitySettings_.collisionRadius, direction, distance, filter);
            return hit.isHit ? start + direction * (std::max)(0.0f, hit.distance - 0.05f) : end;
        };
        const Aabb bounds = regionSafetySubject_->GetAabb();
        const engine::Vector3 anchor = math.Length(bounds.max - bounds.min) > 0.001f
            ? (bounds.min + bounds.max) * 0.5f
            : regionSafetySubject_->GetWorldPosition() + engine::Vector3{0.0f, 0.8f, 0.0f};
        engine::Vector3 safeEye = safeEnd(anchor, eye_);
        // 終点が安全でも、切替の途中で壁を横断しないよう前回視点からも調べる。
        if (hasRegionSafeEye_) safeEye = safeEnd(previousRegionSafeEye_, safeEye);
        eye_ = safeEye;
        previousRegionSafeEye_ = safeEye;
        hasRegionSafeEye_ = true;
        SyncRotationToCurrentView();
    }

    // 位置の確定後に手ブレと画角演出を加算し、最後に描画用行列へ変換する。
    engine::Vector3 viewEye = eye_;
    engine::Vector3 viewTarget = target_;
    engine::Vector3 mixedShakeOffset = engine::Vector3::ZERO;

    for (ShakeLayer& layer : shakeLayers_) {
        const float progress = std::clamp(layer.elapsed / layer.duration, 0.0f, 1.0f);
        const float envelope = (1.0f - progress) * (1.0f - progress);
        const float phase = layer.elapsed * layer.frequency * 2.0f * std::numbers::pi_v<float>;
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

    // FOVの開始側と終了側で別の包絡線を使い、複数パルスを合成する。
    float mixedFovOffset = 0.0f;

    for (FovPulseLayer& layer : fovPulseLayers_) {
        const float progress = std::clamp(layer.elapsed / layer.duration, 0.0f, 1.0f);
        float envelope = 0.0f;

        if (progress < layer.attackRatio) {
            const float attack = progress / layer.attackRatio;
            envelope = Easing::SmoothStep(attack);
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

    engine::Vector3 forward = { viewTarget.x - viewEye.x, viewTarget.y - viewEye.y, viewTarget.z - viewEye.z };
    engine::Vector3 currentUp = up_;

    // 真上・真下を見るとLookAtの上方向が視線と平行になるため、別の上方向を選ぶ。
    if (std::abs(forward.x) < 0.001f && std::abs(forward.z) < 0.001f) {
        if (forward.y < 0.0f) {
            currentUp = { 0.0f, 0.0f, 1.0f };
        }
        else {
            currentUp = { 0.0f, 0.0f, -1.0f };
        }
    }

    viewMatrix_ = math.MakeLookAtMatrix(viewEye, viewTarget, currentUp);
    engine::Vector3 lens = {fovY_, nearClip_, farClip_};
    if (regionProjectionActive_) {
        if (isOverridden_) {
            lens = regionProjectionStart_ + (regionProjection_ - regionProjectionStart_) * overrideWeight_;
        } else {
            const float weight = std::clamp(overrideWeight_ / regionProjectionExitWeight_, 0.0f, 1.0f);
            lens = lens + (regionProjection_ - lens) * weight;
        }
    }
    const float renderFov = std::clamp(lens.x + mixedFovOffset, 5.0f * std::numbers::pi_v<float> / 180.0f, 170.0f * std::numbers::pi_v<float> / 180.0f);
    // 切替に引き継ぐ基礎画角へ演出分を含めると、次フレームで同じPulseが二重に加算される。
    renderedProjection_ = lens;
    projectionMatrix_ = math.MakePerspectiveFovMatrix(renderFov, aspectRatio_, lens.y, lens.z);
    RefreshDerivedMatrices();
}

void Camera::ConfigFixed(const engine::Vector3& offset) {
    fixedOffset_ = offset;
}

void Camera::ConfigAimable(float distance, float height, const engine::Vector3& angle) {
    // 設定画面の角度は度単位なので、内部の回転表現へ渡す前にラジアンへ変換する。
    aimDistance_ = distance;
    aimHeight_ = height;
    aimAngle_ = angle;
    float toRad = std::numbers::pi_v<float> / 180.0f;
    rotation_.x = angle.x * toRad;
    rotation_.y = angle.y * toRad;
    rotation_.z = angle.z * toRad;
}

void Camera::ConfigFirstPerson(const engine::Vector3& eyeOffset) {
    firstPersonOffset_ = eyeOffset;
}

void Camera::SnapToThirdPerson(float distance, float height, float pitch) {
    collisionDistance_ = -1.0f;
    // 追従補間の途中値も更新し、モード切り替え直後のカメラの滑りをなくす。
    if (!followObject_)
        return;

    // ワープ・復帰では以前の足場の高さを持ち越さない。
    followHeight_ = followObject_->GetWorldPosition().y;
    hasFollowHeight_ = true;
    followMode_ = FollowMode::Aimable;
    aimDistance_ = distance;
    aimHeight_ = height;
    rotation_.x = pitch;

    engine::Vector3 playerPos = followObject_->GetWorldPosition();
    engine::Vector3 targetPos = playerPos;
    targetPos.y += aimHeight_;

    engine::Matrix4x4 rotateMat = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
    engine::Vector3 offset = { 0.0f, 0.0f, -aimDistance_ };
    offset = math.TransformNormal(offset, rotateMat);

    smoothTarget_ = targetPos;
    smoothEye_ = targetPos + offset;
    target_ = smoothTarget_;
    eye_ = smoothEye_;
    isEyeFrozen_ = false;
}

void Camera::AddRotation(const engine::Vector2& mouseDelta) {
    if (IsGuidedGameplay()) {
        if (!isInputEnabled_) return;
        const auto* settings = GameSettingsManager::GetInstance();
        const float speed = 0.005f * rotationSensitivity_;
        guidedLookOffset_.x = std::clamp(guidedLookOffset_.x + mouseDelta.x * speed * (settings->GetCameraInvertX() ? -1.0f : 1.0f), -guidedYawLimit_, guidedYawLimit_);
        guidedLookOffset_.y = std::clamp(guidedLookOffset_.y + mouseDelta.y * speed * (settings->GetCameraInvertY() ? -1.0f : 1.0f), -guidedPitchLimit_, guidedPitchLimit_);
        guidedInputThisFrame_ = mouseDelta.x != 0 || mouseDelta.y != 0;
        return;
    }
    manualLookInput_ = manualLookInput_ || mouseDelta.x != 0.0f || mouseDelta.y != 0.0f;
    // Aimable と FirstPerson で共用の回転処理
    const float rotateSpeed = 0.005f * rotationSensitivity_;
    const auto* settings = GameSettingsManager::GetInstance();
    rotation_.x += mouseDelta.y * rotateSpeed * (settings->GetCameraInvertY() ? -1.f : 1.f);
    rotation_.y += mouseDelta.x * rotateSpeed * (settings->GetCameraInvertX() ? -1.f : 1.f);

    // X軸の回転（ピッチ）に制限をかける
    const float pitchLimit = std::numbers::pi_v<float> / 2.0f - 0.01f; // 90度手前
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));

    // Y軸の回転（ヨー）は 2PI でラップアラウンド
    if (rotation_.y > std::numbers::pi_v<float>) {
        rotation_.y -= 2.0f * std::numbers::pi_v<float>;
    }
    if (rotation_.y < -std::numbers::pi_v<float>) {
        rotation_.y += 2.0f * std::numbers::pi_v<float>;
    }
}

void Camera::SetRotationSensitivity(float sensitivity) {
    rotationSensitivity_ = std::clamp(sensitivity, 0.2f, 2.0f);
}

void Camera::SyncRotationToCurrentView() {
    static engine::Math math;

    engine::Vector3 currentTarget = target_;

    // 現在のカメラ位置(eye_)から、注視点(currentTarget)への方向ベクトル
    engine::Vector3 forward = currentTarget - eye_;
    float distanceValue = math.Length(forward);

    // 距離が近すぎる場合の安全対策
    if (distanceValue < 0.001f) {
        forward = { 0.0f, 0.0f, 1.0f };
    }
    else {
        forward = math.Normalize(forward);
    }

    // 方向ベクトルから、ヨー(Y軸)とピッチ(X軸)の角度を逆算
    rotation_.y = std::atan2(forward.x, forward.z);
    rotation_.x = std::asin(-forward.y);

    // 真上・真下を向きすぎないように制限
    const float pitchLimit = std::numbers::pi_v<float> / 2.0f - 0.01f;
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));

    // カメラの距離も現在の距離に同期する（一瞬で近づくのを防ぐ）
    aimDistance_ = distanceValue;
}

void Camera::StartShake(float duration, float amplitude, float frequency, const engine::Vector3& axisWeight) {
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
    // 別のショットへ切り替える時も、現在の視点を始点にして新しい補間を先頭から始める。
    ++overrideRevision_;
    ClearRegionProjection();
    isOverridden_ = true;
    overrideParams_ = params;
    overrideDuration_ = (std::max)(params.duration, 0.0f);
    overrideStartEye_ = eye_;
    overrideStartTarget_ = target_;
    overrideFollowInitialized_ = false;

    overrideTimer_ = 0.0f;
    overrideWeight_ = 0.0f;
}

void Camera::EndOverride(float duration) {
    if (regionProjectionActive_) {
        regionProjection_ = renderedProjection_;
        regionProjectionExitWeight_ = (std::max)(0.0001f, overrideWeight_);
    }
    isOverridden_ = false;
    overrideDuration_ = (std::max)(duration, 0.0f);
    overrideStartEye_ = eye_;
    overrideStartTarget_ = target_;

    // 終了中に追従Objectが元の場所へ戻っても画面が引っ張られないよう、最後の構図を固定します。
    overrideParams_.eyeSource = OverrideEyeSource::Fixed;
    overrideParams_.targetSource = OverrideTargetSource::Fixed;
    overrideParams_.eyeObject = nullptr;
    overrideParams_.eyeReferenceObject = nullptr;
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
