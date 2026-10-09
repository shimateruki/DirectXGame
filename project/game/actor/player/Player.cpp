#include "game/actor/player/Player.Internal.h"

using namespace PlayerImplementation;

Player::~Player() = default;

bool Player::IsImpactBreakActive() const {
    const bool baseImpact = baseCombatController_ && baseCombatController_->CanBreakImpactGate();
    const bool copyImpact = copyAbilityController_ && copyAbilityController_->CanBreakImpactGate();
    return baseImpact || copyImpact;
}

bool Player::IsPinkBounceSlamImpactActive() const {
    return copyAbilityController_ && copyAbilityController_->IsPinkBounceSlamImpactActive();
}

void Player::Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem,
                        SpriteCommon* spriteCommon) {
    // 親クラス(Character)の初期化
    Character::Initialize(common);

    // 外部システムの依存注入
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;
    baseCombatController_ = std::make_unique<PlayerBaseCombatController>();
    copyAbilityController_ = std::make_unique<PlayerCopyAbilityController>();

    // 自機としての基本設定
    SetClassName("Player");
    SetSaveCategory("Player");
    if (!param_.has_value()) {
        param_.emplace();
    }
    jumpCount_ = 0;

    // スライムの潰れ・伸びは見た目だけに適用し、接地判定の高さを変化させません。
    // コライダーが描画scaleへ追従すると、歩行周期ごとに押し戻しが発生して
    // Player座標と追従カメラが上下してしまいます。
    if (GetCollider()) {
        GetCollider()->SetScaleOverride(managedBaseScale_);
    }

    // 移動コンポーネントの構築
    mover_ = std::make_unique<PlayerMover>();
    mover_->Initialize(this, inputManager, particleSystem);
    auto* statusManager = GameplayStatusManager::GetInstance();
    statusManager->Initialize();
    statusManager->ApplyPlayerStatus(this, true);

    // ステートマシン初期化 (待機状態からスタート)
    ChangeState(std::make_unique<PlayerStateIdle>());

    // フック用の到達地点マーカーを初期化
    if (common) {
        hookMarker_ = std::make_unique<Object3d>();
        hookMarker_->Initialize(common);
        hookMarker_->SetModel("Characters/slimeBody");
        // スライムっぽい半透明な緑色
        hookMarker_->SetColor({0.3f, 1.0f, 0.5f, 0.6f});
        hookMarker_->SetIsVisible(false);
        // 当たり判定は不要
        hookMarker_->SetCollisionAttribute(0);
        hookMarker_->SetCollisionMask(0);
        // マーカーサイズを少し小さめに
        hookMarker_->GetTransform()->scale = {0.5f, 0.5f, 0.5f};

        // 解除時に排出するスライム成分。シーンへ登録せず、Playerが描画まで管理します。
        enemyMorphReleaseVisual_ = std::make_unique<Object3d>();
        enemyMorphReleaseVisual_->Initialize(common);
        enemyMorphReleaseVisual_->SetName("EnemyMorphReleaseResidue");
        enemyMorphReleaseVisual_->SetClassName("Effect");
        enemyMorphReleaseVisual_->SetModel("Characters/slimeBody");
        enemyMorphReleaseVisual_->SetTexture("Resources/sprite/common/white.png");
        enemyMorphReleaseVisual_->SetMaterialType(0);
        enemyMorphReleaseVisual_->SetEmissive(1.35f);
        enemyMorphReleaseVisual_->SetCollisionAttribute(0);
        enemyMorphReleaseVisual_->SetCollisionMask(0);
        enemyMorphReleaseVisual_->SetIsVisible(false);
    }
}

void Player::ApplyManagedScale(const Vector3& scale) {
    managedBaseScale_ = scale;
    SetScale(scale);
    if (GetCollider()) {
        GetCollider()->SetScaleOverride(scale);
    }
    slimeAnimator_.Reset(scale, this);
}

Vector3 Player::GetLockOnAttackDirection() const {
    if (!isLockingOn_ || !lockOnAimValid_ || isDead || isCinematicLocked_)
        return GetForwardDirection();
    Vector3 direction = lockOnAimPoint_ - GetWorldPosition();
    direction.y = 0.0f;
    const float length = Math::Length(direction);
    return length > 0.001f ? direction / length : GetForwardDirection();
}

