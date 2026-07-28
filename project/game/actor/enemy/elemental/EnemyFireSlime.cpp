#include "EnemyFireSlime.h"
#include "SlimeBounceAnimator.h"
#include "BulletManager.h"
#include "Camera.h"
#include "CameraManager.h"
#include "CollisionConfig.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "Player.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

namespace {
// ブレス、火球、頭上炎の発生間隔をまとめる調整値
constexpr const char* kBreathAttackId = "flame_breath";
constexpr const char* kFireballAttackId = "fireball";
constexpr float kCarriedFireCooldown = 0.58f;
constexpr float kCarriedBreathDamageInterval = 0.22f;
constexpr float kCarriedFireballSpeed = 31.0f;
constexpr float kGroundCollisionWorldRadius = 0.82f;
constexpr float kThrownCollisionWorldRadius = 1.18f;
constexpr float kMoveHopInterval = 0.28f;
constexpr float kMoveHopPower = 4.7f;
constexpr float kFireSlimeModelYawOffset = 3.1415926535f;
constexpr float kHeadFlameHeight = 0.62f;
constexpr float kHeadFlameBaseScale = 0.78f;
constexpr int kHeadFlameMaterialType = 11;
constexpr float kHeadFlameEffectType = 2.0f;
constexpr float kBreathFlameEffectType = 3.0f;
constexpr int kBreathFlameVisualCount = 7;
constexpr const char* kBreathPreset = "fire_slime_breath";
constexpr const char* kBreathEmberPreset = "fire_slime_breath_embers";
constexpr const char* kCastPreset = "fire_slime_cast";
constexpr const char* kHeadFlamePreset = "fire_slime_head_flame";
constexpr const char* kHeadEmberPreset = "fire_slime_head_embers";

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}

BulletVisualConfig MakeFireVisual(float scale) {
    BulletVisualConfig visual;
    visual.materialType = 11;
    visual.blendMode = BlendMode::kNormal;
    visual.color = { 1.0f, 0.34f, 0.07f, 0.96f };
    visual.emissive = 2.8f;
    visual.visualScale = scale;
    visual.effectType = 1.0f;
    visual.effectScale = 1.15f;
    visual.effectSoftness = 0.42f;
    visual.effectIntensity = 0.96f;
    visual.billboardScale = 0.68f;
    return visual;
}

StatusEffectApplication MakeStatusEffect(const EnemyAttackDefinition& attack) {
    StatusEffectApplication status;
    if (attack.statusEffectType == "burning") {
        status.type = StatusEffectType::Burning;
    }
    status.duration = attack.statusDuration;
    status.tickInterval = attack.statusTickInterval;
    status.tickDamage = attack.statusTickDamage;
    status.vfxPreset = attack.statusVfx;
    return status;
}
}

// 炎スライムの初期化
void EnemyFireSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_FireSlime");
    SetEnemyType("FireSlime");
    ReloadAttackProfile();
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    defaultColor_ = GetColor();

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SyncGroundCollisionRadius();
}

