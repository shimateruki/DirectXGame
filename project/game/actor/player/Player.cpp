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
    spriteCommon_ = spriteCommon;

    // レティクルの初期化
    if (spriteCommon_) {
        // 確実に存在するロックオン用の画像を代用してテストする
        uint32_t reticleTex = TextureManager::GetInstance()->Load("Resources/sprite/lockOn.png");
        reticleSprite_ = std::make_unique<Sprite>();
        reticleSprite_->Initialize(spriteCommon_, reticleTex);
        reticleSprite_->SetAnchorPoint({ 0.5f, 0.5f });
        reticleSprite_->SetPosition({ (float)WinApp::kClientWidth / 2.0f, (float)WinApp::kClientHeight / 2.0f });
        reticleSprite_->SetSize({ 64.0f, 64.0f }); // 小さく調整
        reticleSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 不透明な白
        DebugConsole::GetInstance()->AddLog("Reticle Initialized.");
    }

    // 自機としての基本設定
    SetClassName("Player");
    jumpCount_ = 0;

    // 移動コンポーネントの構築
    mover_ = std::make_unique<PlayerMover>();
    mover_->Initialize(this, inputManager, particleSystem);

    // ステートマシン初期化 (待機状態からスタート)
    ChangeState(std::make_unique<PlayerStateIdle>());
}

void Player::Update(float deltaTime)
{
    // 初回更新時に初期位置を記録
    if (isFirstUpdate_) {
        respawnPosition_ = transform_.translate;
        isFirstUpdate_ = false;
    }

    // 時間が進んでいる（ポーズ中ではない）時だけ、操作や状態を更新
    if (deltaTime > 0.0f)
    {
        // 1. 無敵タイマーの管理 (ダメージ被弾時)
        if (damageCooldownTimer_ > 0.0f)
        {
            damageCooldownTimer_ -= deltaTime;
            if (damageCooldownTimer_ <= 0.0f)
            {
                damageCooldownTimer_ = 0.0f;
                // 被弾無敵フラグを解除 (赤色が消える)
                SetDamageInvincible(false);
            }
        }

        // 2. 移動制御の更新 (PlayerMover)
        if (isControlActive_ && mover_)
        {
            mover_->Update(deltaTime);
        }

        // 3. 状態(State)の更新
        if (state_)
        {
            state_->Update(this);
        }

        // 4. 死亡判定
        // ※画面の暗転演出などはプレイヤー内でやらず、HPの状態だけ見てステートを変える
        if (GetHp() <= 0.0f && !isDead)
        {
            isDead = true;
            deathTimer_ = 0.0f;
            ChangeState(std::make_unique<PlayerStateDead>());
            DebugConsole::GetInstance()->AddLog("Player DEAD! 死亡状態へ移行");

            // 死亡イベントを発行
            PlayerDeathEvent deathEvent;
            deathEvent.player = this;
            EventManager::GetInstance()->Dispatch(deathEvent);
        }

        // 5. 落下判定
        if (transform_.translate.y < -40.0f && !isDead && isControlActive_) {
            // 落下演出状態へ移行
            ChangeState(std::make_unique<PlayerStateFallingOut>());
            
            // 残機を減らす
            GameDataManager::GetInstance()->SubtractLife();

            if (GameDataManager::GetInstance()->GetLives() <= 0) {
                // 残機なし：ゲームオーバーシーンへ
                SceneManager::GetInstance()->ChangeScene("GAMEOVER");
            }
            else {
                DebugConsole::GetInstance()->AddLog("Fell out of bounds! Life subtracted. Starting Iris Out.");
            }
        }
    }

    // 6. 親クラスの更新 (重力計算・行列計算・衝突リストのリセットなど)
    Character::Update(deltaTime);

    // --- 7. 分身システムの更新 ---
    // クールタイムの更新
    if (cloneCooldownTimer_ > 0.0f) {
        cloneCooldownTimer_ -= deltaTime;
    }

    // 操作
    if (isControlActive_) {
        // 右クリック中
        if (inputManager_->IsMouseButtonPressed(1)) {
            // 分身がいなくてクールタイムも終わっていれば「狙い」状態
            if (!activeClone_ && cloneCooldownTimer_ <= 0.0f) {
                isAimingClone_ = true;
                if (reticleSprite_) reticleSprite_->Update();
            }
            // 分身がいれば「テレポート」（空中でも可能）
            else if (activeClone_) {
                // テレポート実行
                Vector3 targetPos = activeClone_->GetWorldPosition();
                transform_.translate = targetPos;
                
                // 勢いをリセット
                velocity_ = { 0, 0, 0 };
                
                // CollisionManagerから解除してから消す！
                CollisionManager::GetInstance()->RemoveObject(activeClone_.get());
                activeClone_ = nullptr;
                cloneCooldownTimer_ = 3.0f;
                
                // テレポート演出（パーティクル）
                if (particleSystem_) {
                    particleSystem_->SpawnParticles(targetPos, 20, 1.0f, nullptr, 1.0f,
                        { 0.5f, 0.8f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f },
                        0.1f, 0.5f, 1.0f, 0.1f);
                }
                
                DebugConsole::GetInstance()->AddLog("Teleported to Clone! Cooldown started.");
            }
        }
        
        // 右クリックを離したとき（狙い解除 ＋ 射出）
        if (isAimingClone_ && inputManager_->IsMouseButtonReleased(1)) {
            // カメラの視線方向を計算
            Camera* camera = CameraManager::GetInstance()->GetMainCamera();
            Vector3 eye = camera->GetEye();
            Vector3 target = camera->GetTargetPoint();
            Vector3 shootDir = Math::Normalize(target - eye);
            
            float shootSpeed = 65.0f; // 飛距離アップ (40.0 -> 65.0)
            
            // 生成位置を少し高くする（足元だと即座に接地判定されるため）
            Vector3 spawnPos = transform_.translate;
            spawnPos.y += 1.0f;
            
            activeClone_ = std::make_unique<PlayerClone>();
            activeClone_->Initialize(common_, spawnPos, shootDir * shootSpeed);
            
            // ★プレイヤー本体の当たり判定設定（サイズやオフセットなど）を分身にコピー
            activeClone_->SetColliderConfig(GetColliderConfig());
            
            // 生成時に一度だけ衝突判定に登録
            CollisionManager::GetInstance()->AddObject(activeClone_.get());
            
            isAimingClone_ = false;
            DebugConsole::GetInstance()->AddLog("Clone Launched!");
        }
    }

    // 最後に分身の更新と管理を行う（入力判定の後にしないと、接地フラグがリセットされてしまうため）
    if (activeClone_) {
        activeClone_->Update(deltaTime);

        if (activeClone_->IsExpired()) {
            CollisionManager::GetInstance()->RemoveObject(activeClone_.get());
            activeClone_ = nullptr;
            cloneCooldownTimer_ = 3.0f; // 自然消滅したらクールタイム開始
            DebugConsole::GetInstance()->AddLog("Clone Expired. Cooldown started.");
        }
    }
}

