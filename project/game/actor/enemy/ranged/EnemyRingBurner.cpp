#define NOMINMAX
#include "EnemyRingBurner.h"
#include "game/system/SceneGameplayAccess.h"

#include "CollisionConfig.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "HitEffectDirector.h"
#include "MeshEffectManager.h"
#include "Player.h"
#include "SceneManager.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kModelHeight = 1.46f;
constexpr float kTopRadius = 1.05f;
constexpr float kWaveStartRadiusRatio = 1.60f / 11.0f;
// 波の寿命は effect_ring_burner_wave / wave_heat の Lifetime と揃えます。
constexpr float kWaveDuration = 3.6f;
constexpr float kRecoveryDuration = 0.62f;
constexpr float kSuppressedReleaseDuration = 4.0f;
constexpr float kWaveBandHalfWidth = 0.72f;
constexpr float kWaveJumpClearHeight = 1.52f;
constexpr float kWaveDamage = 1.0f;
constexpr const char* kChargeEffect = "Resources/json/effect/effect_ring_burner_charge.json";
constexpr const char* kWaveEffect = "Resources/json/effect/effect_ring_burner_wave.json";
constexpr const char* kWaveHeatEffect = "Resources/json/effect/effect_ring_burner_wave_heat.json";
constexpr const char* kChargeParticle = "ring_burner_charge_sparks";
constexpr const char* kFireParticle = "ring_burner_discharge_embers";

// 熱輪演出の終点へ向けて強く減速する補間率を返します。
float EaseOutQuint(float value) {
    // 発射前の予兆が終盤で急に跳ねないよう、半径の変化を滑らかに収束させる。
    const float t = (std::clamp)(value, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse * inverse * inverse;
}

float PlanarDistance(const engine::Vector3& first, const engine::Vector3& second) {
    // 床を伝う輪の判定では高さを距離から除き、高さは別条件で調べる。
    const float x = first.x - second.x;
    const float z = first.z - second.z;
    return std::sqrt(x * x + z * z);
}

engine::Vector3 NormalizePlanar(const engine::Vector3& value) {
    // 完全に重なった対象にも一定方向の押し出しを与える。
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return {value.x / length, 0.0f, value.z / length};
}
} // namespace

// 状態ごとの進行と姿勢を同じ実装へ集めます。State の数値は Replay JSON と互換に保ちます。
class EnemyRingBurner::StateBehavior {
  public:
    virtual ~StateBehavior() = default;
    virtual void Enter(EnemyRingBurner&) const {
        // 入場処理がない状態では何も変更しない。
    }
    virtual void Update(EnemyRingBurner& owner, float deltaTime, float targetDistance, float detectRange) const = 0;
    virtual void ApplyVisualPose(const EnemyRingBurner& owner, engine::Vector3& scale, engine::Vector4& color,
                                 float& emissive) const = 0;
    virtual void Exit(EnemyRingBurner&) const {
        // 退出処理が必要な状態だけで演出を回収する。
    }
};

/// <summary>待機中の呼吸演出を行い、対象が射程に入ると充填状態へ移る。</summary>
class EnemyRingBurner::IdleBehavior final : public StateBehavior {
  public:
    void Update(EnemyRingBurner& owner, float deltaTime, float targetDistance, float detectRange) const override {
        // 待機中だけ再使用待ちを減らし、対象が射程に入ったら詠唱へ進む。
        owner.cooldownTimer_ = (std::max)(0.0f, owner.cooldownTimer_ - deltaTime);
        owner.HideAttackTelegraph();
        if (owner.target_ && targetDistance <= detectRange && owner.cooldownTimer_ <= 0.0f) {
            owner.EnterState(State::Charge, owner.GetChargeDuration());
        }
    }

    void ApplyVisualPose(const EnemyRingBurner& owner, engine::Vector3& scale, engine::Vector4&, float& emissive) const override {
        // 待機中は元の大きさを基準に弱い呼吸と発光だけを与える。
        const float breathe = std::sin(owner.idleTimer_ * 2.8f) * 0.025f;
        scale.x *= 1.0f + breathe;
        scale.y *= 1.0f - breathe * 0.45f;
        scale.z *= 1.0f + breathe;
        emissive = 1.35f + (breathe + 0.025f) * 8.0f;
    }
};

