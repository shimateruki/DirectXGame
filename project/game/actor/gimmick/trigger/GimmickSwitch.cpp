#include "GimmickSwitch.h"
#include "ObjectPropertyDefinition.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <cmath>

void GimmickSwitch::Initialize(engine::Object3DCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("Switch");
    SetName("Gimmick_Switch");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());

    // 押し板の石・金属・中央ボタンの素材色を保ちながら、起動状態を色で伝えます。
    SetColor({0.85f, 0.94f, 1.0f, 1.0f});
    SetScale({1.5f, 0.25f, 1.5f});

    if (!GetParameters().has_value())
        ResetParameters();

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickSwitch;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "汎用スイッチ",
        "Target IDと受信側のMy Event IDを合わせてください。");
    using Parameter = engine::Object3D::EntityParameter;
    properties.Parameter("スイッチ方式", "switchMode", &Parameter::switchMode, 0.0f, 3.0f,
        nullptr, {"押している間だけ", "押すたび切替", "一定時間だけ", "一度押すと保持"});
    properties.Parameter("有効時間", "interval", &Parameter::interval, 0.1f, 60.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->switchMode == 2); });
}

void GimmickSwitch::Update(float deltaTime) {
    // 短い接触間隔では押下状態を保ち、踏んだ直後の瞬間的な判定抜けを吸収する。
    if (!GetParameters().has_value())
        ResetParameters();

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
            timedTimer_ = GetParameters()->interval > 0.0f ? GetParameters()->interval : 3.0f;
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

bool GimmickSwitch::OnCollision(engine::Object3D* other) {
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
    int targetId = GetTargetId();
    if (targetId == -1)
        return;

    if (BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
        scene->SetEventActive(targetId, active);
    }
}

int GimmickSwitch::GetSwitchMode() const {
    // 未設定の配置物は通常モードとして扱う。
    return GetParameters().has_value() ? GetParameters()->switchMode : 0;
}

std::unique_ptr<engine::Object3D> GimmickSwitch::Clone() const {
    // スイッチ固有の設定も含めて配置物を複製する。
    auto newObj = std::make_unique<GimmickSwitch>();
    assert(GetObjectCommon() != nullptr);
    newObj->Initialize(GetObjectCommon(), GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

void GimmickSwitch::ResetEscapeCycle() {
    // 再挑戦時は押下と時間制限を戻し、接続先へ解除を通知する。
    contactTimer_ = timedTimer_ = 0.0f;
    wasPressed_ = false;
    SendActive(false);
}
