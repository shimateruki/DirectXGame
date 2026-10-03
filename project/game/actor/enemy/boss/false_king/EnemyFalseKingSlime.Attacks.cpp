#include "game/actor/combat/CombatGroundQuery.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.Internal.h"

using namespace EnemyFalseKingSlimeImplementation;

namespace {
constexpr const char* kUmbraFlameRedEnergyPreset = "false_king_umbra_flame_charge_red";
constexpr const char* kAstralReaverChargePreset = "false_king_astral_reaver_charge";

void PlaceMagicLayer(EffectObject3d* effect, float deltaTime, bool visible,
    const Vector3& position, const Vector3& rotation, const Vector3& scale,
    const Vector4& color) {
    if (!effect) return;
    effect->Update(deltaTime);
    effect->SetIsVisible(visible);
    effect->SetTranslate(position);
    effect->SetRotation(rotation);
    effect->SetScale(scale);
    effect->SetColor(color);
    effect->UpdateLocalMatrix();
    effect->UpdateWorldMatrix(true);
}

Vector3 AstralReaverRimOffset(const Vector3& rotation, const Vector3& scale, float phase) {
    const Matrix4x4 ringMatrix = Math::MakeAffineMatrix(scale, rotation, {});
    return Math::TransformNormal({ std::cos(phase), 0.0f, std::sin(phase) }, ringMatrix);
}

float MagicEase(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vector3 AstralReaverFlightPosition(const Vector3& origin, const Vector3& destination,
    const Vector3& forward, const Vector3& right, float progress,
    size_t index, float hitRadius) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    // 前半は外へ回り込み、後半で急加速して着弾点へ収束します。
    const float early = MagicEase(t / 0.56f);
    const float late = std::clamp((t - 0.56f) / 0.44f, 0.0f, 1.0f);
    const float travel = t < 0.56f ? early * 0.38f :
        0.38f + 0.62f * (1.0f - (1.0f - late) * (1.0f - late) * (1.0f - late));
    const float orbitEnvelope = MagicEase((t - 0.04f) / 0.22f) *
        (1.0f - MagicEase((t - 0.75f) / 0.25f));
    const float dx = destination.x - origin.x;
    const float dz = destination.z - origin.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    const float orbitRadius = std::clamp(distance * 0.27f,
        (std::max)(1.2f, hitRadius * 0.7f),
        (std::max)(2.4f, hitRadius * 1.6f));
    const float turnDirection = index == 1 ? -1.0f : 1.0f;
    const float orbitPhase = turnDirection * (t * kTwoPi * 1.15f) +
        static_cast<float>(index) * 2.12f;
    Vector3 position = origin + (destination - origin) * travel;
    position += right * (std::sin(orbitPhase) * orbitRadius * orbitEnvelope);
    position += forward * (std::cos(orbitPhase) * orbitRadius * orbitEnvelope * 0.33f);
    position.y += std::sin(t * kPi) * (1.45f + static_cast<float>(index) * 0.27f) +
        std::cos(orbitPhase) * orbitEnvelope * 0.42f;
    return position;
}
}

// 元の更新順序と判定を維持して、担当処理をまとめています。
void EnemyFalseKingSlime::PrepareLanceTargets(bool dominionPattern) {
    const Vector3 center = dominionPattern ? FindGroundPoint(GetTranslate()) : lockedTargetPosition_;
    const float phaseOffset = std::fmod(
        idleTimer_ * 1.37f + static_cast<float>(automaticAttackSerial_) * 0.83f,
        kTwoPi);
    for (size_t index = 0; index < lanceTargets_.size(); ++index) {
        if (!dominionPattern && index == 0) {
            lanceTargets_[index] = lockedTargetPosition_;
            continue;
        }
        const int ring = static_cast<int>(index) / 6;
        const int spoke = static_cast<int>(index) % 6;
        const float radius = dominionPattern
            ? 7.0f + static_cast<float>(ring) * 7.3f
            : 2.7f + static_cast<float>(ring) * 3.4f + static_cast<float>(spoke % 2) * 0.65f;
        const float angle = phaseOffset + static_cast<float>(spoke) * kPi / 3.0f + static_cast<float>(ring) * kPi / 7.0f;
        lanceTargets_[index] = FindGroundPoint(center + Vector3{ std::sin(angle) * radius, 0.0f, std::cos(angle) * radius });
    }
}

void EnemyFalseKingSlime::SpawnNextCrownLance(bool dominionPattern) {
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(lanceTargets_.size())) {
        return;
    }
    SpawnCrownLance(lanceTargets_[static_cast<size_t>(actionIndex_)], dominionPattern ? 0.10f : 0.0f);
}

void EnemyFalseKingSlime::SpawnCrownLance(
    const Vector3& groundPosition,
    float delay,
    float readyDuration) {
    CrownLance lance;
    lance.visual = CreateRoyalMesh(
        "FalseKing_CrownLance_" + std::to_string(crownLances_.size()),
        kCrownLanceModel,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        battlePhase_ >= 3 ? 1.95f : 1.65f);
    lance.portal = CreateRoyalVisual(
        "FalseKing_CrownLancePortal_" + std::to_string(lanceSpawnSerial_),
        "Gimmicks/portal_surface",
        kGatePortalMaterialType,
        battlePhase_ >= 3
            ? Vector4{ 0.94f, 0.30f, 1.0f, 0.94f }
            : Vector4{ 1.0f, 0.66f, 0.18f, 0.94f },
        battlePhase_ >= 3 ? 2.8f : 2.5f);
    if (!lance.visual) {
        return;
    }
    lance.groundPosition = FindGroundPoint(groundPosition);
    lance.delay = delay;
    lance.readyDuration = (std::max)(readyDuration, 0.08f);
    lance.flightDuration = battlePhase_ >= 3 ? 0.44f : 0.54f;

    const int formationIndex = lanceSpawnSerial_++;
    constexpr std::array<int, 5> kColumnOpenOrder = { 2, 1, 3, 0, 4 };
    const int column = kColumnOpenOrder[static_cast<size_t>(formationIndex % 5)];
    const int row = (formationIndex / 5) % 3;
    const float sideOffset = static_cast<float>(column - 2) * 2.35f;
    const Vector3 planarForward = NormalizePlanar(lockedDirection_);
    const Vector3 planarRight = { planarForward.z, 0.0f, -planarForward.x };
    const Vector3 bossCenter = GetWorldPosition();
    lance.portalPosition = bossCenter
        - planarForward * (1.65f + static_cast<float>(row) * 0.72f)
        + planarRight * sideOffset
        + Vector3{ 0.0f, 3.7f + static_cast<float>(row) * 1.72f + std::abs(sideOffset) * 0.12f, 0.0f };
    const Vector3 impactAim = lance.groundPosition + Vector3{ 0.0f, 0.55f, 0.0f };
    lance.flightDirection = SafeNormalize(impactAim - lance.portalPosition, { 0.0f, -1.0f, 0.0f });

    lance.visual->SetTranslate(lance.portalPosition - lance.flightDirection * 1.25f);
    lance.visual->SetScale({ 1.12f, 2.35f, 1.12f });
    lance.visual->GetTransform()->quaternion = MakeAxisToDirectionQuaternion(
        { 0.0f, -1.0f, 0.0f }, lance.flightDirection);
    lance.visual->GetTransform()->isQuaternionMaster = true;
    lance.visual->SetIsVisible(false);
    lance.visual->UpdateWorldMatrix(true);
    if (lance.portal) {
        lance.portal->SetBlendMode(BlendMode::kNormal);
        lance.portal->SetTranslate(lance.portalPosition);
        lance.portal->SetScale({ 0.16f, 0.16f, 0.16f });
        lance.portal->GetTransform()->quaternion = MakeAxisToDirectionQuaternion(
            { 0.0f, 0.0f, 1.0f }, lance.flightDirection);
        lance.portal->GetTransform()->isQuaternionMaster = true;
        lance.portal->SetIsVisible(false);
        if (MeshRenderer* renderer = lance.portal->GetMeshRenderer()) {
            if (MeshRenderer::WaterParamForGPU* portal = renderer->GetWaterParamData()) {
                portal->waveSpeed = battlePhase_ >= 3 ? 2.8f : 2.25f;
                portal->waveHeight = 1.18f;
                portal->waveFrequency = 19.0f;
                portal->effectSoftness = 0.56f;
                portal->effectIntensity = 0.08f;
                portal->effectScale = 1.12f;
                portal->effectScaleX = 1.0f;
                portal->effectScaleY = 1.0f;
                portal->effectScaleZ = 0.12f;
                portal->effectType = battlePhase_ >= 3 ? 2.0f : 1.0f;
                portal->flowSpeedX = 0.0f;
                portal->flowSpeedY = 0.0f;
                portal->uvOffsetX = 0.0f;
                portal->uvOffsetY = 0.0f;
                portal->billboardScale = 1.0f;
            }
        }
        lance.portal->UpdateWorldMatrix(true);
    }
    crownLances_.push_back(std::move(lance));
}

