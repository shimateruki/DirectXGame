#include "BaseEnemy.h"
#include "CollisionConfig.h" // kEnemyなどの定義を使うため
#include "Event.h"           //  DamageEventを使うため
#include "EventManager.h"    //  イベントを発行(Dispatch)するため
#include "Player.h"          //  プレイヤーの状態を見るため

#include "CollisionManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kWanderPi = 3.14159265358979323846f;
constexpr float kThrownMaxTime = 3.0f;
constexpr float kThrownSettleTime = 0.22f;
constexpr float kThrownSettleSpeed = 1.4f;
constexpr float kThrownGroundFriction = 0.90f;
constexpr float kThrownAirDrag = 0.995f;
constexpr float kThrownBounceMinSpeed = 5.0f;
constexpr float kThrownSlamMinSpeed = 13.0f;
constexpr float kThrownFollowUpSlamMinSpeed = 7.0f;
constexpr float kThrownGroundNormal = 0.55f;
constexpr float kThrowRecoveryDuration = 0.45f;
constexpr float kThrowRecoveryGroundFriction = 0.82f;
constexpr int kMaxSlamImpacts = 2;
constexpr float kSlamImpactCooldown = 0.16f;
constexpr float kSlamRadiusBase = 4.0f;
constexpr float kSlamDamageBase = 46.0f;
constexpr float kSlamDamageScale = 0.70f;
constexpr float kDefeatEffectDuration = 1.05f;
constexpr float kDefeatRiseHeight = 1.45f;
constexpr float kDefeatSpinSpeed = 5.4f;
constexpr float kDefeatParticleInterval = 0.055f;

