#include "EnemyGiantSlime.h"
#include "BaseScene.h"
#include "CollisionConfig.h"
#include "DebugConsole.h"
#include "EnemyFactory.h"
#include "EventManager.h"
#include "ParticleSystem.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

namespace {
// 大型スライムの着地衝撃波とフック分裂の調整値
constexpr float kShockwaveRadius = 7.0f;
constexpr float kShockwaveDamage = 2.0f;
constexpr float kHookSplitDuration = 1.85f;
constexpr int kSplitSlimeCount = 4;
constexpr float kSplitSlimeRadius = 3.1f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}
}

// 大型スライムの初期化
void EnemyGiantSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_GiantSlime");
    SetEnemyType("GiantSlime");
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    defaultColor_ = GetColor();
    SetScale({ 2.0f, 2.0f, 2.0f });

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(2.2f);
}

// ジャンプ攻撃、徘徊ジャンプ、着地衝撃波の更新
void EnemyGiantSlime::Update(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (isCarried_) {
        return;
    }

    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }

    if (isHookSplitPulled_) {
        idleTimer_ += deltaTime;
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        return;
    }
    if (IsThrowRecovering()) {
        BaseEnemy::Update(deltaTime);
        return;
    }

    idleTimer_ += deltaTime;
    if (landingPulseTimer_ > 0.0f) {
        landingPulseTimer_ = (std::max)(0.0f, landingPulseTimer_ - deltaTime);
    }

    if (isJumpingAttack_ && isGrounded_) {
        isJumpingAttack_ = false;
        landingPulseTimer_ = 0.35f;
        DispatchLandingShockwave();
    }

    if (target_ && param_.has_value()) {
        Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        Vector3 direction = NormalizePlanar(toTarget);
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

        if (distance <= detectionRange_) {
            const float targetYaw = std::atan2(direction.x, direction.z);
            SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.08f));
        }

        if (isGrounded_ && !isJumpingAttack_) {
            Vector3 velocity = GetVelocity();
            velocity.x *= 0.65f;
            velocity.z *= 0.65f;
            SetVelocity(velocity);

            if (distance <= detectionRange_) {
                jumpTimer_ += deltaTime;
                if (jumpTimer_ >= 1.35f) {
                    LaunchJump(direction, distance);
                    jumpTimer_ = 0.0f;
                }
            } else {
                jumpTimer_ += deltaTime * 0.65f;
                Vector3 wanderVelocity = CalculateWanderVelocity(deltaTime, 1.25f, 0.55f);
                Vector3 wanderDirection = { wanderVelocity.x, 0.0f, wanderVelocity.z };
                const float wanderLength = Math::Length(wanderDirection);
                if (wanderLength > 0.001f) {
                    wanderDirection = wanderDirection / wanderLength;
                    const float targetYaw = std::atan2(wanderDirection.x, wanderDirection.z);
                    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.05f));
                }

                if (jumpTimer_ >= 1.75f && wanderLength > 0.05f) {
                    const float jumpPower = param_->jumpPower > 0.0f ? param_->jumpPower * 0.5f : 11.0f;
                    SetVelocity({ wanderDirection.x * 5.2f, jumpPower, wanderDirection.z * 5.2f });
                    jumpTimer_ = 0.0f;
                }
            }
        }
    }

    ApplySlimeAnimation(deltaTime);
    BaseEnemy::Update(deltaTime);
}

std::unique_ptr<Object3d> EnemyGiantSlime::Clone() const {
    auto clone = std::make_unique<EnemyGiantSlime>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

// フックで引っ張られて分裂する特殊処理
void EnemyGiantSlime::BeginHookSplitPull(const Vector3& hookOwnerPos) {
    (void)hookOwnerPos;
    if (hasSplit_) return;

    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }

    isHookSplitPulled_ = true;
    hookSplitPullTimer_ = 0.0f;
    hookSplitBasePosition_ = GetTranslate();
    hookSplitBaseScale_ = GetScale();
    isJumpingAttack_ = false;
    jumpTimer_ = 0.0f;
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetColor({ 0.42f, 0.88f, 1.0f, 1.0f });
}

bool EnemyGiantSlime::UpdateHookSplitPull(float deltaTime, const Vector3& hookOwnerPos, ParticleSystem* particleSystem) {
    if (hasSplit_) {
        return true;
    }
    if (!isHookSplitPulled_) {
        BeginHookSplitPull(hookOwnerPos);
    }

    hookSplitPullTimer_ += deltaTime;
    const float progress = GetHookSplitProgress();

    Vector3 toOwner = hookOwnerPos - hookSplitBasePosition_;
    toOwner.y = 0.0f;
    float distance = Math::Length(toOwner);
    Vector3 dir = distance > 0.001f ? toOwner / distance : Vector3{ 0.0f, 0.0f, 1.0f };

    const float tugDistance = std::min(distance * 0.22f, 4.2f);
    const float shakePower = (1.0f - progress * 0.4f) * 0.22f;
    const float shakeX = std::sin(hookSplitPullTimer_ * 48.0f) * shakePower;
    const float shakeZ = std::cos(hookSplitPullTimer_ * 41.0f) * shakePower;
    Vector3 pulledPos = hookSplitBasePosition_ + dir * tugDistance * (0.45f + progress * 0.55f);
    pulledPos.x += shakeX;
    pulledPos.z += shakeZ;
    SetTranslate(pulledPos);

    const float stretch = std::sin(hookSplitPullTimer_ * 24.0f) * 0.08f;
    SetScale({
        hookSplitBaseScale_.x * (1.0f + progress * 0.34f + stretch),
        hookSplitBaseScale_.y * (1.0f - progress * 0.38f - stretch * 0.6f),
        hookSplitBaseScale_.z * (1.0f + progress * 0.34f - stretch)
    });

    Vector3 rot = GetRotation();
    rot.x = std::sin(hookSplitPullTimer_ * 18.0f) * progress * 0.28f;
    rot.z = std::cos(hookSplitPullTimer_ * 16.0f) * progress * 0.28f;
    SetRotation(rot);
    UpdateLocalMatrix();
    UpdateWorldMatrix();

    if (progress >= 1.0f) {
        SplitIntoSmallSlimes(particleSystem);
        return true;
    }
    return false;
}

