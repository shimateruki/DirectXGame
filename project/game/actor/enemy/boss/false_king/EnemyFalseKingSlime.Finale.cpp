#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.Internal.h"

#include "game/actor/enemy/boss/core/BossChantVisual.h"
#include "DebrisEffectManager.h"

using namespace EnemyFalseKingSlimeImplementation;

namespace {
// ボスが所有する結晶。敵の当たり判定を使うので近接・弾・範囲攻撃を共通経路で受けます。
class CrownSealTarget final : public BaseEnemy {
  public:
    void Update(float deltaTime) override {
        damageCooldownTimer_ = std::max(0.0f, damageCooldownTimer_ - deltaTime);
        Object3d::Update(deltaTime);
    }
};

void UpdateFinaleLayer(EffectObject3d* effect, float deltaTime, bool visible, const Vector3& position,
                       const Vector3& scale, const Vector4& color, const Vector3& rotation = {}) {
    if (!effect)
        return;
    effect->Update(deltaTime);
    effect->SetIsVisible(visible);
    effect->SetTranslate(position);
    effect->SetRotation(rotation);
    effect->SetScale(scale);
    effect->SetColor(color);
    effect->UpdateLocalMatrix();
    effect->UpdateWorldMatrix(true);
}

// XZ平面のオーラをカメラへ向け、横から見ても光の厚みを保ちます。
Vector3 FinaleBillboardRotation(const Vector3& center) {
    if (const auto* camera = CameraManager::GetInstance()->GetMainCamera()) {
        const Vector3 toEye = camera->GetEye() - center;
        return {kPi * 0.5f - std::atan2(toEye.y, std::sqrt(toEye.x * toEye.x + toEye.z * toEye.z)),
                std::atan2(toEye.x, toEye.z), 0.0f};
    }
    return {kPi * 0.5f, 0.0f, 0.0f};
}
} // namespace

// 元の更新順序と判定を維持して、担当処理をまとめています。
float EnemyFalseKingSlime::FilterFinaleDamage(float damage) {
    if (!debugPreviewAttackId_.empty() || !IsEncounterControlled())
        return damage;
    if (crownInterlude_ || attackState_ == AttackState::PhaseShift)
        return 0.0f;
    if (battlePhase_ == 2) {
        // 王本体は障壁に守られる。3個の結晶を直接破壊するまでHPを減らしません。
        return 0.0f;
    }
    if (battlePhase_ == 1) {
        return std::clamp(damage, 0.0f, std::max(0.0f, GetEncounterCurrentHp() - GetEncounterMaximumHp() * 0.67f));
    }
    return std::max(0.0f, damage) * (armorOpenTimer_ > 0.0f ? 1.0f : 0.30f);
}

void EnemyFalseKingSlime::OpenCrownArmor() {
    if (battlePhase_ != 3 || crownInterlude_)
        return;
    armorOpenTimer_ = CrownFinaleTuning::Get().armorOpenTime;
    VFXSequencer::PlayOneShot(kPhaseShiftSequence, GetWorldPosition(), {1.1f, 1.1f, 1.1f});
}

void EnemyFalseKingSlime::ResetCrownFinale() {
    crownShieldRing_.reset();
    crownShieldShell_.reset();
    crownShieldBarrier_.reset();
    crownCastFloor_.reset();
    crownArenaRing_.reset();
    crownBarrageFlash_.reset();
    crownBarrageSkySigil_.reset();
    crownBarrageSkyCounterSigil_.reset();
    crownBarrageSkyOuterRing_.reset();
    crownBarrageSkyInnerRing_.reset();
    sealBarrage_.clear();
    for (auto& layer : crownBarrageLayers_)
        layer.reset();
    for (auto& seal : crownSeals_) {
        if (seal.crystal)
            CollisionManager::GetInstance()->RemoveObject(seal.crystal.get());
        seal = CrownSeal{};
    }
    sealState_ = SealState::Reveal;
    sealStateTimer_ = sealVfxTimer_ = 0.0f;
    sealAttackStage_ = 0;
    sealImpactPlayed_ = false;
    brokenSeals_ = 0;
    sealAttackTimer_ = 0.0f;
    sealPressureDamage_ = 0.0f;
    sealAttackSerial_ = 0;
    armorOpenTimer_ = 0.0f;
    crownInterlude_ = false;
    finalArenaStarted_ = false;
    sealTargetsLocked_ = false;
    sealBarrageDamageApplied_ = false;
}

