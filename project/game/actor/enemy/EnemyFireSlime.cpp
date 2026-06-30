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
constexpr float kBreathRange = 4.3f;
constexpr float kRangedMinRange = 6.2f;
constexpr float kBreathDuration = 0.62f;
constexpr float kBreathDamage = 1.0f;
constexpr float kFireballLifetime = 2.65f;
constexpr float kCarriedFireCooldown = 0.58f;
constexpr float kCarriedBreathDamageInterval = 0.22f;
constexpr float kCarriedFireballSpeed = 31.0f;
constexpr float kGroundCollisionWorldRadius = 0.82f;
constexpr float kThrownCollisionWorldRadius = 1.18f;
constexpr float kMoveHopInterval = 0.28f;
constexpr float kMoveHopPower = 4.7f;
constexpr float kFireSlimeModelYawOffset = 3.1415926535f;
constexpr float kHeadFlameHeight = 0.62f;
constexpr float kHeadFlameBaseScale = 1.08f;
constexpr int kHeadFlameMaterialType = 11;
constexpr float kHeadFlameEffectType = 2.0f;
constexpr float kBreathFlameEffectType = 3.0f;
constexpr int kBreathFlameVisualCount = 4;
constexpr const char* kBreathPreset = "fire_slime_breath";
constexpr const char* kCastPreset = "fire_slime_cast";

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
    visual.blendMode = BlendMode::kAdd;
    visual.color = { 1.0f, 0.34f, 0.07f, 0.96f };
    visual.emissive = 2.8f;
    visual.visualScale = scale;
    visual.effectType = 1.0f;
    visual.effectScale = 1.15f;
    visual.effectSoftness = 0.42f;
    visual.effectIntensity = 1.28f;
    visual.billboardScale = 0.68f;
    return visual;
}
}

// 炎スライムの初期化
void EnemyFireSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_FireSlime");
    SetEnemyType("FireSlime");
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    defaultColor_ = GetColor();

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SyncGroundCollisionRadius();
}

// 近距離ブレスと中距離火球を切り替えるAI
void EnemyFireSlime::Update(float deltaTime) {
    if (isCarried_) {
        HideAttackTelegraph();
        idleTimer_ += deltaTime;
        return;
    }

    if (isDead || !GetIsVisible()) {
        HideAttackTelegraph();
        breathTimer_ = 0.0f;
        attackTimer_ = 0.0f;
        RequestRemoveHeadFlameVisual();
        RequestRemoveBreathFlameVisuals();
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        return;
    }

    if (ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        RequestRemoveHeadFlameVisual();
        RequestRemoveBreathFlameVisuals();
        BaseEnemy::Update(deltaTime);
        return;
    }
    idleTimer_ += deltaTime;
    UpdateHeadFlame(deltaTime);
    if (IsThrowRecovering()) {
        if (IsThrownPhysics()) {
            SyncThrownCollisionRadius();
        } else {
            SyncGroundCollisionRadius();
        }
        BaseEnemy::Update(deltaTime);
        return;
    }

    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }

    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
    attackTimer_ = (std::max)(0.0f, attackTimer_ - deltaTime);

    Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    if (target_ && param_.has_value()) {
        Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        Vector3 direction = NormalizePlanar(toTarget);
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

        UpdateFacing(direction);

        if (breathTimer_ > 0.0f) {
            UpdateBreath(deltaTime, direction, distance);
        }
        else if (distance <= kBreathRange && attackCooldown_ <= 0.0f) {
            StartBreath();
        }
        else if (distance <= detectionRange_ && distance >= kRangedMinRange && attackCooldown_ <= 0.0f) {
            attackTimer_ = 0.38f;
            attackCooldown_ = 1.45f;
            FireFireball(direction, distance);
        }
        else if (distance <= detectionRange_) {
            const float speed = (std::max)(0.0f, param_->speed);
            const float approach = distance > kBreathRange * 0.78f ? 1.0f : -0.55f;
            velocity.x = direction.x * speed * approach;
            velocity.z = direction.z * speed * approach;
        }
        else {
            const float speed = (std::max)(0.55f, param_->speed * 0.42f);
            velocity = CalculateWanderVelocity(deltaTime, speed, 0.72f);
            UpdateFacing({ velocity.x, 0.0f, velocity.z });
        }
    }

    velocity.y = (std::min)(GetVelocity().y, 0.0f);
    if (breathTimer_ <= 0.0f && SlimeBounceAnimator::StepGroundHop(groundHopTimer_, velocity, isGrounded_, deltaTime, kMoveHopInterval, 0.10f)) {
        velocity.y = (std::max)(velocity.y, kMoveHopPower);
    }
    SetVelocity(velocity);
    ApplySlimeAnimation(deltaTime);
    SyncGroundCollisionRadius();
    BaseEnemy::Update(deltaTime);
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
    breathParticleTimer_ = 0.0f;
    attackTimer_ = 0.0f;
    breathDamageDone_ = false;
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

    BulletManager::GetInstance()->Fire(
        spawnPos,
        direction * kCarriedFireballSpeed + Vector3{ 0.0f, 1.4f, 0.0f },
        kPlayerAttack,
        kEnemy | kAllSolid,
        "Primitives/sphere",
        0.52f,
        kFireballLifetime,
        MakeFireVisual(1.28f));

    EmitFirePreset(kCastPreset, spawnPos);
    carriedFireCooldown_ = kCarriedFireCooldown;
    carriedEffectTimer_ = 0.22f;
}

