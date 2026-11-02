#include "GimmickEventReceiver.h"
#include "Easing.h"
#include <numbers>
#include "ObjectPropertyDefinition.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickEventReceiver::Initialize(engine::Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // イベントを受けた際に表示・移動する地形なので、通常時の地面属性を保持します。
    SetClassName("Gimmick");
    SetGimmickType("EventReceiver");
    SetName("Gimmick_EventReceiver");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetColor({0.65f, 1.0f, 0.65f, 1.0f});
    SetStatic(false);

    if (!GetParameters().has_value())
        ResetParameters();

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickEventReceiver;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "イベント受信ギミック",
        "My Event IDとスイッチのTarget IDを合わせます。王冠落下は編集時は非表示で、撃破通知後にボス頭上から着地点へ落下します。");
    using Parameter = engine::Object3d::EntityParameter;
    properties.Parameter("動作モード", "actionMode", &Parameter::actionMode, 0.0f, 6.0f,
        nullptr, {"出現", "Y方向に移動", "X方向に移動", "Z方向に移動", "有効化", "無効化", "ボス王冠落下"});
    properties.Parameter("移動量", "moveAmount", &Parameter::moveAmount, -500.0f, 500.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode >= 1 && object.GetParameters()->actionMode <= 3); });
    properties.Parameter("移動速度", "moveSpeed", &Parameter::moveSpeed, 0.1f, 60.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode >= 1 && object.GetParameters()->actionMode <= 3); });
    properties.Parameter("ボスから着地点までの距離", "moveAmount_crown", &Parameter::moveAmount, 0.0f, 80.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 6); }, {}, {.parameterId = "moveAmount"});
    properties.Parameter("落下演出時間", "fallDuration", &Parameter::fallDuration, 0.35f, 6.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 6); });
    properties.Parameter("弧の高さ", "jumpPower", &Parameter::jumpPower, 0.0f, 12.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 6); });
    properties.Parameter("王冠の回転速度", "moveSpeed_crown", &Parameter::moveSpeed, -20.0f, 20.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 6); }, {}, {.parameterId = "moveSpeed"});
    properties.Parameter("開始時に有効", "startActive", &Parameter::startActive);
    properties.Parameter("OFFで元に戻す", "returnOnOff", &Parameter::returnOnOff);
}

