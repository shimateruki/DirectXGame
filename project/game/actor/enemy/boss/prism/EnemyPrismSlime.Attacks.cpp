#include "game/actor/enemy/boss/prism/EnemyPrismSlime.Internal.h"

#include "game/actor/enemy/boss/core/BossChantVisual.h"

using namespace EnemyPrismSlimeImplementation;

// 元の更新順序と判定を維持して、担当処理をまとめています。
void EnemyPrismSlime::PreparePrismSpikePoints() {
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const Vector3 casterPosition = GetWorldPosition();
    Vector3 requestedCenter =
        casterPosition + lockedDirection_ * std::clamp(lockedTargetDistance_, 0.0f, (std::max)(attack.maxRange, 0.1f));
    if (target_) {
        requestedCenter = target_->GetWorldPosition();
        Vector3 fromCaster = requestedCenter - casterPosition;
        fromCaster.y = 0.0f;
        const float targetDistance = std::sqrt(fromCaster.x * fromCaster.x + fromCaster.z * fromCaster.z);
        if (targetDistance > attack.maxRange && targetDistance > 0.001f) {
            requestedCenter = casterPosition + fromCaster * (attack.maxRange / targetDistance);
        }
    }
    prismSpellOrigin_ = FindGroundPoint(casterPosition);
    prismSpellCenter_ = FindGroundPoint(requestedCenter);
    prismSpikeSector_ = CreateSpellEffect(kPrismSpikeSectorEffect);
    const float sine = std::clamp(attack.radius / (std::max)(attack.maxRange, 0.1f), 0.0f, 0.95f);
    const float sectorAngle = 2.0f * std::asin(sine);
    if (prismSpikeSector_) {
        prismSpikeSector_->editSlashAngle_ = sectorAngle * (180.0f / kPi);
        prismSpikeSector_->UpdateProceduralMesh();
    }
    // 三重の弧と左右の境界線で、予告と同じ扇形を魔法陣として描きます。
    for (std::size_t sigilIndex = 0; sigilIndex < prismSpikeSigil_.size(); ++sigilIndex) {
        auto& sigil = prismSpikeSigil_[sigilIndex];
        sigil = CreateSpellEffect(kPrismSpikeSectorEffect);
        if (!sigil)
            continue;
        sigil->SetBlendMode(BlendMode::kAdd);
        sigil->editInnerRadius_ = sigilIndex < 3 ? 0.975f : 0.06f;
        sigil->editSlashAngle_ = sigilIndex < 3 ? sectorAngle * (180.0f / kPi) : 1.25f;
        sigil->UpdateProceduralMesh();
    }

    // 距離に比例して列を増やし、扇形の外周まで隙間を均等に埋めます。
    const float halfAngle = std::asin(std::clamp(attack.radius / (std::max)(attack.maxRange, 0.1f), 0.0f, 0.95f));
    std::size_t index = 0;
    for (int row = 0; row < 8; ++row) {
        const int columns = 4 + row * 2;
        const float distance = attack.maxRange * (0.14f + static_cast<float>(row) * 0.118f);
        for (int column = 0; column < columns; ++column) {
            const float fraction =
                (static_cast<float>(column) + (row % 2 == 0 ? 0.5f : 0.22f)) / static_cast<float>(columns);
            const float angle = halfAngle * (fraction * 2.0f - 1.0f);
            const Vector3 direction = RotatePlanar(lockedDirection_, angle);
            prismSpikePositions_[index++] = FindGroundPoint(prismSpellOrigin_ + direction * distance);
        }
    }
    // 最後の主棘は標的付近に置き、扇形の中で詠唱の焦点を作ります。
    prismSpikePositions_.back() = prismSpellCenter_;
}

std::unique_ptr<Object3d> EnemyPrismSlime::CreatePrismSpikeObject(int index) const {
    // 棘は攻撃の視覚部品であり、接触判定は別の扇形攻撃で処理する。
    if (!common_) {
        return nullptr;
    }

    auto spike = std::make_unique<Object3d>();
    spike->Initialize(common_);
    spike->SetName("PrismSpike_" + std::to_string(index));
    spike->SetClassName("EnemyVisualPart");
    spike->SetModel("Effects/prism_crystal_spike");
    spike->SetColliderType(ColliderType::kNone);
    spike->SetCollisionAttribute(0);
    spike->SetCollisionMask(0);
    spike->SetMaterialType(kPrismCrystalMaterialType);

    constexpr std::array<Vector4, 3> kCrystalTints = {
        Vector4{0.34f, 0.20f, 0.74f, 1.0f},
        Vector4{0.20f, 0.43f, 0.78f, 1.0f},
        Vector4{0.66f, 0.22f, 0.70f, 1.0f},
    };
    spike->SetColor(kCrystalTints[static_cast<std::size_t>(index) % kCrystalTints.size()]);
    spike->SetMetallic(0.94f);
    spike->SetRoughness(0.11f);
    spike->SetEnableEnvMap(true);
    spike->SetEnvIntensity(1.12f);
    spike->SetEmissive(0.96f);
    spike->SetBlendMode(BlendMode::kNone);
    return spike;
}

void EnemyPrismSlime::SpawnNextPrismSpike() {
    // 予告済みの座標に一棘ずつ立て、最後の主棘だけ大きさと演出を強める。
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(prismSpikePositions_.size())) {
        return;
    }

    PrismSpikeVisual visual;
    visual.object = CreatePrismSpikeObject(actionIndex_);
    if (!visual.object) {
        return;
    }
    visual.index = actionIndex_;

    const bool isCenterSpike = actionIndex_ == static_cast<int>(prismSpikePositions_.size()) - 1;
    const float heightVariation = isCenterSpike ? 1.58f : 0.78f + static_cast<float>((actionIndex_ * 5) % 7) * 0.075f;
    const float widthVariation = isCenterSpike ? 1.38f : 0.92f + static_cast<float>((actionIndex_ * 7) % 5) * 0.06f;
    visual.groundPosition = prismSpikePositions_[actionIndex_];
    visual.fullScale = {widthVariation, 1.22f * heightVariation, widthVariation * 1.86f};
    visual.object->SetTranslate(visual.groundPosition + Vector3{0.0f, -0.24f, 0.0f});
    visual.object->SetRotation({
        0.0f,
        std::atan2(lockedDirection_.x, lockedDirection_.z) + static_cast<float>(actionIndex_ - 2) * 0.13f,
        0.0f,
    });
    visual.object->SetScale({visual.fullScale.x * 0.42f, 0.015f, visual.fullScale.z * 0.42f});
    visual.object->Update(0.0f);
    visual.object->UpdateWorldMatrix(true);

    const float groundEffectScale = isCenterSpike ? 1.34f : 0.74f;
    if ((isCenterSpike || actionIndex_ % 2 == 0) && MeshEffectManager::GetInstance()) {
        MeshEffectManager* effects = MeshEffectManager::GetInstance();
        effects->SpawnEffectAt(kPrismSpikeGroundEffect, visual.groundPosition + Vector3{0.0f, 0.045f, 0.0f},
                               {0.0f, static_cast<float>(actionIndex_) * 0.37f, 0.0f},
                               {groundEffectScale, 1.0f, groundEffectScale});
    }

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const char* activePreset = attack.activeVfx.empty() ? kPrismSpikeBurstPreset : attack.activeVfx.c_str();
    if (isCenterSpike || actionIndex_ % 3 == 0) {
        EmitDirectedPreset(activePreset, visual.groundPosition + Vector3{0.0f, 0.12f, 0.0f}, {0.0f, 1.0f, 0.0f},
                           isCenterSpike ? 1.04f : 0.68f);
    }
    Vector3 fromOrigin = visual.groundPosition - prismSpellOrigin_;
    fromOrigin.y = 0.0f;
    ApplyPrismSpellDamage(Math::Length(fromOrigin) + 1.15f);
    prismSpikeVisuals_.push_back(std::move(visual));
}

