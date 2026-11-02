#include "MeshRenderer.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "LightManager.h"
#include "TextureManager.h"
#include "engine/graphics/core/ColorSpace.h"
#include "DebugConsole.h"
#include <cassert>
#include <SrvManager.h>

#include <algorithm>
#include <cmath>

namespace {

// 非有限値と極端な繰り返し数を材質定数へ渡さないよう制限します。
float ClampTextureTiling(float value) {
    if (!std::isfinite(value)) {
        return 1.0f;
    }

    return std::clamp(value, 0.01f, 1000.0f);
}

engine::Vector2 MakeAutoTilingFromScale(const engine::Vector3& scale) {
    // 材質設定のUV倍率はスケールの絶対値から大きい二軸を採用する。
    float a = std::abs(scale.x);
    float b = std::abs(scale.y);
    float c = std::abs(scale.z);

    if (a < b)
        std::swap(a, b);
    if (a < c)
        std::swap(a, c);
    if (b < c)
        std::swap(b, c);

    return { (std::max)(a, 0.01f), (std::max)(b, 0.01f) };
}

}

// ========================================================================
// MeshRenderer マテリアル設定
// ------------------------------------------------------------------------
// 色、PBR値、通常/ORM/アルベドテクスチャ、UVタイリング、環境反射を担当する。
// Inspectorから編集される見た目の値は、基本的にこのファイルへ集める。
// ========================================================================
// GPU色だけを戻すと次のUpdateで上書きされるため、入力色と描画方式も同時に復元する。
MeshRenderer::MaterialState MeshRenderer::CaptureMaterialState() const {
    MaterialState state;
    if (materialData_) {
        state.parameters_ = *materialData_;
        state.authoringColor_ = materialColor_;
        state.blendMode_ = blendMode_;
        state.valid_ = true;
    }
    return state;
}

void MeshRenderer::RestoreMaterialState(const MaterialState& state) {
    if (!materialData_ || !state.valid_) return;
    *materialData_ = state.parameters_;
    SetColor(state.authoringColor_);
    SetBlendMode(state.blendMode_);
}

void MeshRenderer::SetPortalClip(const PortalClipSettings& settings) {
    if (!materialData_) return;
    const auto finite = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    auto normal = settings.normal;
    const float length = engine::Math::Length(normal);
    normal = std::isfinite(length) && length > 0.0001f ? normal * (1.0f / length) : engine::Vector3{0, 0, 1};
    materialData_->portalClipEnabled = settings.enabled ? 1.0f : 0.0f;
    materialData_->portalClipProgress = std::clamp(finite(settings.progress, 0.0f), 0.0f, 1.0f);
    materialData_->portalClipCenter = {finite(settings.center.x, 0), finite(settings.center.y, 0), finite(settings.center.z, 0)};
    materialData_->portalClipNormal = normal;
    materialData_->portalClipEdgeWidth = (std::max)(0.0001f, finite(settings.edgeWidth, 0.12f));
    materialData_->portalClipDissolve = (std::max)(0.0f, finite(settings.dissolve, 0.18f));
    // HDRの縁色は1を超える値も演出で使うため、上限を設けない。
    materialData_->portalClipColor = {finite(settings.color.x, 1), finite(settings.color.y, 1),
        finite(settings.color.z, 1), std::clamp(finite(settings.color.w, 1), 0.0f, 1.0f)};
}

void MeshRenderer::ClearPortalClip() {
    if (materialData_) materialData_->portalClipEnabled = 0.0f;
}

void MeshRenderer::SetMaterialTime(float time) {
    if (materialData_ && std::isfinite(time)) materialData_->time = time;
}

void MeshRenderer::AdvanceMaterialTime(float deltaTime) {
    if (materialData_ && std::isfinite(deltaTime) && deltaTime > 0.0f)
        SetMaterialTime(materialData_->time + deltaTime);
}

void MeshRenderer::SetShininess(float shininess) {
    if (materialData_ && std::isfinite(shininess)) materialData_->shininess = (std::max)(0.0f, shininess);
}

void MeshRenderer::SetSelectedLighting(int32_t type) {
    if (materialData_) materialData_->selectedLighting = type;
}

void MeshRenderer::SetColor(const engine::Vector4& color) {
    materialColor_ = color;
    if (materialData_)
        materialData_->color = engine::ColorSpace::AuthoringToWorking(materialColor_);
}

void MeshRenderer::SetMaterialType(int32_t type) {
    if (materialData_)
        materialData_->materialType = type;
}