void GimmickEventReceiver::Update(float deltaTime) {
    // 編集時の配置を再生基準にし、ボス報酬はイベント発火まで隠します。
    if (!GetParameters().has_value())
        ResetParameters();

    bool isPlaying = false;

    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        initializedForPlay_ = false;
        hasPendingActive_ = false;
        const bool isBossReward = GetParameters()->actionMode == 6;
        SetIsVisible(!isBossReward);
        SetCollisionEnabled(!isBossReward);
        BaseGimmick::Update(deltaTime);
        return;
    }

    // 最初の再生フレームで元の見た目と衝突設定を記録します。
    if (!initializedForPlay_) {
        basePosition_ = GetTransform()->translate;
        targetPosition_ = basePosition_;
        baseScale_ = GetScale();
        baseRotation_ = GetRotation();
        originalCollisionAttribute_ = GetCollisionAttribute();
        originalCollisionMask_ = GetCollisionMask();
        originalColor_ = GetColor();
        if (originalCollisionAttribute_ == 0)
            originalCollisionAttribute_ = CollisionCategories::Get("ground");
        if (originalCollisionMask_ == 0)
            originalCollisionMask_ = CollisionCategories::GetDefaultGameplayMask();

        active_ = hasPendingActive_ ? pendingActive_ : GetParameters()->startActive;
        hasPendingActive_ = false;
        ApplyActiveState(active_);
        initializedForPlay_ = true;
    }

    // 軸移動モードと報酬の出現演出は同じ受信オブジェクトで処理します。
    int actionMode = GetParameters()->actionMode;

    if (actionMode >= 1 && actionMode <= 3) {
        float speed = (std::max)(0.01f, GetParameters()->moveSpeed);
        float t = 1.0f - std::exp(-speed * deltaTime);
        engine::Vector3 pos = GetTransform()->translate;
        pos.x = engine::Math::Lerp(pos.x, targetPosition_.x, t);
        pos.y = engine::Math::Lerp(pos.y, targetPosition_.y, t);
        pos.z = engine::Math::Lerp(pos.z, targetPosition_.z, t);
        SetTranslate(pos);
    } else if (actionMode == 6 && active_) {
        activationTimer_ += (std::max)(0.0f, deltaTime);
        const float duration = (std::max)(0.35f, GetParameters()->fallDuration);
        const float progress = (std::clamp)(activationTimer_ / duration, 0.0f, 1.0f);
        const float eased = Easing::SmoothStep(progress);
        const engine::Vector3 startPosition = basePosition_ + engine::Vector3{0.0f, 4.05f, -GetParameters()->moveAmount};
        engine::Vector3 position = startPosition * (1.0f - eased) + basePosition_ * eased;
        position.y += std::sin(progress * std::numbers::pi_v<float>) * (std::max)(0.0f, GetParameters()->jumpPower);
        SetTranslate(position);

        engine::Vector3 rotation = baseRotation_;
        rotation.y += activationTimer_ * GetParameters()->moveSpeed;
        rotation.z += std::sin(progress * std::numbers::pi_v<float>) * 0.52f;
        SetRotation(rotation);

        const float startScale = 1.55f;
        const float scaleRate = startScale + (1.0f - startScale) * eased;
        SetScale(baseScale_ * scaleRate);
        SetCollisionEnabled(progress >= 1.0f);
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickEventReceiver::RelocateRuntimeBase(const engine::Vector3& position) {
    // 実行中の相対移動量を保ったまま、演出全体の基準位置だけを移します。
    const engine::Vector3 offset = position - (initializedForPlay_ ? basePosition_ : GetTranslate());
    basePosition_ = position;
    targetPosition_ = initializedForPlay_ ? targetPosition_ + offset : position;
    SetTranslate(GetTranslate() + offset);
    UpdateWorldMatrix(true);
}

void GimmickEventReceiver::OnTrigger() {
    // 単発トリガーはスイッチのオンイベントとして扱います。
    OnSwitchEvent(true);
}

void GimmickEventReceiver::OnSwitchEvent(bool active) {
    // 再生開始前に届いたイベントも、基準姿勢の取得後に適用できるよう保留します。
    if (!GetParameters().has_value())
        ResetParameters();

    if (!active && !GetParameters()->returnOnOff)
        return;
    if (!initializedForPlay_) {
        pendingActive_ = active;
        hasPendingActive_ = true;
        return;
    }

    ApplyActiveState(active);
}

void GimmickEventReceiver::ApplyActiveState(bool active) {
    // モード別に表示・衝突・移動先を切り替え、報酬出現時は着地まで衝突を遅らせます。
    active_ = active;
    activationTimer_ = 0.0f;
    int actionMode = GetParameters().has_value() ? GetParameters()->actionMode : 0;
    float moveAmount = GetParameters().has_value() ? GetParameters()->moveAmount : 10.0f;

    if (actionMode == 0) {
        SetIsVisible(active);
        SetCollisionEnabled(active);

        // エディタで設定した色を尊重し、報酬王冠などを汎用緑色へ上書きしません。
        SetColor(originalColor_);
    } else if (actionMode >= 1 && actionMode <= 3) {
        engine::Vector3 offset{};
        if (actionMode == 1)
            offset.y = moveAmount;
        if (actionMode == 2)
            offset.x = moveAmount;
        if (actionMode == 3)
            offset.z = moveAmount;
        targetPosition_ = active ? basePosition_ + offset : basePosition_;
        SetIsVisible(true);
        SetCollisionEnabled(true);
    } else if (actionMode == 4) {
        SetIsVisible(active);
        SetCollisionEnabled(active);
    } else if (actionMode == 5) {
        SetIsVisible(!active);
        SetCollisionEnabled(!active);
    } else if (actionMode == 6) {
        SetScale(baseScale_);
        SetRotation(baseRotation_);

        if (active) {
            SetTranslate(basePosition_ + engine::Vector3{0.0f, 4.05f, -moveAmount});
            SetIsVisible(true);
            SetCollisionEnabled(false);
        } else {
            SetTranslate(basePosition_);
            SetIsVisible(false);
            SetCollisionEnabled(false);
        }
    }
}

void GimmickEventReceiver::SetCollisionEnabled(bool enabled) {
    // 有効化時は編集データで指定された属性とマスクを復元します。
    if (enabled) {
        SetCollisionAttribute(originalCollisionAttribute_ != 0 ? originalCollisionAttribute_ : CollisionCategories::Get("ground"));
        SetCollisionMask(originalCollisionMask_ != 0 ? originalCollisionMask_ : CollisionCategories::GetDefaultGameplayMask());
    } else {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
    }
}

std::unique_ptr<engine::Object3d> GimmickEventReceiver::Clone() const {
    // 複製先を初期化してから、イベント設定と配置をコピーします。
    auto newObj = std::make_unique<GimmickEventReceiver>();
    assert(GetObjectCommon() != nullptr);
    newObj->Initialize(GetObjectCommon(), GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
