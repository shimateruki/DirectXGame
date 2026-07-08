#define NOMINMAX
#include "Player.h"
#include "Model.h"
#include "CollisionConfig.h"
#include "engine/utility/math/Math.h"
#include "EventManager.h"
#include "CameraManager.h"
#include "IMoveStrategy.h"
#include "PlayerState.h"
#include "PostEffect.h"
#include "GameDataManager.h"
#include "SceneManager.h"
#include"Winapp.h"
#include <DebugConsole.h>
#include <algorithm>
#include <CollisionManager.h>
#include "DirectXCommon.h"
#include"BaseEnemy.h"
#include "EnemyFireSlime.h"
#include "EnemySlime.h"
#include "GimmickHookPullBlock.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
#include <cmath>

namespace {
    constexpr float kPi = 3.1415926535f;
    constexpr float kTwoPi = kPi * 2.0f;
    constexpr float kEnemyMorphDuration = 5.0f;
    constexpr float kEnemyMorphParticleInterval = 0.12f;
    constexpr float kAbsorbEffectDuration = 0.24f;
    constexpr float kAbsorbEffectParticleInterval = 0.035f;
    constexpr float kPlayerModelYawOffset = kPi;
    const Vector3 kPlayerBaseScale = { 2.0f, 2.0f, 2.0f };
    constexpr float kPlayerDefaultHp = 100.0f;
    constexpr float kPlayerDefaultAttackPower = 1.0f;
    constexpr float kPlayerDefaultMoveSpeed = 27.7f;
    constexpr float kPlayerDefaultGravity = 50.0f;
    constexpr float kPlayerDefaultMaxFallSpeed = 60.0f;
    constexpr float kPlayerDefaultJumpPower = 24.0f;
    constexpr float kPlayerDefaultDetectionRange = 20.0f;
    constexpr float kElectricShockShakeAmount = 0.045f;
    constexpr const char* kElectricShockAuraEffectPath = "Resources/json/effect/effect_thunder_slime_constant_aura.json";

    float NormalizeYaw(float yaw) {
        while (yaw > kPi) yaw -= kTwoPi;
        while (yaw < -kPi) yaw += kTwoPi;
        return yaw;
    }

    Vector3 GetStableEnemyCarryScale(Object3d* enemy) {
        if (!enemy) {
            return { 1.0f, 1.0f, 1.0f };
        }

        const Vector3 scale = enemy->GetScale();
        if (!dynamic_cast<BaseEnemy*>(enemy)) {
            return scale;
        }

        const float absX = std::abs(scale.x);
        const float absY = std::abs(scale.y);
        const float absZ = std::abs(scale.z);
        const float minScale = (std::min)({ absX, absY, absZ });
        const float maxScale = (std::max)({ absX, absY, absZ });
        if (minScale <= 0.0001f || maxScale / minScale < 1.18f) {
            return scale;
        }

        const float stableScale = std::cbrt(absX * absY * absZ);
        return { stableScale, stableScale, stableScale };
    }

    Vector3 ResolveTransformEuler(const Transform& transform) {
        if (!transform.isQuaternionMaster) {
            return transform.rotate;
        }

        return Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(transform.quaternion));
    }

    void CalculateThunderAuraShape(const Vector3& position, const Vector3& scale, Vector3& center, float& horizontalDiameter, float& verticalDiameter) {
        const float maxXZ = (std::max)({ std::abs(scale.x), std::abs(scale.z), 1.0f });
        const float yScale = (std::max)(std::abs(scale.y), 0.18f);

        horizontalDiameter = (std::max)(2.55f, maxXZ * 1.35f);
        verticalDiameter = (std::max)(0.46f, yScale * 1.28f);

        center = position;
        center.y += (std::max)(0.22f, yScale * 0.43f);
    }

    void EmitOuterThunderParticles(const Vector3& position, const Vector3& scale, float phase, int count, const char* presetName = "thunder_slime_idle_spark") {
        auto* gpuParticleManager = GPUParticleManager::GetInstance();
        if (!gpuParticleManager || !gpuParticleManager->IsInitialized() || count <= 0) {
            return;
        }

        Vector3 center{};
        float horizontalDiameter = 1.0f;
        float verticalDiameter = 1.0f;
        CalculateThunderAuraShape(position, scale, center, horizontalDiameter, verticalDiameter);

        const float horizontalRadius = horizontalDiameter * 0.52f;
        const float verticalRadius = verticalDiameter * 0.38f;

        for (int i = 0; i < count; ++i) {
            const float angle = phase + kTwoPi * static_cast<float>(i) / static_cast<float>(count);
            Vector3 emitPos = center;
            emitPos.x += std::cos(angle) * horizontalRadius;
            emitPos.z += std::sin(angle) * horizontalRadius;
            emitPos.y += std::sin(angle * 1.37f) * verticalRadius;
            gpuParticleManager->Emit(presetName, emitPos);
        }
    }

    void EmitThunderMorphBurst(const Vector3& position) {
        EmitOuterThunderParticles(position, kPlayerBaseScale, 0.35f, 4);
    }

    Object3d::EntityParameter MakeDefaultPlayerParameter() {
        Object3d::EntityParameter param;
        param.hp = kPlayerDefaultHp;
        param.maxHp = kPlayerDefaultHp;
        param.attackPower = kPlayerDefaultAttackPower;
        param.speed = kPlayerDefaultMoveSpeed;
        param.gravity = kPlayerDefaultGravity;
        param.maxFallSpeed = kPlayerDefaultMaxFallSpeed;
        param.jumpPower = kPlayerDefaultJumpPower;
        param.detectionRange = kPlayerDefaultDetectionRange;
        return param;
    }

}

// =================================================================
// 初期化・更新・描画
// =================================================================

