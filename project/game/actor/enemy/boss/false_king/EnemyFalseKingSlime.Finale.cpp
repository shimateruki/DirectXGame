#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.Internal.h"

using namespace EnemyFalseKingSlimeImplementation;

namespace {
void UpdateFinaleLayer(EffectObject3d* effect, float deltaTime, bool visible,
    const Vector3& position, const Vector3& scale, const Vector4& color) {
    if (!effect) return;
    effect->Update(deltaTime);
    effect->SetIsVisible(visible);
    effect->SetTranslate(position);
    effect->SetScale(scale);
    effect->SetColor(color);
    effect->UpdateLocalMatrix();
    effect->UpdateWorldMatrix(true);
}
}

// 元の更新順序と判定を維持して、担当処理をまとめています。
float EnemyFalseKingSlime::FilterFinaleDamage(float damage) {
    if (!debugPreviewAttackId_.empty() || !IsEncounterControlled()) return damage;
    if (crownInterlude_ || attackState_ == AttackState::PhaseShift) return 0.0f;
    if (battlePhase_ == 2) {
        const auto& tuning = CrownFinaleTuning::Get();
        const float incoming = std::max(0.0f, damage);
        // 封印が残る間も攻撃を無意味にせず、累積した王冠への圧力で光る封印を破れます。
        if (brokenSeals_ < 3 && sealState_ != SealState::Broken) {
            sealPressureDamage_ += incoming;
            if (sealPressureDamage_ >= tuning.sealBreakDamage) {
                TryBreakCrownSeal(crownSeals_[brokenSeals_].position, brokenSeals_);
            }
        }
        const float floorHp = GetEncounterMaximumHp() * 0.34f;
        return std::min(incoming * tuning.sealArmorDamageRate,
            std::max(0.0f, GetEncounterCurrentHp() - floorHp));
    }
    if (battlePhase_ == 1) {
        return std::clamp(damage, 0.0f, std::max(0.0f, GetEncounterCurrentHp() - GetEncounterMaximumHp() * 0.67f));
    }
    return std::max(0.0f, damage) * (armorOpenTimer_ > 0.0f ? 1.0f : 0.30f);
}

void EnemyFalseKingSlime::OpenCrownArmor() {
    if (battlePhase_ != 3 || crownInterlude_) return;
    armorOpenTimer_ = CrownFinaleTuning::Get().armorOpenTime;
    VFXSequencer::PlayOneShot(kPhaseShiftSequence, GetWorldPosition(), { 1.1f, 1.1f, 1.1f });
}

void EnemyFalseKingSlime::ResetCrownFinale() {
    crownShieldRing_.reset();
    crownShieldShell_.reset();
    for (auto& seal : crownSeals_) { seal = CrownSeal{}; }
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
}

void EnemyFalseKingSlime::InitializeCrownSeals() {
    const auto& tuning = CrownFinaleTuning::Get();
    brokenSeals_ = 0;
    // ボス初期位置の前方に配置し、闘技場の入口側から順に誘導できます。
    const Vector3 center = authoredPosition_ + Vector3{ 0.0f, 0.0f, 15.0f };
    for (int i = 0; i < 3; ++i) {
        auto& seal = crownSeals_[i];
        const float angle = -kPi / 2.0f + static_cast<float>(i) * kTwoPi / 3.0f;
        seal.position = FindGroundPoint(center + Vector3{ std::sin(angle) * tuning.sealRadius, 0.0f, std::cos(angle) * tuning.sealRadius });
        seal.broken = false;
        seal.breakAge = 0.0f;
        seal.crystal = CreateRoyalMesh("CrownSeal", "Effects/false_king_seal_core", { 1, 1, 1, 1 }, 1.2f);
        seal.ring = CreateRoyalMesh("CrownSealBase", "Effects/false_king_seal_base", { 1, 1, 1, 1 }, 0.6f);
        seal.link = CreateRoyalMesh("CrownSealLink", "Effects/false_king_seal_link", { 1, 0.65f, 0.18f, 0.5f }, 1.4f);
        seal.gem = CreateRoyalMesh("CrownSealGem", "Effects/false_king_seal_core", { 1, 1, 1, 1 }, 1.5f);
        if (seal.link) { seal.link->SetBlendMode(BlendMode::kAdd); seal.link->SetEnableLighting(false); }
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
    auto makeShield = [this](const char* path, const char* name) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common_);
        if (!effect->LoadFromJson(path)) return std::unique_ptr<EffectObject3d>{};
        effect->SetName(name);
        effect->SetIsVisible(false);
        effect->Play(100.0f);
        return effect;
    };
    crownShieldRing_ = makeShield(kNightfallLancesRingEffect, "FalseKing_CrownShieldRing");
    crownShieldShell_ = makeShield(kAstralReaverOrbEffect, "FalseKing_CrownShieldShell");
    HideAttackTelegraph();
}

