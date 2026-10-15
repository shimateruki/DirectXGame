#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.Internal.h"

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

    // 登場中は衝突を無効にし、四段階の伸縮で着地までの動きを作ります。
    encounterTimer_ += deltaTime;
    idleTimer_ += deltaTime;
    const float progress = std::clamp(encounterTimer_ / GetAppearanceDuration(), 0.0f, 1.0f);
    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({0.0f, 0.0f, 0.0f});
    float horizontal = 1.0f;
    float vertical = 1.0f;
    float heightOffset = 0.0f;
    if (progress < 0.45f) {
        const float rate = SmoothStep01(progress / 0.45f);
        horizontal = engine::Math::Lerp(0.88f, 1.32f, rate);
        vertical = engine::Math::Lerp(1.20f, 0.58f, rate);
        heightOffset = (1.0f - rate) * kAppearanceRise;
    } else if (progress < 0.68f) {
        const float rate = SmoothStep01((progress - 0.45f) / 0.23f);
        horizontal = engine::Math::Lerp(1.32f, 0.86f, rate);
        vertical = engine::Math::Lerp(0.58f, 1.25f, rate);
        heightOffset = std::sin(rate * kPi) * 0.48f;
    } else if (progress < 0.88f) {
        const float rate = SmoothStep01((progress - 0.68f) / 0.20f);
        horizontal = engine::Math::Lerp(0.86f, 1.08f, rate);
        vertical = engine::Math::Lerp(1.25f, 0.90f, rate);
        heightOffset = std::sin(rate * kPi) * 0.16f;
    } else {
        const float rate = SmoothStep01((progress - 0.88f) / 0.12f);
        horizontal = engine::Math::Lerp(1.08f, 1.0f, rate);
        vertical = engine::Math::Lerp(0.90f, 1.0f, rate);
    }

    // 元の配置姿勢を基準に補間し、演出を繰り返しても誤差を蓄積させません。
    SetTranslate({authoredPosition_.x, authoredPosition_.y + heightOffset, authoredPosition_.z});
    SetScale({authoredScale_.x * horizontal, authoredScale_.y * vertical, authoredScale_.z * horizontal});
    SetRotation({
        authoredRotation_.x + std::sin(progress * kPi) * 0.14f,
        authoredRotation_.y + std::sin(progress * kPi) * 0.12f,
        authoredRotation_.z + std::sin(progress * kPi * 2.0f) * 0.055f,
    });
    SetColor({
        authoredColor_.x,
        authoredColor_.y,
        authoredColor_.z,
        std::max(0.01f, SmoothStep01(std::min(progress / 0.30f, 1.0f))),
    });
    SetEmissive(1.0f + std::sin(std::min(progress / 0.72f, 1.0f) * kPi) * 2.2f);
    SyncCollisionBounds();
    if (progress >= 1.0f) {
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
    const bool startActive = param_.has_value() && param_->startActive;
    if (startActive || activationRequested_) {
        BeginAppearance();
    } else {
        ApplyDormantState();
    }
}

