#include "BossIntroSettings.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.Internal.h"
#include <cmath>
#include <numbers>

using namespace EnemyFalseKingSlimeImplementation;

// Editorでは配置値を復元し、Play中の待機・登場だけ通常AIより先に処理します。
bool EnemyFalseKingSlime::UpdateEncounterState(float deltaTime) {
    // この関数が待機・登場を処理した場合は通常 AI の更新を抑止します。
    if (!IsEncounterControlled()) {
        encounterState_ = EncounterState::Normal;
        initializedForPlay_ = false;
        return false;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();

    if (!sceneManager || !sceneManager->IsPlaying()) {
        ResetForEditor();
        return true;
    }

    if (!initializedForPlay_) {
        InitializeEncounterState();
    }

    if (encounterState_ == EncounterState::Dormant) {
        SetVelocity({0.0f, 0.0f, 0.0f});
        ClearTransientVisuals();
        HideAttackTelegraph();
        return true;
    }

    if (encounterState_ != EncounterState::Appearing) {
        return false;
    }

    // 登場中の姿勢はTimelineが担当し、ボス側は判定と戦闘状態を管理する。
    encounterTimer_ += deltaTime;
    idleTimer_ += deltaTime;
    const float progress = std::clamp(encounterTimer_ / GetAppearanceDuration(), 0.0f, 1.0f);
    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({0.0f, 0.0f, 0.0f});
    // 透明度と発光は描画材質の状態なので、TimelineのTransformと競合しない。
    SetColor({
        authoredColor_.x,
        authoredColor_.y,
        authoredColor_.z,
        std::max(0.01f, SmoothStep01(std::min(progress / 0.30f, 1.0f))),
    });
    SetEmissive(1.0f + std::sin(std::min(progress / 0.72f, 1.0f) * kPi) * 2.2f);
    SyncCollisionBounds();

    if (introPortalEnabled_) { SetColor(authoredColor_); SetEmissive(authoredEmissive_); }
    if (progress >= 1.0f && !introPresentationActive_) {
        FinishAppearance();
    }

    return true;
}

// 配置時の姿勢と衝突設定を退避します。force はEditorへ戻る時の再取得用です。
void EnemyFalseKingSlime::CaptureAuthoredState(bool force) {
    // 実行中に変形した姿勢を保存値へ混ぜず、必要な時だけ再取得します。
    if (authoredStateCaptured_ && !force) {
        return;
    }

    authoredPosition_ = GetTranslate();
    authoredScale_ = GetScale();
    authoredRotation_ = GetRotation();
    authoredColor_ = GetColor();
    authoredCollisionAttribute_ = GetCollisionAttribute();
    authoredCollisionMask_ = GetCollisionMask();
    authoredMaterialType_ = GetMaterialType();
    authoredEmissive_ = GetEmissive();
    if (authoredCollisionAttribute_ == 0)
        authoredCollisionAttribute_ = CollisionCategories::Get("enemy");
    if (authoredCollisionMask_ == 0)
        authoredCollisionMask_ = CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet");
    baseScale_ = authoredScale_;
    hasBaseScale_ = true;
    authoredStateCaptured_ = true;
}

// Play開始時に攻撃履歴と形態を初期化し、配置設定に応じて待機または登場へ進めます。
void EnemyFalseKingSlime::InitializeEncounterState() {
    // 前回の王冠決戦と攻撃履歴を消し、配置設定から初期状態を決めます。
    ResetLocomotion();
    locomotionStepSerial_ = 0;
    ResetCrownFinale();
    initializedForPlay_ = true;
    CaptureAuthoredState(true);
    battlePhase_ = 1;
    pendingBattlePhase_ = 1;
    attackCyclePhase_ = 0;
    attackCycleCount_ = 0;
    attackCycleIndex_ = 0;
    lastAutomaticAttack_ = AttackKind::None;
    automaticAttackSerial_ = 0;
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    attackCooldown_ = 1.05f;
    const bool startActive = GetParameters().has_value() && GetParameters()->startActive;

    if (startActive || activationRequested_) {
        BeginAppearance();
    } else {
        ApplyDormantState();
    }
}

// Play中の一時状態を破棄して、Editor上の配置姿勢へ戻します。
void EnemyFalseKingSlime::ResetForEditor() {
    // プレイ中の姿勢・材質・衝突を破棄して編集時の設定へ戻します。
    ResetLocomotion();
    locomotionStepSerial_ = 0;
    introPresentationActive_ = false;
    EnableIntroPortal(false);
    ClearBossAura();
    ResetCrownFinale();

    if (initializedForPlay_) {
        SetTranslate(authoredPosition_);
        SetScale(authoredScale_);
        SetRotation(authoredRotation_);
        SetColor(authoredColor_);
        SetMaterialType(authoredMaterialType_);
        SetEmissive(authoredEmissive_);
    }

    CaptureAuthoredState(true);
    initializedForPlay_ = false;
    activationRequested_ = false;
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    battlePhase_ = 1;
    pendingBattlePhase_ = 1;
    attackCyclePhase_ = 0;
    attackCycleCount_ = 0;
    attackCycleIndex_ = 0;
    lastAutomaticAttack_ = AttackKind::None;
    automaticAttackSerial_ = 0;
    attackState_ = AttackState::Idle;
    currentAttack_ = AttackKind::None;
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetCollisionAttribute(authoredCollisionAttribute_);
    SetCollisionMask(authoredCollisionMask_);
    SetIsVisible(true);
    ClearTransientVisuals();
    HideAttackTelegraph();
    SyncCollisionBounds();
}

// 登場演出中は攻撃判定を外し、出現VFXを再生します。
void EnemyFalseKingSlime::BeginAppearance() {
    encounterState_ = EncounterState::Appearing;
    encounterTimer_ = 0.0f;
    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({0.0f, 0.0f, 0.0f});
    // Timelineの登場開始時刻でボスが有効化されるため、VFXもここで同期する。
    engine::Vector3 effectPosition = authoredPosition_;
    effectPosition.y += 0.2f;
    if (!introPortalEnabled_)
        VfxSequencer::PlayOneShot(kAppearanceSequence, effectPosition, {2.4f, 2.4f, 2.4f});
}

void EnemyFalseKingSlime::SetIntroPresentationActive(bool active, float duration) {
    introPresentationActive_ = active;
    if (!active) EnableIntroPortal(false);
    if (active) {
        introPresentationDuration_ = (std::max)(0.05f, duration);
    }
}

void EnemyFalseKingSlime::ApplyDormantState() {
    EnableIntroPortal(false);
    ClearBossAura();
    // 入口イベント前はモデルも衝突も隠し、待機中の攻撃演出を消します。
    ResetLocomotion();
    encounterState_ = EncounterState::Dormant;
    encounterTimer_ = 0.0f;
    SetTranslate(authoredPosition_);
    SetScale(authoredScale_);
    SetRotation(authoredRotation_);
    SetColor(authoredColor_);
    SetMaterialType(authoredMaterialType_);
    SetEmissive(authoredEmissive_);
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    ClearTransientVisuals();
    HideAttackTelegraph();
}

// 登場終了後に配置時の衝突設定を戻して戦闘AIを再開します。
void EnemyFalseKingSlime::FinishAppearance() {
    // 演出後に元の判定と姿勢を戻して戦闘 AI を始めます。
    ResetLocomotion();
    encounterState_ = EncounterState::Active;
    encounterTimer_ = 0.0f;
    SetTranslate(authoredPosition_);
    SetScale(authoredScale_);
    SetRotation(authoredRotation_);
    SetColor(authoredColor_);
    SetMaterialType(authoredMaterialType_);
    SetEmissive(authoredEmissive_);
    SetCollisionAttribute(authoredCollisionAttribute_);
    SetCollisionMask(authoredCollisionMask_);
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetIsVisible(true);
    attackCooldown_ = 0.82f;
    SyncCollisionBounds();
}

void EnemyFalseKingSlime::SyncCollisionBounds() {
    // 描画用の伸縮で接地判定が上下しないよう、OBBは配置時のスケールへ固定します。
    if (Collider* collider = GetCollider()) {
        collider->SetScaleOverride(hasBaseScale_ ? baseScale_ : GetScale());
    }

    // 登場時の伸縮中も本体用 Obb の基準寸法を設定します。
    engine::Object3D::ColliderConfig collider = GetColliderConfig();
    collider.type = ColliderType::Obb;
    collider.center = {0.0f, 1.46f, 0.0f};
    collider.size = {2.82f, 1.46f, 2.28f};
    SetColliderConfig(collider);
}

bool EnemyFalseKingSlime::IsEncounterControlled() const {
    // 配置データのモードが入口イベント管理を指定しているか調べます。
    return GetParameters().has_value() && GetParameters()->actionMode == kEncounterControlledActionMode;
}

float EnemyFalseKingSlime::GetAppearanceDuration() const {
    // 専用の登場演出中は通常の配置設定より長い尺を使います。
    if (introPresentationActive_)
        return introPresentationDuration_;
    return GetParameters().has_value() ? std::clamp(GetParameters()->shakeDuration, 0.55f, 3.5f) : kDefaultAppearanceDuration;
}

bool EnemyFalseKingSlime::IsEncounterHudActive() const {
    // 待機中と撃破後は HP 表示を出しません。
    if (!IsEncounterControlled() || IsDead()) {
        return false;
    }

    return encounterState_ == EncounterState::Appearing || encounterState_ == EncounterState::Active;
}

float EnemyFalseKingSlime::GetEncounterCurrentHp() const {
    // UI へ渡す HP は負数にならないようにします。
    return GetParameters().has_value() ? std::max(0.0f, GetParameters()->hp) : 0.0f;
}

float EnemyFalseKingSlime::GetEncounterMaximumHp() const {
    // HP バーの除算でゼロを使わないようにします。
    return GetParameters().has_value() ? std::max(1.0f, GetParameters()->maxHp) : 1.0f;
}

float EnemyFalseKingSlime::GetEncounterAppearanceProgress() const {
    // 登場中だけ実時間の進捗を返し、戦闘開始後は完了扱いにします。
    if (encounterState_ == EncounterState::Active) {
        return 1.0f;
    }

    if (encounterState_ != EncounterState::Appearing) {
        return 0.0f;
    }

    return std::clamp(encounterTimer_ / GetAppearanceDuration(), 0.0f, 1.0f);
}

float EnemyFalseKingSlime::GetPhaseTransitionProgress() const {
    // 形態移行中は経過時間、移行外は形態の一致状態を返します。
    if (attackState_ != AttackState::PhaseShift) {
        return battlePhase_ == pendingBattlePhase_ ? 1.0f : 0.0f;
    }

    return std::clamp(phaseTransitionTimer_ / (std::max)(phaseTransitionDuration_, 0.01f), 0.0f, 1.0f);
}

void EnemyFalseKingSlime::TriggerDebugDefeat() {
    // プレビューの撃破操作では HP をゼロにし、残った攻撃表示を片付けます。
    if (!GetParameters().has_value() || IsDead() || IsDefeatEffectPlaying()) {
        return;
    }

    SetParameter(&engine::EntityParameter::hp, 0.0f);
    SetVelocity({0.0f, 0.0f, 0.0f});
    ClearTransientVisuals();
    HideAttackTelegraph();
}

bool EnemyFalseKingSlime::CanTriggerDebugPhaseTwoTransition() const {
    // 第一形態の通常戦闘中だけデバッグ移行を受け付けます。
    return IsEncounterControlled() && encounterState_ == EncounterState::Active && !IsDead() &&
           debugPreviewAttackId_.empty() && !crownInterlude_ && !finalArenaStarted_ && battlePhase_ == 1 &&
           attackState_ != AttackState::PhaseShift;
}

bool EnemyFalseKingSlime::CanTriggerDebugCrownInterlude() const {
    // 王冠幕間の重複開始や最終決戦中の逆戻りを防ぎます。
    return IsEncounterControlled() && encounterState_ == EncounterState::Active && !IsDead() &&
           debugPreviewAttackId_.empty() && !crownInterlude_ && !finalArenaStarted_ && battlePhase_ <= 2 &&
           attackState_ != AttackState::PhaseShift;
}

bool EnemyFalseKingSlime::CanTriggerDebugFinalDefeat() const {
    // 最終決戦に到達している時だけ撃破プレビューを許可します。
    return IsEncounterControlled() && encounterState_ == EncounterState::Active && !IsDead() && !crownInterlude_ &&
           finalArenaStarted_ && battlePhase_ == 3;
}

void EnemyFalseKingSlime::TriggerDebugPhaseTwoTransition() {
    // 実際の形態変更は更新側へ予約し、ここでは要求だけ記録します。
    if (!CanTriggerDebugPhaseTwoTransition())
        return;
    pendingDebugSeals_ = true;
}

void EnemyFalseKingSlime::TriggerDebugCrownInterlude() {
    // 更新途中で幕間へ飛ばず、次の進行タイミングへ予約します。
    if (!CanTriggerDebugCrownInterlude())
        return;
    pendingDebugInterlude_ = true;
}

void EnemyFalseKingSlime::HoldFinalDefeat() {
    // 勝利演出がボスを参照する間は死亡確定を保留し、攻撃判定だけ止めます。
    finalDefeatHeld_=true;
    finalDefeatCompleted_=false;
    SetVelocity({});
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    HideAttackTelegraph();
    ClearTransientVisuals();
}

void EnemyFalseKingSlime::CompleteFinalDefeat() {
    // 演出終了後に一度だけ死亡を確定し、入口イベントへ撃破を通知します。
    if(finalDefeatCompleted_)
        return;
    finalDefeatCompleted_=true;
    SetDead(true);
    SetIsVisible(false);

    // 継承演出もボスを参照するため、Sceneの破棄まで本体は保持します。
    if(auto* scene=SceneManager::GetInstance()->GetCurrentScene();scene && GetTargetId()>0)
        scene->TriggerEvent(GetTargetId());
}

void EnemyFalseKingSlime::CancelFinalDefeat() {
    // 演出の中断時は保留フラグを解除し、通常の戦闘へ戻せます。
    finalDefeatHeld_=false;
    finalDefeatCompleted_=false;
    SetDead(false);
}

// 登場表示をまとめて所有し、途中終了とEditor停止でも残さないようにします。
void EnemyFalseKingSlime::EnableIntroPortal(bool enabled) {
    introPortalEnabled_ = enabled;
    bossAuraTimer_ = 0.0f;
    introPreviousTime_ = 0.0f;
    if (enabled) ClearBossAura();
    if (!enabled) {
        ClearPortalClip();
        introPortal_.reset();
        introShockwave_.reset();
    }
}

void EnemyFalseKingSlime::ConfigureIntroTiming(float start, float landing, float pose, float closing, float auraEnd) {
    introStartTime_ = start;
    introLandTime_ = (std::max)(start + .7f, landing);
    introPoseTime_ = (std::max)(introLandTime_ + .1f, pose);
    introCloseTime_ = (std::max)(introPoseTime_, closing);
    introAuraEndTime_ = (std::max)(introPoseTime_, auraEnd);
}

// timeは登場Timelineの再生秒、dtは経過秒。有効かつ開始時刻以降のポータル・着地演出を更新します。
void EnemyFalseKingSlime::UpdateIntroPortal(float time, float frameDeltaTime) {
    if (!introPortalEnabled_ || time < introStartTime_) return;
    const auto& style = BossIntroSettings::Get();
    // 最初の全身寸法を保持し、本体の伸縮でゲートまで揺れないようにします。
    if (introWidth_ <= 0.0f) {
        const auto bounds = GetModelWorldAabb();
        introWidth_ = (std::max)(2.0f, bounds.max.x - bounds.min.x);
        introHeight_ = (std::max)(2.0f, bounds.max.y - bounds.min.y);
    }
    const float width = introWidth_, height = introHeight_;
    const engine::Vector3 center = authoredPosition_ + engine::Vector3{0, height * .72f, -4};
    auto ensureVisual = [this](std::unique_ptr<engine::Object3D>& visual) {
        if (visual) return;
        visual = std::make_unique<engine::Object3D>();
        visual->Initialize(GetObjectCommon());
        visual->SetModel("Gimmicks/portal_surface");
        visual->SetColliderType(ColliderType::None);
        visual->SetCollisionAttribute(0);
        visual->SetMaterialType(22);
        visual->SetBlendMode(BlendMode::Normal);
        visual->SetEnableLighting(false);
        visual->SetEmissive(1.0f);
        visual->SetCameraOcclusionMode(engine::Object3D::CameraOcclusionMode::KeepVisible);
    };
    ensureVisual(introPortal_);
    ensureVisual(introShockwave_);

    const float opening = SmoothStep01((time - introStartTime_) / .65f);
    const float closing = SmoothStep01((time - introCloseTime_) / style.closeDuration);
    introPortal_->SetIsVisible(closing < 1.0f && opening > .001f);
    introPortal_->SetTranslate(center);
    introPortal_->SetScale({width * .80f * style.gateScale * opening, height * .98f * style.gateScale, 1});
    introPortal_->SetColor({style.gateColor[0], style.gateColor[1], style.gateColor[2], .65f});
    if (auto* v = introPortal_->GetMeshRenderer()->GetWaterParamData()) {
        v->effectType = -2; v->waveSpeed = 1.1f; v->waveFrequency = 16; v->waveHeight = .85f;
        v->effectIntensity = 1.4f; v->effectSoftness = .6f; v->effectScale = 1.12f;
        v->effectScaleX = v->effectScaleY = 1; v->effectScaleZ = .12f;
        v->uvOffsetX = opening; v->uvOffsetY = 1; v->flowSpeedX = closing; v->billboardScale = 1;
    }
    if (time < introLandTime_ + .1f) {
        engine::Object3D::PortalClipSettings clip;
        clip.enabled = true; clip.center = center; clip.normal = {0, 0, -1};
        clip.progress = .47f; clip.edgeWidth = .14f; clip.dissolve = .08f;
        clip.color = {.65f, .42f, 1.7f, 1};
        SetPortalClip(clip);
    } else ClearPortalClip();

    // 登場の強さをゼロへ落とさず、0.8秒で通常戦闘の濃さへ引き継ぎます。
    const float handoff = SmoothStep01((time - introAuraEndTime_) / .8f);
    const float aura = SmoothStep01((time - introStartTime_ - .9f) / .65f) *
        std::lerp(1.0f, style.idleAuraStrength, handoff);
    const float poseBurst = std::exp(-std::pow((time - introPoseTime_) / .25f, 2.0f));
    bossAuraStrength_ = aura;
    UpdateAuraVisual(aura, poseBurst, frameDeltaTime);

    // 着地時だけ床を走る輪を広げます。縮小して消すのではなく、広がりながら薄くします。
    const float impact = std::clamp((time - introLandTime_) / .8f, 0.0f, 1.0f);
    const float radius = width * (.28f + 1.25f * (1 - (1-impact)*(1-impact))) * style.impactScale;
    introShockwave_->SetIsVisible(time >= introLandTime_ && impact < 1);
    if (introShockwave_->GetIsVisible())
        introShockwave_->SetTranslate(FindGroundPoint(authoredPosition_) + engine::Vector3{0, .08f, 0});
    // 標準のπ/2から0方向へ1段階寄せ、従来の衝撃波の回転値を維持します。
    const float shockwaveRotationX = std::nextafter(std::numbers::pi_v<float> / 2.0f, 0.0f);
    introShockwave_->SetRotation({shockwaveRotationX, 0, 0});
    introShockwave_->SetScale({radius, radius, 1});
    introShockwave_->SetColor({.58f, .36f, 1.0f, (1-impact)*(1-impact) * .9f});
    if (auto* v = introShockwave_->GetMeshRenderer()->GetWaterParamData()) {
        v->effectType = -4; v->billboardScale = 1; v->effectScaleX = v->effectScaleY = v->effectScaleZ = 1;
    }
    auto* particles = GpuParticleManager::GetInstance();
    auto emit = [&](const char* name, const engine::Vector3& position, const engine::Vector3& velocity,
                    float size, int count, const engine::Vector3& area) {
        if (!particles || !particles->IsInitialized()) return;
        const auto found = particles->GetPresets().find(name);
        if (found == particles->GetPresets().end()) return;
        auto config = found->second;
        config.emitPos = position; config.emitVelocity = velocity; config.emitArea = area;
        config.baseSize *= size; config.midSize *= size; config.endSize *= size;
        config.emitCount = count; config.isLooping = false;
        particles->EmitFromConfig(config);
    };
    // 閾値を跨いだフレームに一度だけ発生させ、低FPSでも連続噴射しません。
    if (introPreviousTime_ < introLandTime_ && time >= introLandTime_) {
        constexpr float kFullTurn = 2.0f * std::numbers::pi_v<float>; // 1周分の角度（ラジアン）。
        for (int i = 0; i < 12; ++i) {
            const float angle = i * kFullTurn / 12;
            const engine::Vector3 direction{std::cos(angle), 0, std::sin(angle)};
            emit("false_king_intro_gold", FindGroundPoint(authoredPosition_) + direction * width * .3f + engine::Vector3{0,.12f,0},
                 direction * 5.0f + engine::Vector3{0, 1.5f, 0}, style.impactScale, 4, {.1f,.1f,.1f});
        }
    }
    if (introPreviousTime_ < introPoseTime_ && time >= introPoseTime_)
        emit("false_king_intro_gold", GetWorldPosition() + engine::Vector3{0,height*.52f,0},
             {0,4,0}, 1.35f * style.auraScale, 32, {width*.35f,height*.2f,.4f});
    // 消失の後半には縁から中心へ流れる粒子を残します。
    if (time > introCloseTime_ && closing < .98f && introPreviousTime_ < time &&
        int(time * 12) != int(introPreviousTime_ * 12)) {
        for (int i = 0; i < 5; ++i) {
            const float angle = time * 1.7f + i * 1.256637f;
            const engine::Vector3 offset{std::cos(angle)*width*.48f, std::sin(angle)*height*.48f, .12f};
            emit("false_king_intro_gold", center + offset, offset * -1.4f,
                 1-closing*.65f, 2, {.08f,.08f,.08f});
        }
    }
    introPreviousTime_ = time;
    for (auto* visual : {introPortal_.get(), introShockwave_.get()}) {
        visual->Update(frameDeltaTime);
        visual->UpdateWorldMatrix(true);
    }
}

// ゲートとは独立した戦闘表示。撃破・待機・Editor停止では状態ごと解放します。
void EnemyFalseKingSlime::ClearBossAura() {
    bossAura_.reset();
    bossAuraStrength_ = bossAuraTime_ = bossAuraTimer_ = 0.0f;
    introWidth_ = introHeight_ = 0.0f;
}

void EnemyFalseKingSlime::UpdateBossAura(float frameDeltaTime) {
    const auto* manager = SceneManager::GetInstance();
    if (!manager || !manager->IsPlaying() || IsDead() || !GetIsVisible() ||
        finalDefeatHeld_ || ShouldHandleDefeatEffect()) {
        ClearBossAura();
        return;
    }
    // 登場中の更新はTimelineが担当し、同じフレームに二重更新しません。
    if (introPresentationActive_) return;
    if (IsDormant() || (IsEncounterControlled() && encounterState_ == EncounterState::Dormant)) {
        ClearBossAura();
        return;
    }
    const auto& style = BossIntroSettings::Get();
    const bool sealAttacking = battlePhase_ == 2 &&
        (sealState_ == SealState::Chant || sealState_ == SealState::Attack);
    const bool attacking = attackState_ == AttackState::Windup || attackState_ == AttackState::Active || sealAttacking;
    const float target = crownInterlude_ ? 0.0f :
        attackState_ == AttackState::PhaseShift ? (std::max)(1.0f, style.attackAuraStrength) :
        attacking ? style.attackAuraStrength : style.idleAuraStrength;
    bossAuraStrength_ += (target - bossAuraStrength_) * (1.0f - std::exp(-5.0f * frameDeltaTime));
    if (crownInterlude_ && bossAuraStrength_ < .002f) {
        ClearBossAura();
        return;
    }
    UpdateAuraVisual(bossAuraStrength_, 0.0f, frameDeltaTime);
}

void EnemyFalseKingSlime::AlignBossAura(Camera* camera) {
    if (!bossAura_ || !camera) return;
    const auto position = GetWorldPosition();
    auto forward = camera->GetEye() - position;
    forward.y = 0;
    const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    forward = length > .001f ? forward / length : engine::Vector3{0,0,1};
    bossAura_->SetRotation({0, std::atan2(forward.x, forward.z), 0});
    // カメラから見て体の奥へ置き、顔を覆わず左右の輪郭へ流れを残します。
    bossAura_->SetTranslate(position + engine::Vector3{0,introHeight_*.34f,0} - forward*.25f);
    bossAura_->UpdateLocalMatrix();
    bossAura_->UpdateWorldMatrix(true);
}

void EnemyFalseKingSlime::UpdateAuraVisual(float strength, float poseBurst, float frameDeltaTime) {
    const auto& style = BossIntroSettings::Get();
    if (introWidth_ <= 0) {
        const auto bounds = GetModelWorldAabb();
        introWidth_ = (std::max)(2.0f,bounds.max.x-bounds.min.x);
        introHeight_ = (std::max)(2.0f,bounds.max.y-bounds.min.y);
    }
    if (!bossAura_) {
        bossAura_ = std::make_unique<engine::Object3D>();
        bossAura_->Initialize(GetObjectCommon());
        bossAura_->SetModel("Gimmicks/portal_surface");
        bossAura_->SetColliderType(ColliderType::None);
        bossAura_->SetCollisionAttribute(0);
        bossAura_->SetMaterialType(22);
        bossAura_->SetBlendMode(BlendMode::Normal);
        bossAura_->SetEnableLighting(false);
        bossAura_->SetEmissive(1);
        bossAura_->SetCameraOcclusionMode(engine::Object3D::CameraOcclusionMode::KeepVisible);
        auto* v = bossAura_->GetMeshRenderer()->GetWaterParamData();
        v->effectType=-3;v->billboardScale=1;v->effectScaleX=v->effectScaleY=v->effectScaleZ=1;
    }
    bossAuraTime_ += frameDeltaTime;
    const float breath = introPresentationActive_ ? 1.0f : .96f + .04f*std::sin(bossAuraTime_*2);
    bossAura_->SetIsVisible(strength*style.auraScale*style.auraOpacity > .001f);
    bossAura_->SetScale({introWidth_*(.87f+poseBurst*.10f)*style.auraScale,
                        introHeight_*.72f*style.auraScale,1});
    bossAura_->SetColor({.35f,.13f,.92f,strength*style.auraOpacity*(.85f+.25f*poseBurst)*breath});
    bossAura_->Update(frameDeltaTime);
    auto* camera=CameraManager::GetInstance()->GetMainCamera();
    AlignBossAura(camera);
    bossAuraTimer_ -= frameDeltaTime;
    if (!bossAura_->GetIsVisible() || strength < .01f || frameDeltaTime <= 0 || bossAuraTimer_ > 0) return;
    // 通常戦闘では発生間隔と個数も抑え、攻撃予兆と視界を確保します。
    bossAuraTimer_ = introPresentationActive_ ? .07f : .14f;
    auto forward = camera ? camera->GetEye()-GetWorldPosition() : engine::Vector3{0,0,1};
    forward.y=0;
    const float length=std::sqrt(forward.x*forward.x+forward.z*forward.z);
    forward=length>.001f?forward/length:engine::Vector3{0,0,1};
    const engine::Vector3 right{forward.z,0,-forward.x};
    auto* particles=GpuParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) return;
    for (float side:{-1.0f,1.0f}) {
        const auto pos=GetWorldPosition()+right*(side*introWidth_*(.43f+.04f*std::sin(bossAuraTime_*2.6f+side)))+
            engine::Vector3{0,introHeight_*.18f,0}+forward*.12f;
        for (const char* name:{"false_king_intro_wisp","false_king_intro_gold"}) {
            const auto found=particles->GetPresets().find(name);
            if(found==particles->GetPresets().end())continue;
            auto config=found->second;
            config.emitPos=pos;config.emitVelocity=right*(-side*.6f)+engine::Vector3{0,3.5f,0};
            config.emitArea={introWidth_*.08f,introHeight_*.08f,.12f};
            const float size=strength*style.auraScale;
            config.baseSize*=size;config.midSize*=size;config.endSize*=size;
            config.baseColor.w*=style.auraOpacity;config.midColor.w*=style.auraOpacity;config.endColor.w*=style.auraOpacity;
            config.emitCount=introPresentationActive_?3:1;config.isLooping=false;
            particles->EmitFromConfig(config);
        }
    }
}