void EnemyFalseKingSlime::UpdateCrownLances(float deltaTime) {
    auto lance = crownLances_.begin();
    while (lance != crownLances_.end()) {
        if (!lance->visual) {
            lance = crownLances_.erase(lance);
            continue;
        }
        lance->age += deltaTime;
        const float localAge = lance->age - lance->delay;
        if (localAge < 0.0f) {
            ++lance;
            continue;
        }

        const float launchTime = lance->portalOpenDuration + lance->readyDuration;
        const float openProgress = SmoothStep01(localAge / (std::max)(lance->portalOpenDuration, 0.01f));
        const float loadProgress = SmoothStep01(
            (localAge - lance->portalOpenDuration * 0.35f) /
            (std::max)(lance->portalOpenDuration * 0.65f + lance->readyDuration, 0.01f));
        const float flightProgress = std::clamp(
            (localAge - launchTime) / (std::max)(lance->flightDuration, 0.01f), 0.0f, 1.0f);
        const bool launched = localAge >= launchTime;
        const float impactAge = localAge - launchTime - lance->flightDuration;
        const float dissolveProgress = SmoothStep01(
            (impactAge - lance->impactHoldDuration) /
            (std::max)(lance->dissolveDuration, 0.01f));
        const float glow = 0.90f + std::sin(localAge * 19.0f) * 0.10f;
        const Vector3 loadedPosition = lance->portalPosition
            + lance->flightDirection * Math::Lerp(-1.32f, 0.48f, loadProgress);
        const Vector3 impactPosition = lance->groundPosition + Vector3{ 0.0f, 0.72f, 0.0f };
        const Vector3 flightPosition = LerpVector3(
            lance->portalPosition + lance->flightDirection * 0.48f,
            impactPosition,
            EaseOutCubic(flightProgress));
        // 予兆中はゲートだけを見せ、槍本体は射出フレームまで完全に隠します。
        lance->visual->SetIsVisible(launched && dissolveProgress < 1.0f);
        lance->visual->SetTranslate(launched ? flightPosition : loadedPosition);
        lance->visual->SetScale({
            1.10f * glow,
            2.35f + (1.0f - flightProgress) * 0.18f,
            1.10f * glow });
        if (impactAge >= lance->impactHoldDuration) {
            const float edgePeak = 1.0f - std::abs(dissolveProgress * 2.0f - 1.0f);
            lance->visual->SetMaterialType(4);
            lance->visual->SetBlendMode(BlendMode::kNormal);
            lance->visual->SetEmissive(1.65f + edgePeak * 1.35f);
            lance->visual->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            if (auto* material = lance->visual->GetMaterialData()) {
                material->portalClipEnabled = 1.0f;
                material->portalClipProgress = dissolveProgress;
                material->portalClipCenter = impactPosition + lance->flightDirection * 0.52f;
                material->portalClipNormal = lance->flightDirection;
                material->portalClipEdgeWidth = 0.22f - dissolveProgress * 0.10f;
                material->portalClipDissolve = 0.72f - dissolveProgress * 0.16f;
                material->portalClipColor = battlePhase_ >= 3
                    ? Vector4{ 0.96f, 0.34f, 1.0f, 0.96f }
                    : Vector4{ 1.0f, 0.66f, 0.18f, 0.96f };
            }
        }
        lance->visual->Update(deltaTime);
        lance->visual->UpdateWorldMatrix(true);

        const float closeProgress = SmoothStep01(
            (localAge - launchTime) / (std::max)(lance->portalCloseDuration, 0.01f));
        if (lance->portal) {
            const float portalVisibility = launched ? 1.0f - closeProgress : openProgress;
            const float portalScale = Math::Lerp(
                0.16f,
                battlePhase_ >= 3 ? 3.05f : 2.72f,
                portalVisibility);
            lance->portal->SetIsVisible(portalVisibility > 0.02f);
            lance->portal->SetScale({ portalScale, portalScale, portalScale });
            lance->portal->SetColor(battlePhase_ >= 3
                ? Vector4{ 0.94f, 0.30f, 1.0f, 0.94f * portalVisibility }
                : Vector4{ 1.0f, 0.66f, 0.18f, 0.94f * portalVisibility });
            if (MeshRenderer* renderer = lance->portal->GetMeshRenderer()) {
                if (MeshRenderer::WaterParamForGPU* portal = renderer->GetWaterParamData()) {
                    portal->effectIntensity = 0.08f + portalVisibility * 1.74f;
                    portal->uvOffsetY = portalVisibility;
                    portal->uvOffsetX = (launched ? 1.0f - closeProgress : openProgress) *
                        (0.42f + 0.58f * loadProgress);
                }
            }
            lance->portal->Update(deltaTime);
            lance->portal->UpdateWorldMatrix(true);
        }

        lance->particleTimer -= deltaTime;
        if (lance->particleTimer <= 0.0f && launched && flightProgress < 0.96f) {
            EmitDirectedPreset(
                kLanceTrailPreset,
                flightPosition,
                lance->flightDirection,
                1.15f + flightProgress * 0.70f);
            lance->particleTimer += 0.08f;
        }
        if (flightProgress >= 1.0f && !lance->impactPlayed) {
            lance->impactPlayed = true;
            EmitDirectedPreset(kLanceImpactPreset, lance->groundPosition, { 0.0f, 1.0f, 0.0f }, 1.35f);
            TryBreakCrownSeal(lance->groundPosition);
        }
        if (flightProgress >= 1.0f && !lance->damageApplied) {
            const EnemyAttackDefinition& attack = currentAttack_ == AttackKind::CrownDominion
                ? GetAttackDefinition(kCrownDominionAttackId)
                : GetAttackDefinition(kCrownLanceRainAttackId);
            Vector3 knockbackDirection = target_
                ? NormalizePlanar(target_->GetWorldPosition() - lance->groundPosition)
                : lockedDirection_;
            DamageTargetAt(
                lance->groundPosition,
                attack.radius + (target_ ? target_->GetCollisionRadius() : 0.0f),
                3.8f,
                attack.damage,
                DamageType::Explosion,
                { knockbackDirection.x * 7.0f, 7.5f, knockbackDirection.z * 7.0f });
            lance->damageApplied = true;
        }
        if (localAge >= launchTime + lance->flightDuration +
            lance->impactHoldDuration + lance->dissolveDuration) {
            lance = crownLances_.erase(lance);
            continue;
        }
        ++lance;
    }
}

void EnemyFalseKingSlime::SpawnRoyalWaves() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kRoyalShockwaveAttackId);
    // 着地後は柔らかな白い波を使わず、金属質の波頭と王冠片で危険な攻撃として見せます。
    // 三発を明確に数えられる間隔で出し、等速の波として回避判断を可能にします。
    constexpr int kWaveCount = 3;
    const float waveDelay = battlePhase_ >= 3 ? 1.00f : 1.12f;
    const float waveLifetime = battlePhase_ >= 3 ? 2.40f : 2.65f;
    for (int index = 0; index < kWaveCount; ++index) {
        RoyalWave wave;
        const bool alternateColor = index % 2 != 0;
        wave.alternateColor = alternateColor;
        const Vector4 waveColor = battlePhase_ >= 3
            ? (alternateColor
                ? Vector4{ 0.24f, 0.72f, 1.0f, 0.96f }
                : Vector4{ 0.94f, 0.24f, 1.0f, 0.96f })
            : (alternateColor
                ? Vector4{ 1.0f, 0.24f, 0.72f, 0.96f }
                : Vector4{ 1.0f, 0.76f, 0.08f, 0.96f });
        wave.visual = CreateRoyalMesh(
            "FalseKing_RoyalWave_" + std::to_string(index),
            kRoyalShockwaveWallModel,
            waveColor,
            battlePhase_ >= 3 ? 3.4f : 3.0f);
        wave.glow = CreateRoyalMesh(
            "FalseKing_RoyalWaveCore_" + std::to_string(index),
            kRoyalShockwaveWallModel,
            battlePhase_ >= 3
                ? Vector4{ 0.86f, 0.78f, 1.0f, 0.72f }
                : Vector4{ 1.0f, 0.94f, 0.46f, 0.72f },
            battlePhase_ >= 3 ? 5.4f : 4.8f);
        if (!wave.visual) {
            continue;
        }
        wave.visual->SetBlendMode(BlendMode::kAdd);
        wave.visual->SetEnableLighting(false);
        wave.visual->SetEnableEnvMap(false);
        wave.visual->SetMetallic(0.0f);
        if (wave.glow) {
            wave.glow->SetBlendMode(BlendMode::kAdd);
            wave.glow->SetEnableLighting(false);
            wave.glow->SetEnableEnvMap(false);
            wave.glow->SetMetallic(0.0f);
        }
        wave.center = FindGroundPoint(GetTranslate()) + Vector3{ 0.0f, 0.04f, 0.0f };
        wave.delay = static_cast<float>(index) * waveDelay;
        wave.lifetime = waveLifetime;
        wave.startRadius = 0.82f + static_cast<float>(index) * 0.08f;
        wave.endRadius = attack.maxRange * (0.90f + static_cast<float>(index) * 0.022f);
        wave.previousRadius = wave.startRadius;
        wave.particleTimer = static_cast<float>(index) * 0.025f;
        wave.visual->SetTranslate(wave.center);
        wave.visual->SetScale({ wave.startRadius, 1.45f, wave.startRadius });
        wave.visual->SetIsVisible(index == 0);
        wave.visual->UpdateWorldMatrix(true);
        if (wave.glow) {
            wave.glow->SetTranslate(wave.center + Vector3{ 0.0f, 0.025f, 0.0f });
            wave.glow->SetScale({ wave.startRadius * 0.982f, 0.92f, wave.startRadius * 0.982f });
            wave.glow->SetIsVisible(index == 0);
            wave.glow->UpdateWorldMatrix(true);
        }
        royalWaves_.push_back(std::move(wave));
    }
}

void EnemyFalseKingSlime::UpdateRoyalWaves(float deltaTime) {
    auto wave = royalWaves_.begin();
    while (wave != royalWaves_.end()) {
        if (!wave->visual) {
            wave = royalWaves_.erase(wave);
            continue;
        }
        wave->age += deltaTime;
        const float localAge = wave->age - wave->delay;
        if (localAge < 0.0f) {
            ++wave;
            continue;
        }
        const float progress = std::clamp(localAge / (std::max)(wave->lifetime, 0.01f), 0.0f, 1.0f);
        const float radius = Math::Lerp(wave->startRadius, wave->endRadius, progress);
        const float crest = std::sin(progress * kPi);
        const float verticalScale = 1.45f + crest * 0.62f;
        const float waveAlpha = (1.0f - SmoothStep01((progress - 0.68f) / 0.32f)) * 0.96f;
        const float lift = 0.025f;
        wave->visual->SetIsVisible(progress < 1.0f);
        wave->visual->SetTranslate(wave->center + Vector3{ 0.0f, lift, 0.0f });
        wave->visual->SetScale({ radius, verticalScale, radius });
        wave->visual->SetRotationY(wave->age * (battlePhase_ >= 3 ? 2.2f : 1.65f));
        wave->visual->SetColor(
            battlePhase_ >= 3
                ? (wave->alternateColor
                    ? Vector4{ 0.24f, 0.72f, 1.0f, waveAlpha }
                    : Vector4{ 0.94f, 0.24f, 1.0f, waveAlpha })
                : (wave->alternateColor
                    ? Vector4{ 1.0f, 0.24f, 0.72f, waveAlpha }
                    : Vector4{ 1.0f, 0.76f, 0.08f, waveAlpha }));
        wave->visual->Update(deltaTime);
        wave->visual->UpdateWorldMatrix(true);
        if (wave->glow) {
            wave->glow->SetIsVisible(progress < 1.0f);
            wave->glow->SetTranslate(wave->center + Vector3{ 0.0f, lift + 0.04f, 0.0f });
            wave->glow->SetScale({ radius * 0.982f, verticalScale * 0.64f, radius * 0.982f });
            wave->glow->SetRotationY(-wave->age * (battlePhase_ >= 3 ? 2.8f : 2.1f));
            wave->glow->SetColor(
                battlePhase_ >= 3
                    ? Vector4{ 0.86f, 0.78f, 1.0f, waveAlpha * 0.72f }
                    : Vector4{ 1.0f, 0.94f, 0.46f, waveAlpha * 0.72f });
            wave->glow->Update(deltaTime);
            wave->glow->UpdateWorldMatrix(true);
        }

        wave->particleTimer -= deltaTime;
        if (wave->particleTimer <= 0.0f && progress < 0.88f) {
            constexpr int kCrestPointCount = 8;
            const float rotationOffset = wave->age * 2.4f;
            for (int pointIndex = 0; pointIndex < kCrestPointCount; ++pointIndex) {
                const float angle = rotationOffset +
                    static_cast<float>(pointIndex) * kTwoPi / static_cast<float>(kCrestPointCount);
                const Vector3 direction = { std::sin(angle), 0.24f, std::cos(angle) };
                const Vector3 crestPosition = wave->center + Vector3{
                    direction.x * radius,
                    0.52f + crest * 0.38f,
                    direction.z * radius };
                EmitDirectedPreset(
                    kShockwaveCrestPreset,
                    crestPosition,
                    direction,
                    1.0f + crest * 0.48f);
                EmitDirectedPreset(
                    kShockwaveShardPreset,
                    crestPosition + Vector3{ 0.0f, 0.12f, 0.0f },
                    direction,
                    0.88f + crest * 0.36f);
            }
            wave->particleTimer += 0.09f;
        }

        if (!wave->hitTarget && target_) {
            const float distance = PlanarDistance(target_->GetWorldPosition(), wave->center);
            const float padding = target_->GetCollisionRadius() + 0.82f;
            // 波の上を飛び越えられるよう、接地付近だけを当たりにします。
            if (distance >= wave->previousRadius - padding && distance <= radius + padding &&
                std::abs(target_->GetWorldPosition().y - wave->center.y) <= 2.25f) {
                const EnemyAttackDefinition& attack = GetAttackDefinition(kRoyalShockwaveAttackId);
                const Vector3 direction = NormalizePlanar(target_->GetWorldPosition() - wave->center);
                DamageTargetAt(
                    target_->GetWorldPosition(),
                    padding,
                    2.25f,
                    attack.damage,
                    DamageType::Explosion,
                    { direction.x * 8.0f, 5.4f, direction.z * 8.0f });
                wave->hitTarget = true;
            }
        }
        wave->previousRadius = radius;
        if (progress >= 1.0f) {
            wave = royalWaves_.erase(wave);
            continue;
        }
        ++wave;
    }
}

