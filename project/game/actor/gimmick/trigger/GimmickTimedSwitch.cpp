#include "GimmickTimedSwitch.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <cmath>

void GimmickTimedSwitch::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 上面を踏める地面として登録し、待機時の色を設定する。
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("TimedSwitch");
    SetName("Gimmick_TimedSwitch");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetColor({0.25f, 0.75f, 1.0f, 1.0f});
    SetScale({1.5f, 0.25f, 1.5f});
}

void GimmickTimedSwitch::Update(float deltaTime) {
    // 接触が途切れたら短い保持時間後に待機色へ戻す。
    if (contactTimer_ > 0.0f) {
        contactTimer_ -= deltaTime;
        if (contactTimer_ < 0.0f)
            contactTimer_ = 0.0f;
    }

    bool isPressed = contactTimer_ > 0.0f;
    if (isPressed) {
        float pulse = 0.85f + std::sin(contactTimer_ * 35.0f) * 0.15f;
        SetColor({0.2f, 1.0f, 0.45f + pulse * 0.2f, 1.0f});
    } else {
        SetColor({0.25f, 0.75f, 1.0f, 1.0f});
    }

    BaseGimmick::Update(deltaTime);
}

// 上面への接触中だけタイマーを更新し、接続先イベントを通知します。
bool GimmickTimedSwitch::OnCollision(Object3d* other) {
    // 上面から踏まれた間だけ指定 EventID へ通知する。
    if (!other || other->GetClassName() != "Player")
        return true;

    CollisionInfo info = CheckCollision(other);
    if (info.isColliding && info.normal.y < -0.5f) {
        contactTimer_ = 0.12f;

        int targetID = GetTargetID();
        if (targetID != -1) {
            if (BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
                scene->TriggerEvent(targetID);
            }
        }
    }

    return true;
}

std::unique_ptr<Object3d> GimmickTimedSwitch::Clone() const {
    // 押下タイマーは持ち込まず、配置と接続先の設定だけを複製する。
    auto newObj = std::make_unique<GimmickTimedSwitch>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
