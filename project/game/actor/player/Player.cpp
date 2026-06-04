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
#include "GimmickHookPullBlock.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
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
    jumpCount_ = 0;

    // 移動コンポーネントの構築
    mover_ = std::make_unique<PlayerMover>();
    mover_->Initialize(this, inputManager, particleSystem);

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
        respawnPosition_ = transform_.translate;
        baseRotation_ = transform_.rotate;
        isFirstUpdate_ = false;
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

        // 2. 移動制御の更新
        if (isControlActive_ && mover_) {
            // 右クリック中は移動入力を受け付けず、水平移動を停止させる
            if (inputManager_->IsMouseButtonPressed(1)) {
                Vector3 v = GetVelocity();
                SetVelocity({ 0.0f, v.y, 0.0f });

                Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                if (camera) {
                    Vector3 camRot = camera->GetRotation();
                    SetRotation({ baseRotation_.x + camRot.x, camRot.y, baseRotation_.z });

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

                        // 当たった対象が「敵」なら引き寄せ、それ以外なら自分が飛ぶ
                        if (aimTargetObject_ && aimTargetObject_->GetGimmickType() == "HookAnchor") {
                            ChangeState(std::make_unique<PlayerStateSwingHook>(targetPos));
                        }
                        else if (aimTargetObject_ && aimTargetObject_->GetGimmickType() == "HookPullBlock") {
                            ChangeState(std::make_unique<PlayerStatePullObject>(aimTargetObject_, targetPos));
                        }
                        else if (aimTargetObject_ && (aimTargetObject_->GetCollisionAttribute() & kEnemy)) {
                            ChangeState(std::make_unique<PlayerStatePullEnemy>(aimTargetObject_, targetPos));
                        }
                        else {
                            ChangeState(std::make_unique<PlayerStateHook>(targetPos));
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
            state_->Update(this);
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
        if (transform_.translate.y < -40.0f && !isDead && isControlActive_) {
            ChangeState(std::make_unique<PlayerStateFallingOut>());
            GameDataManager::GetInstance()->SubtractLife();

            if (GameDataManager::GetInstance()->GetLives() <= 0) {
                SceneManager::GetInstance()->ChangeScene("GAMEOVER");
            }
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
    const bool isCarryingBat = carriedEnemyBase && carriedEnemyBase->GetEnemyType() == "Bat";

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

    if (isCarryingBat && velocity_.y < -0.25f && deltaTime > 0.0f) {
        carryGlideEffectTimer_ -= deltaTime;
        if (carryGlideEffectTimer_ <= 0.0f) {
            const Vector3 playerPos = GetWorldPosition();
            const float yaw = GetRotation().y;
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

    const bool carryAbilityHeld =
        inputManager_->IsKeyPressed(DIK_E) ||
        inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_Y) ||
        inputManager_->IsActionPressed("Ability");
    const bool carryAbilityTriggered =
        inputManager_->IsKeyTriggered(DIK_E) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_Y) ||
        inputManager_->IsActionTriggered("Ability");

    if (carriedEnemy_ && inputManager_->IsMouseButtonPressed(0)) {
        BaseEnemy* enemyBase = dynamic_cast<BaseEnemy*>(carriedEnemy_);
        if (enemyBase) {
            // ① カメラの向いている方向（投げる方向）を計算
            Camera* camera = CameraManager::GetInstance()->GetMainCamera();
            Vector3 throwForward = { std::sin(GetRotation().y), 0.0f, std::cos(GetRotation().y) };
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

            // 投げた時は元の大きさと状態に戻す
            enemyBase->GetTransform()->scale = { 1.0f, 1.0f, 1.0f };
            enemyBase->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
            enemyBase->GetTransform()->isQuaternionMaster = true;

            Vector3 throwVelocity = {
                throwForward.x * 32.0f,
                throwUpSpeed,
                throwForward.z * 32.0f
            };
            enemyBase->BeginThrown(throwVelocity);

            // 【案D：投げアクション演出】
            // ① プレイヤー本体を鋭く前方に伸ばす
            transform_.scale = { 0.7f, 1.8f, 0.7f };
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
        carriedEnemy_ = nullptr;
        DebugConsole::GetInstance()->AddLog("Throw Enemy!");
    }
    else if (carriedEnemy_ && carriedEnemyBase &&
        (carriedEnemyBase->GetEnemyType() == "Bomber" ? carryAbilityHeld : carryAbilityTriggered)) {
        BaseEnemy* enemyBase = dynamic_cast<BaseEnemy*>(carriedEnemy_);
        if (enemyBase) {
            // 乗せている敵の「固有能力」を呼び出す！
            enemyBase->ExecuteAbility(this);
        }
    }
    if (carriedEnemy_) {
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

        float baseScale = 0.6f;
        float stretch = std::sin(struggleTimer_ * 25.0f) * 0.05f;
        carriedEnemy_->GetTransform()->scale = { 
            baseScale - stretch, baseScale + stretch, baseScale - stretch 
        };

        // 行列を強制更新して、1フレームの遅れもなくピタッと追従させる
        carriedEnemy_->UpdateLocalMatrix();
        carriedEnemy_->UpdateWorldMatrix();

        if (BaseEnemy* enemyBase = dynamic_cast<BaseEnemy*>(carriedEnemy_)) {
            enemyBase->UpdateCarriedAbility(this, deltaTime);
        }
    }

    // --- スケールの自然な復元（Squash & Stretch のための自動リセット） ---
    // どの状態からでも、変形させられたスケールを 0.15 の速度で 1.0 に戻します
    if (!dynamic_cast<PlayerStateDamage*>(state_.get())) {
        Vector3 currentScale = transform_.scale;
        Vector3 targetScale = { 1.0f, 1.0f, 1.0f };
        transform_.scale.x = Math::Lerp(currentScale.x, targetScale.x, 0.15f);
        transform_.scale.y = Math::Lerp(currentScale.y, targetScale.y, 0.15f);
        transform_.scale.z = Math::Lerp(currentScale.z, targetScale.z, 0.15f);
    }
}


void Player::DrawUI()
{
}
void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource)
{
    Character::Draw(pointLightResource, spotLightResource);
    // ※フックマーカーはプレイヤーがカメラ外（フラスタムカリング）になった時でも
    // 描画されるように、GamePlayScene 側で直接描画するように変更しました。
}

// =================================================================
// 衝突処理
// =================================================================

bool Player::OnCollision(Object3d* other)
{
    if (!other) return false;

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
            // ダメージイベントを発行 (GameRule で HP 減少が処理される)
            DamageEvent dmgEvent;
            dmgEvent.target = this;
            dmgEvent.attacker = other;
            dmgEvent.damageAmount = 1.0f; // 1ダメージを与える
            EventManager::GetInstance()->Dispatch(dmgEvent);

            // 無敵時間をセット
            damageCooldownTimer_ = 1.0f;
            SetDamageInvincible(true);

            // ノックバック状態へ移行 (衝突法線を利用)
            ChangeState(std::make_unique<PlayerStateDamage>(info.normal));
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

void Player::UpdateColor() {
    Vector4 targetColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 基本は白(通常色)

    if (isDashInvincible_) {
        targetColor = { 0.0f, 0.0f, 1.0f, 1.0f }; // 回避中は青色を設定
    }

    // 本体と子パーツの色を一括変更
    SetColor(targetColor);
    for (Object3d* child : GetChildren()) {
        if (child) child->SetColor(targetColor);
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
