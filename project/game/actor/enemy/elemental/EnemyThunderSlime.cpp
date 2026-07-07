#include "EnemyThunderSlime.h"
#include "SlimeBounceAnimator.h"
#include "BulletManager.h"
#include "CameraManager.h"
#include "CollisionConfig.h"
#include "EffectObject3d.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace {
// 雷スライムの放電範囲、タメ時間、常時オーラの調整値
constexpr float kShockStartRange = 4.1f;
constexpr float kShockRadius = 5.2f;
constexpr float kShockDamage = 1.0f;
constexpr float kChargeDuration = 0.52f;
constexpr float kWildShockCooldown = 2.35f;
constexpr float kMoveSpeedScale = 1.12f;
constexpr float kCarriedShockCooldown = 0.72f;
constexpr float kGroundCollisionWorldRadius = 0.82f;
constexpr float kThrownCollisionWorldRadius = 1.18f;
constexpr float kShockSquashDuration = 0.34f;
constexpr float kMoveHopInterval = 0.25f;
constexpr float kMoveHopPower = 5.0f;
constexpr float kThunderSlimeModelYawOffset = 3.1415926535f;
constexpr const char* kDischargePreset = "thunder_slime_discharge";
constexpr const char* kIdleSparkPreset = "thunder_slime_idle_spark";
constexpr const char* kConstantAuraEffectPath = "Resources/json/effect/effect_thunder_slime_constant_aura.json";
constexpr float kIdleSparkInterval = 0.095f;
constexpr float kTwoPi = 6.283185307f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}

BulletVisualConfig MakeShockVisual() {
    BulletVisualConfig visual;
    visual.materialType = 0;
    visual.blendMode = BlendMode::kAdd;
    visual.color = { 1.0f, 0.92f, 0.12f, 0.0f };
    visual.emissive = 0.0f;
    visual.visualScale = 0.04f;
    visual.effectType = 1.0f;
    visual.effectScale = 0.86f;
    visual.effectSoftness = 0.18f;
    visual.effectIntensity = 1.55f;
    visual.billboardScale = 0.72f;
    return visual;
}
}

// 明示的な破棄処理は持たず、unique_ptr に任せる
EnemyThunderSlime::~EnemyThunderSlime() = default;

// 雷スライム本体と常時オーラの初期化
void EnemyThunderSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_ThunderSlime");
    SetEnemyType("ThunderSlime");
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    defaultColor_ = GetColor();

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SyncGroundCollisionRadius();
    InitializeAuraEffect();
}

// 近距離放電、追跡、徘徊、オーラ同期の更新
void EnemyThunderSlime::Update(float deltaTime) {
    if (UpdateInactiveState(deltaTime)) {
        return;
    }

    EnsureBaseScale();
    UpdateWildTimers(deltaTime);

    Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    UpdateWildBehavior(deltaTime, velocity);
    ApplyGroundMovementAndAnimation(deltaTime, velocity);
    BaseEnemy::Update(deltaTime);
    UpdateAuraEffect(deltaTime);
}

