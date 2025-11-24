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
    SetColliderType(ColliderType::kAABB);
    SetCollisionSize({ 1.0f, 1.0f, 1.0f });
}

void Player::Update(float deltaTime) {
    static Math math;

    // --- 1. 移動戦略（ストラテジー）による移動ベクトルの計算 ---
    if (moveStrategy_) {
        // 戦略クラスに移動ベクトルを計算してもらう
        Vector3 move = moveStrategy_->CalculateVelocity(this);

        // 計算結果を速度に反映 (X と Z のみ)
        velocity_.x = move.x;
        velocity_.z = move.z;
    }

    // --- 2. 回転処理 ---
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

    // --- 3. ジャンプ処理 ---
    if (isGrounded_ && inputManager_->IsKeyTriggered(DIK_SPACE)) {
        const float kJumpVelocity = 30.0f; 
        velocity_.y = kJumpVelocity;
        //ジャンプパーティクル
        if (particleSystem_) {
            Vector3 pos = GetWorldPosition(); // 足元の位置
            pos.y -= 1.0f; // ブロックの半分 (1.0f) 下、地面の位置

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



    // XZ平面の速度（速さ）を計算
    Vector3 horizontalVelocity = { velocity_.x, 0.0f, velocity_.z };
    float speed = math.Length(horizontalVelocity);

    // 一定の速度以上で、かつ地面にいる時
    const float kDashSpeedThreshold = 5.0f; // この値より速く走ったらエフェクトON
    if (speed > kDashSpeedThreshold && isGrounded_ && particleSystem_) {

        // 速度ベクトルの逆方向（後ろに流すため）
        Vector3 direction = math.Normalize(horizontalVelocity) * -1.0f;

        Vector3 pos = GetWorldPosition();
        pos.y -= 1.0f; // 地面の位置

        particleSystem_->SpawnParticles(
            pos,                      // 発生座標 (足元)
            3,                        // 個数 (毎フレーム出すので1個で十分)
            1.0f,                     // 初速 (少しだけ)
            &direction,               // 方向 (移動と逆方向)
            30.0f,                    // 拡散範囲 (30度くらい)
            { 0.8f, 0.8f, 0.8f, 1.0f }, // 初期色 (白/煙)
            { 0.5f, 0.5f, 0.5f, 0.0f }, // 終了色 (暗く・透明に)
            0.2f, 0.3f,               // 生存時間 (ごく短く)
            2.0f,                     // 開始サイズ
            1.0f                      // 終了サイズ (小さくなって消える)
        );
    }


    if (!isGrounded_ && velocity_.y > 0.0f) {
        if (particleSystem_) {
            // 足元の位置
            Vector3 footPos = transform_.translate;
            footPos.y -= 1.0f; // ブロックの下端にあわせる

            // 少しランダムに散らす
            float spread = 0.5f;
            footPos.x += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * spread;
            footPos.z += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * spread;

            // 下向きのベクトル
            Vector3 downVel = { 0.0f, -5.0f, 0.0f }; // 下に噴射

            particleSystem_->SpawnParticles(
                footPos,
                1,                            // 個数：毎フレーム1個
                0.0f,                         // 初速：
                nullptr,
                0.0f,
                { 0.8f, 0.8f, 0.8f, 0.8f },   // 色：白/グレー（煙）
                { 0.0f, 0.0f, 0.0f, 0.0f },   // 終了色：黒/透明
                0.3f, 0.5f,                   // 寿命：短め
                0.8f, 0.0f                    // サイズ
            );
        }
    }
    if (!wasGrounded_ && isGrounded_) {

        // 足元の位置
        Vector3 footPos = transform_.translate;
        footPos.y -= 1.0f; // ブロックの底面に合わせる

        // 土煙を放射状に出す
        particleSystem_->SpawnParticles(
            footPos,
            10,                           // 個数：10個くらいバッと出す
            3.0f,                         // 初速：少し勢いよく
            nullptr,                      // 方向：指定なし（全方位）
            90.0f,                        // 拡散
            { 0.6f, 0.5f, 0.4f, 1.0f },   // 色：茶色っぽい土色
            { 0.6f, 0.5f, 0.4f, 0.0f },   // 終了色：透明へ
            0.5f, 0.8f,                   // 寿命：0.5秒くらいかけてふわっと消える
            2.0f, 4.0f                    // サイズ：2.0 から 4.0 へ大きく広がる
        );

        // 衝撃の瞬間に少し白い煙も混ぜるとリアル
        particleSystem_->SpawnParticles(
            footPos,
            5,
            5.0f,                         // 速い
            nullptr,
            90.0f,
            { 0.9f, 0.9f, 0.9f, 0.8f },   // 白
            { 1.0f, 1.0f, 1.0f, 0.0f },
            0.2f, 0.4f,                   // 短命
            1.0f, 0.0f
        );
    }

    // 次のフレームのために今の状態を記録
    wasGrounded_ = isGrounded_;

    // --- 4. 親(Character)のUpdateを呼ぶ ---
    Character::Update(deltaTime);
}

bool Player::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();

    // ★ 1. まず Player 側で CollisionInfo を取得
    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) {
        return false; // 当たってないなら即終了
    }

    // --- 2. 物理処理 (地面) ---
    if (attribute & kAllSolid) {
        // ★ 親の物理処理関数を呼ぶ (押し戻し・接地判定)
        ApplyPhysicsCollision(info, attribute);
    }

    // --- 3. イベント発行 ---
    if (attribute & (kEnemy)) { 

        // ★ イベントに法線(info.normal)を詰めて発行する
        EventManager::GetInstance()->Dispatch(PlayerHitEvent{ other, info.normal });
    }

    return true; // (当たった)
}

/// <summary>
/// 描画処理
/// </summary>
void Player::Draw() {

    Character::Draw();
}