void EnemyFalseKingSlime::InitializeCrownSeals() {
    const auto& tuning = CrownFinaleTuning::Get();
    for (auto& seal : crownSeals_) {
        if (seal.crystal)
            CollisionManager::GetInstance()->RemoveObject(seal.crystal.get());
        seal = CrownSeal{};
    }
    sealBarrage_.clear();
    sealBarrageDamageApplied_ = false;
    brokenSeals_ = 0;
    // 中央の王を囲む正三角形に配置し、3個を同時に攻撃可能にします。
    const Vector3 center = debugPreviewAttackId_ == kSealBarragePreviewId
                               ? GetTranslate()
                               : authoredPosition_ + Vector3{0.0f, 0.0f, tuning.sealCenterOffsetZ};
    for (int i = 0; i < 3; ++i) {
        auto& seal = crownSeals_[i];
        const float angle = -kPi / 2.0f + static_cast<float>(i) * kTwoPi / 3.0f;
        seal.position = FindGroundPoint(
            center + Vector3{std::sin(angle) * tuning.sealRadius, 0.0f, std::cos(angle) * tuning.sealRadius});
        seal.broken = false;
        seal.breakAge = 0.0f;
        auto crystal = std::make_unique<CrownSealTarget>();
        crystal->Initialize(common_, "Effects/false_king_seal_core");
        crystal->SetName("FalseKing_CrownSeal_" + std::to_string(i + 1));
        crystal->SetEnemyType("FalseKingSeal");
        crystal->SetCollisionAttribute(debugPreviewAttackId_ == kSealBarragePreviewId ? 0 : CollisionCategories::Get("enemy"));
        crystal->SetCollisionMask(debugPreviewAttackId_ == kSealBarragePreviewId ? 0
                                                                                 : CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
        crystal->SetColliderType(ColliderType::kSphere);
        crystal->SetCollisionRadius(1.8f);
        crystal->SetEnableLighting(true);
        crystal->SetEnableEnvMap(true);
        crystal->SetEnvIntensity(0.72f);
        crystal->SetEmissive(1.8f);
        crystal->SetRoughness(0.22f);
        crystal->SetMetallic(0.55f);
        crystal->SetTranslate(seal.position + Vector3{0.0f, 2.0f, 0.0f});
        crystal->SetScale({1.0f, 1.0f, 1.0f});
        crystal->UpdateWorldMatrix(true);
        crystal->param_.emplace();
        crystal->param_->maxHp = tuning.sealCrystalHp;
        crystal->param_->hp = tuning.sealCrystalHp;
        seal.crystal = std::move(crystal);
        CollisionManager::GetInstance()->AddObject(seal.crystal.get());
        seal.ring = CreateRoyalMesh("CrownSealBase", "Effects/false_king_seal_base",
                                    {0.72f, 0.32f, 1.0f, 1.0f}, 1.25f);
        seal.link = CreateRoyalMesh("CrownSealLink", "Effects/false_king_seal_link", {1, 0.65f, 0.18f, 0.5f}, 1.4f);
        seal.gem = CreateRoyalMesh("CrownSealGem", "Effects/false_king_seal_core",
                                   {0.86f, 0.48f, 1.0f, 1.0f}, 2.0f);
        if (seal.link) {
            seal.link->SetBlendMode(BlendMode::kNone);
            seal.link->SetEnableLighting(false);
        }
    }
    sealAttackTimer_ = 0.0f;
    sealPressureDamage_ = 0.0f;
    sealAttackSerial_ = 0;
    sealTargetsLocked_ = false;
    sealState_ = SealState::Reveal;
    sealStateTimer_ = sealVfxTimer_ = 0.0f;
    sealAttackStage_ = 0;
    sealImpactPlayed_ = false;
    ClearTransientVisuals();
    BossChantStyle chantStyle;
    chantStyle.sigilEffectPath = kUmbraFlameSigilEffect;
    chantStyle.motePreset = "false_king_seal_embers";
    chantStyle.completeAtRelease = true;
    chantStyle.accentColor = {1.0f, 0.06f, 0.015f, 1.0f};
    chantStyle.radius = std::clamp(baseScale_.x * 1.45f, 2.4f, 4.2f);
    chantStyle.fadeStart = 0.25f;
    spellChant_ = std::make_unique<BossChantVisual>();
    if (!spellChant_->Start(common_, std::move(chantStyle)))
        spellChant_.reset();
    auto makeShield = [this](const char* path, const char* name) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common_);
        if (!effect->LoadFromJson(path))
            return std::unique_ptr<EffectObject3d>{};
        effect->SetName(name);
        effect->SetIsVisible(false);
        effect->Play(100.0f);
        return effect;
    };
    for (auto& seal : crownSeals_) {
        for (auto& pulse : seal.energyPulses)
            pulse = makeShield(kCrownTransferPulseEffect, "FalseKing_CrownTransferPulse");
    }
    crownShieldRing_ = makeShield(kNightfallLancesRingEffect, "FalseKing_CrownShieldRing");
    crownShieldShell_ = makeShield(kAstralReaverOrbEffect, "FalseKing_CrownShieldShell");
    // 中ボスの封鎖壁と同じ GatePortal マテリアルを、円筒の代理形状で描画します。
    crownShieldBarrier_ = CreateRoyalVisual("FalseKing_CrownBarrier", "Gimmicks/portal_surface",
                                            kGatePortalMaterialType, {0.30f, 0.86f, 1.0f, 0.84f}, 2.4f);
    if (crownShieldBarrier_) {
        crownShieldBarrier_->SetBlendMode(BlendMode::kNormal);
        crownShieldBarrier_->SetCastShadow(false);
        crownShieldBarrier_->SetIsVisible(false);
        if (MeshRenderer* renderer = crownShieldBarrier_->GetMeshRenderer()) {
            if (MeshRenderer::WaterParamForGPU* effect = renderer->GetWaterParamData()) {
                effect->waveSpeed = 1.25f;
                effect->waveHeight = 0.72f;
                effect->waveFrequency = 18.0f;
                effect->effectType = 4.0f;
                effect->effectScale = 1.0f;
                effect->effectSoftness = 0.48f;
                effect->effectIntensity = 0.06f;
                effect->billboardScale = 1.0f;
                effect->effectScaleX = 1.0f;
                effect->effectScaleY = 1.0f;
                effect->effectScaleZ = 1.0f;
            }
        }
    }
    crownCastFloor_ = makeShield(kUmbraFlameSigilEffect, "FalseKing_CrownCastFloor");
    crownArenaRing_ = makeShield("Resources/json/effect/effect_false_king_seal_ground_current.json", "FalseKing_CrownArenaCharge");
    crownBarrageFlash_ = makeShield("Resources/json/effect/effect_false_king_seal_ground_aura.json", "FalseKing_CrownBarrageFlash");
    crownBarrageSkySigil_ = makeShield("Resources/json/effect/effect_false_king_seal_sky_sigil.json", "FalseKing_CrownBarrageSkySigil");
    crownBarrageSkyCounterSigil_ = makeShield("Resources/json/effect/effect_false_king_seal_sky_sigil.json", "FalseKing_CrownBarrageSkyCounterSigil");
    crownBarrageSkyOuterRing_ = makeShield(kNightfallLancesRingEffect, "FalseKing_CrownBarrageSkyOuterRing");
    crownBarrageSkyInnerRing_ = makeShield(kNightfallLancesRingEffect, "FalseKing_CrownBarrageSkyInnerRing");
    const char* layerPaths[] = {
        "Resources/json/effect/effect_false_king_seal_ground_aura.json",
        "Resources/json/effect/effect_false_king_seal_red_aura.json",
        "Resources/json/effect/effect_false_king_seal_ground_current.json", "Resources/json/effect/effect_false_king_seal_ground_current.json",
        "Resources/json/effect/effect_false_king_seal_red_aura.json",
        "Resources/json/effect/effect_false_king_seal_red_aura.json"};
    for (size_t index = 0; index < crownBarrageLayers_.size(); ++index)
        crownBarrageLayers_[index] = makeShield(layerPaths[index], "FalseKing_SealBarrageLayer");
    DebrisEffectManager::GetInstance()->PrewarmPreset("false_king_seal_shatter");
    GPUParticleManager::GetInstance()->PreloadPresetSystems(
        {"false_king_seal_embers", "false_king_seal_impact", "false_king_seal_dissolve",
         "false_king_seal_ground_sparks", "false_king_seal_air_aura"});
    HideAttackTelegraph();
}

Vector3 EnemyFalseKingSlime::GetLastBrokenCrownSealPosition() const {
    const CrownSeal* latest = nullptr;
    for (const auto& seal : crownSeals_)
        if (seal.broken && (!latest || seal.breakAge < latest->breakAge)) latest = &seal;
    return latest ? latest->position + Vector3{0,2,0}
                  : GetTranslate() + Vector3{-CrownFinaleTuning::Get().sealRadius,2,0};
}