void MeshRenderer::SetIntensity(float intensity) {
    if (directionalLightData_)
        directionalLightData_->intensity = intensity;
}

engine::Vector4 MeshRenderer::GetColor() const {
    return materialColor_;
}

int32_t MeshRenderer::GetMaterialType() const {
    return materialData_ ? materialData_->materialType : 0;
}

void MeshRenderer::SetMetallic(float metallic) {
    if (materialData_)
        materialData_->metallic = metallic;
}

void MeshRenderer::SetRoughness(float roughness) {
    if (materialData_)
        materialData_->roughness = roughness;
}

float MeshRenderer::GetMetallic() const {
    return materialData_ ? materialData_->metallic : 0.0f;
}

float MeshRenderer::GetRoughness() const {
    return materialData_ ? materialData_->roughness : 0.3f;
}

void MeshRenderer::SetEnableNormalMap(bool enable) {
    if (materialData_)
        materialData_->enableNormalMap = enable ? 1 : 0;
}

bool MeshRenderer::GetEnableNormalMap() const {
    return materialData_ ? (materialData_->enableNormalMap == 1) : false;
}

void MeshRenderer::SetNormalMap(const std::string& texturePath) {
    // 法線テクスチャは色変換せず線形空間で読み、解除時は旧ハンドルを使わない。
    normalMapPath_ = texturePath;

    if (!texturePath.empty()) {
        normalMapHandle_ = TextureManager::GetInstance()->Load(texturePath, TextureManager::TextureColorSpace::Linear);
    } else {
        normalMapHandle_ = 0;
    }
}

void MeshRenderer::SetOrmMap(const std::string& texturePath) {
    // ORM の各成分は色ではなく係数なので線形空間で読み込む。
    ormMapPath_ = texturePath;

    if (!texturePath.empty()) {
        ormMapHandle_ = TextureManager::GetInstance()->Load(texturePath, TextureManager::TextureColorSpace::Linear);
    } else {
        ormMapHandle_ = 0;
    }
}


void MeshRenderer::SetTexture(const std::string& texturePath) {
    // ベースカラーだけは sRGB として読み込み、空パスで参照を解除する。
    texturePath_ = texturePath;

    if (!texturePath.empty()) {
        textureHandle_ = TextureManager::GetInstance()->Load(texturePath, TextureManager::TextureColorSpace::SRGB);
    } else {
        textureHandle_ = 0;
    }
}

void MeshRenderer::SetTextureTiling(const engine::Vector2& tiling) {
    // 無効な繰り返し率を補正してから UV 行列へ反映する。
    textureTiling_ = {
        ClampTextureTiling(tiling.x),
        ClampTextureTiling(tiling.y)
    };

    UpdateUvTransform();
}

void MeshRenderer::SetAutoTextureTiling(bool enabled) {
    autoTextureTiling_ = enabled;
    UpdateUvTransform();
}

void MeshRenderer::UpdateUvTransform() {
    // 手動タイリングへ必要に応じてオブジェクトの大きさを掛け、見た目の密度を保つ。
    if (!materialData_) {
        return;
    }

    engine::Vector2 effectiveTiling = {
        ClampTextureTiling(textureTiling_.x),
        ClampTextureTiling(textureTiling_.y)
    };

    if (autoTextureTiling_ && transform_) {
        engine::Vector2 scaleTiling = MakeAutoTilingFromScale(transform_->scale);
        effectiveTiling.x *= scaleTiling.x;
        effectiveTiling.y *= scaleTiling.y;
    }

    materialData_->uvTransform = engine::Math::MakeScaleMatrix({ effectiveTiling.x, effectiveTiling.y, 1.0f });
}

void MeshRenderer::SetEnableEnvMap(bool enable) {
    if (materialData_)
        materialData_->enableEnvMap = enable ? 1 : 0;
}

bool MeshRenderer::GetEnableEnvMap() const {
    return materialData_ ? (materialData_->enableEnvMap == 1) : false;
}

void MeshRenderer::SetEnvIntensity(float intensity) {
    if (materialData_)
        materialData_->envIntensity = intensity;
}

float MeshRenderer::GetEnvIntensity() const {
    return materialData_ ? materialData_->envIntensity : 1.0f;
}

void MeshRenderer::SetEmissive(float emissive) {
    if (materialData_)
        materialData_->emissive = emissive;
}

float MeshRenderer::GetEmissive() const {
    return materialData_ ? materialData_->emissive : 1.0f;
}