void Player::Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem, SpriteCommon* spriteCommon)
{
    // 親クラス(Character)の初期化
    Character::Initialize(common);

    // 外部システムの依存注入
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;

    // 自機としての基本設定
    SetClassName("Player");
    SetSaveCategory("Player");
    if (!param_.has_value()) {
        param_ = MakeDefaultPlayerParameter();
    }
    jumpCount_ = 0;

    // 移動コンポーネントの構築
    mover_ = std::make_unique<PlayerMover>();
    mover_->Initialize(this, inputManager, particleSystem);
    slimeAnimator_.Reset(kPlayerBaseScale);

    // ステートマシン初期化 (待機状態からスタート)
    ChangeState(std::make_unique<PlayerStateIdle>());

    // フック用の到達地点マーカーを初期化
    if (common) {
        hookMarker_ = std::make_unique<Object3d>();
        hookMarker_->Initialize(common);
        hookMarker_->SetModel("Characters/slimeBody");
        // スライムっぽい半透明な緑色
        hookMarker_->SetColor({ 0.3f, 1.0f, 0.5f, 0.6f });
        hookMarker_->SetIsVisible(false);
        // 当たり判定は不要
        hookMarker_->SetCollisionAttribute(0);
        hookMarker_->SetCollisionMask(0);
        // マーカーサイズを少し小さめに
        hookMarker_->GetTransform()->scale = { 0.5f, 0.5f, 0.5f };
    }
}
void Player::Update(float deltaTime)
{
    // 初回更新時に初期位置と初期回転を記録
    if (isFirstUpdate_) {
        const float maxScale = (std::max)({ std::abs(transform_.scale.x), std::abs(transform_.scale.y), std::abs(transform_.scale.z) });
        if (maxScale < 1.2f) {
            SetScale(kPlayerBaseScale);
        }
        respawnPosition_ = transform_.translate;
        baseRotation_ = ResolveTransformEuler(transform_);
        baseRotation_.y = kPlayerModelYawOffset;
        SetRotation(baseRotation_);
        isFirstUpdate_ = false;
    }

    if (gateReturnAnimation_.IsActive()) {
        gateReturnAnimation_.Update(this, deltaTime);
        UpdateEnemyMorph(deltaTime);
        return;
    }

    // 時間が進んでいる（ポーズ中ではない）時だけ、操作や状態を更新
    if (deltaTime > 0.0f)
    {
        // 1. 無敵タイマーの管理 (ダメージ被弾時)
        if (damageCooldownTimer_ > 0.0f) {
            damageCooldownTimer_ -= deltaTime;
            if (damageCooldownTimer_ <= 0.0f) {
                damageCooldownTimer_ = 0.0f;
                SetDamageInvincible(false);
            }
        }

        if (electricShockControlLocked_) {
            SetIsControlActive(false);
            SetVelocity({ 0.0f, 0.0f, 0.0f });
            SetTranslate(electricShockLockedPosition_);
        }

        // 2. 移動制御の更新
        if (isControlActive_ && mover_) {
            // 右クリック中は移動入力を受け付けず、水平移動を停止させる
            if (inputManager_->IsMouseButtonPressed(1)) {
                SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::AimHook);
                Vector3 v = GetVelocity();
                SetVelocity({ 0.0f, v.y, 0.0f });

                Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                if (camera) {
                    Vector3 camRot = camera->GetRotation();
                    SetRotation({ baseRotation_.x + camRot.x, NormalizeYaw(camRot.y + GetVisualYawOffset()), baseRotation_.z });

                    // --- フック到達地点の計算とマーカー表示 ---
                    if (hookMarker_) {
                        Vector3 start = camera->GetEye();

                        Vector3 dir;
                        dir.x = std::sin(camRot.y) * std::cos(camRot.x);
                        dir.y = -std::sin(camRot.x);
                        dir.z = std::cos(camRot.y) * std::cos(camRot.x);

                        float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                        if (length > 0.0f) {
                            dir.x /= length; dir.y /= length; dir.z /= length;
                        }

                        float maxDistance = 150.0f;
                        uint32_t mask = kAllSolid | kEnemy | kHookAnchor;

                        RaycastHit hit = CollisionManager::GetInstance()->Raycast(start, dir, maxDistance, mask);

                        if (hit.isHit) {
                            hookMarker_->SetIsVisible(true);
                            hookMarker_->GetTransform()->translate = hit.hitPoint;
                            aimTargetObject_ = hit.hitObject;

                            // 【案A：エイムフィードバック】
                            // 敵をロックオンしている時はマーカーを強調する（赤色に変える）
                            if (aimTargetObject_ && aimTargetObject_->GetGimmickType() == "HookAnchor") {
                                hookMarker_->SetColor({ 0.2f, 0.85f, 1.0f, 1.0f });
                                static float pulseTimer = 0.0f;
                                pulseTimer += deltaTime;
                                float pulse = 1.25f + std::sin(pulseTimer * 10.0f) * 0.25f;
                                hookMarker_->GetTransform()->scale = { pulse, pulse, pulse };
                            }
                            else if (aimTargetObject_ && (aimTargetObject_->GetCollisionAttribute() & kEnemy)) {
                                hookMarker_->SetColor({ 1.0f, 0.2f, 0.2f, 1.0f }); // 赤色に変更
                                static float pulseTimer = 0.0f;
                                pulseTimer += deltaTime;
                                float pulse = 1.2f + std::sin(pulseTimer * 15.0f) * 0.3f;
                                hookMarker_->GetTransform()->scale = { pulse, pulse, pulse };
                            }
                            else {
                                hookMarker_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 白
                                hookMarker_->GetTransform()->scale = { 1.0f, 1.0f, 1.0f };
                            }

                            hookMarker_->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
                        }
                        else {
                            hookMarker_->SetIsVisible(false);
                            aimTargetObject_ = nullptr;
                        }

                        hookMarker_->Update(deltaTime);
                        hookMarker_->UpdateLocalMatrix();
                        hookMarker_->UpdateWorldMatrix();
                    }
                }
            }
            else {
                // 右クリック解除時にフックアクションへ移行
                if (hookMarker_) {
                    if (hookMarker_->GetIsVisible()) {
                        Vector3 targetPos = hookMarker_->GetTransform()->translate;

                        // フック移動は地面全体では停止中。HookAnchor だけ旧フック移動を使う。
                        if (aimTargetObject_ && aimTargetObject_->GetGimmickType() == "HookAnchor") {
                            ChangeState(std::make_unique<PlayerStateHook>(targetPos));
                        }
                        else if (aimTargetObject_ && aimTargetObject_->GetGimmickType() == "HookPullBlock") {
                            ChangeState(std::make_unique<PlayerStatePullObject>(aimTargetObject_, targetPos));
                        }
                        else if (aimTargetObject_ && (aimTargetObject_->GetCollisionAttribute() & kEnemy)) {
                            ChangeState(std::make_unique<PlayerStatePullEnemy>(aimTargetObject_, targetPos));
                        }
                        else {
                            // ChangeState(std::make_unique<PlayerStateHook>(targetPos));
                            hookMarker_->SetIsVisible(false);
                            aimTargetObject_ = nullptr;
                        }
                    }
                }

                // 通常移動時はX軸（ピッチ）とZ軸（ロール）の回転を初期値に戻す
                Vector3 rot = GetRotation();
                rot.x = baseRotation_.x;
                rot.z = baseRotation_.z;
                SetRotation(rot);

                mover_->Update(deltaTime);
            }
        }

        // 3. 状態(State)の更新
        if (state_) {
            state_->Update(this, deltaTime);
        }

        // 4. 死亡判定
        if (GetHp() <= 0.0f && !isDead) {
            isDead = true;
            deathTimer_ = 0.0f;
            ChangeState(std::make_unique<PlayerStateDead>());

            PlayerDeathEvent deathEvent;
            deathEvent.player = this;
            EventManager::GetInstance()->Dispatch(deathEvent);
        }

        // 5. 落下判定
        if (transform_.translate.y < -18.0f && !isDead && isControlActive_) {
            ChangeState(std::make_unique<PlayerStateFallingOut>());
            GameDataManager::GetInstance()->SubtractLife();
        }

        // 6. ポストエフェクト：HPが1の時に赤枠（ピンチ演出）を出す
        if (!isDead && GetHp() <= 1.0f && GetHp() > 0.0f) {
            // 脈打つ赤枠を有効化 (1.0f だと少し強いかもしれないので 0.8f 程度に調整)
            PostEffect::GetInstance()->GetParams()->dangerVignette = 0.8f;
        }
        else {
            PostEffect::GetInstance()->GetParams()->dangerVignette = 0.0f;
        }
    }

    BaseEnemy* carriedEnemyBase = dynamic_cast<BaseEnemy*>(carriedEnemy_);
    BaseEnemy* activeMorphSource = isEnemyMorphed_ ? enemyMorphSource_ : nullptr;
    const bool isBatMorphActive = isEnemyMorphed_ && enemyMorphType_ == EnemyMorphType::Bat;
    const bool isCarryingBat = (carriedEnemyBase && carriedEnemyBase->GetEnemyType() == "Bat") || isBatMorphActive;

    if (isControlActive_) {
        float gravity = inputManager_->IsMouseButtonPressed(1) ? 10.0f : 50.0f;
        float maxFallSpeed = 60.0f;

        if (isCarryingBat && velocity_.y < 0.0f) {
            gravity = (std::min)(gravity, 12.0f);
            maxFallSpeed = 12.0f;
            if (velocity_.y < -maxFallSpeed) {
                velocity_.y = Math::Lerp(velocity_.y, -maxFallSpeed, 0.22f);
            }
        }

        SetGravity(gravity);
        SetMaxFallSpeed(maxFallSpeed);
    }

    // 6. 親クラスの更新
    Character::Update(deltaTime);

    if (electricShockControlLocked_) {
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        SetTranslate(electricShockLockedPosition_);
    }

    UpdateElectricShockFeedback(deltaTime);

    if (IsInvincible() && deltaTime > 0.0f) {
        invincibleBlinkTimer_ += deltaTime;
    }

    UpdateDamageInvincibleBlinkVisibility();

    if (electricShockFeedbackTimer_ <= 0.0f && IsInvincible() && deltaTime > 0.0f) {
        const float blink = 0.5f + 0.5f * std::sin(invincibleBlinkTimer_ * 42.0f);
        const Vector4 base = isEnemyMorphed_ ? GetEnemyMorphTint(enemyMorphType_) : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        const Vector4 flash = isDashInvincible_
            ? Vector4{ 0.34f, 0.74f, 1.0f, 1.0f }
            : Vector4{ 1.0f, 0.78f, 0.32f, 1.0f };
        const float flashRate = blink > 0.48f ? 0.72f : 0.12f;
        const Vector4 color = {
            base.x * (1.0f - flashRate) + flash.x * flashRate,
            base.y * (1.0f - flashRate) + flash.y * flashRate,
            base.z * (1.0f - flashRate) + flash.z * flashRate,
            1.0f
        };
        SetColor(color);
        if (!isEnemyMorphed_) {
            for (Object3d* child : GetChildren()) {
                if (child) {
                    child->SetColor(color);
                }
            }
        }
    } else if (!IsInvincible()) {
        invincibleBlinkTimer_ = 0.0f;
    }

    if (isCarryingBat && velocity_.y < -0.25f && deltaTime > 0.0f) {
        carryGlideEffectTimer_ -= deltaTime;
        if (carryGlideEffectTimer_ <= 0.0f) {
            const Vector3 playerPos = GetWorldPosition();
            const float yaw = GetMoveYaw();
            Vector3 windDir = { -std::sin(yaw), 0.18f, -std::cos(yaw) };
            const float windLength = std::sqrt(windDir.x * windDir.x + windDir.y * windDir.y + windDir.z * windDir.z);
            if (windLength > 0.001f) {
                windDir = windDir / windLength;
            }

            if (particleSystem_) {
                particleSystem_->SpawnParticles(
                    playerPos + Vector3{ 0.0f, 1.25f, 0.0f },
                    10,
                    2.4f,
                    &windDir,
                    34.0f,
                    { 0.55f, 0.85f, 1.0f, 0.65f },
                    { 0.75f, 1.0f, 1.0f, 0.0f },
                    0.22f,
                    0.5f,
                    0.32f,
                    0.04f
                );
            }

            if (MeshEffectManager::GetInstance()) {
                MeshEffectManager::GetInstance()->SpawnEffectAt(
                    "Resources/json/effect/effect_carry_bat_glide_ring.json",
                    playerPos + Vector3{ 0.0f, 1.15f, 0.0f },
                    { 1.5707963f, yaw, 0.0f },
                    { 1.0f, 1.0f, 1.0f }
                );
            }
            if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
                gpuParticleManager->Emit("carry_bat_glide_wisp", playerPos + Vector3{ 0.0f, 1.1f, 0.0f });
            }
            carryGlideEffectTimer_ = 0.14f;
        }
    }
    else {
        carryGlideEffectTimer_ = 0.0f;
    }

    const bool carryAbilityTriggered =
        inputManager_->IsKeyTriggered(DIK_E) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_Y);

    if (!absorbEffectActive_ && carriedEnemy_ && !isEnemyMorphed_ && inputManager_->IsMouseButtonPressed(0)) {
        BaseEnemy* enemyBase = dynamic_cast<BaseEnemy*>(carriedEnemy_);
        if (enemyBase) {
            CancelEnemyMorph();

            // ① カメラの向いている方向（投げる方向）を計算
            Camera* camera = CameraManager::GetInstance()->GetMainCamera();
            Vector3 throwForward = GetForwardDirection();
            float throwUpSpeed = 17.0f;
            if (camera) {
                Vector3 camRot = camera->GetRotation();
                throwForward = { std::sin(camRot.y), 0.0f, std::cos(camRot.y) };
                throwUpSpeed = (std::clamp)(17.0f - std::sin(camRot.x) * 7.0f, 10.0f, 21.0f);
            }
            float forwardLength = std::sqrt(throwForward.x * throwForward.x + throwForward.z * throwForward.z);
            if (forwardLength > 0.001f) {
                throwForward.x /= forwardLength;
                throwForward.z /= forwardLength;
            }
            else {
                throwForward = { 0.0f, 0.0f, 1.0f };
            }

            // ② プレイヤーに自爆ヒットしないように、少し前方にずらして配置
            Vector3 playerPos = GetWorldPosition();
            enemyBase->GetTransform()->translate = {
                playerPos.x + throwForward.x * 2.2f,
                playerPos.y + 2.2f,
                playerPos.z + throwForward.z * 2.2f
            };

            // ③ 無力化を解除（当たり判定復活）して、初速（Velocity）を与える！
            enemyBase->SetCarried(false);

            // 投げた時は捕まえた時点の大きさへ戻す
            enemyBase->GetTransform()->scale = hasCarriedEnemyBaseScale_ ? carriedEnemyBaseScale_ : enemyBase->GetScale();
            enemyBase->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
            enemyBase->GetTransform()->isQuaternionMaster = true;

            Vector3 throwVelocity = {
                throwForward.x * 32.0f,
                throwUpSpeed,
                throwForward.z * 32.0f
            };
            enemyBase->BeginThrown(throwVelocity);

            TriggerSlimeImpulse({ 1.4f, 3.6f, 1.4f }, 0.18f);
            // ② 投げた瞬間の衝撃エフェクト（パーティクル）
            if (particleSystem_) {
                Vector3 effectDir = { throwForward.x, 0.35f, throwForward.z };
                float effectDirLength = std::sqrt(effectDir.x * effectDir.x + effectDir.y * effectDir.y + effectDir.z * effectDir.z);
                if (effectDirLength > 0.001f) {
                    effectDir = effectDir / effectDirLength;
                }
                particleSystem_->SpawnParticles(
                    playerPos + Vector3{0.0f, 2.5f, 0.0f}, 24, 1.6f, &effectDir, 32.0f,
                    { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f },
                    0.1f, 0.3f, 0.8f, 0.1f
                );
            }
        }

        // 手放す
        SetCarriedEnemy(nullptr);
        DebugConsole::GetInstance()->AddLog("Throw Enemy!");
    }
    else if (!absorbEffectActive_ && activeMorphSource && carryAbilityTriggered) {
        activeMorphSource->ExecuteAbility(this);
    }
    else if (!absorbEffectActive_ && activeMorphSource && enemyMorphType_ == EnemyMorphType::FireSlime && inputManager_->IsMouseButtonPressed(0)) {
        if (auto* fireSlime = dynamic_cast<EnemyFireSlime*>(activeMorphSource)) {
            fireSlime->ExecuteBreathAbility(this);
        }
    }
	else if (!absorbEffectActive_ && carriedEnemy_ && carriedEnemyBase && carryAbilityTriggered) {
		BaseEnemy* enemyBase = dynamic_cast<BaseEnemy*>(carriedEnemy_);
		if (enemyBase) {
			if (!isEnemyMorphed_) {
                if (ShouldPlayAbsorbEffect(enemyBase)) {
                    BeginAbsorbEffect(enemyBase);
                } else {
                    StartEnemyMorph(enemyBase);
                }
			} else {
				// 吸収済みのときだけ、拘束中の敵能力を発動する。
				enemyBase->ExecuteAbility(this);
			}
		}
	}
    if (absorbEffectActive_) {
        UpdateAbsorbEffect(deltaTime);
    } else if (carriedEnemy_ && !isEnemyMorphed_) {
        SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Carry);
        BaseEnemy* carriedBase = dynamic_cast<BaseEnemy*>(carriedEnemy_);
        if (carriedBase && !carriedBase->IsCarried()) {
            carriedBase->SetCarried(true);
        }
        carriedEnemy_->SetCollisionAttribute(0);
        carriedEnemy_->SetCollisionMask(0);

        Vector3 playerPos = GetWorldPosition();
        
        static float struggleTimer_ = 0.0f;
        struggleTimer_ += deltaTime;

        // 【抗っている感の演出（汎用プロシージャルアニメーション）】
        float offsetX = std::sin(struggleTimer_ * 35.0f) * 0.15f;
        float offsetZ = std::cos(struggleTimer_ * 30.0f) * 0.15f;
        float offsetY = std::sin(struggleTimer_ * 45.0f) * 0.08f;

        carriedEnemy_->GetTransform()->translate = { 
            playerPos.x + offsetX, playerPos.y + 2.5f + offsetY, playerPos.z + offsetZ 
        };

        Vector3 rot;
        rot.x = std::sin(struggleTimer_ * 20.0f) * 0.2f;
        rot.y = GetRotation().y + std::sin(struggleTimer_ * 15.0f) * 0.4f;
        rot.z = std::cos(struggleTimer_ * 22.0f) * 0.2f;
        carriedEnemy_->GetTransform()->rotate = rot;
        carriedEnemy_->GetTransform()->isQuaternionMaster = false;

        if (!hasCarriedEnemyBaseScale_) {
            carriedEnemyBaseScale_ = GetStableEnemyCarryScale(carriedEnemy_);
            hasCarriedEnemyBaseScale_ = true;
        }
        float stretch = std::sin(struggleTimer_ * 25.0f) * 0.05f;
        const Vector3 targetScale = {
            carriedEnemyBaseScale_.x * (1.0f - stretch),
            carriedEnemyBaseScale_.y * (1.0f + stretch),
            carriedEnemyBaseScale_.z * (1.0f - stretch)
        };
        carriedEnemy_->GetTransform()->scale = Math::Lerp(
            carriedEnemy_->GetScale(),
            targetScale,
            std::clamp(deltaTime * 10.0f, 0.0f, 1.0f)
        );

        // 行列を強制更新して、1フレームの遅れもなくピタッと追従させる
        carriedEnemy_->UpdateLocalMatrix();
        carriedEnemy_->UpdateWorldMatrix();

        if (carriedBase) {
            carriedBase->UpdateCarriedAbility(this, deltaTime);
        }
    } else if (carriedEnemy_ && isEnemyMorphed_) {
        BaseEnemy* carriedBase = dynamic_cast<BaseEnemy*>(carriedEnemy_);
        carriedEnemy_->SetIsVisible(false);
        carriedEnemy_->SetCollisionAttribute(0);
        carriedEnemy_->SetCollisionMask(0);
        if (carriedBase) {
            carriedBase->UpdateCarriedAbility(this, deltaTime);
        }
    }
    if (activeMorphSource) {
        activeMorphSource->SetIsVisible(false);
        activeMorphSource->SetCollisionAttribute(0);
        activeMorphSource->SetCollisionMask(0);
        activeMorphSource->UpdateCarriedAbility(this, deltaTime);
    }

    if (forcedSlimeAnimationModeActive_) {
        slimeAnimator_.SetMode(forcedSlimeAnimationMode_);
        slimeAnimator_.SetMotionDirection(forcedSlimeAnimationDirection_);
        forcedSlimeAnimationModeActive_ = false;
    }

    if (!dynamic_cast<PlayerStateDamage*>(state_.get()) && !absorbEffectActive_ && electricShockFeedbackTimer_ <= 0.0f && !isDead) {
        slimeAnimator_.Update(this, deltaTime);
    }

    UpdateEnemyMorph(deltaTime);
}