void EnemyPrismSlime::UpdatePrismSpikeVisuals(float deltaTime) {
    auto spike = prismSpikeVisuals_.begin();
    while (spike != prismSpikeVisuals_.end()) {
        spike->age += (std::max)(deltaTime, 0.0f);

        Vector3 scale = spike->fullScale;
        Vector3 position = spike->groundPosition;
        if (spike->age < kPrismSpikeRiseDuration) {
            const float progress = SmoothStep01(spike->age / kPrismSpikeRiseDuration);
            const float overshoot = std::sin(progress * kPi) * 0.12f;
            scale.x *= std::lerp(0.42f, 1.0f, progress);
            scale.y *= (std::max)(0.015f, progress + overshoot);
            scale.z *= std::lerp(0.42f, 1.0f, progress);
            position.y -= (1.0f - progress) * 0.24f;
        } else if (spike->age < kPrismSpikeRiseDuration + kPrismSpikeHoldDuration) {
            const float shimmer = std::sin((spike->age - kPrismSpikeRiseDuration) * 13.0f) * 0.012f;
            scale.x *= 1.0f + shimmer;
            scale.z *= 1.0f - shimmer;
        } else {
            const float fractureTime = spike->age - kPrismSpikeRiseDuration - kPrismSpikeHoldDuration;
            const float fracture = SmoothStep01(fractureTime / kPrismSpikeFractureDuration);
            const float tension = std::sin(fracture * kPi);
            scale.x *= 1.0f + tension * 0.075f;
            scale.y *= 1.0f + tension * 0.035f;
            scale.z *= 1.0f + tension * 0.075f;
            position.y += tension * 0.035f;
        }

        spike->object->SetTranslate(position);
        spike->object->SetScale(scale);
        spike->object->Update(deltaTime);
        // このObject3dはSceneの所有物ではないため、通常の一括GPU更新対象に含まれません。
        spike->object->UpdateWorldMatrix(true);

        if (spike->age >= kPrismSpikeLifetime) {
            SpawnPrismSpikeShatter(*spike);
            spike = prismSpikeVisuals_.erase(spike);
        } else {
            ++spike;
        }
    }
}

void EnemyPrismSlime::SpawnPrismSpikeShatter(const PrismSpikeVisual& spike) {
    const bool isMainSpike = spike.index == static_cast<int>(prismSpikePositions_.size()) - 1;
    const Vector3 burstPosition = spike.groundPosition + Vector3{
                                                             0.0f,
                                                             spike.fullScale.y * 1.18f,
                                                             0.0f,
                                                         };

    // 多数の棘を同時に消すため、破片は間引いて描画負荷を抑えます。
    const bool showShatter = isMainSpike || spike.index % 3 == 0;
    if (showShatter && DebrisEffectManager::GetInstance()) {
        DebrisEffectManager* debris = DebrisEffectManager::GetInstance();
        debris->SpawnOnGround(kPrismSpikeShatterDebrisPreset, burstPosition, spike.groundPosition.y + 0.02f);
        if (isMainSpike) {
            debris->SpawnOnGround(kPrismSpikeShatterDebrisPreset,
                                  burstPosition + Vector3{0.0f, spike.fullScale.y * 0.82f, 0.0f},
                                  spike.groundPosition.y + 0.02f);
        }
    }

    if (showShatter) {
        EmitDirectedPreset(kPrismSpikeBurstPreset, burstPosition, {0.0f, 1.0f, 0.0f}, isMainSpike ? 1.34f : 0.92f);
    }

    if (showShatter && MeshEffectManager::GetInstance()) {
        MeshEffectManager* effects = MeshEffectManager::GetInstance();
        const float groundFlashScale = isMainSpike ? 1.28f : 0.78f;
        effects->SpawnEffectAt(kPrismSpikeGroundEffect, spike.groundPosition + Vector3{0.0f, 0.05f, 0.0f},
                               {0.0f, spike.age * 0.71f, 0.0f}, {groundFlashScale, 1.0f, groundFlashScale});
    }
}

void EnemyPrismSlime::PrepareCrystalLances() {
    // 前の斉射の部品と命中フラグを消してから、新しい槍と円形魔法陣を用意する。
    crystalLanceVisuals_.clear();
    crystalLanceVisuals_.reserve(kCrystalLanceCount);
    crystalVolleyDamageApplied_ = false;
    crystalCircle_ = CreateCrystalCircleEffect(false);
    crystalCircleGlow_ = CreateCrystalCircleEffect(true);

    for (int index = 0; index < kCrystalLanceCount; ++index) {
        CrystalLanceVisual lance;
        lance.object = CreatePrismSpikeObject(index + 10);
        lance.slotIndex = index;
        if (!lance.object) {
            continue;
        }

        lance.object->SetName("PrismCrystalLance_" + std::to_string(index));
        lance.object->SetScale({0.01f, 0.01f, 0.01f});
        lance.object->SetTranslate(GetWorldPosition());
        lance.object->Update(0.0f);
        lance.object->UpdateWorldMatrix(true);
        crystalLanceVisuals_.push_back(std::move(lance));
    }
}

std::unique_ptr<EffectObject3d> EnemyPrismSlime::CreateCrystalCircleEffect(bool glow) const {
    // 同じ JSON を通常層と加算発光層で使い、後段で別々の透明度を制御する。
    if (!common_) {
        return nullptr;
    }

    auto effect = std::make_unique<EffectObject3d>();
    effect->Initialize(common_);
    if (!effect->LoadFromJson(kPrismCrystalLanceCircleEffect)) {
        return nullptr;
    }
    effect->SetName(glow ? "PrismCrystalCircleGlow" : "PrismCrystalCircle");
    if (glow) {
        effect->SetBlendMode(BlendMode::kAdd);
        effect->SetUseTextureColor(false);
    }
    effect->SetRevealProgress(0.0f);
    effect->SetColor({0.92f, 0.97f, 1.0f, 0.0f});
    return effect;
}