void EnemyFalseKingSlime::BeginRush() {
    rushStep_ = 0;
    rushWingsVisual_ = CreateRoyalMesh(
        "FalseKing_RushWings",
        kRushWingsModel,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        battlePhase_ >= 3 ? 2.0f : 1.7f);
    if (rushWingsVisual_) {
        rushWingsVisual_->SetIsVisible(false);
        rushWingsVisual_->UpdateWorldMatrix(true);
    }
    BeginRushStep(rushStep_);
}

void EnemyFalseKingSlime::BeginRushStep(int step) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kKingRushAttackId);
    attackStartPosition_ = FindGroundPoint(GetTranslate());
    if (target_) {
        lockedTargetPosition_ = FindGroundPoint(target_->GetWorldPosition());
        lockedDirection_ = NormalizePlanar(lockedTargetPosition_ - attackStartPosition_);
    }
    const float distance = (std::min)(attack.maxRange, 13.0f + static_cast<float>(battlePhase_) * 2.6f);
    attackEndPosition_ = FindGroundPoint(attackStartPosition_ + lockedDirection_ * distance);
    rushStepTimer_ = 0.0f;
    rushStepDuration_ = battlePhase_ >= 3 ? 0.70f : 0.82f;
    rushDamageApplied_ = false;
    ShowAttackTelegraphLine(
        attackStartPosition_, lockedDirection_, distance, attack.radius * 2.0f, 0.85f,
        battlePhase_ >= 3 ? Vector4{ 0.88f, 0.18f, 1.0f, 0.82f } : Vector4{ 1.0f, 0.64f, 0.10f, 0.82f });
    (void)step;
}

void EnemyFalseKingSlime::UpdateRush(
    float deltaTime,
    const Vector3& targetDirection,
    Vector3& velocity) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kKingRushAttackId);
    const int rushCount = battlePhase_ == 1 ? 2 : 3;
    if (rushStep_ >= rushCount) {
        velocity = { 0.0f, 0.0f, 0.0f };
        HideAttackTelegraph();
        if (rushWingsVisual_) {
            rushWingsVisual_->SetIsVisible(false);
        }
        return;
    }
    rushStepTimer_ += deltaTime;
    const float progress = std::clamp(rushStepTimer_ / (std::max)(rushStepDuration_, 0.01f), 0.0f, 1.0f);
    const float chargeEnd = 0.22f;
    if (progress < chargeEnd) {
        velocity = { 0.0f, 0.0f, 0.0f };
        lockedDirection_ = NormalizePlanar(targetDirection);
        const float distance = (std::min)(attack.maxRange, 13.0f + static_cast<float>(battlePhase_) * 2.6f);
        attackEndPosition_ = FindGroundPoint(attackStartPosition_ + lockedDirection_ * distance);
        UpdateFacing(lockedDirection_, 0.26f);
        const float telegraphProgress = SmoothStep01(progress / chargeEnd);
        ShowAttackTelegraphLine(
            attackStartPosition_, lockedDirection_, distance, attack.radius * 2.0f,
            telegraphProgress,
            { 1.0f, battlePhase_ >= 3 ? 0.18f : 0.60f, battlePhase_ >= 3 ? 1.0f : 0.08f, 0.82f });
        if (rushWingsVisual_) {
            const float yaw = std::atan2(lockedDirection_.x, lockedDirection_.z);
            const float spread = 0.55f + telegraphProgress * 0.55f;
            rushWingsVisual_->SetIsVisible(telegraphProgress > 0.08f);
            rushWingsVisual_->SetTranslate(attackStartPosition_ + Vector3{ 0.0f, 0.78f, 0.0f });
            rushWingsVisual_->SetRotationY(yaw);
            rushWingsVisual_->SetScale({ spread, 0.82f + telegraphProgress * 0.18f, spread });
            rushWingsVisual_->Update(deltaTime);
            rushWingsVisual_->UpdateWorldMatrix(true);
        }
        return;
    }
    HideAttackTelegraph();
    const float dashProgress = SmoothStep01((progress - chargeEnd) / (1.0f - chargeEnd));
    const Vector3 position = LerpVector3(attackStartPosition_, attackEndPosition_, dashProgress);
    SetTranslate(position);
    velocity = { 0.0f, 0.0f, 0.0f };
    UpdateFacing(lockedDirection_, 0.45f);
    if (rushWingsVisual_) {
        const float yaw = std::atan2(lockedDirection_.x, lockedDirection_.z);
        const float wingPulse = 1.22f + std::sin(rushStepTimer_ * 28.0f) * 0.08f;
        rushWingsVisual_->SetIsVisible(true);
        rushWingsVisual_->SetTranslate(position + Vector3{ 0.0f, 0.78f, 0.0f });
        rushWingsVisual_->SetRotationY(yaw);
        rushWingsVisual_->SetScale({ wingPulse, 1.0f, wingPulse });
        rushWingsVisual_->Update(deltaTime);
        rushWingsVisual_->UpdateWorldMatrix(true);
    }
    effectTimer_ -= deltaTime;
    if (effectTimer_ <= 0.0f) {
        EmitDirectedPreset(
            kRushWakePreset,
            position + Vector3{ 0.0f, 0.42f, 0.0f },
            { -lockedDirection_.x, 0.30f, -lockedDirection_.z },
            1.2f + static_cast<float>(battlePhase_) * 0.18f);
        effectTimer_ += 0.11f;
    }
    if (!rushDamageApplied_ && target_ &&
        PlanarDistance(target_->GetWorldPosition(), position) <= attack.radius + target_->GetCollisionRadius()) {
        DamageTargetAt(
            position,
            attack.radius + target_->GetCollisionRadius(),
            3.4f,
            attack.damage,
            DamageType::Physical,
            { lockedDirection_.x * 13.0f, 6.2f, lockedDirection_.z * 13.0f });
        rushDamageApplied_ = true;
    }
    if (progress >= 1.0f) {
        EmitDirectedPreset(kLanceImpactPreset, attackEndPosition_, { -lockedDirection_.x, 0.7f, -lockedDirection_.z }, 1.1f);
        ++rushStep_;
        if (rushStep_ < rushCount) {
            BeginRushStep(rushStep_);
        } else if (rushWingsVisual_) {
            rushWingsVisual_->SetIsVisible(false);
        }
    }
}

void EnemyFalseKingSlime::SpawnRoyalBeams(bool dominionPattern) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(
        dominionPattern ? kCrownDominionAttackId : kRoyalCrossAttackId);
    const int beamCount = dominionPattern ? 6 : (battlePhase_ >= 3 ? 4 : 3);
    const float baseAngle = std::atan2(lockedDirection_.x, lockedDirection_.z);
    for (int index = 0; index < beamCount; ++index) {
        RoyalBeam beam;
        beam.visual = CreateRoyalMesh(
            "FalseKing_RoyalBeam_" + std::to_string(index),
            kRoyalBeamModel,
            { 1.0f, 1.0f, 1.0f, 1.0f },
            dominionPattern ? 2.1f : 1.75f);
        beam.glow = CreateRoyalVisual(
            "FalseKing_RoyalBeamGlow_" + std::to_string(index),
            "Primitives/cube",
            kLaserMaterialType,
            dominionPattern ? Vector4{ 0.82f, 0.16f, 1.0f, 0.38f } : Vector4{ 1.0f, 0.62f, 0.08f, 0.38f },
            dominionPattern ? 2.2f : 1.8f);
        if (!beam.visual) {
            continue;
        }
        beam.origin = FindGroundPoint(GetTranslate());
        beam.origin.y += dominionPattern ? 0.48f : 0.38f;
        beam.delay = static_cast<float>(index) * (dominionPattern ? 0.045f : 0.06f);
        beam.lifetime = attack.activeDuration;
        beam.angle = baseAngle + static_cast<float>(index) * kTwoPi / static_cast<float>(beamCount);
        beam.angularSpeed = (dominionPattern ? 0.56f : 0.78f) * (index % 2 == 0 ? 1.0f : -1.0f);
        beam.length = attack.maxRange;
        beam.width = attack.radius * (dominionPattern ? 0.90f : 1.0f);
        beam.damageTimer = 0.0f;
        beam.visual->SetIsVisible(false);
        beam.visual->UpdateWorldMatrix(true);
        if (beam.glow) {
            beam.glow->SetIsVisible(false);
            beam.glow->UpdateWorldMatrix(true);
        }
        royalBeams_.push_back(std::move(beam));
    }
}

void EnemyFalseKingSlime::UpdateRoyalBeams(float deltaTime) {
    auto beam = royalBeams_.begin();
    while (beam != royalBeams_.end()) {
        if (!beam->visual) {
            beam = royalBeams_.erase(beam);
            continue;
        }
        beam->age += deltaTime;
        const float localAge = beam->age - beam->delay;
        if (localAge < 0.0f) {
            ++beam;
            continue;
        }
        const float progress = std::clamp(localAge / (std::max)(beam->lifetime, 0.01f), 0.0f, 1.0f);
        const float envelope = (std::min)(
            SmoothStep01(progress / 0.12f),
            SmoothStep01((1.0f - progress) / 0.14f));
        beam->angle += beam->angularSpeed * deltaTime;
        const Vector3 direction = { std::sin(beam->angle), 0.0f, std::cos(beam->angle) };
        const Vector3 center = beam->origin + direction * (beam->length * 0.5f);
        const float pulse = 0.92f + std::sin(localAge * 18.0f) * 0.08f;
        beam->visual->SetIsVisible(envelope > 0.01f);
        beam->visual->SetTranslate(center);
        beam->visual->SetRotationY(beam->angle);
        beam->visual->SetScale({ beam->width * pulse, 0.85f + envelope * 0.15f, beam->length * 0.5f });
        beam->visual->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        beam->visual->Update(deltaTime);
        beam->visual->UpdateWorldMatrix(true);
        if (beam->glow) {
            beam->glow->SetIsVisible(envelope > 0.01f);
            beam->glow->SetTranslate(center + Vector3{ 0.0f, 0.08f, 0.0f });
            beam->glow->SetRotationY(beam->angle);
            beam->glow->SetScale({
                beam->width * 0.42f * pulse,
                0.20f + envelope * 0.10f,
                beam->length * 0.5f });
            beam->glow->SetColor({
                battlePhase_ >= 3 ? 0.82f : 1.0f,
                battlePhase_ >= 3 ? 0.16f : 0.56f,
                battlePhase_ >= 3 ? 1.0f : 0.08f,
                envelope * 0.38f });
            beam->glow->Update(deltaTime);
            beam->glow->UpdateWorldMatrix(true);
        }
        if (envelope > 0.65f) {
            DamageTargetOnBeam(*beam, deltaTime);
        }
        if (progress >= 1.0f) {
            beam = royalBeams_.erase(beam);
            continue;
        }
        ++beam;
    }
}