void Player::DrawUI()
{
}

float Player::GetVisualYawOffset() const
{
    if (isEnemyMorphed_) {
        return GetEnemyMorphModelYawOffset();
    }
    return kPlayerModelYawOffset;
}

float Player::GetEnemyMorphModelYawOffset() const
{
    switch (enemyMorphType_) {
    case EnemyMorphType::Slime:
    case EnemyMorphType::FireSlime:
    case EnemyMorphType::ThunderSlime:
    case EnemyMorphType::Bomber:
    case EnemyMorphType::BeamDrone:
        return kPi;
    default:
        return 0.0f;
    }
}

float Player::GetMoveYaw() const
{
    return NormalizeYaw(transform_.rotate.y - GetVisualYawOffset());
}

Vector3 Player::GetForwardDirection() const
{
    const float yaw = GetMoveYaw();
    return { std::sin(yaw), 0.0f, std::cos(yaw) };
}

void Player::SetMoveYaw(float yaw)
{
    SetRotationY(NormalizeYaw(yaw + GetVisualYawOffset()));
}

void Player::SetCarriedEnemy(Object3d* enemy)
{
    if (carriedEnemy_ == enemy) {
        if (enemy && !hasCarriedEnemyBaseScale_) {
            carriedEnemyBaseScale_ = GetStableEnemyCarryScale(enemy);
            const Transform* enemyTransform = enemy->GetTransform();
            if (enemyTransform) {
                carriedEnemyBaseRotation_ = enemyTransform->rotate;
                carriedEnemyBaseQuaternion_ = enemyTransform->quaternion;
                carriedEnemyBaseQuaternionMaster_ = enemyTransform->isQuaternionMaster;
            }
            hasCarriedEnemyBaseScale_ = true;
        }
        return;
    }

    carriedEnemy_ = enemy;
    if (enemy) {
        carriedEnemyBaseScale_ = GetStableEnemyCarryScale(enemy);
        const Transform* enemyTransform = enemy->GetTransform();
        if (enemyTransform) {
            carriedEnemyBaseRotation_ = enemyTransform->rotate;
            carriedEnemyBaseQuaternion_ = enemyTransform->quaternion;
            carriedEnemyBaseQuaternionMaster_ = enemyTransform->isQuaternionMaster;
        }
        hasCarriedEnemyBaseScale_ = true;
    }
    else {
        carriedEnemyBaseScale_ = { 1.0f, 1.0f, 1.0f };
        carriedEnemyBaseRotation_ = { 0.0f, 0.0f, 0.0f };
        carriedEnemyBaseQuaternion_ = { 0.0f, 0.0f, 0.0f, 1.0f };
        carriedEnemyBaseQuaternionMaster_ = true;
        hasCarriedEnemyBaseScale_ = false;
    }
}