void EnemyFalseKingSlime::TryBreakCrownSeal(const Vector3& impact, int attackStage, bool force) {
    if (battlePhase_ != 2 || !debugPreviewAttackId_.empty() || brokenSeals_ >= 3)
        return;
    if (attackStage < 0 || attackStage >= static_cast<int>(crownSeals_.size()))
        return;
    auto& seal = crownSeals_[attackStage];
    if (!force && (!seal.crystal || !seal.crystal->param_ || seal.crystal->param_->hp > 0.0f))
        return;
    if (seal.broken ||
        PlanarDistance(impact, seal.position) > CrownFinaleTuning::Get().sealHitRadius)
        return;
    seal.broken = true;
    seal.breakAge = 0.0f;
    if (seal.crystal) {
        CollisionManager::GetInstance()->RemoveObject(seal.crystal.get());
        seal.crystal->SetCollisionAttribute(0);
        seal.crystal->SetCollisionMask(0);
    }
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera())
        camera->StartShake(0.32f, 0.11f + brokenSeals_ * 0.025f, 24.0f, {0.6f, 0.5f, 0.25f});
    EmitDirectedPreset(kLanceImpactPreset, seal.position, {0.0f, 1.0f, 0.0f}, 1.8f);
    ++brokenSeals_;
    if (brokenSeals_ == 3) {
        sealState_ = SealState::Broken;
        sealStateTimer_ = 0.0f;
        ClearTransientVisuals();
        HideAttackTelegraph();
    }
}

float EnemyFalseKingSlime::GetSealCastRemainingTime() const {
    return battlePhase_ == 2 && sealState_ == SealState::Chant
               ? std::max(0.0f, CrownFinaleTuning::Get().sealCastTime - sealStateTimer_)
               : 0.0f;
}

