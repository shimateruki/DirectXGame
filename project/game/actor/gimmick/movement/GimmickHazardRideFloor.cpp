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
} // namespace

void GimmickHazardRideFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // プレイヤーを運ぶ床として衝突を持ち、移動距離と途中の危険イベント数を設定します。
    SetClassName("Gimmick");
    SetGimmickType("HazardRideFloor");
    SetName("Gimmick_HazardRideFloor");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
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
    // 乗車後に予告、移動、終点の警告、落下の順で進めます。
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
        SetTranslateX(startPosition_.x + (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * intensity);
        SetTranslateZ(startPosition_.z + (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * intensity);
        if (stateTimer_ >= delay) {
            SetTranslate(startPosition_);
            ChangeState(State::Moving);
        }
        break;
    }

    case State::Moving: {
        travelledDistance_ = (std::min)(GetTravelDistance(), travelledDistance_ + GetTravelSpeed() * deltaTime);
        const float progress = GetTravelDistance() > 0.0f ? travelledDistance_ / GetTravelDistance() : 1.0f;
        SetTranslate(startPosition_ + GetTravelDirection() * travelledDistance_);
        SetTranslateY(GetTranslate().y + std::sin(progress * 18.8495559f) * 0.055f);
        TriggerPendingHazards(progress);
        if (travelledDistance_ >= GetTravelDistance()) {
            ChangeState(State::Warning);
        }
        break;
    }

    case State::Warning: {
        const float intensity = 0.04f + (std::min)(stateTimer_ / GetEndWarningDuration(), 1.0f) * 0.08f;
        const Vector3 endPosition = startPosition_ + GetTravelDirection() * GetTravelDistance();
        SetTranslate(endPosition);
        SetTranslateX(GetTranslate().x + (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * intensity);
        SetTranslateZ(GetTranslate().z + (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * intensity);
        if (stateTimer_ >= GetEndWarningDuration()) {
            ChangeState(State::Falling);
        }
        break;
    }

    case State::Falling:
        fallVelocityY_ -= GetFallGravity() * deltaTime;
        SetTranslateY(GetTranslate().y + fallVelocityY_ * deltaTime);
        SetEulerX(GetRotation().x + 0.8f * deltaTime);
        SetEulerZ(GetRotation().z + 0.55f * deltaTime);
        SetQuaternionMaster(false);
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

    // このフレームの床の移動量を接触中のプレイヤーへ渡します。
    frameDelta_ = GetTransform()->translate - previousPosition;
    BaseGimmick::Update(deltaTime);
}

bool GimmickHazardRideFloor::OnCollision(Object3d* other) {
    // 上面へ乗ったプレイヤーだけを運び、横からの接触では発進させません。
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
        other->SetTranslate(other->GetTranslate() + frameDelta_);
    }
    return true;
}

void GimmickHazardRideFloor::OnTrigger() {
    // 外部イベントからも乗車と同じ発進手順を開始できます。
    BeginRide();
}

std::unique_ptr<Object3d> GimmickHazardRideFloor::Clone() const {
    // 複製先を初期化してから走行設定と配置を引き継ぎます。
    auto clone = std::make_unique<GimmickHazardRideFloor>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickHazardRideFloor::CaptureStartTransform() {
    // 編集時の配置を一度だけ保存し、再挑戦時の復帰先にします。
    if (hasCapturedStartTransform_) {
        return;
    }
    startPosition_ = GetTransform()->translate;
    startRotation_ = GetTransform()->rotate;
    hasCapturedStartTransform_ = true;
}

void GimmickHazardRideFloor::BeginRide() {
    // 二重の開始通知でタイマーが巻き戻らないよう待機中だけ開始します。
    if (state_ != State::Waiting) {
        return;
    }
    CaptureStartTransform();
    ChangeState(State::Starting);
}

void GimmickHazardRideFloor::TriggerPendingHazards(float progress) {
    // 走行距離を均等分割した地点で、対応する危険イベントを一度ずつ発火します。
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
    // 再挑戦では移動・落下・発火済み数を消し、元の足場へ戻します。
    state_ = State::Waiting;
    stateTimer_ = 0.0f;
    travelledDistance_ = 0.0f;
    fallVelocityY_ = 0.0f;
    nextHazardIndex_ = 0;
    frameDelta_ = {};
    SetIsVisible(true);
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetTranslate(startPosition_);
    SetEulerAngles(startRotation_);
    SetQuaternionMaster(false);
}

void GimmickHazardRideFloor::ChangeState(State nextState) {
    // 消失状態に入る時点で表示と床判定を同時に無効化します。
    state_ = nextState;
    stateTimer_ = 0.0f;

    if (state_ == State::Falling) {
        fallVelocityY_ = 0.0f;
    } else if (state_ == State::Hidden) {
        SetIsVisible(false);
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        frameDelta_ = {};
    }
}

bool GimmickHazardRideFloor::ShouldReturnAfterRide() const {
    // 復帰はステージ側で明示的に有効化した床に限ります。
    return param_.has_value() && param_->returnOnOff;
}

Vector3 GimmickHazardRideFloor::GetTravelDirection() const {
    // 配置時のヨー角を進行方向に変え、逆走モードでは符号を反転します。
    const float yaw = startRotation_.y;
    Vector3 direction = {std::cos(yaw), 0.0f, std::sin(yaw)};
    if (param_.has_value() && param_->actionMode == 1) {
        direction = direction * -1.0f;
    }
    return direction;
}

float GimmickHazardRideFloor::GetTravelSpeed() const {
    // 負値やゼロでも床が停止しないよう最低速度を保証します。
    return param_.has_value() ? (std::max)(0.1f, std::abs(param_->speed)) : kDefaultTravelSpeed;
}

float GimmickHazardRideFloor::GetTravelDistance() const {
    // 走行距離は正の値として扱います。
    return param_.has_value() ? (std::max)(0.1f, std::abs(param_->moveAmount)) : kDefaultTravelDistance;
}

float GimmickHazardRideFloor::GetStartDelay() const {
    // 発進前の予告はゼロ秒まで許可します。
    return param_.has_value() ? (std::max)(0.0f, param_->shakeDuration) : kDefaultStartDelay;
}

float GimmickHazardRideFloor::GetEndWarningDuration() const {
    // 終点の警告には最低時間を設け、即落下を避けます。
    return param_.has_value() ? (std::max)(0.1f, param_->interval) : kDefaultEndWarningDuration;
}

float GimmickHazardRideFloor::GetFallDuration() const {
    // 落下演出が少なくとも一フレーム続く長さにします。
    return param_.has_value() ? (std::max)(0.1f, param_->fallDuration) : kDefaultFallDuration;
}

float GimmickHazardRideFloor::GetFallGravity() const {
    // 落下加速度がゼロや負値にならないよう制限します。
    return param_.has_value() ? (std::max)(0.1f, param_->gravity) : kDefaultFallGravity;
}

int GimmickHazardRideFloor::GetHazardCount() const {
    // 途中イベントの数を上限内に収め、過剰な発火を防ぎます。
    return param_.has_value() ? (std::clamp)(param_->maxCount, 0, 16) : kDefaultHazardCount;
}