std::unique_ptr<EffectObject3d> EnemyPrismSlime::CreateSpellEffect(const char* path) const {
    // 演出データの再生状態だけ準備し、実際の表示時刻は攻撃状態が決める。
    if (!common_) {
        return nullptr;
    }
    auto effect = std::make_unique<EffectObject3d>();
    effect->Initialize(common_);
    if (!effect->LoadFromJson(path)) {
        return nullptr;
    }
    effect->Play(10.0f);
    effect->SetIsVisible(false);
    return effect;
}

void EnemyPrismSlime::UpdatePrismSpikeSectorVisual(float deltaTime) {
    // 詠唱中に扇形の弧と境界を順に組み立て、攻撃終了後は残像を隠す。
    if (currentAttack_ != AttackKind::PrismSpikes ||
        (attackState_ != AttackState::Windup && attackState_ != AttackState::Active)) {
        if (prismSpikeSector_)
            prismSpikeSector_->SetIsVisible(false);
        for (auto& sigil : prismSpikeSigil_)
            if (sigil)
                sigil->SetIsVisible(false);
        return;
    }
    const bool windup = attackState_ == AttackState::Windup;
    const float progress =
        windup ? 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f) : 1.0f;
    const float fade = windup ? 1.0f : 0.48f + 0.52f * SmoothStep01(std::clamp(attackTimer_ / 0.24f, 0.0f, 1.0f));
    const float yaw = std::atan2(lockedDirection_.x, lockedDirection_.z);
    const float extent = GetCurrentAttackDefinition().maxRange;
    if (prismSpikeSector_) {
        prismSpikeSector_->Update(deltaTime);
        prismSpikeSector_->SetIsVisible(progress > 0.01f && fade > 0.01f);
        prismSpikeSector_->SetTranslate(prismSpellOrigin_ + Vector3{0.0f, 0.045f, 0.0f});
        prismSpikeSector_->SetRotation({0.0f, yaw, 0.0f});
        prismSpikeSector_->SetScale({extent, 1.0f, extent});
        prismSpikeSector_->SetColor({0.32f, 0.68f, 1.0f, fade * (0.12f + progress * 0.18f)});
        prismSpikeSector_->UpdateLocalMatrix();
        prismSpikeSector_->UpdateWorldMatrix();
    }
    const float halfAngle =
        std::asin(std::clamp(GetCurrentAttackDefinition().radius / (std::max)(extent, 0.1f), 0.0f, 0.95f));
    for (std::size_t index = 0; index < prismSpikeSigil_.size(); ++index) {
        auto& sigil = prismSpikeSigil_[index];
        if (!sigil)
            continue;
        sigil->Update(deltaTime);
        const float assembly =
            SmoothStep01(std::clamp((progress - static_cast<float>(index) * 0.08f) / 0.60f, 0.0f, 1.0f));
        const float distance = extent * (index < 3 ? (index == 0 ? 0.38f : (index == 1 ? 0.68f : 1.0f)) : 1.0f);
        const float rotation = yaw + (index == 3 ? -halfAngle : (index == 4 ? halfAngle : 0.0f));
        sigil->SetIsVisible(assembly > 0.01f && fade > 0.01f);
        sigil->SetTranslate(prismSpellOrigin_ + Vector3{0.0f, 0.072f + static_cast<float>(index) * 0.002f, 0.0f});
        sigil->SetRotation({0.0f, rotation, 0.0f});
        sigil->SetScale({distance, 1.0f, distance});
        sigil->SetColor({0.47f, 0.86f, 1.0f, assembly * fade * (index == 2 ? 0.80f : 0.55f)});
        sigil->SetIntensity(1.65f);
        sigil->UpdateLocalMatrix();
        sigil->UpdateWorldMatrix();
    }
}

void EnemyPrismSlime::UpdateWindSpellVisual(float deltaTime) {
    if (currentAttack_ != AttackKind::WindWave ||
        (attackState_ != AttackState::Windup && attackState_ != AttackState::Active)) {
        for (WindVortexVisual& vortex : windVortices_) {
            if (vortex.circle)
                vortex.circle->SetIsVisible(false);
            if (vortex.circleGlow)
                vortex.circleGlow->SetIsVisible(false);
            for (auto& effect : vortex.outer)
                if (effect)
                    effect->SetIsVisible(false);
            for (auto& effect : vortex.inner)
                if (effect)
                    effect->SetIsVisible(false);
        }
        return;
    }

    const bool assembling = attackState_ == AttackState::Windup;
    const float progress =
        assembling ? 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f) : 1.0f;
    const float fade = assembling ? 1.0f : 0.55f + 0.45f * SmoothStep01(std::clamp(attackTimer_ / 0.26f, 0.0f, 1.0f));

    for (std::size_t vortexIndex = 0; vortexIndex < windVortices_.size(); ++vortexIndex) {
        WindVortexVisual& vortex = windVortices_[vortexIndex];
        const bool primary = vortexIndex == 0;
        const float delayedProgress =
            std::clamp((progress - static_cast<float>(vortexIndex) * 0.11f) / 0.78f, 0.0f, 1.0f);
        const float reveal = SmoothStep01(delayedProgress);
        const float circleRadius = GetCurrentAttackDefinition().radius * (primary ? 1.0f : 0.72f);
        // 陣は召喚地点に固定し、移動する竜巻とは別の存在として見せます。
        const Vector3 circlePosition = vortex.origin;
        auto updateCircle = [&](std::unique_ptr<EffectObject3d>& circle, bool glow) {
            if (!circle)
                return;
            circle->Update(deltaTime);
            circle->SetRevealProgress(reveal);
            circle->SetIsVisible(reveal > 0.01f);
            circle->SetTranslate(circlePosition + Vector3{0.0f, glow ? 0.077f : 0.065f, 0.0f});
            circle->SetRotation({0.0f, idleTimer_ * (glow ? -0.70f : 0.46f), 0.0f});
            const float radius = circleRadius * (0.72f + reveal * 0.28f);
            circle->SetScale({radius, 1.0f, radius});
            circle->SetColor(glow ? Vector4{0.45f, 1.0f, 0.78f, reveal * fade * 0.24f}
                                  : Vector4{0.88f, 1.0f, 0.94f, reveal * fade * 0.78f});
            circle->UpdateLocalMatrix();
            circle->UpdateWorldMatrix();
        };
        updateCircle(vortex.circle, false);
        updateCircle(vortex.circleGlow, true);

        if (assembling)
            continue;
        const float localAge = windTornadoAge_ - static_cast<float>(vortexIndex) * 0.15f;
        const float appear = SmoothStep01(std::clamp(localAge / 0.26f, 0.0f, 1.0f));
        for (std::size_t layer = 0; layer < vortex.outer.size(); ++layer) {
            auto& effect = vortex.outer[layer];
            if (!effect)
                continue;
            effect->Update(deltaTime);
            effect->SetIsVisible(appear > 0.01f);
            const float height = (primary ? 0.42f : 0.32f) + static_cast<float>(layer) * (primary ? 1.42f : 1.08f);
            const float radius =
                (primary ? 1.18f + static_cast<float>(layer) * 0.74f : 0.86f + static_cast<float>(layer) * 0.49f) *
                appear;
            effect->SetTranslate(vortex.position + Vector3{0.0f, height, 0.0f});
            effect->SetRotation(
                {0.0f, localAge * (primary ? 14.0f : -16.0f) + static_cast<float>(layer) * 1.65f, 0.0f});
            effect->SetScale({radius, primary ? 2.2f : 1.8f, radius});
            effect->SetColor({0.74f, 1.0f, 0.92f, fade * appear * 0.72f});
            effect->UpdateLocalMatrix();
            effect->UpdateWorldMatrix();
        }
        for (auto& effect : vortex.inner) {
            if (!effect)
                continue;
            effect->Update(deltaTime);
            effect->SetIsVisible(appear > 0.01f);
            effect->SetTranslate(vortex.position + Vector3{0.0f, primary ? 2.90f : 2.15f, 0.0f});
            effect->SetRotation({0.0f, -localAge * 20.0f + static_cast<float>(vortexIndex) * 1.4f, 0.0f});
            const float radius = (primary ? 2.62f : 1.78f) * appear;
            effect->SetScale({radius, primary ? 3.4f : 2.6f, radius});
            effect->SetColor({0.38f, 0.86f, 1.0f, fade * appear * 0.60f});
            effect->UpdateLocalMatrix();
            effect->UpdateWorldMatrix();
        }
    }
}