void EnemyFireSlime::ExecuteBreathAbility(Player* player) {
    if (!player || !isCarried_) {
        return;
    }

    breathTimer_ = (std::max)(breathTimer_, kBreathDuration);
    breathParticleTimer_ = 0.0f;
    carriedEffectTimer_ = 0.12f;
    attackTimer_ = kBreathDuration;
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
            UpdateBreathFlameVisuals(direction, deltaTime);

            if (carriedBreathDamageTimer_ <= 0.0f) {
                DispatchCarriedBreathDamage(player, direction);
                carriedBreathDamageTimer_ = kCarriedBreathDamageInterval;
            }

            if (breathParticleTimer_ <= 0.0f) {
                Vector3 pos = player->GetWorldPosition() + direction * 1.55f;
                pos.y += 0.78f;
                EmitFirePreset(kBreathPreset, pos);
                breathParticleTimer_ = 0.18f;
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
    breathTimer_ = kBreathDuration;
    breathParticleTimer_ = 0.0f;
    breathDamageDone_ = false;
    attackCooldown_ = 1.55f;
    attackTimer_ = kBreathDuration;
    SetColor({ 1.0f, 0.82f, 0.62f, 1.0f });
}

void EnemyFireSlime::UpdateBreath(float deltaTime, const Vector3& direction, float distance) {
    breathTimer_ = (std::max)(0.0f, breathTimer_ - deltaTime);
    breathParticleTimer_ -= deltaTime;
    const float progress = 1.0f - (std::clamp)(breathTimer_ / kBreathDuration, 0.0f, 1.0f);
    ShowAttackTelegraphLine(
        GetTranslate(),
        direction,
        kBreathRange + 0.55f,
        1.65f,
        progress,
        { 1.0f, 0.30f, 0.05f, 0.78f });
    UpdateBreathFlameVisuals(direction, deltaTime);

    if (!breathDamageDone_ && breathTimer_ <= kBreathDuration * 0.62f) {
        TriggerAttackTelegraphCue({ 1.0f, 0.12f, 0.02f, 1.0f });
        DispatchBreathDamage(direction, distance);
        breathDamageDone_ = true;
    }

    if (breathParticleTimer_ <= 0.0f) {
        Vector3 pos = GetTranslate() + direction * 1.8f;
        pos.y += 0.72f;
        EmitFirePreset(kBreathPreset, pos);
        breathParticleTimer_ = 0.22f;
    }

    if (breathTimer_ <= 0.0f) {
        HideAttackTelegraph();
        SetColor(defaultColor_);
        HideBreathFlameVisuals();
    }
}

void EnemyFireSlime::DispatchBreathDamage(const Vector3& direction, float distance) {
    if (!target_ || distance > kBreathRange + 0.55f) {
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
    damageEvent.damageAmount = kBreathDamage;
    damageEvent.knockbackVelocity = { direction.x * 9.5f, 4.4f, direction.z * 9.5f };
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
        if (distance > kBreathRange + 0.85f || distance <= 0.001f) {
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
        damageEvent.damageAmount = kBreathDamage;
        damageEvent.knockbackVelocity = { direction.x * 8.2f, 3.8f, direction.z * 8.2f };
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

    const float speed = std::clamp(distance * 1.65f, 15.0f, 28.0f);
    BulletManager::GetInstance()->Fire(
        spawnPos,
        aim * speed,
        kEnemyAttack,
        kPlayer | kAllSolid,
        "Primitives/sphere",
        0.5f,
        kFireballLifetime,
        MakeFireVisual(1.2f));

    EmitFirePreset(kCastPreset, spawnPos);
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
    flame->SetBlendMode(BlendMode::kAdd);
    flame->SetMaterialType(kHeadFlameMaterialType);
    flame->SetSelectedLighting(0);
    flame->SetEnableLighting(false);
    flame->SetColor({ 1.0f, 0.24f, 0.04f, 0.82f });
    flame->SetEmissive(2.3f);

    if (auto* renderer = flame->GetMeshRenderer()) {
        if (auto* water = renderer->GetWaterParamData()) {
            water->effectType = kHeadFlameEffectType;
            water->waveSpeed = 1.75f;
            water->effectScale = 0.74f;
            water->effectSoftness = 0.68f;
            water->effectIntensity = 1.16f;
            water->billboardScale = 0.66f;
            water->effectScaleX = 1.18f;
            water->effectScaleY = 0.86f;
        }
    }

    headFlameVisual_ = flame.get();
    scene->AddObject(std::move(flame));
}

void EnemyFireSlime::UpdateHeadFlameVisual(float deltaTime) {
    (void)deltaTime;
    EnsureHeadFlameVisual();
    if (!headFlameVisual_) {
        return;
    }

    const Vector3 scale = GetScale();
    const float bodyScale = (std::max)({ 0.7f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    Vector3 velocity = GetVelocity();
    velocity.y = 0.0f;
    const float moveSpeed = Math::Length(velocity);
    Vector3 pos = GetTranslate();
    pos.y += kHeadFlameHeight * (std::max)(0.65f, std::abs(scale.y));
    if (moveSpeed > 0.001f) {
        const Vector3 trailDirection = Math::Normalize(velocity) * -1.0f;
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
            flowX = (std::clamp)(Math::Dot(velocity, viewRight) * 0.10f, -1.0f, 1.0f);
        }
    }

    const float pulse = std::sin(idleTimer_ * 8.0f) * 0.06f;
    const float speedRate = (std::clamp)(moveSpeed * 0.055f, 0.0f, 1.0f);
    const float width = kHeadFlameBaseScale * (1.10f + pulse + speedRate * 0.08f) * bodyScale;
    const float height = kHeadFlameBaseScale * (0.90f - pulse * 0.18f + speedRate * 0.08f) * bodyScale;

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
            water->effectIntensity = 1.14f + std::sin(idleTimer_ * 4.1f) * 0.08f + speedRate * 0.12f;
            water->effectScaleX = 1.18f + speedRate * 0.12f;
            water->effectScaleY = 0.86f + speedRate * 0.08f;
        }
    }

    headFlameVisual_->UpdateLocalMatrix();
    headFlameVisual_->UpdateWorldMatrix();
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
        flame->SetBlendMode(BlendMode::kAdd);
        flame->SetMaterialType(kHeadFlameMaterialType);
        flame->SetSelectedLighting(0);
        flame->SetEnableLighting(false);
        flame->SetIsVisible(false);
        flame->SetColor({ 1.0f, 0.31f, 0.05f, 0.0f });
        flame->SetEmissive(2.8f);

        if (auto* renderer = flame->GetMeshRenderer()) {
            if (auto* water = renderer->GetWaterParamData()) {
                water->effectType = kBreathFlameEffectType;
                water->waveSpeed = 2.45f;
                water->effectScale = 0.92f;
                water->effectSoftness = 0.58f;
                water->effectIntensity = 1.28f;
                water->billboardScale = 0.72f;
                water->effectScaleX = 1.0f;
                water->effectScaleY = 0.72f;
            }
        }

        breathFlameVisuals_[i] = flame.get();
        scene->AddObject(std::move(flame));
    }
}

void EnemyFireSlime::UpdateBreathFlameVisuals(const Vector3& direction, float deltaTime) {
    (void)deltaTime;
    EnsureBreathFlameVisuals();

    const Vector3 scale = GetScale();
    const float bodyScale = (std::max)({ 0.7f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    const float progress = 1.0f - (std::clamp)(breathTimer_ / kBreathDuration, 0.0f, 1.0f);
    const float fadeIn = (std::clamp)(progress / 0.16f, 0.0f, 1.0f);
    const float fadeOut = (std::clamp)(breathTimer_ / (kBreathDuration * 0.18f), 0.0f, 1.0f);
    const float breathAlpha = fadeIn * fadeOut;
    const Vector3 side = { -direction.z, 0.0f, direction.x };

    Camera* camera = CameraManager::GetInstance() ? CameraManager::GetInstance()->GetActiveCamera() : nullptr;

    for (int i = 0; i < kBreathFlameVisualCount; ++i) {
        Object3d* flame = breathFlameVisuals_[i];
        if (!flame) {
            continue;
        }

        const float step = static_cast<float>(i + 1) / static_cast<float>(kBreathFlameVisualCount);
        const float distance = 0.72f + step * (kBreathRange * 0.72f);
        const float wobble = std::sin(idleTimer_ * (6.2f + step * 2.0f) + step * 5.1f) * (0.04f + step * 0.10f);
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

        const float pulse = std::sin(idleTimer_ * (9.0f + step * 2.0f) + step * 3.6f) * 0.05f;
        const float width = (0.72f + step * 0.72f + pulse) * bodyScale;
        const float height = (0.36f + step * 0.25f - pulse * 0.22f) * bodyScale;
        const float alpha = breathAlpha * (0.72f - step * 0.08f);

        flame->SetIsVisible(alpha > 0.02f);
        flame->SetTranslate(pos);
        flame->SetRotation({ 0.0f, yaw, 0.0f });
        flame->SetScale({ width, height, width });
        flame->SetColor({ 1.0f, 0.30f + step * 0.06f, 0.045f, alpha });

        if (auto* renderer = flame->GetMeshRenderer()) {
            if (auto* water = renderer->GetWaterParamData()) {
                water->flowSpeedX = flowX;
                water->flowSpeedY = 0.72f + step * 0.38f;
                water->waveSpeed = 2.35f + step * 0.58f;
                water->effectIntensity = 1.24f + step * 0.14f;
                water->effectScaleX = 0.98f + step * 0.52f;
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

void EnemyFireSlime::EmitFirePreset(const char* presetName, const Vector3& position) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    particles->Emit(presetName, position);
}

void EnemyFireSlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    if (breathTimer_ > 0.0f) {
        const float t = 1.0f - (breathTimer_ / kBreathDuration);
        const float pulse = std::sin(t * 16.0f) * 0.08f;
        targetScale.x = baseScale_.x * (1.18f + pulse);
        targetScale.y = baseScale_.y * (0.78f - pulse * 0.25f);
        targetScale.z = baseScale_.z * (1.22f + pulse);
    }
    else if (attackTimer_ > 0.0f) {
        const float pulse = std::sin(attackTimer_ * 30.0f) * 0.12f;
        targetScale.x = baseScale_.x * (1.08f + pulse);
        targetScale.y = baseScale_.y * (0.9f - pulse * 0.2f);
        targetScale.z = baseScale_.z * (1.08f + pulse);
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

    Vector3 scale = GetScale();
    scale = Math::Lerp(scale, targetScale, (std::min)(1.0f, deltaTime * 10.0f));
    SetScale(scale);
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
