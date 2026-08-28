#define NOMINMAX
#include "GimmickHazardRideFloor.h"

#include "BaseScene.h"
#include "CollisionConfig.h"
#include "SceneManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
constexpr float kDefaultTravelSpeed = 7.5f;
constexpr float kDefaultTravelDistance = 46.0f;
constexpr float kDefaultStartDelay = 0.55f;
constexpr float kDefaultEndWarningDuration = 0.7f;
constexpr float kDefaultFallDuration = 2.0f;
constexpr float kDefaultFallGravity = 38.0f;
constexpr float kReturnDelaySeconds = 5.0f;
constexpr int kDefaultHazardCount = 4;
}

void GimmickHazardRideFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("HazardRideFloor");
    SetName("Gimmick_HazardRideFloor");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetStatic(false);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->speed = kDefaultTravelSpeed;
    param_->moveAmount = kDefaultTravelDistance;
    param_->shakeDuration = kDefaultStartDelay;
    param_->interval = kDefaultEndWarningDuration;
    param_->fallDuration = kDefaultFallDuration;
    param_->gravity = kDefaultFallGravity;
    param_->maxCount = kDefaultHazardCount;
    param_->actionMode = 0;
    // 従来の一度きりの挙動を既定とし、必要なステージだけ明示的に復帰を有効化する。
    param_->returnOnOff = false;
}