Vector3 EnemyPrismSlime::GetChantSigilCenter() const {
    // 詠唱の印を本体の前方に置き、攻撃先の予告とは区別する。
    return GetWorldPosition() + lockedDirection_ * std::clamp(baseScale_.x * 1.75f, 1.7f, 2.8f) +
           Vector3{0.0f, baseScale_.y * 0.55f + 1.05f, 0.0f};
}

void EnemyPrismSlime::UpdateChantSigilVisual(float deltaTime) {
    // 攻撃状態から詠唱共通部品の進行率を作り、見た目を本体に追従させる。
    if (!chantVisual_) {
        return;
    }
    BossChantFrame frame;
    frame.casterPosition = GetWorldPosition();
    frame.groundPosition = FindGroundPoint(frame.casterPosition);
    const AABB bodyBounds = GetModelWorldAABB();
    frame.casterRadius = (std::max)({(bodyBounds.max.x - bodyBounds.min.x) * 0.5f,
                                     (bodyBounds.max.z - bodyBounds.min.z) * 0.5f, baseScale_.x * 0.84f});
    frame.casterHeight = (std::max)(bodyBounds.max.y - bodyBounds.min.y, baseScale_.y * 1.20f);
    frame.sigilCenter = GetChantSigilCenter();
    frame.forward = lockedDirection_;
    frame.casting = attackState_ == AttackState::Windup;
    frame.releasing = attackState_ == AttackState::Active;
    frame.windupProgress =
        frame.casting ? 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f) : 1.0f;
    frame.activeElapsed = frame.releasing ? (std::max)(0.0f, attackStateDuration_ - attackTimer_) : 0.0f;
    frame.time = idleTimer_;
    frame.deltaTime = deltaTime;
    chantVisual_->Update(frame);
}

void EnemyPrismSlime::UpdateCrystalCircleVisual(float deltaTime) {
    if (!crystalCircle_) {
        return;
    }
    if (currentAttack_ != AttackKind::CrystalLanceVolley ||
        (attackState_ != AttackState::Windup && attackState_ != AttackState::Active)) {
        crystalCircle_->SetIsVisible(false);
        if (crystalCircleGlow_) {
            crystalCircleGlow_->SetIsVisible(false);
        }
        return;
    }

    const bool assembling = attackState_ == AttackState::Windup;
    const float windupProgress =
        assembling ? 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f) : 1.0f;
    // 予備動作で描き上げ、発射後の余光を短く残します。
    const float assembly = SmoothStep01(std::clamp((windupProgress - 0.04f) / 0.84f, 0.0f, 1.0f));
    const float activeElapsed = assembling ? 0.0f : (std::max)(0.0f, attackStateDuration_ - attackTimer_);
    const float fade = assembling ? 1.0f : 1.0f - SmoothStep01(std::clamp(activeElapsed / 0.32f, 0.0f, 1.0f));

    crystalCircle_->Update(deltaTime);
    crystalCircle_->SetRevealProgress(assembly);
    crystalCircle_->SetIsVisible(fade > 0.01f && assembly > 0.001f);
    const Vector3 ground = FindGroundPoint(GetWorldPosition());
    const float radiusScale = std::clamp(baseScale_.x * 1.45f, 1.8f, 3.1f);
    const float completionPulse = SmoothStep01(std::clamp((windupProgress - 0.82f) / 0.18f, 0.0f, 1.0f));
    crystalCircle_->SetTranslate(ground + Vector3{0.0f, 0.08f, 0.0f});
    crystalCircle_->SetRotation({0.0f, idleTimer_ * 0.11f, 0.0f});
    crystalCircle_->SetScale(
        {radiusScale * (0.91f + assembly * 0.09f), 1.0f, radiusScale * (0.91f + assembly * 0.09f)});
    crystalCircle_->SetColor({0.92f, 0.97f, 1.0f, (0.35f + assembly * 0.40f + completionPulse * 0.15f) * fade});
    crystalCircle_->UpdateLocalMatrix();
    crystalCircle_->UpdateWorldMatrix();
    if (crystalCircleGlow_) {
        crystalCircleGlow_->Update(deltaTime);
        crystalCircleGlow_->SetRevealProgress(assembly);
        crystalCircleGlow_->SetIsVisible(fade > 0.01f && assembly > 0.24f);
        crystalCircleGlow_->SetTranslate(ground + Vector3{0.0f, 0.092f, 0.0f});
        crystalCircleGlow_->SetRotation({0.0f, idleTimer_ * 0.11f, 0.0f});
        crystalCircleGlow_->SetScale(
            {radiusScale * (0.94f + assembly * 0.09f), 1.0f, radiusScale * (0.94f + assembly * 0.09f)});
        crystalCircleGlow_->SetColor({0.42f, 0.78f, 1.0f, (0.10f + assembly * 0.12f + completionPulse * 0.11f) * fade});
        crystalCircleGlow_->SetIntensity(1.7f);
        crystalCircleGlow_->UpdateLocalMatrix();
        crystalCircleGlow_->UpdateWorldMatrix();
    }
}