void Player::ReleaseCarriedEnemy(bool restorePose)
{
    if (absorbEffectActive_) {
        CancelAbsorbEffect(restorePose);
    }

    Object3d* enemy = carriedEnemy_;
    if (!enemy) {
        return;
    }

    if (auto* enemyBase = dynamic_cast<BaseEnemy*>(enemy)) {
        enemyBase->SetCarried(false);
    }
    else {
        enemy->SetCollisionAttribute(kEnemy);
        enemy->SetCollisionMask(kPlayer | kAllSolid | kAttributePlayerBullet | kPlayerAttack);
    }

    enemy->SetIsVisible(true);
    if (restorePose) {
        Transform* enemyTransform = enemy->GetTransform();
        if (enemyTransform) {
            enemyTransform->scale = hasCarriedEnemyBaseScale_ ? carriedEnemyBaseScale_ : enemy->GetScale();
            enemyTransform->rotate = carriedEnemyBaseRotation_;
            enemyTransform->quaternion = carriedEnemyBaseQuaternion_;
            enemyTransform->isQuaternionMaster = carriedEnemyBaseQuaternionMaster_;
            enemy->UpdateLocalMatrix();
            enemy->UpdateWorldMatrix();
        }
    }

    SetCarriedEnemy(nullptr);
}

bool Player::ShouldPlayAbsorbEffect(BaseEnemy* enemy) const
{
    if (!enemy) {
        return false;
    }

    const EnemyMorphType type = ResolveEnemyMorphType(enemy->GetEnemyType());
    return type == EnemyMorphType::Slime || type == EnemyMorphType::FireSlime;
}

void Player::BeginAbsorbEffect(BaseEnemy* enemy)
{
    if (!enemy) {
        return;
    }

    pendingAbsorbType_ = ResolveEnemyMorphType(enemy->GetEnemyType());
    if (pendingAbsorbType_ != EnemyMorphType::Slime && pendingAbsorbType_ != EnemyMorphType::FireSlime) {
        StartEnemyMorph(enemy);
        return;
    }

    pendingAbsorbEnemy_ = enemy;
    pendingAbsorbEnemyBaseScale_ = hasCarriedEnemyBaseScale_ ? carriedEnemyBaseScale_ : GetStableEnemyCarryScale(enemy);
    pendingAbsorbEnemyStartPos_ = enemy->GetWorldPosition();
    pendingAbsorbTint_ = pendingAbsorbType_ == EnemyMorphType::Slime
        ? Vector4{ 1.0f, 0.42f, 0.82f, 1.0f }
        : GetEnemyMorphTint(pendingAbsorbType_);
    pendingAbsorbPlayerBaseColor_ = GetColor();
    absorbEffectTimer_ = 0.0f;
    absorbEffectEmitTimer_ = 0.0f;
    absorbEffectActive_ = true;

    enemy->SetCarried(true);
    enemy->SetCollisionAttribute(0);
    enemy->SetCollisionMask(0);
    enemy->SetIsVisible(true);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    transform_.scale = { 2.35f, 1.65f, 2.35f };
}

void Player::UpdateAbsorbEffect(float deltaTime)
{
    if (!absorbEffectActive_) {
        return;
    }
    if (!pendingAbsorbEnemy_) {
        CancelAbsorbEffect(false);
        return;
    }
    if (deltaTime <= 0.0f) {
        return;
    }

    absorbEffectTimer_ += deltaTime;
    absorbEffectEmitTimer_ -= deltaTime;

    const float t = std::clamp(absorbEffectTimer_ / kAbsorbEffectDuration, 0.0f, 1.0f);
    const float ease = t * t * (3.0f - 2.0f * t);
    const Vector3 playerPos = GetWorldPosition();
    const Vector3 targetPos = playerPos + Vector3{ 0.0f, 1.35f, 0.0f };

    Vector3 absorbPos{
        Math::Lerp(pendingAbsorbEnemyStartPos_.x, targetPos.x, ease),
        Math::Lerp(pendingAbsorbEnemyStartPos_.y, targetPos.y, ease),
        Math::Lerp(pendingAbsorbEnemyStartPos_.z, targetPos.z, ease)
    };
    absorbPos.y += std::sin(t * kPi) * 0.22f;

    const float scaleRate = Math::Lerp(1.0f, 0.06f, ease);
    const float squash = std::sin(t * kPi) * 0.22f;
    Transform* enemyTransform = pendingAbsorbEnemy_->GetTransform();
    if (enemyTransform) {
        enemyTransform->translate = absorbPos;
        enemyTransform->scale = {
            pendingAbsorbEnemyBaseScale_.x * scaleRate * (1.0f + squash),
            pendingAbsorbEnemyBaseScale_.y * scaleRate * (1.0f - squash * 0.75f),
            pendingAbsorbEnemyBaseScale_.z * scaleRate * (1.0f + squash)
        };
        enemyTransform->rotate.x += 9.0f * deltaTime;
        enemyTransform->rotate.y += 18.0f * deltaTime;
        enemyTransform->rotate.z += 7.0f * deltaTime;
        enemyTransform->isQuaternionMaster = false;
        pendingAbsorbEnemy_->UpdateLocalMatrix();
        pendingAbsorbEnemy_->UpdateWorldMatrix();
    }

    pendingAbsorbEnemy_->SetColor(pendingAbsorbTint_);
    pendingAbsorbEnemy_->SetCollisionAttribute(0);
    pendingAbsorbEnemy_->SetCollisionMask(0);

    const float flash = std::sin(t * kPi);
    const float colorRate = 0.35f + flash * 0.55f;
    Vector4 flashColor{
        std::clamp(pendingAbsorbPlayerBaseColor_.x * (1.0f - colorRate) + pendingAbsorbTint_.x * colorRate + flash * 0.18f, 0.0f, 1.0f),
        std::clamp(pendingAbsorbPlayerBaseColor_.y * (1.0f - colorRate) + pendingAbsorbTint_.y * colorRate + flash * 0.18f, 0.0f, 1.0f),
        std::clamp(pendingAbsorbPlayerBaseColor_.z * (1.0f - colorRate) + pendingAbsorbTint_.z * colorRate + flash * 0.18f, 0.0f, 1.0f),
        1.0f
    };
    SetColor(flashColor);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetColor(flashColor);
        }
    }

    const float bodyPulse = std::sin(t * kPi);
    transform_.scale = {
        2.0f + bodyPulse * 0.55f,
        2.0f - bodyPulse * 0.38f,
        2.0f + bodyPulse * 0.55f
    };

    if (particleSystem_ && absorbEffectEmitTimer_ <= 0.0f) {
        Vector3 toPlayer = targetPos - absorbPos;
        const float length = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y + toPlayer.z * toPlayer.z);
        if (length > 0.001f) {
            toPlayer = toPlayer / length;
        } else {
            toPlayer = { 0.0f, 1.0f, 0.0f };
        }

        const int count = pendingAbsorbType_ == EnemyMorphType::FireSlime ? 14 : 12;
        particleSystem_->SpawnParticles(
            absorbPos,
            count,
            1.4f,
            &toPlayer,
            26.0f,
            pendingAbsorbTint_,
            { pendingAbsorbTint_.x, pendingAbsorbTint_.y, pendingAbsorbTint_.z, 0.0f },
            0.08f,
            0.24f,
            0.34f,
            0.035f
        );
        absorbEffectEmitTimer_ = kAbsorbEffectParticleInterval;
    }

    if (absorbEffectTimer_ >= kAbsorbEffectDuration) {
        FinishAbsorbEffect();
    }
}