float PlanarDistance(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

float PlanarLength(const Vector3& value) {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

Vector3 NormalizePlanarOr(const Vector3& value, const Vector3& fallback) {
    Vector3 result = { value.x, 0.0f, value.z };
    const float length = PlanarLength(result);
    if (length <= 0.001f) {
        return fallback;
    }
    return { result.x / length, 0.0f, result.z / length };
}

float EaseOutCubic(float t) {
    t = (std::clamp)(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

float CalculateSlamDamage(float impactSpeed) {
    return kSlamDamageBase + impactSpeed * kSlamDamageScale;
}
}

void BaseEnemy::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 1. 親クラス(Character)の初期化
    Character::Initialize(common);

    // 2. モデルをセット
    SetModel(modelName);

    // 3. 当たり判定の設定
    SetCollisionAttribute(kEnemy);       // 自分は「敵」グループ
    SetCollisionMask(kPlayer | kGround | kAttributePlayerBullet | kPlayerAttack);
    SetClassName("Enemy");
    defaultColor_ = GetColor();
}

void BaseEnemy::CaptureWanderOrigin() {
    if (hasWanderOrigin_) {
        return;
    }

    wanderOrigin_ = GetTranslate();
    wanderTarget_ = wanderOrigin_;
    wanderSeed_ ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    wanderSeed_ ^= static_cast<uint32_t>((std::abs(wanderOrigin_.x) + 1.0f) * 73856093.0f);
    wanderSeed_ ^= static_cast<uint32_t>((std::abs(wanderOrigin_.z) + 1.0f) * 19349663.0f);
    if (wanderSeed_ == 0u) {
        wanderSeed_ = 0x12345678u;
    }
    hasWanderOrigin_ = true;
    PickWanderTarget(0.65f, 0.0f);
}

void BaseEnemy::ResetWanderOrigin() {
    hasWanderOrigin_ = false;
    wanderWaitTimer_ = 0.0f;
    wanderRetargetTimer_ = 0.0f;
}

float BaseEnemy::NextWanderRandom() {
    wanderSeed_ = wanderSeed_ * 1664525u + 1013904223u;
    return static_cast<float>((wanderSeed_ >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float BaseEnemy::GetWanderRadius(float radiusScale) const {
    const float range = param_.has_value() ? param_->detectionRange : detectionRange_;
    const float baseRadius = (std::max)(3.0f, range * radiusScale);
    return (std::clamp)(baseRadius, 3.0f, 18.0f);
}

void BaseEnemy::PickWanderTarget(float radiusScale, float verticalOffset) {
    const float radius = GetWanderRadius(radiusScale);
    const float angle = NextWanderRandom() * kWanderPi * 2.0f;
    const float distance = radius * (0.25f + NextWanderRandom() * 0.75f);

    wanderTarget_.x = wanderOrigin_.x + std::sin(angle) * distance;
    wanderTarget_.y = wanderOrigin_.y + verticalOffset;
    wanderTarget_.z = wanderOrigin_.z + std::cos(angle) * distance;
    wanderRetargetTimer_ = 2.4f + NextWanderRandom() * 2.6f;
}

Vector3 BaseEnemy::GetWanderTargetPosition(float deltaTime, float radiusScale, float verticalOffset) {
    CaptureWanderOrigin();

    if (wanderWaitTimer_ > 0.0f) {
        wanderWaitTimer_ = (std::max)(0.0f, wanderWaitTimer_ - deltaTime);
        return wanderTarget_;
    }

    wanderRetargetTimer_ = (std::max)(0.0f, wanderRetargetTimer_ - deltaTime);
    const float radius = GetWanderRadius(radiusScale);
    const float distanceToTarget = PlanarDistance(GetTranslate(), wanderTarget_);
    const float distanceFromOrigin = PlanarDistance(GetTranslate(), wanderOrigin_);

    if (distanceFromOrigin > radius * 1.08f) {
        Vector3 toOrigin = wanderOrigin_ - GetTranslate();
        toOrigin.y = 0.0f;
        const float length = (std::max)(0.001f, std::sqrt(toOrigin.x * toOrigin.x + toOrigin.z * toOrigin.z));
        wanderTarget_ = GetTranslate() + Vector3{ toOrigin.x / length, 0.0f, toOrigin.z / length } * (radius * 0.45f);
        wanderTarget_.y = wanderOrigin_.y + verticalOffset;
        wanderRetargetTimer_ = 1.2f;
    } else if (distanceToTarget < 0.85f) {
        wanderWaitTimer_ = 0.45f + NextWanderRandom() * 1.0f;
        PickWanderTarget(radiusScale, verticalOffset);
    } else if (wanderRetargetTimer_ <= 0.0f) {
        PickWanderTarget(radiusScale, verticalOffset);
    }

    return wanderTarget_;
}

Vector3 BaseEnemy::CalculateWanderVelocity(float deltaTime, float moveSpeed, float radiusScale, float verticalOffset, bool includeVertical) {
    Vector3 velocity = GetVelocity();
    const Vector3 target = GetWanderTargetPosition(deltaTime, radiusScale, verticalOffset);

    if (wanderWaitTimer_ > 0.0f) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        if (includeVertical) {
            velocity.y = 0.0f;
        }
        return velocity;
    }

    Vector3 toTarget = target - GetTranslate();
    if (!includeVertical) {
        toTarget.y = 0.0f;
    }

    const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
    if (distance <= 0.001f) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        if (includeVertical) {
            velocity.y = 0.0f;
        }
        return velocity;
    }

    const float speed = (std::max)(0.0f, moveSpeed);
    const Vector3 direction = toTarget / distance;
    const float slowRate = (std::clamp)(distance / 2.0f, 0.25f, 1.0f);
    velocity.x = direction.x * speed * slowRate;
    velocity.z = direction.z * speed * slowRate;
    if (includeVertical) {
        velocity.y = direction.y * speed * slowRate;
    }
    return velocity;
}

void BaseEnemy::BeginThrown(const Vector3& initialVelocity) {
    throwRecoveryTargetRotation_ = GetRotation();

    if (isCarried_) {
        SetCarried(false);
    }

    isThrownPhysics_ = true;
    isThrowRotationRecovering_ = false;
    slamImpactCount_ = 0;
    slamImpactCooldownTimer_ = 0.0f;
    thrownTimer_ = 0.0f;
    thrownSettleTimer_ = 0.0f;
    throwRecoveryRotateTimer_ = 0.0f;
    throwRecoveryTimer_ = 0.0f;
    velocity_ = initialVelocity;
    if (!param_.has_value()) {
        param_.emplace();
    }

    const float planarSpeed = PlanarLength(initialVelocity);
    thrownAngularVelocity_ = {
        (std::clamp)(initialVelocity.z * 0.08f, -7.5f, 7.5f),
        (std::clamp)(planarSpeed * 0.08f, 1.5f, 8.0f),
        (std::clamp)(-initialVelocity.x * 0.08f, -7.5f, 7.5f)
    };

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kAllSolid | kAttributePlayerBullet | kPlayerAttack);
}

void BaseEnemy::UpdateThrownPhysics(float deltaTime) {
    thrownTimer_ += deltaTime;
    slamImpactCooldownTimer_ = (std::max)(0.0f, slamImpactCooldownTimer_ - deltaTime);

    velocity_.x *= kThrownAirDrag;
    velocity_.z *= kThrownAirDrag;

    if (isGrounded_) {
        velocity_.x *= kThrownGroundFriction;
        velocity_.z *= kThrownGroundFriction;

        if (PlanarLength(velocity_) <= kThrownSettleSpeed && std::abs(velocity_.y) <= 1.0f) {
            thrownSettleTimer_ += deltaTime;
        }
        else {
            thrownSettleTimer_ = 0.0f;
        }

        if (slamImpactCount_ > 0 && thrownSettleTimer_ >= kThrownSettleTime) {
            EndThrownPhysics();
        }
    }

    if (thrownTimer_ >= kThrownMaxTime) {
        EndThrownPhysics();
    }

    if (!isThrownPhysics_) {
        return;
    }

    Vector3 rotation = GetRotation();
    rotation.x += thrownAngularVelocity_.x * deltaTime;
    rotation.y += thrownAngularVelocity_.y * deltaTime;
    rotation.z += thrownAngularVelocity_.z * deltaTime;
    SetRotation(rotation);
}

void BaseEnemy::HandleThrownCollision(const CollisionInfo& info, uint32_t attribute) {
    const Vector3 incomingVelocity = velocity_;
    const float normalSpeed = -Math::Dot(incomingVelocity, info.normal);
    const bool isGroundLike = info.normal.y > kThrownGroundNormal;

    ApplyPhysicsCollision(info, attribute);

    if (normalSpeed <= 0.0f) {
        return;
    }

    const float slamSpeedThreshold = slamImpactCount_ == 0 ? kThrownSlamMinSpeed : kThrownFollowUpSlamMinSpeed;
    if (isGroundLike &&
        slamImpactCount_ < kMaxSlamImpacts &&
        slamImpactCooldownTimer_ <= 0.0f &&
        normalSpeed >= slamSpeedThreshold) {
        ++slamImpactCount_;
        slamImpactCooldownTimer_ = kSlamImpactCooldown;
        OnSlamImpact(GetTranslate(), normalSpeed);
    }

    if (normalSpeed < kThrownBounceMinSpeed) {
        return;
    }

    const float normalDot = Math::Dot(incomingVelocity, info.normal);
    const Vector3 tangentVelocity = incomingVelocity - info.normal * normalDot;
    const float tangentKeep = isGroundLike ? 0.62f : 0.78f;
    const float bounceRate = isGroundLike ? 0.34f : 0.36f;

    velocity_ = tangentVelocity * tangentKeep + info.normal * (normalSpeed * bounceRate);

    if (isGroundLike && velocity_.y < 3.2f && slamImpactCount_ < kMaxSlamImpacts) {
        velocity_.y = 3.2f;
    }
}

void BaseEnemy::EndThrownPhysics() {
    if (!isThrownPhysics_) {
        return;
    }

    isThrownPhysics_ = false;
    thrownTimer_ = 0.0f;
    thrownSettleTimer_ = 0.0f;
    thrownAngularVelocity_ = { 0.0f, 0.0f, 0.0f };
    StartThrowRecovery();
    ResetWanderOrigin();
}

void BaseEnemy::StartThrowRecovery() {
    throwRecoveryTimer_ = kThrowRecoveryDuration;
    throwRecoveryRotateTimer_ = 0.0f;
    throwRecoveryStartRotation_ = GetRotation();
    isThrowRotationRecovering_ = true;

    if (isGrounded_) {
        velocity_.x = 0.0f;
        velocity_.z = 0.0f;
    }
}

void BaseEnemy::UpdateThrowRecovery(float deltaTime) {
    if (throwRecoveryTimer_ > 0.0f) {
        throwRecoveryTimer_ = (std::max)(0.0f, throwRecoveryTimer_ - deltaTime);
    }

    if (isGrounded_) {
        velocity_.x *= kThrowRecoveryGroundFriction;
        velocity_.z *= kThrowRecoveryGroundFriction;
    }

    if (!isThrowRotationRecovering_) {
        return;
    }

    throwRecoveryRotateTimer_ += deltaTime;
    const float t = EaseOutCubic(throwRecoveryRotateTimer_ / kThrowRecoveryDuration);
    const Quaternion start = Math::EulerToQuaternion(throwRecoveryStartRotation_);
    const Quaternion target = Math::EulerToQuaternion(throwRecoveryTargetRotation_);
    const Quaternion current = Math::Slerp(start, target, t);
    SetRotation(Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(current)));

    if (throwRecoveryRotateTimer_ >= kThrowRecoveryDuration) {
        SetRotation(throwRecoveryTargetRotation_);
        isThrowRotationRecovering_ = false;
        throwRecoveryRotateTimer_ = 0.0f;
    }
}

void BaseEnemy::OnSlamImpact(const Vector3& impactPosition, float impactSpeed) {
    if (GetEnemyType() != "Bomb") {
        DamageEvent selfDamage;
        selfDamage.target = this;
        selfDamage.attacker = this;
        selfDamage.damageAmount = CalculateSlamDamage(impactSpeed);
        EventManager::GetInstance()->Dispatch(selfDamage);
    }

    SpawnSlamImpactEffect(impactPosition, impactSpeed);
    DamageSlamTargets(impactPosition, impactSpeed);
}

void BaseEnemy::SpawnSlamImpactEffect(const Vector3& impactPosition, float impactSpeed) {
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->GetCurrentScene()) {
        return;
    }

    ParticleSystem* particleSystem = sceneManager->GetCurrentScene()->GetParticleSystem();
    if (!particleSystem) {
        return;
    }

    Vector3 up = { 0.0f, 1.0f, 0.0f };
    const float power = (std::clamp)(impactSpeed / 22.0f, 0.65f, 1.35f);
    particleSystem->SpawnParticles(
        impactPosition,
        static_cast<int>(28.0f * power),
        5.5f * power,
        &up,
        85.0f,
        { 0.95f, 0.9f, 0.78f, 1.0f },
        { 0.55f, 0.48f, 0.4f, 0.0f },
        0.18f,
        0.45f,
        0.7f * power,
        0.05f
    );
}