bool EnemyThunderSlime::UpdateInactiveState(float deltaTime) {
    if (isDead || !GetIsVisible()) {
        HideAttackTelegraph();
        isCharging_ = false;
        chargeTimer_ = 0.0f;
        chargeParticleTimer_ = 0.0f;
        shockSquashTimer_ = 0.0f;
        idleSparkTimer_ = kIdleSparkInterval;
        HideAuraEffect();
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        return true;
    }

    if (ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        HideAuraEffect();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    idleTimer_ += deltaTime;
    UpdateIdleSpark(deltaTime);
    if (isCarried_) {
        HideAttackTelegraph();
        UpdateAuraEffect(deltaTime);
        return true;
    }
    return UpdateThrowRecoveryState(deltaTime);
}

bool EnemyThunderSlime::UpdateThrowRecoveryState(float deltaTime) {
    if (!IsThrowRecovering()) {
        return false;
    }
    if (IsThrownPhysics()) {
        SyncThrownCollisionRadius();
    } else {
        SyncGroundCollisionRadius();
    }
    BaseEnemy::Update(deltaTime);
    UpdateAuraEffect(deltaTime);
    return true;
}

void EnemyThunderSlime::EnsureBaseScale() {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyThunderSlime::UpdateWildTimers(float deltaTime) {
    shockSquashTimer_ = (std::max)(0.0f, shockSquashTimer_ - deltaTime);
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
}

void EnemyThunderSlime::UpdateWildBehavior(float deltaTime, Vector3& velocity) {
    if (target_ && param_.has_value()) {
        Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        Vector3 direction = NormalizePlanar(toTarget);
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

        UpdateFacing(direction);
        UpdateCombatBehavior(deltaTime, velocity, direction, distance);
    }
    if (!isCharging_) {
        HideAttackTelegraph();
    }
}

void EnemyThunderSlime::UpdateCombatBehavior(float deltaTime, Vector3& velocity, const Vector3& direction, float distance) {
    if (isCharging_) {
        UpdateCharge(deltaTime, direction);
    }
    else if (distance <= kShockStartRange && attackCooldown_ <= 0.0f) {
        lastShockDirection_ = direction;
        StartCharge();
    }
    else if (distance <= detectionRange_) {
        const float speed = (std::max)(0.0f, param_->speed) * kMoveSpeedScale;
        velocity.x = direction.x * speed;
        velocity.z = direction.z * speed;
    }
    else {
        UpdateWanderBehavior(deltaTime, velocity);
    }
}

void EnemyThunderSlime::UpdateWanderBehavior(float deltaTime, Vector3& velocity) {
    const float speed = (std::max)(0.65f, param_->speed * 0.48f);
    velocity = CalculateWanderVelocity(deltaTime, speed, 0.68f);
    UpdateFacing({ velocity.x, 0.0f, velocity.z });
}

void EnemyThunderSlime::ApplyGroundMovementAndAnimation(float deltaTime, Vector3& velocity) {
    velocity.y = (std::min)(GetVelocity().y, 0.0f);
    if (isCharging_) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        groundHopTimer_ = 0.0f;
    } else if (SlimeBounceAnimator::StepGroundHop(groundHopTimer_, velocity, isGrounded_, deltaTime, kMoveHopInterval, 0.10f)) {
        velocity.y = (std::max)(velocity.y, kMoveHopPower);
    }
    SetVelocity(velocity);
    ApplySlimeAnimation(deltaTime);
    SyncGroundCollisionRadius();
}

void EnemyThunderSlime::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    BaseEnemy::Draw(pointLightResource, spotLightResource);
    if (auraEffect_ && auraEffect_->GetIsVisible()) {
        auraEffect_->Draw(pointLightResource, spotLightResource);
    }
}

std::unique_ptr<Object3d> EnemyThunderSlime::Clone() const {
    auto clone = std::make_unique<EnemyThunderSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyThunderSlime::BeginThrown(const Vector3& initialVelocity) {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        const float capturedMaxScale = (std::max)({ std::abs(baseScale_.x), std::abs(baseScale_.y), std::abs(baseScale_.z) });
        if (capturedMaxScale < 1.2f) {
            baseScale_ = { 2.0f, 2.0f, 2.0f };
        }
        hasBaseScale_ = true;
    }
    isCharging_ = false;
    chargeTimer_ = 0.0f;
    chargeParticleTimer_ = 0.0f;
    shockSquashTimer_ = 0.0f;
    HideAttackTelegraph();
    SetScale(baseScale_);
    SyncThrownCollisionRadius();
    BaseEnemy::BeginThrown(initialVelocity);
}

