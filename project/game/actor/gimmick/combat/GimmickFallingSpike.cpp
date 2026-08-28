#define NOMINMAX
#include "GimmickFallingSpike.h"

#include "CollisionConfig.h"
#include "Event.h"
#include "EventManager.h"
#include "Player.h"
#include "SceneManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kDefaultWarningDuration = 0.55f;
constexpr float kDefaultDropDistance = 16.0f;
constexpr float kDefaultGravity = 55.0f;
constexpr float kDefaultDamage = 8.0f;
constexpr float kEmbeddedDuration = 0.8f;
}

void GimmickFallingSpike::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("FallingSpike");
    SetName("Gimmick_FallingSpike");
    SetColor({ 0.62f, 0.95f, 1.0f, 1.0f });
    SetEmissive(1.25f);
    SetRoughness(0.26f);
    SetMetallic(0.18f);
    SetStatic(false);
    SetCollisionAttribute(0);
    SetCollisionMask(0);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = { 0.0f, 0.0f, 0.0f };
    collider.size = { 1.0f, 1.0f, 1.0f };
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->shakeDuration = kDefaultWarningDuration;
    param_->moveAmount = kDefaultDropDistance;
    param_->gravity = kDefaultGravity;
    param_->speed = kDefaultDamage;
    param_->jumpPower = 9.0f;
    param_->moveSpeed = 7.0f;
    param_->startActive = false;
    // 従来の一度きりの挙動を既定とし、必要なステージだけ明示的に再利用を有効化する。
    param_->returnOnOff = false;

    warningTelegraph_.Initialize(common);
}

void GimmickFallingSpike::Update(float deltaTime) {
    CaptureStartTransform();

    const bool isPlaying = SceneManager::GetInstance() && SceneManager::GetInstance()->IsPlaying();
    if (!isPlaying) {
        ResetRuntimeState();
        BaseGimmick::Update(deltaTime);
        return;
    }

    damageCooldown_ = (std::max)(0.0f, damageCooldown_ - deltaTime);
    timer_ += deltaTime;

    switch (state_) {
    case State::Dormant:
        SetIsVisible(false);
        warningTelegraph_.Hide();
        break;

    case State::Warning: {
        SetIsVisible(true);
        const float progress = std::clamp(timer_ / GetWarningDuration(), 0.0f, 1.0f);
        const Vector3 landingPosition = startPosition_ - Vector3{ 0.0f, GetDropDistance(), 0.0f };
        warningTelegraph_.ShowCircle(
            landingPosition + Vector3{ 0.0f, 0.035f, 0.0f },
            1.7f,
            progress,
            { 0.40f, 0.88f, 1.0f, 1.0f });
        const float pulse = 0.5f + 0.5f * std::sin(timer_ * 24.0f);
        SetEmissive(1.0f + pulse * 2.1f);
        GetTransform()->translate = startPosition_;
        GetTransform()->translate.y += std::sin(timer_ * 30.0f) * 0.08f;
        if (timer_ >= GetWarningDuration()) {
            state_ = State::Falling;
            timer_ = 0.0f;
            fallVelocityY_ = 0.0f;
            SetCollisionAttribute(kEnemyAttack);
            SetCollisionMask(kPlayer);
            SetEmissive(1.35f);
        }
        break;
    }

    case State::Falling:
        warningTelegraph_.Hide();
        fallVelocityY_ -= GetGravity() * deltaTime;
        GetTransform()->translate.y += fallVelocityY_ * deltaTime;
        if (GetTransform()->translate.y <= startPosition_.y - GetDropDistance()) {
            GetTransform()->translate.y = startPosition_.y - GetDropDistance();
            state_ = State::Embedded;
            timer_ = 0.0f;
            SetCollisionAttribute(kEnemyAttack);
            SetCollisionMask(kPlayer);
        }
        break;

    case State::Embedded:
        warningTelegraph_.Hide();
        SetEmissive(1.0f + (1.0f - (std::min)(timer_ / kEmbeddedDuration, 1.0f)) * 0.55f);
        if (timer_ >= kEmbeddedDuration) {
            state_ = State::Hidden;
            SetIsVisible(false);
            SetCollisionAttribute(0);
            SetCollisionMask(0);
        }
        break;

    case State::Hidden:
        warningTelegraph_.Hide();
        // 輸送床が再出発する前に待機状態へ戻し、次のイベントを確実に受け取れるようにする。
        if (ShouldReturnAfterUse()) {
            ResetRuntimeState();
        }
        break;
    }

    warningTelegraph_.Update((std::max)(0.0f, deltaTime));
    BaseGimmick::Update(deltaTime);
}