// 入力、状態、移動、見た目の順に更新します。演出が操作権を持つ間は通常操作へ流しません。
void Player::Update(float deltaTime) {
    if (mover_ && deltaTime > 0.0f) {
        mover_->SampleJumpInput(deltaTime, isDead || isCinematicLocked_ || launchStarController_.IsActive() ||
                                               gateReturnAnimation_.IsActive() || absorbEffectActive_ ||
                                               enemyMorphReleaseActive_ || copyPreviewActive_ ||
                                               electricShockControlLocked_);
    }
    // 隔離プレビューは固定刻み側だけが状態を進めます。Scene側のUpdate(0)で
    // 接地や保留入力を再評価すると、コマ送り結果が同じフレーム内で壊れます。
    if (copyPreviewActive_ && deltaTime <= 0.0f) {
        UpdateWorldMatrix(true);
        return;
    }
    if (copyPreviewActive_)
        SetIsControlActive(false);
    // 初回更新時に初期位置と初期回転を記録
    if (isFirstUpdate_) {
        respawnPosition_ = transform_.translate;
        baseRotation_ = ResolveTransformEuler(transform_);
        SetRotation(baseRotation_);
        isFirstUpdate_ = false;
    }

    if (launchStarController_.IsActive()) {
        UpdateLaunchStar(deltaTime);
        Object3d::Update(deltaTime);
        return;
    }

    if (isCinematicLocked_) {
        SetIsControlActive(false);
        SetVelocity({0.0f, 0.0f, 0.0f});
        return;
    }

    if (gateReturnAnimation_.IsActive()) {
        gateReturnAnimation_.Update(this, deltaTime);
        UpdateEnemyMorph(deltaTime);
        return;
    }

    const bool copyAbilityInputMode =
        isEnemyMorphed_ && enemyMorphType_ != EnemyMorphType::None && !enemyMorphReleaseActive_ && !absorbEffectActive_;
    const bool hookAimHeld =
        inputManager_ && inputManager_->IsActionPressed("SecondaryAction") && !copyAbilityInputMode;

    if (copyAbilityInputMode) {
        // コピー中の右クリックは能力専用です。通常形態のフック照準を持ち越しません。
        if (hookMarker_) {
            hookMarker_->SetIsVisible(false);
        }
        aimTargetObject_ = nullptr;
    }

    // 停止フレームでは入力・タイマー・状態遷移を進めず、表示用の更新だけを残します。
    if (deltaTime > 0.0f) {
        // 被弾無敵と回避無敵は別の発生源なので、期限も独立して管理します。
        if (damageCooldownTimer_ > 0.0f) {
            damageCooldownTimer_ -= deltaTime;
            if (damageCooldownTimer_ <= 0.0f) {
                damageCooldownTimer_ = 0.0f;
                SetDamageInvincible(false);
            }
        }

        if (evasionInvincibleTimer_ > 0.0f) {
            evasionInvincibleTimer_ -= deltaTime;
            if (evasionInvincibleTimer_ <= 0.0f) {
                evasionInvincibleTimer_ = 0.0f;
                isEvasionInvincible_ = false;
                UpdateColor();
            }
        }

        if (electricShockControlLocked_) {
            SetIsControlActive(false);
            SetVelocity({0.0f, 0.0f, 0.0f});
            SetTranslate(electricShockLockedPosition_);
        }

        // コピー能力に右クリックを渡す間は、通常形態のフック照準を開始しません。
        if (isControlActive_ && mover_) {
            // 右クリック中は移動入力を受け付けず、水平移動を停止させる
            if (hookAimHeld) {
                SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::AimHook);
                Vector3 v = GetVelocity();
                SetVelocity({0.0f, v.y, 0.0f});

                Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                if (camera) {
                    Vector3 camRot = camera->GetRotation();
                    SetRotation(
                        {baseRotation_.x + camRot.x, NormalizeYaw(camRot.y + GetVisualYawOffset()), baseRotation_.z});

                    // --- フック到達地点の計算とマーカー表示 ---
                    if (hookMarker_) {
                        Vector3 start = camera->GetEye();

                        Vector3 dir;
                        dir.x = std::sin(camRot.y) * std::cos(camRot.x);
                        dir.y = -std::sin(camRot.x);
                        dir.z = std::cos(camRot.y) * std::cos(camRot.x);

                        float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                        if (length > 0.0f) {
                            dir.x /= length;
                            dir.y /= length;
                            dir.z /= length;
                        }

                        // ロック中はカメラからではなく、自機から対象の判定中心へ狙います。
                        // 三人称カメラの壁越し判定や、小さい敵の頭上への空振りを防ぎます。
                        if (isLockingOn_ && lockOnAimValid_) {
                            const AABB bounds = GetAABB();
                            start = (bounds.min + bounds.max) * 0.5f;
                            const Vector3 toTarget = lockOnAimPoint_ - start;
                            if (Math::Length(toTarget) > 0.001f)
                                dir = Math::Normalize(toTarget);
                            const float planarLength = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                            SetRotation({baseRotation_.x - std::atan2(dir.y, planarLength),
                                         NormalizeYaw(std::atan2(dir.x, dir.z) + GetVisualYawOffset()),
                                         baseRotation_.z});
                        }
                        float maxDistance = 150.0f;
                        uint32_t mask = CollisionCategories::Get("ground") | CollisionCategories::Get("enemy") | CollisionCategories::Get("hook_anchor");

                        RaycastHit hit;
                        if (isLockingOn_) {
                            // 球のローカルオフセットにも対応するQuery版を使い、自分と子パーツを除外します。
                            PhysicsQueryFilter filter;
                            filter.mask = mask;
                            filter.ignoredObject = this;
                            filter.includeTriggers = true;
                            if (lockOnAimValid_) {
                                hit = CollisionManager::GetInstance()->Raycast(start, dir, maxDistance, filter);
                            }
                        } else {
                            // 非ロック時の自由照準・フックアンカーの操作は維持します。
                            hit = CollisionManager::GetInstance()->Raycast(start, dir, maxDistance, mask);
                        }

                        if (hit.isHit) {
                            hookMarker_->SetIsVisible(true);
                            hookMarker_->GetTransform()->translate = hit.hitPoint;
                            aimTargetObject_ = hit.hitObject;

                            // 命中先の種類を色と脈動で示し、フック可能な対象を照準中に判別できるようにします。
                            if (aimTargetObject_ && aimTargetObject_->GetGimmickType() == "HookAnchor") {
                                hookMarker_->SetColor({0.2f, 0.85f, 1.0f, 1.0f});
                                static float pulseTimer = 0.0f;
                                pulseTimer += deltaTime;
                                float pulse = 1.25f + std::sin(pulseTimer * 10.0f) * 0.25f;
                                hookMarker_->GetTransform()->scale = {pulse, pulse, pulse};
                            } else if (aimTargetObject_ && (aimTargetObject_->GetCollisionAttribute() & CollisionCategories::Get("enemy"))) {
                                hookMarker_->SetColor({1.0f, 0.2f, 0.2f, 1.0f}); // 赤色に変更
                                static float pulseTimer = 0.0f;
                                pulseTimer += deltaTime;
                                float pulse = 1.2f + std::sin(pulseTimer * 15.0f) * 0.3f;
                                hookMarker_->GetTransform()->scale = {pulse, pulse, pulse};
                            } else {
                                hookMarker_->SetColor({1.0f, 1.0f, 1.0f, 1.0f}); // 白
                                hookMarker_->GetTransform()->scale = {1.0f, 1.0f, 1.0f};
                            }

                            hookMarker_->GetTransform()->rotate = {0.0f, 0.0f, 0.0f};
                        } else {
                            hookMarker_->SetIsVisible(false);
                            aimTargetObject_ = nullptr;
                        }

                        hookMarker_->Update(deltaTime);
                        hookMarker_->UpdateLocalMatrix();
                        hookMarker_->UpdateWorldMatrix();
                    }
                }
            } else {
                // 右クリック解除時にフックアクションへ移行
                if (hookMarker_) {
                    if (hookMarker_->GetIsVisible()) {
                        Vector3 targetPos = hookMarker_->GetTransform()->translate;

                        // フック移動は地面全体では停止中。HookAnchor だけ旧フック移動を使う。
                        if (aimTargetObject_ && aimTargetObject_->GetGimmickType() == "HookAnchor") {
                            ChangeState(std::make_unique<PlayerStateHook>(targetPos));
                        } else if (aimTargetObject_ && aimTargetObject_->GetGimmickType() == "HookPullBlock") {
                            ChangeState(std::make_unique<PlayerStatePullObject>(aimTargetObject_, targetPos));
                        } else if (aimTargetObject_ && (aimTargetObject_->GetCollisionAttribute() & CollisionCategories::Get("enemy"))) {
                            ChangeState(std::make_unique<PlayerStatePullEnemy>(aimTargetObject_, targetPos));
                        } else {
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

        // 移動を決めた後に状態を進め、攻撃・被弾による制御変更を反映します。
        if (state_) {
            state_->Update(this, deltaTime);
        }

        if (tutorialSafetyEnabled_ && param_.has_value()) {
            param_->maxHp = (std::max)(param_->maxHp, 1.0f);
            param_->hp = param_->maxHp;
        }

        // 液面への接触は無敵や残HPに関係なく死亡します。安全チュートリアルは従来どおり保護します。
        float liquidY = 0.0f;
        if (!isDead && !tutorialSafetyEnabled_ && !dynamic_cast<PlayerStateFallingOut*>(state_.get()) &&
            FindLethalLiquidSurface(*this, liquidY) && param_.has_value()) {
            const float footY = GetModelWorldAABB().min.y;
            Vector3 position = GetTranslate();
            position.y += liquidY - footY + 0.04f;
            SetTranslate(position);
            UpdateLocalMatrix();
            UpdateWorldMatrix();
            SetVelocity({0.0f, 0.0f, 0.0f});
            param_->hp = 0.0f;
        }

        // 死亡へ入る前に攻撃とコピーを解除し、イベントを一度だけ通知します。
        if (GetHp() <= 0.0f && !isDead) {
            if (baseCombatController_) {
                baseCombatController_->Cancel(*this, false);
            }
            CancelEnemyMorph();
            isDead = true;
            deathTimer_ = 0.0f;
            ChangeState(std::make_unique<PlayerStateDead>());

            PlayerDeathEvent deathEvent;
            deathEvent.player = this;
            EventManager::GetInstance()->Dispatch(deathEvent);
        }

        // チュートリアル中の落下だけは残機を減らさず、その場で復帰させます。
        if (transform_.translate.y < -18.0f && !isDead && isControlActive_) {
            if (baseCombatController_) {
                baseCombatController_->Cancel(*this, false);
            }
            if (tutorialSafetyEnabled_) {
                SetTranslate(respawnPosition_);
                SetVelocity({0.0f, 0.0f, 0.0f});
            } else {
                ChangeState(std::make_unique<PlayerStateFallingOut>());
                GameDataManager::GetInstance()->SubtractLife();
            }
        }

        // ピンチ表示は毎フレーム明示的に設定し、回復後の赤枠を残しません。
        if (!isDead && GetHp() <= 1.0f && GetHp() > 0.0f) {
            PostEffect::GetInstance()->GetParams()->dangerVignette = 0.8f;
        } else {
            PostEffect::GetInstance()->GetParams()->dangerVignette = 0.0f;
        }
    }

    BaseEnemy* carriedEnemyBase = dynamic_cast<BaseEnemy*>(carriedEnemy_);
    BaseEnemy* activeMorphSource = isEnemyMorphed_ ? enemyMorphSource_ : nullptr;
    const bool isBatMorphActive = isEnemyMorphed_ && enemyMorphType_ == EnemyMorphType::Bat;
    const bool isCarryingBat = (carriedEnemyBase && carriedEnemyBase->GetEnemyType() == "Bat") || isBatMorphActive;

    // 照準中やコウモリ能力中の重力は、このフレームの物理計算だけに適用します。
    // Character::SetGravityはステータス本体を書き換えるため、一時補正には使用しません。
    const float configuredGravity = param_.has_value() ? param_->gravity : 50.0f;
    const float configuredMaxFallSpeed = param_.has_value() ? param_->maxFallSpeed : 60.0f;
    bool hasTemporaryPhysicsOverride = false;

    if (isControlActive_) {
        float gravity = hookAimHeld ? (std::min)(configuredGravity, 10.0f) : configuredGravity;
        float maxFallSpeed = configuredMaxFallSpeed;

        if (isCarryingBat && velocity_.y < 0.0f) {
            gravity = (std::min)(gravity, 12.0f);
            maxFallSpeed = 12.0f;
            if (velocity_.y < -maxFallSpeed) {
                velocity_.y = Math::Lerp(velocity_.y, -maxFallSpeed, 0.22f);
            }
        }

        if (param_.has_value()) {
            param_->gravity = gravity;
            param_->maxFallSpeed = maxFallSpeed;
            hasTemporaryPhysicsOverride = true;
        }
    }

    // 状態と入力を確定してから物理を進め、能力入力に必要な接地を再確認します。
    // 破裂中は物理移動を止め、液面に残して演出を見せます。
    if (isDead) {
        SetVelocity({0.0f, 0.0f, 0.0f});
        Object3d::Update(deltaTime);
    } else {
        Character::Update(deltaTime);
        // 互換モーターは接地をリセットし、シーン末尾の衝突解決まで未接地になります。
        // 能力判定より先に実際の足元接触を確認します。押し戻し・衝突イベントは
        // 従来のCollisionManagerに任せ、ここでは位置や速度を変更しません。
        if (!copyPreviewActive_ && deltaTime > 0.0f && param_.has_value() && !param_->motorContinuousCollision &&
            !IsGrounded() && GetVelocity().y <= 0.0f) {
            UpdateWorldMatrix(false);
            PhysicsQueryFilter filter;
            filter.mask = CollisionCategories::Get("ground") & GetCollisionMask();
            filter.ignoredObject = this;
            const float maxSlope = std::clamp(param_->motorMaxSlopeDegrees, 0.0f, 89.9f);
            const float minimumGroundNormalY = std::cos(maxSlope * kPi / 180.0f);
            for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapAABB(GetAABB(), filter)) {
                // 広域の箱判定だけで接地扱いにせず、自機本来のCollider形状で確認します。
                const CollisionInfo contact = CheckCollision(hit.object);
                if (contact.isColliding && contact.normal.y > minimumGroundNormalY) {
                    SetGrounded(true);
                    break;
                }
            }
        }
        // 隔離空間には物理床がないため、モーターの更新後・能力入力の前に床を解決します。
        // 更新前のSetGroundedだけではBeginFrameで消され、ガードが開始できません。
        if (copyPreviewActive_) {
            const bool grounded = transform_.translate.y <= copyPreviewGroundHeight_ + 0.001f && velocity_.y <= 0.0f;
            if (grounded) {
                transform_.translate.y = copyPreviewGroundHeight_;
                velocity_.y = 0.0f;
            }
            SetGrounded(grounded);
            UpdateWorldMatrix(true);
        }
    }

    if (hasTemporaryPhysicsOverride && param_.has_value()) {
        param_->gravity = configuredGravity;
        param_->maxFallSpeed = configuredMaxFallSpeed;
    }

    if (electricShockControlLocked_) {
        SetVelocity({0.0f, 0.0f, 0.0f});
        SetTranslate(electricShockLockedPosition_);
    }

    UpdateElectricShockFeedback(deltaTime);

    const bool hasVisibleInvincibility =
        isDamageInvincible_ || isDashInvincible_ || isEvasionInvincible_ || isGuardInvincible_;
    if (hasVisibleInvincibility && deltaTime > 0.0f) {
        invincibleBlinkTimer_ += deltaTime;
    }

    UpdateDamageInvincibleBlinkVisibility();

    if (electricShockFeedbackTimer_ <= 0.0f && hasVisibleInvincibility && deltaTime > 0.0f) {
        const float blink = 0.5f + 0.5f * std::sin(invincibleBlinkTimer_ * 42.0f);
        const Vector4 base = isEnemyMorphed_ ? GetEnemyMorphTint(enemyMorphType_) : Vector4{1.0f, 1.0f, 1.0f, 1.0f};
        const Vector4 flash = isGuardInvincible_
                                  ? Vector4{1.0f, 0.42f, 0.84f, 1.0f}
                                  : ((isDashInvincible_ || isEvasionInvincible_) ? Vector4{0.34f, 0.74f, 1.0f, 1.0f}
                                                                                 : Vector4{1.0f, 0.78f, 0.32f, 1.0f});
        const float flashRate = blink > 0.48f ? 0.72f : 0.12f;
        const Vector4 color = {base.x * (1.0f - flashRate) + flash.x * flashRate,
                               base.y * (1.0f - flashRate) + flash.y * flashRate,
                               base.z * (1.0f - flashRate) + flash.z * flashRate, 1.0f};
        SetColor(color);
        if (!isEnemyMorphed_) {
            for (Object3d* child : GetChildren()) {
                if (child) {
                    child->SetColor(color);
                }
            }
        }
    } else if (!hasVisibleInvincibility) {
        invincibleBlinkTimer_ = 0.0f;
    }

    if (isCarryingBat && velocity_.y < -0.25f && deltaTime > 0.0f) {
        carryGlideEffectTimer_ -= deltaTime;
        if (carryGlideEffectTimer_ <= 0.0f) {
            const Vector3 playerPos = GetWorldPosition();
            const float yaw = GetMoveYaw();
            Vector3 windDir = {-std::sin(yaw), 0.18f, -std::cos(yaw)};
            const float windLength = std::sqrt(windDir.x * windDir.x + windDir.y * windDir.y + windDir.z * windDir.z);
            if (windLength > 0.001f) {
                windDir = windDir / windLength;
            }

            if (particleSystem_) {
                particleSystem_->SpawnParticles(playerPos + Vector3{0.0f, 1.25f, 0.0f}, 10, 2.4f, &windDir, 34.0f,
                                                {0.55f, 0.85f, 1.0f, 0.65f}, {0.75f, 1.0f, 1.0f, 0.0f}, 0.22f, 0.5f,
                                                0.32f, 0.04f);
            }

            if (MeshEffectManager::GetInstance()) {
                MeshEffectManager::GetInstance()->SpawnEffectAt(
                    "Resources/json/effect/effect_carry_bat_glide_ring.json", playerPos + Vector3{0.0f, 1.15f, 0.0f},
                    {1.5707963f, yaw, 0.0f}, {1.0f, 1.0f, 1.0f});
            }
            if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
                gpuParticleManager->Emit("carry_bat_glide_wisp", playerPos + Vector3{0.0f, 1.1f, 0.0f});
            }
            carryGlideEffectTimer_ = 0.14f;
        }
    } else {
        carryGlideEffectTimer_ = 0.0f;
    }

    const bool carryAbilityTriggered =
        deltaTime > 0.0f &&
        (copyPreviewActive_ ? copyPreviewInput_.specialTriggered : inputManager_->IsActionTriggered("Ability"));
    const bool morphRightAbilityTriggered =
        deltaTime > 0.0f && (copyPreviewActive_ ? copyPreviewInput_.secondaryTriggered
                                                : inputManager_->IsActionTriggered("SecondaryAction"));
    const bool morphRightAbilityHeld =
        deltaTime > 0.0f &&
        (copyPreviewActive_ ? copyPreviewInput_.secondaryHeld : inputManager_->IsActionPressed("SecondaryAction"));
    const bool morphLeftAbilityTriggered =
        deltaTime > 0.0f &&
        (copyPreviewActive_ ? copyPreviewInput_.primaryTriggered : inputManager_->IsActionTriggered("PrimaryAttack"));
    const bool morphLeftAbilityHeld =
        deltaTime > 0.0f &&
        (copyPreviewActive_ ? copyPreviewInput_.primaryHeld : inputManager_->IsActionPressed("PrimaryAttack"));
    const bool morphReleaseTriggered =
        deltaTime > 0.0f && !copyPreviewActive_ && inputManager_->IsActionTriggered("MorphRelease");

    // 通常形態は左クリック／攻撃アクションで必ず攻撃できます。
    // 運搬中は投げ操作、コピー中はコピー能力を優先します。
    const bool baseAttackTriggered =
        deltaTime > 0.0f && !hookAimHeld &&
        ((!copyPreviewActive_ && inputManager_->IsActionTriggered("Attack")) || morphLeftAbilityTriggered);
    if (baseCombatController_) {
        baseCombatController_->ProcessInput(*this, baseAttackTriggered);
        baseCombatController_->Update(*this, deltaTime);
    }

    if (!absorbEffectActive_ && isEnemyMorphed_ && !enemyMorphReleaseActive_ && isControlActive_ && !isDead &&
        morphReleaseTriggered) {
        BeginEnemyMorphRelease(false);
    }

    if (tutorialCarryThrowEnabled_ && !enemyMorphReleaseActive_ && !absorbEffectActive_ && carriedEnemy_ &&
        !isEnemyMorphed_ && inputManager_->IsActionPressed("PrimaryAttack")) {
        BaseEnemy* enemyBase = dynamic_cast<BaseEnemy*>(carriedEnemy_);
        if (enemyBase) {
            CancelEnemyMorph();

            // カメラの水平方向を投擲方向に使い、見下ろし角に応じて上向きの初速を調整します。
            Camera* camera = CameraManager::GetInstance()->GetMainCamera();
            Vector3 throwForward = GetForwardDirection();
            float throwUpSpeed = 17.0f;
            if (camera) {
                Vector3 camRot = camera->GetRotation();
                throwForward = {std::sin(camRot.y), 0.0f, std::cos(camRot.y)};
                throwUpSpeed = (std::clamp)(17.0f - std::sin(camRot.x) * 7.0f, 10.0f, 21.0f);
            }
            float forwardLength = std::sqrt(throwForward.x * throwForward.x + throwForward.z * throwForward.z);
            if (forwardLength > 0.001f) {
                throwForward.x /= forwardLength;
                throwForward.z /= forwardLength;
            } else {
                throwForward = {0.0f, 0.0f, 1.0f};
            }

            // 解除直後の当たり判定がプレイヤーに重ならないよう、前方に離してから投げます。
            Vector3 playerPos = GetWorldPosition();
            enemyBase->GetTransform()->translate = {playerPos.x + throwForward.x * 2.2f, playerPos.y + 2.2f,
                                                    playerPos.z + throwForward.z * 2.2f};

            // 運搬状態を解除してから、元の大きさと投擲初速を復元します。
            enemyBase->SetCarried(false);

            // 投げた時は捕まえた時点の大きさへ戻す
            enemyBase->GetTransform()->scale =
                hasCarriedEnemyBaseScale_ ? carriedEnemyBaseScale_ : enemyBase->GetScale();
            enemyBase->GetTransform()->rotate = {0.0f, 0.0f, 0.0f};
            enemyBase->GetTransform()->isQuaternionMaster = true;

            Vector3 throwVelocity = {throwForward.x * 32.0f, throwUpSpeed, throwForward.z * 32.0f};
            enemyBase->BeginThrown(throwVelocity);

            TriggerSlimeImpulse({1.4f, 3.6f, 1.4f}, 0.18f);
            // 投擲方向へ衝撃粒子を飛ばし、操作の結果をすぐに示します。
            if (particleSystem_) {
                Vector3 effectDir = {throwForward.x, 0.35f, throwForward.z};
                float effectDirLength =
                    std::sqrt(effectDir.x * effectDir.x + effectDir.y * effectDir.y + effectDir.z * effectDir.z);
                if (effectDirLength > 0.001f) {
                    effectDir = effectDir / effectDirLength;
                }
                particleSystem_->SpawnParticles(playerPos + Vector3{0.0f, 2.5f, 0.0f}, 24, 1.6f, &effectDir, 32.0f,
                                                {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}, 0.1f, 0.3f, 0.8f,
                                                0.1f);
            }
        }

        // プレイヤーの所有参照を最後に外し、敵の投擲更新へ引き継ぎます。
        SetCarriedEnemy(nullptr);
        DebugConsole::GetInstance()->AddLog("Throw Enemy!");
    } else if (!enemyMorphReleaseActive_ && !absorbEffectActive_ && copyAbilityController_ &&
               copyAbilityController_->HandlesMorphType(static_cast<int>(enemyMorphType_))) {
        PlayerCopyAbilityInput abilityInput;
        abilityInput.specialTriggered = carryAbilityTriggered;
        abilityInput.primaryTriggered = morphLeftAbilityTriggered;
        abilityInput.primaryHeld = morphLeftAbilityHeld;
        abilityInput.secondaryTriggered = morphRightAbilityTriggered;
        abilityInput.secondaryHeld = morphRightAbilityHeld;
        copyAbilityController_->ProcessInput(*this, abilityInput);
    } else if (!enemyMorphReleaseActive_ && !absorbEffectActive_ && activeMorphSource && carryAbilityTriggered) {
        // 非スライム系の旧コピー能力は、専用セッションへ移行するまで従来経路を維持します。
        activeMorphSource->ExecuteAbility(this);
    } else if (tutorialCarryAbsorbEnabled_ && !enemyMorphReleaseActive_ && !absorbEffectActive_ && carriedEnemy_ &&
               carriedEnemyBase && carryAbilityTriggered) {
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

        // 運搬中の敵だけ小さく揺らし、拘束されていることを見た目で示します。
        float offsetX = std::sin(struggleTimer_ * 35.0f) * 0.15f;
        float offsetZ = std::cos(struggleTimer_ * 30.0f) * 0.15f;
        float offsetY = std::sin(struggleTimer_ * 45.0f) * 0.08f;

        carriedEnemy_->GetTransform()->translate = {playerPos.x + offsetX, playerPos.y + 1.55f + offsetY,
                                                    playerPos.z + offsetZ};

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
        const Vector3 targetScale = {carriedEnemyBaseScale_.x * (1.0f - stretch),
                                     carriedEnemyBaseScale_.y * (1.0f + stretch),
                                     carriedEnemyBaseScale_.z * (1.0f - stretch)};
        carriedEnemy_->GetTransform()->scale =
            Math::Lerp(carriedEnemy_->GetScale(), targetScale, std::clamp(deltaTime * 10.0f, 0.0f, 1.0f));

        // 親の更新を待たずに行列を確定し、手元への追従を1フレーム遅らせません。
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
    if (copyAbilityController_ && copyAbilityController_->HandlesMorphType(static_cast<int>(enemyMorphType_)) &&
        !enemyMorphReleaseActive_) {
        copyAbilityController_->Update(*this, deltaTime);
    } else if (activeMorphSource && activeMorphSource != carriedEnemy_ && !enemyMorphReleaseActive_) {
        // 非スライム系だけは旧能力経路を維持します。
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

    if (!dynamic_cast<PlayerStateDamage*>(state_.get()) && !absorbEffectActive_ && !enemyMorphReleaseActive_ &&
        electricShockFeedbackTimer_ <= 0.0f && !isDead) {
        slimeAnimator_.Update(this, deltaTime);
    }

    UpdateEnemyMorph(deltaTime);
}

void Player::DrawUI() {}

float Player::GetVisualYawOffset() const {
    if (isEnemyMorphed_) {
        return GetEnemyMorphModelYawOffset();
    }
    return kPlayerModelYawOffset;
}

float Player::GetEnemyMorphModelYawOffset() const {
    switch (enemyMorphType_) {
    case EnemyMorphType::Slime:
    case EnemyMorphType::FireSlime:
    case EnemyMorphType::ThunderSlime:
    case EnemyMorphType::WindSlime:
    case EnemyMorphType::Bomber:
    case EnemyMorphType::BeamDrone:
    case EnemyMorphType::Crown:
        return kPi;
    default:
        return 0.0f;
    }
}

float Player::GetMoveYaw() const { return NormalizeYaw(transform_.rotate.y - GetVisualYawOffset()); }

Vector3 Player::GetForwardDirection() const {
    const float yaw = GetMoveYaw();
    return {std::sin(yaw), 0.0f, std::cos(yaw)};
}

void Player::SetMoveYaw(float yaw) { SetRotationY(NormalizeYaw(yaw + GetVisualYawOffset())); }

// 外部APIを維持し、スター移動固有の状態と軌道計算を委譲します。
void Player::StartLaunchStar(const Vector3& destination, float arcHeight, float duration) {
    launchStarController_.Start(*this, slimeAnimator_, destination, arcHeight, duration);
}

void Player::CancelLaunchStar(bool restoreControl) { launchStarController_.Cancel(*this, restoreControl); }

void Player::UpdateLaunchStar(float deltaTime) {
    const bool updateAnimation =
        !absorbEffectActive_ && !enemyMorphReleaseActive_ && electricShockFeedbackTimer_ <= 0.0f && !isDead;
    launchStarController_.Update(*this, slimeAnimator_, deltaTime, updateAnimation);
}

void Player::StepCopyPreviewObjects(float deltaTime, float groundY) {
    if (!IsEditorInternal())
        return;
    for (auto& object : copyPreviewObjects_) {
        object->Update(deltaTime);
        if (auto* enemy = dynamic_cast<BaseEnemy*>(object.get())) {
            Vector3 position = enemy->GetTranslate();
            Vector3 velocity = enemy->GetVelocity();
            if (position.y < groundY + 0.48f && velocity.y <= 0) {
                position.y = groundY + 0.48f;
                velocity.y = 0;
                velocity.x *= 0.96f;
                velocity.z *= 0.96f;
                enemy->SetTranslate(position);
                enemy->SetVelocity(velocity);
                enemy->SetGrounded(true);
                enemy->UpdateWorldMatrix(true);
            }
        }
    }
}

void Player::UpdateCopyAdornment() {
    const char* model = nullptr;
    if (isEnemyMorphed_ && !enemyMorphReleaseActive_) {
        switch (enemyMorphType_) {
        case EnemyMorphType::Slime:
            model = "Characters/copy_band_pink";
            break;
        case EnemyMorphType::FireSlime:
            model = "Characters/copy_band_fire";
            break;
        case EnemyMorphType::ThunderSlime:
            model = "Characters/copy_band_thunder";
            break;
        case EnemyMorphType::WindSlime:
            model = "Characters/copy_band_wind";
            break;
        case EnemyMorphType::Bomber:
            model = "Characters/copy_band_bomb";
            break;
        default:
            break; // 覇王は既存モデルに王冠を含みます。
        }
    }
    if (!model || !GetIsVisible()) {
        if (copyAdornment_)
            copyAdornment_->SetIsVisible(false);
        return;
    }
    if (!copyAdornment_) {
        copyAdornment_ = std::make_unique<Object3d>();
        copyAdornment_->Initialize(common_);
        copyAdornment_->SetColliderType(ColliderType::kNone);
        copyAdornment_->SetCollisionAttribute(0);
        copyAdornment_->SetCollisionMask(0);
        copyAdornment_->SetMaterialType(0);
        copyAdornment_->SetEnableLighting(true);
    }
    if (copyAdornment_->GetModelName() != model)
        copyAdornment_->SetModel(model);
    copyAdornment_->SetIsVisible(true);
    // 本体の潰れ・伸び・回転を含めて追従。能力解除時は描かず、元敵には付けません。
    *copyAdornment_->GetTransform() = transform_;
    if (auto* decorationRenderer = copyAdornment_->GetMeshRenderer()) {
        if (auto* bodyRenderer = GetMeshRenderer()) {
            decorationRenderer->SetVisualTransform(bodyRenderer->GetVisualScale(), bodyRenderer->GetVisualRotation(),
                                                   bodyRenderer->GetVisualOffset());
            decorationRenderer->SetVisualShear(bodyRenderer->GetVisualShear(), bodyRenderer->GetVisualShearPivotY());
        }
    }
    copyAdornment_->UpdateWorldMatrix(true);
}

void Player::DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource,
                           int previewBufferIndex) {
    Character::DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    if (!GetIsVisible())
        return;
    UpdateCopyAdornment();
    if (copyAdornment_ && copyAdornment_->GetIsVisible())
        copyAdornment_->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    if (copyAbilityController_)
        copyAbilityController_->Draw(*this, pointLightResource, spotLightResource, camera, previewBufferIndex);
    if (enemyMorphReleaseVisual_ && enemyMorphReleaseVisual_->GetIsVisible())
        enemyMorphReleaseVisual_->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    for (auto& object : copyPreviewObjects_)
        object->DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
}

void Player::DrawSpecialMaterialForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle,
                                          int previewBufferIndex) {
    Character::DrawSpecialMaterialForCamera(camera, depthSrvHandle, grabSrvHandle, previewBufferIndex);
    if (!GetIsVisible())
        return;
    auto* lights = LightManager::GetInstance();
    if (enemyMorphReleaseVisual_ && enemyMorphReleaseVisual_->GetIsVisible())
        enemyMorphReleaseVisual_->DrawForCamera(camera, lights->GetPointLightResource(), lights->GetSpotLightResource(),
                                                previewBufferIndex);
    for (auto& object : copyPreviewObjects_)
        object->DrawForCamera(camera, lights->GetPointLightResource(), lights->GetSpotLightResource(),
                              previewBufferIndex);
}

bool Player::HasOwnedSpecialMaterialVisuals() const {
    return copyAbilityController_ && copyAbilityController_->HasOwnedVisuals();
}

void Player::DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (copyAbilityController_ && GetIsVisible()) {
        copyAbilityController_->DrawOwnedVisuals(*this, depthSrvHandle, grabSrvHandle);
    }
}

void Player::DrawOwnedSpecialMaterialVisualsForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle,
                                                      int previewBufferIndex) {
    if (copyAbilityController_ && GetIsVisible()) {
        copyAbilityController_->DrawOwnedVisuals(*this, depthSrvHandle, grabSrvHandle, camera, previewBufferIndex);
    }
}