// 近距離ブレスと中距離火球を切り替えるAI
void EnemyFireSlime::Update(float deltaTime) {
    if (UpdateInactiveState(deltaTime)) {
        return;
    }

    idleTimer_ += deltaTime;
    UpdateHeadFlame(deltaTime);
    if (UpdateThrowRecoveryState(deltaTime)) {
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
}

bool EnemyFireSlime::UpdateInactiveState(float deltaTime) {
    if (isCarried_) {
        HideAttackTelegraph();
        idleTimer_ += deltaTime;
        return true;
    }

    if (isDead || !GetIsVisible()) {
        HideAttackTelegraph();
        breathTimer_ = 0.0f;
        fireballWindupTimer_ = 0.0f;
        attackTimer_ = 0.0f;
        breathWarningTriggered_ = false;
        fireballWarningTriggered_ = false;
        fireballAimLocked_ = false;
        RequestRemoveHeadFlameVisual();
        RequestRemoveBreathFlameVisuals();
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        return true;
    }

    if (ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        RequestRemoveHeadFlameVisual();
        RequestRemoveBreathFlameVisuals();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    return false;
}

bool EnemyFireSlime::UpdateThrowRecoveryState(float deltaTime) {
    if (IsThrowRecovering()) {
        if (IsThrownPhysics()) {
            SyncThrownCollisionRadius();
        } else {
            SyncGroundCollisionRadius();
        }
        BaseEnemy::Update(deltaTime);
        return true;
    }
    return false;
}

void EnemyFireSlime::EnsureBaseScale() {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyFireSlime::UpdateWildTimers(float deltaTime) {
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);
}

void EnemyFireSlime::UpdateWildBehavior(float deltaTime, Vector3& velocity) {
    if (target_ && param_.has_value()) {
        Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        Vector3 direction = NormalizePlanar(toTarget);
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

        UpdateFacing(direction);
        UpdateCombatBehavior(deltaTime, velocity, direction, distance);
    }
}

void EnemyFireSlime::UpdateCombatBehavior(float deltaTime, Vector3& velocity, const Vector3& direction, float distance) {
    const EnemyAttackDefinition& breathAttack = GetAttackDefinition(kBreathAttackId);
    const EnemyAttackDefinition& fireballAttack = GetAttackDefinition(kFireballAttackId);
    if (breathTimer_ <= 0.0f && fireballWindupTimer_ <= 0.0f && UpdateNoticeReaction(deltaTime, distance, detectionRange_, direction)) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateFacing(direction);
        return;
    }

    if (fireballWindupTimer_ > 0.0f) {
        UpdateFireballWindup(deltaTime, velocity, direction, distance);
    }
    else if (breathTimer_ > 0.0f) {
        UpdateBreath(deltaTime, direction, distance);
    }
    else if (distance >= breathAttack.minRange && distance <= breathAttack.maxRange && attackCooldown_ <= 0.0f) {
        StartBreath();
    }
    else if (distance <= (std::min)(detectionRange_, fireballAttack.maxRange) && distance >= fireballAttack.minRange && attackCooldown_ <= 0.0f) {
        StartFireballWindup(direction, distance);
    }
    else if (distance <= detectionRange_) {
        const float speed = (std::max)(0.0f, param_->speed);
        const float approach = distance > breathAttack.maxRange * 0.78f ? 1.0f : -0.55f;
        velocity.x = direction.x * speed * approach;
        velocity.z = direction.z * speed * approach;
    }
    else {
        UpdateWanderBehavior(deltaTime, velocity);
    }
}

void EnemyFireSlime::UpdateWanderBehavior(float deltaTime, Vector3& velocity) {
    const float speed = (std::max)(0.55f, param_->speed * 0.42f);
    velocity = CalculateWanderVelocity(deltaTime, speed, 0.72f);
    UpdateFacing({ velocity.x, 0.0f, velocity.z });
}

void EnemyFireSlime::ApplyGroundMovementAndAnimation(float deltaTime, Vector3& velocity) {
    velocity.y = (std::min)(GetVelocity().y, 0.0f);
    if (breathTimer_ <= 0.0f && SlimeBounceAnimator::StepGroundHop(groundHopTimer_, velocity, isGrounded_, deltaTime, kMoveHopInterval, 0.10f)) {
        velocity.y = (std::max)(velocity.y, kMoveHopPower);
    }
    SetVelocity(velocity);
    ApplySlimeAnimation(deltaTime);
    SyncGroundCollisionRadius();
}

void EnemyFireSlime::BeginThrown(const Vector3& initialVelocity) {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        const float capturedMaxScale = (std::max)({ std::abs(baseScale_.x), std::abs(baseScale_.y), std::abs(baseScale_.z) });
        if (capturedMaxScale < 1.2f) {
            baseScale_ = { 2.0f, 2.0f, 2.0f };
        }
        hasBaseScale_ = true;
    }

    breathTimer_ = 0.0f;
    fireballWindupTimer_ = 0.0f;
    breathParticleTimer_ = 0.0f;
    breathEmberTimer_ = 0.0f;
    breathParticleCursor_ = 0;
    attackTimer_ = 0.0f;
    breathDamageDone_ = false;
    breathWarningTriggered_ = false;
    fireballWarningTriggered_ = false;
    fireballAimLocked_ = false;
    HideAttackTelegraph();
    HideBreathFlameVisuals();
    SetColor(defaultColor_);
    SetScale(baseScale_);
    SyncThrownCollisionRadius();
    BaseEnemy::BeginThrown(initialVelocity);
}

