#define NOMINMAX
#include "PlayerState.h"
#include "Player.h"
#include "InputManager.h"
#include "DebugConsole.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include "engine/utility/math/Math.h"
#include "BaseEnemy.h"
#include "EnemyGiantSlime.h"
#include "GimmickHookPullBlock.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

// 拘束した敵を持ち運ぶ状態をまとめています。

void PlayerStateCarry::Enter(Player* player) {
    if (player) {
        player->SetIsControlActive(true); // 持ち運び中もプレイヤーは動ける！
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Carry);
        struggleTimer_ = 0.0f;

        Object3d* enemy = player->GetCarriedEnemy();
        if (enemy) {
            carriedBaseScale_ = enemy->GetScale();
            // 敵の衝突判定を無効化（kNone等）して無力化する
            // ※ColliderのSetAttribute等があればここで当たり判定を無効化します
            // enemy->GetCollider()->SetAttribute(0); 
        }
    }
}

void PlayerStateCarry::Update(Player* player, float deltaTime) {
    if (!player) return;

    struggleTimer_ += deltaTime;

    Object3d* enemy = player->GetCarriedEnemy();
    if (enemy) {
        Vector3 playerPos = player->GetWorldPosition();
        
        // 【抗っている感の演出（汎用プロシージャルアニメーション）】
        // 1. 座標の揺れ（ジタバタと細かく暴れる）
        float offsetX = std::sin(struggleTimer_ * 35.0f) * 0.15f;
        float offsetZ = std::cos(struggleTimer_ * 30.0f) * 0.15f;
        float offsetY = std::sin(struggleTimer_ * 45.0f) * 0.08f;

        enemy->GetTransform()->translate = { 
            playerPos.x + offsetX, 
            playerPos.y + 2.5f + offsetY, 
            playerPos.z + offsetZ 
        };

        // 2. 回転の揺れ（体をよじる、イヤイヤと暴れる動き）
        Vector3 rot;
        rot.x = std::sin(struggleTimer_ * 20.0f) * 0.2f;
        // プレイヤーの現在のY軸回転を基準に、首振り角度をオフセットとして適用する
        rot.y = player->GetRotation().y + std::sin(struggleTimer_ * 15.0f) * 0.4f; 
        rot.z = std::cos(struggleTimer_ * 22.0f) * 0.2f;
        enemy->GetTransform()->rotate = rot;

        // 3. スケールの伸縮（息遣いや力を込めるような Squash & Stretch）
        float stretch = std::sin(struggleTimer_ * 25.0f) * 0.05f;
        enemy->GetTransform()->scale = { 
            carriedBaseScale_.x * (1.0f - stretch),
            carriedBaseScale_.y * (1.0f + stretch),
            carriedBaseScale_.z * (1.0f - stretch)
        };

        // 追加: クォータニオンを無視させ、手動でマトリックスを更新する
        enemy->GetTransform()->isQuaternionMaster = false;
        enemy->UpdateLocalMatrix();
        enemy->UpdateWorldMatrix();
    }

    // （※ここに後で「左クリックで投げる」処理を追加します）
}

void PlayerStateCarry::Exit(Player* player) {
    // 投げた時などに呼ばれる
    if (player) {
        Object3d* enemy = player->GetCarriedEnemy();
        if (enemy) {
            // 持ち運びが終わったら（投げる等）、姿勢とスケールを元に戻す
            enemy->GetTransform()->scale = carriedBaseScale_;
            enemy->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
            enemy->GetTransform()->isQuaternionMaster = true; // クォータニオンモードに戻す
            enemy->UpdateLocalMatrix();
            enemy->UpdateWorldMatrix();
        }
        player->ReleaseCarriedEnemy(true);
    }
}