// 持ち運び中にプレイヤー前方へ短い放電弾を出す能力
void EnemyThunderSlime::ExecuteAbility(Player* player) {
    if (!player || !isCarried_ || carriedShockCooldown_ > 0.0f) {
        return;
    }

    Vector3 direction = player->GetForwardDirection();
    if (Math::Length(direction) <= 0.001f) {
        direction = { 0.0f, 0.0f, 1.0f };
    }
    direction = Math::Normalize(direction);

    Vector3 center = player->GetWorldPosition() + direction * 1.35f;
    center.y += 0.85f;

    BulletManager::GetInstance()->Fire(
        center,
        direction * 2.0f,
        kPlayerAttack,
        kEnemy | kAllSolid,
        "Primitives/sphere",
        1.35f,
        0.26f,
        MakeShockVisual());

    EmitThunderPreset(kDischargePreset, center);
    carriedShockCooldown_ = kCarriedShockCooldown;
    carriedEffectTimer_ = 0.25f;
}

void EnemyThunderSlime::UpdateCarriedAbility(Player* player, float deltaTime) {
    (void)player;
    if (!isCarried_) {
        return;
    }

    carriedShockCooldown_ = (std::max)(0.0f, carriedShockCooldown_ - deltaTime);
    carriedEffectTimer_ = (std::max)(0.0f, carriedEffectTimer_ - deltaTime);

    const float charge = 1.0f - (std::clamp)(carriedShockCooldown_ / kCarriedShockCooldown, 0.0f, 1.0f);
    const float flicker = std::sin(idleTimer_ * 36.0f) * 0.07f;
    SetColor({ 1.0f, (std::clamp)(0.94f + flicker, 0.0f, 1.0f), 0.72f + charge * 0.14f, 1.0f });
}

// 放電攻撃と火花演出
void EnemyThunderSlime::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.0001f) return;
    const float targetYaw = std::atan2(direction.x, direction.z) + kThunderSlimeModelYawOffset;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.16f));
}

void EnemyThunderSlime::StartCharge() {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
    isCharging_ = true;
    chargeTimer_ = kChargeDuration;
    chargeParticleTimer_ = 0.0f;
    attackCooldown_ = kWildShockCooldown;
    SetScale({ baseScale_.x * 3.35f, baseScale_.y * 0.18f, baseScale_.z * 3.35f });
    SyncGroundCollisionRadius();
}

void EnemyThunderSlime::UpdateCharge(float deltaTime, const Vector3& direction) {
    lastShockDirection_ = direction;
    chargeTimer_ = (std::max)(0.0f, chargeTimer_ - deltaTime);
    chargeParticleTimer_ -= deltaTime;
    const float progress = 1.0f - (std::clamp)(chargeTimer_ / kChargeDuration, 0.0f, 1.0f);
    ShowAttackTelegraphCircle(
        GetTranslate(),
        kShockRadius,
        progress,
        { 1.0f, 0.95f, 0.16f, 0.82f });

    if (chargeParticleTimer_ <= 0.0f) {
        EmitOuterThunderEffect(kIdleSparkPreset, 3, 0.55f);
        chargeParticleTimer_ = 0.045f;
    }

    if (chargeTimer_ <= 0.0f) {
        ReleaseShock(lastShockDirection_);
    }
}

void EnemyThunderSlime::ReleaseShock(const Vector3& direction) {
    isCharging_ = false;
    TriggerAttackTelegraphCue({ 1.0f, 0.12f, 0.04f, 1.0f });
    HideAttackTelegraph();
    shockSquashTimer_ = kShockSquashDuration;
    SetScale({ baseScale_.x * 5.4f, baseScale_.y * 0.10f, baseScale_.z * 5.4f });
    SyncGroundCollisionRadius();

    Vector3 center = GetTranslate();
    center.y += (std::max)(0.5f, baseScale_.y * 0.22f);
    EmitOuterThunderEffect(kDischargePreset, 5, 0.35f);
    DispatchShockDamage(center, direction, kShockRadius, kShockDamage);
}

