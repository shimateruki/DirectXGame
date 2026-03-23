#include "LockOnSystem.h"
#include "Object3d.h"
#include "Camera.h"
#include "Player.h"
#include "InputManager.h"
#include "CollisionManager.h" 
#include "Math.h"

// 定義 (kEnemy) のために必要ならインクルード
#include "BaseEnemy.h" // もし属性チェックで必要なら
#include <DebugConsole.h>

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

    // (1) 0キー(Zキー等)入力処理
    if (inputManager_->IsKeyTriggered(DIK_R)) {
        isLockingOn_ = !isLockingOn_;

        if (isLockingOn_) {
            // ロックオン開始 -> 対象検索
            lockOnTarget_ = FindBestTarget(objects, camera, player);
            if (!lockOnTarget_) {
                isLockingOn_ = false; // 見つからなかったら解除
            }
            else {
                lostSightTimer_ = 0.0f; // ★ 新規ロックオン時はタイマーをリセット
            }
        }

        if (!isLockingOn_) {
            // ロックオン解除
            lockOnTarget_ = nullptr;
            camera->SyncRotationToCurrentView();
            camera->SetLockOnTarget(nullptr);
            camera->SetFollowMode(Camera::FollowMode::kAimable); // 通常モード
            lostSightTimer_ = 0.0f; // ★ 手動解除時も念のためリセット
        }

    }

    // (2) ロックオン中の挙動
    if (isLockingOn_) {
        // ターゲットが消えた(死亡など)場合の安全対策
        if (!lockOnTarget_) {
            isLockingOn_ = false;
            // player->SetLockOn(false); // ← ここも念のためコメントアウト！！
            camera->SetFollowMode(Camera::FollowMode::kAimable);
            return;
        }

        // ========================================================
        // ★ 壁による視線切れチェック (モンハン・ダクソ方式)
        // ========================================================
        Vector3 playerPos = player->GetWorldPosition();
        Vector3 enemyPos = lockOnTarget_->GetWorldPosition();

        // 足元だと地面をすってしまうので、胸の高さ(Y+1.0f)からレイを飛ばす
        Vector3 rayStart = { playerPos.x, playerPos.y + 1.0f, playerPos.z };
        Vector3 rayEnd = { enemyPos.x, enemyPos.y + 1.0f, enemyPos.z };
        Vector3 toEnemy = { rayEnd.x - rayStart.x, rayEnd.y - rayStart.y, rayEnd.z - rayStart.z };

        float dist = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.y * toEnemy.y + toEnemy.z * toEnemy.z);
        if (dist > 0.0f) {
            Vector3 dir = { toEnemy.x / dist, toEnemy.y / dist, toEnemy.z / dist };

            // 障害物があるかチェック (第4引数の 1 は kGround などの壁属性)
            RaycastHit hit = CollisionManager::GetInstance()->Raycast(rayStart, dir, dist, 1);

            if (hit.isHit) {
                // 壁に遮られたらタイマーを進める (約60FPS想定で 0.016f ずつ加算)
                lostSightTimer_ += 0.016f;

                // 1.5秒間、壁に隠れ続けたら強制的にロックオン解除！
                if (lostSightTimer_ >= 1.5f) {
                    isLockingOn_ = false;
                    lockOnTarget_ = nullptr;
                    // player->SetLockOn(false); // ← 視線切れ解除時もコメントアウト！！
                    camera->SyncRotationToCurrentView(); // 今のカメラの向きを維持
                    camera->SetLockOnTarget(nullptr);
                    camera->SetFollowMode(Camera::FollowMode::kAimable);
                    lostSightTimer_ = 0.0f;

                    DebugConsole::GetInstance()->AddLog("LockOn Lost: Target behind wall.");
                    return; // 今フレームの処理はここで終了
                }
            }
            else {
                // 見えているならタイマーをリセット（一瞬でも見えれば回復する）
                lostSightTimer_ = 0.0f;
            }
        }

        // カメラ設定 (毎フレーム更新)
        camera->SetFollowMode(Camera::FollowMode::kLockOn);
        camera->SetLockOnTarget(lockOnTarget_);
    }
}
Object3d* LockOnSystem::FindBestTarget(const std::vector<std::unique_ptr<Object3d>>& objects, Camera* camera, Player* player) {
    static Math math;
    if (!player || !camera) return nullptr;

    Object3d* bestTarget = nullptr;
    float minDistance = kMaxLockOnDistance_ + 1.0f;

    Vector3 playerPos = player->GetWorldPosition();
    Vector3 cameraEye = camera->GetEye();

    // ========================================================
    // ★ 修正1: Y軸を無視せず、カメラの「本当の3Dの向き」を計算！
    // ========================================================
    Vector3 cameraForward = camera->GetTargetPoint() - cameraEye;
    float cfLen = std::sqrt(cameraForward.x * cameraForward.x + cameraForward.y * cameraForward.y + cameraForward.z * cameraForward.z);
    if (cfLen > 0.001f) {
        cameraForward.x /= cfLen;
        cameraForward.y /= cfLen;
        cameraForward.z /= cfLen;
    }
    else {
        cameraForward = { 0.0f, 0.0f, 1.0f };
    }

    const uint32_t kTargetAttribute = 2; // kEnemy (例)

    for (const auto& obj : objects) {
        // 1. 敵属性を持たないものは除外
        if (!(obj->GetCollisionAttribute() & kTargetAttribute)) {
            continue;
        }

        // 2. 自分自身、および「自分の子パーツ(武器や手足など)」を除外
        bool isPlayerPart = false;
        Object3d* current = obj.get();
        while (current) {
            if (current == player) { isPlayerPart = true; break; }
            current = current->GetParent();
        }
        if (isPlayerPart) continue;

        // 3. ブロックなど無関係なものを除外
        std::string name = obj->GetName();
        if (name.find("Block") != std::string::npos || name.find("block") != std::string::npos) continue;

        // -----------------------------------------------------------------
        //  (D) 距離と角度のチェック（完全3D化！）
        // -----------------------------------------------------------------
        Vector3 enemyPos = obj->GetWorldPosition();

        // プレイヤーからの距離（ロックオン可能距離の判定などに使う）
        Vector3 toEnemyFromPlayer = enemyPos - playerPos;
        float distance = math.Length(toEnemyFromPlayer);

        if (distance > kMaxLockOnDistance_ || distance < 0.1f) continue;

        // ========================================================
        // ★ 修正2: 「プレイヤーから」ではなく、「カメラから」敵への方向を計算！
        // これにより『画面の中に敵が映っているか』を正確に判定できます。
        // ========================================================
        Vector3 toEnemyFromCam = enemyPos - cameraEye;
        float teCamLen = std::sqrt(toEnemyFromCam.x * toEnemyFromCam.x + toEnemyFromCam.y * toEnemyFromCam.y + toEnemyFromCam.z * toEnemyFromCam.z);
        if (teCamLen > 0.001f) {
            toEnemyFromCam.x /= teCamLen;
            toEnemyFromCam.y /= teCamLen;
            toEnemyFromCam.z /= teCamLen;
        }

        // カメラの向きと、敵への方向の3D内積（画面に入っているかチェック）
        float dot = math.Dot(cameraForward, toEnemyFromCam);

        // ========================================================
        // ★ 修正3: カメラの視野内(dot > -0.2f)にいて、一番近い敵を探す！
        // ========================================================
        if (dot > -0.2f && distance < minDistance) {

            // 15m以内なら壁を無視して確定（激甘判定）
            if (distance < 15.0f) {
                minDistance = distance;
                bestTarget = obj.get();
                continue;
            }

            // 15m以上の遠距離なら壁チェック（レイを1.5mの高さから撃つ）
            Vector3 rayStart = { playerPos.x, playerPos.y + 1.5f, playerPos.z };
            Vector3 rayEnd = { enemyPos.x, enemyPos.y + 1.5f, enemyPos.z };
            Vector3 toEnemy3D = { rayEnd.x - rayStart.x, rayEnd.y - rayStart.y, rayEnd.z - rayStart.z };

            float trueDist = std::sqrt(toEnemy3D.x * toEnemy3D.x + toEnemy3D.y * toEnemy3D.y + toEnemy3D.z * toEnemy3D.z);

            if (trueDist > 0.0f) {
                Vector3 toEnemyNormalized = { toEnemy3D.x / trueDist, toEnemy3D.y / trueDist, toEnemy3D.z / trueDist };

                RaycastHit hit = CollisionManager::GetInstance()->Raycast(
                    rayStart,
                    toEnemyNormalized,
                    trueDist,
                    1
                );

                // 間に壁がなければ、最も良いターゲットとして更新
                if (!hit.isHit) {
                    minDistance = distance;
                    bestTarget = obj.get();
                }
            }
        }
    }

    return bestTarget;
}