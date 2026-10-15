#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.Internal.h"

#include "game/actor/combat/CombatGroundQuery.h"
#include "game/actor/enemy/boss/core/BossChantVisual.h"
#include <algorithm>

using namespace EnemyFalseKingSlimeImplementation;

// 偽王の戦闘フェーズと攻撃状態を進めます。登場・幕間・撃破中は通常AIへ流さず専用演出を優先します。

EnemyFalseKingSlime::EnemyFalseKingSlime() = default;
EnemyFalseKingSlime::~EnemyFalseKingSlime() {
    // 個別登録した結晶の衝突を所有物の破棄前に解除します。
    ResetCrownFinale();
}

void EnemyFalseKingSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 本体の反射はモデル材質を尊重し、攻撃用の衝突を敵属性で登録します。
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_FalseKingSlime");
    SetEnemyType("FalseKingSlime");
    ReloadAttackProfile();

    SetMaterialType(25);
    // 金属値はOBJの各マテリアルへ持たせ、身体と王冠を同じ反射にしません。
    SetMetallic(0.0f);
    SetRoughness(0.24f);
    SetEnableEnvMap(true);
    SetEnvIntensity(0.68f);
    SetEmissive(1.0f);
    SetCollisionAttribute(CollisionCategories::Get("enemy"));
    SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
    SetColliderType(ColliderType::kOBB);
    defaultColor_ = GetColor();
    SyncCollisionBounds();
}

void EnemyFalseKingSlime::Update(float deltaTime) {
    // 登場・幕間・最終撃破中は通常 AI より専用の進行を優先します。
    deltaTime = (std::max)(0.0f, deltaTime);
    // 編集へ戻った時には通常の配置復元へ進ませます。
    if (finalDefeatHeld_ && SceneManager::GetInstance()->IsPlaying()) return;
    if (finalDefeatHeld_) CancelFinalDefeat();
    if (UpdateEncounterState(deltaTime)) {
        // 登場演出中はCharacterの重力を止めつつ、変更したTransformだけ描画へ反映します。
        engine::Object3d::Update(deltaTime);
        return;
    }

    if (introPresentationActive_) {
        SetVelocity({});
        engine::Object3d::Update(deltaTime);
        return;
    }
    if (finalArenaStarted_ && battlePhase_ == 3 && param_ && param_->hp <= 0 && !isDead) {
        auto* scene=dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene());
        if (scene && scene->StartCrownDefeat(this)) return;
    }
    UpdateCrownLances(deltaTime);
    UpdateRoyalWaves(deltaTime);
    UpdateRoyalBeams(deltaTime);
    armorOpenTimer_ = std::max(0.0f, armorOpenTimer_ - deltaTime);
    if (crownInterlude_) {
        SetVelocity({});
        engine::Object3d::Update(deltaTime);
        return;
    }
    if (pendingDebugSeals_ && debugPreviewAttackId_.empty()) {
        pendingDebugSeals_ = false;
        if (param_)
            param_->hp = GetEncounterMaximumHp() * 0.67f;
        BeginPhaseShift(2);
    }
    if (pendingDebugInterlude_ && debugPreviewAttackId_.empty()) {
        pendingDebugInterlude_ = false;
        if (battlePhase_ != 2) {
            battlePhase_ = 2;
            InitializeCrownSeals();
        }
        for (int i = 0; i < 3; ++i)
            TryBreakCrownSeal(crownSeals_[i].position, i, true);
    }
    if (battlePhase_ == 2 &&
        (debugPreviewAttackId_.empty() || debugPreviewAttackId_ == kSealBarragePreviewId) && !isDead) {
        UpdateCrownSeals(deltaTime);
        SetVelocity({});
        ApplyBossAnimation(deltaTime);
        engine::Object3d::Update(deltaTime);
        UpdateSpellChant(deltaTime);
        return;
    }

    if (isDead || !GetIsVisible()) {
        HideAttackTelegraph();
        ClearTransientVisuals();
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (IsDormant() || IsThrowRecovering() || ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        BaseEnemy::Update(deltaTime);
        return;
    }

    EnsureBaseScale();
    idleTimer_ += deltaTime;
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);

    engine::Vector3 targetDirection = lockedDirection_;
    float targetDistance = 9999.0f;
    if (target_) {
        targetDirection = NormalizePlanar(target_->GetWorldPosition() - GetWorldPosition());
        targetDistance = PlanarDistance(target_->GetWorldPosition(), GetWorldPosition());
    }
    UpdateBossTargetEngagement(targetDistance);

    const engine::Vector3 previousVelocity = GetVelocity();
    engine::Vector3 velocity = previousVelocity;
    velocity.x = 0.0f;
    velocity.z = 0.0f;
    UpdateBehavior(deltaTime, targetDirection, targetDistance, velocity);
    if (attackState_ == AttackState::Idle && target_ && param_.has_value()) {
        velocity = CombatMath::SmoothPlanarVelocity(previousVelocity, velocity, deltaTime, 4.5f);
        const engine::Vector3 candidate = GetTranslate() + engine::Vector3{velocity.x * 0.48f, 0.0f, velocity.z * 0.48f};
        if (isGrounded_ && !CombatGroundQuery::CanWalkTo(GetTranslate(), candidate, this, {5.0f, 14.0f}, 1.4f)) {
            velocity.x = 0.0f;
            velocity.z = 0.0f;
        }
    }
    // 移動時は見た目の弾みだけを使い、周期的な実座標のジャンプを止めます。
    SetVelocity(velocity);
    ApplyBossAnimation(deltaTime);
    UpdateRoyalGlow(deltaTime);
    SyncCollisionBounds();
    BaseEnemy::Update(deltaTime);
    UpdateSpellChant(deltaTime);
}

void EnemyFalseKingSlime::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // ボス本体に続けて、結晶や攻撃用の所有モデルを描画します。
    BaseEnemy::Draw(pointLightResource, spotLightResource);
    for (const auto& seal : crownSeals_) {
        for (engine::Object3d* visual : {static_cast<engine::Object3d*>(seal.crystal.get()), seal.ring.get(), seal.link.get(),
                                 seal.gem.get()}) {
            if (visual && visual->GetIsVisible()) {
                visual->RefreshRenderCameraData();
                visual->Draw(pointLightResource, spotLightResource);
            }
        }
    }

    // 特殊マテリアルが使えないEditor描画でも攻撃形状を失わないよう、
    // 加算合成の発光コアを通常パスにも描画します。
    for (const CrownLance& lance : crownLances_) {
        if (lance.visual && lance.visual->GetIsVisible()) {
            lance.visual->RefreshRenderCameraData();
            lance.visual->Draw(pointLightResource, spotLightResource);
        }
    }
    for (const RoyalWave& wave : royalWaves_) {
        for (engine::Object3d* visual : {wave.visual.get(), wave.glow.get()}) {
            if (visual && visual->GetIsVisible()) {
                visual->RefreshRenderCameraData();
                visual->Draw(pointLightResource, spotLightResource);
            }
        }
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.visual && beam.visual->GetIsVisible()) {
            beam.visual->RefreshRenderCameraData();
            beam.visual->Draw(pointLightResource, spotLightResource);
        }
    }
    if (rushWingsVisual_ && rushWingsVisual_->GetIsVisible()) {
        rushWingsVisual_->Draw(pointLightResource, spotLightResource);
    }
    if (dominionSigilVisual_ && dominionSigilVisual_->GetIsVisible()) {
        dominionSigilVisual_->Draw(pointLightResource, spotLightResource);
    }
    if (umbraFlameNetwork_ && umbraFlameNetwork_->GetIsVisible()) {
        umbraFlameNetwork_->Draw(pointLightResource, spotLightResource);
    }
    if (spellChant_) {
        spellChant_->Draw(pointLightResource, spotLightResource);
    }
    for (const UmbraFlamePortal& portal : umbraFlames_) {
        if (portal.sigil && portal.sigil->GetIsVisible()) {
            portal.sigil->Draw(pointLightResource, spotLightResource);
        }
    }
    for (const AstralReaverOrb& orb : astralReaverOrbs_) {
        for (EffectObject3d* effect : {orb.halo.get(), orb.blade.get(), orb.arc.get(), orb.core.get()}) {
            if (effect && effect->GetIsVisible())
                effect->Draw(pointLightResource, spotLightResource);
        }
    }
    for (const auto& ring : astralReaverImpactRings_) {
        if (ring && ring->GetIsVisible())
            ring->Draw(pointLightResource, spotLightResource);
    }
    for (EffectObject3d* effect : {astralReaverTargetRing_.get(), astralReaverImpactCore_.get()}) {
        if (effect && effect->GetIsVisible())
            effect->Draw(pointLightResource, spotLightResource);
    }
    for (EffectObject3d* effect :
         {nightfallLances_.skySigil.get(), nightfallLances_.skyCounterSigil.get(),
          nightfallLances_.skyOuterRing.get(), nightfallLances_.skyInnerRing.get(),
          nightfallLances_.targetSigil.get(), nightfallLances_.targetRing.get(), nightfallLances_.innerRing.get(),
          nightfallLances_.spearGlow.get(), nightfallLances_.spearCore.get(), nightfallLances_.impactRing.get(),
          nightfallLances_.impactCore.get(), nightfallLances_.groundArc.get(), nightfallLances_.impactBeam.get(),
          nightfallLances_.impactBeamHalo.get(), nightfallLances_.blastRing.get(), nightfallLances_.blastShell.get()}) {
        if (effect && effect->GetIsVisible())
            effect->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& barb : nightfallLances_.barbs) {
        if (barb && barb->GetIsVisible())
            barb->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& lightning : nightfallLances_.impactLightning) {
        if (lightning && lightning->GetIsVisible())
            lightning->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& lightning : nightfallLances_.groundLightning) {
        if (lightning && lightning->GetIsVisible())
            lightning->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& satellite : sealNightfallSatellites_) {
        for (EffectObject3d* effect : {satellite.sigil.get(), satellite.core.get(), satellite.glow.get()}) {
            if (effect && effect->GetIsVisible())
                effect->Draw(pointLightResource, spotLightResource);
        }
    }
    for (EffectObject3d* effect : {crownShieldRing_.get(), crownShieldShell_.get(),
                                   crownCastFloor_.get(), crownArenaRing_.get(), crownBarrageFlash_.get(),
                                   crownBarrageSkySigil_.get(), crownBarrageSkyCounterSigil_.get(),
                                   crownBarrageSkyOuterRing_.get(), crownBarrageSkyInnerRing_.get()}) {
        if (effect && effect->GetIsVisible())
            effect->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& effect : crownBarrageLayers_) {
        if (effect && effect->GetIsVisible())
            effect->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& spear : sealBarrage_) {
        if (spear.model && spear.model->GetIsVisible())
            spear.model->Draw(pointLightResource, spotLightResource);
        for (EffectObject3d* effect : {spear.glow.get(), spear.portal.get(), spear.impactLightning.get(),
                                       spear.groundArc.get(), spear.impactArc.get(), spear.impactBeam.get()})
            if (effect && effect->GetIsVisible())
                effect->Draw(pointLightResource, spotLightResource);
    }
    for (const auto& seal : crownSeals_)
        for (const auto& pulse : seal.energyPulses)
            if (pulse && pulse->GetIsVisible())
                pulse->Draw(pointLightResource, spotLightResource);
}