void EnemyFalseKingSlime::UpdateDominion(float deltaTime) {
    if (dominionSigilVisual_) {
        const float duration = (std::max)(attackStateDuration_, 0.01f);
        const float progress = 1.0f - std::clamp(attackTimer_ / duration, 0.0f, 1.0f);
        const float pulse = 2.45f + std::sin(progress * kPi * 10.0f) * 0.14f;
        dominionSigilVisual_->SetIsVisible(true);
        dominionSigilVisual_->SetTranslate(GetTranslate() + Vector3{ 0.0f, 4.8f, 0.0f });
        dominionSigilVisual_->SetRotationY(idleTimer_ * 1.7f);
        dominionSigilVisual_->SetScale({ pulse, 1.0f, pulse });
        dominionSigilVisual_->Update(deltaTime);
        dominionSigilVisual_->UpdateWorldMatrix(true);
    }
    dominionLanceTimer_ -= deltaTime;
    if (dominionLanceTimer_ > 0.0f) {
        return;
    }
    PrepareLanceTargets(true);
    // 一度に全面を埋めず、6本ずつの回転帯にして必ず抜け道を残します。
    const int bandOffset = (actionIndex_ * 5) % static_cast<int>(lanceTargets_.size());
    for (int index = 0; index < 6; ++index) {
        const int targetIndex = (bandOffset + index * 3) % static_cast<int>(lanceTargets_.size());
        SpawnCrownLance(lanceTargets_[static_cast<size_t>(targetIndex)], static_cast<float>(index) * 0.055f);
    }
    ++actionIndex_;
    dominionLanceTimer_ += kDominionLanceInterval;
}

std::unique_ptr<Object3d> EnemyFalseKingSlime::CreateRoyalVisual(
    const std::string& name,
    const std::string& model,
    int materialType,
    const Vector4& color,
    float emissive) const {
    if (!common_) {
        return nullptr;
    }
    auto visual = std::make_unique<Object3d>();
    visual->Initialize(common_);
    visual->SetName(name);
    visual->SetClassName("EnemyVisualPart");
    visual->SetModel(model);
    visual->SetColliderType(ColliderType::kNone);
    visual->SetCollisionAttribute(0);
    visual->SetCollisionMask(0);
    visual->SetMaterialType(materialType);
    // 通常描画の発光コアと特殊マテリアルの外周を重ねても暗くならないようにします。
    visual->SetBlendMode(BlendMode::kAdd);
    visual->SetSelectedLighting(0);
    visual->SetEnableLighting(false);
    visual->SetEnableEnvMap(false);
    visual->SetColor(color);
    visual->SetEmissive(emissive);
    visual->SetRoughness(0.18f);
    visual->SetMetallic(0.0f);

    if (MeshRenderer* renderer = visual->GetMeshRenderer()) {
        if (MeshRenderer::WaterParamForGPU* parameter = renderer->GetWaterParamData()) {
            const bool shockwave = materialType == kShockwaveMaterialType;
            const bool crown = materialType == kCrownMaterialType;
            parameter->waveSpeed = shockwave ? 2.0f : 3.1f;
            parameter->waveHeight = shockwave ? 0.18f : 0.34f;
            parameter->waveFrequency = shockwave ? 5.8f : 3.6f;
            parameter->flowSpeedX = crown ? 0.10f : 0.04f;
            parameter->flowSpeedY = crown ? 0.42f : 0.74f;
            parameter->effectType = shockwave ? 1.0f : 0.0f;
            parameter->effectScale = crown ? 1.18f : 0.88f;
            parameter->effectSoftness = crown ? 0.48f : 0.28f;
            parameter->effectIntensity = crown ? 1.72f : 1.62f;
            parameter->billboardScale = 1.0f;
        }
    }
    return visual;
}

std::unique_ptr<Object3d> EnemyFalseKingSlime::CreateRoyalMesh(
    const std::string& name,
    const std::string& model,
    const Vector4& color,
    float emissive) const {
    if (!common_) {
        return nullptr;
    }
    auto visual = std::make_unique<Object3d>();
    visual->Initialize(common_);
    visual->SetName(name);
    visual->SetClassName("EnemyVisualPart");
    visual->SetModel(model);
    visual->SetColliderType(ColliderType::kNone);
    visual->SetCollisionAttribute(0);
    visual->SetCollisionMask(0);
    visual->SetMaterialType(0);
    visual->SetBlendMode(BlendMode::kNone);
    visual->SetEnableLighting(true);
    visual->SetEnableEnvMap(true);
    visual->SetEnvIntensity(0.72f);
    visual->SetColor(color);
    visual->SetEmissive(emissive);
    visual->SetRoughness(0.22f);
    visual->SetMetallic(0.55f);
    return visual;
}

Vector3 EnemyFalseKingSlime::FindGroundPoint(const Vector3& samplePosition) const {
    return CombatGroundQuery::FindGroundPoint(
        samplePosition, this, { 16.0f, 34.0f }, authoredStateCaptured_ ? authoredPosition_.y : GetTranslate().y);
}

void EnemyFalseKingSlime::DamageTargetAt(
    const Vector3& center,
    float radius,
    float verticalTolerance,
    float damage,
    DamageType damageType,
    const Vector3& knockback) {
    if (!target_ || target_->isDead || !target_->GetIsVisible()) {
        return;
    }
    const Vector3 targetPosition = target_->GetWorldPosition();
    if (std::abs(targetPosition.y - center.y) > verticalTolerance ||
        PlanarDistance(targetPosition, center) > radius) {
        return;
    }
    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = damage;
    event.damageType = damageType;
    event.knockbackVelocity = knockback;
    EventManager::GetInstance()->Dispatch(event);
}

void EnemyFalseKingSlime::PrepareAstralReaver() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kAstralReaverAttackId);
    astralReaverElapsed_ = 0.0f;
    astralReaverImpactAge_ = -1.0f;
    astralReaverDamageApplied_ = false;
    const Vector3 forward = NormalizePlanar(lockedDirection_);
    const Vector3 right{ forward.z, 0.0f, -forward.x };
    const AABB bounds = GetModelWorldAABB();
    const float casterRadius = (std::max)(baseScale_.x * 0.84f,
        (bounds.max.x - bounds.min.x) * 0.5f);
    const float casterHeight = (std::max)(baseScale_.y * 1.2f,
        bounds.max.y - bounds.min.y);
    auto makeEffect = [this](const char* path, const std::string& name) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common_);
        if (!effect->LoadFromJson(path)) return std::unique_ptr<EffectObject3d>{};
        effect->SetName(name);
        effect->SetIsVisible(false);
        effect->Play(10.0f);
        return effect;
    };
    for (size_t index = 0; index < astralReaverOrbs_.size(); ++index) {
        AstralReaverOrb& orb = astralReaverOrbs_[index];
        orb.halo = makeEffect(kAstralReaverCutterHaloEffect,
            "FalseKing_AstralReaverHalo_" + std::to_string(index));
        orb.blade = makeEffect(kAstralReaverCutterBladeEffect,
            "FalseKing_AstralReaverBlade_" + std::to_string(index));
        orb.arc = makeEffect(kAstralReaverCutterArcEffect,
            "FalseKing_AstralReaverArc_" + std::to_string(index));
        orb.core = makeEffect(kAstralReaverOrbEffect,
            "FalseKing_AstralReaverGlint_" + std::to_string(index));
        const float lateral = static_cast<float>(index) - 1.0f;
        orb.origin = GetWorldPosition() + forward * (casterRadius + 0.7f) +
            right * (lateral * 0.82f) +
            Vector3{ 0.0f, casterHeight * (0.57f + 0.13f * static_cast<float>(index % 2)), 0.0f };
        orb.destination = lockedTargetPosition_ +
            right * (lateral * attack.radius * 0.10f) + Vector3{ 0.0f, 1.05f, 0.0f };
        // Editorの攻撃継続時間に合わせ、軌道と着弾の順番を維持します。
        orb.launchDelay = attack.activeDuration * static_cast<float>(index) * 0.072f;
        orb.flightDuration = attack.activeDuration *
            (0.678f + static_cast<float>(index) * 0.028f);
        orb.trailTimer = 0.0f;
    }
    astralReaverTargetRing_ = makeEffect(kAstralReaverRingEffect, "FalseKing_AstralReaverTarget");
    astralReaverImpactCore_ = makeEffect(kAstralReaverOrbEffect, "FalseKing_AstralReaverImpactCore");
    for (size_t index = 0; index < astralReaverImpactRings_.size(); ++index) {
        astralReaverImpactRings_[index] = makeEffect(kAstralReaverRingEffect,
            "FalseKing_AstralReaverImpactRing_" + std::to_string(index));
    }
}