void BaseEnemy::DamageSlamTargets(const Vector3& impactPosition, float impactSpeed) {
    CollisionManager* collisionManager = CollisionManager::GetInstance();
    if (!collisionManager) {
        return;
    }

    const float radius = kSlamRadiusBase + (std::clamp)((impactSpeed - kThrownSlamMinSpeed) * 0.06f, 0.0f, 1.8f);
    const float damage = CalculateSlamDamage(impactSpeed);

    for (Object3d* object : collisionManager->GetObjects()) {
        if (!object || object == this || object->isDead) {
            continue;
        }
        if (!(object->GetCollisionAttribute() & kEnemy)) {
            continue;
        }

        Vector3 diff = object->GetWorldPosition() - impactPosition;
        diff.y *= 0.45f;
        const float distance = Math::Length(diff);
        if (distance > radius) {
            continue;
        }

        Vector3 knockbackDir = NormalizePlanarOr(diff, { 0.0f, 0.0f, 1.0f });
        const float distanceRate = 1.0f - (std::clamp)(distance / radius, 0.0f, 1.0f);

        DamageEvent damageEvent;
        damageEvent.target = object;
        damageEvent.attacker = this;
        damageEvent.damageAmount = damage * (0.55f + distanceRate * 0.45f);
        damageEvent.knockbackVelocity = {
            knockbackDir.x * (10.0f + impactSpeed * 0.22f) * distanceRate,
            7.0f + impactSpeed * 0.10f,
            knockbackDir.z * (10.0f + impactSpeed * 0.22f) * distanceRate
        };
        EventManager::GetInstance()->Dispatch(damageEvent);
    }
}