void EnemyPrismSlime::LaunchNextCrystalLance() {
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(crystalLanceVisuals_.size())) {
        return;
    }

    CrystalLanceVisual& lance = crystalLanceVisuals_[static_cast<std::size_t>(actionIndex_)];
    if (!lance.object || lance.launched) {
        return;
    }

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    Vector3 aimPosition = lance.position + lockedDirection_ * (std::max)(attack.maxRange, 1.0f);
    if (target_) {
        aimPosition = target_->GetWorldPosition() + Vector3{0.0f, 0.72f, 0.0f};
    }

    Vector3 launchDirection = aimPosition - lance.position;
    if (Math::Length(launchDirection) <= 0.001f) {
        launchDirection = lockedDirection_;
    } else {
        launchDirection = Math::Normalize(launchDirection);
    }

    lance.velocity = launchDirection * (std::max)(12.0f, attack.maxSpeed);
    lance.launched = true;
    lance.age = 0.0f;
    lance.trailTimer = 0.0f;
    lance.object->SetRotation(MakeUpAxisAimRotation(launchDirection));
    lance.object->SetScale({0.42f, 0.72f, 0.42f});
    lance.object->SetEmissive(3.1f);
    // 陣を抜ける位置で閃光を出し、晶槍が魔法陣から射出されたようにつなぎます。
    EmitDirectedPreset(kPrismLanceReleasePreset, lance.position + lockedDirection_ * 0.40f, launchDirection, 1.0f);
    const char* trailPreset = attack.activeVfx.empty() ? kPrismLanceTrailPreset : attack.activeVfx.c_str();
    EmitDirectedPreset(trailPreset, lance.position, launchDirection * -1.0f, 1.25f);
}

void EnemyPrismSlime::UpdateCrystalLanceVisuals(float deltaTime) {
    if (crystalLanceVisuals_.empty()) {
        return;
    }

    const EnemyAttackDefinition& attack = GetAttackDefinition(kCrystalLanceVolleyAttackId);
    float revealProgress = 1.0f;
    if (currentAttack_ == AttackKind::CrystalLanceVolley && attackState_ == AttackState::Windup) {
        revealProgress = 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f);
    }
    const Vector3 side = {lockedDirection_.z, 0.0f, -lockedDirection_.x};

    auto lance = crystalLanceVisuals_.begin();
    while (lance != crystalLanceVisuals_.end()) {
        if (!lance->object) {
            lance = crystalLanceVisuals_.erase(lance);
            continue;
        }

        if (!lance->launched) {
            const float centeredSlot =
                static_cast<float>(lance->slotIndex) - static_cast<float>(kCrystalLanceCount - 1) * 0.5f;
            const float arch = 1.0f - std::abs(centeredSlot) * 0.17f;
            const float bob = std::sin(idleTimer_ * 4.6f + static_cast<float>(lance->slotIndex) * 0.9f) * 0.10f;
            const float buildStart = 0.10f + static_cast<float>(lance->slotIndex) * 0.095f;
            const float lanceBuild = SmoothStep01(std::clamp((revealProgress - buildStart) / 0.50f, 0.0f, 1.0f));
            // 陣の奥で結晶を作り、完成するにつれて射出口へ寄せます。
            lance->position = GetChantSigilCenter() - lockedDirection_ * (0.82f - lanceBuild * 0.42f) +
                              side * centeredSlot * 1.02f + Vector3{0.0f, arch * 0.22f + bob, 0.0f};

            Vector3 aimPosition = lance->position + lockedDirection_ * (std::max)(attack.maxRange, 1.0f);
            if (target_) {
                aimPosition = target_->GetWorldPosition() + Vector3{0.0f, 0.72f, 0.0f};
            }
            Vector3 aimDirection = aimPosition - lance->position;
            if (Math::Length(aimDirection) <= 0.001f) {
                aimDirection = lockedDirection_;
            }

            const float pulse = std::sin(idleTimer_ * 7.2f + static_cast<float>(lance->slotIndex)) * 0.018f;
            const float charge = lanceBuild * lanceBuild;
            lance->object->SetTranslate(lance->position);
            lance->object->SetRotation(MakeUpAxisAimRotation(aimDirection));
            lance->object->SetScale({
                (0.38f + pulse + charge * 0.035f) * lanceBuild,
                (0.67f - pulse + charge * 0.08f) * lanceBuild,
                (0.38f + pulse + charge * 0.035f) * lanceBuild,
            });
            lance->object->SetEmissive(0.65f + charge * 2.05f);
            if (currentAttack_ == AttackKind::CrystalLanceVolley && attackState_ == AttackState::Windup &&
                lanceBuild > 0.16f) {
                lance->trailTimer -= deltaTime;
                if (lance->trailTimer <= 0.0f) {
                    const float orbit = idleTimer_ * 5.2f + static_cast<float>(lance->slotIndex) * 2.4f;
                    const Vector3 motePosition =
                        lance->position +
                        Vector3{std::cos(orbit) * 0.48f, std::sin(orbit * 1.3f) * 0.32f, std::sin(orbit) * 0.48f};
                    EmitPreset(kPrismSpellMotesPreset, motePosition);
                    lance->trailTimer += 0.17f - charge * 0.045f;
                }
            }
            lance->object->Update(deltaTime);
            lance->object->UpdateWorldMatrix(true);
            ++lance;
            continue;
        }

        lance->age += (std::max)(deltaTime, 0.0f);
        // 発射の閃光だけを強くし、飛行中は結晶の輪郭が読める明るさへ戻します。
        lance->object->SetEmissive(1.28f + 1.82f * std::exp(-lance->age * 13.0f));
        lance->trailTimer -= deltaTime;
        const Vector3 previousPosition = lance->position;
        lance->position += lance->velocity * deltaTime;

        if (lance->trailTimer <= 0.0f) {
            const char* trailPreset = attack.activeVfx.empty() ? kPrismLanceTrailPreset : attack.activeVfx.c_str();
            EmitDirectedPreset(trailPreset, lance->position, lance->velocity * -1.0f, 1.0f);
            lance->trailTimer += kCrystalLanceTrailInterval;
        }

        bool hitTarget = false;
        if (target_) {
            const Vector3 targetCenter = target_->GetWorldPosition() + Vector3{0.0f, 0.72f, 0.0f};
            const float hitRadius = (std::max)(0.90f, target_->GetCollisionRadius() + 0.34f);
            hitTarget =
                DistanceSquaredToSegment(targetCenter, previousPosition, lance->position) <= hitRadius * hitRadius;
        }

        if (hitTarget && !crystalVolleyDamageApplied_) {
            const Vector3 knockbackDirection = NormalizePlanar(lance->velocity);
            DamageEvent event;
            event.target = target_;
            event.attacker = this;
            event.damageAmount = attack.damage;
            event.damageType = DamageType::Physical;
            event.knockbackVelocity = {
                knockbackDirection.x * 5.5f,
                4.2f,
                knockbackDirection.z * 5.5f,
            };
            EventManager::GetInstance()->Dispatch(event);
            crystalVolleyDamageApplied_ = true;
        }

        const bool expired = lance->age >= (std::max)(0.3f, attack.lifetime);
        if (hitTarget || expired) {
            SpawnCrystalLanceShatter(*lance);
            lance = crystalLanceVisuals_.erase(lance);
            continue;
        }

        lance->object->SetTranslate(lance->position);
        lance->object->SetRotation(MakeUpAxisAimRotation(lance->velocity));
        lance->object->Update(deltaTime);
        lance->object->UpdateWorldMatrix(true);
        ++lance;
    }
}