void EnemyFalseKingSlime::UpdateCrownSeals(float deltaTime) {
    const auto& tuning = CrownFinaleTuning::Get();
    idleTimer_ += deltaTime;
    sealStateTimer_ += deltaTime;
    sealVfxTimer_ += deltaTime;
    SetVelocity({});
    HideAttackTelegraph();

    // 結晶のHPは既存のダメージイベントが減らす。描画と衝突は同じ位置へ同期します。
    for (int i = 0; i < static_cast<int>(crownSeals_.size()); ++i) {
        auto& seal = crownSeals_[i];
        if (!seal.broken && seal.crystal && seal.crystal->param_ && seal.crystal->param_->hp <= 0.0f)
            TryBreakCrownSeal(seal.position, i);
        if (seal.broken)
            seal.breakAge += deltaTime;

        const float remaining = seal.broken ? std::max(0.0f, 1.0f - seal.breakAge / 0.60f) : 1.0f;
        const float pulse = 1.0f + 0.07f * std::sin(idleTimer_ * 4.0f + static_cast<float>(i));
        const Vector3 crystalPosition = seal.position + Vector3{0.0f, 2.0f + 0.15f * std::sin(idleTimer_ * 2.2f), 0.0f};
        if (seal.crystal) {
            seal.crystal->SetIsVisible(remaining > 0.0f);
            seal.crystal->SetTranslate(crystalPosition);
            seal.crystal->SetScale({pulse * remaining, pulse * remaining, pulse * remaining});
            seal.crystal->SetRotation({0.0f, idleTimer_ * 0.75f, 0.0f});
            seal.crystal->SetColor({0.85f, 0.42f, 1.0f, remaining});
            seal.crystal->SetEmissive(1.65f + 0.35f * pulse);
            seal.crystal->Update(deltaTime);
        }
        if (seal.ring) {
            seal.ring->SetIsVisible(!seal.broken);
            seal.ring->SetTranslate(seal.position + Vector3{0.0f, 0.10f, 0.0f});
            const float radius = tuning.sealHitRadius / 1.29f;
            seal.ring->SetScale({radius * pulse, 0.35f, radius * pulse});
            seal.ring->SetEmissive(1.75f + 0.25f * pulse);
            seal.ring->Update(deltaTime);
        }
        const Vector3 crown = GetTranslate() + Vector3{(i - 1) * 1.1f, baseScale_.y * 2.0f, 0.0f};
        const Vector3 line = crown - crystalPosition;
        const float length = Math::Length(line);
        if (seal.link) {
            seal.link->SetIsVisible(!seal.broken);
            seal.link->SetTranslate((crystalPosition + crown) * 0.5f);
            seal.link->SetScale({0.09f, length, 0.09f});
            seal.link->SetRotation({std::atan2(std::sqrt(line.x * line.x + line.z * line.z), line.y),
                                    std::atan2(line.x, line.z), 0.0f});
            seal.link->SetColor({0.34f, 0.10f, 0.58f, 1.0f});
            seal.link->SetEmissive(1.35f + 0.15f * pulse);
            seal.link->Update(deltaTime);
        }
        // 結晶から王へ向かう光点を時間差で流し、エネルギーの送り先を見せます。
        for (size_t pulseIndex = 0; pulseIndex < seal.energyPulses.size(); ++pulseIndex) {
            const float travel = std::fmod(idleTimer_ * 0.78f + static_cast<float>(pulseIndex) / 3.0f +
                                               static_cast<float>(i) * 0.11f,
                                           1.0f);
            const float fade = SmoothStep01(std::min(travel, 1.0f - travel) * 7.0f);
            const float size = (0.22f + 0.12f * std::sin(travel * kPi)) * fade;
            UpdateFinaleLayer(seal.energyPulses[pulseIndex].get(), deltaTime,
                              !seal.broken && sealState_ != SealState::Recover &&
                                  sealState_ != SealState::Broken,
                              crystalPosition + line * travel, {size, size, size},
                              {0.60f, 0.22f, 0.88f, fade * 0.95f});
        }
        if (seal.gem) {
            seal.gem->SetIsVisible(!seal.broken);
            seal.gem->SetTranslate(crown);
            seal.gem->SetScale({0.28f, 0.35f, 0.28f});
            seal.gem->SetRotation({0.0f, idleTimer_, 0.0f});
            seal.gem->Update(deltaTime);
        }
    }

    const float barrierBuild = sealState_ == SealState::Reveal
                                   ? SmoothStep01((sealStateTimer_ - 0.25f) / 1.0f)
                                   : (sealState_ == SealState::Broken
                                          ? 1.0f - SmoothStep01(sealStateTimer_ / 0.75f)
                                          : (sealState_ == SealState::Recover ? 0.0f : 1.0f));
    const bool barrierVisible = barrierBuild > 0.01f;
    const float crystalPower = static_cast<float>(3 - brokenSeals_) / 3.0f;
    const float barrierAlpha = barrierBuild *
                               (0.54f + 0.20f * crystalPower + 0.04f * std::sin(idleTimer_ * 5.0f));
    const Vector3 ground = FindGroundPoint(GetTranslate());
    if (crownShieldBarrier_) {
        crownShieldBarrier_->SetIsVisible(barrierVisible);
        crownShieldBarrier_->SetTranslate(
            ground + Vector3{0.0f, tuning.sealBarrierHeight * barrierBuild * 0.5f, 0.0f});
        crownShieldBarrier_->SetScale({tuning.sealBarrierRadius * barrierBuild,
                                      tuning.sealBarrierHeight * barrierBuild * 0.5f,
                                      tuning.sealBarrierRadius * barrierBuild});
        crownShieldBarrier_->SetColor({0.30f, 0.86f, 1.0f,
                                      0.84f * barrierBuild * (0.70f + 0.30f * crystalPower)});
        if (MeshRenderer* renderer = crownShieldBarrier_->GetMeshRenderer()) {
            if (MeshRenderer::WaterParamForGPU* effect = renderer->GetWaterParamData()) {
                effect->effectIntensity = 0.06f + barrierBuild * 1.72f;
                effect->uvOffsetY = barrierBuild;
                effect->uvOffsetX = sealState_ == SealState::Reveal ? 1.0f - barrierBuild : 0.0f;
            }
        }
        crownShieldBarrier_->Update(deltaTime);
    }
    UpdateFinaleLayer(crownShieldShell_.get(), deltaTime, barrierVisible,
                      GetTranslate() + Vector3{0.0f, baseScale_.y * 1.2f, 0.0f},
                      {baseScale_.x * 2.65f * barrierBuild, baseScale_.y * 2.65f * barrierBuild,
                       baseScale_.z * 2.65f * barrierBuild},
                      {0.28f, 0.10f, 0.8f, barrierAlpha * 0.38f});
    UpdateFinaleLayer(crownShieldRing_.get(), deltaTime, barrierVisible,
                      ground + Vector3{0.0f, 0.18f, 0.0f},
                      {tuning.sealBarrierRadius * barrierBuild, 1.0f,
                       tuning.sealBarrierRadius * barrierBuild},
                      {0.68f, 0.21f, 1.0f, 0.80f});
    UpdateFinaleLayer(crownCastFloor_.get(), deltaTime, barrierVisible,
                      ground + Vector3{0.0f, 0.12f, 0.0f}, {5.2f, 1.0f, 5.2f},
                      {0.76f, 0.34f, 1.0f, 0.66f});
    // 紋章の最後の一画は攻撃開始フレームで完成させます。
    const float chantProgress = sealState_ == SealState::Chant
        ? std::clamp(sealStateTimer_ / tuning.sealCastTime, 0.0f, 1.0f)
        : (sealState_ == SealState::Attack ? 1.0f : 0.0f);
    if (crownCastFloor_)
        crownCastFloor_->SetRevealProgress(chantProgress);
    const float groundCharge = sealState_ == SealState::Chant
        ? 0.08f + 0.62f * chantProgress * chantProgress
        : (sealState_ == SealState::Attack ? 0.70f + 0.30f * SmoothStep01(sealStateTimer_ / 2.45f) : 0.0f);
    const float aftermath = sealState_ == SealState::Attack
        ? 1.0f - SmoothStep01((sealStateTimer_ - tuning.sealBarrageMovieDuration + 0.45f) / 0.45f) : 1.0f;
    const float dischargeAge = sealStateTimer_ - 2.76f;
    const float discharge = sealState_ == SealState::Attack && dischargeAge >= 0.0f
        ? 1.0f - SmoothStep01((sealStateTimer_ - tuning.sealBarrageMovieDuration + 0.38f) / 0.38f) : 0.0f;
    const Vector3 strike = !sealBarrage_.empty() ? sealBarrage_.front().ground : ground;
    UpdateFinaleLayer(crownArenaRing_.get(), deltaTime, groundCharge > 0.01f,
        ground + Vector3{0.0f, 0.18f, 0.0f},
        {tuning.sealBarrageRadius, 1.0f, tuning.sealBarrageRadius},
        {1.0f, 1.0f, 1.0f, groundCharge * 0.80f * aftermath});
    // UVを移動させず、地表に固定した電流の分岐・明滅・走行で圧力を増します。
    UpdateFinaleLayer(crownBarrageLayers_[0].get(), deltaTime, groundCharge > 0.01f,
        ground + Vector3{0.0f, 0.10f, 0.0f},
        {tuning.sealBarrageRadius, 1.0f, tuning.sealBarrageRadius},
        {1.0f, 0.025f, 0.005f, (0.06f + groundCharge * 0.28f) * aftermath});
    const Vector3 arenaAura = ground + Vector3{0.0f, 4.2f, 0.0f};
    UpdateFinaleLayer(crownBarrageLayers_[1].get(), deltaTime, groundCharge > 0.01f,
        arenaAura, {tuning.sealBarrageRadius, 1.0f, 10.0f + groundCharge * 7.0f},
        {1.0f, 0.025f, 0.008f, (groundCharge * 0.20f + discharge * 0.24f) * aftermath},
        FinaleBillboardRotation(arenaAura));
    for (int index = 2; index < 4; ++index) {
        // 主槍が刺さった瞬間、接地点だけでなく闘技場の全域を同時に放電させます。
        const float radius = tuning.sealBarrageRadius;
        UpdateFinaleLayer(crownBarrageLayers_[index].get(), deltaTime, discharge > 0.01f,
            ground + Vector3{0.0f, 0.30f + index * 0.03f, 0.0f}, {radius, 1.0f, radius},
            {1.0f, 1.0f, 1.0f, discharge * 0.96f}, {0.0f, index * 1.7f, 0.0f});
    }
    const Vector3 impactAura = strike + Vector3{0.0f, 5.5f, 0.0f};
    UpdateFinaleLayer(crownBarrageLayers_[4].get(), deltaTime, discharge > 0.01f,
        impactAura, {13.0f, 1.0f, 14.0f},
        {1.0f, 0.035f, 0.008f, discharge * 0.52f}, FinaleBillboardRotation(impactAura));
    UpdateFinaleLayer(crownBarrageLayers_[5].get(), deltaTime, chantProgress > 0.01f,
        ground + Vector3{0.0f, tuning.sealBarrageSkyHeight, 0.0f},
        {tuning.sealBarrageRadius * 0.83f, 1.5f, tuning.sealBarrageRadius * 0.83f},
        {1.0f, 0.03f, 0.012f, chantProgress * 0.14f * aftermath});
    UpdateFinaleLayer(crownBarrageFlash_.get(), deltaTime, discharge > 0.01f,
        ground + Vector3{0.0f, 0.24f, 0.0f},
        {tuning.sealBarrageRadius * 0.98f, 1.1f, tuning.sealBarrageRadius * 0.98f},
        {1.0f, 0.07f, 0.02f, discharge * 0.28f});

    // 詠唱開始から天空の術式を描き、失敗確定時に完成させます。
    const float skyBuild = chantProgress;
    const float skyFade = sealState_ == SealState::Attack
        ? 1.0f - SmoothStep01((sealStateTimer_ - 2.55f) / 0.65f) : 1.0f;
    const bool skyVisible = skyBuild > 0.001f && skyFade > 0.01f;
    const float skyRadius = tuning.sealBarrageRadius * tuning.sealBarrageSkyRadiusRate *
                            (0.72f + skyBuild * 0.28f);
    const Vector3 skyCenter = ground + Vector3{0.0f, tuning.sealBarrageSkyHeight, 0.0f};
    const float skyPulse = 0.88f + 0.12f * std::sin(idleTimer_ * 6.0f);
    UpdateFinaleLayer(crownBarrageSkySigil_.get(), deltaTime, skyVisible, skyCenter,
                      {skyRadius / 1.2f, 1.0f, skyRadius / 1.2f},
                      {1.0f, 0.16f, 0.055f, (0.25f + skyBuild * 0.65f) * skyPulse * skyFade},
                      {kPi, idleTimer_ * 0.18f, 0.0f});
    if (crownBarrageSkySigil_)
        crownBarrageSkySigil_->SetRevealProgress(skyBuild);
    UpdateFinaleLayer(crownBarrageSkyCounterSigil_.get(), deltaTime, skyVisible,
                      skyCenter + Vector3{0.0f, -0.10f, 0.0f},
                      {skyRadius * 0.40f, 1.0f, skyRadius * 0.40f},
                      {1.0f, 0.32f, 0.12f, skyBuild * 0.56f * skyFade},
                      {kPi, -idleTimer_ * 0.33f, 0.0f});
    if (crownBarrageSkyCounterSigil_)
        crownBarrageSkyCounterSigil_->SetRevealProgress(skyBuild);
    UpdateFinaleLayer(crownBarrageSkyOuterRing_.get(), deltaTime, skyVisible,
                      skyCenter + Vector3{0.0f, 0.06f, 0.0f},
                      {skyRadius, 1.0f, skyRadius},
                      {1.0f, 0.12f, 0.38f, skyBuild * 0.90f * skyFade});
    UpdateFinaleLayer(crownBarrageSkyInnerRing_.get(), deltaTime, skyVisible,
                      skyCenter + Vector3{0.0f, -0.18f, 0.0f},
                      {skyRadius * 0.66f, 1.0f, skyRadius * 0.66f},
                      {0.54f, 0.80f, 1.0f, skyBuild * 0.60f * skyFade});

    if (sealState_ == SealState::Broken) {
        attackState_ = AttackState::Recovery;
        if (sealStateTimer_ >= 1.2f) {
            auto* scene = dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene());
            if (scene && scene->StartCrownFinale(this)) {
                crownInterlude_ = true;
                ClearTransientVisuals();
            }
        }
        return;
    }

    if (sealState_ == SealState::Reveal) {
        attackState_ = AttackState::Windup;
        if (sealStateTimer_ >= 1.25f) {
            sealState_ = SealState::Chant;
            sealStateTimer_ = 0.0f;
        }
        return;
    }

    if (sealState_ == SealState::Chant) {
        attackState_ = AttackState::Windup;
        currentAttack_ = AttackKind::NightfallLances;
        attackStateDuration_ = tuning.sealCastTime;
        attackTimer_ = std::max(0.0f, tuning.sealCastTime - sealStateTimer_);
        if (sealVfxTimer_ >= 0.18f) {
            sealVfxTimer_ = 0.0f;
            EmitDirectedPreset("false_king_seal_embers",
                               GetTranslate() + Vector3{0.0f, baseScale_.y * 1.5f, 0.0f},
                               {0.0f, 1.0f, 0.0f}, 1.2f + sealStateTimer_ / tuning.sealCastTime);
            // 空間に散らした赤い火花も詠唱後半ほど密になります。
            const int emberSites = 2 + static_cast<int>(chantProgress * 7.0f);
            for (int index = 0; index < emberSites; ++index) {
                const float angle = index * 2.4f + idleTimer_ * 0.47f;
                const float radius = tuning.sealBarrageRadius * (0.18f + 0.09f * index);
                EmitDirectedPreset("false_king_seal_embers",
                    ground + Vector3{radius * std::cos(angle), 0.24f, radius * std::sin(angle)},
                    {0.0f, 1.0f, 0.0f}, 0.6f + chantProgress * 1.4f);
            }
            const int currentSites = 5 + static_cast<int>(chantProgress * 14.0f);
            for (int index = 0; index < currentSites; ++index) {
                const float angle = index * 2.39996f + std::floor(idleTimer_ * 5.0f) * 0.23f;
                const float radius = tuning.sealBarrageRadius * std::sqrt((index + 0.5f) / currentSites);
                EmitDirectedPreset("false_king_seal_ground_sparks",
                    ground + Vector3{radius * std::cos(angle), 0.28f, radius * std::sin(angle)},
                    {std::cos(angle + 1.5f), 0.025f, std::sin(angle + 1.5f)}, 6.0f + chantProgress * 8.0f);
            }
            for (const auto& seal : crownSeals_)
                if (!seal.broken)
                    EmitDirectedPreset(kChargePreset, seal.position + Vector3{0.0f, 1.3f, 0.0f},
                                       {0.0f, 1.0f, 0.0f}, 0.55f);
        }
        if (sealStateTimer_ < tuning.sealCastTime)
            return;

        // 失敗が確定してから操作を止める。詠唱中の結晶破壊は最後まで可能です。
        auto* gameplay = dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene());
        const Vector3 heroGround = target_ ? FindGroundPoint(target_->GetWorldPosition()) : ground;
        if (gameplay && debugPreviewAttackId_.empty())
            gameplay->StartSealBarrageMovie(this, heroGround);

        // 主役の大槍をプレイヤーの位置へ置き、小槍は広域攻撃の広がりだけを担います。
        sealState_ = SealState::Attack;
        sealStateTimer_ = 0.0f;
        sealBarrageDamageApplied_ = false;
        sealBarrage_.clear();
        const auto makeEffect = [this](const char* path) {
            auto effect = std::make_unique<EffectObject3d>();
            effect->Initialize(common_);
            if (!effect->LoadFromJson(path))
                return std::unique_ptr<EffectObject3d>{};
            effect->SetIsVisible(false);
            effect->Play(8.0f);
            return effect;
        };
        for (int ring = 0; ring < 3; ++ring) {
            const int count = ring == 0 ? 1 : (ring == 1 ? 6 : 10);
            const float radius = ring == 0 ? 0.0f : tuning.sealBarrageRadius * (ring == 1 ? 0.42f : 0.83f);
            for (int index = 0; index < count; ++index) {
                const float angle = count == 1 ? 0.0f : (static_cast<float>(index) + 0.5f * ring) * kTwoPi / count;
                SealBarrageSpear spear;
                spear.hero = ring == 0;
                spear.ground = spear.hero ? heroGround : FindGroundPoint(ground + Vector3{
                    radius * std::cos(angle), 0.0f, radius * std::sin(angle)});
                spear.delay = spear.hero ? 2.13f : (ring == 1 ? 1.30f + index * 0.085f
                                                               : 1.47f + index * 0.050f);
                spear.size = tuning.sealBarrageSpearScale * (spear.hero ? 1.68f : (ring == 1 ? 0.86f : 0.64f));
                spear.model = CreateRoyalMesh("FalseKing_ShadowSpear_" + std::to_string(sealBarrage_.size()),
                                              kCrownBarrageSpearModel, {1.0f, 1.0f, 1.0f, 1.0f}, 0.45f);
                if (spear.model) {
                    spear.model->SetCastShadow(false);
                    spear.model->SetIsVisible(false);
                }
                spear.glow = makeEffect("Resources/json/effect/effect_false_king_seal_red_aura.json");
                spear.portal = makeEffect(kNightfallLancesRingEffect);
                spear.impactLightning = makeEffect("Resources/json/effect/effect_false_king_seal_discharge_burst.json");
                spear.groundArc = makeEffect("Resources/json/effect/effect_false_king_seal_ground_current.json");
                spear.impactArc = makeEffect("Resources/json/effect/effect_false_king_seal_ground_current.json");
                if (spear.hero)
                    spear.impactBeam = makeEffect(kNightfallImpactBeamEffect);
                sealBarrage_.push_back(std::move(spear));
            }
        }
    }

    if (sealState_ == SealState::Attack) {
        attackState_ = AttackState::Active;
        attackStateDuration_ = tuning.sealBarrageMovieDuration;
        attackTimer_ = std::max(0.0f, attackStateDuration_ - sealStateTimer_);
        const bool emitAirAura = sealVfxTimer_ >= 0.12f;
        if (emitAirAura)
            sealVfxTimer_ = 0.0f;
        const float dissolveStart = tuning.sealBarrageMovieDuration - 0.80f;
        if (emitAirAura) {
            const int sites = discharge > 0.0f ? 36 : 20;
            for (int index = 0; index < sites; ++index) {
                const float angle = index * 2.39996f + std::floor(idleTimer_ * 7.0f) * 0.31f;
                const float radius = tuning.sealBarrageRadius * std::sqrt((index + 0.5f) / sites);
                const Vector3 site = ground + Vector3{radius * std::cos(angle), 0.3f, radius * std::sin(angle)};
                EmitDirectedPreset("false_king_seal_ground_sparks", site,
                    {std::cos(angle), 0.03f, std::sin(angle)}, discharge > 0.0f ? 18.0f : 10.0f);
                if (discharge > 0.0f)
                    EmitDirectedPreset("false_king_seal_air_aura", site + Vector3{0.0f, 1.6f, 0.0f},
                        {0.0f, 1.0f, 0.0f}, 4.0f);
            }
        }
        for (auto& spear : sealBarrage_) {
            const float age = sealStateTimer_ - spear.delay;
            const float fall = std::pow(std::clamp(age / 0.63f, 0.0f, 1.0f), 1.55f);
            const float reveal = SmoothStep01(sealStateTimer_ / (spear.hero ? 0.54f : 0.77f));
            const float dissolve = SmoothStep01((sealStateTimer_ - dissolveStart) / 0.70f);
            const float fade = 1.0f - dissolve;
            const bool visible = reveal > 0.01f && fade > 0.01f;
            const float spearScale = spear.size * std::max(0.05f, reveal);
            const float centerHeight = 4.90f * spearScale + tuning.sealBarrageSkyHeight * (1.0f - fall) - fall * 0.55f;
            const Vector3 center = spear.ground + Vector3{0.0f, centerHeight, 0.0f};
            const float spin = idleTimer_ * (spear.hero ? 0.40f : 0.78f) + spear.delay * 3.4f;
            if (spear.model) {
                spear.model->SetIsVisible(visible);
                spear.model->SetTranslate(center);
                spear.model->SetRotation({0.0f, spin, 0.0f});
                spear.model->SetScale({spearScale, spearScale, spearScale});
                spear.model->SetEmissive((spear.hero ? 0.65f : 0.42f) + 0.35f * (1.0f - fall));
                // 槍を上端からノイズ付きの赤い縁で崩し、破片と火花に引き継ぎます。
                if (auto* material = spear.model->GetMaterialData()) {
                    material->time = idleTimer_;
                    material->portalClipEnabled = dissolve > 0.0f ? 1.0f : 0.0f;
                    material->portalClipProgress = 0.5f;
                    material->portalClipCenter = spear.ground + Vector3{0.0f,
                        (centerHeight + 4.3f * spearScale + 0.8f) * (1.0f - dissolve) - dissolve, 0.0f};
                    material->portalClipNormal = {0.0f, 1.0f, 0.0f};
                    material->portalClipEdgeWidth = 0.18f * spear.size;
                    material->portalClipDissolve = 0.7f;
                    material->portalClipColor = {4.0f, 0.20f, 0.025f, 1.0f};
                }
                spear.model->UpdateLocalMatrix();
                spear.model->UpdateWorldMatrix(true);
            }
            // 着地を条件にせず、空中の形成段階から槍全体を赤いオーラで包みます。
            UpdateFinaleLayer(spear.glow.get(), deltaTime, visible, center,
                              {2.7f * spearScale, 1.0f, 6.4f * spearScale},
                              {1.0f, 0.018f, 0.008f, reveal * fade * (0.36f + 0.16f * fall)},
                              FinaleBillboardRotation(center));
            if (emitAirAura && visible) {
                EmitDirectedPreset("false_king_seal_air_aura", center,
                    {0.0f, 1.0f, 0.0f}, 1.0f);
                EmitPreset("false_king_seal_embers", center);
            }
            const float portalFade = reveal * (1.0f - SmoothStep01(age / 0.45f));
            UpdateFinaleLayer(spear.portal.get(), deltaTime, portalFade > 0.01f,
                              spear.ground + Vector3{0.0f, tuning.sealBarrageSkyHeight - 0.08f, 0.0f},
                              {spear.hero ? 4.1f : 2.4f, 1.0f, spear.hero ? 4.1f : 2.4f},
                              {1.0f, 0.20f, 0.40f, portalFade * 0.82f});
            // 予測線は使わず、地表を這う電流が時刻とともに広がります。
            const float charge = SmoothStep01(sealStateTimer_ / 2.35f);
            const float chargeRadius = (spear.hero ? 5.4f : 2.8f) * (0.96f + charge * 0.04f);
            const float chargeFlicker = 0.82f + 0.18f * std::sin(idleTimer_ * 17.0f + spear.delay * 7.0f);
            UpdateFinaleLayer(spear.groundArc.get(), deltaTime, charge > 0.01f,
                              spear.ground + Vector3{0.0f, 0.20f, 0.0f},
                              {chargeRadius, 1.0f, chargeRadius},
                              {1.0f, 1.0f, 1.0f,
                               (0.18f + 0.46f * charge) * chargeFlicker * fade},
                              {0.0f, spear.delay * 3.4f, 0.0f});
            const float strikeAge = age - 0.63f;
            const float lightningFade = std::max(discharge * 0.78f,
                strikeAge >= 0.0f ? 1.0f - SmoothStep01(strikeAge / 0.90f) : 0.0f);
            const Vector3 boltCenter = spear.ground + Vector3{0.0f, spear.hero ? 4.0f : 2.8f, 0.0f};
            UpdateFinaleLayer(spear.impactLightning.get(), deltaTime, lightningFade > 0.01f,
                              boltCenter, {spear.hero ? 9.0f : 7.0f, 1.0f, spear.hero ? 7.0f : 5.0f},
                              {1.0f, 1.0f, 1.0f, lightningFade * 0.90f}, FinaleBillboardRotation(boltCenter));
            const float arcFade = strikeAge >= 0.0f ? 1.0f - SmoothStep01(strikeAge / 0.52f) : 0.0f;
            const float arcRadius = (2.2f + 5.2f * SmoothStep01(strikeAge / 0.36f)) *
                                    (spear.hero ? 1.30f : 0.78f);
            UpdateFinaleLayer(spear.impactArc.get(), deltaTime, arcFade > 0.01f,
                              spear.ground + Vector3{0.0f, 0.28f, 0.0f},
                              {arcRadius, 1.0f, arcRadius},
                              {1.0f, 1.0f, 1.0f, arcFade * 0.82f}, {0.0f, spin, 0.0f});
            const float beamFade = spear.hero && strikeAge >= 0.0f
                                       ? 1.0f - SmoothStep01(strikeAge / 0.85f)
                                       : 0.0f;
            UpdateFinaleLayer(spear.impactBeam.get(), deltaTime, beamFade > 0.01f,
                              spear.ground + Vector3{0.0f, 8.2f, 0.0f},
                              {2.8f, 1.8f, 2.8f}, {1.0f, 0.84f, 0.90f, beamFade});
            if (age >= 0.0f && age - deltaTime < 0.0f)
                EmitDirectedPreset("false_king_seal_embers",
                                   spear.ground + Vector3{0.0f, tuning.sealBarrageSkyHeight, 0.0f},
                                   {0.0f, -1.0f, 0.0f}, spear.hero ? 4.1f : 1.6f);
            if (strikeAge >= 0.0f && strikeAge - deltaTime < 0.0f) {
                EmitDirectedPreset("false_king_seal_impact", spear.ground,
                                   {0.0f, 1.0f, 0.0f}, spear.hero ? 4.5f : 1.7f);
                EmitPreset("false_king_seal_embers", spear.ground + Vector3{0.0f, 0.24f, 0.0f});
            }
        }
        if (sealStateTimer_ >= dissolveStart && sealStateTimer_ - deltaTime < dissolveStart) {
            for (const auto& spear : sealBarrage_) {
                const Vector3 breakup = spear.ground + Vector3{0.0f, 3.4f * spear.size, 0.0f};
                DebrisEffectManager::GetInstance()->SpawnOnGround("false_king_seal_shatter", breakup, spear.ground.y);
                EmitPreset("false_king_seal_dissolve", breakup);
            }
        }
        if (!sealBarrageDamageApplied_ && sealStateTimer_ >= 2.76f) {
            sealBarrageDamageApplied_ = true;
            if (target_) {
                // 結晶を守れなかった場合だけ、移動と回避の無敵時間を貫く全域攻撃です。
                DamageEvent event;
                event.target = target_;
                event.attacker = this;
                event.damageAmount = tuning.sealBarrageDamage;
                event.lethalPlayerDamage = true;
                event.damageType = DamageType::Electric;
                event.knockbackVelocity = {0.0f, 5.0f, 0.0f};
                event.bypassPlayerInvincibility = true;
                EventManager::GetInstance()->Dispatch(event);
            }
            // 主槍の着弾を合図に、全ての接地点から同時に放電粒子を噴き上げます。
            for (const auto& spear : sealBarrage_)
                EmitDirectedPreset("false_king_seal_impact", spear.ground + Vector3{0.0f, 0.2f, 0.0f},
                    {0.0f, 1.0f, 0.0f}, spear.hero ? 2.0f : 1.0f);
            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera())
                camera->StartShake(0.65f, 0.22f, 23.0f, {0.7f, 0.7f, 0.35f});
        }
        if (sealStateTimer_ >= tuning.sealBarrageMovieDuration) {
            sealState_ = SealState::Recover;
            sealStateTimer_ = 0.0f;
            sealBarrage_.clear();
        }
        return;
    }

    attackState_ = AttackState::Recovery;
    if (sealState_ == SealState::Recover && sealStateTimer_ >= tuning.sealBarrageRecovery)
        InitializeCrownSeals();
}

