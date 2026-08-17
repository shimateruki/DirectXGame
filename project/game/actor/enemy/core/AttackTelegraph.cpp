#define NOMINMAX
#include "AttackTelegraph.h"
#include "Camera.h"
#include "CameraManager.h"
#include "TextureManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGroundLift = 0.055f;
constexpr float kTimingLayerLift = 0.012f;
constexpr float kCircleInnerRate = 0.72f;
constexpr float kCueOuterScale = 1.24f;
constexpr float kHalfPi = 1.57079632679f;
constexpr float kWarningCameraOffset = 0.14f;
constexpr std::size_t kConeSegmentCount = 8;
constexpr float kCountdownOuterScale = 1.72f;
constexpr float kTimingMarkerMinLength = 0.18f;
constexpr float kTimingMarkerMaxLength = 0.78f;
constexpr const char* kAttackWarningEffectPath = "Resources/json/effect/effect_attack_warning_flash.json";
constexpr const char* kAttackWarningTexturePath = "Resources/sprite/effect/attack_warning_flash.dds";
constexpr const char* kWhiteTexturePath = "Resources/sprite/common/white.dds";

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
    // 初回攻撃の瞬間にDDS読込が走らないよう、シーンロード中に共有テクスチャを温めます。
    TextureManager::GetInstance()->Load(kAttackWarningTexturePath);
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

void AttackTelegraph::EnsureWarningEffect(Object3dCommon* common) {
    if (warningEffect_ || !common) {
        return;
    }

    warningEffect_ = std::make_unique<EffectObject3d>();
    warningEffect_->Initialize(common);
    warningEffect_->SetName("AttackWarningCue");
    if (!warningEffect_->LoadFromJson(kAttackWarningEffectPath)) {
        // JSONを編集中でも予兆自体が消えないよう、最小構成へフォールバックします。
        warningEffect_->SetBlendMode(BlendMode::kAdd);
        warningEffect_->SetEnableReveal(false);
        warningEffect_->SetEnableDistortion(false);
        warningEffect_->SetEnableNoiseTexture(false);
        warningEffect_->SetIntensity(5.2f);
        warningEffect_->SetEdgeFadeStrength(0.35f);
        warningEffect_->SetAlphaReference(0.008f);
        warningEffect_->SetProceduralType(7);
        warningEffect_->editPlaneSize_ = { 1.0f, 1.0f };
        warningEffect_->editMeshSegments_ = 1;
        warningEffect_->UpdateProceduralMesh();
        warningEffect_->SetTexture(kAttackWarningTexturePath);
        warningEffect_->SetStartScale({ 0.52f, 0.52f, 0.52f });
        warningEffect_->SetEndScale({ 1.18f, 1.18f, 1.18f });
        warningEffect_->SetStartColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        warningEffect_->SetEndColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        warningEffect_->SetEasingType(5);
        warningEffect_->Play(0.18f);
    }
    warningEffect_->SetIsVisible(false);
}

void AttackTelegraph::EnsurePatternEffects(std::size_t count) {
    if (!common_) {
        return;
    }

    while (patternEffects_.size() < count) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common_);
        effect->SetName("AttackTelegraphPattern_" + std::to_string(patternEffects_.size()));
        effect->SetBlendMode(BlendMode::kAdd);
        effect->SetEnableReveal(false);
        effect->SetEnableDistortion(false);
        effect->SetEnableNoiseTexture(false);
        effect->SetEnableColorRamp(false);
        effect->SetIntensity(2.35f);
        effect->SetEdgeFadeStrength(1.0f);
        effect->SetAlphaReference(0.0f);
        ConfigureEffectShape(effect.get(), patternShape_);
        effect->SetIsVisible(false);
        patternEffects_.push_back(std::move(effect));
    }
}

