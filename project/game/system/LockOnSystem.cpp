#include "LockOnSystem.h"
#include "Object3d.h"
#include "Camera.h"
#include "Player.h"
#include "InputManager.h"
#include "CollisionManager.h" 
#include "Math.h"

// 定義 (kEnemy) のために必要ならインクルード
#include "BaseEnemy.h" // もし属性チェックで必要なら

LockOnSystem::LockOnSystem() {
    inputManager_ = nullptr;
    lockOnTarget_ = nullptr;
    isLockingOn_ = false;
}

LockOnSystem::~LockOnSystem() {
    // ポインタは外部管理なのでdeleteしない
}

void LockOnSystem::Initialize(InputManager* inputManager) {
    inputManager_ = inputManager;
}

void LockOnSystem::Update(const std::vector<std::unique_ptr<Object3d>>& objects, Camera* camera, Player* player) {
    if (!inputManager_ || !camera || !player) return;

    //// (1) Zキー入力処理
    //if (inputManager_->IsKeyTriggered(DIK_0)) {
    //    isLockingOn_ = !isLockingOn_;

    //    if (isLockingOn_) {
    //        // ロックオン開始 -> 対象検索
    //        lockOnTarget_ = FindBestTarget(objects, camera, player);
    //        if (!lockOnTarget_) {
    //            isLockingOn_ = false; // 見つからなかったら解除
    //        }
    //    }

    //    if (!isLockingOn_) {
    //        // ロックオン解除
    //        lockOnTarget_ = nullptr;
    //        camera->SyncRotationToCurrentView();
    //        camera->SetLockOnTarget(nullptr);
    //        camera->SetFollowMode(Camera::FollowMode::kAimable); // 通常モード
    //    }

    //    player->SetLockOn(isLockingOn_); // プレイヤーに通知
    //}

    //// (2) ロックオン中の挙動
    //if (isLockingOn_) {
    //    // ターゲットが消えた(死亡など)場合の安全対策
    //    if (!lockOnTarget_) {
    //        isLockingOn_ = false;
    //        player->SetLockOn(false);
    //        camera->SetFollowMode(Camera::FollowMode::kAimable);
    //        return;
    //    }

    //    // カメラ設定 (毎フレーム更新)
    //    camera->SetFollowMode(Camera::FollowMode::kLockOn);
    //    camera->SetLockOnTarget(lockOnTarget_);

    //    // プレイヤーの向き制御 (Y軸だけ敵に向ける)
    //    Vector3 playerPos = player->GetWorldPosition();
    //    Vector3 enemyPos = lockOnTarget_->GetWorldPosition();
    //    Vector3 toEnemy = enemyPos - playerPos;

    //    static Math math;
    //    player->SetRotationY(std::atan2(toEnemy.x, toEnemy.z));
    //}
}

Object3d* LockOnSystem::FindBestTarget(const std::vector<std::unique_ptr<Object3d>>& objects, Camera* camera, Player* player) {
    static Math math;
    if (!player || !camera) return nullptr;

    Object3d* bestTarget = nullptr;
    float maxDot = -2.0f; // 内積の初期値

    Vector3 playerPos = player->GetWorldPosition();
    Vector3 cameraForward = math.Normalize(camera->GetTargetPoint() - camera->GetEye());


    const uint32_t kTargetAttribute = 2; // kEnemy (例)

    for (const auto& obj : objects) {
        // 敵属性を持ち、かつ自分自身でないもの
        if (!(obj->GetCollisionAttribute() & kTargetAttribute) || obj.get() == player) {
            continue;
        }

        Vector3 enemyPos = obj->GetWorldPosition();
        Vector3 toEnemy = enemyPos - playerPos;
        float distance = math.Length(toEnemy);

        // 距離チェック
        if (distance > kMaxLockOnDistance_ || distance < 0.1f) continue;

        Vector3 toEnemyNormalized = toEnemy / distance;
        float dot = math.Dot(cameraForward, toEnemyNormalized);

        // 視界チェック (正面に近いほど優先)
        if (dot > kMinLockOnDot_ && dot > maxDot) {

            // レイキャストによる遮蔽物チェック
            RaycastHit hit = CollisionManager::GetInstance()->Raycast(
                playerPos,          // 開始点
                toEnemyNormalized,  // 方向
                distance,           // 最大距離
                1                   // kGround (例: 地面・壁属性)
            );

            // 間に壁がなければ採用
            if (!hit.isHit) {
                maxDot = dot;
                bestTarget = obj.get();
            }
        }
    }

    return bestTarget;
}