#include "Player.h"
#include "Model.h"
#include "CollisionConfig.h"
#include "engine/utility/math/Math.h"
#include <string> 
#include "EventManager.h"
#include "CameraManager.h"
void Player::Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem) {
    Object3d::Initialize(common);
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;
    SetClassName("Player");
    SetColliderType(ColliderType::kOBB);
    SetCollisionSize({ 2.0f, 2.0f, 2.0f });
}

void Player::Update(float deltaTime) {
    static Math math;

    // =================================================================
    // 1. 操作権限がある場合の処理 (入力・移動・回転)
    // =================================================================
    if (isControlActive_) {

        // --- A. 移動戦略（ストラテジー）による移動ベクトルの計算 ---
        if (moveStrategy_) {
            // 戦略クラスに移動ベクトルを計算してもらう
            Vector3 move = moveStrategy_->CalculateVelocity(this);

            // 計算結果を速度に反映 (X と Z のみ)
            velocity_.x = move.x;
            velocity_.z = move.z;
        }

        // --- B. 回転処理 ---
        if (!isLockingOn_) {
            // (ロックオン中でない場合のみ、移動方向を向く)
            Camera* camera = CameraManager::GetInstance()->GetMainCamera();
            Camera::FollowMode cameraMode = camera ? camera->GetFollowMode() : Camera::FollowMode::kFixed;

            if (cameraMode == Camera::FollowMode::kFirstPerson) {
                transform_.rotate.y = camera->GetRotation().y;
            }
            // XZ平面での速度ベクトルの長さで判断
            else if (math.Length(Vector3{ velocity_.x, 0.0f, velocity_.z }) > 0.001f) {
                transform_.rotate.y = std::atan2(velocity_.x, velocity_.z);
            }
        }

        // --- C. ジャンプ処理 ---
        if (isGrounded_ && inputManager_->IsKeyTriggered(DIK_SPACE)) {
            const float kJumpVelocity = 30.0f;
            velocity_.y = kJumpVelocity;

            // ジャンプパーティクル
            if (particleSystem_) {
                Vector3 pos = GetWorldPosition(); // 足元の位置
                pos.y -= 1.0f; // ブロックの半分下

                particleSystem_->SpawnParticles(
                    pos,
                    15,
                    1.5f,
                    nullptr,
                    180.0f,
                    { 1.0f, 0.8f, 0.2f, 1.0f },
                    { 0.3f, 0.1f, 0.0f, 0.0f },
                    0.2f, 0.4f,
                    2.0f,
                    1.0f
                );
            }
        }
    }
    // =================================================================
    // 2. 操作権限がない場合の処理 (停止)
    // =================================================================
    else {
        // 操作していないなら、勝手に滑らないように横移動を止める
        velocity_.x = 0.0f;
        velocity_.z = 0.0f;

    }

    // =================================================================
    // 3. 共通処理 (エフェクト・物理演算)
    // =================================================================

    // --- ダッシュエフェクト判定 ---
    Vector3 horizontalVelocity = { velocity_.x, 0.0f, velocity_.z };
    float speed = math.Length(horizontalVelocity);

    const float kDashSpeedThreshold = 5.0f;
    if (speed > kDashSpeedThreshold && isGrounded_ && particleSystem_) {

        Vector3 direction = math.Normalize(horizontalVelocity) * -1.0f;
        Vector3 pos = GetWorldPosition();
        pos.y -= 1.0f;

        particleSystem_->SpawnParticles(
            pos,
            3,
            1.0f,
            &direction,
            30.0f,
            { 0.8f, 0.8f, 0.8f, 1.0f },
            { 0.5f, 0.5f, 0.5f, 0.0f },
            0.2f, 0.3f,
            2.0f,
            1.0f
        );
    }

    // --- 4. 親(Character)のUpdateを呼ぶ ---
    Character::Update(deltaTime);
}


bool Player::OnCollision(Object3d* other) {
    // 1. 相手の属性などを取得
    uint32_t attribute = other->GetCollisionAttribute();

    // 2. 衝突判定 (CollisionInfoを取得)
    CollisionInfo info = CheckCollision(other);

    // 当たっていなければここで終了
    if (!info.isColliding) {
        return false;
    }

    // =================================================================
    //  3. イベント発行 
    // =================================================================

    PlayerHitEvent event;
    event.me = this;         //「ぶつかったのは私(Player)です」と伝える
    event.hitObject = other; // ぶつかった相手
    event.normal = info.normal; //  ぶつかった角度

    // 全体通知！ -> GameRule がこれを受け取って処理してくれる
    EventManager::GetInstance()->Dispatch(event);

    // =================================================================
    // 4. 物理挙動 (押し戻し処理)
    // =================================================================
    // 地面や壁など「固いもの」なら、めり込みを直す物理処理を行う
    if (attribute & kAllSolid) {
        // 親クラス(Character)などが持つ物理処理関数へ委譲
        ApplyPhysicsCollision(info, attribute);
    }

    return true; // 衝突処理完了
}

/// <summary>
/// 描画処理
/// </summary>
void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {

    Character::Draw(pointLightResource, spotLightResource);
}