void EnemyGiantSlime::CancelHookSplitPull() {
    if (hasSplit_) return;
    isHookSplitPulled_ = false;
    hookSplitPullTimer_ = 0.0f;
    SetColor(defaultColor_);
    SetScale(hookSplitBaseScale_);
    Vector3 rot = GetRotation();
    rot.x = 0.0f;
    rot.z = 0.0f;
    SetRotation(rot);
}

float EnemyGiantSlime::GetHookSplitProgress() const {
    return std::clamp(hookSplitPullTimer_ / kHookSplitDuration, 0.0f, 1.0f);
}

// ジャンプ攻撃、着地衝撃波、伸縮の補助処理
void EnemyGiantSlime::LaunchJump(const Vector3& direction, float distance) {
    if (!param_.has_value()) return;

    const float horizontalSpeed = std::clamp(distance * 0.55f, 8.0f, 18.0f);
    const float jumpPower = param_->jumpPower > 0.0f ? param_->jumpPower : 22.0f;
    SetVelocity({ direction.x * horizontalSpeed, jumpPower, direction.z * horizontalSpeed });
    isJumpingAttack_ = true;
}

void EnemyGiantSlime::DispatchLandingShockwave() {
    if (!target_) return;

    Vector3 diff = target_->GetTranslate() - GetTranslate();
    diff.y = 0.0f;
    const float distance = Math::Length(diff);
    if (distance > kShockwaveRadius) return;

    Vector3 direction = distance > 0.001f ? diff / distance : Vector3{ 0.0f, 0.0f, 1.0f };
    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = kShockwaveDamage;
    damageEvent.knockbackVelocity = { direction.x * 18.0f, 13.0f, direction.z * 18.0f };
    EventManager::GetInstance()->Dispatch(damageEvent);
}

void EnemyGiantSlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    if (landingPulseTimer_ > 0.0f) {
        const float t = landingPulseTimer_ / 0.35f;
        targetScale.x = baseScale_.x * (1.35f - t * 0.25f);
        targetScale.y = baseScale_.y * (0.62f + t * 0.25f);
        targetScale.z = baseScale_.z * (1.35f - t * 0.25f);
    } else if (!isGrounded_) {
        targetScale.x = baseScale_.x * 0.86f;
        targetScale.y = baseScale_.y * 1.24f;
        targetScale.z = baseScale_.z * 0.86f;
    } else if (jumpTimer_ > 0.95f) {
        targetScale.x = baseScale_.x * 1.22f;
        targetScale.y = baseScale_.y * 0.72f;
        targetScale.z = baseScale_.z * 1.22f;
    } else {
        const float breathe = std::sin(idleTimer_ * 2.0f) * 0.035f;
        targetScale.x = baseScale_.x * (1.0f + breathe);
        targetScale.y = baseScale_.y * (1.0f - breathe);
        targetScale.z = baseScale_.z * (1.0f + breathe);
    }

    SetScale(Math::Lerp(GetScale(), targetScale, (std::min)(1.0f, deltaTime * 8.0f)));
}

// 小型スライムへの分裂生成
void EnemyGiantSlime::SplitIntoSmallSlimes(ParticleSystem* particleSystem) {
    if (hasSplit_) return;
    hasSplit_ = true;
    isHookSplitPulled_ = false;

    Vector3 splitCenter = GetTranslate();
    if (particleSystem) {
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        particleSystem->SpawnParticles(
            splitCenter,
            48,
            2.0f,
            &up,
            34.0f,
            { 0.35f, 0.85f, 1.0f, 1.0f },
            { 0.35f, 0.85f, 1.0f, 0.0f },
            0.25f,
            0.65f,
            0.85f,
            0.08f);
    }

    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (scene && scene->GetObject3dCommon()) {
        for (int i = 0; i < kSplitSlimeCount; ++i) {
            auto slime = EnemyFactory::GetInstance()->CreateEnemy("Slime", scene->GetObject3dCommon());
            if (!slime) continue;

            const float angle = (6.28318531f / static_cast<float>(kSplitSlimeCount)) * static_cast<float>(i);
            Vector3 dir = { std::sin(angle), 0.0f, std::cos(angle) };
            Vector3 pos = splitCenter + dir * kSplitSlimeRadius;
            pos.y += 0.45f;

            slime->SetTranslate(pos);
            slime->SetScale({ 2.0f, 2.0f, 2.0f });
            slime->SetTarget(target_);
            slime->SetDetectionRange(18.0f);
            slime->SetVelocity({ dir.x * 12.0f, 12.0f, dir.z * 12.0f });
            if (slime->param_.has_value()) {
                slime->param_->hp = 25.0f;
                slime->param_->maxHp = 25.0f;
                slime->param_->speed = 0.35f;
                slime->param_->detectionRange = 18.0f;
            }
            scene->AddObject(std::move(slime));
        }
    }

    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    isDead = true;

    if (BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr) {
        scene->RequestRemoveObject(this);
    }
    DebugConsole::GetInstance()->AddLog("GiantSlime split into small slimes!");
}
