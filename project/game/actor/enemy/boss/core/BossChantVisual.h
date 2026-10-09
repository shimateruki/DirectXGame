#pragma once

#include "EffectObject3d.h"
#include "GPUParticleManager.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

// ボス固有の攻撃処理から独立した、詠唱中の魔法陣と収束粒子の表示設定。
// 紋章と粒子の外観はそれぞれの JSON プリセットからエディターで調整できます。
struct BossChantStyle {
    std::string sigilEffectPath;
    std::string groundLightEffectPath = "Resources/json/effect/effect_boss_chant_ground_light.json";
    std::string groundCoreEffectPath = "Resources/json/effect/effect_boss_chant_ground_core.json";
    std::string footRibbonEffectPath = "Resources/json/effect/effect_boss_chant_foot_ribbon.json";
    std::string risingLightEffectPath = "Resources/json/effect/effect_boss_chant_rising_light.json";
    std::string motePreset;
    std::string glintPreset;
    std::string releasePreset;
    Vector4 accentColor = {0.42f, 0.78f, 1.0f, 1.0f};
    float radius = 2.4f;
    float fadeStart = 0.30f;
    bool completeAtRelease = false; // trueなら詠唱時間全体で紋章を形成します。
};

// 1 フレーム分の術者姿勢と詠唱進行率。座標はすべてワールド空間です。
struct BossChantFrame {
    Vector3 casterPosition{};
    Vector3 groundPosition{};
    Vector3 sigilCenter{};
    float casterRadius = 1.0f;
    float casterHeight = 1.5f;
    Vector3 forward = {0.0f, 0.0f, 1.0f};
    float windupProgress = 0.0f;
    float activeElapsed = 0.0f;
    float time = 0.0f;
    float deltaTime = 0.0f;
    bool casting = false;
    bool releasing = false;
};

/// <summary>ボス共通の詠唱陣、足元の発光、収束粒子を一括して更新します。</summary>
class BossChantVisual {
  public:
    /// <summary>JSON プリセットから詠唱の描画層を生成します。</summary>
    /// <param name="common">エフェクト生成に使う描画共通オブジェクト。</param>
    /// <param name="style">魔法陣のパス、色、半径、粒子プリセット。</param>
    /// <returns>必須の魔法陣を生成できた場合は true。</returns>
    bool Start(Object3dCommon* common, BossChantStyle style) {
        Reset();
        if (!common || style.sigilEffectPath.empty()) {
            return false;
        }
        style_ = std::move(style);
        sigil_ = CreateEffect(common, style_.sigilEffectPath, "BossChantSigil");
        glow_ = CreateEffect(common, style_.sigilEffectPath, "BossChantGlow");
        floor_ = CreateEffect(common, style_.sigilEffectPath, "BossChantFloor");
        groundLight_ = CreateEffect(common, style_.groundLightEffectPath, "BossChantGroundLight");
        groundCore_ = CreateEffect(common, style_.groundCoreEffectPath, "BossChantGroundCore");
        footRibbon_ = CreateEffect(common, style_.footRibbonEffectPath, "BossChantFootRibbon");
        risingLight_ = CreateEffect(common, style_.risingLightEffectPath, "BossChantRisingLight");
        risingLightCounter_ = CreateEffect(common, style_.risingLightEffectPath, "BossChantRisingLightCounter");

        if (!sigil_ || !glow_ || !floor_) {
            Reset();
            return false;
        }
        if (const EffectObject3d::EffectMaterial* material = sigil_->GetMaterialData()) {
            presetAlpha_ = material->color.w;
            presetIntensity_ = material->intensity;
        }
        if (groundLight_ && groundLight_->GetMaterialData()) {
            const EffectObject3d::EffectMaterial* material = groundLight_->GetMaterialData();
            groundLightAlpha_ = material->color.w;
            groundLightIntensity_ = material->intensity;
        }
        if (groundCore_ && groundCore_->GetMaterialData()) {
            const EffectObject3d::EffectMaterial* material = groundCore_->GetMaterialData();
            groundCoreAlpha_ = material->color.w;
            groundCoreIntensity_ = material->intensity;
        }
        if (risingLight_ && risingLight_->GetMaterialData()) {
            const EffectObject3d::EffectMaterial* material = risingLight_->GetMaterialData();
            risingLightAlpha_ = material->color.w;
            risingLightIntensity_ = material->intensity;
        }
        if (footRibbon_ && footRibbon_->GetMaterialData()) {
            const EffectObject3d::EffectMaterial* material = footRibbon_->GetMaterialData();
            footRibbonAlpha_ = material->color.w;
            footRibbonIntensity_ = material->intensity;
        }

        sigil_->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
        glow_->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
        floor_->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
        glow_->SetBlendMode(BlendMode::kAdd);
        glow_->SetUseTextureColor(false);
        for (EffectObject3d* effect : {groundLight_.get(), groundCore_.get(), footRibbon_.get(), risingLight_.get(),
                                       risingLightCounter_.get()}) {
            if (!effect) {
                continue;
            }
            effect->SetBlendMode(BlendMode::kAdd);
            effect->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
        }

        moteTimer_ = 0.0f;
        wasReleasing_ = false;
        return true;
    }

