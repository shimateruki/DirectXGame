#define NOMINMAX
#include "AttackTelegraph.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGroundLift = 0.055f;
constexpr float kCircleInnerRate = 0.72f;
constexpr float kCueOuterScale = 1.24f;

Vector3 NormalizePlanar(Vector3 direction) {
    direction.y = 0.0f;
    const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { direction.x / length, 0.0f, direction.z / length };
}

float EaseOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv;
}
}

void AttackTelegraph::Initialize(Object3dCommon* common) {
    common_ = common;
    EnsureEffect(common_);
}

void AttackTelegraph::EnsureEffect(Object3dCommon* common) {
    if (effect_ || !common) {
        return;
    }

    effect_ = std::make_unique<EffectObject3d>();
    effect_->Initialize(common);
    effect_->SetName("AttackTelegraph");
    effect_->SetBlendMode(BlendMode::kAdd);
    effect_->SetEnableReveal(false);
    effect_->SetEnableDistortion(false);
    effect_->SetEnableNoiseTexture(false);
    effect_->SetEnableColorRamp(false);
    effect_->SetIntensity(2.35f);
    effect_->SetEdgeFadeStrength(1.0f);
    effect_->SetAlphaReference(0.0f);
    ConfigureShape(shape_);
    effect_->SetIsVisible(false);
}

void AttackTelegraph::EnsureCueEffect(Object3dCommon* common) {
    if (cueEffect_ || !common) {
        return;
    }

    cueEffect_ = std::make_unique<EffectObject3d>();
    cueEffect_->Initialize(common);
    cueEffect_->SetName("AttackTelegraphCue");
    cueEffect_->SetBlendMode(BlendMode::kAdd);
    cueEffect_->SetEnableReveal(false);
    cueEffect_->SetEnableDistortion(false);
    cueEffect_->SetEnableNoiseTexture(false);
    cueEffect_->SetEnableColorRamp(false);
    cueEffect_->SetIntensity(4.4f);
    cueEffect_->SetEdgeFadeStrength(0.35f);
    cueEffect_->SetAlphaReference(0.0f);
    ConfigureCueShape(cueShape_);
    cueEffect_->SetIsVisible(false);
}

void AttackTelegraph::ConfigureEffectShape(EffectObject3d* effect, Shape shape) {
    if (!effect) {
        return;
    }

    if (shape == Shape::Circle) {
        effect->SetProceduralType(10);
        effect->editRingOuterRadius_ = 1.0f;
        effect->editRingInnerRadius_ = kCircleInnerRate;
        effect->editMeshSegments_ = 64;
    } else {
        effect->SetProceduralType(7);
        effect->editPlaneSize_ = { 1.0f, 1.0f };
        effect->editMeshSegments_ = 1;
    }
    effect->UpdateProceduralMesh();
}

void AttackTelegraph::ConfigureShape(Shape shape) {
    if (!effect_ || (isShapeConfigured_ && shape_ == shape)) {
        return;
    }

    shape_ = shape;
    isShapeConfigured_ = true;
    ConfigureEffectShape(effect_.get(), shape_);
}

void AttackTelegraph::ConfigureCueShape(Shape shape) {
    if (!cueEffect_ || (isCueShapeConfigured_ && cueShape_ == shape)) {
        return;
    }

    cueShape_ = shape;
    isCueShapeConfigured_ = true;
    ConfigureEffectShape(cueEffect_.get(), cueShape_);
}

Vector4 AttackTelegraph::MakeDisplayColor(const Vector4& color, float progress) const {
    progress = std::clamp(progress, 0.0f, 1.0f);
    const float pulse = 0.55f + std::sin(pulseTimer_ * (8.0f + progress * 8.0f)) * 0.45f;
    const float alpha = std::clamp(color.w * (0.42f + progress * 0.42f + pulse * 0.16f), 0.0f, 1.0f);
    return {
        std::clamp(color.x + progress * 0.18f, 0.0f, 1.0f),
        std::clamp(color.y * (1.0f - progress * 0.22f), 0.0f, 1.0f),
        std::clamp(color.z * (1.0f - progress * 0.20f), 0.0f, 1.0f),
        alpha
    };
}

Vector4 AttackTelegraph::MakeCueColor(const Vector4& color, float progress) const {
    progress = std::clamp(progress, 0.0f, 1.0f);
    const float flash = 1.0f - progress;
    const float alpha = std::clamp(color.w * flash * flash, 0.0f, 1.0f);
    return {
        std::clamp(color.x + flash * 0.35f, 0.0f, 1.0f),
        std::clamp(color.y * 0.25f + flash * 0.35f, 0.0f, 1.0f),
        std::clamp(color.z * 0.22f + flash * 0.20f, 0.0f, 1.0f),
        alpha
    };
}

void AttackTelegraph::ApplyCircle(EffectObject3d* effect, const Vector3& center, float radius, float progress, const Vector4& color, float scaleMultiplier) const {
    if (!effect) {
        return;
    }

    progress = std::clamp(progress, 0.0f, 1.0f);
    radius = (std::max)(radius, 0.05f);
    const float scaleRate = (0.58f + EaseOut(progress) * 0.42f) * (std::max)(scaleMultiplier, 0.01f);

    Vector3 pos = center;
    pos.y += kGroundLift;
    effect->SetTranslate(pos);
    effect->SetRotation({ 0.0f, 0.0f, 0.0f });
    effect->SetScale({ radius * scaleRate, 1.0f, radius * scaleRate });
    effect->SetColor(color);
    effect->UpdateLocalMatrix();
    effect->UpdateWorldMatrix();
}