void EnemyThunderSlime::DispatchShockDamage(const Vector3& center, const Vector3& direction, float radius, float damage) {
    if (!target_) {
        return;
    }

    Vector3 diff = target_->GetTranslate() - center;
    diff.y *= 0.45f;
    const float distance = Math::Length(diff);
    if (distance > radius) {
        return;
    }

    const float distanceRate = 1.0f - (std::clamp)(distance / radius, 0.0f, 1.0f);
    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = damage;
    damageEvent.knockbackVelocity = {
        direction.x * (8.0f + distanceRate * 5.0f),
        5.8f + distanceRate * 2.0f,
        direction.z * (8.0f + distanceRate * 5.0f)
    };
    EventManager::GetInstance()->Dispatch(damageEvent);
}

void EnemyThunderSlime::UpdateIdleSpark(float deltaTime) {
    idleSparkTimer_ -= deltaTime;
    if (idleSparkTimer_ > 0.0f) {
        return;
    }

    EmitOuterThunderEffect(kIdleSparkPreset, isCharging_ ? 3 : 2);
    if (isCharging_) {
        idleSparkTimer_ = kIdleSparkInterval * 0.62f;
    }
    else if (isCarried_) {
        idleSparkTimer_ = kIdleSparkInterval * 0.76f;
    }
    else {
        idleSparkTimer_ = kIdleSparkInterval;
    }
}

// 常時オーラの表示位置と形状同期
void EnemyThunderSlime::InitializeAuraEffect() {
    if (!common_ || auraEffect_) {
        return;
    }

    auraEffect_ = std::make_unique<EffectObject3d>();
    auraEffect_->Initialize(common_);
    auraEffect_->SetName("ThunderSlime_RoundAura");
    if (!auraEffect_->LoadFromJson(kConstantAuraEffectPath)) {
        auraEffect_.reset();
        return;
    }
    auraEffect_->SetIsVisible(false);
    auraEffect_->Play(99999.0f);
}

void EnemyThunderSlime::UpdateAuraEffect(float deltaTime) {
    if (!auraEffect_) {
        InitializeAuraEffect();
    }
    if (!auraEffect_) {
        return;
    }

    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        const float capturedMaxScale = (std::max)({ std::abs(baseScale_.x), std::abs(baseScale_.y), std::abs(baseScale_.z) });
        if (capturedMaxScale < 1.2f) {
            baseScale_ = { 2.0f, 2.0f, 2.0f };
        }
        hasBaseScale_ = true;
    }

    Vector3 auraPos{};
    float horizontalDiameter = 1.0f;
    float verticalDiameter = 1.0f;
    CalculateAuraShape(auraPos, horizontalDiameter, verticalDiameter);

    float yaw = GetRotation().y;
    Camera* camera = CameraManager::GetInstance() ? CameraManager::GetInstance()->GetActiveCamera() : nullptr;
    if (camera) {
        const Vector3 toCamera = camera->GetEye() - auraPos;
        if (std::abs(toCamera.x) + std::abs(toCamera.z) > 0.001f) {
            yaw = std::atan2(toCamera.x, toCamera.z);
        }
    }

    auraEffect_->SetIsVisible(true);
    auraEffect_->Update(deltaTime);
    auraEffect_->SetTranslate(auraPos);
    auraEffect_->SetRotation({ 1.5707963f, yaw, 0.0f });
    auraEffect_->SetScale({ horizontalDiameter, 1.0f, verticalDiameter });
    auraEffect_->UpdateLocalMatrix();
    auraEffect_->UpdateWorldMatrix();
}

void EnemyThunderSlime::HideAuraEffect() {
    if (auraEffect_) {
        auraEffect_->SetIsVisible(false);
    }
}

void EnemyThunderSlime::CalculateAuraShape(Vector3& center, float& horizontalDiameter, float& verticalDiameter) const {
    const Vector3 scale = GetScale();
    const float maxXZ = (std::max)({ std::abs(scale.x), std::abs(scale.z), 1.0f });
    const float yScale = (std::max)(std::abs(scale.y), 0.18f);

    horizontalDiameter = (std::max)(2.55f, maxXZ * 1.35f);
    verticalDiameter = (std::max)(0.46f, yScale * 1.28f);

    center = GetWorldPosition();
    center.y += (std::max)(0.22f, yScale * 0.43f);
}