void EnemyFalseKingSlime::UpdateAstralReaver(float deltaTime, bool active) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kAstralReaverAttackId);
    const Vector3 forward = NormalizePlanar(lockedDirection_);
    const Vector3 right{ forward.z, 0.0f, -forward.x };
    const float yaw = std::atan2(forward.x, forward.z);
    const float finalArrival = astralReaverOrbs_.back().launchDelay +
        astralReaverOrbs_.back().flightDuration;
    HideAttackTelegraph();
    const float windupProgress = active ? 1.0f :
        1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f);
    if (astralReaverTargetRing_) {
        astralReaverTargetRing_->Update(deltaTime);
        const float fade = active ? std::clamp((finalArrival - astralReaverElapsed_) * 2.0f, 0.0f, 1.0f)
            : std::clamp(windupProgress * 2.4f, 0.0f, 1.0f);
        astralReaverTargetRing_->SetIsVisible(fade > 0.01f);
        astralReaverTargetRing_->SetTranslate(lockedTargetPosition_ + Vector3{ 0.0f, 0.10f, 0.0f });
        astralReaverTargetRing_->SetRotation({ 0.0f, idleTimer_ * 0.55f, 0.0f });
        astralReaverTargetRing_->SetScale({ attack.radius, 1.0f, attack.radius });
        astralReaverTargetRing_->SetColor({ 0.80f, 0.25f, 1.0f, fade * 0.58f });
        astralReaverTargetRing_->UpdateLocalMatrix();
        astralReaverTargetRing_->UpdateWorldMatrix();
    }
    if (!active) {
        for (size_t index = 0; index < astralReaverOrbs_.size(); ++index) {
            AstralReaverOrb& orb = astralReaverOrbs_[index];
            // 発射直前に輪郭を組み立て、詠唱中の術式を隠さないようにします。
            const float assembly = std::clamp((windupProgress - 0.66f) / 0.34f, 0.0f, 1.0f);
            const float size = 0.42f + assembly * 0.58f;
            const float phase = idleTimer_ * 5.2f + static_cast<float>(index) * 1.8f;
            const Vector3 rotation{ 1.06f, yaw, 0.20f + std::sin(phase) * 0.13f };
            const Vector3 haloRotation{ rotation.x - 0.16f, rotation.y - 0.08f, rotation.z - 0.14f };
            const Vector3 arcRotation{ rotation.x + 0.13f, rotation.y,
                rotation.z + phase * 0.65f };
            const float rimPhase = phase * 2.7f;
            const Vector3 glintOffset = AstralReaverRimOffset(rotation,
                { 1.45f * size, 1.0f, 0.98f * size }, rimPhase);
            PlaceMagicLayer(orb.halo.get(), deltaTime, assembly > 0.01f,
                orb.origin, haloRotation, { 1.65f * size, size, 1.12f * size },
                { 0.78f, 0.18f, 1.0f, assembly * 0.44f });
            PlaceMagicLayer(orb.blade.get(), deltaTime, assembly > 0.01f,
                orb.origin, rotation, { 1.45f * size, size, 0.98f * size },
                { 1.0f, 0.75f, 1.0f, assembly * 0.72f });
            PlaceMagicLayer(orb.arc.get(), deltaTime, assembly > 0.01f,
                orb.origin, arcRotation, { 1.82f * size, size, 1.20f * size },
                { 1.0f, 0.38f, 1.0f, assembly * 0.62f });
            PlaceMagicLayer(orb.core.get(), deltaTime, assembly > 0.01f,
                orb.origin + glintOffset, {}, { 0.24f * size, 0.24f * size, 0.24f * size },
                { 1.0f, 0.88f, 1.0f, assembly * 0.96f });
            if (assembly > 0.08f) {
                orb.trailTimer -= deltaTime;
                if (orb.trailTimer <= 0.0f) {
                    // 外側の光を回転する刃先へ吸い込み、発射前に密度を上げます。
                    const Vector3 outerOffset = AstralReaverRimOffset(rotation,
                        { 2.10f * size, 1.0f, 1.42f * size }, rimPhase + 0.34f);
                    EmitDirectedPreset(kAstralReaverChargePreset, orb.origin + outerOffset,
                        glintOffset - outerOffset, 1.0f + assembly * 0.35f);
                    orb.trailTimer = 0.045f;
                }
            } else {
                orb.trailTimer = 0.0f;
            }
        }
        return;
    }

    astralReaverElapsed_ += deltaTime;
    for (size_t index = 0; index < astralReaverOrbs_.size(); ++index) {
        AstralReaverOrb& orb = astralReaverOrbs_[index];
        const float t = std::clamp((astralReaverElapsed_ - orb.launchDelay) /
            orb.flightDuration, 0.0f, 1.0f);
        const Vector3 previousPosition = AstralReaverFlightPosition(orb.origin,
            orb.destination, forward, right, t - 0.012f, index, attack.radius);
        const Vector3 nextPosition = AstralReaverFlightPosition(orb.origin,
            orb.destination, forward, right, t + 0.012f, index, attack.radius);
        Vector3 position = AstralReaverFlightPosition(orb.origin,
            orb.destination, forward, right, t, index, attack.radius);
        if (t >= 1.0f && astralReaverImpactAge_ < 0.0f) {
            // 先着した輪は急に位置を飛ばさず、標的の周囲へ回り込みます。
            const float orbit = astralReaverElapsed_ * 8.0f + static_cast<float>(index) * 2.1f;
            const Vector3 orbitPosition = lockedTargetPosition_ + right * (std::cos(orbit) * 0.72f) +
                forward * (std::sin(orbit) * 0.72f) + Vector3{ 0.0f, 1.15f, 0.0f };
            const float holdAge = astralReaverElapsed_ - orb.launchDelay - orb.flightDuration;
            const float blend = MagicEase(holdAge / 0.14f);
            position = position + (orbitPosition - position) * blend;
        }
        const bool visible = astralReaverElapsed_ >= orb.launchDelay && astralReaverImpactAge_ < 0.20f;
        const float fade = astralReaverImpactAge_ < 0.0f ? 1.0f :
            std::clamp(1.0f - astralReaverImpactAge_ / 0.20f, 0.0f, 1.0f);
        const float phase = astralReaverElapsed_ * 12.0f + static_cast<float>(index) * 1.8f;
        const Vector3 pathTangent = nextPosition - previousPosition;
        const float pathYaw = pathTangent.x * pathTangent.x +
            pathTangent.z * pathTangent.z > 0.0001f ?
            std::atan2(pathTangent.x, pathTangent.z) : yaw;
        const Vector3 rotation{ 1.06f + std::sin(phase * 0.50f) * 0.10f,
            pathYaw,
            0.20f + std::sin(phase) * 0.20f };
        const Vector3 haloRotation{ rotation.x - 0.16f, rotation.y - 0.08f,
            rotation.z - 0.14f };
        const Vector3 arcRotation{ rotation.x + 0.13f, rotation.y,
            rotation.z + phase * 0.75f };
        const float rimPhase = phase * 1.4f;
        const Vector3 glintOffset = AstralReaverRimOffset(rotation,
            { 1.45f, 1.0f, 0.98f }, rimPhase);
        PlaceMagicLayer(orb.halo.get(), deltaTime, visible, position,
            haloRotation, { 1.65f, 1.0f, 1.12f },
            { 0.78f, 0.18f, 1.0f, fade * 0.44f });
        PlaceMagicLayer(orb.blade.get(), deltaTime, visible, position,
            rotation, { 1.45f, 1.0f, 0.98f },
            { 1.0f, 0.75f, 1.0f, fade * 0.78f });
        PlaceMagicLayer(orb.arc.get(), deltaTime, visible, position,
            arcRotation, { 1.82f, 1.0f, 1.20f },
            { 1.0f, 0.38f, 1.0f, fade * 0.66f });
        PlaceMagicLayer(orb.core.get(), deltaTime, visible, position + glintOffset,
            {}, { 0.24f, 0.24f, 0.24f },
            { 1.0f, 0.88f, 1.0f, fade * 0.98f });
        orb.trailTimer -= deltaTime;
        if (astralReaverElapsed_ >= orb.launchDelay && t < 1.0f && orb.trailTimer <= 0.0f) {
            // 刃先から後方へ光跡を残し、輪が一つの流れとして見えるようにします。
            const Vector3 trailDirection = previousPosition - nextPosition;
            EmitDirectedPreset(kAstralReaverTrailPreset, position + glintOffset,
                trailDirection, 1.0f);
            EmitDirectedPreset(kAstralReaverTrailPreset, position - glintOffset,
                trailDirection, 0.70f);
            const Vector3 tangent = AstralReaverRimOffset(rotation,
                { 1.45f, 1.0f, 0.98f }, rimPhase + 0.28f) - glintOffset;
            EmitDirectedPreset(kAstralReaverChargePreset, position + glintOffset,
                tangent, 1.4f);
            orb.trailTimer = 0.045f;
        }
    }
    if (!astralReaverDamageApplied_ && astralReaverElapsed_ >= finalArrival) {
        astralReaverDamageApplied_ = true;
        astralReaverImpactAge_ = 0.0f;
        DamageTargetAt(lockedTargetPosition_, attack.radius, 3.2f,
            attack.damage, DamageType::Physical,
            lockedDirection_ * 4.0f + Vector3{ 0.0f, 2.2f, 0.0f });
        EmitPreset(kAstralReaverImpactPreset,
            lockedTargetPosition_ + Vector3{ 0.0f, 1.1f, 0.0f });
    }
    if (astralReaverImpactAge_ < 0.0f) return;
    astralReaverImpactAge_ += deltaTime;
    const float fade = std::clamp(1.0f - astralReaverImpactAge_ / 0.72f, 0.0f, 1.0f);
    if (astralReaverImpactCore_) {
        astralReaverImpactCore_->Update(deltaTime);
        astralReaverImpactCore_->SetIsVisible(fade > 0.01f);
        astralReaverImpactCore_->SetTranslate(lockedTargetPosition_ + Vector3{ 0.0f, 1.0f, 0.0f });
        const float size = 0.45f + astralReaverImpactAge_ * 2.8f;
        astralReaverImpactCore_->SetScale({ size, size, size });
        astralReaverImpactCore_->SetColor({ 0.92f, 0.68f, 1.0f, fade * 0.64f });
        astralReaverImpactCore_->UpdateLocalMatrix();
        astralReaverImpactCore_->UpdateWorldMatrix();
    }
    for (size_t index = 0; index < astralReaverImpactRings_.size(); ++index) {
        auto& ring = astralReaverImpactRings_[index];
        if (!ring) continue;
        ring->Update(deltaTime);
        const float ringAge = astralReaverImpactAge_ - static_cast<float>(index) * 0.065f;
        ring->SetIsVisible(ringAge >= 0.0f && fade > 0.01f);
        ring->SetTranslate(lockedTargetPosition_ + Vector3{ 0.0f, 1.0f, 0.0f });
        const float angle = static_cast<float>(index) * (kPi / 3.0f);
        ring->SetRotation({ angle + ringAge * 1.8f,
            ringAge * 2.4f + static_cast<float>(index) * 1.9f,
            angle * 0.7f });
        const float size = (0.65f + (std::max)(ringAge, 0.0f) * 4.6f) *
            (0.82f + static_cast<float>(index) * 0.16f);
        ring->SetScale({ size, size, size });
        ring->SetColor({ 0.76f, 0.24f, 1.0f, fade * 0.68f });
        ring->UpdateLocalMatrix();
        ring->UpdateWorldMatrix();
    }
}

void EnemyFalseKingSlime::PrepareNightfallLances() {
    nightfallLances_ = {};
    auto makeEffect = [this](const char* path, const std::string& name) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common_);
        if (!effect->LoadFromJson(path)) return std::unique_ptr<EffectObject3d>{};
        effect->SetName(name);
        effect->SetIsVisible(false);
        effect->Play(10.0f);
        return effect;
    };
    nightfallLances_.targetSigil = makeEffect(kUmbraFlameSigilEffect, "FalseKing_NightfallLancesTargetSigil");
    if (nightfallLances_.targetSigil) nightfallLances_.targetSigil->SetUseTextureColor(false);
    nightfallLances_.targetRing = makeEffect(kNightfallLancesRingEffect, "FalseKing_NightfallLancesTargetRing");
    nightfallLances_.innerRing = makeEffect(kNightfallLancesRingEffect, "FalseKing_NightfallLancesInnerRing");
    nightfallLances_.spearCore = makeEffect(kNightfallLancesCoreEffect, "FalseKing_NightfallLancesCore");
    nightfallLances_.spearGlow = makeEffect(kNightfallLancesGlowEffect, "FalseKing_NightfallLancesGlow");
    for (size_t index = 0; index < nightfallLances_.barbs.size(); ++index) {
        nightfallLances_.barbs[index] = makeEffect(kNightfallLancesCoreEffect,
            "FalseKing_NightfallLancesBarb_" + std::to_string(index));
    }
    nightfallLances_.impactRing = makeEffect(kNightfallLancesRingEffect, "FalseKing_NightfallLancesImpactRing");
    nightfallLances_.impactCore = makeEffect(kAstralReaverOrbEffect, "FalseKing_NightfallLancesImpactCore");
    nightfallLances_.blastRing = makeEffect(kNightfallLancesRingEffect, "FalseKing_NightfallLancesBlastRing");
    nightfallLances_.blastShell = makeEffect(kAstralReaverOrbEffect, "FalseKing_NightfallLancesBlastShell");
}