void EnemyFalseKingSlime::TryBreakCrownSeal(const Vector3& impact, int attackStage) {
    if (battlePhase_ != 2 || !debugPreviewAttackId_.empty() || brokenSeals_ >= 3) return;
    // 光っている封印のみ有効。雨の複数弾が同フレームに全封印を壊すことを防ぎます。
    auto& seal = crownSeals_[brokenSeals_];
    if (seal.broken || attackStage != brokenSeals_ ||
        PlanarDistance(impact, seal.position) > CrownFinaleTuning::Get().sealHitRadius) return;
    seal.broken = true;
    seal.breakAge = 0.0f;
    sealState_ = SealState::Broken;
    sealStateTimer_ = 0.0f;
    sealPressureDamage_ = 0.0f;
    HideAttackTelegraph();
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera())
        camera->StartShake(0.32f, 0.11f + brokenSeals_ * 0.025f, 24.0f, { 0.6f, 0.5f, 0.25f });
    EmitDirectedPreset(kLanceImpactPreset, seal.position, { 0.0f, 1.0f, 0.0f }, 1.8f);
    ++brokenSeals_;
    sealAttackSerial_ = 0;
    if (param_) param_->hp = std::min(param_->hp,
        GetEncounterMaximumHp() * (0.67f - 0.11f * static_cast<float>(brokenSeals_)));
}