// Play中の一時状態を破棄して、Editor上の配置姿勢へ戻します。
void EnemyFalseKingSlime::ResetForEditor() {
    // プレイ中の姿勢・材質・衝突を破棄して編集時の設定へ戻します。
    introPresentationActive_ = false;
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

// 登場演出中は攻撃判定を外し、出現VFXを一度だけ再生します。
void EnemyFalseKingSlime::BeginAppearance() {
    // 登場途中に攻撃が当たらないよう判定を外してから VFX を一度再生します。
    encounterState_ = EncounterState::Appearing;
    encounterTimer_ = 0.0f;
    SetIsVisible(true);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetVelocity({0.0f, 0.0f, 0.0f});
    engine::Vector3 effectPosition = authoredPosition_;
    effectPosition.y += 0.2f;
    VFXSequencer::PlayOneShot(kAppearanceSequence, effectPosition, {2.4f, 2.4f, 2.4f});
}

void EnemyFalseKingSlime::ApplyDormantState() {
    // 入口イベント前はモデルも衝突も隠し、待機中の攻撃演出を消します。
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
    // 登場時の伸縮中も本体用 OBB の基準寸法を設定します。
    engine::Object3d::ColliderConfig collider = GetColliderConfig();
    collider.type = ColliderType::kOBB;
    collider.center = {0.0f, 1.48f, 0.0f};
    collider.size = {2.82f, 1.46f, 2.28f};
    SetColliderConfig(collider);
}

bool EnemyFalseKingSlime::IsEncounterControlled() const {
    // 配置データのモードが入口イベント管理を指定しているか調べます。
    return param_.has_value() && param_->actionMode == kEncounterControlledActionMode;
}

float EnemyFalseKingSlime::GetAppearanceDuration() const {
    // 専用の登場演出中は通常の配置設定より長い尺を使います。
    if (introPresentationActive_)
        return 4.2f;
    return param_.has_value() ? std::clamp(param_->shakeDuration, 0.55f, 3.5f) : kDefaultAppearanceDuration;
}

bool EnemyFalseKingSlime::IsEncounterHudActive() const {
    // 待機中と撃破後は HP 表示を出しません。
    if (!IsEncounterControlled() || isDead) {
        return false;
    }
    return encounterState_ == EncounterState::Appearing || encounterState_ == EncounterState::Active;
}

float EnemyFalseKingSlime::GetEncounterCurrentHp() const {
    // UI へ渡す HP は負数にならないようにします。
    return param_.has_value() ? std::max(0.0f, param_->hp) : 0.0f;
}

float EnemyFalseKingSlime::GetEncounterMaximumHp() const {
    // HP バーの除算でゼロを使わないようにします。
    return param_.has_value() ? std::max(1.0f, param_->maxHp) : 1.0f;
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
    if (!param_.has_value() || isDead || IsDefeatEffectPlaying()) {
        return;
    }
    param_->hp = 0.0f;
    SetVelocity({0.0f, 0.0f, 0.0f});
    ClearTransientVisuals();
    HideAttackTelegraph();
}

bool EnemyFalseKingSlime::CanTriggerDebugPhaseTwoTransition() const {
    // 第一形態の通常戦闘中だけデバッグ移行を受け付けます。
    return IsEncounterControlled() && encounterState_ == EncounterState::Active && !isDead &&
           debugPreviewAttackId_.empty() && !crownInterlude_ && !finalArenaStarted_ && battlePhase_ == 1 &&
           attackState_ != AttackState::PhaseShift;
}

bool EnemyFalseKingSlime::CanTriggerDebugCrownInterlude() const {
    // 王冠幕間の重複開始や最終決戦中の逆戻りを防ぎます。
    return IsEncounterControlled() && encounterState_ == EncounterState::Active && !isDead &&
           debugPreviewAttackId_.empty() && !crownInterlude_ && !finalArenaStarted_ && battlePhase_ <= 2 &&
           attackState_ != AttackState::PhaseShift;
}

bool EnemyFalseKingSlime::CanTriggerDebugFinalDefeat() const {
    // 最終決戦に到達している時だけ撃破プレビューを許可します。
    return IsEncounterControlled() && encounterState_ == EncounterState::Active && !isDead && !crownInterlude_ &&
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
    finalDefeatHeld_=true;finalDefeatCompleted_=false;
    SetVelocity({});SetCollisionAttribute(0);SetCollisionMask(0);
    HideAttackTelegraph();ClearTransientVisuals();
}

void EnemyFalseKingSlime::CompleteFinalDefeat() {
    // 演出終了後に一度だけ死亡を確定し、入口イベントへ撃破を通知します。
    if(finalDefeatCompleted_)return;
    finalDefeatCompleted_=true;isDead=true;SetIsVisible(false);
    // 継承演出もボスを参照するため、Sceneの破棄まで本体は保持します。
    if(auto* scene=SceneManager::GetInstance()->GetCurrentScene();scene && GetTargetID()>0)
        scene->TriggerEvent(GetTargetID());
}

void EnemyFalseKingSlime::CancelFinalDefeat() {
    // 演出の中断時は保留フラグを解除し、通常の戦闘へ戻せます。
    finalDefeatHeld_=false;finalDefeatCompleted_=false;isDead=false;
}