void Player::FinishAbsorbEffect()
{
    BaseEnemy* enemy = pendingAbsorbEnemy_;
    if (!enemy) {
        CancelAbsorbEffect(false);
        return;
    }

    const Vector3 burstPos = GetWorldPosition() + Vector3{ 0.0f, 1.2f, 0.0f };
    if (particleSystem_) {
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        particleSystem_->SpawnParticles(
            burstPos,
            pendingAbsorbType_ == EnemyMorphType::FireSlime ? 34 : 30,
            2.0f,
            &up,
            30.0f,
            pendingAbsorbTint_,
            { pendingAbsorbTint_.x, pendingAbsorbTint_.y, pendingAbsorbTint_.z, 0.0f },
            0.12f,
            0.38f,
            0.42f,
            0.05f
        );
    }

    SetColor(pendingAbsorbPlayerBaseColor_);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetColor(pendingAbsorbPlayerBaseColor_);
        }
    }

    transform_.scale = kPlayerBaseScale;
    absorbEffectActive_ = false;
    pendingAbsorbEnemy_ = nullptr;
    pendingAbsorbType_ = EnemyMorphType::None;
    absorbEffectTimer_ = 0.0f;
    absorbEffectEmitTimer_ = 0.0f;

    StartEnemyMorph(enemy);
    transform_.scale = { 2.65f, 0.72f, 2.65f };
}

void Player::CancelAbsorbEffect(bool restoreEnemy)
{
    if (restoreEnemy && pendingAbsorbEnemy_) {
        if (Transform* enemyTransform = pendingAbsorbEnemy_->GetTransform()) {
            enemyTransform->scale = pendingAbsorbEnemyBaseScale_;
            enemyTransform->isQuaternionMaster = true;
            pendingAbsorbEnemy_->UpdateLocalMatrix();
            pendingAbsorbEnemy_->UpdateWorldMatrix();
        }
        pendingAbsorbEnemy_->SetIsVisible(true);
    }

    SetColor(pendingAbsorbPlayerBaseColor_);
    if (!isEnemyMorphed_) {
        for (Object3d* child : GetChildren()) {
            if (child) {
                child->SetColor(pendingAbsorbPlayerBaseColor_);
            }
        }
    }

    absorbEffectActive_ = false;
    pendingAbsorbEnemy_ = nullptr;
    pendingAbsorbType_ = EnemyMorphType::None;
    absorbEffectTimer_ = 0.0f;
    absorbEffectEmitTimer_ = 0.0f;
}

void Player::StartEnemyMorph(BaseEnemy* enemy)
{
    if (!enemy) {
        return;
    }

    const float currentMoveYaw = GetMoveYaw();

    if (!isEnemyMorphed_) {
        savedMorphModelName_ = GetModelName();
        savedMorphTexturePath_ = GetTexturePath();
        savedMorphAnimName_ = animName_;
        savedMorphAnimLoop_ = isAnimLoop_;
        savedMorphColor_ = GetColor();
        savedMorphScale_ = GetScale();
        savedMorphMaterialType_ = GetMaterialType();
        savedMorphEmissive_ = GetEmissive();
        savedMorphSourceVisible_ = enemy->GetIsVisible();
        savedMorphChildVisible_.clear();
        savedMorphChildVisible_.reserve(GetChildren().size());
        for (Object3d* child : GetChildren()) {
            savedMorphChildVisible_.push_back(child ? child->GetIsVisible() : false);
        }
    }

    enemyMorphType_ = ResolveEnemyMorphType(enemy->GetEnemyType());
    enemyMorphSource_ = enemy;
    enemyMorphHasTimeLimit_ = !enemy->param_.has_value() || enemy->param_->morphLimited;
    enemyMorphDuration_ = enemy->param_.has_value()
        ? (std::max)(0.1f, enemy->param_->morphDuration)
        : kEnemyMorphDuration;
    enemyMorphTimer_ = enemyMorphHasTimeLimit_ ? enemyMorphDuration_ : 0.0f;
    enemyMorphEffectTimer_ = 0.0f;
    enemyMorphVisualTimer_ = 0.0f;
    enemyMorphTint_ = GetEnemyMorphTint(enemyMorphType_);
    isEnemyMorphed_ = true;
    SetMoveYaw(currentMoveYaw);
    enemy->SetCarried(true);
    if (enemy->param_.has_value()) {
        enemy->param_->hp = 0.0f;
    }
    enemy->isDead = true;
    enemy->SetIsVisible(false);
    enemy->SetCollisionAttribute(0);
    enemy->SetCollisionMask(0);
    enemy->SetParticleName("");
    enemy->SetGPUParticleName("");
    enemy->SetMeshEffect1Name("");
    enemy->SetMeshEffect2Name("");

    if (!enemy->GetModelName().empty()) {
        SetModel(enemy->GetModelName());
    }
    const std::string enemyTexture = enemy->GetTexturePath();
    if (!enemyTexture.empty()) {
        SetTexture(enemyTexture);
    }
    SetMaterialType(enemy->GetMaterialType());
    SetEmissive(enemy->GetEmissive());
    animName_.clear();
    animationTime_ = 0.0f;
    SetColor(enemy->GetColor());

    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetIsVisible(false);
        }
    }

    if (enemyMorphType_ == EnemyMorphType::ThunderSlime) {
        EmitThunderMorphBurst(GetWorldPosition() + Vector3{ 0.0f, 1.0f, 0.0f });
    }
    else if (particleSystem_ && enemyMorphType_ != EnemyMorphType::Slime && enemyMorphType_ != EnemyMorphType::GiantSlime) {
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        particleSystem_->SpawnParticles(
            GetWorldPosition() + Vector3{ 0.0f, 1.0f, 0.0f },
            32,
            2.2f,
            &up,
            22.0f,
            enemyMorphTint_,
            { enemyMorphTint_.x, enemyMorphTint_.y, enemyMorphTint_.z, 0.0f },
            0.14f,
            0.45f,
            0.42f,
            0.08f
        );
    }

    DebugConsole::GetInstance()->AddLog("Enemy morph start: " + enemy->GetEnemyType());
    if (carriedEnemy_ == enemy) {
        SetCarriedEnemy(nullptr);
    }
}