void EnemyFalseKingSlime::UpdateCrownSeals(float deltaTime) {
    idleTimer_ += deltaTime;
    sealStateTimer_ += deltaTime;
    sealVfxTimer_ += deltaTime;
    const float shieldPulse = 1.0f + 0.07f * std::sin(idleTimer_ * 4.6f);
    const float shieldAlpha = brokenSeals_ < 3 ? (0.11f + 0.035f * std::sin(idleTimer_ * 5.0f)) : 0.0f;
    const Vector3 shieldCenter = GetTranslate() + Vector3{ 0.0f, baseScale_.y * 1.15f, 0.0f };
    UpdateFinaleLayer(crownShieldShell_.get(), deltaTime, brokenSeals_ < 3,
        shieldCenter, { baseScale_.x * 2.8f * shieldPulse,
            baseScale_.y * 2.8f * shieldPulse, baseScale_.z * 2.8f * shieldPulse },
        { 0.42f, 0.08f, 0.80f, shieldAlpha });
    UpdateFinaleLayer(crownShieldRing_.get(), deltaTime, brokenSeals_ < 3,
        FindGroundPoint(GetTranslate()) + Vector3{ 0.0f, 0.16f, 0.0f },
        { baseScale_.x * 4.2f * shieldPulse, 1.0f, baseScale_.z * 4.2f * shieldPulse },
        { 0.95f, 0.25f, 1.0f, 0.65f });
    if (brokenSeals_ >= 3 && sealStateTimer_ > 1.1f) {
        auto* scene = dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene());
        if (scene && scene->StartCrownFinale(this)) {
            crownInterlude_ = true;
            ClearTransientVisuals();
            HideAttackTelegraph();
        }
        return;
    }
    for (int i = 0; i < 3; ++i) {
        auto& seal = crownSeals_[i];
        if (seal.broken) seal.breakAge += deltaTime;
        const bool active = i == brokenSeals_;
        const float pulse = 1.0f + 0.06f * std::sin(idleTimer_ * 4.0f);
        const float remaining = seal.broken ? std::max(0.0f, 1.0f - seal.breakAge / 0.6f) : 1.0f;
        if (seal.crystal) {
            seal.crystal->SetIsVisible(remaining > 0.0f);
            seal.crystal->SetTranslate(seal.position + Vector3{ 0.0f, 2.0f + 0.2f * std::sin(idleTimer_ * 2.0f), 0.0f });
            seal.crystal->SetScale({ 0.9f * pulse * remaining, pulse * remaining, 0.9f * pulse * remaining });
            seal.crystal->SetRotation({ 0.0f, idleTimer_, 0.0f });
            seal.crystal->SetColor(active ? Vector4{ 1.0f, 0.82f, 0.3f, 1.0f } : Vector4{ 0.26f, 0.35f, 0.52f, 1.0f });
            seal.crystal->SetEmissive(active ? 2.0f : 0.65f);
            seal.crystal->Update(deltaTime);
        }
        if (seal.ring) {
            seal.ring->SetTranslate(seal.position + Vector3{ 0.0f, 0.1f, 0.0f });
            const float ringRadius = CrownFinaleTuning::Get().sealHitRadius / 1.29f;
            seal.ring->SetScale({ ringRadius * pulse, 0.35f, ringRadius * pulse });
            seal.ring->SetEmissive(seal.broken ? 0.0f : (active ? 1.8f : 0.5f));
            seal.ring->SetColor(seal.broken ? Vector4{0.2f,0.2f,0.25f,1} : Vector4{1,1,1,1});
            seal.ring->Update(deltaTime);
        }
        const Vector3 crown = GetTranslate() + Vector3{(i - 1) * 1.1f, baseScale_.y * 2.0f, 0};
        const Vector3 source = seal.position + Vector3{0,2,0};
        const Vector3 line = crown - source;
        const float length = Math::Length(line);
        if (seal.link) {
            seal.link->SetIsVisible(!seal.broken);
            seal.link->SetTranslate((source + crown) * 0.5f);
            seal.link->SetScale({active ? 0.075f : 0.035f, length, active ? 0.075f : 0.035f});
            seal.link->SetRotation({std::atan2(std::sqrt(line.x*line.x + line.z*line.z), line.y), std::atan2(line.x,line.z),0});
            seal.link->Update(deltaTime);
        }
        if (seal.gem) {
            seal.gem->SetIsVisible(!seal.broken);
            seal.gem->SetTranslate(crown);
            seal.gem->SetScale({0.28f,0.35f,0.28f});
            seal.gem->SetRotation({0,idleTimer_,0});
            seal.gem->Update(deltaTime);
        }
    }
    // 破壊反応中は次の攻撃を始めず、残存弾の更新後に安全に片付けます。
    if (sealState_ == SealState::Broken) {
        attackState_ = AttackState::Recovery;
        // 槍の直撃で封印が割れても、槍は残りのディゾルブを最後まで再生します。
        if (currentAttack_ == AttackKind::NightfallLances && nightfallLances_.activeAge > 0.0f &&
            nightfallLances_.activeAge < 2.25f) {
            UpdateNightfallLances(deltaTime, true);
            UpdateSealNightfallSatellites(deltaTime, true);
        }
        if (sealStateTimer_ < 1.4f || brokenSeals_ >= 3) return;
        ClearTransientVisuals();
        sealState_ = SealState::Reveal;
        sealStateTimer_ = 0.0f;
    }
    if (!target_) return;
    const float aim = CrownFinaleTuning::Get().sealAimTime;
    const auto& tuning = CrownFinaleTuning::Get();
    const float lockTime = tuning.sealDodgeTime;
    if (sealState_ == SealState::Reveal || sealState_ == SealState::Recover) {
        attackState_ = AttackState::Idle;
        if (rushWingsVisual_) rushWingsVisual_->SetIsVisible(false);
        if (sealStateTimer_ < tuning.sealRecoveryTime) return;
        sealAttackStage_ = brokenSeals_;
        // 未攻略の封印でも毎回同じ一撃に固定しません。黒晶墜槍を軸に体当たり系を交互に挟みます。
        currentAttack_ = sealAttackStage_ == 0 || sealAttackSerial_ % 2 == 0
            ? AttackKind::NightfallLances
            : (sealAttackStage_ == 1 ? AttackKind::RoyalShockwave : AttackKind::KingRush);
        ++sealAttackSerial_;
        attackStateDuration_ = aim + lockTime;
        if (currentAttack_ == AttackKind::NightfallLances) {
            PrepareNightfallLances();
            PrepareSealNightfallSatellites();
        }
        if (currentAttack_ == AttackKind::KingRush && !rushWingsVisual_)
            rushWingsVisual_ = CreateRoyalMesh("SealRushWings", "Effects/false_king_rush_wings", {1,1,1,1},1.3f);
        sealState_ = SealState::Aim;
        sealStateTimer_ = 0;
    }
    if (sealState_ == SealState::Aim || sealState_ == SealState::Locked) {
        attackState_ = AttackState::Windup;
        attackTimer_ = sealState_ == SealState::Aim ? aim - sealStateTimer_ + lockTime : lockTime - sealStateTimer_;
        if (sealState_ == SealState::Aim)
            sealLockedTarget_ = FindGroundPoint(target_->GetWorldPosition());
        UpdateFacing(NormalizePlanar(sealLockedTarget_ - GetWorldPosition()));
        if (currentAttack_ == AttackKind::NightfallLances) {
            lockedTargetPosition_ = sealLockedTarget_;
            UpdateNightfallLances(deltaTime, false);
            UpdateSealNightfallSatellites(deltaTime, false);
        } else {
            ShowAttackTelegraphCircle(sealLockedTarget_, 2.8f, std::clamp(1.0f - attackTimer_ / attackStateDuration_,0.0f,1.0f), {1,0.4f,0.12f,0.8f});
        }
        if (currentAttack_ == AttackKind::KingRush) {
            const Vector3 direction = NormalizePlanar(sealLockedTarget_ - GetTranslate());
            ShowAttackTelegraphLine(GetTranslate(),direction,PlanarDistance(GetTranslate(),sealLockedTarget_),3.0f,
                std::clamp(1.0f-attackTimer_/attackStateDuration_,0.0f,1.0f),{1,0.4f,0.12f,0.8f});
        }
        if (sealVfxTimer_ > 0.18f) {
            EmitDirectedPreset(kChargePreset, GetTranslate() + Vector3{0,2,0}, {0,1,0},0.5f);
            sealVfxTimer_ = 0;
        }
    }
    if (sealState_ == SealState::Aim && sealStateTimer_ >= aim) {
        sealLockedTarget_ = FindGroundPoint(target_->GetWorldPosition());
        // 光る封印の近くで狙われたときは封印中心に吸着し、誘導成功を数十cmの誤差で失わせません。
        if (currentAttack_ == AttackKind::NightfallLances && brokenSeals_ < 3 &&
            PlanarDistance(sealLockedTarget_, crownSeals_[brokenSeals_].position) <= tuning.sealTargetAssistRadius) {
            sealLockedTarget_ = crownSeals_[brokenSeals_].position;
        }
        sealState_ = SealState::Locked;
        sealStateTimer_ = 0;
    }
    if (sealState_ == SealState::Locked && sealStateTimer_ >= lockTime) {
        sealState_ = SealState::Attack;
        sealStateTimer_ = 0;
        attackState_ = AttackState::Active;
        attackStartPosition_ = FindGroundPoint(GetTranslate());
        attackEndPosition_ = sealLockedTarget_;
        lockedDirection_ = NormalizePlanar(attackEndPosition_ - attackStartPosition_);
        shockwaveJumpTimer_ = 0;
        shockwaveJumpDuration_ = 1.2f;
        shockwaveJumpHeight_ = tuning.sealJumpHeight;
        shockwaveLanded_ = false;
        rushDamageApplied_ = false;
        HideAttackTelegraph();
    }
    if (sealState_ == SealState::Attack) {
        if (currentAttack_ == AttackKind::NightfallLances) {
            lockedTargetPosition_ = sealLockedTarget_;
            UpdateNightfallLances(deltaTime, true);
            UpdateSealNightfallSatellites(deltaTime, true);
        } else if (currentAttack_ == AttackKind::RoyalShockwave) {
            Vector3 unused{};
            UpdateShockwaveJump(deltaTime, unused);
        } else if (currentAttack_ == AttackKind::KingRush) {
            const Vector3 previous = GetTranslate();
            const float duration = std::max(0.6f, PlanarDistance(attackStartPosition_,attackEndPosition_) / tuning.sealRushSpeed);
            const float t = std::clamp(sealStateTimer_ / duration,0.0f,1.0f);
            SetTranslate(LerpVector3(attackStartPosition_,attackEndPosition_,t));
            // フレーム間の線分で判定し、高速移動でも封印を通り抜けません。
            const Vector3 segment = GetTranslate() - previous;
            const float squared = segment.x*segment.x + segment.z*segment.z;
            const Vector3 toSeal = crownSeals_[2].position - previous;
            const float u = squared > 0.0001f ? std::clamp((toSeal.x*segment.x + toSeal.z*segment.z)/squared,0.0f,1.0f) : 0;
            TryBreakCrownSeal(previous + segment * u, 2);
            const Vector3 toPlayer = target_->GetWorldPosition() - previous;
            const float hitU = squared > 0.0001f ? std::clamp((toPlayer.x*segment.x+toPlayer.z*segment.z)/squared,0.0f,1.0f) : 0;
            const Vector3 hitPosition = previous + segment * hitU;
            if (!rushDamageApplied_ && PlanarDistance(hitPosition,target_->GetWorldPosition()) < 2.8f &&
                std::abs(hitPosition.y-target_->GetWorldPosition().y) < 3.2f) {
                DamageTargetAt(hitPosition, 2.8f, 3.2f, GetAttackDefinition(kKingRushAttackId).damage,
                    DamageType::Explosion, {lockedDirection_.x*8,4,lockedDirection_.z*8});
                rushDamageApplied_ = true;
            }
            if (sealVfxTimer_ > 0.10f) {
                EmitDirectedPreset(kShockwaveDustPreset,GetTranslate(),{-lockedDirection_.x,0.1f,-lockedDirection_.z},0.8f);
                sealVfxTimer_ = 0;
            }
        }
        const float end = currentAttack_ == AttackKind::RoyalShockwave ? 5.8f :
            (currentAttack_ == AttackKind::KingRush ? std::max(0.6f,PlanarDistance(attackStartPosition_,attackEndPosition_)/tuning.sealRushSpeed)+0.8f : 2.5f);
        if (sealState_ == SealState::Attack && sealStateTimer_ > end) {
            sealState_ = SealState::Recover;
            sealStateTimer_ = 0;
            HideAttackTelegraph();
        }
    }
    if (rushWingsVisual_ && currentAttack_ == AttackKind::KingRush) {
        rushWingsVisual_->SetIsVisible(sealState_ == SealState::Aim || sealState_ == SealState::Locked || sealState_ == SealState::Attack);
        const Vector3 direction = NormalizePlanar(sealLockedTarget_ - attackStartPosition_);
        rushWingsVisual_->SetTranslate(GetTranslate()+Vector3{0,0.78f,0});
        rushWingsVisual_->SetRotationY(std::atan2(direction.x,direction.z));
        rushWingsVisual_->SetScale({1.2f,1.0f,1.2f});
        rushWingsVisual_->Update(deltaTime);
    }
}