void Player::DrawUI()
{
    // 狙い中、または分身が存在する間はレティクルを表示
    if ((isAimingClone_ || activeClone_) && reticleSprite_) {
        reticleSprite_->Draw();
    }
}
void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource)
{
    Character::Draw(pointLightResource, spotLightResource);
    
    // 分身がいれば描画
    if (activeClone_) {
        activeClone_->Draw(pointLightResource, spotLightResource);
    }
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
    if (attribute & kAllSolid)
    {
        ApplyPhysicsCollision(info, attribute);
    }

    // =======================================================
    // 2. ダメージ処理 / 踏みつけ攻撃 / 突進攻撃
    // =======================================================
    if (attribute & (kEnemy | kEnemyAttack))
    {
        // ★ 突進（タックル）判定
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

        // ★ 踏みつけ判定: 敵の本体(kEnemy)かつ、上方向から衝突したか
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
            dmgEvent.damageAmount = 1.0f; // ★ 1ダメージ
            EventManager::GetInstance()->Dispatch(dmgEvent);

            // 無敵時間をセット
            damageCooldownTimer_ = 1.0f;
            SetDamageInvincible(true);

            // ★ ノックバック状態へ移行 (衝突法線を利用)
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

    if (isDamageInvincible_) {
        targetColor = { 1.0f, 0.0f, 0.0f, 1.0f }; // ★ ダメージ中は最優先で「赤」！
    }
    else if (isDashInvincible_) {
        targetColor = { 0.0f, 0.0f, 1.0f, 1.0f }; // ★ 回避中は「青」！
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