std::unique_ptr<Object3d> EnemyFireSlime::Clone() const {
    auto clone = std::make_unique<EnemyFireSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

// 持ち運び中にプレイヤー前方へ火球を撃つ能力
void EnemyFireSlime::ExecuteAbility(Player* player) {
    if (!player || !isCarried_ || carriedFireCooldown_ > 0.0f) {
        return;
    }

    Vector3 direction = player->GetForwardDirection();
    if (Math::Length(direction) <= 0.001f) {
        direction = { 0.0f, 0.0f, 1.0f };
    }
    direction = Math::Normalize(direction);

    Vector3 spawnPos = player->GetWorldPosition() + direction * 1.75f;
    spawnPos.y += 1.35f;

    const EnemyAttackDefinition& fireballAttack = GetAttackDefinition(kFireballAttackId);
    const float carriedSpeed = fireballAttack.maxSpeed > 0.0f ? fireballAttack.maxSpeed : kCarriedFireballSpeed;
    BulletManager::GetInstance()->Fire(
        spawnPos,
        direction * carriedSpeed + Vector3{ 0.0f, 1.4f, 0.0f },
        kPlayerAttack,
        kEnemy | kAllSolid,
        "Primitives/sphere",
        0.52f,
        fireballAttack.lifetime,
        MakeFireVisual(1.28f),
        fireballAttack.damage,
        MakeStatusEffect(fireballAttack),
        DamageType::Fire);

    EmitFirePreset(fireballAttack.activeVfx.empty() ? kCastPreset : fireballAttack.activeVfx.c_str(), spawnPos);
    carriedFireCooldown_ = kCarriedFireCooldown;
    carriedEffectTimer_ = 0.22f;
}

void EnemyFireSlime::ExecuteBreathAbility(Player* player) {
    if (!player || !isCarried_) {
        return;
    }

    const float activeDuration = GetAttackDefinition(kBreathAttackId).activeDuration;
    breathTimer_ = (std::max)(breathTimer_, activeDuration);
    breathParticleTimer_ = 0.0f;
    breathEmberTimer_ = 0.0f;
    breathParticleCursor_ = 0;
    carriedEffectTimer_ = 0.12f;
    attackTimer_ = activeDuration;
    SetColor({ 1.0f, 0.76f, 0.54f, 1.0f });
}

void EnemyFireSlime::UpdateCarriedAbility(Player* player, float deltaTime) {
    if (!isCarried_) {
        return;
    }

    idleTimer_ += deltaTime;
    carriedFireCooldown_ = (std::max)(0.0f, carriedFireCooldown_ - deltaTime);
    carriedEffectTimer_ = (std::max)(0.0f, carriedEffectTimer_ - deltaTime);
    carriedBreathDamageTimer_ = (std::max)(0.0f, carriedBreathDamageTimer_ - deltaTime);

    if (player) {
        Vector3 direction = player->GetForwardDirection();
        if (Math::Length(direction) <= 0.001f) {
            direction = { 0.0f, 0.0f, 1.0f };
        }
        direction = Math::Normalize(direction);

        SetVelocity(player->GetVelocity());
        SetRotationY(std::atan2(direction.x, direction.z) + kFireSlimeModelYawOffset);
        UpdateHeadFlameVisual(deltaTime);

        if (breathTimer_ > 0.0f) {
            breathTimer_ = (std::max)(0.0f, breathTimer_ - deltaTime);
            breathParticleTimer_ -= deltaTime;
            breathEmberTimer_ -= deltaTime;
            UpdateBreathFlameVisuals(direction, deltaTime);

            if (carriedBreathDamageTimer_ <= 0.0f) {
                DispatchCarriedBreathDamage(player, direction);
                carriedBreathDamageTimer_ = kCarriedBreathDamageInterval;
            }

            if (breathParticleTimer_ <= 0.0f) {
                Vector3 origin = player->GetWorldPosition();
                origin.y += 0.78f;
                EmitBreathParticles(origin, direction);
                breathParticleTimer_ += 0.065f;
            }

            if (breathTimer_ <= 0.0f) {
                HideBreathFlameVisuals();
            }
        } else {
            HideBreathFlameVisuals();
        }
    }

    const float ready = 1.0f - (std::clamp)(carriedFireCooldown_ / kCarriedFireCooldown, 0.0f, 1.0f);
    const float pulse = carriedEffectTimer_ > 0.0f ? std::sin(carriedEffectTimer_ * 50.0f) * 0.08f : 0.0f;
    SetColor({ 1.0f, 0.82f + ready * 0.10f + pulse, 0.72f, 1.0f });
}

void EnemyFireSlime::ReleaseCarriedAbilityVisuals() {
    HideAttackTelegraph();
    HideBreathFlameVisuals();
    RequestRemoveBreathFlameVisuals();
    RequestRemoveHeadFlameVisual();
}

// ブレス攻撃、火球、炎演出の補助処理
void EnemyFireSlime::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.0001f) return;
    const float targetYaw = std::atan2(direction.x, direction.z) + kFireSlimeModelYawOffset;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.16f));
}

void EnemyFireSlime::StartBreath() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    breathTimer_ = attack.activeDuration;
    breathParticleTimer_ = 0.0f;
    breathEmberTimer_ = 0.0f;
    breathParticleCursor_ = 0;
    breathDamageDone_ = false;
    breathWarningTriggered_ = false;
    attackCooldown_ = attack.cooldown;
    attackTimer_ = attack.activeDuration;
    SetColor({ 1.0f, 0.82f, 0.62f, 1.0f });
}

void EnemyFireSlime::UpdateBreath(float deltaTime, const Vector3& direction, float distance) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    const float activeDuration = (std::max)(0.01f, attack.activeDuration);
    breathTimer_ = (std::max)(0.0f, breathTimer_ - deltaTime);
    breathParticleTimer_ -= deltaTime;
    breathEmberTimer_ -= deltaTime;
    const float progress = 1.0f - (std::clamp)(breathTimer_ / activeDuration, 0.0f, 1.0f);
    ShowAttackTelegraphLine(
        GetTranslate(),
        direction,
        attack.maxRange + 0.55f,
        attack.radius,
        progress,
        { 1.0f, 0.30f, 0.05f, 0.78f });
    UpdateBreathFlameVisuals(direction, deltaTime);

    const float damageTriggerTime = activeDuration * 0.62f;
    if (!breathWarningTriggered_ && breathTimer_ <= damageTriggerTime + attack.warningLeadTime) {
        TriggerAttackTelegraphCue({ 1.0f, 0.28f, 0.04f, 1.0f });
        breathWarningTriggered_ = true;
    }

    if (!breathDamageDone_ && breathTimer_ <= damageTriggerTime) {
        DispatchBreathDamage(direction, distance);
        breathDamageDone_ = true;
    }

    if (breathParticleTimer_ <= 0.0f) {
        Vector3 origin = GetTranslate();
        origin.y += 0.72f;
        EmitBreathParticles(origin, direction);
        breathParticleTimer_ += 0.065f;
    }

    if (breathTimer_ <= 0.0f) {
        HideAttackTelegraph();
        SetColor(defaultColor_);
        HideBreathFlameVisuals();
    }
}