void EnemyFalseKingSlime::UpdateNightfallLances(float deltaTime, bool active) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kNightfallLancesAttackId);
    HideAttackTelegraph();
    const Vector3 ground = lockedTargetPosition_;
    const float windup = active ? 1.0f :
        1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f);
    if (active) nightfallLances_.activeAge += deltaTime;
    const float age = nightfallLances_.activeAge;
    const float impactTime = 0.68f;
    const float pulseTime = 1.40f;
    const float damageTime = 1.64f;
    const float fallStart = 0.22f;
    const float fall = active ? MagicEase((age - fallStart) / (impactTime - fallStart)) : 0.0f;
    const float groundFade = active ?
        std::clamp(1.0f - (std::max)(0.0f, age - damageTime) / 0.40f, 0.0f, 1.0f)
        : MagicEase(windup / 0.30f);
    const float sigilScale = attack.radius / 1.2f;
    PlaceMagicLayer(nightfallLances_.targetSigil.get(), deltaTime, groundFade > 0.01f,
        ground + Vector3{ 0.0f, 0.15f, 0.0f }, { 0.0f, idleTimer_ * 0.21f, 0.0f },
        { sigilScale, 1.0f, sigilScale }, { 1.0f, 0.10f, 0.18f, groundFade * 0.82f });
    if (nightfallLances_.targetSigil) nightfallLances_.targetSigil->SetRevealProgress(
        active ? 1.0f : MagicEase(windup));
    PlaceMagicLayer(nightfallLances_.targetRing.get(), deltaTime, groundFade > 0.01f,
        ground + Vector3{ 0.0f, 0.19f, 0.0f }, { 0.0f, -idleTimer_ * 0.40f, 0.0f },
        { attack.radius, 1.0f, attack.radius }, { 1.0f, 0.08f, 0.16f, groundFade * 0.82f });
    PlaceMagicLayer(nightfallLances_.innerRing.get(), deltaTime, groundFade > 0.01f,
        ground + Vector3{ 0.0f, 0.21f, 0.0f }, { 0.0f, idleTimer_ * 0.65f, 0.0f },
        { attack.radius * 0.54f, 1.0f, attack.radius * 0.54f },
        { 0.76f, 0.04f, 0.18f, groundFade * 0.58f });

    // 詠唱後半に槍を標的の上空へ組み立て、落下前に逃げる余地を残します。
    const float assembly = active ? 1.0f : MagicEase((windup - 0.42f) / 0.48f);
    const float visibleScale = (std::max)(0.05f, assembly);
    const float dissolveProgress = active ? MagicEase((age - damageTime) / 0.52f) : 0.0f;
    const float spearFade = active ? (dissolveProgress < 1.0f ? 1.0f : 0.0f) : assembly;
    const float spearHeight = 14.0f - fall * 9.2f;
    const Vector3 spearCenter = ground + Vector3{ 0.0f, spearHeight, 0.0f };
    const float spin = idleTimer_ * 1.9f;
    PlaceMagicLayer(nightfallLances_.spearCore.get(), deltaTime, spearFade > 0.01f,
        spearCenter, { 0.0f, spin, 0.0f },
        { 1.50f * visibleScale, 1.20f * visibleScale, 1.50f * visibleScale },
        { 0.045f, 0.018f, 0.09f, spearFade * 0.98f });
    PlaceMagicLayer(nightfallLances_.spearGlow.get(), deltaTime, spearFade > 0.01f,
        spearCenter, { 0.0f, -spin * 0.8f, 0.0f },
        { 1.62f * visibleScale, 1.22f * visibleScale, 1.62f * visibleScale },
        { 0.87f, 0.10f, 0.36f, spearFade * (0.35f - dissolveProgress * 0.16f) });
    for (size_t index = 0; index < nightfallLances_.barbs.size(); ++index) {
        const float angle = spin + static_cast<float>(index) * kTwoPi / 3.0f;
        const Vector3 offset{ std::cos(angle) * 1.08f, 1.15f,
            std::sin(angle) * 1.08f };
        PlaceMagicLayer(nightfallLances_.barbs[index].get(), deltaTime, spearFade > 0.01f,
            spearCenter + offset, { 0.0f, angle, 0.40f },
            { 0.34f * visibleScale, 0.48f * visibleScale, 0.34f * visibleScale },
            { 0.25f, 0.025f, 0.19f, spearFade * 0.92f });
    }

    // Update が再計算するディゾルブ値を描画直前に上書きし、槍の形を保ったまま粒状に削ります。
    auto applySpearDissolve = [dissolveProgress](EffectObject3d* effect) {
        if (effect && effect->GetMaterialData()) {
            effect->GetMaterialData()->dissolveFade = dissolveProgress * 0.88f;
        }
    };
    applySpearDissolve(nightfallLances_.spearCore.get());
    applySpearDissolve(nightfallLances_.spearGlow.get());
    for (const auto& barb : nightfallLances_.barbs) applySpearDissolve(barb.get());

    if (!active || age < damageTime) {
        nightfallLances_.dissolveParticleTimer = 0.0f;
    } else if (dissolveProgress < 1.0f) {
        nightfallLances_.dissolveParticleTimer -= deltaTime;
        if (nightfallLances_.dissolveParticleTimer <= 0.0f) {
            for (int section = 0; section < 3; ++section) {
                const float angle = idleTimer_ * 11.0f + static_cast<float>(section) * kTwoPi / 3.0f;
                const float height = 1.6f + static_cast<float>(section) * 2.7f;
                const Vector3 outward{ std::cos(angle), 0.55f, std::sin(angle) };
                EmitDirectedPreset(kNightfallLancesDissolvePreset,
                    ground + Vector3{ outward.x * 0.65f, height, outward.z * 0.65f },
                    outward, 1.3f);
            }
            nightfallLances_.dissolveParticleTimer = 0.065f;
        }
    }

    nightfallLances_.particleTimer -= deltaTime;
    if (nightfallLances_.particleTimer <= 0.0f &&
        (active ? age < pulseTime : assembly > 0.05f)) {
        const float phase = idleTimer_ * 7.0f;
        if (active && age >= impactTime && age < pulseTime) {
            // 着地した槍へ外周の高所からエネルギーが流れ込み、第二波を溜めます。
            const float radius = attack.radius * 0.82f;
            const Vector3 source = ground + Vector3{
                std::cos(phase) * radius, 5.0f + std::sin(phase * 0.53f) * 2.0f,
                std::sin(phase) * radius };
            const Vector3 destination = ground + Vector3{ 0.0f, 1.1f, 0.0f };
            EmitDirectedPreset(kNightfallLancesChargePreset, source,
                destination - source, 4.6f);
        } else {
            const Vector3 orbit{ std::cos(phase) * 2.15f, 0.0f, std::sin(phase) * 2.15f };
            EmitDirectedPreset(kNightfallLancesChargePreset, spearCenter + orbit,
                Vector3{ -orbit.x, 0.45f, -orbit.z }, 1.0f);
        }
        if (active && age < impactTime) {
            EmitDirectedPreset(kNightfallLancesTrailPreset,
                spearCenter + Vector3{ 0.0f, 1.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 1.2f);
        }
        nightfallLances_.particleTimer += 0.045f;
    }
    if (active && age >= impactTime && age - deltaTime < impactTime) {
        EmitPreset(kNightfallLancesImpactPreset, ground + Vector3{ 0.0f, 0.30f, 0.0f });
    }
    if (active && age >= pulseTime && age - deltaTime < pulseTime) {
        // 収束後にだけ広域衝撃を放ち、槍の着地と範囲ダメージを別の拍にします。
        EmitPreset(kNightfallLancesImpactPreset, ground + Vector3{ 0.0f, 0.65f, 0.0f });
        for (int spoke = 0; spoke < 8; ++spoke) {
            const float angle = static_cast<float>(spoke) * kTwoPi / 8.0f;
            EmitDirectedPreset(kNightfallLancesTrailPreset,
                ground + Vector3{ 0.0f, 0.32f, 0.0f },
                { std::cos(angle), 0.08f, std::sin(angle) }, 16.0f);
        }
    }
    if (active && !nightfallLances_.damageApplied && age >= damageTime) {
        nightfallLances_.damageApplied = true;
        // 衝撃波が判定半径に届いた瞬間、外周も噴き上げて広い攻撃範囲を見せます。
        for (int sector = 0; sector < 12; ++sector) {
            const float angle = static_cast<float>(sector) * kTwoPi / 12.0f;
            const float radius = attack.radius * 0.85f;
            EmitPreset(kNightfallLancesRimPreset,
                ground + Vector3{ std::cos(angle) * radius, 0.24f,
                    std::sin(angle) * radius });
        }
        DamageTargetAt(ground, attack.radius, 3.0f, attack.damage, DamageType::Physical,
            { 0.0f, 5.5f, 0.0f });
        if (battlePhase_ == 2) TryBreakCrownSeal(ground, sealAttackStage_);
    }
    const float impactFade = active && age >= impactTime ?
        std::clamp(1.0f - (std::max)(0.0f, age - damageTime) / 0.40f, 0.0f, 1.0f) : 0.0f;
    const float charge = std::clamp((age - impactTime) / (pulseTime - impactTime), 0.0f, 1.0f);
    const float expansion = std::clamp((age - pulseTime) / (damageTime - pulseTime), 0.0f, 1.0f);
    const float impactRadius = age < pulseTime ? 1.05f + charge * 0.60f :
        1.65f + (attack.radius - 1.65f) * MagicEase(expansion);
    PlaceMagicLayer(nightfallLances_.impactRing.get(), deltaTime, impactFade > 0.01f,
        ground + Vector3{ 0.0f, 0.16f, 0.0f }, {},
        { impactRadius, 1.0f, impactRadius },
        { 1.0f, 0.20f, 0.32f, impactFade * 0.88f });
    PlaceMagicLayer(nightfallLances_.impactCore.get(), deltaTime, impactFade > 0.01f,
        ground + Vector3{ 0.0f, age < pulseTime ? 4.6f - charge * 3.8f : 0.8f, 0.0f }, {},
        { 0.45f + charge * 0.75f + expansion * 2.0f,
            0.45f + charge * 0.75f + expansion * 2.0f,
            0.45f + charge * 0.75f + expansion * 2.0f },
        { 1.0f, 0.38f, 0.76f, impactFade * (0.92f - expansion * 0.62f) });

    // 地表の輪だけに頼らず、体積のある閃光と一段高い衝撃輪を同時に広げます。
    const float blastAge = age - pulseTime;
    const float blastProgress = std::clamp(blastAge / (damageTime - pulseTime), 0.0f, 1.0f);
    const float blastFade = active && blastAge >= 0.0f ?
        std::pow(1.0f - std::clamp(blastAge / 0.58f, 0.0f, 1.0f), 1.5f) : 0.0f;
    const float blastRadius = attack.radius * (0.12f + 0.88f * MagicEase(blastProgress));
    PlaceMagicLayer(nightfallLances_.blastShell.get(), deltaTime, blastFade > 0.01f,
        ground + Vector3{ 0.0f, blastRadius * 0.40f, 0.0f }, {},
        { blastRadius, blastRadius * 0.44f, blastRadius },
        { 0.55f, 0.65f, 1.0f, blastFade * 0.29f });
    PlaceMagicLayer(nightfallLances_.blastRing.get(), deltaTime, blastFade > 0.01f,
        ground + Vector3{ 0.0f, 0.65f + blastProgress * 1.20f, 0.0f },
        { 0.0f, -idleTimer_ * 0.70f, 0.0f },
        { blastRadius, 1.0f, blastRadius },
        { 0.95f, 0.60f, 1.0f, blastFade * 0.92f });
}