/// <summary>床の予兆と充填演出を進め、充填完了時に攻撃の輪を放つ。</summary>
class EnemyRingBurner::ChargeBehavior final : public StateBehavior {
  public:
    void Enter(EnemyRingBurner& owner) const override {
        // 前回の輪の半径を消し、予兆と充填演出を同じ開始時刻に揃える。
        owner.waveRadius_ = 0.0f;
        owner.SpawnChargeEffects();
        owner.TriggerAttackTelegraphCue({1.0f, 0.20f, 0.025f, 1.0f});
    }

    void Update(EnemyRingBurner& owner, float deltaTime, float, float) const override {
        // 充填の進行率で予兆半径を広げ、期限が来た時だけ輪を放出する。
        owner.stateTimer_ -= deltaTime;
        const float progress = 1.0f - (std::max)(0.0f, owner.stateTimer_) / owner.stateDuration_;
        const float pulseRadius = engine::Math::Lerp(1.05f, 1.72f, EaseOutQuint(progress));
        owner.ShowAttackTelegraphCircle(owner.GetWorldPosition() + engine::Vector3{0.0f, 0.06f, 0.0f},
                                        pulseRadius * (std::max)(owner.baseScale_.x, owner.baseScale_.z), progress,
                                        {1.0f, 0.20f, 0.025f, 0.92f});
        if (owner.particleTimer_ <= 0.0f) {
            if (auto* particles = GPUParticleManager::GetInstance(); particles->IsInitialized()) {
                particles->Emit(kChargeParticle, owner.GetWorldPosition() +
                                                 engine::Vector3{0.0f, 1.12f * owner.baseScale_.y, 0.0f});
            }
            owner.particleTimer_ = 0.12f;
        }
        if (owner.stateTimer_ <= 0.0f) {
            owner.EnterState(State::Wave, kWaveDuration);
        }
    }

    void ApplyVisualPose(const EnemyRingBurner& owner, engine::Vector3& scale, engine::Vector4& color,
                         float& emissive) const override {
        // 発射前に押し潰しと発光を強め、床の予兆と本体の状態を一致させる。
        const float progress = 1.0f - (std::max)(0.0f, owner.stateTimer_) / owner.stateDuration_;
        const float pulse = std::sin(progress * kPi * 10.0f) * (0.03f + progress * 0.07f);
        scale.x *= 1.0f + pulse;
        scale.y *= 0.94f - progress * 0.10f;
        scale.z *= 1.0f + pulse;
        color = {1.0f, 0.54f + progress * 0.32f, 0.30f, 1.0f};
        emissive = 2.2f + progress * 4.8f;
    }

    void Exit(EnemyRingBurner& owner) const override {
        // 放出時に詠唱用の床予兆を残さない。
        owner.HideAttackTelegraph();
    }
};

/// <summary>広がる攻撃の輪と当たり判定を同期し、終了後に硬直へ移る。</summary>
class EnemyRingBurner::WaveBehavior final : public StateBehavior {
  public:
    void Enter(EnemyRingBurner& owner) const override {
        // 輪の初期半径を表示アセットの開始半径に合わせ、初回フレームから判定を同期する。
        owner.waveDamageDone_ = false;
        owner.waveRadius_ = owner.GetWaveMaximumRadius() * kWaveStartRadiusRatio;
        owner.SpawnWaveEffects();
    }

    void Update(EnemyRingBurner& owner, float deltaTime, float, float) const override {
        owner.stateTimer_ -= deltaTime;
        const float progress = (std::clamp)(1.0f - owner.stateTimer_ / owner.stateDuration_, 0.0f, 1.0f);
        const float maximumRadius = owner.GetWaveMaximumRadius();
        const float previousRadius = owner.waveRadius_;
        // 発射直後に最大半径へ飛ばず、見た目と同じ等速で一周の輪を広げます。
        owner.waveRadius_ = engine::Math::Lerp(maximumRadius * kWaveStartRadiusRatio, maximumRadius, progress);
        owner.UpdateWaveDamage(previousRadius);
        if (owner.stateTimer_ <= 0.0f) {
            owner.EnterState(State::Recovery, kRecoveryDuration);
        }
    }

