#include "GimmickCoin.h"
#include "CollisionConfig.h"
#include "GameDataManager.h"
#include <DebugConsole.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kCollectAnimationDuration = 0.58f;
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
    collectAnimationTimer_ = 0.0f;
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
        }

        BaseGimmick::Update(deltaTime);
        return;
    }

    rotate.y += rotationSpeed_ * deltaTime;
    if (rotate.y > kPi * 2.0f) {
        rotate.y -= kPi * 2.0f;
    }
    SetRotation(rotate);

    BaseGimmick::Update(deltaTime);
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

std::unique_ptr<Object3d> GimmickCoin::Clone() const {
    auto newObj = std::make_unique<GimmickCoin>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