void EnemyFalseKingSlime::BeginFinalArena(const Vector3& position) {
    // 次の闘技場へ持ち越す前に、結晶の衝突登録を含む第二形態の所有物を破棄します。
    ResetCrownFinale();
    crownInterlude_ = false;
    finalArenaStarted_ = true;
    battlePhase_ = pendingBattlePhase_ = 3;
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    attackCooldown_ = 2.5f;
    ClearTransientVisuals();
    SetTranslate(position);
    SetScale(baseScale_);
    SetVelocity({});
    SetGrounded(true);
    SetCollisionAttribute(authoredCollisionAttribute_);
    SetCollisionMask(authoredCollisionMask_);
    if (param_)
        param_->hp = GetEncounterMaximumHp() * 0.34f;
    SetColor({0.78f, 0.65f, 1.0f, 1.0f});
    SyncCollisionBounds();
    UpdateWorldMatrix(true);
}

void EnemyFalseKingSlime::AbortCrownFinale() {
    crownInterlude_ = false;
    finalArenaStarted_ = false;
    battlePhase_ = pendingBattlePhase_ = 2;
    attackState_ = AttackState::Idle;
    SetTranslate(FindGroundPoint(authoredPosition_ +
                                 Vector3{0.0f, 0.0f, CrownFinaleTuning::Get().sealCenterOffsetZ}));
    SetScale(baseScale_);
    if (param_)
        param_->hp = GetEncounterMaximumHp() * 0.67f;
    InitializeCrownSeals();
}