void EnemyPrismSlime::SpawnCrystalLanceShatter(const CrystalLanceVisual& lance) {
    // 槍の寿命が尽きた位置で破片と発光を一度だけ発生させる。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kCrystalLanceVolleyAttackId);
    const Vector3 groundPosition = FindGroundPoint(lance.position);
    if (DebrisEffectManager* debris = DebrisEffectManager::GetInstance()) {
        debris->SpawnOnGround(kPrismSpikeShatterDebrisPreset, lance.position, groundPosition.y + 0.02f);
    }
    const char* impactPreset = attack.impactVfx.empty() ? kPrismSpikeBurstPreset : attack.impactVfx.c_str();
    EmitDirectedPreset(impactPreset, lance.position, lance.velocity * -1.0f, 1.08f);
}

void EnemyPrismSlime::ClearPrismAttackVisuals() {
    // 死亡・遭遇演出・プレビューの再生成では、未発射のイベントも取り消します。
    attackSequence_.Cancel();
    prismSpikeVisuals_.clear();
    crystalLanceVisuals_.clear();
    crystalCircle_.reset();
    crystalCircleGlow_.reset();
    prismSpikeSector_.reset();
    for (auto& sigil : prismSpikeSigil_)
        sigil.reset();
    for (WindVortexVisual& vortex : windVortices_) {
        vortex.circle.reset();
        vortex.circleGlow.reset();
        for (auto& effect : vortex.outer)
            effect.reset();
        for (auto& effect : vortex.inner)
            effect.reset();
    }
    chantVisual_.reset();
    summonPortalVisuals_.clear();
}

void EnemyPrismSlime::ApplyPrismSpellDamage(float reachedDistance) {
    // プレイヤーが現在までに伸びた扇形の内側に入ったときだけ一度命中させる。
    if (!target_ || damageApplied_) {
        return;
    }

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    Vector3 difference = target_->GetWorldPosition() - prismSpellOrigin_;
    difference.y = 0.0f;
    const float distance = std::sqrt(difference.x * difference.x + difference.z * difference.z);
    const float forward = Math::Dot(difference, lockedDirection_);
    const float halfAngle = std::asin(std::clamp(attack.radius / (std::max)(attack.maxRange, 0.1f), 0.0f, 0.95f));
    if (distance > attack.maxRange || distance > reachedDistance ||
        (distance > 0.001f && forward / distance < std::cos(halfAngle))) {
        return;
    }

    const Vector3 direction = distance > 0.001f ? NormalizePlanar(difference) : lockedDirection_;
    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = attack.damage;
    event.damageType = DamageType::Physical;
    event.knockbackVelocity = {
        direction.x * 5.5f,
        10.5f,
        direction.z * 5.5f,
    };
    EventManager::GetInstance()->Dispatch(event);
    damageApplied_ = true;
}

void EnemyPrismSlime::FireNextFanProjectile() {
    // 中央を基準に角度をずらし、同じ攻撃定義から各弾の威力と射程を取る。
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const float centerIndex = static_cast<float>(kFireFanProjectileCount - 1) * 0.5f;
    const float fanAngle = (static_cast<float>(actionIndex_) - centerIndex) * 0.13f;
    Vector3 direction = RotatePlanar(lockedDirection_, fanAngle);

    Vector3 spawnPosition = GetWorldPosition() + direction * (baseScale_.x * 0.48f);
    spawnPosition.y += baseScale_.y * 0.48f + std::abs(static_cast<float>(actionIndex_) - centerIndex) * 0.10f;
    const float speed = std::clamp(lockedTargetDistance_ * 1.55f + 4.0f, (std::max)(11.0f, attack.minSpeed),
                                   (std::max)(22.0f, attack.maxSpeed));
    const float lifetime = attack.lifetime > 0.0f ? attack.lifetime : 2.8f;

    BulletManager::GetInstance()->Fire(spawnPosition, direction * speed + Vector3{0.0f, 0.45f, 0.0f}, CollisionCategories::Get("enemy_attack"),
                                       CollisionCategories::Get("player") | CollisionCategories::Get("ground"), "Primitives/sphere", (std::max)(0.42f, attack.radius),
                                       lifetime, MakePrismFireVisual(), attack.damage, MakeBurningStatus(attack),
                                       DamageType::Fire);
    EmitDirectedPreset(kFireCastPreset, spawnPosition, direction, 1.15f);
}

void EnemyPrismSlime::StrikeNextThunderPoint() {
    // 雷の見た目は地面から上へ伸ばし、判定は水平半径と柱の高さで限定する。
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(thunderStrikePositions_.size())) {
        return;
    }
    const Vector3 groundPosition = thunderStrikePositions_[actionIndex_];
    const float yaw = std::atan2(lockedDirection_.x, lockedDirection_.z);
    constexpr float strikeHeight = 10.5f;
    Vector3 strikeCenter = groundPosition;
    strikeCenter.y += strikeHeight * 0.5f;

    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        effects->SpawnEffectAt(kThunderBoltEffect, strikeCenter, {kHalfPi, yaw, 0.0f}, {1.15f, 1.18f, 1.15f});
        effects->SpawnEffectAt(kThunderBoltEffect, strikeCenter, {kHalfPi, yaw + kHalfPi, 0.0f}, {0.76f, 1.18f, 0.90f});
        effects->SpawnEffectAt(kThunderCoreEffect, strikeCenter, {0.0f, 0.0f, 0.0f}, {1.18f, 1.18f, 1.18f});
        effects->SpawnEffectAt(kThunderImpactEffect, groundPosition + Vector3{0.0f, 0.055f, 0.0f}, {0.0f, yaw, 0.0f},
                               {1.35f, 1.0f, 1.35f});
        effects->SpawnEffectAt(kThunderScorchEffect, groundPosition + Vector3{0.0f, 0.04f, 0.0f}, {0.0f, yaw, 0.0f});
    }
    EmitPreset(kThunderImpactPreset, groundPosition + Vector3{0.0f, 0.22f, 0.0f});

    if (!target_ || damageApplied_) {
        return;
    }
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    Vector3 difference = target_->GetWorldPosition() - groundPosition;
    const float verticalDistance = difference.y;
    difference.y = 0.0f;
    if (Math::Length(difference) > attack.radius || verticalDistance < -0.6f || verticalDistance > strikeHeight) {
        return;
    }
    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = attack.damage;
    event.damageType = DamageType::Electric;
    event.knockbackVelocity = {lockedDirection_.x * 8.0f, 7.5f, lockedDirection_.z * 8.0f};
    EventManager::GetInstance()->Dispatch(event);
    damageApplied_ = true;
}