void BaseEnemy::UpdateDamageFeedbackTimers(float deltaTime) {
    if (damageCooldownTimer_ > 0.0f) {
        damageCooldownTimer_ -= deltaTime;
    }

    if (colorResetTimer_ > 0.0f) {
        colorResetTimer_ -= deltaTime;
        if (colorResetTimer_ <= 0.0f) {
            SetColor(defaultColor_);
        }
    }
}

bool BaseEnemy::ShouldHandleDefeatEffect() const {
    if (isDefeatEffectFinished_) {
        return false;
    }
    if (GetEnemyType() == "Bomb") {
        return false;
    }
    if (isDefeatEffectPlaying_) {
        return true;
    }
    return param_.has_value() && param_->hp <= 0.0f && !isDead;
}

ParticleSystem* BaseEnemy::GetCurrentParticleSystem() const {
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->GetCurrentScene()) {
        return nullptr;
    }
    return sceneManager->GetCurrentScene()->GetParticleSystem();
}

void BaseEnemy::BeginDefeatEffect() {
    isDefeatEffectPlaying_ = true;
    isDefeatEffectFinished_ = false;
    defeatEffectTimer_ = 0.0f;
    defeatEffectParticleTimer_ = 0.0f;
    defeatBasePosition_ = GetTranslate();
    defeatBaseScale_ = GetScale();
    defeatBaseColor_ = GetColor();

    isCarried_ = false;
    isThrownPhysics_ = false;
    isThrowRotationRecovering_ = false;
    throwRecoveryTimer_ = 0.0f;
    thrownTimer_ = 0.0f;
    thrownSettleTimer_ = 0.0f;
    slamImpactCooldownTimer_ = 0.0f;
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetSelectedLighting(2);
    SetMaterialType(4);
    SetBlendMode(BlendMode::kNormal);
    SetEmissive(2.4f);
    SetColor({ 1.0f, 0.96f, 0.72f, 1.0f });

    SpawnDefeatStartParticles();
}

