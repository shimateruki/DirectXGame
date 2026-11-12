#include "GimmickPhaseFlipFloor.h"
#include "Easing.h"
#include <numbers>
#include "ObjectPropertyDefinition.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickPhaseFlipFloor::Initialize(engine::Object3DCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // 床の衝突を維持したまま、色別の順番で回転する初期値を用意します。
    SetClassName("Gimmick");
    SetGimmickType("PhaseFlipFloor");
    SetName("Gimmick_PhaseFlipFloor");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetColor({0.35f, 0.75f, 1.0f, 1.0f});
    SetScale({2.0f, 0.25f, 2.0f});
    SetStatic(false);

    if (!GetParameters().has_value())
        ResetParameters();
    SetParameter(&engine::EntityParameter::colorType, 0);
    SetParameter(&engine::EntityParameter::maxCount, 3);
    SetParameter(&engine::EntityParameter::interval, 1.0f);
    SetParameter(&engine::EntityParameter::startActive, true);

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickPhaseFlipFloor;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "順番反転床",
        "床番号1→2→3…の順に、当たり判定を残したまま180度回転します。");
    using Parameter = engine::Object3D::EntityParameter;

    // 表示だけ1始まりにし、従来のcolorType（0始まり）をそのまま保存します。
    properties.Accessor<int>("床番号", "colorType", "param.colorType",
        [](const Self& object) { return object.GetParameters() ? object.GetParameters()->colorType : 0; },
        [](Self& object, const int& value) {
            if (!object.GetParameters()) object.ResetParameters();
            object.SetParameter(&engine::EntityParameter::colorType, std::clamp(value, 0, (std::max)(1, object.GetParameters()->maxCount) - 1));
        }, 0, 15, nullptr, {}, {.integerDisplayOffset = 1, .maximum = +[](const Self& object) {
            return static_cast<float>(object.GetParameters() ? (std::max)(1, object.GetParameters()->maxCount) - 1 : 0);
        }});
    properties.Accessor<int>("全体の床数", "maxCount", "param.maxCount",
        [](const Self& object) { return object.GetParameters() ? object.GetParameters()->maxCount : 3; },
        [](Self& object, const int& value) {
            if (!object.GetParameters()) object.ResetParameters();
            object.SetParameter(&engine::EntityParameter::maxCount, std::clamp(value, 1, 16));
            object.SetParameter(&engine::EntityParameter::colorType, std::clamp(object.GetParameters()->colorType, 0, object.GetParameters()->maxCount - 1));
        }, 1, 16);
    properties.Parameter("1フェーズの時間", "interval", &Parameter::interval, 0.1f, 30.0f);
    properties.Parameter("正方向に回転", "startActive", &Parameter::startActive);
}

void GimmickPhaseFlipFloor::Update(float deltaTime) {
    // 編集時は元の状態へ戻し、再生時だけ位相のタイマーを進めます。
    if (!GetParameters().has_value())
        ResetParameters();

    bool isPlaying = false;

    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        initializedForPlay_ = false;
        cycleTimer_ = 0.0f;
        flipCount_ = 0;
        wasOwnPhase_ = false;
        SetCollisionAttribute(CollisionCategories::Get("ground"));
        SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
        SetIsVisible(true);
        SetColor(GetPhaseColor(1.0f));
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        originalCollisionAttribute_ = GetCollisionAttribute();
        originalCollisionMask_ = GetCollisionMask();
        if (originalCollisionAttribute_ == 0)
            originalCollisionAttribute_ = CollisionCategories::Get("ground");
        if (originalCollisionMask_ == 0)
            originalCollisionMask_ = CollisionCategories::GetDefaultGameplayMask();
        baseRotation_ = GetTransform()->rotate;
        cycleTimer_ = 0.0f;
        flipCount_ = 0;
        wasOwnPhase_ = false;
        initializedForPlay_ = true;
    }

    cycleTimer_ += deltaTime;

    const int phaseCount = GetPhaseCount();
    const float phaseDuration = GetPhaseDuration();
    const float cycleLength = phaseDuration * static_cast<float>(phaseCount);

    if (cycleLength > 0.0f && cycleTimer_ >= cycleLength) {
        cycleTimer_ = std::fmod(cycleTimer_, cycleLength);
    }

    // 自分の位相へ入るたびに半回転を一回加え、途中経過は滑らかに補間します。
    const int currentPhase = static_cast<int>(cycleTimer_ / phaseDuration) % phaseCount;
    const float phaseProgress = std::fmod(cycleTimer_, phaseDuration) / phaseDuration;
    const bool isOwnPhase = currentPhase == GetPhaseIndex();

    if (isOwnPhase && !wasOwnPhase_) {
        flipCount_++;
    }

    wasOwnPhase_ = isOwnPhase;

    ApplyState(isOwnPhase, phaseProgress);
    BaseGimmick::Update(deltaTime);
}

