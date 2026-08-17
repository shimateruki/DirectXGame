#include "BaseEnemy.h"
#include "AttackTelegraph.h"
#include "CollisionConfig.h" // kEnemyなどの定義を使うため
#include "Event.h"           //  DamageEventを使うため
#include "EventManager.h"    //  イベントを発行(Dispatch)するため
#include "Player.h"          //  プレイヤーの状態を見るため
#include "Bullet.h"

#include "CollisionManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "ParticleSystem.h"
#include "HitEffectDirector.h"
#include "MeshEffectManager.h"
#include "VFXSequencer.h"
#include "GimmickCoin.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
// 共通の徘徊、投げ物理、叩きつけ、撃破演出で使う調整値
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
constexpr float kDefeatParticleInterval = 0.085f;
constexpr const char* kEnemyDefeatPopSequence = "enemy_defeat_pop_cue";
constexpr const char* kEnemyDefeatCoreEffect = "Resources/json/effect/effect_enemy_defeat_core_flash.json";
constexpr const char* kEnemyDefeatRingEffect = "Resources/json/effect/effect_enemy_defeat_pop_ring.json";
constexpr const char* kEnemyDropCoinModel = "Gimmicks/koin";
constexpr Vector3 kEnemyDropCoinScale = { 0.055f, 0.055f, 0.014f };
constexpr float kEnemyDropCoinWorldCollectRadius = 0.50f;
constexpr float kEnemyDropCoinLifetime = 8.0f;
constexpr float kEnemyDropCoinBlinkStart = 2.2f;
constexpr const char* kEnemyNoticeMarkModel = "GeneratedText/text3d_240c8dec";
constexpr float kEnemyNoticeDuration = 0.36f;
constexpr float kEnemyNoticeCooldown = 1.25f;
constexpr float kDamageReactionMinDuration = 0.22f;
constexpr float kDamageReactionMaxDuration = 0.34f;