void EnemyFalseKingSlime::BeginFinalArena(const Vector3& position) {
    crownInterlude_ = false;
    finalArenaStarted_ = true;
    battlePhase_ = pendingBattlePhase_ = 3;
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    attackCooldown_ = 2.5f;
    ClearTransientVisuals();
    crownShieldRing_.reset();
    crownShieldShell_.reset();
    SetTranslate(position);
    SetScale(baseScale_);
    SetVelocity({});
    SetGrounded(true);
    SetCollisionAttribute(authoredCollisionAttribute_);
    SetCollisionMask(authoredCollisionMask_);
    if (param_) param_->hp = GetEncounterMaximumHp() * 0.34f;
    SetColor({ 0.78f, 0.65f, 1.0f, 1.0f });
    SyncCollisionBounds();
    UpdateWorldMatrix(true);
}

void EnemyFalseKingSlime::AbortCrownFinale() {
    crownInterlude_ = false;
    finalArenaStarted_ = false;
    battlePhase_ = pendingBattlePhase_ = 2;
    attackState_ = AttackState::Idle;
    SetTranslate(authoredPosition_);
    SetScale(baseScale_);
    if (param_) param_->hp = GetEncounterMaximumHp() * 0.67f;
    InitializeCrownSeals();
}

void EnemyFalseKingSlime::DrawFinaleEditor() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("王冠最終戦 / 第2形態・脱出", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& t = CrownFinaleTuning::Get();
    ImGui::Text("形態 %d / 封印 %d / 3", battlePhase_, brokenSeals_);
    ImGui::TextWrapped("黒晶墜槍を光る封印へ誘導、または王を攻撃して封印を破ります。3個破壊後に最終戦へ移行します。");
    ImGui::SliderFloat("封印の配置半径", &t.sealRadius, 8.0f, 22.0f);
    ImGui::SliderFloat("封印の命中半径", &t.sealHitRadius, 1.5f, 5.0f);
    ImGui::SliderFloat("狙いを固定するまで", &t.sealAimTime, 0.8f, 3.0f);
    ImGui::SliderFloat("固定後の回避猶予", &t.sealDodgeTime, 0.4f, 2.0f);
    ImGui::SliderFloat("封印攻撃の休止時間", &t.sealRecoveryTime, 1.0f, 4.0f);
    ImGui::SliderFloat("封印中の被ダメージ倍率", &t.sealArmorDamageRate, 0.05f, 1.0f);
    ImGui::SliderFloat("攻撃で封印が割れる蓄積量", &t.sealBreakDamage, 10.0f, 200.0f);
    ImGui::SliderFloat("槍を封印へ吸着する距離", &t.sealTargetAssistRadius, 0.0f, 10.0f);
    ImGui::SliderFloat("追撃の小槍の広がり", &t.sealSatelliteSpread, 3.0f, 10.0f);
    ImGui::SliderFloat("封印ジャンプの高さ", &t.sealJumpHeight, 4.0f, 12.0f);
    ImGui::SliderFloat("封印突進の速度", &t.sealRushSpeed, 8.0f, 24.0f);
    ImGui::TextUnformatted("封印攻略: 黒晶墜槍を誘導 / 王への攻撃を重ねる。第2・第3封印では別技も交互に使用");
    ImGui::SliderFloat("継承・脱出ムービー秒数", &t.movieDuration, 12.0f, 24.0f);
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
    if (ImGui::Button("最終戦設定を保存")) saveResult = t.Save() ? 1 : -1;
    ImGui::SameLine();
    if (ImGui::Button("設定を再読込")) saveResult = t.Load() ? 1 : -1;
    if (saveResult) ImGui::TextUnformatted(saveResult > 0 ? "設定を反映しました" : "設定ファイルを開けませんでした");
    if (SceneManager::GetInstance()->IsPlaying() && !crownInterlude_ && !finalArenaStarted_ && debugPreviewAttackId_.empty()) {
        if (ImGui::Button("第2形態から確認")) TriggerDebugPhaseTwoTransition();
        ImGui::SameLine();
        if (ImGui::Button("王冠継承・脱出を確認")) TriggerDebugCrownInterlude();
    }
#endif
}