void EnemyFalseKingSlime::PrepareSealNightfallSatellites() {
    sealNightfallSatellites_ = {};
    auto makeEffect = [this](const char* path, const std::string& name) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common_);
        if (!effect->LoadFromJson(path)) return std::unique_ptr<EffectObject3d>{};
        effect->SetName(name);
        effect->SetIsVisible(false);
        effect->Play(10.0f);
        return effect;
    };
    for (size_t index = 0; index < sealNightfallSatellites_.size(); ++index) {
        auto& satellite = sealNightfallSatellites_[index];
        const std::string suffix = std::to_string(index);
        satellite.sigil = makeEffect(kNightfallLancesRingEffect, "FalseKing_SealSpearSigil_" + suffix);
        satellite.core = makeEffect(kNightfallLancesCoreEffect, "FalseKing_SealSpearCore_" + suffix);
        satellite.glow = makeEffect(kNightfallLancesGlowEffect, "FalseKing_SealSpearGlow_" + suffix);
        satellite.impactAge = 0.92f + 0.23f * static_cast<float>(index);
    }
}

void EnemyFalseKingSlime::UpdateSealNightfallSatellites(float deltaTime, bool active) {
    if (battlePhase_ != 2) return;
    const Vector3 center = lockedTargetPosition_;
    const Vector3 forward = NormalizePlanar(center - GetTranslate());
    const Vector3 right{ forward.z, 0.0f, -forward.x };
    const float spread = CrownFinaleTuning::Get().sealSatelliteSpread;
    const float age = nightfallLances_.activeAge;
    for (size_t index = 0; index < sealNightfallSatellites_.size(); ++index) {
        auto& satellite = sealNightfallSatellites_[index];
        const float side = static_cast<float>(index) - 1.0f;
        const Vector3 offset = right * (side * spread) + forward * (index == 1 ? spread * 0.85f : spread * 0.25f);
        satellite.ground = FindGroundPoint(center + offset);
        const float untilImpact = satellite.impactAge - age;
        const float fall = active ? MagicEase((age - satellite.impactAge + 0.34f) / 0.34f) : 0.0f;
        const float fade = active ? std::clamp((satellite.impactAge + 0.38f - age) / 0.38f, 0.0f, 1.0f) : 1.0f;
        const float warning = active ? std::clamp(untilImpact / 0.45f, 0.0f, 1.0f) : 1.0f;
        PlaceMagicLayer(satellite.sigil.get(), deltaTime, warning > 0.01f,
            satellite.ground + Vector3{ 0.0f, 0.23f, 0.0f }, { 0.0f, idleTimer_ * 0.55f, 0.0f },
            { 1.45f, 1.0f, 1.45f }, { 0.95f, 0.18f, 0.70f, warning * 0.80f });
        const Vector3 spearCenter = satellite.ground + Vector3{ 0.0f, 10.5f - fall * 7.1f, 0.0f };
        const bool spearVisible = (!active || age < satellite.impactAge + 0.38f) && fade > 0.01f;
        PlaceMagicLayer(satellite.core.get(), deltaTime, spearVisible,
            spearCenter, { 0.0f, idleTimer_ * 2.5f, 0.0f },
            { 0.68f, 0.75f, 0.68f }, { 0.08f, 0.02f, 0.16f, fade });
        PlaceMagicLayer(satellite.glow.get(), deltaTime, spearVisible,
            spearCenter, { 0.0f, -idleTimer_ * 2.0f, 0.0f },
            { 0.78f, 0.80f, 0.78f }, { 0.88f, 0.14f, 0.72f, fade * 0.55f });
        if (active && !satellite.damageApplied && age >= satellite.impactAge) {
            satellite.damageApplied = true;
            EmitPreset(kNightfallLancesImpactPreset, satellite.ground + Vector3{ 0.0f, 0.25f, 0.0f });
            DamageTargetAt(satellite.ground, 1.5f, 2.8f,
                GetAttackDefinition(kNightfallLancesAttackId).damage * 0.42f,
                DamageType::Physical, { 0.0f, 3.0f, 0.0f });
        }
    }
}

void EnemyFalseKingSlime::DamageTargetOnBeam(RoyalBeam& beam, float deltaTime) {
    if (!target_ || target_->isDead || !target_->GetIsVisible()) {
        return;
    }
    beam.damageTimer = (std::max)(0.0f, beam.damageTimer - deltaTime);
    if (beam.damageTimer > 0.0f) {
        return;
    }
    const Vector3 direction = { std::sin(beam.angle), 0.0f, std::cos(beam.angle) };
    const Vector3 toTarget = target_->GetWorldPosition() - beam.origin;
    const float projection = toTarget.x * direction.x + toTarget.z * direction.z;
    if (projection < 0.0f || projection > beam.length ||
        std::abs(target_->GetWorldPosition().y - beam.origin.y) > 3.0f) {
        return;
    }
    const float lateralX = toTarget.x - direction.x * projection;
    const float lateralZ = toTarget.z - direction.z * projection;
    const float lateralDistance = std::sqrt(lateralX * lateralX + lateralZ * lateralZ);
    if (lateralDistance > beam.width + target_->GetCollisionRadius()) {
        return;
    }
    const EnemyAttackDefinition& attack = GetAttackDefinition(
        currentAttack_ == AttackKind::CrownDominion ? kCrownDominionAttackId : kRoyalCrossAttackId);
    const Vector3 side = { direction.z, 0.0f, -direction.x };
    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = attack.damage;
    event.damageType = DamageType::Electric;
    event.knockbackVelocity = { side.x * 7.0f, 3.8f, side.z * 7.0f };
    EventManager::GetInstance()->Dispatch(event);
    beam.damageTimer = 0.72f;
}

void EnemyFalseKingSlime::BeginShockwaveJump() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kRoyalShockwaveAttackId);
    attackStartPosition_ = FindGroundPoint(GetTranslate());
    Vector3 toTarget = lockedTargetPosition_ - attackStartPosition_;
    toTarget.y = 0.0f;
    const float targetDistance = Math::Length(toTarget);
    lockedDirection_ = targetDistance > 0.001f ? toTarget / targetDistance : lockedDirection_;
    const float maximumTravel = std::clamp(attack.recommendedTargetDistance, 6.0f, 14.0f);
    const float travelDistance = (std::min)(targetDistance, maximumTravel);
    attackEndPosition_ = FindGroundPoint(attackStartPosition_ + lockedDirection_ * travelDistance);

    shockwaveJumpTimer_ = 0.0f;
    shockwaveJumpDuration_ = std::clamp(attack.activeDuration * 0.31f, 0.74f, 1.05f);
    shockwaveJumpHeight_ = 5.6f + static_cast<float>(battlePhase_) * 0.85f;
    shockwaveLanded_ = false;
    shockwaveChargeVisual_.reset();
    SetGrounded(true);
    SetVelocity({});
    EmitDirectedPreset(kShockwaveDustPreset, attackStartPosition_, { 0.0f, 1.0f, 0.0f }, 0.58f);
}

void EnemyFalseKingSlime::UpdateShockwaveJump(float deltaTime, Vector3& velocity) {
    velocity = {};
    if (shockwaveLanded_) {
        shockwaveJumpTimer_ += deltaTime;
        HideAttackTelegraph();
        return;
    }

    shockwaveJumpTimer_ += deltaTime;
    const float progress = std::clamp(
        shockwaveJumpTimer_ / (std::max)(shockwaveJumpDuration_, 0.01f), 0.0f, 1.0f);
    Vector3 position = LerpVector3(attackStartPosition_, attackEndPosition_, SmoothStep01(progress));
    position.y += std::sin(progress * kPi) * shockwaveJumpHeight_;
    SetTranslate(position);
    SetGrounded(true);
    UpdateFacing(lockedDirection_, 0.34f);

    const EnemyAttackDefinition& attack = GetAttackDefinition(kRoyalShockwaveAttackId);
    ShowAttackTelegraphCircle(
        attackEndPosition_,
        (std::max)(2.8f, attack.radius * 3.4f),
        SmoothStep01(progress),
        battlePhase_ >= 3
            ? Vector4{ 0.90f, 0.18f, 1.0f, 0.78f }
            : Vector4{ 1.0f, 0.62f, 0.10f, 0.78f });

    effectTimer_ -= deltaTime;
    if (effectTimer_ <= 0.0f && progress > 0.10f && progress < 0.92f) {
        EmitDirectedPreset(
            kChargePreset,
            position + Vector3{ 0.0f, baseScale_.y * 0.72f, 0.0f },
            { -lockedDirection_.x, 0.16f, -lockedDirection_.z },
            0.78f + progress * 0.44f);
        effectTimer_ += 0.12f;
    }

    if (progress >= 1.0f) {
        FinishShockwaveLanding();
    }
}

void EnemyFalseKingSlime::FinishShockwaveLanding() {
    if (shockwaveLanded_) return;
    shockwaveLanded_ = true;
    SetTranslate(attackEndPosition_);
    SetGrounded(true);
    SetVelocity({});
    HideAttackTelegraph();

    const Vector3 impactPosition = FindGroundPoint(attackEndPosition_);
    TryBreakCrownSeal(impactPosition, 1);
    SpawnRoyalWaves();
    EmitDirectedPreset(kShockwavePreset, impactPosition, { 0.0f, 1.0f, 0.0f }, 1.72f);
    EmitDirectedPreset(kShockwaveDustPreset, impactPosition, { 0.0f, 1.0f, 0.0f }, 1.32f);

    const EnemyAttackDefinition& attack = GetAttackDefinition(kRoyalShockwaveAttackId);
    DamageTargetAt(
        impactPosition,
        (std::max)(2.7f, attack.radius * 3.1f),
        3.2f,
        attack.damage,
        DamageType::Explosion,
        { lockedDirection_.x * 6.5f, 7.2f, lockedDirection_.z * 6.5f });

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->StartShake(
            0.24f,
            battlePhase_ >= 3 ? 0.085f : 0.065f,
            27.0f,
            { 0.58f, 0.38f, 0.20f });
    }
}