void AttackTelegraph::EnsureTimingEffects(std::size_t count) {
    if (!common_) {
        return;
    }

    while (timingEffects_.size() < count) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common_);
        effect->SetName("AttackTelegraphTiming_" + std::to_string(timingEffects_.size()));
        effect->SetEnableReveal(false);
        effect->SetEnableDistortion(false);
        effect->SetEnableNoiseTexture(false);
        effect->SetEnableColorRamp(false);
        effect->SetAlphaReference(0.0f);
        ConfigureTimingEffectShape(effect.get(), timingShape_);
        effect->SetIsVisible(false);
        timingEffects_.push_back(std::move(effect));
    }
}

void AttackTelegraph::BeginPattern(Shape shape, std::size_t count) {
    if (effect_) {
        effect_->SetIsVisible(false);
    }

    const bool shapeChanged = !isPatternShapeConfigured_ || patternShape_ != shape;
    patternShape_ = shape;
    EnsurePatternEffects(count);
    if (shapeChanged) {
        for (auto& effect : patternEffects_) {
            ConfigureEffectShape(effect.get(), patternShape_);
        }
        isPatternShapeConfigured_ = true;
    }

    patternActiveCount_ = (std::min)(count, patternEffects_.size());
    for (std::size_t index = 0; index < patternEffects_.size(); ++index) {
        patternEffects_[index]->SetIsVisible(index < patternActiveCount_);
    }
    isVisible_ = patternActiveCount_ > 0;
    hasLastShape_ = false;
}

void AttackTelegraph::BeginTiming(Shape shape, std::size_t count) {
    const bool shapeChanged = !isTimingShapeConfigured_ || timingShape_ != shape;
    timingShape_ = shape;
    EnsureTimingEffects(count);
    if (shapeChanged) {
        for (auto& effect : timingEffects_) {
            ConfigureTimingEffectShape(effect.get(), timingShape_);
        }
        isTimingShapeConfigured_ = true;
    }

    timingActiveCount_ = (std::min)(count, timingEffects_.size());
    for (std::size_t index = 0; index < timingEffects_.size(); ++index) {
        timingEffects_[index]->SetIsVisible(index < timingActiveCount_);
    }
}

void AttackTelegraph::HidePatternEffects() {
    patternActiveCount_ = 0;
    patternCueTimer_ = 0.0f;
    for (auto& effect : patternEffects_) {
        effect->SetIsVisible(false);
    }
}

void AttackTelegraph::HideTimingEffects() {
    timingActiveCount_ = 0;
    for (auto& effect : timingEffects_) {
        effect->SetIsVisible(false);
    }
}

void AttackTelegraph::ConfigureEffectShape(EffectObject3d* effect, Shape shape) {
    if (!effect) {
        return;
    }

    const bool isCue = effect == cueEffect_.get();
    if (shape == Shape::Circle) {
        effect->SetProceduralType(10);
        effect->editRingOuterRadius_ = 1.0f;
        effect->editRingInnerRadius_ = kCircleInnerRate;
        effect->editMeshSegments_ = 64;
        effect->SetTexture(kWhiteTexturePath);
        effect->SetBlendMode(BlendMode::kAdd);
        effect->SetIntensity(isCue ? 4.4f : 2.35f);
        effect->SetEdgeFadeStrength(isCue ? 0.35f : 1.0f);
    } else if (shape == Shape::DecalCircle) {
        effect->SetProceduralType(7);
        effect->editPlaneSize_ = { 2.0f, 2.0f };
        effect->editMeshSegments_ = 1;
        effect->SetTexture(decalTexturePath_);
        effect->SetBlendMode(isCue ? BlendMode::kAdd : BlendMode::kNormal);
        effect->SetIntensity(isCue ? 2.25f : 1.16f);
        effect->SetEdgeFadeStrength(0.0f);
    } else {
        effect->SetProceduralType(7);
        effect->editPlaneSize_ = { 1.0f, 1.0f };
        effect->editMeshSegments_ = 1;
        effect->SetTexture(kWhiteTexturePath);
        effect->SetBlendMode(BlendMode::kAdd);
        effect->SetIntensity(isCue ? 4.4f : 2.35f);
        effect->SetEdgeFadeStrength(isCue ? 0.35f : 1.0f);
    }
    effect->UpdateProceduralMesh();
}