    // 攻撃プレビューの遮蔽確認用に、表示中の描画層だけ数えます。
    int GetVisibleLayerCount() const {
        int count = 0;
        for (const EffectObject3d* effect :
             {sigil_.get(), glow_.get(), floor_.get(), groundLight_.get(), groundCore_.get(), footRibbon_.get(),
              risingLight_.get(), risingLightCounter_.get()}) {
            if (effect && effect->GetIsVisible()) {
                ++count;
            }
        }
        return count;
    }

    // 攻撃中断やモデル再生成時は、前の描画層とタイマーを破棄します。
    void Reset() {
        sigil_.reset();
        glow_.reset();
        floor_.reset();
        groundLight_.reset();
        groundCore_.reset();
        footRibbon_.reset();
        risingLight_.reset();
        risingLightCounter_.reset();
        moteTimer_ = 0.0f;
        wasReleasing_ = false;
        presetAlpha_ = 1.0f;
        presetIntensity_ = 1.0f;
        groundLightAlpha_ = 1.0f;
        groundLightIntensity_ = 1.0f;
        groundCoreAlpha_ = 1.0f;
        groundCoreIntensity_ = 1.0f;
        footRibbonAlpha_ = 1.0f;
        footRibbonIntensity_ = 1.0f;
        risingLightAlpha_ = 1.0f;
        risingLightIntensity_ = 1.0f;
    }

