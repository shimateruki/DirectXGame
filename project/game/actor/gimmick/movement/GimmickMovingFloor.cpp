#include "GimmickMovingFloor.h"
#include <algorithm>
#include <cmath>
#include "CollisionConfig.h"

namespace {
constexpr float kLegacyMoveRange = 3.0f;
constexpr float kMaxFloatAmplitude = 2.0f;
constexpr float kFloatPitchScale = 0.025f;
constexpr float kFloatRollScale = 0.035f;
constexpr float kTwoPi = 6.28318530718f;
}

void GimmickMovingFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("MovingFloor");
    
    if (!param_.has_value()) param_.emplace();
    if (param_->speed == 0.0f) param_->speed = 2.0f; // デフォルト速度
    
    // 当たり判定設定 (地面として扱う)
    SetCollisionAttribute(kGround);
}

void GimmickMovingFloor::Update(float deltaTime) {
    // Ghost RecorderのPathが割り当てられている床は、記録済みの軌道を唯一の移動元にする。
    // 手続き的な往復移動を先に重ねると、Editorで調整したPathと乗り移り判定がずれる。
    if (!GetRecordPathName().empty()) {
        const Vector3 previousPosition = GetTransform()->translate;
        BaseGimmick::Update(deltaTime);
        frameDelta_ = GetTransform()->translate - previousPosition;
        hasCapturedStartTransform_ = true;
        return;
    }

    // JSONのTransformはInitialize後に反映されるため、最初のUpdateで実際の配置を記録する。
    if (!hasCapturedStartTransform_) {
        startPosition_ = GetTransform()->translate;
        startRotation_ = GetTransform()->rotate;
        phaseOffset_ = std::fmod(std::abs(startPosition_.x * 0.037f + startPosition_.z * 0.061f), kTwoPi);
        hasCapturedStartTransform_ = true;
    }

    const Vector3 previousPosition = GetTransform()->translate;
    time_ += deltaTime;

    const float speed = param_.has_value() ? (std::max)(param_->speed, 0.0f) : 0.0f;
    const int actionMode = param_.has_value() ? param_->actionMode : 0;
    // 配置位置を往復運動の中心として扱う。位相差は自然な浮遊モードだけに適用する。
    const float waveTime = time_ * speed + (actionMode == 1 ? phaseOffset_ : 0.0f);
    Transform* transform = GetTransform();
    transform->translate = startPosition_;
    transform->rotate = startRotation_;

    if (actionMode == 1) {
        // 溶岩に浮く足場用。移動床の位置を保ったまま、緩やかな浮き沈みと傾きだけを加える。
        const float amplitude = (std::clamp)(std::abs(param_->moveAmount), 0.0f, kMaxFloatAmplitude);
        const float tiltWeight = (std::min)(amplitude, 1.0f);
        transform->translate.y += std::sin(waveTime) * amplitude;
        transform->rotate.x += std::sin(waveTime * 0.73f + 1.2f) * kFloatPitchScale * tiltWeight;
        transform->rotate.z += std::sin(waveTime * 0.91f) * kFloatRollScale * tiltWeight;
        transform->isQuaternionMaster = false;
    } else if (actionMode >= 2 && actionMode <= 4) {
        // ステージ制作用の往復移動。振幅と速度はInspectorから調整できる。
        const float travel = (std::max)(0.0f, std::abs(param_->moveAmount));
        const float offset = std::sin(waveTime) * travel;
        if (actionMode == 2) transform->translate.x += offset;
        if (actionMode == 3) transform->translate.z += offset;
        if (actionMode == 4) transform->translate.y += offset;
    } else {
        // 従来ステージとの互換性を保つ通常の上下移動。
        transform->translate.y += std::sin(waveTime) * kLegacyMoveRange;
    }

    frameDelta_ = hasCapturedStartTransform_ ? transform->translate - previousPosition : Vector3{};

    // 移動後のTransformを同じフレームで行列と当たり判定へ反映する。
    BaseGimmick::Update(deltaTime);
}

bool GimmickMovingFloor::OnCollision(Object3d* other) {
    if (!other || other->GetClassName() != "Player") {
        return true;
    }

    // 足場側から見てプレイヤーが上にいる場合だけ、そのフレームの移動量を引き継ぐ。
    // 押し戻し自体はPlayer側が担当するため、ここでは床速度の反映だけを行う。
    const CollisionInfo info = CheckCollision(other);
    if (info.isColliding && info.normal.y < -0.5f) {
        other->GetTransform()->translate += frameDelta_;
    }
    return true;
}