void BaseEnemy::UpdateDefeatEffect(float deltaTime) {
    defeatEffectTimer_ += deltaTime;
    defeatEffectParticleTimer_ -= deltaTime;

    const float progress = (std::clamp)(defeatEffectTimer_ / kDefeatEffectDuration, 0.0f, 1.0f);
    const float eased = EaseOutCubic(progress);
    const float pulse = 1.0f + std::sin(progress * kWanderPi * 6.0f) * 0.06f * (1.0f - progress);
    const float shrink = 1.0f - eased * 0.34f;

    Vector3 scale = defeatBaseScale_;
    scale.x *= shrink * pulse;
    scale.z *= shrink * pulse;
    scale.y *= (1.0f - eased * 0.18f) * (1.0f + std::sin(progress * kWanderPi * 3.0f) * 0.10f * (1.0f - progress));
    SetScale(scale);

    Vector3 position = defeatBasePosition_;
    position.y += kDefeatRiseHeight * eased;
    SetTranslate(position);

    Vector3 rotation = GetRotation();
    rotation.y += (kDefeatSpinSpeed + progress * 4.0f) * deltaTime;
    rotation.x += 0.55f * (1.0f - progress) * deltaTime;
    SetRotation(rotation);

    const float dissolveThreshold = 1.0f - (progress * progress * (3.0f - 2.0f * progress));
    const Vector3 glowColor = {
        defeatBaseColor_.x * (1.0f - eased) + 1.0f * eased,
        defeatBaseColor_.y * (1.0f - eased) + 0.92f * eased,
        defeatBaseColor_.z * (1.0f - eased) + 0.48f * eased
    };
    SetColor({ glowColor.x, glowColor.y, glowColor.z, dissolveThreshold });

    if (defeatEffectParticleTimer_ <= 0.0f && progress < 0.96f) {
        SpawnDefeatLoopParticles();
        defeatEffectParticleTimer_ = kDefeatParticleInterval;
    }

    if (progress >= 1.0f) {
        isDefeatEffectPlaying_ = false;
        isDefeatEffectFinished_ = true;
        isDead = true;
        SetIsVisible(false);
        if (BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr) {
            scene->RequestRemoveObject(this);
        }
    }
}

void BaseEnemy::SpawnDefeatStartParticles() {
    ParticleSystem* particleSystem = GetCurrentParticleSystem();
    if (!particleSystem) {
        return;
    }

    Vector3 center = GetWorldPosition();
    center.y += (std::max)(0.35f, defeatBaseScale_.y * 0.45f);
    Vector3 up = { 0.0f, 1.0f, 0.0f };
    particleSystem->SpawnParticles(
        center,
        36,
        4.6f,
        nullptr,
        0.0f,
        { 1.0f, 0.95f, 0.58f, 1.0f },
        { 0.55f, 0.82f, 1.0f, 0.0f },
        0.25f,
        0.72f,
        0.55f,
        0.04f
    );
    particleSystem->SpawnParticles(
        center,
        18,
        2.2f,
        &up,
        1.1f,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 0.78f, 0.32f, 0.0f },
        0.22f,
        0.58f,
        0.35f,
        0.02f
    );
}

void BaseEnemy::SpawnDefeatLoopParticles() {
    ParticleSystem* particleSystem = GetCurrentParticleSystem();
    if (!particleSystem) {
        return;
    }

    const float progress = (std::clamp)(defeatEffectTimer_ / kDefeatEffectDuration, 0.0f, 1.0f);
    Vector3 center = GetWorldPosition();
    center.y += (std::max)(0.2f, defeatBaseScale_.y * (0.2f + progress * 0.45f));
    Vector3 up = { 0.0f, 1.0f, 0.0f };
    particleSystem->SpawnParticles(
        center,
        8,
        1.8f + progress * 1.4f,
        &up,
        0.9f,
        { 1.0f, 0.92f, 0.48f, 1.0f },
        { 0.72f, 0.9f, 1.0f, 0.0f },
        0.18f,
        0.46f,
        0.28f,
        0.02f
    );
}