    /// <summary>詠唱進行率から魔法陣・足元の光・収束粒子を更新します。</summary>
    /// <param name="frame">術者位置、詠唱進行率、攻撃開始後の経過時間。</param>
    void Update(const BossChantFrame& frame) {
        if (!sigil_) {
            return;
        }
        if (!frame.casting && !frame.releasing) {
            sigil_->SetIsVisible(false);
            glow_->SetIsVisible(false);
            floor_->SetIsVisible(false);
            for (EffectObject3d* effect : {groundLight_.get(), groundCore_.get(), footRibbon_.get(), risingLight_.get(),
                                           risingLightCounter_.get()}) {
                if (effect) {
                    effect->SetIsVisible(false);
                }
            }
            wasReleasing_ = false;
            return;
        }

        const float progress = frame.casting ? std::clamp(frame.windupProgress, 0.0f, 1.0f) : 1.0f;
        const float assembly = style_.completeAtRelease ? progress
            : SmoothStep(std::clamp((progress - 0.04f) / 0.64f, 0.0f, 1.0f));
        const float charge = SmoothStep(std::clamp((progress - 0.68f) / 0.27f, 0.0f, 1.0f));
        const float fade =
            frame.casting ? 1.0f
                          : 1.0f - SmoothStep(std::clamp((frame.activeElapsed - style_.fadeStart) / 0.24f, 0.0f, 1.0f));
        const float releaseFlash = frame.releasing ? std::exp(-frame.activeElapsed * 14.0f) : 0.0f;
        const float pulse = 1.0f + std::sin(frame.time * (3.0f + charge * 3.0f)) * 0.025f * assembly;
        const Vector3 forward = ResolveForward(frame.forward);
        const Vector3 side = {forward.z, 0.0f, -forward.x};
        // 紋章は視点ではなく攻撃方向に向け、術者の動きと整合させます。
        const Vector3 uprightRotation = MakeUprightRotation(forward);
        const float radius = style_.radius;
        const float casterRadius = (std::max)(frame.casterRadius, radius * 0.75f);
        const float casterHeight = (std::max)(frame.casterHeight, casterRadius * 1.2f);

        if (frame.releasing && !wasReleasing_ && !style_.releasePreset.empty()) {
            if (GPUParticleManager* particles = GPUParticleManager::GetInstance();
                particles && particles->IsInitialized()) {
                particles->EmitDirected(style_.releasePreset, frame.sigilCenter, forward, 1.0f);
            }
        }
        wasReleasing_ = frame.releasing;

        sigil_->Update(frame.deltaTime);
        sigil_->SetRevealProgress(assembly);
        sigil_->SetIsVisible(fade > 0.01f && assembly > 0.001f);
        sigil_->SetTranslate(frame.sigilCenter);
        sigil_->SetRotation(uprightRotation);
        const float sigilRadius = radius * 0.62f;
        sigil_->SetScale({sigilRadius * pulse, 1.0f, sigilRadius * pulse});
        sigil_->SetColor({1.0f, 1.0f, 1.0f, (0.22f + assembly * 0.43f + charge * 0.15f) * fade * presetAlpha_});
        sigil_->SetIntensity(presetIntensity_ * (1.0f + charge * 0.26f + releaseFlash * 0.33f));
        sigil_->UpdateLocalMatrix();
        sigil_->UpdateWorldMatrix();

        glow_->Update(frame.deltaTime);
        glow_->SetRevealProgress(assembly);
        glow_->SetIsVisible(fade > 0.01f && assembly > 0.24f);
        glow_->SetTranslate(frame.sigilCenter + forward * 0.012f);
        glow_->SetRotation(uprightRotation);
        glow_->SetScale({sigilRadius * pulse * 1.02f, 1.0f, sigilRadius * pulse * 1.02f});
        Vector4 glowColor = style_.accentColor;
        glowColor.w = (0.08f + assembly * 0.11f + charge * 0.09f + releaseFlash * 0.15f) * fade * presetAlpha_;
        glow_->SetColor(glowColor);
        glow_->SetIntensity(presetIntensity_ * (1.30f + releaseFlash * 0.44f));
        glow_->UpdateLocalMatrix();
        glow_->UpdateWorldMatrix();

        // 足元の紋章は術者の動きに追従し、前方の陣とは異なる速度で回します。
        floor_->Update(frame.deltaTime);
        floor_->SetRevealProgress(style_.completeAtRelease ? progress
            : SmoothStep(std::clamp(progress / 0.72f, 0.0f, 1.0f)));
        floor_->SetIsVisible(fade > 0.01f && assembly > 0.04f);
        floor_->SetTranslate(frame.groundPosition + Vector3{0.0f, 0.075f, 0.0f});
        floor_->SetRotation({0.0f, frame.time * 0.24f, 0.0f});
        const float floorRadius = casterRadius * (1.25f + assembly * 0.10f);
        floor_->SetScale({floorRadius, 1.0f, floorRadius});
        floor_->SetColor({1.0f, 1.0f, 1.0f, (0.12f + assembly * 0.19f + releaseFlash * 0.12f) * fade * presetAlpha_});
        floor_->SetIntensity(presetIntensity_ * (0.82f + charge * 0.21f));
        floor_->UpdateLocalMatrix();
        floor_->UpdateWorldMatrix();

        // 足元を主役にして、胴体を横切る光は細い残光だけに抑えます。
        // 術者の輪郭が読めるよう、光の幅・高さ・重なりを分けます。
        const float aura = SmoothStep(std::clamp((progress - 0.02f) / 0.50f, 0.0f, 1.0f));
        const float auraScale = casterRadius * (1.72f + aura * 0.10f);
        UpdateLightLayer(groundLight_.get(), frame.deltaTime, frame.groundPosition + Vector3{0.0f, 0.16f, 0.0f},
                         {0.0f, frame.time * 0.70f, 0.0f},
                         {auraScale * (1.03f + std::sin(frame.time * 5.0f) * 0.025f), 1.0f,
                          auraScale * (1.03f + std::sin(frame.time * 5.0f) * 0.025f)},
                         style_.accentColor,
                         (0.32f + charge * 0.12f + releaseFlash * 0.08f) * aura * fade * groundLightAlpha_,
                         groundLightIntensity_);
        UpdateLightLayer(
            groundCore_.get(), frame.deltaTime, frame.groundPosition + Vector3{0.0f, 0.17f, 0.0f},
            {0.0f, -frame.time * 0.38f, 0.0f}, {auraScale * 0.91f, 1.0f, auraScale * 0.91f}, style_.accentColor,
            (0.26f + charge * 0.10f + releaseFlash * 0.09f) * aura * fade * groundCoreAlpha_, groundCoreIntensity_);

        // 足首付近の短い流れは輪郭の外側を通し、胸まで巻き付けません。
        const float footRadius = casterRadius * (1.14f + charge * 0.03f);
        const float footSpan = casterHeight * 0.23f;
        UpdateLightLayer(
            footRibbon_.get(), frame.deltaTime, frame.groundPosition + Vector3{0.0f, footSpan * 0.5f + 0.12f, 0.0f},
            {0.0f, -frame.time * 0.95f, 0.0f}, {footRadius, footSpan, footRadius}, style_.accentColor,
            (0.34f + charge * 0.10f + releaseFlash * 0.06f) * aura * fade * footRibbonAlpha_, footRibbonIntensity_);

        const Vector3 risingCenter = frame.groundPosition + Vector3{0.0f, casterHeight * 0.37f, 0.0f};
        // 上昇する残光は外縁をかすめる程度にして、顔を覆わないようにします。
        const float risingRadius = casterRadius * (1.17f + charge * 0.03f);
        const float risingHeightScale = casterHeight * 0.58f / 2.65f;
        UpdateLightLayer(risingLight_.get(), frame.deltaTime, risingCenter, {0.0f, frame.time * 1.65f, 0.0f},
                         {risingRadius, risingHeightScale, risingRadius}, style_.accentColor,
                         (0.25f + charge * 0.08f) * aura * fade * risingLightAlpha_, risingLightIntensity_);
        UpdateLightLayer(risingLightCounter_.get(), frame.deltaTime,
                         risingCenter + Vector3{0.0f, -casterHeight * 0.04f, 0.0f},
                         {0.0f, -frame.time * 1.20f + 2.3f, 0.0f},
                         {risingRadius * 0.97f, risingHeightScale * 0.92f, risingRadius * 0.97f}, style_.accentColor,
                         (0.10f + charge * 0.04f) * aura * fade * risingLightAlpha_, risingLightIntensity_ * 0.65f);

        if (frame.casting && assembly > 0.12f) {
            moteTimer_ -= frame.deltaTime;
            if (moteTimer_ <= 0.0f) {
                EmitGatheringMotes(frame, side, casterRadius, casterHeight, charge);
                moteTimer_ = 0.15f - charge * 0.045f;
            }
        }
    }

