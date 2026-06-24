#include "GimmickCoin.h"
#include "CollisionConfig.h"
#include "GameDataManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include <DebugConsole.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kCollectAnimationDuration = 0.58f;
constexpr float kDropGravity = 22.0f;
constexpr float kDropBounceRate = 0.42f;
constexpr float kDropGroundFriction = 0.86f;
constexpr float kDropStopSpeed = 0.45f;
}

void GimmickCoin::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(1.0f);
    SetColor({ 1.0f, 0.9f, 0.0f, 1.0f });
    SetScale({ 0.6f, 0.6f, 0.15f });

    isCollected_ = false;
    isTemporaryDrop_ = false;
    collectAnimationTimer_ = 0.0f;
    dropAge_ = 0.0f;
    dropLifetime_ = 0.0f;
    dropBlinkStartTime_ = 0.0f;
    dropGroundY_ = GetTranslate().y;
    dropSettleTimer_ = 0.0f;
    dropVelocity_ = { 0.0f, 0.0f, 0.0f };
    collectStartPosition_ = GetTranslate();
    collectStartScale_ = GetScale();

    SetClassName("Gimmick");
    SetGimmickType("Coin");
}

void GimmickCoin::Update(float deltaTime) {
    Vector3 rotate = GetRotation();

    if (isCollected_) {
        collectAnimationTimer_ += deltaTime;
        const float t = std::clamp(collectAnimationTimer_ / kCollectAnimationDuration, 0.0f, 1.0f);
        const float easeOut = 1.0f - (1.0f - t) * (1.0f - t);
        const float hop = std::sin(t * kPi);

        Vector3 position = collectStartPosition_;
        position.y += 1.25f * easeOut + 0.25f * hop;
        SetTranslate(position);

        rotate.y += (rotationSpeed_ + 18.0f) * deltaTime;
        rotate.z += 10.0f * deltaTime;
        SetRotation(rotate);

        const float popScale = 1.0f + 0.18f * hop;
        const float vanish = 1.0f - std::clamp((t - 0.55f) / 0.45f, 0.0f, 1.0f);
        SetScale({
            collectStartScale_.x * popScale * vanish,
            collectStartScale_.y * popScale * vanish,
            collectStartScale_.z * popScale * vanish
        });
        SetColor({ 1.0f, 0.92f, 0.18f, vanish });

        if (t >= 1.0f) {
            SetIsVisible(false);
            isDead = true;
            RequestSelfRemove();
        }

        BaseGimmick::Update(deltaTime);
        return;
    }

    if (isTemporaryDrop_) {
        UpdateTemporaryDrop(deltaTime);
        if (isDead) {
            BaseGimmick::Update(deltaTime);
            return;
        }
    }

    rotate.y += rotationSpeed_ * deltaTime;
    if (rotate.y > kPi * 2.0f) {
        rotate.y -= kPi * 2.0f;
    }
    SetRotation(rotate);

    BaseGimmick::Update(deltaTime);
}

void GimmickCoin::ConfigureTemporaryDrop(const Vector3& initialVelocity, float lifetime, float blinkStartTime, float groundY) {
    isTemporaryDrop_ = true;
    dropVelocity_ = initialVelocity;
    dropLifetime_ = (std::max)(0.2f, lifetime);
    dropBlinkStartTime_ = std::clamp(blinkStartTime, 0.0f, dropLifetime_);
    dropAge_ = 0.0f;
    dropGroundY_ = groundY;
    dropSettleTimer_ = 0.0f;
    SetColor({ 1.0f, 0.92f, 0.18f, 1.0f });
    SetIsVisible(true);
}

void GimmickCoin::UpdateTemporaryDrop(float deltaTime) {
    dropAge_ += deltaTime;

    if (dropAge_ >= dropLifetime_) {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        isDead = true;
        RequestSelfRemove();
        return;
    }

    Vector3 position = GetTranslate();
    if (dropSettleTimer_ <= 0.0f) {
        dropVelocity_.y -= kDropGravity * deltaTime;
        position += dropVelocity_ * deltaTime;

        if (position.y <= dropGroundY_) {
            position.y = dropGroundY_;
            if (std::abs(dropVelocity_.y) > kDropStopSpeed) {
                dropVelocity_.y = -dropVelocity_.y * kDropBounceRate;
                dropVelocity_.x *= kDropGroundFriction;
                dropVelocity_.z *= kDropGroundFriction;
            } else {
                dropVelocity_.y = 0.0f;
                dropSettleTimer_ = 0.01f;
            }
        }
    } else {
        dropSettleTimer_ += deltaTime;
        dropVelocity_.x *= std::pow(kDropGroundFriction, deltaTime * 18.0f);
        dropVelocity_.z *= std::pow(kDropGroundFriction, deltaTime * 18.0f);
        position.x += dropVelocity_.x * deltaTime;
        position.z += dropVelocity_.z * deltaTime;
    }
    SetTranslate(position);

    const float remaining = dropLifetime_ - dropAge_;
    if (dropBlinkStartTime_ > 0.0f && remaining <= dropBlinkStartTime_) {
        const float blinkProgress = 1.0f - std::clamp(remaining / dropBlinkStartTime_, 0.0f, 1.0f);
        const float blinkSpeed = 8.0f + blinkProgress * 18.0f;
        const bool visible = std::sin(dropAge_ * blinkSpeed) >= -0.25f;
        SetIsVisible(visible);
        const float alpha = visible ? (0.45f + 0.55f * (1.0f - blinkProgress * 0.35f)) : 0.0f;
        SetColor({ 1.0f, 0.92f, 0.18f, alpha });
    } else {
        SetIsVisible(true);
        SetColor({ 1.0f, 0.92f, 0.18f, 1.0f });
    }
}

bool GimmickCoin::OnCollision(Object3d* other) {
    if (isCollected_) {
        return false;
    }

    if (other->GetCollisionAttribute() & CollisionAttribute::kPlayer) {
        Collect();
        return true;
    }

    return false;
}

void GimmickCoin::Collect() {
    if (isCollected_) {
        return;
    }

    isCollected_ = true;
    collectAnimationTimer_ = 0.0f;
    collectStartPosition_ = GetTranslate();
    collectStartScale_ = GetScale();
    SetCollisionAttribute(0);
    SetCollisionMask(0);

    GameDataManager::GetInstance()->AddCoin(1);

    const int currentCoins = GameDataManager::GetInstance()->GetCoins();
    const int currentLives = GameDataManager::GetInstance()->GetLives();
    DebugConsole::GetInstance()->AddLog(
        "Coin Collected! (" + std::to_string(currentCoins) + "/100) Lives: " + std::to_string(currentLives)
    );
}

void GimmickCoin::RequestSelfRemove() {
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (scene) {
        scene->RequestRemoveObject(this);
    }
}

std::unique_ptr<Object3d> GimmickCoin::Clone() const {
    auto newObj = std::make_unique<GimmickCoin>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