void AttackTelegraph::ConfigureTimingEffectShape(EffectObject3d* effect, Shape shape) {
    if (!effect) {
        return;
    }

    ConfigureEffectShape(effect, shape);
    effect->SetBlendMode(BlendMode::kAdd);
    effect->SetIntensity(5.1f);
    effect->SetEdgeFadeStrength(0.22f);
    if (shape == Shape::Circle) {
        effect->editRingInnerRadius_ = 0.86f;
        effect->UpdateProceduralMesh();
    }
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

Vector4 AttackTelegraph::MakeDecalColor(const Vector4& color, float progress) const {
    progress = std::clamp(progress, 0.0f, 1.0f);
    const float pulse = 0.5f + std::sin(pulseTimer_ * (5.0f + progress * 3.0f)) * 0.5f;
    const float alpha = std::clamp(color.w * (0.54f + progress * 0.30f + pulse * 0.10f), 0.0f, 1.0f);
    return { color.x, color.y, color.z, alpha };
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

void AttackTelegraph::ApplyDecalCircle(EffectObject3d* effect, const Vector3& center, float radius, float progress, const Vector4& color, float scaleMultiplier) const {
    if (!effect) {
        return;
    }

    progress = std::clamp(progress, 0.0f, 1.0f);
    radius = (std::max)(radius, 0.05f);
    scaleMultiplier = (std::max)(scaleMultiplier, 0.01f);

    Vector3 position = center;
    position.y += kGroundLift + 0.006f;
    effect->SetTranslate(position);
    effect->SetRotation({ 0.0f, pulseTimer_ * 0.12f + progress * 0.08f, 0.0f });
    effect->SetScale({ radius * scaleMultiplier, 1.0f, radius * scaleMultiplier });
    effect->SetColor(color);
    effect->UpdateLocalMatrix();
    effect->UpdateWorldMatrix();
}

Vector4 AttackTelegraph::MakeTimingColor(const Vector4& color, float progress) const {
    progress = std::clamp(progress, 0.0f, 1.0f);
    const float urgency = std::clamp((progress - 0.56f) / 0.44f, 0.0f, 1.0f);
    const float urgencySmooth = urgency * urgency * (3.0f - 2.0f * urgency);
    const float pulse = 0.5f + std::sin(pulseTimer_ * (10.0f + urgencySmooth * 22.0f)) * 0.5f;
    const float whiteRate = 0.20f + urgencySmooth * 0.56f;
    return {
        std::clamp(color.x + (1.0f - color.x) * whiteRate, 0.0f, 1.0f),
        std::clamp(color.y + (1.0f - color.y) * whiteRate, 0.0f, 1.0f),
        std::clamp(color.z + (1.0f - color.z) * whiteRate, 0.0f, 1.0f),
        std::clamp(color.w * (0.72f + urgencySmooth * 0.18f + pulse * 0.10f), 0.0f, 1.0f)
    };
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

void AttackTelegraph::ApplyLineTimingMarker(
    EffectObject3d* effect,
    const Vector3& origin,
    const Vector3& direction,
    float length,
    float width,
    float progress,
    const Vector4& color) const {
    if (!effect) {
        return;
    }

    length = (std::max)(length, 0.05f);
    width = (std::max)(width, 0.05f);
    progress = std::clamp(progress, 0.0f, 1.0f);
    const Vector3 normalizedDirection = NormalizePlanar(direction);
    const float markerLength = std::clamp(length * 0.075f, kTimingMarkerMinLength, kTimingMarkerMaxLength);
    const float startDistance = std::clamp(
        length * progress - markerLength * 0.5f,
        0.0f,
        (std::max)(0.0f, length - markerLength));
    ApplyLine(
        effect,
        origin + normalizedDirection * startDistance,
        normalizedDirection,
        markerLength,
        width * 1.08f,
        1.0f,
        color,
        1.0f);
}

void AttackTelegraph::ShowCircleTiming(
    const Vector3& center,
    float radius,
    float progress,
    const Vector4& color) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    BeginTiming(Shape::Circle, 1);
    const float countdownScale = kCountdownOuterScale - (kCountdownOuterScale - 1.0f) * progress;
    Vector3 timingCenter = center;
    timingCenter.y += kTimingLayerLift;
    ApplyCircle(
        timingEffects_[0].get(),
        timingCenter,
        radius,
        1.0f,
        MakeTimingColor(color, progress),
        countdownScale);
}

void AttackTelegraph::ShowLineTiming(
    const Vector3& origin,
    const Vector3& direction,
    float length,
    float width,
    float progress,
    const Vector4& color) {
    BeginTiming(Shape::Line, 1);
    Vector3 timingOrigin = origin;
    timingOrigin.y += kTimingLayerLift;
    ApplyLineTimingMarker(
        timingEffects_[0].get(),
        timingOrigin,
        direction,
        length,
        width,
        progress,
        MakeTimingColor(color, progress));
}

void AttackTelegraph::ShowCircle(const Vector3& center, float radius, float progress, const Vector4& color) {
    EnsureEffect(common_);
    if (!effect_) {
        return;
    }

    HidePatternEffects();
    ConfigureShape(Shape::Circle);
    ApplyCircle(effect_.get(), center, radius, 1.0f, MakeDisplayColor(color, progress), 1.0f);
    ShowCircleTiming(center, radius, progress, color);
    effect_->SetIsVisible(true);
    isVisible_ = true;
    hasLastShape_ = true;
    lastShape_ = Shape::Circle;
    lastCenter_ = center;
    lastRadius_ = radius;
    lastColor_ = color;
}

void AttackTelegraph::ShowDecalCircle(
    const Vector3& center,
    float radius,
    float progress,
    const Vector4& color,
    const std::string& texturePath) {
    EnsureEffect(common_);
    if (!effect_ || texturePath.empty()) {
        return;
    }

    HidePatternEffects();
    if (decalTexturePath_ != texturePath) {
        decalTexturePath_ = texturePath;
        isShapeConfigured_ = false;
        isCueShapeConfigured_ = false;
    }
    ConfigureShape(Shape::DecalCircle);
    ApplyDecalCircle(effect_.get(), center, radius, progress, MakeDecalColor(color, progress), 1.0f);
    ShowCircleTiming(center, radius, progress, color);
    effect_->SetIsVisible(true);
    isVisible_ = true;
    hasLastShape_ = true;
    lastShape_ = Shape::DecalCircle;
    lastCenter_ = center;
    lastRadius_ = radius;
    lastColor_ = color;
}

void AttackTelegraph::ShowLine(const Vector3& center, const Vector3& direction, float length, float width, float progress, const Vector4& color) {
    EnsureEffect(common_);
    if (!effect_) {
        return;
    }

    HidePatternEffects();
    ConfigureShape(Shape::Line);
    ApplyLine(effect_.get(), center, direction, length, width, 1.0f, MakeDisplayColor(color, progress), 1.0f);
    ShowLineTiming(center, direction, length, width, progress, color);
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

void AttackTelegraph::ShowCone(
    const Vector3& origin,
    const Vector3& direction,
    float length,
    float startWidth,
    float endWidth,
    float progress,
    const Vector4& color) {
    length = (std::max)(length, 0.05f);
    startWidth = (std::max)(startWidth, 0.05f);
    endWidth = (std::max)(endWidth, startWidth);
    progress = std::clamp(progress, 0.0f, 1.0f);

    BeginPattern(Shape::Line, kConeSegmentCount);
    const Vector3 normalizedDirection = NormalizePlanar(direction);
    const float segmentLength = length / static_cast<float>(kConeSegmentCount);
    const Vector4 displayColor = MakeDisplayColor(color, progress);

    for (std::size_t index = 0; index < kConeSegmentCount; ++index) {
        EffectObject3d* segment = patternEffects_[index].get();
        const float startRate = static_cast<float>(index) / static_cast<float>(kConeSegmentCount);
        const float centerRate = (static_cast<float>(index) + 0.5f) / static_cast<float>(kConeSegmentCount);
        const float segmentWidth = startWidth + (endWidth - startWidth) * centerRate;
        Vector4 segmentColor = displayColor;
        segmentColor.w *= 0.72f + centerRate * 0.28f;
        ApplyLine(
            segment,
            origin + normalizedDirection * (length * startRate),
            normalizedDirection,
            segmentLength * 1.08f,
            segmentWidth,
            1.0f,
            segmentColor,
            1.0f);
        segment->SetIsVisible(true);
    }

    const float timingWidth = startWidth + (endWidth - startWidth) * progress;
    ShowLineTiming(origin, normalizedDirection, length, timingWidth, progress, color);
}

void AttackTelegraph::ShowImpactAreas(
    const Vector3* centers,
    std::size_t centerCount,
    float radius,
    float progress,
    const Vector4& color) {
    if (!centers || centerCount == 0) {
        Hide();
        return;
    }

    radius = (std::max)(radius, 0.05f);
    progress = std::clamp(progress, 0.0f, 1.0f);
    BeginPattern(Shape::Circle, centerCount);
    BeginTiming(Shape::Circle, centerCount);
    const Vector4 displayColor = MakeDisplayColor(color, progress);
    const float sequenceStep = centerCount > 1
        ? (std::min)(0.105f, 0.38f / static_cast<float>(centerCount - 1))
        : 0.0f;

    for (std::size_t index = 0; index < centerCount; ++index) {
        const float localProgress = std::clamp(
            progress - static_cast<float>(index) * sequenceStep,
            0.0f,
            1.0f);
        EffectObject3d* area = patternEffects_[index].get();
        Vector4 areaColor = displayColor;
        areaColor.w *= 0.68f + localProgress * 0.32f;
        ApplyCircle(area, centers[index], radius, 1.0f, areaColor, 1.0f);
        area->SetIsVisible(true);

        const float countdownScale = kCountdownOuterScale -
            (kCountdownOuterScale - 1.0f) * localProgress;
        Vector3 timingCenter = centers[index];
        timingCenter.y += kTimingLayerLift;
        ApplyCircle(
            timingEffects_[index].get(),
            timingCenter,
            radius,
            1.0f,
            MakeTimingColor(color, localProgress),
            countdownScale);
    }
}

void AttackTelegraph::ShowLaneFan(
    const Vector3& origin,
    const Vector3& direction,
    float length,
    float width,
    int laneCount,
    float lateralSpacing,
    float angleStep,
    float progress,
    const Vector4& color) {
    laneCount = (std::max)(1, laneCount);
    length = (std::max)(length, 0.05f);
    width = (std::max)(width, 0.05f);
    progress = std::clamp(progress, 0.0f, 1.0f);

    BeginPattern(Shape::Line, static_cast<std::size_t>(laneCount));
    BeginTiming(Shape::Line, static_cast<std::size_t>(laneCount));
    const Vector3 baseDirection = NormalizePlanar(direction);
    const Vector3 side = { baseDirection.z, 0.0f, -baseDirection.x };
    const float centerIndex = static_cast<float>(laneCount - 1) * 0.5f;
    const Vector4 displayColor = MakeDisplayColor(color, progress);

    for (int index = 0; index < laneCount; ++index) {
        const float centeredIndex = static_cast<float>(index) - centerIndex;
        const float angle = centeredIndex * angleStep;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const Vector3 laneDirection = NormalizePlanar({
            baseDirection.x * cosine + baseDirection.z * sine,
            0.0f,
            -baseDirection.x * sine + baseDirection.z * cosine,
        });
        ApplyLine(
            patternEffects_[static_cast<std::size_t>(index)].get(),
            origin + side * (centeredIndex * lateralSpacing),
            laneDirection,
            length,
            width,
            1.0f,
            displayColor,
            1.0f);
        ApplyLineTimingMarker(
            timingEffects_[static_cast<std::size_t>(index)].get(),
            origin + side * (centeredIndex * lateralSpacing) + Vector3{ 0.0f, kTimingLayerLift, 0.0f },
            laneDirection,
            length,
            width,
            progress,
            MakeTimingColor(color, progress));
    }
}

void AttackTelegraph::TriggerCue(const Vector4& color) {
    if (patternActiveCount_ > 0) {
        patternCueTimer_ = cueDuration_;
        patternCueColor_ = color;
        return;
    }
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
    } else if (lastShape_ == Shape::DecalCircle) {
        ApplyDecalCircle(cueEffect_.get(), lastCenter_, lastRadius_, 1.0f, cueColor, 1.0f);
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

void AttackTelegraph::TriggerWarningCue(Object3d* target, const Vector3& offset, float size, const Vector4& accentColor) {
    EnsureWarningEffect(common_);
    if (!warningEffect_) {
        return;
    }

    warningTarget_ = target;
    warningOffset_ = offset;
    warningCenter_ = target ? target->GetWorldPosition() + offset : offset;
    warningSize_ = (std::max)(size, 0.1f);
    warningAccentColor_ = {
        std::clamp(accentColor.x, 0.0f, 1.0f),
        std::clamp(accentColor.y, 0.0f, 1.0f),
        std::clamp(accentColor.z, 0.0f, 1.0f),
        std::clamp(accentColor.w, 0.0f, 1.0f)
    };
    warningEffect_->Restart();
    warningEffect_->SetIsVisible(true);
}

void AttackTelegraph::Hide() {
    isVisible_ = false;
    if (effect_) {
        effect_->SetIsVisible(false);
    }
    HidePatternEffects();
    HideTimingEffects();
}

void AttackTelegraph::Update(float deltaTime) {
    pulseTimer_ += (std::max)(deltaTime, 0.0f);
    if (effect_ && isVisible_) {
        effect_->UpdateLocalMatrix();
        effect_->UpdateWorldMatrix();
    }
    for (std::size_t index = 0; index < patternActiveCount_; ++index) {
        if (patternEffects_[index]->GetIsVisible()) {
            patternEffects_[index]->UpdateLocalMatrix();
            patternEffects_[index]->UpdateWorldMatrix();
        }
    }
    for (std::size_t index = 0; index < timingActiveCount_; ++index) {
        if (timingEffects_[index]->GetIsVisible()) {
            timingEffects_[index]->UpdateLocalMatrix();
            timingEffects_[index]->UpdateWorldMatrix();
        }
    }
    if (patternCueTimer_ > 0.0f && patternActiveCount_ > 0) {
        patternCueTimer_ = (std::max)(0.0f, patternCueTimer_ - (std::max)(deltaTime, 0.0f));
        const float progress = 1.0f - std::clamp(patternCueTimer_ / cueDuration_, 0.0f, 1.0f);
        const Vector4 cueColor = MakeCueColor(patternCueColor_, progress);
        for (std::size_t index = 0; index < patternActiveCount_; ++index) {
            if (patternEffects_[index]->GetIsVisible()) {
                patternEffects_[index]->SetColor(cueColor);
            }
        }
    }
    if (cueEffect_ && cueTimer_ > 0.0f && hasLastShape_) {
        cueTimer_ = (std::max)(0.0f, cueTimer_ - (std::max)(deltaTime, 0.0f));
        const float progress = 1.0f - std::clamp(cueTimer_ / cueDuration_, 0.0f, 1.0f);
        const float scaleMultiplier = 1.0f + (kCueOuterScale - 1.0f) * EaseOut(progress);
        const Vector4 cueColor = MakeCueColor(cueColor_, progress);
        if (lastShape_ == Shape::Circle) {
            ApplyCircle(cueEffect_.get(), lastCenter_, lastRadius_, 1.0f, cueColor, scaleMultiplier);
        } else if (lastShape_ == Shape::DecalCircle) {
            ApplyDecalCircle(cueEffect_.get(), lastCenter_, lastRadius_, 1.0f, cueColor, scaleMultiplier);
        } else {
            ApplyLine(cueEffect_.get(), lastCenter_, lastDirection_, lastLength_, lastWidth_, 1.0f, cueColor, scaleMultiplier);
        }
        cueEffect_->SetIsVisible(cueTimer_ > 0.0f);
    } else if (cueEffect_) {
        cueEffect_->SetIsVisible(false);
    }
    UpdateWarningEffect(deltaTime);
}

void AttackTelegraph::UpdateWarningEffect(float deltaTime) {
    if (!warningEffect_ || !warningEffect_->GetIsVisible()) {
        return;
    }

    warningEffect_->Update((std::max)(deltaTime, 0.0f));
    const float progress = warningEffect_->GetPlaybackProgress();
    if (!warningEffect_->IsPlaying() && progress >= 1.0f) {
        warningEffect_->SetIsVisible(false);
        return;
    }

    const Vector3 presetScale = warningEffect_->GetScale();
    const auto* material = warningEffect_->GetMaterialData();
    const Vector4 presetColor = material ? material->color : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f - progress };

    // 最初は全敵共通の白金色、消える直前だけ属性色を残して判別しやすくします。
    const float accentRate = progress * progress;
    const Vector4 commonColor = { 1.0f, 0.96f, 0.64f, 1.0f };
    const Vector4 tint = {
        commonColor.x + (warningAccentColor_.x - commonColor.x) * accentRate,
        commonColor.y + (warningAccentColor_.y - commonColor.y) * accentRate,
        commonColor.z + (warningAccentColor_.z - commonColor.z) * accentRate,
        warningAccentColor_.w
    };

    warningCenter_ = warningTarget_ ? warningTarget_->GetWorldPosition() + warningOffset_ : warningCenter_;
    Vector3 position = warningCenter_;
    float yaw = 0.0f;
    float pitch = 0.0f;
    Camera* camera = CameraManager::GetInstance() ? CameraManager::GetInstance()->GetActiveCamera() : nullptr;
    if (camera) {
        Vector3 toCamera = camera->GetEye() - position;
        const float length = std::sqrt(toCamera.x * toCamera.x + toCamera.y * toCamera.y + toCamera.z * toCamera.z);
        if (length > 0.001f) {
            toCamera = toCamera / length;
            position = position + toCamera * kWarningCameraOffset;
            yaw = std::atan2(toCamera.x, toCamera.z);
            const float planarLength = std::sqrt(toCamera.x * toCamera.x + toCamera.z * toCamera.z);
            pitch = std::atan2(toCamera.y, (std::max)(planarLength, 0.001f));
        }
    }

    warningEffect_->SetTranslate(position);
    warningEffect_->SetRotation({ kHalfPi - pitch, yaw, 0.0f });
    warningEffect_->SetScale({
        presetScale.x * warningSize_,
        presetScale.y,
        presetScale.z * warningSize_
    });
    warningEffect_->SetColor({
        presetColor.x * tint.x,
        presetColor.y * tint.y,
        presetColor.z * tint.z,
        presetColor.w * tint.w
    });
    warningEffect_->UpdateLocalMatrix();
    warningEffect_->UpdateWorldMatrix();
}

void AttackTelegraph::DrawGround(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (effect_ && isVisible_ && effect_->GetIsVisible()) {
        effect_->Draw(pointLightResource, spotLightResource);
    }
    for (std::size_t index = 0; index < patternActiveCount_; ++index) {
        if (patternEffects_[index]->GetIsVisible()) {
            patternEffects_[index]->Draw(pointLightResource, spotLightResource);
        }
    }
    for (std::size_t index = 0; index < timingActiveCount_; ++index) {
        if (timingEffects_[index]->GetIsVisible()) {
            timingEffects_[index]->Draw(pointLightResource, spotLightResource);
        }
    }
    if (cueEffect_ && cueEffect_->GetIsVisible()) {
        cueEffect_->Draw(pointLightResource, spotLightResource);
    }
}

void AttackTelegraph::DrawWarning(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (warningEffect_ && warningEffect_->GetIsVisible()) {
        warningEffect_->Draw(pointLightResource, spotLightResource);
    }
}