void EnemyFireSlime::StartFireballWindup(const Vector3& direction, float distance) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kFireballAttackId);
    fireballWindupTimer_ = (std::max)(0.01f, attack.windupDuration);
    pendingFireballDirection_ = direction;
    pendingFireballDistance_ = distance;
    fireballWarningTriggered_ = false;
    fireballAimLocked_ = false;
    attackTimer_ = fireballWindupTimer_;
    SetColor({ 1.0f, 0.72f, 0.48f, 1.0f });
}

void EnemyFireSlime::UpdateFireballWindup(float deltaTime, Vector3& velocity, const Vector3& direction, float distance) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kFireballAttackId);
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    if (!fireballAimLocked_) {
        pendingFireballDirection_ = direction;
        pendingFireballDistance_ = distance;
        UpdateFacing(direction);
    }

    fireballWindupTimer_ = (std::max)(0.0f, fireballWindupTimer_ - deltaTime);
    if (!fireballWarningTriggered_ && fireballWindupTimer_ <= attack.warningLeadTime) {
        TriggerAttackTelegraphCue({ 1.0f, 0.32f, 0.04f, 1.0f });
        fireballWarningTriggered_ = true;
        fireballAimLocked_ = true;
    }

    if (fireballWindupTimer_ > 0.0f) {
        return;
    }

    FireFireball(pendingFireballDirection_, pendingFireballDistance_);
    attackCooldown_ = attack.cooldown;
    attackTimer_ = attack.recoveryDuration;
    fireballAimLocked_ = false;
    SetColor(defaultColor_);
}

void EnemyFireSlime::DispatchBreathDamage(const Vector3& direction, float distance) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    if (!target_ || distance > attack.maxRange + 0.55f) {
        return;
    }

    Vector3 toTarget = NormalizePlanar(target_->GetTranslate() - GetTranslate());
    const float facingDot = direction.x * toTarget.x + direction.z * toTarget.z;
    if (facingDot < 0.34f) {
        return;
    }

    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = attack.damage;
    damageEvent.knockbackVelocity = { direction.x * 9.5f, 4.4f, direction.z * 9.5f };
    damageEvent.damageType = DamageType::Fire;
    damageEvent.statusEffect = MakeStatusEffect(attack);
    EventManager::GetInstance()->Dispatch(damageEvent);
}