void GimmickFallingSpike::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    warningTelegraph_.DrawGround(pointLightResource, spotLightResource);
    BaseGimmick::Draw(pointLightResource, spotLightResource);
}

bool GimmickFallingSpike::OnCollision(Object3d* other) {
    if ((state_ != State::Falling && state_ != State::Embedded) || damageCooldown_ > 0.0f) {
        return true;
    }

    Player* player = dynamic_cast<Player*>(other);
    if (!player || !CheckCollision(player).isColliding) {
        return true;
    }

    ApplyPlayerDamage(player);
    damageCooldown_ = 0.45f;
    return true;
}

void GimmickFallingSpike::OnTrigger() {
    if (state_ != State::Dormant) {
        return;
    }
    CaptureStartTransform();
    state_ = State::Warning;
    timer_ = 0.0f;
    SetIsVisible(true);
}

std::unique_ptr<Object3d> GimmickFallingSpike::Clone() const {
    auto clone = std::make_unique<GimmickFallingSpike>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickFallingSpike::CaptureStartTransform() {
    if (hasCapturedStartTransform_) {
        return;
    }
    startPosition_ = GetTransform()->translate;
    startRotation_ = GetTransform()->rotate;
    hasCapturedStartTransform_ = true;
    if (param_.has_value() && param_->startActive) {
        state_ = State::Warning;
        SetIsVisible(true);
    }
}

void GimmickFallingSpike::ResetRuntimeState() {
    state_ = State::Dormant;
    timer_ = 0.0f;
    fallVelocityY_ = 0.0f;
    damageCooldown_ = 0.0f;
    GetTransform()->translate = startPosition_;
    GetTransform()->rotate = startRotation_;
    GetTransform()->isQuaternionMaster = false;
    SetIsVisible(false);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetEmissive(1.25f);
    warningTelegraph_.Hide();
    warningTelegraph_.Update(0.0f);
}

bool GimmickFallingSpike::ShouldReturnAfterUse() const {
    return param_.has_value() && param_->returnOnOff;
}

void GimmickFallingSpike::ApplyPlayerDamage(Object3d* playerObject) {
    Vector3 away = playerObject->GetWorldPosition() - GetWorldPosition();
    away.y = 0.0f;
    if (Math::Length(away) < 0.001f) {
        away = { 0.0f, 0.0f, 1.0f };
    }
    away = Math::Normalize(away);

    DamageEvent event;
    event.target = playerObject;
    event.attacker = this;
    event.damageAmount = GetDamage();
    event.knockbackVelocity = {
        away.x * param_->moveSpeed,
        (std::max)(0.0f, param_->jumpPower),
        away.z * param_->moveSpeed,
    };
    event.damageType = DamageType::Physical;
    EventManager::GetInstance()->Dispatch(event);
}

float GimmickFallingSpike::GetWarningDuration() const {
    return param_.has_value() ? (std::max)(0.05f, param_->shakeDuration) : kDefaultWarningDuration;
}

float GimmickFallingSpike::GetDropDistance() const {
    return param_.has_value() ? (std::max)(0.1f, std::abs(param_->moveAmount)) : kDefaultDropDistance;
}

float GimmickFallingSpike::GetGravity() const {
    return param_.has_value() ? (std::max)(1.0f, param_->gravity) : kDefaultGravity;
}

float GimmickFallingSpike::GetDamage() const {
    return param_.has_value() ? (std::max)(0.0f, param_->speed) : kDefaultDamage;
}