    void ApplyVisualPose(const EnemyRingBurner& owner, engine::Vector3& scale, engine::Vector4& color,
                         float& emissive) const override {
        // 放出直後だけ反動を大きくし、輪の拡散とともに元の姿勢へ戻す。
        const float progress = 1.0f - (std::max)(0.0f, owner.stateTimer_) / owner.stateDuration_;
        const float recoil = std::exp(-progress * 8.0f);
        scale.x *= 1.0f + recoil * 0.13f;
        scale.y *= 0.84f + (1.0f - recoil) * 0.16f;
        scale.z *= 1.0f + recoil * 0.13f;
        color = {1.0f, 0.48f, 0.22f, 1.0f};
        emissive = 4.2f - progress * 2.2f;
    }

    void Exit(EnemyRingBurner& owner) const override {
        // 判定終了と同時に輪の表示スコープを回収する。
        owner.StopWaveEffects();
    }
};

/// <summary>放出後の硬直と姿勢を維持し、終了時に待機へ戻す。</summary>
class EnemyRingBurner::RecoveryBehavior final : public StateBehavior {
  public:
    void Update(EnemyRingBurner& owner, float deltaTime, float, float) const override {
        // 硬直後に待機へ戻し、その時点から次の充填までの間隔を数える。
        owner.stateTimer_ -= deltaTime;
        if (owner.stateTimer_ <= 0.0f) {
            owner.EnterState(State::Idle, 0.01f);
            owner.cooldownTimer_ = owner.GetCooldownDuration();
        }
    }

    void ApplyVisualPose(const EnemyRingBurner&, engine::Vector3& scale, engine::Vector4&, float& emissive) const override {
        // 放出後の低い姿勢と弱い光を保ち、待機状態との差を出す。
        scale.y *= 0.94f;
        emissive = 1.05f;
    }
};

/// <summary>踏まれている間の攻撃と演出を止め、抑制が解けると硬直へ移る。</summary>
class EnemyRingBurner::SuppressedBehavior final : public StateBehavior {
  public:
    void Enter(EnemyRingBurner& owner) const override {
        // 上に乗られた間は攻撃を止め、予兆と輪の描画も消す。
        owner.HideAttackTelegraph();
        owner.StopWaveEffects();
    }

    void Update(EnemyRingBurner& owner, float deltaTime, float, float) const override {
        // 踏まれなくなって一定時間が経つと、硬直を経て再起動する。
        owner.stateTimer_ -= deltaTime;
        if (owner.stateTimer_ <= 0.0f) {
            owner.EnterState(State::Recovery, kRecoveryDuration);
        }
    }

    void ApplyVisualPose(const EnemyRingBurner&, engine::Vector3& scale, engine::Vector4& color,
                         float& emissive) const override {
        // 停止状態は低い姿勢と寒色で攻撃不能と分かるようにする。
        scale.x *= 1.05f;
        scale.y *= 0.82f;
        scale.z *= 1.05f;
        color = {0.38f, 0.92f, 1.0f, 1.0f};
        emissive = 0.72f;
    }
};

const EnemyRingBurner::StateBehavior& EnemyRingBurner::BehaviorFor(State state) {
    // Replayへ保存したStateの番号と配列順を合わせ、各状態の処理を選ぶ。
    static const IdleBehavior idle;
    static const ChargeBehavior charge;
    static const WaveBehavior wave;
    static const RecoveryBehavior recovery;
    static const SuppressedBehavior suppressed;
    static const std::array<const StateBehavior*, 5> behaviors = {
        &idle, &charge, &wave, &recovery, &suppressed};
    return *behaviors[static_cast<std::size_t>(state)];
}