    // 足元の光を先に描き、前方の紋章を最後に重ねます。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
        DrawEffect(floor_.get(), pointLightResource, spotLightResource);
        DrawEffect(groundLight_.get(), pointLightResource, spotLightResource);
        DrawEffect(groundCore_.get(), pointLightResource, spotLightResource);
        DrawEffect(footRibbon_.get(), pointLightResource, spotLightResource);
        DrawEffect(risingLight_.get(), pointLightResource, spotLightResource);
        DrawEffect(risingLightCounter_.get(), pointLightResource, spotLightResource);
        DrawEffect(sigil_.get(), pointLightResource, spotLightResource);
        DrawEffect(glow_.get(), pointLightResource, spotLightResource);
    }

  private:
    static float SmoothStep(float value) { return value * value * (3.0f - 2.0f * value); }

    // 水平方向だけで正規化し、方向が取れないときは前方を使います。
    static Vector3 ResolveForward(const Vector3& direction) {
        const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        return length > 0.001f ? Vector3{direction.x / length, 0.0f, direction.z / length} : Vector3{0.0f, 0.0f, 1.0f};
    }

    // 地面用の平面エフェクトを、術者の正面に立てる回転です。
    static Vector3 MakeUprightRotation(const Vector3& forward) {
        constexpr float kHalfPi = 1.5707963268f;
        return {kHalfPi, std::atan2(forward.x, forward.z), 0.0f};
    }

    // 必須・任意のどちらの層も、生成失敗時は nullptr を返して呼び出し側で判定します。
    static std::unique_ptr<EffectObject3d> CreateEffect(Object3dCommon* common, const std::string& path,
                                                        const std::string& name) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common);
        if (!effect->LoadFromJson(path)) {
            return nullptr;
        }
        effect->SetName(name);
        effect->SetRevealProgress(0.0f);
        return effect;
    }

    static void DrawEffect(EffectObject3d* effect, ID3D12Resource* pointLightResource,
                           ID3D12Resource* spotLightResource) {
        if (effect && effect->GetIsVisible()) {
            effect->RefreshRenderCameraData();
            effect->Draw(pointLightResource, spotLightResource);
        }
    }

    /// <summary>任意の発光層へ位置、透明度、強度を反映します。</summary>
    /// <param name="alpha">表示判定にも使う最終透明度。</param>
    /// <param name="intensity">プリセットの強度を加味した発光強度。</param>
    static void UpdateLightLayer(EffectObject3d* effect, float deltaTime, const Vector3& position,
                                 const Vector3& rotation, const Vector3& scale, Vector4 color, float alpha,
                                 float intensity) {
        if (!effect) {
            return;
        }
        effect->Update(deltaTime);
        effect->SetIsVisible(alpha > 0.006f);
        effect->SetTranslate(position);
        effect->SetRotation(rotation);
        effect->SetScale(scale);
        color.w = alpha;
        effect->SetColor(color);
        effect->SetIntensity(intensity);
        effect->UpdateLocalMatrix();
        effect->UpdateWorldMatrix();
    }

    // 足元と胴体の外周に粒子を散らし、術者の顔を覆わずに収束感を出します。
    void EmitGatheringMotes(const BossChantFrame& frame, const Vector3& side, float casterRadius, float casterHeight,
                            float charge) {
        GPUParticleManager* particles = GPUParticleManager::GetInstance();
        if (!particles || !particles->IsInitialized() || style_.motePreset.empty()) {
            return;
        }
        const float orbit = frame.time * 5.0f;
        const Vector3 forward = ResolveForward(frame.forward);
        const float orbitRadius = casterRadius * (1.22f + charge * 0.04f);
        const float riseRange = casterHeight * 0.76f;
        const Vector3 ringPosition = frame.groundPosition + side * (std::cos(orbit) * orbitRadius) +
                                     forward * (std::sin(orbit) * orbitRadius) +
                                     Vector3{0.0f, 0.35f + std::fmod(frame.time * 1.6f, riseRange), 0.0f};
        const Vector3 tangent = side * -std::sin(orbit) + forward * std::cos(orbit);
        particles->EmitDirected(style_.motePreset, ringPosition, tangent + Vector3{0.0f, 0.6f, 0.0f},
                                1.4f + charge * 0.4f);

        // 反対側の軌道も使い、片側だけに粒子が偏らないようにします。
        const float secondOrbit = orbit + 3.14159265f;
        const Vector3 upperPosition =
            frame.groundPosition + side * (std::cos(secondOrbit) * orbitRadius) +
            forward * (std::sin(secondOrbit) * orbitRadius) +
            Vector3{0.0f, 0.35f + std::fmod(frame.time * 1.5f + riseRange * 0.55f, riseRange), 0.0f};
        const Vector3 upperTangent = side * -std::sin(secondOrbit) + forward * std::cos(secondOrbit);
        particles->EmitDirected(style_.motePreset, upperPosition, upperTangent + Vector3{0.0f, 0.45f, 0.0f},
                                1.1f + charge * 0.3f);
        if (!style_.glintPreset.empty()) {
            particles->EmitDirected(style_.glintPreset, upperPosition, upperTangent + Vector3{0.0f, 0.55f, 0.0f}, 1.0f);
        }

        // 地面の粒子は低く保ち、胴体のリングとは別の高さで回します。
        const Vector3 floorPosition = frame.groundPosition + side * (std::cos(orbit + 2.1f) * orbitRadius) +
                                      forward * (std::sin(orbit + 2.1f) * orbitRadius) + Vector3{0.0f, 0.12f, 0.0f};
        particles->EmitDirected(style_.motePreset, floorPosition,
                                side * -std::sin(orbit + 2.1f) + forward * std::cos(orbit + 2.1f) +
                                    Vector3{0.0f, 0.55f, 0.0f},
                                1.1f + charge * 0.35f);
        if (!style_.glintPreset.empty()) {
            particles->EmitDirected(style_.glintPreset, floorPosition, Vector3{0.0f, 1.0f, 0.0f}, 0.9f);
        }
        const Vector3 oppositePosition = frame.groundPosition + side * (std::cos(orbit + 4.2f) * orbitRadius) +
                                         forward * (std::sin(orbit + 4.2f) * orbitRadius) + Vector3{0.0f, 0.12f, 0.0f};
        particles->EmitDirected(style_.motePreset, oppositePosition,
                                side * -std::sin(orbit + 4.2f) + forward * std::cos(orbit + 4.2f) +
                                    Vector3{0.0f, 0.55f, 0.0f},
                                1.1f + charge * 0.35f);
    }

    BossChantStyle style_{};
    std::unique_ptr<EffectObject3d> sigil_;
    std::unique_ptr<EffectObject3d> glow_;
    std::unique_ptr<EffectObject3d> floor_;
    std::unique_ptr<EffectObject3d> groundLight_;
    std::unique_ptr<EffectObject3d> groundCore_;
    std::unique_ptr<EffectObject3d> footRibbon_;
    std::unique_ptr<EffectObject3d> risingLight_;
    std::unique_ptr<EffectObject3d> risingLightCounter_;
    float moteTimer_ = 0.0f;
    bool wasReleasing_ = false;
    float presetAlpha_ = 1.0f;
    float presetIntensity_ = 1.0f;
    float groundLightAlpha_ = 1.0f;
    float groundLightIntensity_ = 1.0f;
    float groundCoreAlpha_ = 1.0f;
    float groundCoreIntensity_ = 1.0f;
    float footRibbonAlpha_ = 1.0f;
    float footRibbonIntensity_ = 1.0f;
    float risingLightAlpha_ = 1.0f;
    float risingLightIntensity_ = 1.0f;
};