void GimmickHazardRideFloor::Update(float deltaTime) {
    CaptureStartTransform();

    const bool isPlaying = SceneManager::GetInstance() && SceneManager::GetInstance()->IsPlaying();
    if (!isPlaying) {
        ResetForRetry();
        BaseGimmick::Update(deltaTime);
        return;
    }

    const Vector3 previousPosition = GetTransform()->translate;
    stateTimer_ += deltaTime;

    switch (state_) {
    case State::Waiting:
        break;

    case State::Starting: {
        const float delay = GetStartDelay();
        const float normalized = delay > 0.0f ? (std::min)(stateTimer_ / delay, 1.0f) : 1.0f;
        const float intensity = 0.025f + normalized * 0.045f;
        GetTransform()->translate.x = startPosition_.x +
            (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * intensity;
        GetTransform()->translate.z = startPosition_.z +
            (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * intensity;
        if (stateTimer_ >= delay) {
            GetTransform()->translate = startPosition_;
            ChangeState(State::Moving);
        }
        break;
    }

    case State::Moving: {
        travelledDistance_ = (std::min)(GetTravelDistance(), travelledDistance_ + GetTravelSpeed() * deltaTime);
        const float progress = GetTravelDistance() > 0.0f ? travelledDistance_ / GetTravelDistance() : 1.0f;
        GetTransform()->translate = startPosition_ + GetTravelDirection() * travelledDistance_;
        GetTransform()->translate.y += std::sin(progress * 18.8495559f) * 0.055f;
        TriggerPendingHazards(progress);
        if (travelledDistance_ >= GetTravelDistance()) {
            ChangeState(State::Warning);
        }
        break;
    }

    case State::Warning: {
        const float intensity = 0.04f + (std::min)(stateTimer_ / GetEndWarningDuration(), 1.0f) * 0.08f;
        const Vector3 endPosition = startPosition_ + GetTravelDirection() * GetTravelDistance();
        GetTransform()->translate = endPosition;
        GetTransform()->translate.x +=
            (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * intensity;
        GetTransform()->translate.z +=
            (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * intensity;
        if (stateTimer_ >= GetEndWarningDuration()) {
            ChangeState(State::Falling);
        }
        break;
    }

    case State::Falling:
        fallVelocityY_ -= GetFallGravity() * deltaTime;
        GetTransform()->translate.y += fallVelocityY_ * deltaTime;
        GetTransform()->rotate.x += 0.8f * deltaTime;
        GetTransform()->rotate.z += 0.55f * deltaTime;
        GetTransform()->isQuaternionMaster = false;
        if (stateTimer_ >= GetFallDuration()) {
            ChangeState(State::Hidden);
        }
        break;

    case State::Hidden:
        if (ShouldReturnAfterRide() && stateTimer_ >= kReturnDelaySeconds) {
            ResetForRetry();
        }
        break;
    }

    frameDelta_ = GetTransform()->translate - previousPosition;
    BaseGimmick::Update(deltaTime);
}

bool GimmickHazardRideFloor::OnCollision(Object3d* other) {
    if (!other || other->GetClassName() != "Player") {
        return true;
    }

    const CollisionInfo info = CheckCollision(other);
    if (!info.isColliding || info.normal.y >= -0.5f) {
        return true;
    }

    if (state_ == State::Waiting) {
        BeginRide();
    }

    if (state_ != State::Hidden) {
        other->GetTransform()->translate += frameDelta_;
    }
    return true;
}

void GimmickHazardRideFloor::OnTrigger() {
    BeginRide();
}

std::unique_ptr<Object3d> GimmickHazardRideFloor::Clone() const {
    auto clone = std::make_unique<GimmickHazardRideFloor>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickHazardRideFloor::CaptureStartTransform() {
    if (hasCapturedStartTransform_) {
        return;
    }
    startPosition_ = GetTransform()->translate;
    startRotation_ = GetTransform()->rotate;
    hasCapturedStartTransform_ = true;
}

void GimmickHazardRideFloor::BeginRide() {
    if (state_ != State::Waiting) {
        return;
    }
    CaptureStartTransform();
    ChangeState(State::Starting);
}

void GimmickHazardRideFloor::TriggerPendingHazards(float progress) {
    const int count = GetHazardCount();
    while (nextHazardIndex_ < count) {
        const float triggerProgress = static_cast<float>(nextHazardIndex_ + 1) / static_cast<float>(count + 1);
        if (progress + 0.0001f < triggerProgress) {
            break;
        }

        const int baseEventID = GetTargetID();
        if (baseEventID > 0) {
            if (BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
                scene->TriggerEvent(baseEventID + nextHazardIndex_);
            }
        }
        ++nextHazardIndex_;
    }
}

void GimmickHazardRideFloor::ResetForRetry() {
    state_ = State::Waiting;
    stateTimer_ = 0.0f;
    travelledDistance_ = 0.0f;
    fallVelocityY_ = 0.0f;
    nextHazardIndex_ = 0;
    frameDelta_ = {};
    SetIsVisible(true);
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    GetTransform()->translate = startPosition_;
    GetTransform()->rotate = startRotation_;
    GetTransform()->isQuaternionMaster = false;
}

void GimmickHazardRideFloor::ChangeState(State nextState) {
    state_ = nextState;
    stateTimer_ = 0.0f;

    if (state_ == State::Falling) {
        fallVelocityY_ = 0.0f;
    }
    else if (state_ == State::Hidden) {
        SetIsVisible(false);
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        frameDelta_ = {};
    }
}

bool GimmickHazardRideFloor::ShouldReturnAfterRide() const {
    return param_.has_value() && param_->returnOnOff;
}

Vector3 GimmickHazardRideFloor::GetTravelDirection() const {
    const float yaw = startRotation_.y;
    Vector3 direction = { std::cos(yaw), 0.0f, std::sin(yaw) };
    if (param_.has_value() && param_->actionMode == 1) {
        direction = direction * -1.0f;
    }
    return direction;
}

float GimmickHazardRideFloor::GetTravelSpeed() const {
    return param_.has_value() ? (std::max)(0.1f, std::abs(param_->speed)) : kDefaultTravelSpeed;
}

float GimmickHazardRideFloor::GetTravelDistance() const {
    return param_.has_value() ? (std::max)(0.1f, std::abs(param_->moveAmount)) : kDefaultTravelDistance;
}

float GimmickHazardRideFloor::GetStartDelay() const {
    return param_.has_value() ? (std::max)(0.0f, param_->shakeDuration) : kDefaultStartDelay;
}

float GimmickHazardRideFloor::GetEndWarningDuration() const {
    return param_.has_value() ? (std::max)(0.1f, param_->interval) : kDefaultEndWarningDuration;
}

float GimmickHazardRideFloor::GetFallDuration() const {
    return param_.has_value() ? (std::max)(0.1f, param_->fallDuration) : kDefaultFallDuration;
}

float GimmickHazardRideFloor::GetFallGravity() const {
    return param_.has_value() ? (std::max)(0.1f, param_->gravity) : kDefaultFallGravity;
}

int GimmickHazardRideFloor::GetHazardCount() const {
    return param_.has_value() ? (std::clamp)(param_->maxCount, 0, 16) : kDefaultHazardCount;
}
