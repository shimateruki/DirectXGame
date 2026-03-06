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

void Player::Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem) {
    // 親クラス(Character)の初期化
    Character::Initialize(common);

    // 外部システムの依存注入
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;

    // 自機としての基本設定
    SetClassName("Player");
    SetColliderType(ColliderType::kOBB);
    SetCollisionSize({ 2.0f, 2.0f, 2.0f });

    // 移動コンポーネントの構築
    mover_ = std::make_unique<PlayerMover>();
    mover_->Initialize(this, inputManager, particleSystem);

    // ステートマシン初期化 (待機状態からスタート)
    ChangeState(std::make_unique<PlayerStateIdle>());
}

void Player::Update(float deltaTime) {
    // 1. 移動制御の更新 (Strategy)
    if (isControlActive_ && mover_) {
        mover_->Update(deltaTime);
    }

    // 2. アニメーション・状態の更新 (State)
    if (state_) {
        state_->Update(this);
    } else {
        DebugConsole::GetInstance()->AddLog("[ERROR] Player state_ is NULL!");
    }

    // 3. 親クラスの更新 (物理挙動・行列計算など)
    Character::Update(deltaTime);
}

void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    Character::Draw(pointLightResource, spotLightResource);
}

// =================================================================
// 衝突処理
// =================================================================

bool Player::OnCollision(Object3d* other) {
    if (!other) return false;

    // 衝突相手の属性を取得
    uint32_t attribute = 0;
    if (other->GetCollider()) {
        attribute = other->GetCollider()->GetAttribute();
    }

    // 当たり判定の計算
    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) {
        return false;
    }

    // イベントの発行 (ゲームルールやUIへの通知)
    PlayerHitEvent event;
    event.me = this;
    event.hitObject = other;
    event.normal = info.normal;
    EventManager::GetInstance()->Dispatch(event);

    // 物理挙動の適用 (ソリッドな壁や床からの押し戻し)
    if (attribute & kAllSolid) {
        ApplyPhysicsCollision(info, attribute);
    }

    return true; // 衝突処理完了
}

// =================================================================
// 移動制御 (Strategy Pattern)
// =================================================================

void Player::SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy) {
    if (mover_) {
        mover_->SetStrategy(std::move(strategy));
    }
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