void EnemyPrismSlime::ApplyWindTornado() {
    // 有効化済みの渦だけを調べ、複数重なる場合は中心に近い渦を採用する。
    if (!target_ || damageApplied_) {
        return;
    }
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    Vector3 difference{};
    float distance = 0.0f;
    float hitRadius = 0.0f;
    bool hit = false;
    for (std::size_t index = 0; index < windVortices_.size(); ++index) {
        if (windTornadoAge_ < 0.16f + static_cast<float>(index) * 0.15f)
            continue;
        const WindVortexVisual& vortex = windVortices_[index];
        Vector3 candidate = target_->GetWorldPosition() - vortex.position;
        const float verticalDistance = candidate.y;
        candidate.y = 0.0f;
        const float candidateDistance = Math::Length(candidate);
        const float candidateRadius = attack.radius * (index == 0 ? 1.0f : 0.72f);
        if (candidateDistance <= candidateRadius && verticalDistance >= -0.6f &&
            verticalDistance <= (index == 0 ? 6.2f : 5.0f) &&
            (!hit || candidateDistance / candidateRadius < distance / hitRadius)) {
            difference = candidate;
            distance = candidateDistance;
            hitRadius = candidateRadius;
            hit = true;
        }
    }
    if (!hit) {
        return;
    }

    const Vector3 pushDirection = distance > 0.001f ? NormalizePlanar(difference) : lockedDirection_;
    const float falloff = 1.0f - std::clamp(distance / (std::max)(hitRadius, 0.1f), 0.0f, 1.0f);
    if (Character* character = dynamic_cast<Character*>(target_)) {
        character->ApplyExternalImpulse(
            {
                pushDirection.x * (13.0f + falloff * 6.0f),
                4.8f + falloff * 2.0f,
                pushDirection.z * (13.0f + falloff * 6.0f),
            },
            0.25f);
    }

    EmitDirectedPreset(kWindImpactPreset, target_->GetWorldPosition() + Vector3{0.0f, 0.72f, 0.0f}, pushDirection,
                       1.25f);
    DamageEvent event;
    event.target = target_;
    event.attacker = this;
    event.damageAmount = attack.damage;
    event.damageType = DamageType::Physical;
    event.knockbackVelocity = {pushDirection.x * 15.0f, 7.0f, pushDirection.z * 15.0f};
    EventManager::GetInstance()->Dispatch(event);
    damageApplied_ = true;
}

void EnemyPrismSlime::PrepareThunderPoints() {
    // 開始時の向きに沿って落雷地点を固定し、各地点へ先に予告を出す。
    const EnemyAttackDefinition& attack = GetAttackDefinition(kThunderChainAttackId);
    const Vector3 origin = GetWorldPosition();
    const float spacing = (std::max)(2.0f, attack.maxRange / 6.4f);
    for (std::size_t index = 0; index < thunderStrikePositions_.size(); ++index) {
        const float distance = 3.2f + spacing * static_cast<float>(index);
        thunderStrikePositions_[index] = FindGroundPoint(origin + lockedDirection_ * distance);
        if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
            effects->SpawnEffectAt(kThunderWarningEffect, thunderStrikePositions_[index] + Vector3{0.0f, 0.045f, 0.0f},
                                   {0.0f, std::atan2(lockedDirection_.x, lockedDirection_.z), 0.0f},
                                   {attack.radius, 1.0f, attack.radius});
        }
    }
}

void EnemyPrismSlime::PrepareSummonPortals() {
    summonPortalVisuals_.clear();
    summonPortalVisuals_.reserve(kSummonPortalCount);

    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    const Vector3 center = GetWorldPosition();
    const float placementRadius = (std::max)(3.6f, attack.radius);
    std::uint32_t randomState = 0x9e3779b9u ^ ((summonRandomSerial_ + 1u) * 0x85ebca6bu);
    const float startAngle = NextSummonRandom01(randomState) * kPi * 2.0f;

    for (int index = 0; index < kSummonPortalCount; ++index) {
        const float spreadAngle = (kPi * 2.0f * static_cast<float>(index)) / static_cast<float>(kSummonPortalCount);
        const float angleJitter = (NextSummonRandom01(randomState) - 0.5f) * 0.58f;
        const float distanceRate = 0.62f + NextSummonRandom01(randomState) * 0.32f;
        const float angle = startAngle + spreadAngle + angleJitter;

        Vector3 samplePosition = center + Vector3{
                                              std::sin(angle) * placementRadius * distanceRate,
                                              0.0f,
                                              std::cos(angle) * placementRadius * distanceRate,
                                          };

        SummonPortalVisual portal;
        portal.groundPosition = FindGroundPoint(samplePosition);
        portal.effect = CreateSummonPortalEffect(false);
        portal.glow = CreateSummonPortalEffect(true);
        if (portal.effect) {
            portal.effect->SetRevealProgress(0.0f);
        }
        if (portal.glow) {
            portal.glow->SetRevealProgress(0.0f);
        }

        const int kindIndex = (std::min)(3, static_cast<int>(NextSummonRandom01(randomState) * 4.0f));
        portal.slimeKind = static_cast<SummonSlimeKind>(kindIndex);
        switch (portal.slimeKind) {
        case SummonSlimeKind::Fire:
            portal.color = {1.0f, 0.20f, 0.035f, 0.90f};
            break;
        case SummonSlimeKind::Bomber:
            // 暗い敵色は発光層で読めなくなるため、紫のハイライトで種類を示します。
            portal.color = {0.62f, 0.33f, 0.92f, 0.90f};
            break;
        case SummonSlimeKind::Pink:
            portal.color = {1.0f, 0.24f, 0.68f, 0.90f};
            break;
        case SummonSlimeKind::Thunder:
            portal.color = {1.0f, 0.86f, 0.055f, 0.94f};
            break;
        }

        if (portal.effect) {
            portal.effect->SetTranslate(portal.groundPosition + Vector3{0.0f, 0.065f, 0.0f});
            portal.effect->SetScale({0.48f, 1.0f, 0.48f});
            portal.effect->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
            portal.effect->UpdateLocalMatrix();
            portal.effect->UpdateWorldMatrix();
        }
        summonPortalVisuals_.push_back(std::move(portal));
    }

    ++summonRandomSerial_;
}

std::unique_ptr<EffectObject3d> EnemyPrismSlime::CreateSummonPortalEffect(bool glow) const {
    // 召喚陣は結晶円の材質を使い、名前だけを用途に合わせて区別する。
    auto effect = CreateCrystalCircleEffect(glow);
    if (effect) {
        effect->SetName(glow ? "PrismSlimeSummonGlow" : "PrismSlimeSummonCircle");
    }
    return effect;
}