void EnemyRingBurner::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_RingBurner");
    SetEnemyType("RingBurner");
    SetStatic(false);
    SetMaterialType(0);
    SetMetallic(0.72f);
    SetRoughness(0.24f);
    SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    SetEmissive(1.35f);
    defaultColor_ = GetColor();

    // groundを併記し、上面だけは安全な足場として押し戻せるようにします。
    SetCollisionAttribute(CollisionCategories::Get("enemy") | CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
    SetColliderType(ColliderType::kOBB);
    SetCollisionSize({1.25f, kModelHeight * 0.5f, 1.25f});
    ColliderConfig collider = GetColliderConfig();
    collider.center = {0.0f, kModelHeight * 0.5f, 0.0f};
    SetColliderConfig(collider);

    baseScale_ = GetScale();
    hasBaseScale_ = true;
    cooldownTimer_ = 0.18f;
}

void EnemyRingBurner::Update(float deltaTime) {
    // 上面にプレイヤーがいる間は攻撃状態より抑止状態を優先する。
    ResolveRuntimeTarget();
    if (UpdateInactiveState(deltaTime)) {
        return;
    }

    EnsureBaseScale();
    idleTimer_ += deltaTime;
    particleTimer_ = (std::max)(0.0f, particleTimer_ - deltaTime);
    SetVelocity({0.0f, 0.0f, 0.0f});

    if (IsTargetStandingOnTop()) {
        if (state_ != State::Suppressed) {
            EnterState(State::Suppressed, kSuppressedReleaseDuration);
        }
        stateTimer_ = kSuppressedReleaseDuration;
        cooldownTimer_ = GetCooldownDuration() * 0.38f;
    } else {
        UpdateCombat(deltaTime);
    }

    UpdateVisualPose(deltaTime);
    BaseEnemy::Update(deltaTime);
}