float PlanarDistance(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

float PlanarLength(const Vector3& value) {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

float MaxAbsScaleComponent(const Vector3& value) {
    return (std::max)({ std::abs(value.x), std::abs(value.y), std::abs(value.z), 0.001f });
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

float NextDropRandom(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>((seed >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

int GetCoinDropCountByEnemyType(const std::string& enemyType) {
    if (enemyType == "PrismSlime") {
        return 12;
    }
    if (enemyType == "GiantSlime") {
        return 8;
    }
    if (enemyType == "FireSlime" || enemyType == "ThunderSlime" || enemyType == "WindSlime" ||
        enemyType == "Bomber" || enemyType == "BeamDrone") {
        return 4;
    }
    if (enemyType == "Bat" || enemyType == "Mushroom") {
        return 2;
    }
    return 3;
}
}

// 基本初期化
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
    hasSpawnedDefeatCoinDrops_ = false;
    wasTargetDetected_ = false;
    isNoticeReactionActive_ = false;
    noticeReactionTimer_ = 0.0f;
    noticeReactionCooldown_ = 0.0f;
    noticeMarkYaw_ = 0.0f;
    damageReactionTimer_ = 0.0f;
    damageReactionDuration_ = 0.0f;
    damageReactionStrength_ = 0.0f;
    damageReactionLocalDirection_ = { 0.0f, 0.0f, -1.0f };
    attackTelegraph_ = std::make_unique<AttackTelegraph>();
    attackTelegraph_->Initialize(common);
}

bool BaseEnemy::ReloadAttackProfile(std::string* errorMessage) {
    const std::string enemyType = GetEnemyType();
    if (enemyType.empty()) {
        if (errorMessage) {
            *errorMessage = "敵タイプが未設定のため攻撃プロファイルを読み込めません。";
        }
        return false;
    }
    return EnemyAttackProfile::LoadCachedForEnemy(enemyType, attackProfile_, errorMessage);
}

const EnemyAttackDefinition& BaseEnemy::GetAttackDefinition(const std::string& attackId) const {
    if (const EnemyAttackDefinition* attack = attackProfile_.FindAttack(attackId)) {
        return *attack;
    }

    static const EnemyAttackDefinition fallback;
    return fallback;
}

// 徘徊目標の管理
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

// 持ち上げ後に投げられた敵の物理挙動
void BaseEnemy::BeginThrown(const Vector3& initialVelocity) {
    CancelNoticeReaction();
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

// 投げ衝突の叩きつけダメージと演出
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
    const Vector3 groundImpactPosition = HitEffectDirector::ResolveGroundEffectPosition(impactPosition);
    HitEffectDirector::SpawnThrowSlamShockwave(groundImpactPosition, impactSpeed);

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
        groundImpactPosition + Vector3{ 0.0f, 0.04f, 0.0f },
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
        SetColor({
            1.0f,
            defaultColor_.y * 0.32f + 0.12f,
            defaultColor_.z * 0.32f + 0.12f,
            defaultColor_.w
        });
        if (colorResetTimer_ <= 0.0f) {
            SetColor(defaultColor_);
        }
    }

    if (damageReactionTimer_ > 0.0f) {
        damageReactionTimer_ = (std::max)(0.0f, damageReactionTimer_ - deltaTime);
    }
}

void BaseEnemy::PlayDamageReaction(Object3d* attacker, const Vector3& knockbackVelocity, float damage) {
    if (isDead || isDefeatEffectPlaying_ || isDefeatEffectFinished_) {
        return;
    }

    Vector3 worldDirection = { knockbackVelocity.x, 0.0f, knockbackVelocity.z };
    float planarLength = std::sqrt(
        worldDirection.x * worldDirection.x +
        worldDirection.z * worldDirection.z);
    if (planarLength <= 0.001f && attacker) {
        worldDirection = GetWorldPosition() - attacker->GetWorldPosition();
        worldDirection.y = 0.0f;
        planarLength = std::sqrt(
            worldDirection.x * worldDirection.x +
            worldDirection.z * worldDirection.z);
    }
    if (planarLength <= 0.001f) {
        const float yaw = GetRotation().y;
        worldDirection = { -std::sin(yaw), 0.0f, -std::cos(yaw) };
        planarLength = 1.0f;
    }
    worldDirection.x /= planarLength;
    worldDirection.z /= planarLength;

    const float yaw = GetRotation().y;
    const Vector3 localRight = { std::cos(yaw), 0.0f, -std::sin(yaw) };
    const Vector3 localForward = { std::sin(yaw), 0.0f, std::cos(yaw) };
    damageReactionLocalDirection_ = {
        worldDirection.x * localRight.x + worldDirection.z * localRight.z,
        0.0f,
        worldDirection.x * localForward.x + worldDirection.z * localForward.z
    };

    const float knockbackStrength = std::clamp(planarLength / 18.0f, 0.0f, 1.0f);
    const float damageStrength = std::clamp(damage / 24.0f, 0.0f, 1.0f);
    damageReactionStrength_ = std::clamp(
        0.72f + knockbackStrength * 0.28f + damageStrength * 0.34f,
        0.72f,
        1.30f);
    damageReactionDuration_ = kDamageReactionMinDuration +
        (kDamageReactionMaxDuration - kDamageReactionMinDuration) * damageStrength;
    damageReactionTimer_ = damageReactionDuration_;
    colorResetTimer_ = (std::max)(colorResetTimer_, 0.11f);
}

float BaseEnemy::GetDamageReactionWeight() const {
    if (damageReactionDuration_ <= 0.0001f || damageReactionTimer_ <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(damageReactionTimer_ / damageReactionDuration_, 0.0f, 1.0f);
}

EnemyVisualReactionPose BaseEnemy::GetDamageReactionPose() const {
    EnemyVisualReactionPose pose;
    if (damageReactionDuration_ <= 0.0001f || damageReactionTimer_ <= 0.0f) {
        return pose;
    }

    const float progress = 1.0f - std::clamp(
        damageReactionTimer_ / damageReactionDuration_, 0.0f, 1.0f);
    float response = 0.0f;
    if (progress < 0.18f) {
        const float t = progress / 0.18f;
        response = t * t * (3.0f - 2.0f * t);
    } else if (progress < 0.52f) {
        const float t = (progress - 0.18f) / 0.34f;
        const float eased = t * t * (3.0f - 2.0f * t);
        response = 1.0f + (-0.38f - 1.0f) * eased;
    } else {
        const float t = (progress - 0.52f) / 0.48f;
        response = -0.38f * (1.0f - t) * std::cos(t * kWanderPi * 2.0f);
    }

    const float impulse = response * damageReactionStrength_;
    const float localX = damageReactionLocalDirection_.x;
    const float localZ = damageReactionLocalDirection_.z;
    const float alongX = std::abs(localX);
    const float alongZ = std::abs(localZ);

    pose.scale.x = 1.0f - impulse * 0.14f * alongX + impulse * 0.075f * alongZ;
    pose.scale.y = 1.0f + impulse * 0.075f;
    pose.scale.z = 1.0f - impulse * 0.14f * alongZ + impulse * 0.075f * alongX;
    pose.rotation.x = -localZ * impulse * 0.13f;
    pose.rotation.z = localX * impulse * 0.13f;
    pose.offset = {
        localX * impulse * 0.055f,
        std::abs(impulse) * 0.018f,
        localZ * impulse * 0.055f
    };
    pose.weight = std::abs(impulse);
    return pose;
}

void BaseEnemy::ApplyDamageReactionPose(
    Vector3& scale,
    Vector3& rotation,
    Vector3* offset,
    float intensity) const {
    const EnemyVisualReactionPose reaction = GetDamageReactionPose();
    const float safeIntensity = (std::max)(0.0f, intensity);
    scale.x *= 1.0f + (reaction.scale.x - 1.0f) * safeIntensity;
    scale.y *= 1.0f + (reaction.scale.y - 1.0f) * safeIntensity;
    scale.z *= 1.0f + (reaction.scale.z - 1.0f) * safeIntensity;
    rotation.x += reaction.rotation.x * safeIntensity;
    rotation.y += reaction.rotation.y * safeIntensity;
    rotation.z += reaction.rotation.z * safeIntensity;
    if (offset) {
        offset->x += reaction.offset.x * safeIntensity;
        offset->y += reaction.offset.y * safeIntensity;
        offset->z += reaction.offset.z * safeIntensity;
    }
}

void BaseEnemy::EnsureNoticeMarkObject() {
    if (noticeMarkObject_ || !common_) {
        return;
    }

    noticeMarkObject_ = std::make_unique<Object3d>();
    noticeMarkObject_->Initialize(common_);
    noticeMarkObject_->SetName("__EnemyNoticeMark");
    noticeMarkObject_->SetClassName("Effect");
    noticeMarkObject_->SetModel(kEnemyNoticeMarkModel);
    noticeMarkObject_->SetTexture("Resources/sprite/common/white.png");
    noticeMarkObject_->SetMaterialType(0);
    noticeMarkObject_->SetBlendMode(BlendMode::kNormal);
    noticeMarkObject_->SetColor({ 1.0f, 0.88f, 0.16f, 1.0f });
    noticeMarkObject_->SetEmissive(4.0f);
    noticeMarkObject_->SetCollisionAttribute(0);
    noticeMarkObject_->SetCollisionMask(0);
    noticeMarkObject_->SetIsVisible(false);
}

void BaseEnemy::BeginNoticeReaction() {
    noticeBaseScale_ = GetScale();
    noticeBaseColor_ = GetColor();
    isNoticeReactionActive_ = true;
    noticeReactionTimer_ = kEnemyNoticeDuration;
    noticeReactionCooldown_ = kEnemyNoticeCooldown;
    noticeMarkYaw_ = 0.0f;

    SetVelocity({ 0.0f, 0.0f, 0.0f });
    TriggerAttackTelegraphCue({ 1.0f, 0.82f, 0.12f, 0.9f });
    EnsureNoticeMarkObject();
    if (noticeMarkObject_) {
        noticeMarkObject_->SetIsVisible(true);
    }
}

void BaseEnemy::EndNoticeReaction(bool restoreVisual) {
    if (restoreVisual && isNoticeReactionActive_) {
        SetScale(noticeBaseScale_);
    }

    isNoticeReactionActive_ = false;
    noticeReactionTimer_ = 0.0f;
    if (noticeMarkObject_) {
        noticeMarkObject_->SetIsVisible(false);
    }
}

void BaseEnemy::CancelNoticeReaction() {
    EndNoticeReaction(true);
    wasTargetDetected_ = false;
    noticeReactionCooldown_ = 0.0f;
}

void BaseEnemy::UpdateNoticeMark(float deltaTime, float progress) {
    EnsureNoticeMarkObject();
    if (!noticeMarkObject_) {
        return;
    }

    const float bodyScale = MaxAbsScaleComponent(GetScale());
    const float pop = 1.0f + std::sin((std::clamp)(progress / 0.55f, 0.0f, 1.0f) * kWanderPi) * 0.34f;
    const float hover = std::sin(progress * kWanderPi * 2.0f) * 0.18f;
    const float markScale = (std::clamp)(bodyScale * 0.34f, 0.32f, 1.15f) * pop;

    Vector3 position = GetWorldPosition();
    position.y += (std::clamp)(bodyScale * 1.45f, 1.10f, 4.80f) + hover;
    noticeMarkYaw_ += deltaTime * 1.6f;

    noticeMarkObject_->SetTranslate(position);
    noticeMarkObject_->SetScale({ markScale, markScale, markScale });
    noticeMarkObject_->SetRotation({ 0.0f, noticeMarkYaw_, 0.0f });
    noticeMarkObject_->SetColor({ 1.0f, 0.82f + std::sin(progress * kWanderPi) * 0.14f, 0.12f, 1.0f });
    noticeMarkObject_->SetIsVisible(true);
    noticeMarkObject_->UpdateLocalMatrix();
    noticeMarkObject_->UpdateWorldMatrix();
}

bool BaseEnemy::UpdateNoticeReaction(float deltaTime, float targetDistance, float detectRange, const Vector3& targetDirection) {
    (void)targetDirection;

    noticeReactionCooldown_ = (std::max)(0.0f, noticeReactionCooldown_ - deltaTime);
    const bool detected = target_ && targetDistance <= (std::max)(0.0f, detectRange);
    if (!detected) {
        wasTargetDetected_ = false;
        if (!isNoticeReactionActive_ && noticeMarkObject_) {
            noticeMarkObject_->SetIsVisible(false);
        }
        return isNoticeReactionActive_;
    }

    if (!wasTargetDetected_ && !isNoticeReactionActive_ && noticeReactionCooldown_ <= 0.0f) {
        BeginNoticeReaction();
    }
    wasTargetDetected_ = true;

    if (!isNoticeReactionActive_) {
        return false;
    }

    noticeReactionTimer_ = (std::max)(0.0f, noticeReactionTimer_ - deltaTime);
    const float progress = 1.0f - (noticeReactionTimer_ / kEnemyNoticeDuration);
    const float clampedProgress = (std::clamp)(progress, 0.0f, 1.0f);
    const float squash = std::sin(clampedProgress * kWanderPi);
    const float hopPop = std::sin(clampedProgress * kWanderPi * 2.0f) * 0.035f;

    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetScale({
        noticeBaseScale_.x * (1.0f + squash * 0.10f - hopPop),
        noticeBaseScale_.y * (1.0f + squash * 0.12f + hopPop),
        noticeBaseScale_.z * (1.0f + squash * 0.10f - hopPop)
    });
    UpdateNoticeMark(deltaTime, clampedProgress);

    if (noticeReactionTimer_ <= 0.0f) {
        EndNoticeReaction(true);
    }
    return true;
}

// HPが尽きた時の共通撃破演出
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

void BaseEnemy::SpawnDefeatCoinDrops() {
    if (hasSpawnedDefeatCoinDrops_) {
        return;
    }
    hasSpawnedDefeatCoinDrops_ = true;

    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (!scene || !scene->GetObject3dCommon()) {
        return;
    }

    const int dropCount = GetCoinDropCountByEnemyType(GetEnemyType());
    if (dropCount <= 0) {
        return;
    }

    const Vector3 basePosition = GetWorldPosition();
    const Vector3 baseScale = GetScale();
    const float rawBodyScale = (std::max)({ 0.8f, std::abs(baseScale.x), std::abs(baseScale.y), std::abs(baseScale.z) });
    const float bodyScale = GetEnemyType() == "GiantSlime" ? std::min(rawBodyScale, 3.6f) : std::min(rawBodyScale, 1.45f);
    const float groundY = basePosition.y + 0.18f;
    const float spawnY = basePosition.y + (std::max)(0.65f, bodyScale * 0.32f);

    uint32_t seed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    seed ^= static_cast<uint32_t>((std::abs(basePosition.x) + 13.0f) * 8191.0f);
    seed ^= static_cast<uint32_t>((std::abs(basePosition.z) + 7.0f) * 131071.0f);
    if (seed == 0u) {
        seed = 0xC01A5EEDu;
    }

    for (int i = 0; i < dropCount; ++i) {
        auto coin = std::make_unique<GimmickCoin>();
        coin->Initialize(scene->GetObject3dCommon(), kEnemyDropCoinModel);

        const float angleBase = (kWanderPi * 2.0f / static_cast<float>(dropCount)) * static_cast<float>(i);
        const float angle = angleBase + (NextDropRandom(seed) - 0.5f) * 0.72f;
        const float radialOffset = 0.18f + NextDropRandom(seed) * 0.24f;
        const float speed = 4.2f + NextDropRandom(seed) * 2.8f;
        const float upSpeed = 6.4f + NextDropRandom(seed) * 2.7f;

        Vector3 spawnPosition = {
            basePosition.x + std::sin(angle) * radialOffset,
            spawnY,
            basePosition.z + std::cos(angle) * radialOffset
        };
        Vector3 velocity = {
            std::sin(angle) * speed,
            upSpeed,
            std::cos(angle) * speed
        };

        coin->SetName("DropCoin_" + GetEnemyType() + "_" + std::to_string(i));
        coin->SetTranslate(spawnPosition);
        coin->SetScale(kEnemyDropCoinScale);
        coin->SetCollisionRadius(kEnemyDropCoinWorldCollectRadius / kEnemyDropCoinScale.x);
        coin->ConfigureTemporaryDrop(velocity, kEnemyDropCoinLifetime, kEnemyDropCoinBlinkStart, groundY);
        scene->AddObject(std::move(coin));
    }
}

void BaseEnemy::BeginDefeatEffect() {
    CancelNoticeReaction();
    isDefeatEffectPlaying_ = true;
    isDefeatEffectFinished_ = false;
    SpawnDefeatCoinDrops();
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

    Vector3 cuePosition = GetWorldPosition();
    cuePosition.y += (std::max)(0.42f, defeatBaseScale_.y * 0.48f);
    VFXSequencer::PlayOneShot(kEnemyDefeatPopSequence, cuePosition);

    const float rawBodyScale = (std::max)({ 0.9f, std::abs(defeatBaseScale_.x), std::abs(defeatBaseScale_.y), std::abs(defeatBaseScale_.z) });
    const float bodyScale = GetEnemyType() == "GiantSlime" ? std::min(rawBodyScale, 3.6f) : std::min(rawBodyScale, 1.45f);
    if (auto* meshEffect = MeshEffectManager::GetInstance()) {
        meshEffect->SpawnEffectAt(kEnemyDefeatCoreEffect, cuePosition, { 0.0f, 0.0f, 0.0f }, { bodyScale, bodyScale, bodyScale });

        Vector3 ringPosition = GetWorldPosition();
        ringPosition.y += (std::max)(0.12f, defeatBaseScale_.y * 0.12f);
        meshEffect->SpawnEffectAt(kEnemyDefeatRingEffect, ringPosition, { 0.0f, 0.0f, 0.0f }, { bodyScale, bodyScale, bodyScale });
    }

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
            const int defeatEventID = GetTargetID();
            if (defeatEventID > 0) {
                scene->TriggerEvent(defeatEventID);
            }
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
    const float rawBodyScale = (std::max)({ 0.9f, std::abs(defeatBaseScale_.x), std::abs(defeatBaseScale_.y), std::abs(defeatBaseScale_.z) });
    const float bodyScale = GetEnemyType() == "GiantSlime" ? std::min(rawBodyScale, 3.6f) : std::min(rawBodyScale, 1.45f);

    const Vector3 smokeOffsets[] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.42f, 0.08f, 0.10f },
        { -0.38f, 0.16f, -0.08f },
        { 0.12f, 0.34f, -0.36f },
        { -0.18f, 0.52f, 0.30f }
    };

    for (const Vector3& offset : smokeOffsets) {
        Vector3 smokePos = center + offset * bodyScale;
        particleSystem->SpawnParticles(
            smokePos,
            8,
            0.85f + offset.y * 0.28f,
            &up,
            0.9f,
            { 1.0f, 0.96f, 0.82f, 0.90f },
            { 0.74f, 0.72f, 0.60f, 0.0f },
            0.46f,
            0.82f,
            0.34f * bodyScale,
            1.05f * bodyScale
        );
    }

    particleSystem->SpawnParticles(
        center,
        30,
        4.9f,
        nullptr,
        0.0f,
        { 1.0f, 0.86f, 0.24f, 1.0f },
        { 0.52f, 1.0f, 1.0f, 0.0f },
        0.22f,
        0.62f,
        0.22f * bodyScale,
        0.02f
    );
    particleSystem->SpawnParticles(
        center,
        16,
        2.4f,
        &up,
        1.0f,
        { 1.0f, 1.0f, 0.92f, 0.95f },
        { 1.0f, 0.72f, 0.20f, 0.0f },
        0.18f,
        0.48f,
        0.20f * bodyScale,
        0.0f
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
    const float rawBodyScale = (std::max)({ 0.9f, std::abs(defeatBaseScale_.x), std::abs(defeatBaseScale_.y), std::abs(defeatBaseScale_.z) });
    const float bodyScale = GetEnemyType() == "GiantSlime" ? std::min(rawBodyScale, 3.6f) : std::min(rawBodyScale, 1.45f);
    particleSystem->SpawnParticles(
        center,
        7,
        1.05f + progress * 0.95f,
        &up,
        0.78f,
        { 1.0f, 0.96f, 0.82f, 0.72f },
        { 0.72f, 0.72f, 0.62f, 0.0f },
        0.28f,
        0.62f,
        0.24f * bodyScale,
        0.72f * bodyScale
    );
    particleSystem->SpawnParticles(
        center,
        5,
        2.2f + progress * 0.7f,
        nullptr,
        0.0f,
        { 0.75f, 1.0f, 0.98f, 0.86f },
        { 1.0f, 0.44f, 0.92f, 0.0f },
        0.14f,
        0.32f,
        0.12f * bodyScale,
        0.0f
    );
}

void BaseEnemy::SetDormant(bool dormant) {
    isDormant_ = dormant;
    if (dormant) {
        HideAttackTelegraph();
        CancelNoticeReaction();
        SetVelocity({ 0.0f, 0.0f, 0.0f });
    }
}

// 全敵共通の最終更新。固有AIの後に呼ばれる想定。
void BaseEnemy::Update(float deltaTime) {
    if (isDormant_) {
        HideAttackTelegraph();
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        Object3d::Update(deltaTime);
        return;
    }

    if (attackTelegraph_) {
        attackTelegraph_->Update(deltaTime);
    }

    if (ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        if (!isDefeatEffectPlaying_) {
            BeginDefeatEffect();
        }
        UpdateDefeatEffect(deltaTime);
        Object3d::Update(deltaTime);
        return;
    }

    if (isCarried_) {
        HideAttackTelegraph();
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

    // Nav Agentを追加した敵だけ、既存AIが決めた速さを保ったままA*経路方向へ補正します。
    // Component未追加の既存敵には一切影響しません。
    if (target_) {
        if (NavAgentComponent* navAgent = GetNavAgentComponent(); navAgent && navAgent->IsEnabled()) {
            Vector3 velocity = GetVelocity();
            const float planarSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
            if (planarSpeed > 0.01f) {
                const Vector3 direction = navAgent->CalculateDirection(
                    this, GetWorldPosition(), target_->GetWorldPosition(), deltaTime);
                velocity.x = direction.x * planarSpeed;
                velocity.z = direction.z * planarSpeed;
                SetVelocity(velocity);
            }
        }
    }

    Character::Update(deltaTime);
    UpdateDamageFeedbackTimers(deltaTime);
}

void BaseEnemy::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (attackTelegraph_) {
        attackTelegraph_->DrawGround(pointLightResource, spotLightResource);
    }
    Character::Draw(pointLightResource, spotLightResource);
    if (noticeMarkObject_ && noticeMarkObject_->GetIsVisible()) {
        noticeMarkObject_->Draw(pointLightResource, spotLightResource);
    }
    if (attackTelegraph_) {
        attackTelegraph_->DrawWarning(pointLightResource, spotLightResource);
    }
}

void BaseEnemy::ShowAttackTelegraphCircle(const Vector3& center, float radius, float progress, const Vector4& color) {
    if (!attackTelegraph_ && common_) {
        attackTelegraph_ = std::make_unique<AttackTelegraph>();
        attackTelegraph_->Initialize(common_);
    }
    if (attackTelegraph_) {
        attackTelegraph_->ShowCircle(center, radius, progress, color);
    }
}

void BaseEnemy::ShowAttackTelegraphDecalCircle(
    const Vector3& center,
    float radius,
    float progress,
    const Vector4& color,
    const std::string& texturePath) {
    if (!attackTelegraph_ && common_) {
        attackTelegraph_ = std::make_unique<AttackTelegraph>();
        attackTelegraph_->Initialize(common_);
    }
    if (attackTelegraph_) {
        attackTelegraph_->ShowDecalCircle(center, radius, progress, color, texturePath);
    }
}

void BaseEnemy::ShowAttackTelegraphLine(const Vector3& center, const Vector3& direction, float length, float width, float progress, const Vector4& color) {
    if (!attackTelegraph_ && common_) {
        attackTelegraph_ = std::make_unique<AttackTelegraph>();
        attackTelegraph_->Initialize(common_);
    }
    if (attackTelegraph_) {
        attackTelegraph_->ShowLine(center, direction, length, width, progress, color);
    }
}

void BaseEnemy::TriggerAttackTelegraphCue(const Vector4& color) {
    if (!attackTelegraph_ && common_) {
        attackTelegraph_ = std::make_unique<AttackTelegraph>();
        attackTelegraph_->Initialize(common_);
    }
    if (attackTelegraph_) {
        const Vector3 scale = GetScale();
        const float horizontalScale = (std::max)({ 0.75f, std::abs(scale.x), std::abs(scale.z) });
        const float verticalScale = (std::max)(0.75f, std::abs(scale.y));
        // 雷スライムなど横潰れする敵は現在Scaleが極端に広がるため、見た目上の体格へ補正します。
        const float bodyScale = (std::min)(horizontalScale, (std::max)(1.5f, verticalScale * 2.4f));
        const Vector3 bodyOffset = { 0.0f, (std::clamp)(verticalScale * 0.62f, 0.55f, 3.8f), 0.0f };

        // 床の範囲強調と、敵本体の入力タイミング表示は別レイヤーとして同時に再生します。
        if (attackTelegraph_->IsVisible()) {
            attackTelegraph_->TriggerCue(color);
        }
        attackTelegraph_->TriggerWarningCue(
            this,
            bodyOffset,
            (std::clamp)(bodyScale * 1.18f, 1.0f, 5.2f),
            color);
    }
}

void BaseEnemy::ShowAttackTelegraphCone(const Vector3& origin, const Vector3& direction, float length, float startWidth, float endWidth, float progress, const Vector4& color) {
    if (!attackTelegraph_ && common_) {
        attackTelegraph_ = std::make_unique<AttackTelegraph>();
        attackTelegraph_->Initialize(common_);
    }
    if (attackTelegraph_) {
        attackTelegraph_->ShowCone(origin, direction, length, startWidth, endWidth, progress, color);
    }
}

void BaseEnemy::ShowAttackTelegraphImpactAreas(const Vector3* centers, std::size_t centerCount, float radius, float progress, const Vector4& color) {
    if (!attackTelegraph_ && common_) {
        attackTelegraph_ = std::make_unique<AttackTelegraph>();
        attackTelegraph_->Initialize(common_);
    }
    if (attackTelegraph_) {
        attackTelegraph_->ShowImpactAreas(centers, centerCount, radius, progress, color);
    }
}

void BaseEnemy::ShowAttackTelegraphLaneFan(const Vector3& origin, const Vector3& direction, float length, float width, int laneCount, float lateralSpacing, float angleStep, float progress, const Vector4& color) {
    if (!attackTelegraph_ && common_) {
        attackTelegraph_ = std::make_unique<AttackTelegraph>();
        attackTelegraph_->Initialize(common_);
    }
    if (attackTelegraph_) {
        attackTelegraph_->ShowLaneFan(origin, direction, length, width, laneCount, lateralSpacing, angleStep, progress, color);
    }
}

void BaseEnemy::HideAttackTelegraph() {
    if (attackTelegraph_) {
        attackTelegraph_->Hide();
    }
}

bool BaseEnemy::OnCollision(Object3d* other) {
    if (isDormant_ || !other) {
        return false;
    }

    if (isCarried_) {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        return false;
    }

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
        if (const Bullet* attackBullet = dynamic_cast<const Bullet*>(other)) {
            dmgEvent.damageAmount = attackBullet->GetDamage();
            dmgEvent.damageType = attackBullet->GetDamageType();
            dmgEvent.statusEffect = attackBullet->GetStatusEffect();
        }
        else {
            dmgEvent.damageAmount = 10.0f;
        }
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
        CancelNoticeReaction();
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