void BaseEnemy::Update(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        if (!isDefeatEffectPlaying_) {
            BeginDefeatEffect();
        }
        UpdateDefeatEffect(deltaTime);
        Object3d::Update(deltaTime);
        return;
    }

    if (isCarried_) {
        return; // 重力も、タイマーの減少も全てストップ
    }
    // 0. パラメータ(JSON)との同期
    if (param_.has_value()) {
        detectionRange_ = param_->detectionRange;
    }

    if (isThrownPhysics_) {
        UpdateThrownPhysics(deltaTime);
        Character::Update(deltaTime);
        UpdateDamageFeedbackTimers(deltaTime);
        return;
    }

    // 重力処理などは親クラス(Character)に任せる
    if (throwRecoveryTimer_ > 0.0f || isThrowRotationRecovering_) {
        UpdateThrowRecovery(deltaTime);
    }

    Character::Update(deltaTime);
    UpdateDamageFeedbackTimers(deltaTime);
}

bool BaseEnemy::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();
    CollisionInfo info = CheckCollision(other);

    if (!info.isColliding) {
        return false;
    }

    // ========================================================
    // プレイヤーの攻撃（剣など）に当たった時の処理
    // ========================================================
    if (attribute & kPlayerAttack) {

        // ★ 追加: クールダウン中（無敵時間中）ならダメージ処理を無視して抜ける！
        if (damageCooldownTimer_ > 0.0f) {
            return true;
        }

        // ダメージイベントの発行
        DamageEvent dmgEvent;
        dmgEvent.target = this;
        dmgEvent.attacker = other;
        dmgEvent.damageAmount = 10.0f;
        EventManager::GetInstance()->Dispatch(dmgEvent);

   
        damageCooldownTimer_ = 0.5f; // 0.5秒間は次の攻撃を食らわない（剣の持続ヒット防止）
        colorResetTimer_ = 0.15f;    // 0.15秒間だけ赤くする

        SetColor({ 1.0f, 0.0f, 0.0f, 1.0f }); // 真っ赤にする

        // ※もし敵が複数のパーツ(子オブジェクト)でできている場合は以下も有効化してください
        // for (Object3d* child : GetChildren()) { child->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f }); }

        return true;
    }

    // 地面や壁（kAllSolid）なら、物理的な押し戻しを実行
    if (attribute & kAllSolid) {
        if (isThrownPhysics_) {
            HandleThrownCollision(info, attribute);
        }
        else {
            ApplyPhysicsCollision(info, attribute);
        }
    }

    return true;
}
// ==========================================
// 持ち運び状態の切り替え
// ==========================================
void BaseEnemy::SetCarried(bool isCarried) {
    const bool wasCarried = isCarried_;
    isCarried_ = isCarried;

    if (isCarried_) {
        isThrownPhysics_ = false;
        isThrowRotationRecovering_ = false;
        slamImpactCount_ = 0;
        slamImpactCooldownTimer_ = 0.0f;
        thrownTimer_ = 0.0f;
        thrownSettleTimer_ = 0.0f;
        throwRecoveryRotateTimer_ = 0.0f;
        thrownAngularVelocity_ = { 0.0f, 0.0f, 0.0f };
        throwRecoveryTargetRotation_ = GetRotation();

        // --- 無力化 ---
        // ★ 修正: Object3d の関数を使って完全に当たり判定を消滅させる！
        SetCollisionAttribute(0);
        SetCollisionMask(0);

        // 引っ張られるので速度も強制ゼロに
        SetVelocity({ 0.0f, 0.0f, 0.0f });
    }
    else {
        // --- 復活（地面に投げ出された時など） ---
   
        SetCollisionAttribute(kEnemy);
        if (wasCarried) {
            throwRecoveryTimer_ = 0.75f;
            ResetWanderOrigin();
        }
        SetCollisionMask(kPlayer | kAllSolid | kAttributePlayerBullet | kPlayerAttack); // 念のため kAllSolid にしておく
    }
}
void BaseEnemy::ExecuteAbility(Player* player) {
}

void BaseEnemy::UpdateCarriedAbility(Player* player, float deltaTime) {
    (void)player;
    (void)deltaTime;
}