void Player::UpdateEnemyMorph(float deltaTime)
{
    if (!isEnemyMorphed_) {
        return;
    }

    if (enemyMorphSource_ && enemyMorphSource_->IsDefeatEffectPlaying()) {
        CancelEnemyMorph();
        return;
    }

    if (enemyMorphHasTimeLimit_ && deltaTime > 0.0f) {
        enemyMorphTimer_ -= deltaTime;
        enemyMorphEffectTimer_ -= deltaTime;
        enemyMorphVisualTimer_ += deltaTime;
    } else if (!enemyMorphHasTimeLimit_ && deltaTime > 0.0f) {
        enemyMorphEffectTimer_ -= deltaTime;
        enemyMorphVisualTimer_ += deltaTime;
    }

    if (enemyMorphSource_) {
        enemyMorphSource_->SetIsVisible(false);
    }

    if (enemyMorphHasTimeLimit_ && enemyMorphTimer_ <= 0.0f) {
        CancelEnemyMorph();
        return;
    }

    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetIsVisible(false);
        }
    }

    if (enemyMorphEffectTimer_ <= 0.0f) {
        if (enemyMorphType_ == EnemyMorphType::ThunderSlime) {
            const float morphElapsed = (std::max)(0.0f, enemyMorphDuration_ - enemyMorphTimer_);
            EmitOuterThunderParticles(GetWorldPosition(), GetScale(), morphElapsed * 3.4f, 3);
            enemyMorphEffectTimer_ = 0.32f;
            return;
        }
        else if (particleSystem_) {
            Vector3 up = { 0.0f, 1.0f, 0.0f };
            particleSystem_->SpawnParticles(
                GetWorldPosition() + Vector3{ 0.0f, 1.1f, 0.0f },
                6,
                1.2f,
                &up,
                12.0f,
                enemyMorphTint_,
                { enemyMorphTint_.x, enemyMorphTint_.y, enemyMorphTint_.z, 0.0f },
                0.08f,
                0.22f,
                0.26f,
                0.03f
            );
        }
        enemyMorphEffectTimer_ = kEnemyMorphParticleInterval;
    }
}

float Player::GetEnemyMorphRate() const
{
    if (!isEnemyMorphed_) {
        return 0.0f;
    }
    if (!enemyMorphHasTimeLimit_) {
        return 1.0f;
    }
    if (enemyMorphDuration_ <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(enemyMorphTimer_ / enemyMorphDuration_, 0.0f, 1.0f);
}

bool Player::IsPinkSlimeMorphed() const
{
    return isEnemyMorphed_ && enemyMorphType_ == EnemyMorphType::Slime;
}

void Player::CancelEnemyMorph()
{
    if (absorbEffectActive_) {
        CancelAbsorbEffect(true);
    }

    if (!isEnemyMorphed_) {
        return;
    }

    const float currentMoveYaw = GetMoveYaw();
    BaseEnemy* morphSource = enemyMorphSource_;

    if (auto* slime = dynamic_cast<EnemySlime*>(morphSource)) {
        slime->CancelCarriedAbility(this);
    }

    if (!savedMorphModelName_.empty()) {
        SetModel(savedMorphModelName_);
    }
    if (!savedMorphTexturePath_.empty()) {
        SetTexture(savedMorphTexturePath_);
    } else {
        SetTexture("");
    }
    SetMaterialType(savedMorphMaterialType_);
    SetEmissive(savedMorphEmissive_);
    SetColor(savedMorphColor_);
    SetScale(savedMorphScale_);
    animName_ = savedMorphAnimName_;
    isAnimLoop_ = savedMorphAnimLoop_;
    animationTime_ = 0.0f;

    const auto& children = GetChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        Object3d* child = children[i];
        if (child) {
            child->SetColor(savedMorphColor_);
            if (i < savedMorphChildVisible_.size()) {
                child->SetIsVisible(savedMorphChildVisible_[i]);
            } else {
                child->SetIsVisible(true);
            }
        }
    }

    if (morphSource && !morphSource->isDead && !morphSource->IsDefeatEffectPlaying()) {
        morphSource->SetIsVisible(savedMorphSourceVisible_);
    }
    if (auto* fireSlime = dynamic_cast<EnemyFireSlime*>(morphSource)) {
        fireSlime->ReleaseCarriedAbilityVisuals();
    }
    if (carriedEnemy_ == morphSource) {
        SetCarriedEnemy(nullptr);
    }

    isEnemyMorphed_ = false;
    enemyMorphType_ = EnemyMorphType::None;
    enemyMorphSource_ = nullptr;
    enemyMorphHasTimeLimit_ = true;
    enemyMorphTimer_ = 0.0f;
    enemyMorphEffectTimer_ = 0.0f;
    enemyMorphVisualTimer_ = 0.0f;
    savedMorphChildVisible_.clear();
    savedMorphSourceVisible_ = true;
    SetMoveYaw(currentMoveYaw);
    DebugConsole::GetInstance()->AddLog("Enemy morph end.");
}

void Player::StartElectricShockFeedback(float duration, float invincibleDuration)
{
    duration = (std::max)(0.0f, duration);
    electricShockPendingInvincibleDuration_ = (std::max)(
        electricShockPendingInvincibleDuration_,
        (std::max)(0.0f, invincibleDuration)
    );

    if (duration <= 0.0f) {
        EndElectricShockFeedback();
        return;
    }

    RestoreDamageBlinkVisibility();

    if (!electricShockControlLocked_) {
        electricShockWasControlActive_ = isControlActive_;
        electricShockLockedPosition_ = GetTranslate();
        electricShockBaseScale_ = GetScale();
        electricShockBaseRotation_ = GetRotation();
        electricShockControlLocked_ = true;
    }

    electricShockFeedbackTimer_ = (std::max)(electricShockFeedbackTimer_, duration);
    electricShockFeedbackTotalDuration_ = (std::max)(electricShockFeedbackTotalDuration_, electricShockFeedbackTimer_);
    electricShockFeedbackEmitTimer_ = 0.0f;

    SetIsControlActive(false);
    SetVelocity({ 0.0f, 0.0f, 0.0f });
    SetTranslate(electricShockLockedPosition_);
    UpdateElectricShockAuraEffect(1.0f / 60.0f);
}

void Player::UpdateElectricShockFeedback(float deltaTime)
{
    if (electricShockFeedbackTimer_ <= 0.0f || deltaTime <= 0.0f) {
        return;
    }

    const float previousShockTimer = electricShockFeedbackTimer_;
    electricShockFeedbackTimer_ = (std::max)(0.0f, electricShockFeedbackTimer_ - deltaTime);
    electricShockFeedbackEmitTimer_ -= deltaTime;

    const float totalDuration = (std::max)(electricShockFeedbackTotalDuration_, 0.001f);
    const float elapsed = (std::max)(0.0f, totalDuration - electricShockFeedbackTimer_);
    const float flicker = std::sin(elapsed * 74.0f);
    const Vector4 base = isEnemyMorphed_ ? GetEnemyMorphTint(enemyMorphType_) : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    SetColor({
        (std::clamp)(base.x + 0.24f * std::max(0.0f, flicker), 0.0f, 1.0f),
        (std::clamp)(base.y + 0.18f, 0.0f, 1.0f),
        (std::clamp)(base.z + 0.42f * std::max(0.0f, -flicker), 0.0f, 1.0f),
        base.w
    });

    if (electricShockControlLocked_) {
        Vector3 shockPosition = electricShockLockedPosition_;
        shockPosition.x += std::sin(elapsed * 94.0f) * kElectricShockShakeAmount;
        shockPosition.z += std::cos(elapsed * 83.0f) * kElectricShockShakeAmount * 0.75f;
        SetTranslate(shockPosition);
        SetVelocity({ 0.0f, 0.0f, 0.0f });

        const float squash = 0.055f + std::abs(std::sin(elapsed * 46.0f)) * 0.075f;
        SetScale({
            electricShockBaseScale_.x * (1.0f + squash),
            electricShockBaseScale_.y * (1.0f - squash * 0.65f),
            electricShockBaseScale_.z * (1.0f + squash * 0.9f)
        });

        Vector3 shockRotation = electricShockBaseRotation_;
        shockRotation.x += std::sin(elapsed * 67.0f) * 0.045f;
        shockRotation.z += std::sin(elapsed * 89.0f) * 0.075f;
        SetRotation(shockRotation);
    }

    UpdateElectricShockAuraEffect(deltaTime);

    if (electricShockFeedbackEmitTimer_ <= 0.0f) {
        EmitOuterThunderParticles(GetWorldPosition(), GetScale(), elapsed * 3.4f, 5);
        electricShockFeedbackEmitTimer_ = 0.045f;
    }

    if (previousShockTimer > 0.0f && electricShockFeedbackTimer_ <= 0.0f) {
        EndElectricShockFeedback();
    }
}

void Player::EndElectricShockFeedback()
{
    if (electricShockControlLocked_) {
        SetTranslate(electricShockLockedPosition_);
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        SetScale(electricShockBaseScale_);
        SetRotation(electricShockBaseRotation_);
        SetIsControlActive(!isDead && electricShockWasControlActive_);
        electricShockControlLocked_ = false;
    }

    electricShockFeedbackTimer_ = 0.0f;
    electricShockFeedbackEmitTimer_ = 0.0f;
    electricShockFeedbackTotalDuration_ = 0.0f;
    HideElectricShockAuraEffect();
    UpdateColor();

    const float invincibleDuration = electricShockPendingInvincibleDuration_;
    electricShockPendingInvincibleDuration_ = 0.0f;
    if (invincibleDuration > 0.0f && !isDead) {
        damageCooldownTimer_ = (std::max)(damageCooldownTimer_, invincibleDuration);
        invincibleBlinkTimer_ = 0.0f;
        SetDamageInvincible(true);
    }
}