void GimmickPhaseFlipFloor::ApplyState(bool isOwnPhase, float phaseProgress) {
    // 回転中も元の衝突設定を維持し、床が抜けないようにします。
    SetCollisionAttribute(originalCollisionAttribute_ != 0 ? originalCollisionAttribute_ : CollisionCategories::Get("ground"));
    SetCollisionMask(originalCollisionMask_ != 0 ? originalCollisionMask_ : CollisionCategories::GetDefaultGameplayMask());
    SetIsVisible(true);

    const float direction = GetParameters().has_value() && !GetParameters()->startActive ? -1.0f : 1.0f;
    float turn = static_cast<float>(flipCount_);

    if (isOwnPhase) {
        turn = static_cast<float>((std::max)(0, flipCount_ - 1)) + SmoothStep(phaseProgress);
    }

    SetColor(GetPhaseColor(1.0f));

    engine::Vector3 rot = baseRotation_;
    rot.x += direction * turn * std::numbers::pi_v<float>;
    SetEulerAngles(rot);
    SetQuaternionMaster(false);
}

engine::Vector4 GimmickPhaseFlipFloor::GetPhaseColor(float alpha) const {
    // 最大四つの位相を色で識別できるようにします。
    switch (GetPhaseIndex() % 4) {
    case 0:
        return {0.35f, 0.75f, 1.0f, alpha};

    case 1:
        return {1.0f, 0.65f, 0.25f, alpha};

    case 2:
        return {0.45f, 1.0f, 0.45f, alpha};

    default:
        return {0.95f, 0.55f, 1.0f, alpha};
    }
}

int GimmickPhaseFlipFloor::GetPhaseIndex() const {
    // 設定値が位相数を超えても存在する位相へ収めます。
    int phaseCount = GetPhaseCount();
    int index = GetParameters().has_value() ? GetParameters()->colorType : 0;
    return std::clamp(index, 0, phaseCount - 1);
}

int GimmickPhaseFlipFloor::GetPhaseCount() const {
    // ゼロで割らないよう、周期の位相数は一以上にします。
    int count = GetParameters().has_value() ? GetParameters()->maxCount : 3;
    return (std::max)(1, count);
}

float GimmickPhaseFlipFloor::GetPhaseDuration() const {
    // 時間設定がゼロでも位相が進むよう、最短時間を設けます。
    float duration = GetParameters().has_value() ? GetParameters()->interval : 1.0f;
    return (std::max)(0.1f, duration);
}

float GimmickPhaseFlipFloor::SmoothStep(float t) const {
    // 補間曲線と入力範囲の扱いを共通のEasingへ委譲する。
    return Easing::SmoothStep01(t);
}

std::unique_ptr<engine::Object3D> GimmickPhaseFlipFloor::Clone() const {
    // 複製先の基本資源を作ってから配置と周期設定を移します。
    auto newObj = std::make_unique<GimmickPhaseFlipFloor>();
    assert(GetObjectCommon() != nullptr);
    newObj->Initialize(GetObjectCommon(), GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