void EnemyFireSlime::DispatchCarriedBreathDamage(Player* player, const Vector3& direction) {
    if (!player) {
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (!scene) {
        return;
    }

    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    const Vector3 origin = player->GetWorldPosition();
    for (const auto& object : scene->GetObjects()) {
        Object3d* target = object.get();
        if (!target || target == this || target == player || target->isDead || !target->GetIsVisible()) {
            continue;
        }

        BaseEnemy* enemy = dynamic_cast<BaseEnemy*>(target);
        if (!enemy || enemy->IsCarried() || enemy->IsDefeatEffectPlaying()) {
            continue;
        }

        Vector3 toTarget = target->GetTranslate() - origin;
        toTarget.y = 0.0f;
        const float distance = Math::Length(toTarget);
        if (distance > attack.maxRange + 0.85f || distance <= 0.001f) {
            continue;
        }

        const Vector3 targetDirection = Math::Normalize(toTarget);
        const float facingDot = direction.x * targetDirection.x + direction.z * targetDirection.z;
        if (facingDot < 0.28f) {
            continue;
        }

        DamageEvent damageEvent;
        damageEvent.target = target;
        damageEvent.attacker = player;
        damageEvent.damageAmount = attack.damage;
        damageEvent.knockbackVelocity = { direction.x * 8.2f, 3.8f, direction.z * 8.2f };
        damageEvent.damageType = DamageType::Fire;
        damageEvent.statusEffect = MakeStatusEffect(attack);
        EventManager::GetInstance()->Dispatch(damageEvent);
    }
}

void EnemyFireSlime::FireFireball(const Vector3& direction, float distance) {
    if (!target_) return;

    Vector3 spawnPos = GetTranslate() + direction * 0.65f;
    spawnPos.y += 1.05f;

    Vector3 aim = target_->GetTranslate() - spawnPos;
    aim.y += 0.35f;
    if (Math::Length(aim) <= 0.001f) {
        aim = { direction.x, 0.12f, direction.z };
    }
    aim = Math::Normalize(aim);

    const EnemyAttackDefinition& attack = GetAttackDefinition(kFireballAttackId);
    const float speed = std::clamp(distance * 1.65f, attack.minSpeed, attack.maxSpeed);
    BulletManager::GetInstance()->Fire(
        spawnPos,
        aim * speed,
        kEnemyAttack,
        kPlayer | kAllSolid,
        "Primitives/sphere",
        0.5f,
        attack.lifetime,
        MakeFireVisual(1.2f),
        attack.damage,
        MakeStatusEffect(attack),
        DamageType::Fire);

    EmitFirePreset(attack.activeVfx.empty() ? kCastPreset : attack.activeVfx.c_str(), spawnPos);
}

void EnemyFireSlime::UpdateHeadFlame(float deltaTime) {
    UpdateHeadFlameVisual(deltaTime);
}

void EnemyFireSlime::EnsureHeadFlameVisual() {
    if (headFlameVisual_ || headFlameRemoveRequested_) {
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (!scene || !scene->GetObject3dCommon()) {
        return;
    }

    auto flame = std::make_unique<Object3d>();
    flame->Initialize(scene->GetObject3dCommon());
    flame->SetName(GetName() + "_HeadFlameShader");
    flame->SetClassName("Effect");
    flame->SetModel("Primitives/sphere");
    flame->SetColliderType(ColliderType::kNone);
    flame->SetCollisionAttribute(0);
    flame->SetCollisionMask(0);
    flame->SetBlendMode(BlendMode::kNormal);
    flame->SetMaterialType(kHeadFlameMaterialType);
    flame->SetSelectedLighting(0);
    flame->SetEnableLighting(false);
    flame->SetColor({ 1.0f, 0.24f, 0.04f, 0.82f });
    flame->SetEmissive(1.7f);

    if (auto* renderer = flame->GetMeshRenderer()) {
        if (auto* water = renderer->GetWaterParamData()) {
            water->effectType = kHeadFlameEffectType;
            water->waveSpeed = 1.75f;
            water->effectScale = 0.68f;
            water->effectSoftness = 0.72f;
            water->effectIntensity = 0.84f;
            water->billboardScale = 0.58f;
            water->effectScaleX = 0.92f;
            water->effectScaleY = 0.78f;
            water->uvOffsetX = 2.73f;
            water->uvOffsetY = 5.17f;
        }
    }

    headFlameVisual_ = flame.get();
    scene->AddObject(std::move(flame));
}

void EnemyFireSlime::UpdateHeadFlameVisual(float deltaTime) {
    EnsureHeadFlameVisual();
    if (!headFlameVisual_) {
        return;
    }

    const Vector3 scale = GetScale();
    const float bodyScale = (std::max)({ 0.7f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    Vector3 velocity = GetVelocity();
    velocity.y = 0.0f;
    const float velocityResponse = 1.0f - std::exp(-(std::max)(0.0f, deltaTime) * 9.0f);
    smoothedFlameVelocity_ = Math::Lerp(smoothedFlameVelocity_, velocity, velocityResponse);
    const float moveSpeed = Math::Length(smoothedFlameVelocity_);
    Vector3 pos = GetTranslate();
    pos.y += kHeadFlameHeight * (std::max)(0.65f, std::abs(scale.y));
    if (moveSpeed > 0.001f) {
        const Vector3 trailDirection = Math::Normalize(smoothedFlameVelocity_) * -1.0f;
        pos = pos + trailDirection * (std::min)(0.32f, moveSpeed * 0.022f);
    }

    float yaw = GetRotation().y;
    float flowX = 0.0f;
    Camera* camera = CameraManager::GetInstance() ? CameraManager::GetInstance()->GetActiveCamera() : nullptr;
    if (camera) {
        const Vector3 toCamera = camera->GetEye() - pos;
        if (std::abs(toCamera.x) + std::abs(toCamera.z) > 0.001f) {
            yaw = std::atan2(toCamera.x, toCamera.z);

            Vector3 viewForward = Math::Normalize(toCamera);
            Vector3 upSeed = std::abs(viewForward.y) > 0.96f ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
            Vector3 viewRight = Math::Normalize(Math::Cross(upSeed, viewForward));
            flowX = (std::clamp)(Math::Dot(smoothedFlameVelocity_, viewRight) * 0.12f, -1.0f, 1.0f);
        }
    }

    const float pulse = std::sin(idleTimer_ * 8.0f) * 0.06f;
    const float speedRate = (std::clamp)(moveSpeed * 0.055f, 0.0f, 1.0f);
    const float width = kHeadFlameBaseScale * (0.94f + pulse + speedRate * 0.06f) * bodyScale;
    const float height = kHeadFlameBaseScale * (0.82f - pulse * 0.16f + speedRate * 0.08f) * bodyScale;

    headFlameVisual_->SetIsVisible((GetIsVisible() && !isDead) || isCarried_);
    headFlameVisual_->SetTranslate(pos);
    headFlameVisual_->SetRotation({ 0.0f, yaw, 0.0f });
    headFlameVisual_->SetScale({ width, height, width });
    headFlameVisual_->SetColor({ 1.0f, 0.24f + std::sin(idleTimer_ * 5.3f) * 0.035f, 0.04f, 0.80f });

    if (auto* renderer = headFlameVisual_->GetMeshRenderer()) {
        if (auto* water = renderer->GetWaterParamData()) {
            water->flowSpeedX = flowX;
            water->flowSpeedY = speedRate;
            water->waveSpeed = 1.75f + std::sin(idleTimer_ * 1.7f) * 0.16f + speedRate * 0.42f;
            water->effectIntensity = 0.82f + std::sin(idleTimer_ * 4.1f) * 0.045f + speedRate * 0.07f;
            water->effectScaleX = 0.92f + speedRate * 0.10f;
            water->effectScaleY = 0.78f + speedRate * 0.08f;
        }
    }

    headFlameVisual_->UpdateLocalMatrix();
    headFlameVisual_->UpdateWorldMatrix();

    headFlameParticleTimer_ -= deltaTime;
    headEmberParticleTimer_ -= deltaTime;
    if (headFlameVisual_->GetIsVisible()) {
        Vector3 particleDirection = { -smoothedFlameVelocity_.x * 0.075f, 0.78f, -smoothedFlameVelocity_.z * 0.075f };
        if (headFlameParticleTimer_ <= 0.0f) {
            constexpr float kGoldenAngle = 2.39996323f;
            const float phase = idleTimer_ * 2.8f + static_cast<float>(headFlameParticleCursor_) * kGoldenAngle;
            const float ringRadius = bodyScale * (0.22f + 0.04f * std::sin(phase * 1.7f));
            Vector3 emitPosition = pos;
            emitPosition.x += std::cos(phase) * ringRadius;
            emitPosition.z += std::sin(phase) * ringRadius;
            emitPosition.y += 0.04f + 0.08f * std::sin(phase * 1.31f);
            EmitDirectedFirePreset(kHeadFlamePreset, emitPosition, particleDirection, 0.90f + speedRate * 0.34f);
            ++headFlameParticleCursor_;
            headFlameParticleTimer_ += 0.055f;
        }
        if (headEmberParticleTimer_ <= 0.0f) {
            const float phase = idleTimer_ * 4.2f + static_cast<float>(headFlameParticleCursor_) * 1.37f;
            Vector3 emitPosition = pos;
            emitPosition.x += std::cos(phase) * bodyScale * 0.26f;
            emitPosition.z += std::sin(phase) * bodyScale * 0.26f;
            emitPosition.y += 0.12f;
            EmitDirectedFirePreset(kHeadEmberPreset, emitPosition, particleDirection, 1.02f + speedRate * 0.44f);
            headEmberParticleTimer_ += 0.14f;
        }
    }
}

void EnemyFireSlime::RequestRemoveHeadFlameVisual() {
    if (headFlameRemoveRequested_) {
        return;
    }

    headFlameRemoveRequested_ = true;
    if (!headFlameVisual_) {
        return;
    }

    headFlameVisual_->SetIsVisible(false);
    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (scene) {
        scene->RequestRemoveObject(headFlameVisual_);
    }
    headFlameVisual_ = nullptr;
}

void EnemyFireSlime::EnsureBreathFlameVisuals() {
    if (breathFlameRemoveRequested_) {
        return;
    }

    bool needsCreate = false;
    for (int i = 0; i < kBreathFlameVisualCount; ++i) {
        if (!breathFlameVisuals_[i]) {
            needsCreate = true;
            break;
        }
    }
    if (!needsCreate) {
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (!scene || !scene->GetObject3dCommon()) {
        return;
    }

    for (int i = 0; i < kBreathFlameVisualCount; ++i) {
        if (breathFlameVisuals_[i]) {
            continue;
        }

        auto flame = std::make_unique<Object3d>();
        flame->Initialize(scene->GetObject3dCommon());
        flame->SetName(GetName() + "_BreathFlameShader_" + std::to_string(i));
        flame->SetClassName("Effect");
        flame->SetModel("Primitives/sphere");
        flame->SetColliderType(ColliderType::kNone);
        flame->SetCollisionAttribute(0);
        flame->SetCollisionMask(0);
        flame->SetBlendMode(BlendMode::kNormal);
        flame->SetMaterialType(kHeadFlameMaterialType);
        flame->SetSelectedLighting(0);
        flame->SetEnableLighting(false);
        flame->SetIsVisible(false);
        flame->SetColor({ 1.0f, 0.31f, 0.05f, 0.0f });
        flame->SetEmissive(1.8f);

        if (auto* renderer = flame->GetMeshRenderer()) {
            if (auto* water = renderer->GetWaterParamData()) {
                water->effectType = kBreathFlameEffectType;
                water->waveSpeed = 2.45f;
                water->effectScale = 0.92f;
                water->effectSoftness = 0.58f;
                water->effectIntensity = 0.92f;
                water->billboardScale = 0.72f;
                water->effectScaleX = 1.0f;
                water->effectScaleY = 0.72f;
                water->uvOffsetX = 3.71f * static_cast<float>(i + 1);
                water->uvOffsetY = 6.13f * static_cast<float>(i + 1);
            }
        }

        breathFlameVisuals_[i] = flame.get();
        scene->AddObject(std::move(flame));
    }
}

void EnemyFireSlime::UpdateBreathFlameVisuals(const Vector3& direction, float deltaTime) {
    (void)deltaTime;
    EnsureBreathFlameVisuals();

    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    const float activeDuration = (std::max)(0.01f, attack.activeDuration);
    const Vector3 scale = GetScale();
    const float bodyScale = (std::max)({ 0.7f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    const float progress = 1.0f - (std::clamp)(breathTimer_ / activeDuration, 0.0f, 1.0f);
    const float fadeIn = (std::clamp)(progress / 0.16f, 0.0f, 1.0f);
    const float fadeOut = (std::clamp)(breathTimer_ / (activeDuration * 0.18f), 0.0f, 1.0f);
    const float breathAlpha = fadeIn * fadeOut;
    const Vector3 side = { -direction.z, 0.0f, direction.x };

    Camera* camera = CameraManager::GetInstance() ? CameraManager::GetInstance()->GetActiveCamera() : nullptr;

    for (int i = 0; i < kBreathFlameVisualCount; ++i) {
        Object3d* flame = breathFlameVisuals_[i];
        if (!flame) {
            continue;
        }

        const float step = (static_cast<float>(i) + 0.5f) / static_cast<float>(kBreathFlameVisualCount);
        const float distance = 0.50f + step * (attack.maxRange * 0.84f);
        const float wobble = std::sin(idleTimer_ * (6.2f + step * 1.4f) + step * 8.3f) * (0.025f + step * 0.07f);
        Vector3 pos = GetTranslate() + direction * distance + side * wobble;
        pos.y += (0.62f + step * 0.12f) * (std::max)(0.7f, std::abs(scale.y));

        float yaw = GetRotation().y;
        float flowX = 0.0f;
        if (camera) {
            const Vector3 toCamera = camera->GetEye() - pos;
            if (std::abs(toCamera.x) + std::abs(toCamera.z) > 0.001f) {
                yaw = std::atan2(toCamera.x, toCamera.z);

                Vector3 viewForward = Math::Normalize(toCamera);
                Vector3 upSeed = std::abs(viewForward.y) > 0.96f ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
                Vector3 viewRight = Math::Normalize(Math::Cross(upSeed, viewForward));
                flowX = (std::clamp)(Math::Dot(direction, viewRight) * 0.85f + wobble * 0.45f, -1.0f, 1.0f);
            }
        }

        const float pulse = std::sin(idleTimer_ * (8.2f + step * 2.3f) + step * 5.7f) * 0.035f;
        const float width = (0.64f + step * 0.78f + pulse) * bodyScale;
        const float height = (0.34f + step * 0.23f - pulse * 0.18f) * bodyScale;
        const float alpha = breathAlpha * (0.78f - step * 0.12f);

        flame->SetIsVisible(alpha > 0.02f);
        flame->SetTranslate(pos);
        flame->SetRotation({ 0.0f, yaw, 0.0f });
        flame->SetScale({ width, height, width });
        flame->SetColor({ 1.0f, 0.30f + step * 0.06f, 0.045f, alpha });

        if (auto* renderer = flame->GetMeshRenderer()) {
            if (auto* water = renderer->GetWaterParamData()) {
                water->flowSpeedX = flowX;
                water->flowSpeedY = 0.82f + step * 0.42f;
                water->waveSpeed = 2.35f + step * 0.58f;
                water->effectIntensity = 0.88f + step * 0.10f;
                water->effectScaleX = 1.08f + step * 0.56f;
                water->effectScaleY = 0.64f + step * 0.18f;
            }
        }

        flame->UpdateLocalMatrix();
        flame->UpdateWorldMatrix();
    }
}

void EnemyFireSlime::HideBreathFlameVisuals() {
    for (Object3d* flame : breathFlameVisuals_) {
        if (flame) {
            flame->SetIsVisible(false);
        }
    }
}

void EnemyFireSlime::RequestRemoveBreathFlameVisuals() {
    if (breathFlameRemoveRequested_) {
        return;
    }

    breathFlameRemoveRequested_ = true;
    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;

    for (Object3d*& flame : breathFlameVisuals_) {
        if (!flame) {
            continue;
        }
        flame->SetIsVisible(false);
        if (scene) {
            scene->RequestRemoveObject(flame);
        }
        flame = nullptr;
    }
}

void EnemyFireSlime::EmitBreathParticles(const Vector3& origin, const Vector3& direction) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kBreathAttackId);
    const float step = (static_cast<float>(breathParticleCursor_ % kBreathFlameVisualCount) + 0.5f) /
        static_cast<float>(kBreathFlameVisualCount);
    Vector3 emitPosition = origin + direction * (0.48f + step * attack.maxRange * 0.82f);
    emitPosition.y += std::sin(idleTimer_ * 8.0f + step * 9.1f) * 0.08f;

    Vector3 streamDirection = direction;
    streamDirection.y = 0.10f + step * 0.10f;
    streamDirection = Math::Normalize(streamDirection);
    const char* bodyPreset = attack.activeVfx.empty() ? kBreathPreset : attack.activeVfx.c_str();
    EmitDirectedFirePreset(bodyPreset, emitPosition, streamDirection, 0.82f + step * 0.34f);

    if (breathEmberTimer_ <= 0.0f) {
        EmitDirectedFirePreset(kBreathEmberPreset, emitPosition, streamDirection, 0.95f + step * 0.42f);
        breathEmberTimer_ += 0.14f;
    }
    ++breathParticleCursor_;
}

void EnemyFireSlime::EmitFirePreset(const char* presetName, const Vector3& position) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    particles->Emit(presetName, position);
}

void EnemyFireSlime::EmitDirectedFirePreset(
    const char* presetName,
    const Vector3& position,
    const Vector3& direction,
    float speedScale) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    particles->EmitDirected(presetName, position, direction, speedScale);
}

void EnemyFireSlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    Vector3 targetRotation = { 0.0f, GetRotation().y, 0.0f };
    if (breathTimer_ > 0.0f) {
        const float activeDuration = (std::max)(0.01f, GetAttackDefinition(kBreathAttackId).activeDuration);
        const float t = std::clamp(1.0f - (breathTimer_ / activeDuration), 0.0f, 1.0f);
        const float inhale = std::sin(std::clamp(t / 0.24f, 0.0f, 1.0f) * 3.14159265f);
        const float exhale = std::clamp((t - 0.16f) / 0.24f, 0.0f, 1.0f);
        const float pulse = std::sin(t * 22.0f) * 0.035f * exhale;
        targetScale.x = baseScale_.x * (1.0f + inhale * 0.12f + exhale * 0.08f + pulse);
        targetScale.y = baseScale_.y * (1.0f + inhale * 0.10f - exhale * 0.20f - pulse * 0.30f);
        targetScale.z = baseScale_.z * (1.0f - inhale * 0.12f + exhale * 0.30f - pulse * 0.55f);
        targetRotation.x = inhale * 0.09f - exhale * 0.07f;
    }
    else if (fireballWindupTimer_ > 0.0f) {
        const float windupDuration = (std::max)(0.01f, GetAttackDefinition(kFireballAttackId).windupDuration);
        const float charge = std::clamp(1.0f - fireballWindupTimer_ / windupDuration, 0.0f, 1.0f);
        const float tremble = std::sin(idleTimer_ * 31.0f) * 0.025f * charge;
        targetScale.x = baseScale_.x * (1.0f + charge * 0.18f + tremble);
        targetScale.y = baseScale_.y * (1.0f - charge * 0.24f);
        targetScale.z = baseScale_.z * (1.0f - charge * 0.08f - tremble * 0.6f);
        targetRotation.x = charge * 0.12f;
        targetRotation.z = tremble * 1.6f;
    }
    else if (attackTimer_ > 0.0f) {
        const float recoveryDuration = (std::max)(0.01f, GetAttackDefinition(kFireballAttackId).recoveryDuration);
        const float remaining = std::clamp(attackTimer_ / recoveryDuration, 0.0f, 1.0f);
        const float recoil = std::sin((1.0f - remaining) * 3.14159265f);
        targetScale.x = baseScale_.x * (1.0f - recoil * 0.08f);
        targetScale.y = baseScale_.y * (1.0f + recoil * 0.12f);
        targetScale.z = baseScale_.z * (1.0f + recoil * 0.20f);
        targetRotation.x = -recoil * 0.16f;
    }
    else {
        SlimeBounceAnimator::Params params;
        params.speedForFullBounce = 1.85f;
        params.idleAmplitude = 0.075f;
        params.moveAmplitude = 0.27f;
        params.hopFrequency = 10.4f;
        params.horizontalSquash = 0.28f;
        params.verticalStretch = 0.35f;
        params.airborneStretch = 0.34f;
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    }

    ApplyDamageReactionPose(targetScale, targetRotation);
    const float poseRate = (std::min)(1.0f, deltaTime * 13.0f);
    Vector3 scale = Math::Lerp(GetScale(), targetScale, poseRate);
    SetScale(scale);
    const Vector3 currentRotation = GetRotation();
    SetRotation({
        currentRotation.x + (targetRotation.x - currentRotation.x) * poseRate,
        currentRotation.y,
        currentRotation.z + (targetRotation.z - currentRotation.z) * poseRate
    });
}

void EnemyFireSlime::SyncWorldCollisionRadius(float worldRadius) {
    const Vector3 scale = GetScale();
    const float maxScale = (std::max)({ 0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    SetCollisionRadius(worldRadius / maxScale);
}

void EnemyFireSlime::SyncGroundCollisionRadius() {
    SyncWorldCollisionRadius(kGroundCollisionWorldRadius);
}

void EnemyFireSlime::SyncThrownCollisionRadius() {
    SyncWorldCollisionRadius(kThrownCollisionWorldRadius);
}
