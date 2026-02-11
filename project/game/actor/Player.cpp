#include "Player.h"
#include "Model.h"
#include "CollisionConfig.h"
#include "engine/utility/math/Math.h"
#include "EventManager.h"
#include "CameraManager.h"
#include "IMoveStrategy.h"
#include "PlayerState.h" // ★重要: 具体的なStateクラスを知るために必要
#include <DebugConsole.h>

void Player::Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem) {
    // 親クラスの初期化 (Character -> Object3d)
    Character::Initialize(common); // ※CharacterにInitialize(Object3dCommon*)がある前提

    // 依存オブジェクトの保存
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;

    // 基本設定
    SetClassName("Player");
    SetColliderType(ColliderType::kOBB);
    SetCollisionSize({ 2.0f, 2.0f, 2.0f }); // 必要に応じて調整

    // 移動コンポーネント (Mover) の初期化
    mover_ = std::make_unique<PlayerMover>();
    mover_->Initialize(this, inputManager, particleSystem);

    // ★ステートマシン (Animation) の初期化
    // 最初は「待機 (Idle)」状態からスタート
    ChangeState(std::make_unique<PlayerStateIdle>());
}

void Player::Update(float deltaTime) {
    // 1. 移動コンポーネントの更新 (入力 -> 速度計算)
    if (isControlActive_ && mover_) {
        mover_->Update(deltaTime);
    }

    // 2. ステートマシン (ここが重要！)
    if (state_) {
        // 中身があるなら Update を呼ぶ
        state_->Update(this);
    } else {
   
        DebugConsole::GetInstance()->AddLog("[ERROR] state_ is NULL!");
    }

    // 3. 親クラスの更新 (物理挙動、行列計算など)
    Character::Update(deltaTime);
}

bool Player::OnCollision(Object3d* other) {
    // 1. 相手が有効かチェック
    if (!other) return false;

    // 2. 衝突属性を取得
    // ※ Object3d に GetAttribute() がない場合は collider_ から取得するなど調整してください
    // ここでは一般的な実装として記述します
    uint32_t attribute = 0;
    if (other->GetCollider()) {
        attribute = other->GetCollider()->GetAttribute();
    }

    // 3. 衝突判定 (CollisionInfoを取得)
    // Characterクラスの CheckCollision を使用
    CollisionInfo info = CheckCollision(other);

    // 当たっていなければ終了
    if (!info.isColliding) {
        return false;
    }

    // =================================================================
    //  4. イベント発行 (GameRuleへの通知など)
    // =================================================================
    
    PlayerHitEvent event;
    event.me = this;
    event.hitObject = other;
    event.normal = info.normal;
    EventManager::GetInstance()->Dispatch(event);
    

    // =================================================================
    // 5. 物理挙動 (押し戻し処理)
    // =================================================================

     if (attribute & kAllSolid) {
    ApplyPhysicsCollision(info, attribute);
     }

    return true; // 衝突処理完了
}

void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    Character::Draw(pointLightResource, spotLightResource);
}

void Player::SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy) {
    if (mover_) {
        mover_->SetStrategy(std::move(strategy));
    }
}

// --- State Machine ---

void Player::ChangeState(std::unique_ptr<IAnimationState> newState) {
    // 切り替えログを出す
    DebugConsole::GetInstance()->AddLog("★ ChangeState Called!");

    // 1. 古い状態から出る
    if (state_) {
        state_->Exit(this);
    }

    // 2. 新しい状態に入れ替える
    state_ = std::move(newState);

    // 3. 新しい状態に入る
    if (state_) {
        state_->Enter(this);
        DebugConsole::GetInstance()->AddLog(" -> State Enter Success.");
    } else {
        DebugConsole::GetInstance()->AddLog("[ERROR] Failed to set new state.");
    }
}

void Player::PlayAnimation(const std::string& animName, bool loop) {
    // 同じアニメーションならリセットしない（滑らかにするため）
    if (animName_ != animName) {
        animName_ = animName;
        animationTime_ = 0.0f; 
    }
    isAnimLoop_ = loop;
}