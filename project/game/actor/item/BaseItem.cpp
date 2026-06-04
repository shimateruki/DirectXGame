#include "BaseItem.h"
#include "CollisionConfig.h"
#include "ModelManager.h"
#include "Player.h"
#include <cassert>

void BaseItem::Initialize(Object3dCommon* common, const std::string& modelName) {
    Object3d::Initialize(common);

    ModelManager::GetInstance()->LoadModel(modelName);
    SetModel(modelName);

    SetClassName("Item");
    SetSaveCategory("Object");
    SetItemType("");
    SetStatic(false);

    ColliderConfig config;
    config.type = ColliderType::kSphere;
    config.size = { 1.0f, 1.0f, 1.0f };
    SetColliderConfig(config);
    SetCollisionRadius(1.0f);

    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
}

void BaseItem::Update(float deltaTime) {
    if (isCollected_) {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        isDead = true;
        return;
    }

    Vector3 rotate = GetRotation();
    rotate.y += rotationSpeed_ * deltaTime;
    if (rotate.y > 6.283185f) {
        rotate.y -= 6.283185f;
    }
    SetRotation(rotate);

    Object3d::Update(deltaTime);
}

bool BaseItem::OnCollision(Object3d* other) {
    if (isCollected_ || !other) {
        return false;
    }

    Player* player = dynamic_cast<Player*>(other);
    if (!player) {
        return false;
    }

    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) {
        return false;
    }

    Collect(player);
    return true;
}

void BaseItem::Collect(Player* player) {
    (void)player;
    MarkCollected();
}

void BaseItem::MarkCollected() {
    isCollected_ = true;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    isDead = true;
}

std::unique_ptr<Object3d> BaseItem::Clone() const {
    auto newObj = std::make_unique<BaseItem>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