void EnemyThunderSlime::EmitOuterThunderEffect(const char* presetName, int count, float phaseOffset) {
    if (count <= 0) {
        return;
    }

    Vector3 center{};
    float horizontalDiameter = 1.0f;
    float verticalDiameter = 1.0f;
    CalculateAuraShape(center, horizontalDiameter, verticalDiameter);

    const float horizontalRadius = horizontalDiameter * 0.52f;
    const float verticalRadius = verticalDiameter * 0.38f;
    const float phase = idleTimer_ * 3.4f + phaseOffset;
    for (int i = 0; i < count; ++i) {
        const float angle = phase + (kTwoPi * static_cast<float>(i) / static_cast<float>(count));
        Vector3 pos = center;
        pos.x += std::cos(angle) * horizontalRadius;
        pos.z += std::sin(angle) * horizontalRadius;
        pos.y += std::sin(angle * 1.37f) * verticalRadius;
        EmitThunderPreset(presetName, pos);
    }
}

void EnemyThunderSlime::EmitThunderPreset(const char* presetName, const Vector3& position) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    particles->Emit(presetName, position);
}

void EnemyThunderSlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    if (isCharging_) {
        const float t = 1.0f - (chargeTimer_ / kChargeDuration);
        const float slam = t * t * (3.0f - 2.0f * t);
        const float shake = std::sin(t * 46.0f) * 0.055f;
        targetScale.x = baseScale_.x * (2.1f + slam * 2.3f + shake);
        targetScale.y = baseScale_.y * (1.0f - slam * 0.91f - std::abs(shake) * 0.28f);
        targetScale.z = baseScale_.z * (2.0f + slam * 2.35f - shake);
        SetColor({ 1.0f, 0.98f, 0.70f + std::sin(t * 58.0f) * 0.08f, 1.0f });
    }
    else if (shockSquashTimer_ > 0.0f) {
        const float p = (std::clamp)(shockSquashTimer_ / kShockSquashDuration, 0.0f, 1.0f);
        const float pulse = std::sin((1.0f - p) * 3.14159265f) * 0.16f;
        targetScale.x = baseScale_.x * (1.0f + p * 4.35f + pulse);
        targetScale.y = baseScale_.y * (1.0f - p * 0.91f);
        targetScale.z = baseScale_.z * (1.0f + p * 4.1f + pulse);
        SetColor({ 1.0f, 0.98f, 0.62f + p * 0.22f, 1.0f });
    }
    else {
        SlimeBounceAnimator::Params params;
        params.speedForFullBounce = 2.1f;
        params.idleAmplitude = 0.085f;
        params.moveAmplitude = 0.30f;
        params.hopFrequency = 11.0f;
        params.horizontalSquash = 0.30f;
        params.verticalStretch = 0.38f;
        params.airborneStretch = 0.36f;
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
        SetColor(defaultColor_);
    }

    Vector3 scale = GetScale();
    const float lerpRate = isCharging_ || shockSquashTimer_ > 0.0f ? 1.0f : (std::min)(1.0f, deltaTime * 11.0f);
    scale = Math::Lerp(scale, targetScale, lerpRate);
    SetScale(scale);
}

void EnemyThunderSlime::SyncWorldCollisionRadius(float worldRadius) {
    const Vector3 scale = GetScale();
    const float maxScale = (std::max)({ 0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    SetCollisionRadius(worldRadius / maxScale);
}

void EnemyThunderSlime::SyncGroundCollisionRadius() {
    SyncWorldCollisionRadius(kGroundCollisionWorldRadius);
}

void EnemyThunderSlime::SyncThrownCollisionRadius() {
    SyncWorldCollisionRadius(kThrownCollisionWorldRadius);
}
