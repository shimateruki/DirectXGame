#include "GimmickMovingFloor.h"
#include <algorithm>
#include <cmath>
#include "CollisionConfig.h"
#include "RidingPlatformController.h"
#include "MovingFloorSupport.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include "Player.h"
#include "game/actor/gimmick/combat/GimmickMagmaGeyser.h"

GimmickMovingFloor::GimmickMovingFloor() = default;
GimmickMovingFloor::~GimmickMovingFloor() = default;

bool GimmickMovingFloor::IsRidePlatform() const {
    const auto* path = GetPathMoverComponent();
    return path && !path->GetRideSequence().empty();
}

namespace {
constexpr float kLegacyMoveRange = 3.0f;
constexpr float kMaxFloatAmplitude = 2.0f;
constexpr float kFloatPitchScale = 0.025f;
constexpr float kFloatRollScale = 0.035f;
constexpr float kTwoPi = 6.28318530718f;
}

void GimmickMovingFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("MovingFloor");
    
    if (!param_.has_value()) param_.emplace();
    if (param_->speed == 0.0f) param_->speed = 2.0f; // デフォルト速度
    
    // 当たり判定設定 (地面として扱う)
    SetCollisionAttribute(kGround);
}

void GimmickMovingFloor::Update(float deltaTime) {
    if (!SceneManager::GetInstance()->IsPlaying()) {
        // 編集中の配置を次回再生の原点にする。手続き移動でInspectorの座標を上書きしない。
        startPosition_ = GetTranslate();
        startRotation_ = GetRotation();
        hasCapturedStartTransform_ = true;
        time_ = 0.0f;
        frameDelta_ = {};
        if (param_ && param_->actionMode == 5) SetEmissive(0.10f);
        BaseGimmick::Update(deltaTime);
        return;
    }
    if (IsRidePlatform()) {
        if (!ride_) ride_ = std::make_unique<RidingPlatformController>();
        ride_->Update(*this, deltaTime);
        BaseGimmick::Update(deltaTime);
        return;
    }
    std::vector<Character*> passengers;
    auto* scene = SceneManager::GetInstance()->GetCurrentScene();
    if (scene && SceneManager::GetInstance()->IsPlaying() && deltaTime > 0.0f) {
        for (auto& object : scene->GetObjects()) {
            auto* actor = object && !object->IsReplayRemoved() ? dynamic_cast<Character*>(object.get()) : nullptr;
            if (actor && IsStandingOnMovingFloor(*this, *actor)) passengers.push_back(actor);
        }
    }
    const auto carryPassengers = [&]() {
        // 押し戻し後の再判定には依存せず、移動前に接地していた乗員を一度だけ運ぶ。
        for (auto* actor : passengers) {
            actor->SetTranslate(actor->GetTranslate() + frameDelta_);
            actor->UpdateLocalMatrix();
            actor->UpdateWorldMatrix(false);
            if (auto* player = dynamic_cast<Player*>(actor)) player->SetPlatformMotion(frameDelta_ * (1.0f / deltaTime));
        }
    };
    // Ghost RecorderのPathが割り当てられている床は、記録済みの軌道を唯一の移動元にする。
    // 手続き的な往復移動を先に重ねると、Editorで調整したPathと乗り移り判定がずれる。
    if (!GetRecordPathName().empty()) {
        const Vector3 previousPosition = GetTransform()->translate;
        BaseGimmick::Update(deltaTime);
        frameDelta_ = GetTransform()->translate - previousPosition;
        // RecorderはObject更新の終盤で座標を変える。乗員の接地照会より前に反映する。
        UpdateWorldMatrix(false);
        hasCapturedStartTransform_ = true;
        carryPassengers();
        return;
    }

    // JSONのTransformはInitialize後に反映されるため、最初のUpdateで実際の配置を記録する。
    if (!hasCapturedStartTransform_) {
        startPosition_ = GetTransform()->translate;
        startRotation_ = GetTransform()->rotate;
        phaseOffset_ = std::fmod(std::abs(startPosition_.x * 0.037f + startPosition_.z * 0.061f), kTwoPi);
        hasCapturedStartTransform_ = true;
    }

    const Vector3 previousPosition = GetTransform()->translate;
    time_ += deltaTime;

    const float speed = param_.has_value() ? (std::max)(param_->speed, 0.0f) : 0.0f;
    const int actionMode = param_.has_value() ? param_->actionMode : 0;
    // 配置位置を往復運動の中心として扱う。位相差は自然な浮遊モードだけに適用する。
    const float waveTime = time_ * speed + (actionMode == 1 ? phaseOffset_ : 0.0f);
    Transform* transform = GetTransform();
    transform->translate = startPosition_;
    transform->rotate = startRotation_;

    if (actionMode == 5) {
        // Target IDで噴出口に接続する。未接続なら原点で待機する。
        if (scene && GetTargetID() >= 0 && SceneManager::GetInstance()->IsPlaying()) {
            for (const auto& object : scene->GetObjects()) {
                auto* geyser = dynamic_cast<GimmickMagmaGeyser*>(object.get());
                if (geyser && geyser->GetEventID() == GetTargetID()) {
                    SetEmissive(0.10f + geyser->GetWarningProgress() * 0.65f);
                    const float desiredY = startPosition_.y + geyser->GetLiftOffset();
                    const float maxStep = 12.0f * (std::max)(0.0f, deltaTime);
                    transform->translate.y = std::clamp(desiredY, previousPosition.y - maxStep, previousPosition.y + maxStep);
                    break;
                }
            }
        }
    } else if (actionMode == 1) {
        // 溶岩に浮く足場用。移動床の位置を保ったまま、緩やかな浮き沈みと傾きだけを加える。
        const float amplitude = (std::clamp)(std::abs(param_->moveAmount), 0.0f, kMaxFloatAmplitude);
        const float tiltWeight = (std::min)(amplitude, 1.0f);
        transform->translate.y += std::sin(waveTime) * amplitude;
        transform->rotate.x += std::sin(waveTime * 0.73f + 1.2f) * kFloatPitchScale * tiltWeight;
        transform->rotate.z += std::sin(waveTime * 0.91f) * kFloatRollScale * tiltWeight;
        transform->isQuaternionMaster = false;
    } else if (actionMode >= 2 && actionMode <= 4) {
        // ステージ制作用の往復移動。振幅と速度はInspectorから調整できる。
        const float travel = (std::max)(0.0f, std::abs(param_->moveAmount));
        const float offset = std::sin(waveTime) * travel;
        if (actionMode == 2) transform->translate.x += offset;
        if (actionMode == 3) transform->translate.z += offset;
        if (actionMode == 4) transform->translate.y += offset;
    } else {
        // 従来ステージとの互換性を保つ通常の上下移動。
        transform->translate.y += std::sin(waveTime) * kLegacyMoveRange;
    }

    frameDelta_ = hasCapturedStartTransform_ ? transform->translate - previousPosition : Vector3{};

    // 移動後のTransformを同じフレームで行列と当たり判定へ反映する。
    BaseGimmick::Update(deltaTime);
    carryPassengers();
}

bool GimmickMovingFloor::OnCollision(Object3d* other) {
    // 全移動床で更新前に乗員を運ぶ。衝突処理は押し戻しだけにする。
    (void)other;
    return true;
}

std::unique_ptr<Object3d> GimmickMovingFloor::Clone() const {
    // 複製時は配置と設定だけを引き継ぎ、乗員・周期・原点は再生時に再取得する。
    auto clone = std::make_unique<GimmickMovingFloor>();
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickMovingFloor::ResetEscapeCycle() {
    time_ = 0.0f;
    frameDelta_ = {};
    if (hasCapturedStartTransform_) {
        SetTranslate(startPosition_);
        UpdateLocalMatrix();
        UpdateWorldMatrix();
    }
}