void Player::InitializeElectricShockAuraEffect()
{
    if (!common_ || electricShockAuraEffect_) {
        return;
    }

    electricShockAuraEffect_ = std::make_unique<EffectObject3d>();
    electricShockAuraEffect_->Initialize(common_);
    electricShockAuraEffect_->SetName("Player_ElectricShockAura");
    if (!electricShockAuraEffect_->LoadFromJson(kElectricShockAuraEffectPath)) {
        electricShockAuraEffect_.reset();
        return;
    }
    electricShockAuraEffect_->SetIsVisible(false);
    electricShockAuraEffect_->Play(99999.0f);
}

void Player::UpdateElectricShockAuraEffect(float deltaTime)
{
    if (!electricShockAuraEffect_) {
        InitializeElectricShockAuraEffect();
    }
    if (!electricShockAuraEffect_) {
        return;
    }

    Vector3 auraPos{};
    float horizontalDiameter = 1.0f;
    float verticalDiameter = 1.0f;
    CalculateElectricShockAuraShape(auraPos, horizontalDiameter, verticalDiameter);

    float yaw = GetRotation().y;
    Camera* camera = CameraManager::GetInstance() ? CameraManager::GetInstance()->GetActiveCamera() : nullptr;
    if (camera) {
        const Vector3 toCamera = camera->GetEye() - auraPos;
        if (std::abs(toCamera.x) + std::abs(toCamera.z) > 0.001f) {
            yaw = std::atan2(toCamera.x, toCamera.z);
        }
    }

    electricShockAuraEffect_->SetIsVisible(true);
    electricShockAuraEffect_->Update(deltaTime);
    electricShockAuraEffect_->SetTranslate(auraPos);
    electricShockAuraEffect_->SetRotation({ 1.5707963f, yaw, 0.0f });
    electricShockAuraEffect_->SetScale({ horizontalDiameter, 1.0f, verticalDiameter });
    electricShockAuraEffect_->UpdateLocalMatrix();
    electricShockAuraEffect_->UpdateWorldMatrix();
}

void Player::HideElectricShockAuraEffect()
{
    if (electricShockAuraEffect_) {
        electricShockAuraEffect_->SetIsVisible(false);
    }
}

void Player::CalculateElectricShockAuraShape(Vector3& center, float& horizontalDiameter, float& verticalDiameter) const
{
    const Vector3 scale = GetScale();
    const float maxXZ = (std::max)({ std::abs(scale.x), std::abs(scale.z), 1.0f });
    const float yScale = (std::max)(std::abs(scale.y), 0.18f);

    horizontalDiameter = (std::max)(2.55f, maxXZ * 1.35f);
    verticalDiameter = (std::max)(0.46f, yScale * 1.28f);

    center = GetWorldPosition();
    center.y += (std::max)(0.22f, yScale * 0.43f);
}

Player::EnemyMorphType Player::ResolveEnemyMorphType(const std::string& enemyType) const
{
    if (enemyType == "Bomber") return EnemyMorphType::Bomber;
    if (enemyType == "Bat") return EnemyMorphType::Bat;
    if (enemyType == "BeamDrone") return EnemyMorphType::BeamDrone;
    if (enemyType == "Mushroom") return EnemyMorphType::Mushroom;
    if (enemyType == "GiantSlime") return EnemyMorphType::GiantSlime;
    if (enemyType == "FireSlime") return EnemyMorphType::FireSlime;
    if (enemyType == "ThunderSlime") return EnemyMorphType::ThunderSlime;
    if (enemyType == "Slime") return EnemyMorphType::Slime;
    return EnemyMorphType::None;
}

Vector4 Player::GetEnemyMorphTint(EnemyMorphType type) const
{
    switch (type) {
    case EnemyMorphType::Bomber:
        return { 1.0f, 0.45f, 0.22f, 1.0f };
    case EnemyMorphType::Bat:
        return { 0.55f, 0.64f, 1.0f, 1.0f };
    case EnemyMorphType::BeamDrone:
        return { 0.42f, 0.95f, 1.0f, 1.0f };
    case EnemyMorphType::Mushroom:
        return { 1.0f, 0.34f, 0.72f, 1.0f };
    case EnemyMorphType::GiantSlime:
        return { 1.0f, 0.50f, 0.86f, 1.0f };
    case EnemyMorphType::FireSlime:
        return { 1.0f, 0.24f, 0.16f, 1.0f };
    case EnemyMorphType::ThunderSlime:
        return { 1.0f, 0.92f, 0.18f, 1.0f };
    case EnemyMorphType::Slime:
        return { 1.0f, 0.46f, 0.86f, 1.0f };
    default:
        return { 1.0f, 1.0f, 1.0f, 1.0f };
    }
}

void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource)
{
    Character::Draw(pointLightResource, spotLightResource);
    if (electricShockAuraEffect_ && electricShockAuraEffect_->GetIsVisible()) {
        electricShockAuraEffect_->Draw(pointLightResource, spotLightResource);
    }
    // ※フックマーカーはプレイヤーがカメラ外（フラスタムカリング）になった時でも
    // 描画されるように、GamePlayScene 側で直接描画するように変更しました。
}

// =================================================================
// 衝突処理
// =================================================================

bool Player::OnCollision(Object3d* other)
{
    if (isDead) {
        return false;
    }

    if (!other) return false;

    if (other == carriedEnemy_) {
        return false;
    }

    if (BaseEnemy* enemy = dynamic_cast<BaseEnemy*>(other)) {
        if (enemy->IsCarried()) {
            enemy->SetCollisionAttribute(0);
            enemy->SetCollisionMask(0);
            return false;
        }
    }

    // 衝突相手の属性を取得
    uint32_t attribute = 0;
    if (other->GetCollider())
    {
        attribute = other->GetCollider()->GetAttribute();
    }

    // ① まずプレイヤー本体の当たり判定を計算
    CollisionInfo info = CheckCollision(other);

    // ② 子オブジェクト（パーツ）の当たり判定も全てチェックする
    for (Object3d* child : GetChildren())
    {
        if (!child) continue;
        CollisionInfo childInfo = child->CheckCollision(other);
        if (childInfo.isColliding)
        {
            // パーツがぶつかっていた場合、本体よりめり込みが深ければ
            // そのパーツの押し出し情報（法線とめり込み量）を採用して親を動かす！
            if (!info.isColliding || childInfo.penetration > info.penetration)
            {
                info = childInfo;
            }
        }
    }

    // 本体もパーツも当たっていなければ終了
    if (!info.isColliding)
    {
        return false;
    }

    // =======================================================
    // 1. 物理挙動の適用 (ソリッドな壁や床からの押し戻し)
    // 無敵中でも壁抜けは厳禁なので、一番最初に処理します。
    // =======================================================
    if (other->GetGimmickType() == "OneWayFloor") {
        bool canStand = info.normal.y > 0.55f && velocity_.y <= 1.0f;
        if (!canStand) {
            return false;
        }
    }

    if (attribute & kAllSolid)
    {
        ApplyPhysicsCollision(info, attribute);
    }

    // =======================================================
    // 2. ダメージ処理 / 踏みつけ攻撃 / 突進攻撃
    // =======================================================
    if (attribute & (kEnemy | kEnemyAttack))
    {
        // 突進（タックル）判定
        if ((attribute & kEnemy) && mover_->IsDashing())
        {
            // 敵に大ダメージ
            DamageEvent tackleDmg;
            tackleDmg.target = other;
            tackleDmg.attacker = this;
            tackleDmg.damageAmount = 30.0f; // 踏みつけより強力！

            // 吹き飛ばしベクトル：向いている方向に斜め上に弾き飛ばす
            float yaw = transform_.rotate.y;
            Vector3 pushDir = { std::sin(yaw), 0.4f, std::cos(yaw) };
            tackleDmg.knockbackVelocity = pushDir * 50.0f; // 結構な勢いで飛ばす

            EventManager::GetInstance()->Dispatch(tackleDmg);

            DebugConsole::GetInstance()->AddLog("Slime Tackle! Enemy Blasted.");
            return true; // 突進中はダメージを受けず、相手を倒す
        }

        // 踏みつけ判定: 敵本体(kEnemy)かつ上方からの衝突かを確認
        // 判定を安定させるため、法線のしきい値を0.5(60度)に広げ、速度条件も緩和します
        bool isAbove = GetWorldPosition().y > other->GetWorldPosition().y;
        if ((attribute & kEnemy) && info.normal.y > 0.5f && isAbove)
        {
            // 敵にダメージ (踏みつけ成功！)
            DamageEvent enemyDmg;
            enemyDmg.target = other;
            enemyDmg.attacker = this;
            enemyDmg.damageAmount = 10.0f;
            EventManager::GetInstance()->Dispatch(enemyDmg);

            // プレイヤーを上に跳ね返らせる
            Vector3 v = GetVelocity();
            v.y = 15.0f; // 跳ね返り力を少し強化
            SetVelocity(v);
            ChangeState(std::make_unique<PlayerStateJump>());

            DebugConsole::GetInstance()->AddLog("Stomp Success!");
            return true; // 踏みつけ成功時は自身のダメージ処理をスキップ
        }

        // 通常のダメージ処理 (被弾)
        if (damageCooldownTimer_ <= 0.0f && !IsInvincible())
        {
            const bool isThunderSlimeContact = other->GetEnemyType() == "ThunderSlime";

            // ダメージイベントを発行 (GameRule で HP 減少が処理される)
            DamageEvent dmgEvent;
            dmgEvent.target = this;
            dmgEvent.attacker = other;
            dmgEvent.damageAmount = 1.0f; // 1ダメージを与える
            EventManager::GetInstance()->Dispatch(dmgEvent);

            if (isThunderSlimeContact) {
                damageCooldownTimer_ = 0.8f;
            }
            else {
                // 無敵時間をセット
                damageCooldownTimer_ = 1.0f;
                SetDamageInvincible(true);

                // ノックバック状態へ移行 (衝突法線を利用)
                ChangeState(std::make_unique<PlayerStateDamage>(info.normal));
            }
        }
    }

    // =======================================================
    // 3. ギミック・汎用イベントの発行
    // =======================================================
    // ワープやスイッチなど、ダメージ以外の判定のために通知します。
    PlayerHitEvent hitEvent;
    hitEvent.me = this;
    hitEvent.hitObject = other;
    hitEvent.normal = info.normal;
    EventManager::GetInstance()->Dispatch(hitEvent);

    return true;
}
// =================================================================
// 移動制御 (Strategy Pattern)
// =================================================================

