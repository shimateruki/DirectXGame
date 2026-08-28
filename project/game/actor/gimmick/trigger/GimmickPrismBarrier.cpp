#define NOMINMAX
#include "GimmickPrismBarrier.h"

#include "CollisionConfig.h"
#include "SceneManager.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
constexpr float kDefaultTransitionDuration = 0.42f;

float SmoothStep01(float value) {
    value = (std::clamp)(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}
}

void GimmickPrismBarrier::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("PrismBarrier");
    SetName("Gimmick_PrismBarrier");
    SetStatic(false);
    SetCastShadow(false);
    SetMaterialType(22);
    SetBlendMode(BlendMode::kNormal);
    SetEnableEnvMap(false);
    SetColor({ 0.30f, 0.86f, 1.0f, 0.84f });
    SetEmissive(2.4f);
    SetCollisionAttribute(CollisionAttribute::kGround);
    SetCollisionMask(0xffffffffu);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = { 0.0f, 0.0f, 0.0f };
    collider.size = { 1.0f, 1.0f, 0.45f };
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->gimmickType = "PrismBarrier";
    param_->moveSpeed = kDefaultTransitionDuration;
    param_->interval = 2.4f;
    param_->startActive = false;
    param_->returnOnOff = true;
}

void GimmickPrismBarrier::Update(float deltaTime) {
    SceneManager* sceneManager = SceneManager::GetInstance();
    const bool isPlaying = sceneManager && sceneManager->IsPlaying();
    if (!isPlaying) {
        if (!initializedForPlay_) {
            CaptureAuthoredState(true);
        }
        ResetForEditor();
        BaseGimmick::Update(deltaTime);
        return;
    }

    CaptureAuthoredState();

    if (!initializedForPlay_) {
        initializedForPlay_ = true;
        if (!activationRequestReceived_) {
            targetActivation_ = param_.has_value() && param_->startActive ? 1.0f : 0.0f;
        }
        activation_ = targetActivation_;
        ApplyRuntimeVisual();
    }

    visualTime_ += (std::max)(0.0f, deltaTime);
    const float duration = GetTransitionDuration();
    const float step = duration > 0.0001f ? deltaTime / duration : 1.0f;
    if (activation_ < targetActivation_) {
        activation_ = (std::min)(targetActivation_, activation_ + step);
    } else if (activation_ > targetActivation_) {
        activation_ = (std::max)(targetActivation_, activation_ - step);
    }
    ApplyRuntimeVisual();
    BaseGimmick::Update(deltaTime);
}

void GimmickPrismBarrier::OnTrigger() {
    OnSwitchEvent(true);
}

void GimmickPrismBarrier::OnSwitchEvent(bool active) {
    if (!param_.has_value()) {
        param_.emplace();
    }
    if (!active && !param_->returnOnOff) {
        return;
    }

    CaptureAuthoredState();
    activationRequestReceived_ = true;
    targetActivation_ = active ? 1.0f : 0.0f;
    if (active) {
        SetIsVisible(true);
    } else {
        SetCollisionEnabled(false);
        // 戦闘開始前の無効化はフェード元を描画せず、その場で完全に隠します。
        // 一度展開した後の解除だけは Update 側の縮退アニメーションを残します。
        if (!initializedForPlay_ || activation_ <= 0.005f) {
            activation_ = 0.0f;
            ApplyRuntimeVisual();
        }
    }
}

std::unique_ptr<Object3d> GimmickPrismBarrier::Clone() const {
    auto clone = std::make_unique<GimmickPrismBarrier>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickPrismBarrier::CaptureAuthoredState(bool force) {
    if (authoredStateCaptured_ && !force) {
        return;
    }

    authoredPosition_ = GetTransform()->translate;
    authoredScale_ = GetScale();
    authoredColor_ = GetColor();
    const uint32_t collisionAttribute = GetCollisionAttribute();
    const uint32_t collisionMask = GetCollisionMask();
    if (collisionAttribute != 0 || !authoredStateCaptured_) {
        authoredCollisionAttribute_ = collisionAttribute;
    }
    if (collisionMask != 0 || !authoredStateCaptured_) {
        authoredCollisionMask_ = collisionMask;
    }
    if (authoredCollisionAttribute_ == 0) {
        authoredCollisionAttribute_ = CollisionAttribute::kGround;
    }
    if (authoredCollisionMask_ == 0) {
        authoredCollisionMask_ = 0xffffffffu;
    }
    authoredStateCaptured_ = true;
}

void GimmickPrismBarrier::ResetForEditor() {
    initializedForPlay_ = false;
    activation_ = 0.0f;
    targetActivation_ = 0.0f;
    visualTime_ = 0.0f;
    activationRequestReceived_ = false;
    SetTranslate(authoredPosition_);
    SetScale(authoredScale_);
    SetColor(authoredColor_);
    SetCollisionEnabled(false);
    SetIsVisible(false);
}

void GimmickPrismBarrier::ApplyRuntimeVisual() {
    const float eased = SmoothStep01(activation_);
    const float minimumScaleY = (std::max)(0.015f, authoredScale_.y * 0.012f);
    const float currentScaleY = Math::Lerp(minimumScaleY, authoredScale_.y, eased);
    const float authoredBottom = authoredPosition_.y - authoredScale_.y;
    SetTranslate({ authoredPosition_.x, authoredBottom + currentScaleY, authoredPosition_.z });
    SetScale({ authoredScale_.x, currentScaleY, authoredScale_.z });

    const float pulseSpeed = param_.has_value() ? (std::max)(0.1f, param_->interval) : 2.4f;
    const float pulse = 0.5f + 0.5f * std::sin(visualTime_ * pulseSpeed * 6.2831853f);
    SetColor({
        authoredColor_.x,
        authoredColor_.y,
        authoredColor_.z,
        authoredColor_.w * eased,
    });
    SetEmissive((1.45f + pulse * 0.72f) * eased);

    if (auto* renderer = GetMeshRenderer(); renderer && renderer->GetWaterParamData()) {
        auto* effect = renderer->GetWaterParamData();
        effect->waveSpeed = 1.25f;
        effect->waveHeight = 0.72f;
        effect->waveFrequency = 18.0f;
        effect->effectType = 3.0f;
        effect->effectScale = 1.0f;
        effect->effectSoftness = 0.48f;
        effect->effectIntensity = 0.06f + eased * 1.72f;
        effect->billboardScale = 1.0f;
        effect->effectScaleX = 1.0f;
        effect->effectScaleY = 1.0f;
        effect->effectScaleZ = 0.08f;
        effect->uvOffsetX = targetActivation_ > activation_ ? 1.0f - activation_ : activation_ * 0.18f;
        effect->uvOffsetY = eased;
    }

    const bool shouldCollide = targetActivation_ > 0.5f && activation_ >= 0.22f;
    SetCollisionEnabled(shouldCollide);
    if (activation_ <= 0.005f && targetActivation_ <= 0.0f) {
        SetIsVisible(false);
    }
}

void GimmickPrismBarrier::SetCollisionEnabled(bool enabled) {
    SetCollisionAttribute(enabled ? authoredCollisionAttribute_ : 0);
    SetCollisionMask(enabled ? authoredCollisionMask_ : 0);
}

float GimmickPrismBarrier::GetTransitionDuration() const {
    return param_.has_value()
        ? (std::clamp)(param_->moveSpeed, 0.05f, 3.0f)
        : kDefaultTransitionDuration;
}