void AttackTelegraph::ApplyLine(EffectObject3d* effect, const Vector3& center, const Vector3& direction, float length, float width, float progress, const Vector4& color, float scaleMultiplier) const {
    if (!effect) {
        return;
    }

    progress = std::clamp(progress, 0.0f, 1.0f);
    length = (std::max)(length, 0.05f);
    width = (std::max)(width, 0.05f);
    const Vector3 dir = NormalizePlanar(direction);
    const float yaw = std::atan2(dir.x, dir.z);
    const float scaleRate = (0.50f + EaseOut(progress) * 0.50f) * (std::max)(scaleMultiplier, 0.01f);

    Vector3 pos = center + dir * (length * 0.5f * scaleRate);
    pos.y += kGroundLift;
    effect->SetTranslate(pos);
    effect->SetRotation({ 0.0f, yaw, 0.0f });
    effect->SetScale({ width * scaleMultiplier, 1.0f, length * scaleRate });
    effect->SetColor(color);
    effect->UpdateLocalMatrix();
    effect->UpdateWorldMatrix();
}

void AttackTelegraph::ShowCircle(const Vector3& center, float radius, float progress, const Vector4& color) {
    EnsureEffect(common_);
    if (!effect_) {
        return;
    }

    ConfigureShape(Shape::Circle);
    ApplyCircle(effect_.get(), center, radius, progress, MakeDisplayColor(color, progress), 1.0f);
    effect_->SetIsVisible(true);
    isVisible_ = true;
    hasLastShape_ = true;
    lastShape_ = Shape::Circle;
    lastCenter_ = center;
    lastRadius_ = radius;
    lastColor_ = color;
}

void AttackTelegraph::ShowLine(const Vector3& center, const Vector3& direction, float length, float width, float progress, const Vector4& color) {
    EnsureEffect(common_);
    if (!effect_) {
        return;
    }

    ConfigureShape(Shape::Line);
    ApplyLine(effect_.get(), center, direction, length, width, progress, MakeDisplayColor(color, progress), 1.0f);
    effect_->SetIsVisible(true);
    isVisible_ = true;
    hasLastShape_ = true;
    lastShape_ = Shape::Line;
    lastCenter_ = center;
    lastDirection_ = NormalizePlanar(direction);
    lastLength_ = length;
    lastWidth_ = width;
    lastColor_ = color;
}

void AttackTelegraph::TriggerCue(const Vector4& color) {
    if (!hasLastShape_) {
        return;
    }
    EnsureCueEffect(common_);
    if (!cueEffect_) {
        return;
    }

    cueTimer_ = cueDuration_;
    cueColor_ = color;
    ConfigureCueShape(lastShape_);
    const Vector4 cueColor = MakeCueColor(color, 0.0f);
    if (lastShape_ == Shape::Circle) {
        ApplyCircle(cueEffect_.get(), lastCenter_, lastRadius_, 1.0f, cueColor, 1.0f);
    } else {
        ApplyLine(cueEffect_.get(), lastCenter_, lastDirection_, lastLength_, lastWidth_, 1.0f, cueColor, 1.0f);
    }
    cueEffect_->SetIsVisible(true);
}

void AttackTelegraph::TriggerCueAt(const Vector3& center, float radius, const Vector4& color) {
    EnsureCueEffect(common_);
    if (!cueEffect_) {
        return;
    }

    cueTimer_ = cueDuration_;
    cueColor_ = color;
    hasLastShape_ = true;
    lastShape_ = Shape::Circle;
    lastCenter_ = center;
    lastRadius_ = (std::max)(radius, 0.05f);
    lastColor_ = color;

    ConfigureCueShape(Shape::Circle);
    ApplyCircle(cueEffect_.get(), lastCenter_, lastRadius_, 1.0f, MakeCueColor(color, 0.0f), 1.0f);
    cueEffect_->SetIsVisible(true);
}

void AttackTelegraph::Hide() {
    isVisible_ = false;
    if (effect_) {
        effect_->SetIsVisible(false);
    }
}

void AttackTelegraph::Update(float deltaTime) {
    pulseTimer_ += (std::max)(deltaTime, 0.0f);
    if (effect_ && isVisible_) {
        effect_->UpdateLocalMatrix();
        effect_->UpdateWorldMatrix();
    }
    if (cueEffect_ && cueTimer_ > 0.0f && hasLastShape_) {
        cueTimer_ = (std::max)(0.0f, cueTimer_ - (std::max)(deltaTime, 0.0f));
        const float progress = 1.0f - std::clamp(cueTimer_ / cueDuration_, 0.0f, 1.0f);
        const float scaleMultiplier = 1.0f + (kCueOuterScale - 1.0f) * EaseOut(progress);
        const Vector4 cueColor = MakeCueColor(cueColor_, progress);
        if (lastShape_ == Shape::Circle) {
            ApplyCircle(cueEffect_.get(), lastCenter_, lastRadius_, 1.0f, cueColor, scaleMultiplier);
        } else {
            ApplyLine(cueEffect_.get(), lastCenter_, lastDirection_, lastLength_, lastWidth_, 1.0f, cueColor, scaleMultiplier);
        }
        cueEffect_->SetIsVisible(cueTimer_ > 0.0f);
    } else if (cueEffect_) {
        cueEffect_->SetIsVisible(false);
    }
}

void AttackTelegraph::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (!effect_ || !isVisible_ || !effect_->GetIsVisible()) {
        if (cueEffect_ && cueEffect_->GetIsVisible()) {
            cueEffect_->Draw(pointLightResource, spotLightResource);
        }
        return;
    }
    effect_->Draw(pointLightResource, spotLightResource);
    if (cueEffect_ && cueEffect_->GetIsVisible()) {
        cueEffect_->Draw(pointLightResource, spotLightResource);
    }
}