void Player::SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy)
{
    if (mover_)
    {
        mover_->SetStrategy(std::move(strategy));
    }
}

void Player::ApplyDashPanelBoost(float duration, float speedMultiplier, float turnMultiplier)
{
    if (mover_)
    {
        mover_->ApplyDashPanelBoost(duration, speedMultiplier, turnMultiplier);
    }
}

void Player::ApplyIceSurface(float duration, float friction, float steering)
{
    if (mover_)
    {
        mover_->ApplyIceSurface(duration, friction, steering);
    }
}

// =================================================================
// アニメーション・状態管理 (State Pattern)
// =================================================================

void Player::ChangeState(std::unique_ptr<IAnimationState> newState)
{
    // 現在の状態を終了
    if (state_)
    {
        state_->Exit(this);
    }

    // 状態を切り替えて開始
    state_ = std::move(newState);

    if (state_)
    {
        state_->Enter(this);
    }
    else
    {
        DebugConsole::GetInstance()->AddLog("[ERROR] Failed to set new state.");
    }
}

void Player::PlayAnimation(const std::string& animName, bool loop)
{
    // 既に同じアニメーションが再生中ならリセットしない (滑らかな遷移のため)
    if (animName_ != animName)
    {
        animName_ = animName;
        animationTime_ = 0.0f;
    }
    isAnimLoop_ = loop;
}

void Player::SetSlimeAnimationMode(PlayerSlimeAnimator::Mode mode)
{
    slimeAnimator_.SetMode(mode);
}

void Player::SetSlimeAnimationDirection(const Vector3& direction)
{
    slimeAnimator_.SetMotionDirection(direction);
}

void Player::SetSlimePullDirection(const Vector3& direction)
{
    slimeAnimator_.SetPullDirection(direction);
}

void Player::SetSlimePullProgress(float progress)
{
    slimeAnimator_.SetPullProgress(progress);
}

void Player::SetSlimeJumpCharge(float chargeRate)
{
    slimeAnimator_.SetJumpCharge(chargeRate);
}

void Player::TriggerSlimeImpulse(const Vector3& scale, float duration)
{
    slimeAnimator_.TriggerImpulse(scale, duration);
}

void Player::ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode mode, const Vector3& direction)
{
    forcedSlimeAnimationModeActive_ = true;
    forcedSlimeAnimationMode_ = mode;
    forcedSlimeAnimationDirection_ = direction;
}

void Player::StartGateReturnAnimation(const PlayerGateReturnAnimation::Route& route)
{
    gateReturnAnimation_.Start(this, route);
}

bool Player::IsGateReturnAnimationActive() const
{
    return gateReturnAnimation_.IsActive();
}

bool Player::IsGateReturnAnimationFinished() const
{
    return gateReturnAnimation_.IsFinished();
}

void Player::StopGateReturnAnimation(bool restoreControl)
{
    gateReturnAnimation_.Stop(this, restoreControl);
}

// =======================================================
// 無敵関連の実装
// =======================================================
void Player::SetDamageInvincible(bool inv) {
    isDamageInvincible_ = inv;
    UpdateColor(); // 状態が変わったら色を更新
}

void Player::SetDashInvincible(bool inv) {
    isDashInvincible_ = inv;
    UpdateColor(); // 状態が変わったら色を更新
}

void Player::RestoreDamageBlinkVisibility() {
    if (!damageBlinkVisibilityApplied_) {
        return;
    }

    SetIsVisible(damageBlinkBodyVisible_);
    if (!isEnemyMorphed_) {
        const auto& children = GetChildren();
        for (size_t i = 0; i < children.size(); ++i) {
            Object3d* child = children[i];
            if (!child) {
                continue;
            }
            if (i < damageBlinkChildVisible_.size()) {
                child->SetIsVisible(damageBlinkChildVisible_[i]);
            }
        }
    }

    damageBlinkVisibilityApplied_ = false;
    damageBlinkBodyVisible_ = true;
    damageBlinkChildVisible_.clear();
}

void Player::UpdateDamageInvincibleBlinkVisibility() {
    if (electricShockFeedbackTimer_ > 0.0f) {
        RestoreDamageBlinkVisibility();
        return;
    }

    if (isDamageInvincible_) {
        if (!damageBlinkVisibilityApplied_) {
            damageBlinkVisibilityApplied_ = true;
            damageBlinkBodyVisible_ = GetIsVisible();
            damageBlinkChildVisible_.clear();
            if (!isEnemyMorphed_) {
                damageBlinkChildVisible_.reserve(GetChildren().size());
                for (Object3d* child : GetChildren()) {
                    damageBlinkChildVisible_.push_back(child ? child->GetIsVisible() : false);
                }
            }
        }

        const float blinkPhase = std::fmod(invincibleBlinkTimer_, 0.16f);
        const bool blinkVisible = blinkPhase < 0.095f;
        SetIsVisible(damageBlinkBodyVisible_ && blinkVisible);
        if (!isEnemyMorphed_) {
            const auto& children = GetChildren();
            for (size_t i = 0; i < children.size(); ++i) {
                Object3d* child = children[i];
                if (!child) {
                    continue;
                }
                const bool originalVisible = i < damageBlinkChildVisible_.size()
                    ? damageBlinkChildVisible_[i]
                    : child->GetIsVisible();
                child->SetIsVisible(originalVisible && blinkVisible);
            }
        }
        return;
    }

    if (!damageBlinkVisibilityApplied_) {
        return;
    }

    RestoreDamageBlinkVisibility();
}

void Player::UpdateColor() {
    Vector4 targetColor = isEnemyMorphed_
        ? GetEnemyMorphTint(enemyMorphType_)
        : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }; // 基本は白(通常色)

    if (isDashInvincible_) {
        targetColor = { 0.0f, 0.0f, 1.0f, 1.0f }; // 回避中は青色を設定
    }

    // 本体と子パーツの色を一括変更
    SetColor(targetColor);
    if (!isEnemyMorphed_) {
        for (Object3d* child : GetChildren()) {
            if (child) child->SetColor(targetColor);
        }
    }
}

// =======================================================
// コンボ時間窓 API 実装
// =======================================================
void Player::StartComboWindow(float duration)
{
    comboWindowTimer_ = std::max(0.0f, duration);
}

bool Player::IsComboWindowActive() const
{
    return comboWindowTimer_ > 0.0f;
}

// =======================================================
// 攻撃入力バッファ API 実装
// =======================================================
void Player::RecordAttackInput(float duration)
{
    attackInputBuffered_ = true;
    attackInputBufferTimer_ = std::max(0.0f, duration);
    attackBufferUsedForStateStart_ = false;
}

void Player::MarkAttackBufferUsedForStateStart()
{
    if (attackInputBuffered_) attackBufferUsedForStateStart_ = true;
}

bool Player::ConsumeBufferedAttackInput()
{
    if (attackInputBuffered_ && !attackBufferUsedForStateStart_)
    {
        // consume
        attackInputBuffered_ = false;
        attackInputBufferTimer_ = 0.0f;
        attackBufferUsedForStateStart_ = false;
        return true;
    }
    return false;
}