void EnemyPrismSlime::UpdateSummonPortalVisuals(float deltaTime) {
    if (summonPortalVisuals_.empty()) {
        return;
    }

    float revealProgress = 1.0f;
    if (currentAttack_ == AttackKind::SlimeSummon && attackState_ == AttackState::Windup) {
        revealProgress = 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f);
    }
    const float easedReveal = SmoothStep01(revealProgress);

    for (std::size_t index = 0; index < summonPortalVisuals_.size(); ++index) {
        SummonPortalVisual& portal = summonPortalVisuals_[index];
        if (!portal.effect) {
            continue;
        }

        portal.age += deltaTime;
        portal.effect->Update(deltaTime);
        if (portal.glow) {
            portal.glow->Update(deltaTime);
        }
        const float assembly = SmoothStep01(std::clamp((revealProgress - 0.04f) / 0.84f, 0.0f, 1.0f));
        portal.effect->SetRevealProgress(assembly);
        if (portal.glow) {
            portal.glow->SetRevealProgress(assembly);
        }
        float scale = 1.36f * (0.42f + easedReveal * 0.58f);
        float alpha = 0.26f + assembly * 0.58f;
        float glowAlpha = portal.color.w * (0.06f + assembly * 0.18f);
        float intensity = 1.05f + assembly * 0.68f;
        if (portal.spawned) {
            // 敵が出現しても陣を残し、召喚動作が終わる回復フェーズで閉じます。
            if (attackState_ == AttackState::Recovery) {
                portal.spawnedAge += deltaTime;
            }
            const float fade = std::clamp(portal.spawnedAge / kSummonPortalFadeDuration, 0.0f, 1.0f);
            scale *= 1.0f + fade * 0.36f;
            alpha *= (1.0f - fade) * (1.0f - fade);
            glowAlpha *= (1.0f - fade) * (1.0f - fade);
            intensity += (1.0f - fade) * 0.55f;
            if (fade >= 1.0f) {
                portal.effect->SetIsVisible(false);
                if (portal.glow) {
                    portal.glow->SetIsVisible(false);
                }
                continue;
            }
        } else {
            const float pulse = std::sin(portal.age * (5.5f + easedReveal * 7.0f) + static_cast<float>(index) * 1.7f);
            scale *= 1.0f + pulse * (0.015f + easedReveal * 0.035f);
            alpha *= 0.91f + pulse * 0.09f;
            glowAlpha *= 0.85f + pulse * 0.15f;
            intensity += (0.5f + pulse * 0.5f) * assembly * 0.30f;
            if (currentAttack_ == AttackKind::SlimeSummon && attackState_ == AttackState::Windup &&
                easedReveal > 0.10f) {
                portal.moteTimer -= deltaTime;
                if (portal.moteTimer <= 0.0f) {
                    const float orbit = portal.age * 4.0f + static_cast<float>(index) * 2.1f;
                    const float orbitRadius = scale * 0.78f;
                    EmitPreset(kPrismSpellMotesPreset,
                               portal.groundPosition +
                                   Vector3{std::cos(orbit) * orbitRadius, 0.13f, std::sin(orbit) * orbitRadius});
                    portal.moteTimer += 0.23f - easedReveal * 0.12f;
                }
            }
        }

        portal.effect->SetIsVisible(assembly > 0.001f);
        portal.effect->SetTranslate(portal.groundPosition + Vector3{0.0f, 0.065f, 0.0f});
        portal.effect->SetRotation(
            {0.0f, portal.age * (0.46f + easedReveal * 0.92f) * (index == 1 ? -1.0f : 1.0f), 0.0f});
        portal.effect->SetScale({scale, 1.0f, scale});
        portal.effect->SetColor({0.94f, 0.98f, 1.0f, alpha});
        portal.effect->SetIntensity(intensity);
        portal.effect->UpdateLocalMatrix();
        portal.effect->UpdateWorldMatrix();
        if (portal.glow) {
            portal.glow->SetIsVisible(assembly > 0.24f);
            portal.glow->SetTranslate(portal.groundPosition + Vector3{0.0f, 0.077f, 0.0f});
            portal.glow->SetRotation(
                {0.0f, portal.age * (0.46f + easedReveal * 0.92f) * (index == 1 ? -1.0f : 1.0f), 0.0f});
            portal.glow->SetScale({scale * 1.035f, 1.0f, scale * 1.035f});
            portal.glow->SetColor({portal.color.x, portal.color.y, portal.color.z, glowAlpha});
            portal.glow->SetIntensity(1.65f + assembly * 0.50f);
            portal.glow->UpdateLocalMatrix();
            portal.glow->UpdateWorldMatrix();
        }
    }
}

void EnemyPrismSlime::SpawnNextSummonedSlime() {
    // 予兆時に選んだスライム種を実体化し、同じ門から二重召喚しない。
    if (actionIndex_ < 0 || actionIndex_ >= static_cast<int>(summonPortalVisuals_.size())) {
        return;
    }

    SummonPortalVisual& portal = summonPortalVisuals_[static_cast<std::size_t>(actionIndex_)];
    if (portal.spawned) {
        return;
    }

    const char* enemyType = "Slime";
    const char* nameSuffix = "Pink";
    const char* spawnPreset = kPrismPhasePulsePreset;
    switch (portal.slimeKind) {
    case SummonSlimeKind::Fire:
        enemyType = "FireSlime";
        nameSuffix = "Fire";
        spawnPreset = kFireCastPreset;
        break;
    case SummonSlimeKind::Bomber:
        enemyType = "Bomber";
        nameSuffix = "Bomb";
        spawnPreset = kPrismSpikeWarningPreset;
        break;
    case SummonSlimeKind::Pink:
        break;
    case SummonSlimeKind::Thunder:
        enemyType = "ThunderSlime";
        nameSuffix = "Thunder";
        spawnPreset = kThunderChargePreset;
        break;
    }

    std::unique_ptr<BaseEnemy> enemy = EnemyFactory::GetInstance()->CreateEnemy(enemyType, common_);
    if (!enemy) {
        return;
    }

    enemy->SetName(std::string("PrismSummon_") + nameSuffix + "_" + std::to_string(summonRandomSerial_) + "_" +
                   std::to_string(actionIndex_));
    enemy->SetTranslate(portal.groundPosition + Vector3{0.0f, 0.12f, 0.0f});
    enemy->SetVelocity({0.0f, 0.0f, 0.0f});
    enemy->SetGrounded(true);
    enemy->SetDetectionRange(24.0f);
    enemy->SetTarget(target_);

    portal.spawned = true;
    portal.spawnedAge = 0.0f;
    EmitPreset(spawnPreset, portal.groundPosition + Vector3{0.0f, 0.24f, 0.0f});
    for (int mote = 0; mote < 4; ++mote) {
        const float angle = (kPi * 2.0f * static_cast<float>(mote)) / 4.0f;
        EmitPreset(kPrismSpellMotesPreset,
                   portal.groundPosition + Vector3{std::cos(angle) * 0.72f, 0.18f, std::sin(angle) * 0.72f});
    }
    SpawnSummonedSlime(std::move(enemy));
}

void EnemyPrismSlime::SpawnSummonedSlime(std::unique_ptr<BaseEnemy> enemy) {
    // プレビューはコールバックへ、本番は現シーンへ所有権を移す。
    if (!enemy) {
        return;
    }
    if (spawnCallback_) {
        spawnCallback_(std::move(enemy));
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->GetCurrentScene()) {
        return;
    }
    enemy->SetTarget(sceneManager->GetCurrentScene()->GetPlayer());
    sceneManager->GetCurrentScene()->AddObject(std::move(enemy));
}