void EnemyFalseKingSlime::PrepareUmbraFlames() {
    umbraFlames_.clear();
    umbraFlameFanHit_ = false;
    const EnemyAttackDefinition& attack = GetAttackDefinition(kUmbraFlameAttackId);
    const float nearDistance = 1.5f;
    const float farDistance = (std::max)(attack.maxRange * 0.90f, 20.0f);
    const float fanLength = farDistance - nearDistance;
    const float farWidth = farDistance * 0.94f * attack.radius / 1.75f;
    const Vector3 forward = NormalizePlanar(lockedDirection_);
    const Vector3 right{ forward.z, 0.0f, -forward.x };
    // 手前の一点から中段二点、奥三点へ広がる扇状の術式です。
    const std::array<Vector2, 6> nodes = {
        Vector2{ nearDistance + fanLength * 0.148f, 0.0f },
        Vector2{ nearDistance + fanLength * 0.49f, -farWidth * 0.193f },
        Vector2{ nearDistance + fanLength * 0.49f, farWidth * 0.193f },
        Vector2{ nearDistance + fanLength * 0.879f, -farWidth * 0.380f },
        Vector2{ nearDistance + fanLength * 0.879f, 0.0f },
        Vector2{ nearDistance + fanLength * 0.879f, farWidth * 0.380f }
    };
    umbraFlameNetwork_ = std::make_unique<EffectObject3d>();
    umbraFlameNetwork_->Initialize(common_);
    if (!umbraFlameNetwork_->LoadFromJson(kUmbraFlameNetworkEffect)) {
        umbraFlameNetwork_.reset();
    } else {
        umbraFlameNetwork_->SetName("FalseKing_UmbraFlameNetwork");
        umbraFlameNetwork_->SetRevealProgress(0.0f);
        umbraFlameNetwork_->SetIsVisible(false);
        umbraFlameNetwork_->Play(10.0f);
    }
    for (size_t index = 0; index < nodes.size(); ++index) {
        UmbraFlamePortal portal;
        portal.groundPosition = FindGroundPoint(attackStartPosition_ +
            forward * nodes[index].x + right * nodes[index].y);
        portal.delay = static_cast<float>(index) * 0.12f;
        portal.age = -portal.delay;
        portal.sigil = std::make_unique<EffectObject3d>();
        portal.sigil->Initialize(common_);
        if (!portal.sigil->LoadFromJson(kUmbraFlameSigilEffect)) {
            portal.sigil.reset();
        } else {
            portal.sigil->SetName("FalseKing_UmbraFlameSigil_" + std::to_string(index));
            portal.sigil->SetRevealProgress(0.0f);
            portal.sigil->SetIsVisible(false);
            portal.sigil->Play(10);
        }
        // 一つの閉じた体積で扇全体を描き、術式の六点は粒子の起点として使います。
        if (index == 0) {
            portal.flame = CreateRoyalVisual(
                "FalseKing_UmbraFlameFanFlame", "Primitives/cylinder", kFireMaterialType,
                { 0.72f, 0.38f, 1.0f, 0.0f }, 2.4f);
        }
        if (portal.flame) {
            portal.flame->SetBlendMode(BlendMode::kNormal);
            portal.flame->SetIsVisible(false);
            if (MeshRenderer* renderer = portal.flame->GetMeshRenderer()) {
                if (auto* fire = renderer->GetWaterParamData()) {
                    fire->effectType = 7.0f;
                    fire->waveSpeed = 1.45f;
                    fire->effectScale = 1.0f;
                    fire->effectSoftness = 0.0f;
                    fire->effectIntensity = 1.10f;
                    fire->billboardScale = 1.0f;
                    fire->effectScaleX = farWidth * 0.5f;
                    fire->effectScaleY = 4.8f;
                    fire->effectScaleZ = fanLength * 0.5f;
                    fire->uvOffsetX = 0.0f;
                    fire->uvOffsetY = 0.0f;
                }
            }
        }
        umbraFlames_.push_back(std::move(portal));
    }
}

void EnemyFalseKingSlime::UpdateUmbraFlameNetwork(float deltaTime, bool active) {
    if (!umbraFlameNetwork_) return;
    const EnemyAttackDefinition& attack = GetAttackDefinition(kUmbraFlameAttackId);
    const float nearDistance = 1.5f;
    const float farDistance = (std::max)(attack.maxRange * 0.90f, 20.0f);
    const float fanLength = farDistance - nearDistance;
    const float farWidth = farDistance * 0.94f * attack.radius / 1.75f;
    const Vector3 forward = NormalizePlanar(lockedDirection_);
    const Vector3 center = FindGroundPoint(attackStartPosition_ +
        forward * (nearDistance + fanLength * 0.5f));
    const float progress = active ? 1.0f : SmoothStep01(std::clamp(
        (1.0f - attackTimer_ / (std::max)(attackStateDuration_, 0.01f) - 0.04f) / 0.82f,
        0.0f, 1.0f));
    const float fade = active ? SmoothStep01(std::clamp(attackTimer_ / 0.70f, 0.0f, 1.0f)) : 1.0f;
    umbraFlameNetwork_->Update(deltaTime);
    umbraFlameNetwork_->SetIsVisible(progress > 0.01f && fade > 0.01f);
    umbraFlameNetwork_->SetRevealProgress(progress);
    umbraFlameNetwork_->SetTranslate(center + Vector3{ 0.0f, 0.15f, 0.0f });
    umbraFlameNetwork_->SetRotation({ 0.0f, std::atan2(forward.x, forward.z), 0.0f });
    // PlaneSize は 2.4 m。術式の絵柄を実際の扇範囲へ合わせます。
    umbraFlameNetwork_->SetScale({ farWidth / 2.4f, 1.0f, fanLength / 2.4f });
    umbraFlameNetwork_->SetColor({ 1.0f, 1.0f, 1.0f,
        fade * (0.18f + progress * 0.52f) });
    umbraFlameNetwork_->SetIntensity(1.10f + progress * 0.35f);
    umbraFlameNetwork_->UpdateLocalMatrix();
    umbraFlameNetwork_->UpdateWorldMatrix();
}

void EnemyFalseKingSlime::UpdateUmbraFlames(float deltaTime, bool active) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kUmbraFlameAttackId);
    const float nearDistance = 1.5f;
    const float farDistance = (std::max)(attack.maxRange * 0.90f, 20.0f);
    const float fanLength = farDistance - nearDistance;
    const float nearWidth = 1.6f * attack.radius / 1.75f;
    const float farWidth = farDistance * 0.94f * attack.radius / 1.75f;
    const Vector3 forward = NormalizePlanar(lockedDirection_);
    const float elapsed = active ? attackStateDuration_ - attackTimer_ : 0.0f;
    const float waveFront = SmoothStep01(std::clamp(elapsed / 1.55f, 0.0f, 1.0f));
    // 魔法陣だけで予告するため、従来の扇形ガイドは表示しません。
    HideAttackTelegraph();
    UpdateUmbraFlameNetwork(deltaTime, active);
    for (UmbraFlamePortal& portal : umbraFlames_) {
        if (active) portal.activeAge += deltaTime;
        else portal.age += deltaTime;
        const float age = active ? portal.activeAge : portal.age;
        const float assembly = active ? 1.0f : SmoothStep01(std::clamp(age / 0.90f, 0.0f, 1.0f));
        const float fade = active
            ? SmoothStep01(std::clamp(attackTimer_ / 0.70f, 0.0f, 1.0f))
            : 1.0f;
        if (portal.sigil) {
            portal.sigil->Update(deltaTime);
            portal.sigil->SetIsVisible((active || age >= 0.0f) && fade > 0.01f);
            portal.sigil->SetRevealProgress(active ? 1.0f : assembly);
            portal.sigil->SetTranslate(portal.groundPosition + Vector3{ 0.0f, 0.16f, 0.0f });
            portal.sigil->SetRotation({ 0.0f, portal.age * 0.25f, 0.0f });
            const float circleScale = attack.radius * 1.03f * (0.68f + assembly * 0.32f);
            portal.sigil->SetScale({ circleScale, 1.0f, circleScale });
            portal.sigil->SetColor({ 1.0f, 1.0f, 1.0f, fade * (0.42f + assembly * 0.48f) });
            portal.sigil->SetIntensity(1.25f + assembly * 0.55f);
            portal.sigil->UpdateLocalMatrix();
            portal.sigil->UpdateWorldMatrix();
        }
        if (age < 0.0f) continue;

        if (!active) {
            portal.particleTimer -= deltaTime;
            if (portal.particleTimer <= 0.0f && assembly > 0.18f) {
                // 赤と青の光を上空の別方向から魔法陣へ収束させます。
                const float orbit = age * 1.7f + portal.delay * 7.0f;
                const float radius = attack.radius * 1.22f;
                const Vector3 radial{ std::cos(orbit) * radius, 0.0f,
                                      std::sin(orbit) * radius };
                const Vector3 redOrigin = portal.groundPosition + radial +
                    Vector3{ 0.0f, 8.0f, 0.0f };
                const Vector3 blueOrigin = portal.groundPosition - radial +
                    Vector3{ 0.0f, 7.4f, 0.0f };
                const Vector3 destination = portal.groundPosition +
                    Vector3{ 0.0f, 0.14f, 0.0f };
                EmitDirectedPreset(kUmbraFlameRedEnergyPreset, redOrigin,
                    destination - redOrigin, 1.0f);
                EmitDirectedPreset(kUmbraFlameChargePreset,
                    blueOrigin, destination - blueOrigin, 1.0f);
                portal.particleTimer += 0.12f;
            }
            continue;
        }

        // 炎の本体は術者側から奥へ進む一つの扇形体積です。
        const float rise = SmoothStep01(std::clamp(age / 0.34f, 0.0f, 1.0f));
        const float height = 9.6f * rise;
        if (portal.flame) {
            portal.flame->SetIsVisible(rise > 0.01f && fade > 0.01f);
            const Vector3 fanCenter = FindGroundPoint(attackStartPosition_ +
                forward * (nearDistance + fanLength * 0.5f));
            portal.flame->SetTranslate(fanCenter + Vector3{ 0.0f, 4.95f, 0.0f });
            portal.flame->SetRotation({ 0.0f, std::atan2(forward.x, forward.z), 0.0f });
            portal.flame->SetScale({ 1.0f, 1.0f, 1.0f });
            portal.flame->SetColor({ 0.72f, 0.38f, 1.0f, fade * 0.86f });
            if (MeshRenderer* renderer = portal.flame->GetMeshRenderer()) {
                if (auto* fire = renderer->GetWaterParamData()) {
                    fire->effectSoftness = waveFront;
                }
            }
            portal.flame->Update(deltaTime);
            portal.flame->UpdateWorldMatrix(true);
        }
    }
    // 火の粉も六つの魔法陣ではなく、扇を進む炎の前縁から湧かせます。
    if (active && waveFront > 0.03f && attackTimer_ > 0.70f) {
        effectTimer_ -= deltaTime;
        if (effectTimer_ <= 0.0f) {
            const Vector3 right{ forward.z, 0.0f, -forward.x };
            const float frontDistance = nearDistance + fanLength * waveFront;
            const float frontHalfWidth = (nearWidth + (farWidth - nearWidth) * waveFront) * 0.42f;
            for (int lane = -2; lane <= 2; ++lane) {
                const Vector3 ground = FindGroundPoint(attackStartPosition_ +
                    forward * frontDistance + right * (frontHalfWidth * lane * 0.5f));
                EmitDirectedPreset(kUmbraFlameFlamePreset,
                    ground + Vector3{ 0.0f, 0.18f, 0.0f },
                    { 0.0f, 1.0f, 0.0f }, 0.84f);
            }
            effectTimer_ += 0.12f;
        }
    }
    // 炎の前縁が届いた時だけ判定し、画面に見える進行と被弾を一致させます。
    if (!active || umbraFlameFanHit_ || !target_ || target_->isDead || !target_->GetIsVisible()) return;
    const Vector3 targetPosition = target_->GetWorldPosition();
    const Vector3 offset = targetPosition - attackStartPosition_;
    const Vector3 right{ forward.z, 0.0f, -forward.x };
    const float along = offset.x * forward.x + offset.z * forward.z;
    const float side = std::abs(offset.x * right.x + offset.z * right.z);
    const float collisionRadius = target_->GetCollisionRadius();
    const float fanRate = std::clamp((along - nearDistance) / fanLength, 0.0f, 1.0f);
    const float halfWidth = (nearWidth + (farWidth - nearWidth) * fanRate) * 0.5f;
    const Vector3 ground = FindGroundPoint(targetPosition);
    if (along < nearDistance - collisionRadius || along > farDistance + collisionRadius ||
        fanRate > waveFront + collisionRadius / fanLength ||
        side > halfWidth + collisionRadius || std::abs(targetPosition.y - ground.y) > 4.0f) return;
    const Vector3 knockback = NormalizePlanar(targetPosition - attackStartPosition_) * 5.5f;
    DamageTargetAt(ground, collisionRadius + 0.05f, 4.0f, attack.damage, DamageType::Explosion,
        { knockback.x, 6.0f, knockback.z });
    umbraFlameFanHit_ = true;
}