void EnemyRingBurner::ResolveRuntimeTarget() {
    // 編集・再生で対象が未接続でも、現在シーンのプレイヤーを後から結び付ける。
    if (target_) {
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (scene) {
        target_ = GetScenePlayer(scene);
    }
}

bool EnemyRingBurner::UpdateInactiveState(float deltaTime) {
    // 休眠や撃破中は固有AIを動かさず、共通更新だけで退場を進める。
    if (IsDormant() || ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        StopWaveEffects();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    return false;
}

void EnemyRingBurner::EnsureBaseScale() {
    // 外部からスケールを指定された個体では、その値を以後の姿勢補間の基準にする。
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyRingBurner::EnterState(State state, float duration) {
    // 旧状態の演出を片付けてから新状態を開始し、秒数0での進行率除算を防ぐ。
    BehaviorFor(state_).Exit(*this);
    state_ = state;
    stateDuration_ = (std::max)(duration, 0.001f);
    stateTimer_ = stateDuration_;
    if (state != State::Wave) {
        waveDamageDone_ = false;
    }
    BehaviorFor(state_).Enter(*this);
}

void EnemyRingBurner::UpdateCombat(float deltaTime) {
    // JSONの検知距離を安全範囲へ収めてから、現在状態の振る舞いへ渡す。
    const float targetDistance = GetPlanarTargetDistance();
    const float configuredRange = param_.has_value() ? param_->detectionRange : detectionRange_;
    const float detectRange = (std::clamp)(configuredRange, 8.0f, 80.0f);
    BehaviorFor(state_).Update(*this, deltaTime, targetDistance, detectRange);
}

void EnemyRingBurner::UpdateWaveDamage(float previousRadius) {
    if (waveDamageDone_ || !target_) {
        return;
    }

    const engine::Vector3 origin = GetWorldPosition();
    const engine::Vector3 targetPosition = target_->GetWorldPosition();
    const float targetDistance = PlanarDistance(origin, targetPosition);
    const float targetRelativeHeight = targetPosition.y - origin.y;
    const float horizontalScale = (std::max)(baseScale_.x, baseScale_.z);
    const float bandWidth = (std::max)(kWaveBandHalfWidth * horizontalScale, waveRadius_ * 0.045f);
    // フレーム間に通過した帯も調べ、処理落ちで輪がプレイヤーを飛び越えないようにします。
    const bool crossesRing = targetDistance >= previousRadius - bandWidth && targetDistance <= waveRadius_ + bandWidth;
    // 下段の救済床には届かせず、同じ高さのプレイヤーだけを対象にします。
    const bool isLowEnough =
        targetRelativeHeight >= -0.65f * baseScale_.y && targetRelativeHeight <= kWaveJumpClearHeight * baseScale_.y;
    if (!crossesRing || !isLowEnough) {
        return;
    }

    const engine::Vector3 knockbackDirection = NormalizePlanar(targetPosition - origin);
    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = kWaveDamage;
    damageEvent.damageType = DamageType::Fire;
    damageEvent.knockbackVelocity = {knockbackDirection.x * 13.0f, 6.0f, knockbackDirection.z * 13.0f};
    EventManager::GetInstance()->Dispatch(damageEvent);
    HitEffectDirector::SpawnEnemyAbilityHit(targetPosition + engine::Vector3{0.0f, 0.55f, 0.0f});
    waveDamageDone_ = true;
}

void EnemyRingBurner::UpdateVisualPose(float deltaTime) {
    // 各状態が決める目標姿勢を基準スケールから作り、急な切替を補間する。
    engine::Vector3 targetScale = baseScale_;
    engine::Vector4 targetColor = defaultColor_;
    float emissive = 1.35f;

    BehaviorFor(state_).ApplyVisualPose(*this, targetScale, targetColor, emissive);

    engine::Vector3 currentScale = GetScale();
    currentScale = engine::Math::Lerp(currentScale, targetScale, (std::min)(1.0f, deltaTime * 12.0f));
    SetScale(currentScale);
    SetColor(targetColor);
    SetEmissive(emissive);
}

bool EnemyRingBurner::IsTargetStandingOnTop() const {
    // 輪の回避を兼ねた踏みつけを、接地・水平距離・上面との高さで判定する。
    const Player* player = dynamic_cast<const Player*>(target_);
    if (!player || !player->IsGrounded()) {
        return false;
    }

    const engine::Vector3 origin = GetWorldPosition();
    const engine::Vector3 targetPosition = player->GetWorldPosition();
    const float horizontalScale = (std::max)(baseScale_.x, baseScale_.z);
    const float distance = PlanarDistance(origin, targetPosition);
    const float relativeHeight = targetPosition.y - origin.y;
    return distance <= kTopRadius * horizontalScale && relativeHeight >= kModelHeight * baseScale_.y * 0.62f &&
           relativeHeight <= kModelHeight * baseScale_.y + 2.4f;
}

float EnemyRingBurner::GetPlanarTargetDistance() const {
    // 対象がない間は射程外として扱い、不意に充填状態へ入らない。
    return target_ ? PlanarDistance(GetWorldPosition(), target_->GetWorldPosition()) : 99999.0f;
}

float EnemyRingBurner::GetChargeDuration() const {
    // 既存の揺れ時間を充填時間へ流用し、極端な設定値は制限する。
    return param_.has_value() ? (std::clamp)(param_->shakeDuration, 0.45f, 1.40f) : 0.78f;
}

float EnemyRingBurner::GetCooldownDuration() const {
    // 敵の行動間隔を再使用待ちへ流用し、連射や長時間停止を避ける。
    return param_.has_value() ? (std::clamp)(param_->interval, 1.2f, 8.0f) : 3.0f;
}

float EnemyRingBurner::GetWaveMaximumRadius() const {
    // 設定した移動量を輪の到達距離とし、配置スケールにも追従させる。
    const float horizontalScale = (std::max)(baseScale_.x, baseScale_.z);
    const float configured = param_.has_value() ? param_->moveAmount : 11.0f;
    return (std::clamp)(configured, 6.0f, 48.0f) * horizontalScale;
}

void EnemyRingBurner::SpawnChargeEffects() {
    // 充填のメッシュ演出と火花を本体の高さ・大きさに合わせて置く。
    const engine::Vector3 origin = GetWorldPosition() + engine::Vector3{0.0f, 0.18f * baseScale_.y, 0.0f};
    const float scale = (std::max)(baseScale_.x, baseScale_.z);
    if (auto* meshEffects = MeshEffectManager::GetInstance()) {
        meshEffects->SpawnEffectAt(kChargeEffect, origin, {0.0f, 0.0f, 0.0f}, {scale, baseScale_.y, scale});
    }
    if (auto* particles = GPUParticleManager::GetInstance(); particles->IsInitialized()) {
        particles->Emit(kChargeParticle, origin + engine::Vector3{0.0f, 1.12f * baseScale_.y, 0.0f});
    }
}

void EnemyRingBurner::SpawnWaveEffects() {
    // 作成時の半径11を基準に表示を拡縮し、判定の最大半径と揃える。
    const engine::Vector3 origin = GetWorldPosition() + engine::Vector3{0.0f, 0.34f * baseScale_.y, 0.0f};
    const float authoredRadius = 11.0f;
    const float effectScale = GetWaveMaximumRadius() / authoredRadius;
    if (auto* meshEffects = MeshEffectManager::GetInstance()) {
        StopWaveEffects();
        waveEffectScope_ = meshEffects->CreateEffectScope();
        const engine::Vector3 scale = {effectScale, baseScale_.y * 3.0f, effectScale};
        meshEffects->SpawnEffectAtScoped(waveEffectScope_, kWaveHeatEffect, origin, {0.0f, 0.0f, 0.0f}, scale);
        meshEffects->SpawnEffectAtScoped(waveEffectScope_, kWaveEffect, origin, {0.0f, 0.0f, 0.0f}, scale);
    }
    if (auto* particles = GPUParticleManager::GetInstance(); particles->IsInitialized()) {
        particles->Emit(kFireParticle, origin + engine::Vector3{0.0f, 1.05f * baseScale_.y, 0.0f});
    }
}

void EnemyRingBurner::StopWaveEffects() {
    // 踏んで停止した輪は当たり判定だけでなく描画も同時に回収します。
    if (waveEffectScope_ != 0) {
        MeshEffectManager::GetInstance()->StopEffectScope(waveEffectScope_);
        waveEffectScope_ = 0;
    }
}

std::unique_ptr<engine::Object3d> EnemyRingBurner::Clone() const {
    // シーン配置を複製するときも固有AIを新規生成し、対象と調整済みスケールを引き継ぐ。
    auto clone = std::make_unique<EnemyRingBurner>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    clone->baseScale_ = baseScale_;
    clone->hasBaseScale_ = true;
    return clone;
}

void EnemyRingBurner::SetCarried(bool isCarried) {
    // 固定砲台は吸収・持ち運びの対象外です。
    (void)isCarried;
}

void EnemyRingBurner::ApplyManagedScale(const engine::Vector3& scale) {
    // エディターで指定した大きさを、輪の半径と姿勢補間の基準にも反映する。
    baseScale_ = scale;
    hasBaseScale_ = true;
    SetScale(scale);
}

void EnemyRingBurner::CaptureReplayCustomState(json& state) const {
    // 輪の半径と被弾済み状態も保存し、巻き戻し時の二重ダメージを避ける。
    BaseEnemy::CaptureReplayCustomState(state);
    state["ringBurnerState"] = static_cast<int>(state_);
    state["ringBurnerStateTimer"] = stateTimer_;
    state["ringBurnerStateDuration"] = stateDuration_;
    state["ringBurnerCooldown"] = cooldownTimer_;
    state["ringBurnerWaveRadius"] = waveRadius_;
    state["ringBurnerIdleTimer"] = idleTimer_;
    state["ringBurnerWaveDamageDone"] = waveDamageDone_;
}

void EnemyRingBurner::RestoreReplayCustomState(const json& state) {
    // 保存番号を有効な状態範囲へ収め、欠損した録画は待機値で復元する。
    BaseEnemy::RestoreReplayCustomState(state);
    state_ = static_cast<State>((std::clamp)(state.value("ringBurnerState", 0), 0, 4));
    stateTimer_ = state.value("ringBurnerStateTimer", 0.0f);
    stateDuration_ = state.value("ringBurnerStateDuration", 0.0f);
    cooldownTimer_ = state.value("ringBurnerCooldown", 0.0f);
    waveRadius_ = state.value("ringBurnerWaveRadius", 0.0f);
    idleTimer_ = state.value("ringBurnerIdleTimer", 0.0f);
    waveDamageDone_ = state.value("ringBurnerWaveDamageDone", false);
}
