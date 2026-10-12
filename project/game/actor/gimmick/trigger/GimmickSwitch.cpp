#include "GimmickSwitch.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <cmath>

void GimmickSwitch::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("Switch");
    SetName("Gimmick_Switch");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    // 押し板の石・金属・中央ボタンの素材色を保ちながら、起動状態を色で伝えます。
    SetColor({0.85f, 0.94f, 1.0f, 1.0f});
    SetScale({1.5f, 0.25f, 1.5f});

    if (!param_.has_value())
        param_.emplace();
}

void GimmickSwitch::Update(float deltaTime) {
    // 短い接触間隔では押下状態を保ち、踏んだ直後の瞬間的な判定抜けを吸収する。
    if (!param_.has_value())
        param_.emplace();

    if (contactTimer_ > 0.0f) {
        contactTimer_ -= deltaTime;
        if (contactTimer_ < 0.0f)
            contactTimer_ = 0.0f;
    }

    bool isPressed = contactTimer_ > 0.0f;
    bool pressedThisFrame = isPressed && !wasPressed_;
    int mode = GetSwitchMode();

    if (mode == 0) {
        SendActive(isPressed);
    } else if (mode == 1) {
        if (pressedThisFrame) {
            SendActive(!isOutputActive_);
        }
    } else if (mode == 3) {
        if (pressedThisFrame)
            SendActive(true);
    } else {
        if (pressedThisFrame) {
            timedTimer_ = param_->interval > 0.0f ? param_->interval : 3.0f;
            SendActive(true);
        }

        if (timedTimer_ > 0.0f) {
            timedTimer_ -= deltaTime;
            if (timedTimer_ <= 0.0f) {
                timedTimer_ = 0.0f;
                SendActive(false);
            }
        }
    }

    wasPressed_ = isPressed;

    if (isOutputActive_) {
        const float pulse = 0.86f + std::sin(timedTimer_ * 10.0f + contactTimer_ * 30.0f) * 0.06f;
        SetColor({0.72f * pulse, 1.0f, 0.78f * pulse, 1.0f});
    } else if (isPressed) {
        SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    } else {
        SetColor({0.85f, 0.94f, 1.0f, 1.0f});
    }

    BaseGimmick::Update(deltaTime);
}

bool GimmickSwitch::OnCollision(Object3d* other) {
    // プレイヤーが上から踏んだ接触だけ押下に数え、側面衝突を除外する。
    if (!other || other->GetClassName() != "Player")
        return true;

    CollisionInfo info = CheckCollision(other);
    if (info.isColliding && info.normal.y < -0.5f) {
        contactTimer_ = 0.12f;
    }

    return true;
}

void GimmickSwitch::SendActive(bool active) {
    // 状態変更を一度だけイベントとして配信し、連続接触での重複発火を防ぎます。
    if (isOutputActive_ == active)
        return;

    isOutputActive_ = active;
    int targetID = GetTargetID();
    if (targetID == -1)
        return;

    if (BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
        scene->SetEventActive(targetID, active);
    }
}

int GimmickSwitch::GetSwitchMode() const {
    // 未設定の配置物は通常モードとして扱う。
    return param_.has_value() ? param_->switchMode : 0;
}

std::unique_ptr<Object3d> GimmickSwitch::Clone() const {
    // スイッチ固有の設定も含めて配置物を複製する。
    auto newObj = std::make_unique<GimmickSwitch>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

void GimmickSwitch::ResetEscapeCycle() {
    // 再挑戦時は押下と時間制限を戻し、接続先へ解除を通知する。
    contactTimer_ = timedTimer_ = 0.0f;
    wasPressed_ = false;
    SendActive(false);
}
