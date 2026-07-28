#define NOMINMAX
#include "AttackTelegraph.h"
#include "Camera.h"
#include "CameraManager.h"
#include "TextureManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGroundLift = 0.055f;
constexpr float kCircleInnerRate = 0.72f;
constexpr float kCueOuterScale = 1.24f;
constexpr float kHalfPi = 1.57079632679f;
constexpr float kWarningCameraOffset = 0.14f;
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

    if (decalTexturePath_ != texturePath) {
        decalTexturePath_ = texturePath;
        isShapeConfigured_ = false;
        isCueShapeConfigured_ = false;
    }
    ConfigureShape(Shape::DecalCircle);
    ApplyDecalCircle(effect_.get(), center, radius, progress, MakeDecalColor(color, progress), 1.0f);
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
    if (cueEffect_ && cueEffect_->GetIsVisible()) {
        cueEffect_->Draw(pointLightResource, spotLightResource);
    }
}

void AttackTelegraph::DrawWarning(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (warningEffect_ && warningEffect_->GetIsVisible()) {
        warningEffect_->Draw(pointLightResource, spotLightResource);
    }
}
