#include "GimmickPhaseFlipFloor.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickPhaseFlipFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
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

    if (!param_.has_value())
        param_.emplace();
    param_->colorType = 0;
    param_->maxCount = 3;
    param_->interval = 1.0f;
    param_->startActive = true;
}

void GimmickPhaseFlipFloor::Update(float deltaTime) {
    // 編集時は元の状態へ戻し、再生時だけ位相のタイマーを進めます。
    if (!param_.has_value())
        param_.emplace();

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

    const float direction = param_.has_value() && !param_->startActive ? -1.0f : 1.0f;
    float turn = static_cast<float>(flipCount_);
    if (isOwnPhase) {
        turn = static_cast<float>((std::max)(0, flipCount_ - 1)) + SmoothStep(phaseProgress);
    }
    SetColor(GetPhaseColor(1.0f));

    engine::Vector3 rot = baseRotation_;
    rot.x += direction * turn * 3.14159265f;
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
    int index = param_.has_value() ? param_->colorType : 0;
    return std::clamp(index, 0, phaseCount - 1);
}

int GimmickPhaseFlipFloor::GetPhaseCount() const {
    // ゼロで割らないよう、周期の位相数は一以上にします。
    int count = param_.has_value() ? param_->maxCount : 3;
    return (std::max)(1, count);
}

float GimmickPhaseFlipFloor::GetPhaseDuration() const {
    // 時間設定がゼロでも位相が進むよう、最短時間を設けます。
    float duration = param_.has_value() ? param_->interval : 1.0f;
    return (std::max)(0.1f, duration);
}

float GimmickPhaseFlipFloor::SmoothStep(float t) const {
    // 回転の開始と終了で角速度が急変しないようにします。
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::unique_ptr<engine::Object3d> GimmickPhaseFlipFloor::Clone() const {
    // 複製先の基本資源を作ってから配置と周期設定を移します。
    auto newObj = std::make_unique<GimmickPhaseFlipFloor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