void EnemyFalseKingSlime::DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource,
                                        ID3D12Resource* spotLightResource, int previewBufferIndex) {
    engine::Object3d::DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    for (const auto& seal : crownSeals_) {
        for (engine::Object3d* visual : {static_cast<engine::Object3d*>(seal.crystal.get()), seal.ring.get(), seal.link.get(),
                                 seal.gem.get()}) {
            if (visual && visual->GetIsVisible())
                visual->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }

    // 演出カメラPreviewはSceneの特殊マテリアルパスを共有しないため、
    // 攻撃の発光コアをPreview用WVPで直接描きます。
    for (const CrownLance& lance : crownLances_) {
        if (lance.visual && lance.visual->GetIsVisible()) {
            lance.visual->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (const RoyalWave& wave : royalWaves_) {
        for (engine::Object3d* visual : {wave.visual.get(), wave.glow.get()}) {
            if (visual && visual->GetIsVisible()) {
                visual->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
            }
        }
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.visual && beam.visual->GetIsVisible()) {
            beam.visual->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    if (rushWingsVisual_ && rushWingsVisual_->GetIsVisible()) {
        rushWingsVisual_->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
    if (dominionSigilVisual_ && dominionSigilVisual_->GetIsVisible()) {
        dominionSigilVisual_->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
    if (umbraFlameNetwork_ && umbraFlameNetwork_->GetIsVisible()) {
        umbraFlameNetwork_->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
    for (const UmbraFlamePortal& portal : umbraFlames_) {
        if (portal.sigil && portal.sigil->GetIsVisible()) {
            portal.sigil->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (const AstralReaverOrb& orb : astralReaverOrbs_) {
        for (EffectObject3d* effect : {orb.halo.get(), orb.blade.get(), orb.arc.get(), orb.core.get()}) {
            if (effect && effect->GetIsVisible()) {
                effect->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
            }
        }
    }
    for (const auto& ring : astralReaverImpactRings_) {
        if (ring && ring->GetIsVisible()) {
            ring->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (EffectObject3d* effect : {astralReaverTargetRing_.get(), astralReaverImpactCore_.get()}) {
        if (effect && effect->GetIsVisible()) {
            effect->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (EffectObject3d* effect :
         {nightfallLances_.skySigil.get(), nightfallLances_.skyCounterSigil.get(),
          nightfallLances_.skyOuterRing.get(), nightfallLances_.skyInnerRing.get(),
          nightfallLances_.targetSigil.get(), nightfallLances_.targetRing.get(), nightfallLances_.innerRing.get(),
          nightfallLances_.spearGlow.get(), nightfallLances_.spearCore.get(), nightfallLances_.impactRing.get(),
          nightfallLances_.impactCore.get(), nightfallLances_.groundArc.get(), nightfallLances_.impactBeam.get(),
          nightfallLances_.impactBeamHalo.get(), nightfallLances_.blastRing.get(), nightfallLances_.blastShell.get()}) {
        if (effect && effect->GetIsVisible()) {
            effect->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (const auto& barb : nightfallLances_.barbs) {
        if (barb && barb->GetIsVisible()) {
            barb->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (const auto& lightning : nightfallLances_.impactLightning) {
        if (lightning && lightning->GetIsVisible()) {
            lightning->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (const auto& lightning : nightfallLances_.groundLightning) {
        if (lightning && lightning->GetIsVisible()) {
            lightning->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (const auto& satellite : sealNightfallSatellites_) {
        for (EffectObject3d* effect : {satellite.sigil.get(), satellite.core.get(), satellite.glow.get()}) {
            if (effect && effect->GetIsVisible())
                effect->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        }
    }
    for (EffectObject3d* effect : {crownShieldRing_.get(), crownShieldShell_.get(),
                                   crownCastFloor_.get(), crownArenaRing_.get(), crownBarrageFlash_.get(),
                                   crownBarrageSkySigil_.get(), crownBarrageSkyCounterSigil_.get(),
                                   crownBarrageSkyOuterRing_.get(), crownBarrageSkyInnerRing_.get()}) {
        if (effect && effect->GetIsVisible())
            effect->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
    for (const auto& effect : crownBarrageLayers_) {
        if (effect && effect->GetIsVisible())
            effect->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
    for (const auto& spear : sealBarrage_) {
        if (spear.model && spear.model->GetIsVisible())
            spear.model->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
        for (EffectObject3d* effect : {spear.glow.get(), spear.portal.get(), spear.impactLightning.get(),
                                       spear.groundArc.get(), spear.impactArc.get(), spear.impactBeam.get()})
            if (effect && effect->GetIsVisible())
                effect->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
    for (const auto& seal : crownSeals_)
        for (const auto& pulse : seal.energyPulses)
            if (pulse && pulse->GetIsVisible())
                pulse->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
}

void EnemyFalseKingSlime::OnSwitchEvent(bool active) {
    // 外部イベントによる登場要求は、遭遇状態の待機から戦闘への入口です。
    if (!IsEncounterControlled()) {
        BaseEnemy::OnSwitchEvent(active);
        return;
    }

    activationRequested_ = active;
    if (!initializedForPlay_) {
        InitializeEncounterState();
    }
    if (active && encounterState_ == EncounterState::Dormant) {
        BeginAppearance();
    } else if (!active && encounterState_ != EncounterState::Dormant) {
        ApplyDormantState();
    }
}

std::unique_ptr<engine::Object3d> EnemyFalseKingSlime::Clone() const {
    // 描画と衝突資源を複製先で作り、配置設定だけを引き継ぎます。
    auto clone = std::make_unique<EnemyFalseKingSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    clone->baseScale_ = baseScale_;
    clone->hasBaseScale_ = true;
    return clone;
}

void EnemyFalseKingSlime::ApplyManagedScale(const engine::Vector3& scale) {
    // 通常の跳ねと攻撃姿勢が参照する基準スケールも同時に更新します。
    authoredScale_ = scale;
    baseScale_ = scale;
    hasBaseScale_ = true;
    authoredStateCaptured_ = true;
    SetScale(scale);
    SyncCollisionBounds();
}

bool EnemyFalseKingSlime::HasOwnedSpecialMaterialVisuals() const {
    // 特殊材質パスが必要な所有演出物の有無を先に調べます。
    if (!GetIsVisible()) {
        return false;
    }
    if (crownShieldBarrier_ && crownShieldBarrier_->GetIsVisible()) {
        return true;
    }
    for (const CrownLance& lance : crownLances_) {
        if (lance.portal && lance.portal->GetIsVisible())
            return true;
        if (lance.aura && lance.aura->GetIsVisible())
            return true;
    }
    if (shockwaveChargeVisual_ && shockwaveChargeVisual_->GetIsVisible()) {
        return true;
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.glow && beam.glow->GetIsVisible())
            return true;
    }
    for (const UmbraFlamePortal& portal : umbraFlames_) {
        if (portal.flame && portal.flame->GetIsVisible())
            return true;
    }
    return false;
}

void EnemyFalseKingSlime::DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 通常パスで描いた発光コアに、特殊材質の外周を追加します。
    if (crownShieldBarrier_ && crownShieldBarrier_->GetIsVisible()) {
        crownShieldBarrier_->RefreshRenderCameraData();
        crownShieldBarrier_->DrawGatePortal(depthSrvHandle, grabSrvHandle);
    }
    for (const CrownLance& lance : crownLances_) {
        if (lance.portal && lance.portal->GetIsVisible()) {
            lance.portal->RefreshRenderCameraData();
            lance.portal->DrawGatePortal(depthSrvHandle, grabSrvHandle);
        }
        if (lance.aura && lance.aura->GetIsVisible()) {
            lance.aura->RefreshRenderCameraData();
            lance.aura->DrawCrownUnlock(depthSrvHandle, grabSrvHandle);
        }
    }
    if (shockwaveChargeVisual_ && shockwaveChargeVisual_->GetIsVisible()) {
        shockwaveChargeVisual_->RefreshRenderCameraData();
        shockwaveChargeVisual_->DrawShockwave(depthSrvHandle, grabSrvHandle);
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.glow && beam.glow->GetIsVisible()) {
            beam.glow->RefreshRenderCameraData();
            beam.glow->DrawLaser(depthSrvHandle, grabSrvHandle);
        }
    }
    for (const UmbraFlamePortal& portal : umbraFlames_) {
        if (portal.flame && portal.flame->GetIsVisible()) {
            portal.flame->RefreshRenderCameraData();
            portal.flame->DrawFire(depthSrvHandle, grabSrvHandle);
        }
    }
}

void EnemyFalseKingSlime::SetDebugPreviewAttackId(const std::string& attackId) {
    // プレビュー攻撃の切り替え時に前の予兆・所有演出を片付けます。
    if (!attackId.empty()) {
        // 隔離プレビューは原点から離れた場所に置かれ、床に衝突判定がない。
        // 登場演出を通らないため、ここで接地の代替高さと見た目の基準を記録する。
        CaptureAuthoredState(true);
    }
    debugPreviewAttackId_ = attackId;
    currentAttack_ = AttackKind::None;
    attackState_ = AttackState::Idle;
    attackCooldown_ = 0.05f;
    attackTimer_ = 0.0f;
    HideAttackTelegraph();
    ClearTransientVisuals();
    if (attackId == kSealBarragePreviewId) {
        battlePhase_ = 2;
        InitializeCrownSeals();
        // 結晶の出現、制限時間の詠唱、失敗後の墜槍を本番と同じ秒数で再生します。
    } else if (attackId == kCrownDominionAttackId || attackId == kUmbraFlameAttackId || attackId == kAstralReaverAttackId ||
        attackId == kNightfallLancesAttackId) {
        battlePhase_ = 3;
    } else if (attackId == kRoyalCrossAttackId) {
        battlePhase_ = (std::max)(battlePhase_, 2);
    }
}

const char* EnemyFalseKingSlime::GetDebugAttackPhaseName() const {
    // プレビュー表示用に攻撃状態を短い日本語へ対応付ける。
    switch (attackState_) {
    case AttackState::Windup:
        return "予兆";
    case AttackState::Active:
        return "攻撃";
    case AttackState::Recovery:
        return "硬直";
    case AttackState::PhaseShift:
        return "形態移行";
    default:
        return "待機";
    }
}

const char* EnemyFalseKingSlime::GetDebugAttackBodyName() const {
    // 専用プレビュー ID があれば優先し、なければ実行中の攻撃名を返す。
    if (debugPreviewAttackId_ == kSealBarragePreviewId)
        return "Crown Seal Barrage";
    AttackKind kind = currentAttack_;
    if (kind == AttackKind::None) {
        kind = ResolveDebugAttack();
    }
    switch (kind) {
    case AttackKind::CrownLanceRain:
        return "Crown Lance Mesh";
    case AttackKind::RoyalShockwave:
        return "Spiked Royal Wave Mesh";
    case AttackKind::KingRush:
        return "Rush Wings + Boss Body";
    case AttackKind::RoyalCross:
        return "Sweeping Royal Blade Mesh";
    case AttackKind::CrownDominion:
        return "Dominion Sigil + Blades + Lances";
    case AttackKind::UmbraFlame:
        return "Umbra Flame Sigils + Violet Flame Volumes";
    case AttackKind::AstralReaver:
        return "Astral Reaver Ring Cutters + Orbital Impact Rings";
    case AttackKind::NightfallLances:
        return "Nightfall Lance + Target Sigil";
    default:
        return "None";
    }
}

size_t EnemyFalseKingSlime::GetDebugVisibleAttackVisualCount() const {
    // 攻撃中に実際に表示されている演出部品だけを数える。
    size_t visibleCount = 0;
    for (const CrownLance& lance : crownLances_) {
        if (lance.visual && lance.visual->GetIsVisible()) {
            ++visibleCount;
        }
    }
    for (const RoyalWave& wave : royalWaves_) {
        if ((wave.visual && wave.visual->GetIsVisible()) || (wave.glow && wave.glow->GetIsVisible())) {
            ++visibleCount;
        }
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.visual && beam.visual->GetIsVisible()) {
            ++visibleCount;
        }
    }
    if (rushWingsVisual_ && rushWingsVisual_->GetIsVisible()) {
        ++visibleCount;
    }
    if (dominionSigilVisual_ && dominionSigilVisual_->GetIsVisible()) {
        ++visibleCount;
    }
    if (umbraFlameNetwork_ && umbraFlameNetwork_->GetIsVisible()) {
        ++visibleCount;
    }
    if (spellChant_) {
        visibleCount += static_cast<size_t>(spellChant_->GetVisibleLayerCount());
    }
    for (const UmbraFlamePortal& portal : umbraFlames_) {
        if ((portal.sigil && portal.sigil->GetIsVisible()) || (portal.flame && portal.flame->GetIsVisible()))
            ++visibleCount;
    }
    for (const AstralReaverOrb& orb : astralReaverOrbs_) {
        for (EffectObject3d* effect : {orb.halo.get(), orb.blade.get(), orb.arc.get(), orb.core.get()}) {
            if (effect && effect->GetIsVisible())
                ++visibleCount;
        }
    }
    for (const auto& ring : astralReaverImpactRings_) {
        if (ring && ring->GetIsVisible())
            ++visibleCount;
    }
    for (EffectObject3d* effect : {astralReaverTargetRing_.get(), astralReaverImpactCore_.get()}) {
        if (effect && effect->GetIsVisible())
            ++visibleCount;
    }
    for (EffectObject3d* effect :
         {nightfallLances_.skySigil.get(), nightfallLances_.skyCounterSigil.get(),
          nightfallLances_.skyOuterRing.get(), nightfallLances_.skyInnerRing.get(),
          nightfallLances_.targetSigil.get(), nightfallLances_.targetRing.get(), nightfallLances_.innerRing.get(),
          nightfallLances_.spearCore.get(), nightfallLances_.spearGlow.get(), nightfallLances_.impactRing.get(),
          nightfallLances_.impactCore.get(), nightfallLances_.groundArc.get(), nightfallLances_.impactBeam.get(),
          nightfallLances_.impactBeamHalo.get(), nightfallLances_.blastRing.get(), nightfallLances_.blastShell.get()}) {
        if (effect && effect->GetIsVisible())
            ++visibleCount;
    }
    for (const auto& barb : nightfallLances_.barbs) {
        if (barb && barb->GetIsVisible())
            ++visibleCount;
    }
    for (const auto& lightning : nightfallLances_.impactLightning) {
        if (lightning && lightning->GetIsVisible())
            ++visibleCount;
    }
    for (const auto& lightning : nightfallLances_.groundLightning) {
        if (lightning && lightning->GetIsVisible())
            ++visibleCount;
    }
    for (const auto& effect : crownBarrageLayers_) {
        if (effect && effect->GetIsVisible())
            ++visibleCount;
    }
    for (const auto& spear : sealBarrage_) {
        if (spear.model && spear.model->GetIsVisible())
            ++visibleCount;
        for (EffectObject3d* effect : {spear.glow.get(), spear.portal.get(), spear.impactLightning.get(),
                                       spear.groundArc.get(), spear.impactArc.get(), spear.impactBeam.get()}) {
            if (effect && effect->GetIsVisible())
                ++visibleCount;
        }
    }
    for (EffectObject3d* effect : {crownBarrageSkySigil_.get(), crownBarrageSkyCounterSigil_.get(),
                                   crownBarrageSkyOuterRing_.get(), crownBarrageSkyInnerRing_.get()}) {
        if (effect && effect->GetIsVisible())
            ++visibleCount;
    }
    return visibleCount;
}

void EnemyFalseKingSlime::EnsureBaseScale() {
    // 初回だけ配置時の寸法を取得し、アニメーションの復帰先にします。
    if (hasBaseScale_) {
        return;
    }
    baseScale_ = GetScale();
    authoredScale_ = baseScale_;
    hasBaseScale_ = true;
}

// 攻撃と形態移行を優先し、待機中だけ次の技の射程に合わせて位置を調整します。
void EnemyFalseKingSlime::UpdateBehavior(float deltaTime, const engine::Vector3& targetDirection, float targetDistance,
                                         engine::Vector3& velocity) {
    // 形態移行、攻撃中、待機中の順に処理し、同じフレームで二つの行動を進めません。
    if (attackState_ == AttackState::PhaseShift) {
        UpdatePhaseShift(deltaTime, velocity);
        return;
    }
    if (attackState_ != AttackState::Idle) {
        UpdateAttack(deltaTime, targetDirection, targetDistance, velocity);
        return;
    }

    const int desiredPhase = ResolveDesiredBattlePhase();
    if (desiredPhase > battlePhase_ && debugPreviewAttackId_.empty()) {
        BeginPhaseShift(desiredPhase);
        return;
    }

    if (!target_ || !param_.has_value()) {
        const float verticalVelocity = velocity.y;
        velocity = CalculateWanderVelocity(deltaTime, 0.34f, 0.28f);
        velocity.y = verticalVelocity;
        UpdateFacing(velocity, 0.06f);
        return;
    }

    UpdateFacing(targetDirection);
    // 攻撃プレビューでは発見リアクションを挟まず、選択した攻撃の予兆へ直行します。
    if (debugPreviewAttackId_.empty() &&
        UpdateNoticeReaction(deltaTime, targetDistance, detectionRange_, targetDirection)) {
        return;
    }

    const AttackKind debugAttack = ResolveDebugAttack();
    const AttackKind plannedAttack =
        debugAttack != AttackKind::None ? debugAttack : ResolveAutomaticAttack(targetDistance);
    const EnemyAttackDefinition& planned = GetAttackDefinition(GetAttackId(plannedAttack));
    const bool inRange = targetDistance >= planned.minRange && targetDistance <= planned.maxRange;
    if (IsBossTargetEngaged() && attackCooldown_ <= 0.0f && inRange) {
        StartAttack(plannedAttack, targetDirection);
        return;
    }

    // 攻撃範囲外の時だけゆっくり間合いを詰め、広い闘技場を使わせます。
    const float moveSpeed = targetDistance > planned.maxRange
                                ? (std::max)(6.5f, param_->speed * 3.0f)
                                : (std::max)(0.85f, param_->speed * (battlePhase_ >= 3 ? 1.15f : 0.90f));
    const float preferredDistance = (std::clamp)(planned.recommendedTargetDistance, 7.0f, 15.0f);
    const engine::Vector3 spacing =
        CombatMath::MakePacedSpacingVelocity(targetDirection, targetDistance, preferredDistance - 3.0f,
                                             preferredDistance + 2.0f, moveSpeed, 0.48f, 0.42f, idleTimer_, 7.0f);
    velocity.x = spacing.x;
    velocity.z = spacing.z;
}

// 予兆開始時に照準と各演出タイマーを固定し、技ごとの配置物を準備します。
void EnemyFalseKingSlime::StartAttack(AttackKind kind, const engine::Vector3& targetDirection) {
    // 予兆開始時に照準を固定し、技ごとの見た目を攻撃時間に先立って準備します。
    if (kind == AttackKind::None) {
        return;
    }

    currentAttack_ = kind;
    attackState_ = AttackState::Windup;
    lockedDirection_ = NormalizePlanar(targetDirection);
    lockedTargetPosition_ = target_ ? FindGroundPoint(target_->GetWorldPosition())
                                    : FindGroundPoint(GetTranslate() + lockedDirection_ * 9.0f);
    attackStartPosition_ = FindGroundPoint(GetTranslate());
    attackEndPosition_ = attackStartPosition_;
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackStateDuration_ = (std::max)(0.18f, attack.windupDuration);
    attackTimer_ = attackStateDuration_;
    actionTimer_ = 0.0f;
    effectTimer_ = 0.0f;
    actionIndex_ = 0;
    lanceSpawnSerial_ = 0;
    warningTriggered_ = false;
    rushDamageApplied_ = false;
    shockwaveLanded_ = false;

    if (kind == AttackKind::CrownLanceRain) {
        PrepareLanceTargets(false);
        const int count = battlePhase_ == 1 ? 10 : (battlePhase_ == 2 ? 14 : 18);
        const float firstGateDelay = attackStateDuration_ * 0.08f;
        const float gateSequenceDuration = attackStateDuration_ * 0.62f;
        for (int index = 0; index < count; ++index) {
            const float sequenceRate = count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f;
            const float gateDelay = firstGateDelay + gateSequenceDuration * sequenceRate;
            const float launchAt = attackStateDuration_ + static_cast<float>(index) * kLanceInterval;
            const float readyDuration = (std::max)(0.08f, launchAt - gateDelay - 0.24f);
            SpawnCrownLance(lanceTargets_[static_cast<size_t>(index)], gateDelay, readyDuration);
        }
        actionIndex_ = count;
    } else if (kind == AttackKind::RoyalShockwave) {
        // 白い柔らかな輪は攻撃判定ではなく、外から中心へ力が集まる充填層に限定します。
        shockwaveChargeVisual_ = CreateRoyalVisual("FalseKing_ShockwaveCharge", "Primitives/cylinder",
                                                   kShockwaveMaterialType, {1.0f, 0.92f, 0.76f, 0.0f}, 1.65f);
        if (shockwaveChargeVisual_) {
            shockwaveChargeVisual_->SetIsVisible(false);
            if (MeshRenderer* renderer = shockwaveChargeVisual_->GetMeshRenderer()) {
                if (MeshRenderer::WaterParamForGPU* charge = renderer->GetWaterParamData()) {
                    charge->waveSpeed = 0.82f;
                    charge->waveFrequency = 3.2f;
                    charge->effectType = 3.0f;
                    charge->effectScale = 0.92f;
                    charge->effectSoftness = 0.22f;
                    charge->effectIntensity = 1.88f;
                }
            }
            shockwaveChargeVisual_->UpdateWorldMatrix(true);
        }
    } else if (kind == AttackKind::CrownDominion) {
        PrepareLanceTargets(true);
    } else if (kind == AttackKind::UmbraFlame || kind == AttackKind::AstralReaver ||
               kind == AttackKind::NightfallLances) {
        if (kind == AttackKind::UmbraFlame) {
            PrepareUmbraFlames();
        } else if (kind == AttackKind::AstralReaver) {
            PrepareAstralReaver();
        } else {
            PrepareNightfallLances();
        }
        BossChantStyle style;
        style.sigilEffectPath = kUmbraFlameSigilEffect;
        style.motePreset =
            kind == AttackKind::NightfallLances ? kNightfallLancesChargePreset : "false_king_chant_motes";
        style.accentColor = kind == AttackKind::NightfallLances
                                ? engine::Vector4{0.90f, 0.08f, 0.24f, 1.0f}
                                : (kind == AttackKind::AstralReaver ? engine::Vector4{0.82f, 0.22f, 1.0f, 1.0f}
                                                                    : engine::Vector4{0.80f, 0.30f, 1.0f, 1.0f});
        style.radius = std::clamp(baseScale_.x * 1.45f, 2.4f, 4.2f);
        style.fadeStart = kind == AttackKind::AstralReaver || kind == AttackKind::NightfallLances
                              ? 0.16f
                              : (std::max)(0.0f, attack.activeDuration - 0.24f);
        spellChant_ = std::make_unique<BossChantVisual>();
        if (!spellChant_->Start(common_, std::move(style))) {
            spellChant_.reset();
        }
    }
    if (debugPreviewAttackId_.empty()) {
        lastAutomaticAttack_ = kind;
        ++attackCycleIndex_;
        ++automaticAttackSerial_;
    }
}

void EnemyFalseKingSlime::UpdateAttack(float deltaTime, const engine::Vector3& targetDirection, float, engine::Vector3& velocity) {
    // 予兆、攻撃、硬直の状態で時間の減らし方と移動許可を切り替えます。
    if (attackState_ == AttackState::Windup) {
        UpdateWindup(deltaTime, targetDirection);
        return;
    }
    if (attackState_ == AttackState::Active) {
        UpdateActive(deltaTime, targetDirection, velocity);
        return;
    }
    if (attackState_ == AttackState::Recovery) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
        HideAttackTelegraph();
        if (attackTimer_ <= 0.0f) {
            FinishAttack();
        }
    }
}

void EnemyFalseKingSlime::UpdateWindup(float deltaTime, const engine::Vector3& targetDirection) {
    // 予告開始までは照準を追従させ、予告が見えた後は着弾先を固定します。
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
    effectTimer_ -= deltaTime;
    const float progress = 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f);
    const engine::Vector4 warningColor =
        battlePhase_ >= 3 ? engine::Vector4{0.90f, 0.18f, 1.0f, 0.86f} : engine::Vector4{1.0f, 0.72f, 0.12f, 0.86f};

    if (attackTimer_ > attack.warningLeadTime && target_ && currentAttack_ != AttackKind::CrownLanceRain &&
        currentAttack_ != AttackKind::UmbraFlame && currentAttack_ != AttackKind::AstralReaver &&
        currentAttack_ != AttackKind::NightfallLances) {
        lockedDirection_ = NormalizePlanar(targetDirection);
        lockedTargetPosition_ = FindGroundPoint(target_->GetWorldPosition());
        if (currentAttack_ == AttackKind::CrownLanceRain) {
            PrepareLanceTargets(false);
        }
    }
    UpdateFacing(lockedDirection_, 0.18f);

    switch (currentAttack_) {
    case AttackKind::CrownLanceRain: {
        const size_t count = static_cast<size_t>(battlePhase_ == 1 ? 10 : (battlePhase_ == 2 ? 14 : 18));
        ShowAttackTelegraphReadableImpactAreas(lanceTargets_.data(), count, attack.radius, progress, warningColor);
        break;
    }
    case AttackKind::RoyalShockwave: {
        // 溜めはボス直下の一層だけに絞り、着地後の攻撃リングと混同させません。
        const engine::Vector3 chargeCenter = FindGroundPoint(GetTranslate()) + engine::Vector3{0.0f, 0.06f, 0.0f};
        const float contraction = SmoothStep01(progress);
        const float pulse = 1.0f + std::sin(progress * kPi * 8.0f) * 0.06f * progress;
        if (shockwaveChargeVisual_) {
            const float radius = engine::Math::Lerp(5.8f, 2.75f, contraction) * pulse;
            shockwaveChargeVisual_->SetIsVisible(progress > 0.02f);
            shockwaveChargeVisual_->SetTranslate(chargeCenter);
            shockwaveChargeVisual_->SetScale({radius, 0.045f + progress * 0.035f, radius});
            shockwaveChargeVisual_->SetRotationY(-idleTimer_ * (0.45f + progress * 1.35f));
            shockwaveChargeVisual_->SetColor(
                {1.0f, battlePhase_ >= 3 ? 0.78f : 0.94f, battlePhase_ >= 3 ? 1.0f : 0.78f, 0.34f + progress * 0.46f});
            shockwaveChargeVisual_->Update(deltaTime);
            shockwaveChargeVisual_->UpdateWorldMatrix(true);
        }
        // 特殊マテリアルが省略されるプレビューでも、白い収束輪を必ず読めるようにします。
        if (effectTimer_ <= 0.0f) {
            constexpr int kChargePointCount = 12;
            const float chargeRadius = engine::Math::Lerp(5.8f, 2.75f, contraction) * pulse;
            const float rotationOffset = idleTimer_ * (0.55f + progress * 0.65f);
            for (int pointIndex = 0; pointIndex < kChargePointCount; ++pointIndex) {
                const float angle =
                    rotationOffset + static_cast<float>(pointIndex) * kTwoPi / static_cast<float>(kChargePointCount);
                const engine::Vector3 radial{std::sin(angle), 0.0f, std::cos(angle)};
                const engine::Vector3 particlePosition = chargeCenter + radial * chargeRadius + engine::Vector3{0.0f, 0.14f, 0.0f};
                EmitDirectedPreset(kShockwaveChargeRingPreset, particlePosition,
                                   {-radial.x, 0.16f + progress * 0.10f, -radial.z}, 0.46f + progress * 0.38f);
            }
            effectTimer_ += 0.13f;
        }
        break;
    }
    case AttackKind::KingRush:
        ShowAttackTelegraphLine(FindGroundPoint(GetTranslate()), lockedDirection_,
                                (std::min)(attack.maxRange, 13.0f + static_cast<float>(battlePhase_) * 2.6f),
                                attack.radius * 2.0f, progress, warningColor);
        break;
    case AttackKind::RoyalCross:
    case AttackKind::CrownDominion: {
        const bool dominion = currentAttack_ == AttackKind::CrownDominion;
        const int count = RoyalBeamCount(dominion, battlePhase_);
        const float step = kTwoPi / static_cast<float>(count);
        const float fanCenter = std::atan2(lockedDirection_.x, lockedDirection_.z) +
                                static_cast<float>(count - 1) * 0.5f * step;
        // 横ずらしをなくし、ボスの足元から実際の初期射線へ放射状に予告します。
        ShowAttackTelegraphLaneFan(FindGroundPoint(GetTranslate()),
                                   {std::sin(fanCenter), 0.0f, std::cos(fanCenter)}, attack.maxRange,
                                   RoyalBeamHalfWidth(dominion, attack.radius) * 2.0f,
                                   count, 0.0f, step, progress, warningColor);
        break;
    }
    case AttackKind::UmbraFlame:
        UpdateUmbraFlames(deltaTime, false);
        break;
    case AttackKind::AstralReaver:
        UpdateAstralReaver(deltaTime, false);
        break;
    case AttackKind::NightfallLances:
        UpdateNightfallLances(deltaTime, false);
        break;
    default:
        break;
    }

    if (effectTimer_ <= 0.0f && currentAttack_ != AttackKind::RoyalShockwave &&
        currentAttack_ != AttackKind::UmbraFlame && currentAttack_ != AttackKind::AstralReaver &&
        currentAttack_ != AttackKind::NightfallLances) {
        EmitDirectedPreset(currentAttack_ == AttackKind::CrownDominion ? kDominionPreset : kChargePreset,
                           GetWorldPosition() + engine::Vector3{0.0f, baseScale_.y * 1.5f, 0.0f},
                           {-lockedDirection_.x * 0.12f, 1.0f, -lockedDirection_.z * 0.12f}, 0.92f + progress * 0.78f);
        effectTimer_ += (std::max)(0.12f, 0.24f - progress * 0.08f);
    }
    if (!warningTriggered_ && attackTimer_ <= attack.warningLeadTime) {
        TriggerAttackTelegraphCue(warningColor);
        warningTriggered_ = true;
    }
    if (attackTimer_ <= 0.0f) {
        BeginActive();
    }
}

void EnemyFalseKingSlime::BeginActive() {
    // 予兆から実攻撃へ進め、命中判定の開始時刻を揃えます。
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackState_ = AttackState::Active;
    attackStateDuration_ = (std::max)(0.20f, attack.activeDuration);
    attackTimer_ = attackStateDuration_;
    actionTimer_ = 0.0f;
    effectTimer_ = 0.0f;
    actionIndex_ =
        currentAttack_ == AttackKind::CrownLanceRain ? (battlePhase_ == 1 ? 10 : (battlePhase_ == 2 ? 14 : 18)) : 0;
    HideAttackTelegraph();

    switch (currentAttack_) {
    case AttackKind::RoyalShockwave:
        BeginShockwaveJump();
        break;
    case AttackKind::KingRush:
        BeginRush();
        break;
    case AttackKind::RoyalCross:
        SpawnRoyalBeams(false);
        break;
    case AttackKind::CrownDominion:
        SpawnRoyalBeams(true);
        dominionSigilVisual_ =
            CreateRoyalMesh("FalseKing_DominionSigil", kDominionSigilModel, {1.0f, 1.0f, 1.0f, 1.0f}, 2.15f);
        if (dominionSigilVisual_) {
            dominionSigilVisual_->SetIsVisible(false);
            dominionSigilVisual_->UpdateWorldMatrix(true);
        }
        dominionLanceTimer_ = 0.32f;
        VFXSequencer::PlayOneShot(kDominionSequence, FindGroundPoint(GetTranslate()), {2.4f, 2.4f, 2.4f});
        break;
    case AttackKind::UmbraFlame:
        umbraFlameFanHit_ = false;
        for (UmbraFlamePortal& portal : umbraFlames_) {
            portal.activeAge = -portal.delay * 0.55f;
            portal.particleTimer = 0.0f;
        }
        break;
    case AttackKind::AstralReaver:
        astralReaverElapsed_ = 0.0f;
        astralReaverImpactAge_ = -1.0f;
        astralReaverDamageApplied_ = false;
        break;
    case AttackKind::NightfallLances:
        nightfallLances_.activeAge = 0.0f;
        nightfallLances_.particleTimer = 0.0f;
        nightfallLances_.damageApplied = false;
        break;
    default:
        break;
    }
}

void EnemyFalseKingSlime::UpdateActive(float deltaTime, const engine::Vector3& targetDirection, engine::Vector3& velocity) {
    // 技種別に応じた移動・弾・判定を更新し、終了時に硬直へ移します。
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
    actionTimer_ -= deltaTime;
    effectTimer_ -= deltaTime;

    switch (currentAttack_) {
    case AttackKind::CrownLanceRain: {
        const int count = battlePhase_ == 1 ? 10 : (battlePhase_ == 2 ? 14 : 18);
        while (actionIndex_ < count && actionTimer_ <= 0.0f) {
            SpawnNextCrownLance(false);
            ++actionIndex_;
            actionTimer_ += kLanceInterval;
        }
        break;
    }
    case AttackKind::RoyalShockwave:
        UpdateShockwaveJump(deltaTime, velocity);
        break;
    case AttackKind::KingRush:
        UpdateRush(deltaTime, targetDirection, velocity);
        break;
    case AttackKind::RoyalCross:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        break;
    case AttackKind::CrownDominion:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateDominion(deltaTime);
        break;
    case AttackKind::UmbraFlame:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateUmbraFlames(deltaTime, true);
        break;
    case AttackKind::AstralReaver:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateAstralReaver(deltaTime, true);
        break;
    case AttackKind::NightfallLances:
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateNightfallLances(deltaTime, true);
        break;
    default:
        break;
    }

    if (attackTimer_ <= 0.0f) {
        BeginRecovery();
    }
}

void EnemyFalseKingSlime::BeginRecovery() {
    // 攻撃後の硬直では予告を消し、次の攻撃までの間を作ります。
    attackState_ = AttackState::Recovery;
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackStateDuration_ = (std::max)(0.18f, attack.recoveryDuration);
    attackTimer_ = attackStateDuration_;
    HideAttackTelegraph();
    SetVelocity({0.0f, GetVelocity().y, 0.0f});
    rushWingsVisual_.reset();
    dominionSigilVisual_.reset();
    if (currentAttack_ == AttackKind::UmbraFlame) {
        umbraFlames_.clear();
        umbraFlameNetwork_.reset();
    }
    if (currentAttack_ == AttackKind::AstralReaver) {
        for (AstralReaverOrb& orb : astralReaverOrbs_) {
            orb.halo.reset();
            orb.blade.reset();
            orb.arc.reset();
            orb.core.reset();
        }
        for (auto& ring : astralReaverImpactRings_)
            ring.reset();
        astralReaverTargetRing_.reset();
        astralReaverImpactCore_.reset();
    }
    if (currentAttack_ == AttackKind::NightfallLances) {
        nightfallLances_ = {};
    }
    spellChant_.reset();
}

void EnemyFalseKingSlime::FinishAttack() {
    // 攻撃の一時状態を戻し、地面上の位置を確定して待機へ戻ります。
    const EnemyAttackDefinition& attack = GetCurrentAttackDefinition();
    attackCooldown_ =
        (std::max)(0.35f, attack.cooldown * (battlePhase_ == 1 ? 1.0f : (battlePhase_ == 2 ? 0.88f : 0.78f)));
    currentAttack_ = AttackKind::None;
    attackState_ = AttackState::Idle;
    attackTimer_ = 0.0f;
    actionIndex_ = 0;
    SetTranslate(FindGroundPoint(GetTranslate()));
    SetGrounded(true);
}

void EnemyFalseKingSlime::UpdateSpellChant(float deltaTime) {
    // ボスの現在の寸法と向きから、詠唱 VFX の中心と進行度を組み立てます。
    if (!spellChant_)
        return;
    BossChantFrame frame;
    frame.casterPosition = GetWorldPosition();
    frame.groundPosition = FindGroundPoint(frame.casterPosition);
    const AABB bounds = GetModelWorldAABB();
    frame.casterRadius =
        (std::max)({(bounds.max.x - bounds.min.x) * 0.5f, (bounds.max.z - bounds.min.z) * 0.5f, baseScale_.x * 0.84f});
    frame.casterHeight = (std::max)(bounds.max.y - bounds.min.y, baseScale_.y * 1.2f);
    frame.sigilCenter = frame.casterPosition + lockedDirection_ * std::clamp(frame.casterRadius * 1.55f, 2.5f, 5.4f) +
                        engine::Vector3{0.0f, frame.casterHeight * 0.48f, 0.0f};
    frame.forward = lockedDirection_;
    frame.casting = (currentAttack_ == AttackKind::UmbraFlame || currentAttack_ == AttackKind::AstralReaver ||
                     currentAttack_ == AttackKind::NightfallLances) &&
                    attackState_ == AttackState::Windup;
    frame.releasing = (currentAttack_ == AttackKind::UmbraFlame || currentAttack_ == AttackKind::AstralReaver ||
                       currentAttack_ == AttackKind::NightfallLances) &&
                      attackState_ == AttackState::Active;
    frame.windupProgress =
        frame.casting ? 1.0f - std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.001f), 0.0f, 1.0f) : 1.0f;
    frame.activeElapsed = frame.releasing ? (std::max)(0.0f, attackStateDuration_ - attackTimer_) : 0.0f;
    frame.time = idleTimer_;
    frame.deltaTime = deltaTime;
    spellChant_->Update(frame);
}

void EnemyFalseKingSlime::PrepareAttackCycle() {
    // 形態別の候補をシャッフルし、直前と同じ技が先頭に続くのを避けます。
    static constexpr std::array<AttackKind, 3> phaseOne = {AttackKind::CrownLanceRain, AttackKind::RoyalShockwave,
                                                           AttackKind::KingRush};
    static constexpr std::array<AttackKind, 4> phaseTwo = {AttackKind::RoyalCross, AttackKind::CrownLanceRain,
                                                           AttackKind::KingRush, AttackKind::RoyalShockwave};
    static constexpr std::array<AttackKind, 8> phaseThree = {
        AttackKind::CrownDominion,   AttackKind::KingRush,       AttackKind::UmbraFlame, AttackKind::AstralReaver,
        AttackKind::NightfallLances, AttackKind::CrownLanceRain, AttackKind::RoyalCross, AttackKind::RoyalShockwave};

    attackCycleCount_ = battlePhase_ <= 1 ? phaseOne.size() : (battlePhase_ == 2 ? phaseTwo.size() : phaseThree.size());
    const AttackKind* source =
        battlePhase_ <= 1 ? phaseOne.data() : (battlePhase_ == 2 ? phaseTwo.data() : phaseThree.data());
    std::copy_n(source, attackCycleCount_, attackCycle_.begin());
    std::shuffle(attackCycle_.begin(), attackCycle_.begin() + attackCycleCount_, attackRandom_);
    if (attackCycleCount_ > 1 && attackCycle_[0] == lastAutomaticAttack_) {
        std::swap(attackCycle_[0], attackCycle_[1]);
    }
    attackCycleIndex_ = 0;
    attackCyclePhase_ = battlePhase_;
}

EnemyFalseKingSlime::AttackKind EnemyFalseKingSlime::ResolveAutomaticAttack(float targetDistance) {
    // 残り候補の中から現在の射程で使える技を前へ寄せます。
    if (attackCyclePhase_ != battlePhase_ || attackCycleIndex_ >= attackCycleCount_) {
        PrepareAttackCycle();
    }
    // 射程に入っている未使用技を優先し、遠距離で短射程技だけを待つ状態を避けます。
    for (std::size_t i = attackCycleIndex_; i < attackCycleCount_; ++i) {
        if (attackCycleIndex_ == 0 && attackCycle_[i] == lastAutomaticAttack_) {
            continue;
        }
        const EnemyAttackDefinition& attack = GetAttackDefinition(GetAttackId(attackCycle_[i]));
        if (targetDistance >= attack.minRange && targetDistance <= attack.maxRange) {
            std::swap(attackCycle_[attackCycleIndex_], attackCycle_[i]);
            break;
        }
    }
    return attackCycle_[attackCycleIndex_];
}

EnemyFalseKingSlime::AttackKind EnemyFalseKingSlime::ResolveDebugAttack() const {
    // エディターで指定した ID を実行用の攻撃種別へ変換します。
    if (debugPreviewAttackId_ == kCrownLanceRainAttackId)
        return AttackKind::CrownLanceRain;
    if (debugPreviewAttackId_ == kRoyalShockwaveAttackId)
        return AttackKind::RoyalShockwave;
    if (debugPreviewAttackId_ == kKingRushAttackId)
        return AttackKind::KingRush;
    if (debugPreviewAttackId_ == kRoyalCrossAttackId)
        return AttackKind::RoyalCross;
    if (debugPreviewAttackId_ == kCrownDominionAttackId)
        return AttackKind::CrownDominion;
    if (debugPreviewAttackId_ == kUmbraFlameAttackId)
        return AttackKind::UmbraFlame;
    if (debugPreviewAttackId_ == kAstralReaverAttackId)
        return AttackKind::AstralReaver;
    if (debugPreviewAttackId_ == kNightfallLancesAttackId)
        return AttackKind::NightfallLances;
    return AttackKind::None;
}

const char* EnemyFalseKingSlime::GetAttackId(AttackKind kind) const {
    // 実行用の列挙値から設定 JSON の攻撃 ID を取得します。
    switch (kind) {
    case AttackKind::CrownLanceRain:
        return kCrownLanceRainAttackId;
    case AttackKind::RoyalShockwave:
        return kRoyalShockwaveAttackId;
    case AttackKind::KingRush:
        return kKingRushAttackId;
    case AttackKind::RoyalCross:
        return kRoyalCrossAttackId;
    case AttackKind::CrownDominion:
        return kCrownDominionAttackId;
    case AttackKind::UmbraFlame:
        return kUmbraFlameAttackId;
    case AttackKind::AstralReaver:
        return kAstralReaverAttackId;
    case AttackKind::NightfallLances:
        return kNightfallLancesAttackId;
    default:
        return kCrownLanceRainAttackId;
    }
}

const EnemyAttackDefinition& EnemyFalseKingSlime::GetCurrentAttackDefinition() const {
    // 現在技のパラメータは攻撃プロファイルを参照します。
    return GetAttackDefinition(GetAttackId(currentAttack_));
}

int EnemyFalseKingSlime::ResolveDesiredBattlePhase() const {
    // HP が閾値まで減った後は結晶戦へ、最終闘技場への移動後は第三形態へ進めます。
    const float maximumHp = GetEncounterMaximumHp();
    const float hpRate = maximumHp > 0.0f ? GetEncounterCurrentHp() / maximumHp : 1.0f;
    if (finalArenaStarted_)
        return 3;
    if (hpRate <= 0.6701f)
        return 2;
    return 1;
}

void EnemyFalseKingSlime::BeginPhaseShift(int nextPhase) {
    // 古い攻撃の予告と一時描画物を消してから形態移行演出を開始します。
    pendingBattlePhase_ = (std::clamp)(nextPhase, 1, 3);
    attackState_ = AttackState::PhaseShift;
    currentAttack_ = AttackKind::None;
    phaseTransitionTimer_ = 0.0f;
    phaseShiftStartPosition_ = GetTranslate();
    attackTimer_ = 0.0f;
    ClearTransientVisuals();
    HideAttackTelegraph();
    SetVelocity({0.0f, 0.0f, 0.0f});
    VFXSequencer::PlayOneShot(kPhaseShiftSequence, FindGroundPoint(GetTranslate()) + engine::Vector3{0.0f, 0.15f, 0.0f},
                              {2.1f + static_cast<float>(pendingBattlePhase_) * 0.25f,
                               2.1f + static_cast<float>(pendingBattlePhase_) * 0.25f,
                               2.1f + static_cast<float>(pendingBattlePhase_) * 0.25f});
}

void EnemyFalseKingSlime::UpdatePhaseShift(float deltaTime, engine::Vector3& velocity) {
    // 第二形態では結晶の中心へ先に移動し、移行完了後に結晶を生成します。
    phaseTransitionTimer_ += deltaTime;
    velocity = {0.0f, 0.0f, 0.0f};
    const float progress = GetPhaseTransitionProgress();
    if (pendingBattlePhase_ == 2) {
        // 形態変化の間に舞台中央へ移動し、結晶召喚より先に立ち位置を確定させます。
        const engine::Vector3 center = FindGroundPoint(
            authoredPosition_ + engine::Vector3{0.0f, 0.0f, CrownFinaleTuning::Get().sealCenterOffsetZ});
        SetTranslate(LerpVector3(phaseShiftStartPosition_, center, SmoothStep01(progress)));
    }
    effectTimer_ -= deltaTime;
    if (effectTimer_ <= 0.0f) {
        const float angle = idleTimer_ * (4.0f + static_cast<float>(pendingBattlePhase_));
        const engine::Vector3 direction = {std::sin(angle) * 0.45f, 1.0f, std::cos(angle) * 0.45f};
        EmitDirectedPreset(kDominionPreset, GetWorldPosition() + engine::Vector3{0.0f, 1.4f, 0.0f}, direction,
                           1.0f + progress * 0.9f);
        effectTimer_ += 0.14f;
    }
    if (phaseTransitionTimer_ >= phaseTransitionDuration_) {
        battlePhase_ = pendingBattlePhase_;
        if (battlePhase_ == 2 && debugPreviewAttackId_.empty())
            InitializeCrownSeals();
        attackState_ = AttackState::Idle;
        phaseTransitionTimer_ = phaseTransitionDuration_;
        attackCooldown_ = 0.72f;
        SetTranslate(FindGroundPoint(GetTranslate()));
        SetGrounded(true);
    }
}

void EnemyFalseKingSlime::UpdateFacing(const engine::Vector3& direction, float follow) {
    // モデル固有の前方オフセットを足し、最短角度で目標へ向けます。
    if (direction.x * direction.x + direction.z * direction.z <= 0.0001f) {
        return;
    }
    const float targetYaw = std::atan2(direction.x, direction.z) + kModelYawOffset;
    SetRotationY(engine::Math::LerpShortAngle(GetRotation().y, targetYaw, (std::clamp)(follow, 0.0f, 1.0f)));
}

void EnemyFalseKingSlime::ApplyBossAnimation(float deltaTime) {
    // 共通の跳ね姿勢を基準に、予兆・攻撃・形態移行の伸縮を重ねます。
    SlimeBounceAnimator::Params params;
    params.speedForFullBounce = 2.8f;
    params.idleAmplitude = 0.050f;
    params.moveAmplitude = 0.17f;
    params.hopFrequency = 3.2f + static_cast<float>(battlePhase_) * 0.45f;
    params.horizontalSquash = 0.18f;
    params.verticalStretch = 0.23f;
    params.airborneStretch = 0.33f;

    engine::Vector3 targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    engine::Vector3 targetRotation = {authoredRotation_.x, GetRotation().y, authoredRotation_.z};
    if (attackState_ == AttackState::Windup) {
        const float progress = SmoothStep01(1.0f - attackTimer_ / (std::max)(attackStateDuration_, 0.01f));
        const float tremble = std::sin(progress * kPi * 18.0f) * progress * 0.045f;
        targetScale = {
            baseScale_.x * (1.0f + progress * 0.35f + tremble),
            baseScale_.y * (1.0f - progress * 0.32f),
            baseScale_.z * (1.0f + progress * 0.35f - tremble),
        };
        targetRotation.z += tremble * 1.7f;
    } else if (attackState_ == AttackState::Active) {
        const float progress = 1.0f - attackTimer_ / (std::max)(attackStateDuration_, 0.01f);
        if (currentAttack_ == AttackKind::RoyalShockwave) {
            const float jumpProgress =
                std::clamp(shockwaveJumpTimer_ / (std::max)(shockwaveJumpDuration_, 0.01f), 0.0f, 1.0f);
            if (!shockwaveLanded_) {
                const float airStretch = std::sin(jumpProgress * kPi);
                targetScale = {
                    baseScale_.x * (0.92f - airStretch * 0.08f),
                    baseScale_.y * (1.18f + airStretch * 0.28f),
                    baseScale_.z * (0.92f - airStretch * 0.08f),
                };
                targetRotation.x -= std::sin(jumpProgress * kPi) * 0.12f;
            } else {
                const float landingAge = (std::max)(0.0f, shockwaveJumpTimer_ - shockwaveJumpDuration_);
                const float settle = std::clamp(landingAge / 0.42f, 0.0f, 1.0f);
                const float rebound = std::sin(settle * kPi * 2.0f) * (1.0f - settle) * 0.12f;
                targetScale = {
                    baseScale_.x * (1.42f - settle * 0.24f + rebound),
                    baseScale_.y * (0.54f + settle * 0.26f - rebound * 0.55f),
                    baseScale_.z * (1.42f - settle * 0.24f - rebound),
                };
            }
        } else if (currentAttack_ == AttackKind::KingRush) {
            const float pulse = std::sin(progress * kPi * 14.0f) * 0.04f;
            targetScale = {
                baseScale_.x * (1.20f + pulse),
                baseScale_.y * 0.70f,
                baseScale_.z * (1.48f - pulse),
            };
            targetRotation.x -= 0.13f;
        } else {
            const float castPulse = std::sin(progress * kPi * 10.0f);
            targetScale = {
                baseScale_.x * (1.18f + std::abs(castPulse) * 0.08f),
                baseScale_.y * (0.80f - castPulse * 0.07f),
                baseScale_.z * (1.18f + std::abs(castPulse) * 0.08f),
            };
            targetRotation.z += castPulse * 0.055f;
        }
    } else if (attackState_ == AttackState::PhaseShift) {
        const float progress = GetPhaseTransitionProgress();
        const float surge = std::sin(progress * kPi);
        const float tremble = std::sin(progress * kPi * 24.0f) * surge * 0.05f;
        targetScale = {
            baseScale_.x * (1.0f + surge * 0.34f + tremble),
            baseScale_.y * (1.0f + surge * 0.42f),
            baseScale_.z * (1.0f + surge * 0.34f - tremble),
        };
        targetRotation.z += tremble * 1.8f;
    } else if (attackState_ == AttackState::Recovery) {
        const float remaining = std::clamp(attackTimer_ / (std::max)(attackStateDuration_, 0.01f), 0.0f, 1.0f);
        const float bounce = std::sin((1.0f - remaining) * kPi * 2.6f) * remaining;
        targetScale = {
            baseScale_.x * (1.0f + bounce * 0.20f),
            baseScale_.y * (1.0f - bounce * 0.27f),
            baseScale_.z * (1.0f + bounce * 0.20f),
        };
    }
    if (attackState_ == AttackState::Idle) {
        SlimeBounceAnimator::ApplyTravelPose(targetScale, targetRotation, baseScale_, GetVelocity(),
                                             GetRotation().y - kModelYawOffset, 4.2f, 0.045f, 0.060f);
    }
    ApplyDamageReactionPose(targetScale, targetRotation);
    const float follow = 1.0f - std::exp(-deltaTime * 11.0f);
    const engine::Vector3 currentScale = GetScale();
    SetScale({
        engine::Math::Lerp(currentScale.x, targetScale.x, follow),
        engine::Math::Lerp(currentScale.y, targetScale.y, follow),
        engine::Math::Lerp(currentScale.z, targetScale.z, follow),
    });
    const engine::Vector3 currentRotation = GetRotation();
    SetRotation({
        engine::Math::Lerp(currentRotation.x, targetRotation.x, follow),
        currentRotation.y,
        engine::Math::Lerp(currentRotation.z, targetRotation.z, follow),
    });
}

void EnemyFalseKingSlime::UpdateRoyalGlow(float deltaTime) {
    // 形態色と装甲開放状態を発光へ反映し、攻撃中だけ周囲の粒を放出します。
    ambientSparkTimer_ -= deltaTime;
    const bool attacking = attackState_ == AttackState::Windup || attackState_ == AttackState::Active ||
                           attackState_ == AttackState::PhaseShift;
    const float pulse = 0.5f + std::sin(idleTimer_ * (attacking ? 8.2f : 3.4f)) * 0.5f;
    engine::Vector4 phaseColor = authoredColor_;
    if (battlePhase_ == 2) {
        phaseColor = {1.0f, 0.90f, 1.0f, authoredColor_.w};
    } else if (battlePhase_ >= 3) {
        phaseColor = armorOpenTimer_ > 0.0f ? engine::Vector4{1.0f, 0.92f, 0.50f, authoredColor_.w}
                                            : engine::Vector4{0.74f, 0.58f, 1.0f, authoredColor_.w};
    }
    defaultColor_ = phaseColor;
    SetColor(phaseColor);
    SetEmissive(authoredEmissive_ + static_cast<float>(battlePhase_ - 1) * 0.34f + (attacking ? 0.52f : 0.0f) +
                pulse * 0.12f);
    if (!attacking) {
        ambientSparkTimer_ = 0.0f;
        return;
    }
    if (ambientSparkTimer_ <= 0.0f) {
        EmitDirectedPreset(battlePhase_ >= 3 ? kDominionPreset : kChargePreset,
                           GetWorldPosition() + engine::Vector3{0.0f, baseScale_.y * 1.28f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.12f);
        ambientSparkTimer_ += battlePhase_ >= 3 ? 0.14f : 0.18f;
    }
}

void EnemyFalseKingSlime::EmitPreset(const char* presetName, const engine::Vector3& position) const {
    // 粒子管理が未初期化のプレビューでは放出を省略します。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->Emit(presetName, position);
    }
}

void EnemyFalseKingSlime::EmitDirectedPreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                                             float speedScale) const {
    // 方向付き粒子も管理側が使える時だけ発行します。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->EmitDirected(presetName, position, direction, speedScale);
    }
}

void EnemyFalseKingSlime::ClearTransientVisuals() {
    // 技を中断した後も攻撃物が残らないよう、所有演出とタイマーをまとめて破棄します。
    crownLances_.clear();
    lanceSpawnSerial_ = 0;
    royalWaves_.clear();
    royalBeams_.clear();
    umbraFlames_.clear();
    umbraFlameNetwork_.reset();
    spellChant_.reset();
    for (AstralReaverOrb& orb : astralReaverOrbs_) {
        orb.halo.reset();
        orb.blade.reset();
        orb.arc.reset();
        orb.core.reset();
    }
    for (auto& ring : astralReaverImpactRings_)
        ring.reset();
    astralReaverTargetRing_.reset();
    astralReaverImpactCore_.reset();
    astralReaverElapsed_ = 0.0f;
    astralReaverImpactAge_ = -1.0f;
    astralReaverDamageApplied_ = false;
    nightfallLances_ = {};
    sealNightfallSatellites_ = {};
    shockwaveChargeVisual_.reset();
    rushWingsVisual_.reset();
    dominionSigilVisual_.reset();
    shockwaveJumpTimer_ = 0.0f;
    shockwaveLanded_ = false;
}

void EnemyFalseKingSlime::DrawOwnedSpecialMaterialVisualsForCamera(Camera* camera, uint32_t depthSrvHandle,
                                                                   uint32_t grabSrvHandle, int previewBufferIndex) {
    // 指定カメラ用に特殊材質の所有物を描き、別視点プレビューへも対応します。
    if (!camera) {
        return;
    }
    if (crownShieldBarrier_ && crownShieldBarrier_->GetIsVisible()) {
        crownShieldBarrier_->DrawSpecialMaterialForCamera(camera, depthSrvHandle, grabSrvHandle, previewBufferIndex);
    }
    for (const CrownLance& lance : crownLances_) {
        for (engine::Object3d* visual : {lance.portal.get(), lance.aura.get()}) {
            if (visual && visual->GetIsVisible()) {
                visual->DrawSpecialMaterialForCamera(camera, depthSrvHandle, grabSrvHandle, previewBufferIndex);
            }
        }
    }
    if (shockwaveChargeVisual_ && shockwaveChargeVisual_->GetIsVisible()) {
        shockwaveChargeVisual_->DrawSpecialMaterialForCamera(camera, depthSrvHandle, grabSrvHandle, previewBufferIndex);
    }
    for (const RoyalBeam& beam : royalBeams_) {
        if (beam.glow && beam.glow->GetIsVisible()) {
            beam.glow->DrawSpecialMaterialForCamera(camera, depthSrvHandle, grabSrvHandle, previewBufferIndex);
        }
    }
    for (const UmbraFlamePortal& portal : umbraFlames_) {
        if (portal.flame && portal.flame->GetIsVisible()) {
            portal.flame->DrawSpecialMaterialForCamera(camera, depthSrvHandle, grabSrvHandle, previewBufferIndex);
        }
    }
}
