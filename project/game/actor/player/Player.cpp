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
    // 時間が進んでいる（ポーズ中ではない）時だけ、操作やアニメーションを更新する
    if (deltaTime > 0.0f)
    {
        // 1. 移動制御の更新 (Strategy)
        if (isControlActive_ && mover_)
        {
            mover_->Update(deltaTime);
        }

        // 2. アニメーション・状態の更新 (State)
        if (state_)
        {
            state_->Update(this);
        }
        else
        {
            DebugConsole::GetInstance()->AddLog("[ERROR] Player state_ is NULL!");
        }
    }

    // 3. 親クラスの更新 (物理挙動・行列計算など)
    Character::Update(deltaTime);

    // --- アプリ層での最終上書き（モデルアニメ適用後に頭を上書き） ---
    if (state_)
    {
        // Idle ステートなら頭上書き処理を呼ぶ（他ステートも同様に拡張可）
        if (auto idle = dynamic_cast<PlayerStateIdle*>(state_.get()))
        {
            idle->ApplyPostUpdate(this, deltaTime);
        }
        // Run ステートなら走りの後処理（腕・足の振り等）を呼ぶ
        if (auto run = dynamic_cast<PlayerStateRun*>(state_.get()))
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

    // ② ★追加: 子オブジェクト（パーツ）の当たり判定も全てチェックする
    for (Object3d* child : GetChildren())
    {
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

    // 無敵時: ダメージ通知は行わないが物理押し戻しは適用（壁との衝突は処理）
    if (isInvincible_)
    {
        if (attribute & kAllSolid)
        { // ※ kMapBlock などソリッド属性のマクロに合わせてください
            ApplyPhysicsCollision(info, attribute);
        }
        return true;
    }

    // イベントの発行 (ゲームルールやUIへの通知)
    PlayerHitEvent event;
    event.me = this;
    event.hitObject = other;
    event.normal = info.normal;
    EventManager::GetInstance()->Dispatch(event);

    // 物理挙動の適用 (ソリッドな壁や床からの押し戻し)
    if (attribute & kAllSolid)
    {
        ApplyPhysicsCollision(info, attribute); // ここで親の座標が押し上げられ、落下速度もリセットされる
    }

    return true; // 衝突処理完了
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
void Player::SetInvincible(bool inv)
{
    if (inv == isInvincible_) return;

    if (inv)
    {
        // 無敵開始: 現在の色を保存し、青にする（本体）
        savedColor_ = GetColor();
        SetColor({ 0.0f, 0.0f, 1.0f, 1.0f });

        // 子パーツの色も保存して青にする
        childSavedColors_.clear();
        for (Object3d* child : GetChildren())
        {
            if (!child) continue;
            childSavedColors_[child] = child->GetColor();
            child->SetColor({ 0.0f, 0.0f, 1.0f, 1.0f });
        }

        isInvincible_ = true;
    }
    else
    {
        // 無敵終了: 本体色を復元
        SetColor(savedColor_);

        // 子パーツの色を復元
        for (auto& kv : childSavedColors_)
        {
            Object3d* child = kv.first;
            if (!child) continue;
            child->SetColor(kv.second);
        }
        childSavedColors_.clear();

        isInvincible_ = false;
    }
}