void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    Character::Draw(pointLightResource, spotLightResource);
    UpdateCopyAdornment();
    if (copyAdornment_ && copyAdornment_->GetIsVisible())
        copyAdornment_->Draw(pointLightResource, spotLightResource);
    for (auto& object : copyPreviewObjects_)
        object->Draw(pointLightResource, spotLightResource);
    if (copyAbilityController_ && GetIsVisible())
        copyAbilityController_->Draw(*this, pointLightResource, spotLightResource);
    if (enemyMorphReleaseVisual_ && enemyMorphReleaseVisual_->GetIsVisible()) {
        enemyMorphReleaseVisual_->Draw(pointLightResource, spotLightResource);
    }
    if (electricShockAuraEffect_ && electricShockAuraEffect_->GetIsVisible()) {
        electricShockAuraEffect_->Draw(pointLightResource, spotLightResource);
    }
    // ※フックマーカーはプレイヤーがカメラ外（フラスタムカリング）になった時でも
    // 描画されるように、GamePlayScene 側で直接描画するように変更しました。
}

// =================================================================
// 衝突処理
// =================================================================

bool Player::OnCollision(Object3d* other) {
    if (isDead || isCinematicLocked_) {
        return false;
    }

    if (!other)
        return false;

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
    if (other->GetCollider()) {
        attribute = other->GetCollider()->GetAttribute();
    }

    // ① まずプレイヤー本体の当たり判定を計算
    CollisionInfo info = CheckCollision(other);

    // ② 子オブジェクト（パーツ）の当たり判定も全てチェックする
    for (Object3d* child : GetChildren()) {
        if (!child)
            continue;
        CollisionInfo childInfo = child->CheckCollision(other);
        if (childInfo.isColliding) {
            // パーツがぶつかっていた場合、本体よりめり込みが深ければ
            // そのパーツの押し出し情報（法線とめり込み量）を採用して親を動かす！
            if (!info.isColliding || childInfo.penetration > info.penetration) {
                info = childInfo;
            }
        }
    }

    // 本体もパーツも当たっていなければ終了
    if (!info.isColliding) {
        return false;
    }

    // actionMode=6の衝撃ゲートは、押し戻しを行う前に体当たり系攻撃で破壊します。
    // 先に物理解決すると、破壊フレームだけ壁へ大きく押し戻されるためです。
    if (auto* breakableBlock = dynamic_cast<GimmickBreakableBlock*>(other)) {
        if (breakableBlock->TryBreakByPlayerImpact(this)) {
            return true;
        }
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

    if (attribute & CollisionCategories::Get("ground")) {
        ApplyPhysicsCollision(info, attribute);
    }

    // =======================================================
    // 2. ダメージ処理 / 突進攻撃
    // =======================================================
    const BaseEnemy* bodyEnemy = (attribute & CollisionCategories::Get("enemy")) ? dynamic_cast<const BaseEnemy*>(other) : nullptr;
    const float bodyAttackDamage = bodyEnemy ? bodyEnemy->GetActiveBodyAttackDamage() : 0.0f;
    // 本体への接触と攻撃への命中は別。物理解決と下のギミック通知は常に維持します。
    if ((attribute & CollisionCategories::Get("enemy_attack")) || bodyAttackDamage > 0.0f) {
        // ピンクガードは単にダメージを無視するだけでなく、受け止めた瞬間を
        // コピー能力へ通知して反動モーションと次のバウンド強化へつなげます。
        if (isGuardInvincible_ && copyAbilityController_) {
            copyAbilityController_->NotifyGuardedHit(*this, other->GetWorldPosition());
        }
        // 通常のダメージ処理 (被弾)
        else if (damageCooldownTimer_ <= 0.0f && !IsInvincible()) {
            const Bullet* attackBullet = dynamic_cast<const Bullet*>(other);
            const bool isElectricHit = other->GetEnemyType() == "ThunderSlime" ||
                                       (attackBullet && attackBullet->GetDamageType() == DamageType::Electric);

            // ダメージイベントを発行 (GameRule で HP 減少が処理される)
            DamageEvent dmgEvent;
            dmgEvent.target = this;
            dmgEvent.attacker = other;
            dmgEvent.damageAmount =
                attackBullet ? attackBullet->GetDamage() : (bodyAttackDamage > 0.0f ? bodyAttackDamage : 1.0f);
            if (attackBullet) {
                dmgEvent.damageType = attackBullet->GetDamageType();
                dmgEvent.statusEffect = attackBullet->GetStatusEffect();
            }
            EventManager::GetInstance()->Dispatch(dmgEvent);

            if (isElectricHit) {
                damageCooldownTimer_ = 0.8f;
            } else {
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

void Player::SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy) {
    if (mover_) {
        mover_->SetStrategy(std::move(strategy));
    }
}

void Player::ApplyDashPanelBoost(float duration, float speedMultiplier, float turnMultiplier) {
    if (mover_) {
        mover_->ApplyDashPanelBoost(duration, speedMultiplier, turnMultiplier);
    }
}

void Player::ApplyIceSurface(float duration, float friction, float steering) {
    if (mover_) {
        mover_->ApplyIceSurface(duration, friction, steering);
    }
}

void Player::SetPlatformMotion(const Vector3& velocity) {
    if (mover_)
        mover_->SetPlatformMotion(velocity);
}

// =================================================================
// アニメーション・状態管理 (State Pattern)
// =================================================================

void Player::ChangeState(std::unique_ptr<IAnimationState> newState) {
    // 現在の状態を終了
    if (state_) {
        state_->Exit(this);
    }

    // 状態を切り替えて開始
    state_ = std::move(newState);

    if (state_) {
        state_->Enter(this);
    } else {
        DebugConsole::GetInstance()->AddLog("[ERROR] Failed to set new state.");
    }
}

void Player::PlayAnimation(const std::string& animName, bool loop) {
    // 既に同じアニメーションが再生中ならリセットしない (滑らかな遷移のため)
    if (animName_ != animName) {
        animName_ = animName;
        animationTime_ = 0.0f;
    }
    isAnimLoop_ = loop;
}

void Player::SetSlimeAnimationMode(PlayerSlimeAnimator::Mode mode) { slimeAnimator_.SetMode(mode); }

void Player::SetSlimeAnimationDirection(const Vector3& direction) { slimeAnimator_.SetMotionDirection(direction); }

void Player::SetSlimePullDirection(const Vector3& direction) { slimeAnimator_.SetPullDirection(direction); }

void Player::SetSlimePullProgress(float progress) { slimeAnimator_.SetPullProgress(progress); }

void Player::SetSlimeJumpCharge(float chargeRate) { slimeAnimator_.SetJumpCharge(chargeRate); }

void Player::SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose pose, float strength) {
    slimeAnimator_.SetAbilityPose(pose, strength);
}

void Player::TriggerSlimeImpulse(const Vector3& scale, float duration) {
    slimeAnimator_.TriggerImpulse(scale, duration);
}

bool Player::PlaySlimeAbilityMotion(const std::string& clipName, bool loop, float playbackSpeed, float blendInDuration,
                                    float blendOutDuration) {
    return slimeAnimator_.PlayAbilityMotion(clipName, loop, playbackSpeed, blendInDuration, blendOutDuration);
}

void Player::StopSlimeAbilityMotion(float blendOutDuration) { slimeAnimator_.StopAbilityMotion(blendOutDuration); }

void Player::ClearSlimeAbilityMotion() { slimeAnimator_.ClearAbilityMotion(this); }

void Player::ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode mode, const Vector3& direction) {
    forcedSlimeAnimationModeActive_ = true;
    forcedSlimeAnimationMode_ = mode;
    forcedSlimeAnimationDirection_ = direction;
}

void Player::StartGateReturnAnimation(const PlayerGateReturnAnimation::Route& route) {
    gateReturnAnimation_.Start(this, route);
}

bool Player::IsGateReturnAnimationActive() const { return gateReturnAnimation_.IsActive(); }

bool Player::IsGateReturnAnimationFinished() const { return gateReturnAnimation_.IsFinished(); }

void Player::StopGateReturnAnimation(bool restoreControl) { gateReturnAnimation_.Stop(this, restoreControl); }

void Player::StepCinematicPullVisual(float deltaTime, const Vector3& direction, float progress) {
    if (!isCinematicLocked_) return;
    slimeAnimator_.SetMode(PlayerSlimeAnimator::Mode::PullEnemy);
    slimeAnimator_.SetPullDirection(direction);
    slimeAnimator_.SetPullProgress(progress);
    slimeAnimator_.Update(this, deltaTime);
}

void Player::EndCinematicPullVisual() {
    slimeAnimator_.Reset(managedBaseScale_, this);
    slimeAnimator_.SetMode(PlayerSlimeAnimator::Mode::Disabled);
    if (hookMarker_) hookMarker_->SetIsVisible(false);
}

void Player::BeginCinematicLock(bool preserveCopyAbility) {
    if (isCinematicLocked_ || isDead) {
        return;
    }

    // 同じ衝突フレームで後続の被弾イベントが来ても、先に演出専用状態へ切り替えます。
    isCinematicLocked_ = true;
    cinematicSavedControlActive_ = isControlActive_;
    cinematicSavedCollisionAttribute_ = GetCollisionAttribute();
    cinematicSavedCollisionMask_ = GetCollisionMask();

    if (baseCombatController_) {
        baseCombatController_->Cancel(*this, false);
    }
    // ボス導入ではコピー能力を維持し、入力と戦闘状態だけを止めます。
    if (!preserveCopyAbility) {
        CancelEnemyMorph();
    }
    ReleaseCarriedEnemy(true);
    if (electricShockFeedbackTimer_ > 0.0f || electricShockControlLocked_) {
        electricShockPendingInvincibleDuration_ = 0.0f;
        EndElectricShockFeedback();
    }

    RestoreDamageBlinkVisibility();
    damageCooldownTimer_ = 0.0f;
    evasionInvincibleTimer_ = 0.0f;
    isEvasionInvincible_ = false;
    invincibleBlinkTimer_ = 0.0f;
    SetDamageInvincible(false);
    SetDashInvincible(false);
    pendingAttack2_ = false;
    attackInputBuffer_.ClearPendingInput();

    // Damage Stateなどの終了処理で基準姿勢へ戻してから、演出側が姿勢を取得します。
    ChangeState(std::make_unique<PlayerStateIdle>());
    slimeAnimator_.Reset(managedBaseScale_, this);
    slimeAnimator_.SetMode(PlayerSlimeAnimator::Mode::Disabled);

    Vector3 stableRotation = GetRotation();
    stableRotation.x = baseRotation_.x;
    stableRotation.z = baseRotation_.z;
    if (!preserveCopyAbility || !isEnemyMorphed_) {
        SetScale(managedBaseScale_);
    }
    SetRotation(stableRotation);
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetIsControlActive(false);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(true);
    UpdateLocalMatrix();
    UpdateWorldMatrix();
}

void Player::EndCinematicLock(bool restoreControl) {
    if (!isCinematicLocked_) {
        return;
    }

    isCinematicLocked_ = false;
    SetCollisionAttribute(cinematicSavedCollisionAttribute_);
    SetCollisionMask(cinematicSavedCollisionMask_);
    SetVelocity({0.0f, 0.0f, 0.0f});
    slimeAnimator_.Reset(managedBaseScale_, this);
    ChangeState(std::make_unique<PlayerStateIdle>());
    SetIsControlActive(restoreControl && cinematicSavedControlActive_ && !isDead);
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

void Player::SetGuardInvincible(bool inv) {
    isGuardInvincible_ = inv;
    UpdateColor();
}

void Player::SetTutorialSafetyEnabled(bool enabled) {
    tutorialSafetyEnabled_ = enabled;
    if (enabled && param_.has_value()) {
        param_->maxHp = (std::max)(param_->maxHp, 1.0f);
        param_->hp = param_->maxHp;
        isDead = false;
        deathTimer_ = 0.0f;
        SetDamageInvincible(false);
    }
}

void Player::SetTutorialCarryActionPermissions(bool allowThrow, bool allowAbsorb) {
    tutorialCarryThrowEnabled_ = allowThrow;
    tutorialCarryAbsorbEnabled_ = allowAbsorb;

    // 吸収演出の途中で投擲専用工程へ切り替わった場合は、拘束状態へ戻します。
    if (!allowAbsorb && absorbEffectActive_) {
        CancelAbsorbEffect(true);
    }
}

void Player::ResetTutorialCarryActionState() {
    // 誤操作済みの編集再開や工程ジャンプでも再試行できる状態へ戻します。
    CancelEnemyMorph();
    ReleaseCarriedEnemy(true);
}

void Player::StartEvasionInvincibility(float duration) {
    evasionInvincibleTimer_ = (std::max)(evasionInvincibleTimer_, (std::max)(0.0f, duration));
    isEvasionInvincible_ = evasionInvincibleTimer_ > 0.0f;
    UpdateColor();
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
                const bool originalVisible =
                    i < damageBlinkChildVisible_.size() ? damageBlinkChildVisible_[i] : child->GetIsVisible();
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
    Vector4 targetColor =
        isEnemyMorphed_ ? GetEnemyMorphTint(enemyMorphType_) : Vector4{1.0f, 1.0f, 1.0f, 1.0f}; // 基本は白(通常色)

    if (isGuardInvincible_) {
        // 通常のピンクと区別できる、身を固めた濃いローズ色。
        targetColor = {0.78f, 0.12f, 0.34f, 1.0f};
    } else if (isDashInvincible_ || isEvasionInvincible_) {
        targetColor = {0.18f, 0.82f, 1.0f, 1.0f}; // 回避中は雷を思わせる水色を設定
    }

    // 本体と子パーツの色を一括変更
    SetColor(targetColor);
    if (!isEnemyMorphed_) {
        for (Object3d* child : GetChildren()) {
            if (child)
                child->SetColor(targetColor);
        }
    }
}

// =======================================================
// コンボ時間窓 API 実装
// =======================================================
void Player::StartComboWindow(float duration) { attackInputBuffer_.StartComboWindow(duration); }

bool Player::IsComboWindowActive() const { return attackInputBuffer_.IsComboWindowActive(); }

// =======================================================
// 攻撃入力バッファ API 実装
// =======================================================
void Player::RecordAttackInput(float duration) { attackInputBuffer_.Record(duration); }

void Player::MarkAttackBufferUsedForStateStart() { attackInputBuffer_.MarkUsedForStateStart(); }

bool Player::ConsumeBufferedAttackInput() { return attackInputBuffer_.Consume(); }