void EnemyFalseKingSlime::DrawFinaleEditor() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("王冠最終戦 / 第2形態・脱出", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    auto& t = CrownFinaleTuning::Get();
    ImGui::Text("形態 %d / 封印 %d / 3", battlePhase_, brokenSeals_);
    ImGui::TextWrapped("中央で詠唱する王を守る3個の結晶を制限時間内に直接破壊します。失敗すると全域へ黒晶墜槍が降ります。");
    if (battlePhase_ == 2)
        ImGui::Text("詠唱残り %.1f 秒", GetSealCastRemainingTime());
    ImGui::SliderFloat("王の中央位置 Z オフセット", &t.sealCenterOffsetZ, 5.0f, 24.0f);
    ImGui::SliderFloat("封印の配置半径", &t.sealRadius, 8.0f, 22.0f);
    ImGui::SliderFloat("封印の命中半径", &t.sealHitRadius, 1.5f, 5.0f);
    ImGui::SliderFloat("結晶のHP", &t.sealCrystalHp, 5.0f, 200.0f);
    ImGui::SliderFloat("詠唱制限時間", &t.sealCastTime, 8.0f, 40.0f);
    ImGui::SliderFloat("バリア半径", &t.sealBarrierRadius, 3.0f, 12.0f);
    ImGui::SliderFloat("バリア高さ", &t.sealBarrierHeight, 5.0f, 20.0f);
    ImGui::SliderFloat("全域槍の演出半径", &t.sealBarrageRadius, 25.0f, 44.0f);
    ImGui::SliderFloat("槍雨の空中魔法陣の高さ", &t.sealBarrageSkyHeight, 8.0f, 18.0f);
    ImGui::SliderFloat("槍雨の空中魔法陣の広さ", &t.sealBarrageSkyRadiusRate, 0.45f, 0.95f);
    ImGui::SliderFloat("槍雨の槍の大きさ", &t.sealBarrageSpearScale, 0.65f, 1.45f);
    ImGui::SliderFloat("槍雨ムービーの長さ", &t.sealBarrageMovieDuration, 3.1f, 5.0f);
    ImGui::SliderFloat("槍雨カメラの距離", &t.sealBarrageCameraDistance, 0.7f, 1.5f);
    ImGui::SliderFloat("槍雨カメラの高さ", &t.sealBarrageCameraHeight, 0.7f, 1.5f);
    ImGui::TextUnformatted("全域放電：HPと回避無敵に関係なく即死");
    ImGui::SliderFloat("失敗後の再詠唱待ち", &t.sealBarrageRecovery, 1.0f, 8.0f);
    ImGui::SliderFloat("継承・脱出ムービー秒数", &t.movieDuration, 12.0f, 24.0f);
    ImGui::TextUnformatted("中央装飾：破断 → 落下 → 吸い込み → 王冠覚醒");
    auto editMovieTime = [&](const char* label, float& ratio, float lo, float hi) {
        float seconds = ratio * t.movieDuration;
        if (ImGui::SliderFloat(label, &seconds, lo * t.movieDuration, hi * t.movieDuration, "%.2f 秒"))
            ratio = seconds / t.movieDuration;
    };
    editMovieTime("装飾が折れる時刻", t.movieFragmentBreakTime, 0.03f, 0.10f);
    editMovieTime("欠片が着地する時刻", t.movieFragmentLandTime, 0.14f, 0.28f);
    editMovieTime("吸い込み開始時刻", t.movieFragmentPullTime, 0.28f, 0.40f);
    editMovieTime("取り込み・変身時刻", t.movieFragmentAbsorbTime, 0.40f, 0.49f);
    editMovieTime("暴走と衝撃波", t.movieRageTime, 0.235f, 0.305f);
    editMovieTime("金色の体", t.movieBodyTime, 0.475f, 0.545f);
    editMovieTime("王冠の形成", t.movieCrownTime, 0.565f, 0.635f);
    editMovieTime("翼の形成", t.movieWingsTime, 0.645f, 0.715f);
    editMovieTime("踏み切り", t.movieEscapeTime, 0.725f, 0.795f);
    editMovieTime("最終ステージ着地", t.movieArriveTime, 0.895f, 0.965f);
    ImGui::SliderFloat("欠片とプレイヤーの距離", &t.movieFragmentDistance, 1.5f, 5.5f, "%.2f m");
    ImGui::SliderFloat("中央装飾の大きさ", &t.movieFragmentScale, 0.65f, 1.5f, "%.2f 倍");
    ImGui::SliderFloat("欠片の放物線の高さ", &t.movieFragmentFallArc, 0.0f, 6.0f, "%.2f m");
    ImGui::SliderFloat("装飾・吸収カメラの距離", &t.movieFragmentCameraDistance, 0.7f, 1.5f, "%.2f 倍");
    ImGui::TextDisabled("欠片を地面で見せてから吸収します。時刻の前後関係は自動で補正されます。");
    ImGui::SliderFloat("脱出軌道の高さ", &t.escapeHeight, 18.0f, 60.0f);
    ImGui::SliderFloat("ムービーのシェイク強度", &t.movieShake, 0.0f, 2.0f);
    ImGui::SliderFloat("ムービーの王翼サイズ", &t.movieWingScale, 0.5f, 2.0f);
    ImGui::SliderFloat("最終闘技場 X", &t.destinationX, 130.0f, 240.0f);
    ImGui::SliderFloat("王冠槍ダメージ", &t.lanceDamage, 1.0f, 40.0f);
    ImGui::SliderFloat("王威波ダメージ", &t.waveDamage, 1.0f, 60.0f);
    ImGui::SliderFloat("王翼突進ダメージ", &t.dashDamage, 1.0f, 60.0f);
    ImGui::SliderFloat("装甲が開く秒数", &t.armorOpenTime, 2.0f, 12.0f);
    t.Sanitize();
    static int saveResult = 0;
    if (ImGui::Button("最終戦設定を保存"))
        saveResult = t.Save() ? 1 : -1;
    ImGui::SameLine();
    if (ImGui::Button("設定を再読込"))
        saveResult = t.Load() ? 1 : -1;
    if (saveResult)
        ImGui::TextUnformatted(saveResult > 0 ? "設定を反映しました" : "設定ファイルを開けませんでした");
    if (SceneManager::GetInstance()->IsPlaying() && !crownInterlude_ && !finalArenaStarted_ &&
        debugPreviewAttackId_.empty()) {
        if (ImGui::Button("第2形態から確認"))
            TriggerDebugPhaseTwoTransition();
        ImGui::SameLine();
        if (ImGui::Button("王冠継承・脱出を確認"))
            TriggerDebugCrownInterlude();
    }
#endif
}
