#include "Player.h"
#include "Model.h"
#include "CollisionConfig.h"
#include "engine/utility/math/Math.h"
#include "EventManager.h"
#include "CameraManager.h"
#include "IMoveStrategy.h"
#include "PlayerState.h"
#include <DebugConsole.h>

// =================================================================
// 初期化・更新・描画
// =================================================================

void Player::Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem)
{
    // 親クラス(Character)の初期化
    Character::Initialize(common);

    // 外部システムの依存注入
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;

    // 自機としての基本設定
    SetClassName("Player");

    // 移動コンポーネントの構築
    mover_ = std::make_unique<PlayerMover>();
    mover_->Initialize(this, inputManager, particleSystem);

    // ステートマシン初期化 (待機状態からスタート)
    ChangeState(std::make_unique<PlayerStateIdle>());
}

void Player::Update(float deltaTime)
{
    // 時間が進んでいる（ポーズ中ではない）時だけ、操作やアニメーションを更新
    if (deltaTime > 0.0f)
    {
        // 1. 無敵タイマーの管理 (ダメージ被弾時)
        if (damageCooldownTimer_ > 0.0f)
        {
            damageCooldownTimer_ -= deltaTime;
            if (damageCooldownTimer_ <= 0.0f)
            {
                damageCooldownTimer_ = 0.0f;
                // ★重要: 被弾無敵フラグのみを解除 (赤色が消える)
                SetDamageInvincible(false);
            }
        }

        // 2. 移動制御の更新 (Strategy Pattern)
        if (isControlActive_ && mover_)
        {
            mover_->Update(deltaTime);
        }

        // 3. アニメーション・状態の更新 (State Pattern)
        if (state_)
        {
            state_->Update(this);
        }
        else
        {
            DebugConsole::GetInstance()->AddLog("[ERROR] Player state_ is NULL!");
        }
    }

    // 4. 親クラスの更新 (重力計算・行列計算・衝突リストのリセットなど)
    Character::Update(deltaTime);

    // =======================================================
    // 5. モデルアニメ適用後の最終上書き処理 (PostUpdate)
    // =======================================================
    if (state_ && deltaTime > 0.0f)
    {
        // Idleステートなら待機時の微細な動きを適用
        if (auto idle = dynamic_cast<PlayerStateIdle*>(state_.get()))
        {
            idle->ApplyPostUpdate(this, deltaTime);
        }
        // Runステートなら走りに合わせた腕や脚の制御を適用
        else if (auto run = dynamic_cast<PlayerStateRun*>(state_.get()))
        {
            run->ApplyPostUpdate(this, deltaTime);
        }
    }
}
void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource)
{
    Character::Draw(pointLightResource, spotLightResource);
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
    // 2. ダメージ処理 (敵の攻撃に当たった時)
    // =======================================================
    // 属性が「kEnemyAttack」の時のみダメージ判定を行います。
    if (attribute & kEnemyAttack)
    {
        // タイマーと「総合的な無敵状態」の両方をチェック
        if (damageCooldownTimer_ <= 0.0f && !IsInvincible())
        {
            // ダメージイベントを発行（GameRule.cpp で HP 減少等が処理されます）
            DamageEvent dmgEvent;
            dmgEvent.target = this;
            dmgEvent.attacker = other;
            dmgEvent.damageAmount = 20.0f;
            EventManager::GetInstance()->Dispatch(dmgEvent);

            // 無敵時間をセット
            damageCooldownTimer_ = 1.5f;

            // ★重要: ダメージ用の無敵フラグのみを立てる (赤色になる)
            SetDamageInvincible(true);
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