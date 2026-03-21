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
    if (inputManager_->IsKeyTriggered(DIK_0)) {
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
    float maxDot = -2.0f; // 内積の初期値

    Vector3 playerPos = player->GetWorldPosition();

    // -----------------------------------------------------------------
    //  (A) カメラの前方ベクトルを計算（上下のY軸を無視して平面で扱う）
    // -----------------------------------------------------------------
    Vector3 cameraForward = camera->GetTargetPoint() - camera->GetEye();
    cameraForward.y = 0.0f; // 高さを無視
    float cfLen = std::sqrt(cameraForward.x * cameraForward.x + cameraForward.z * cameraForward.z);
    if (cfLen > 0.001f) {
        cameraForward.x /= cfLen;
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

        // -----------------------------------------------------------------
        //  (B) 自分自身、および「自分の子パーツ(武器や手足など)」を完全に除外
        //  ※これでロックオン解除時に自分をターゲットしてしまうバグが消滅します！
        // -----------------------------------------------------------------
        bool isPlayerPart = false;
        Object3d* current = obj.get();
        while (current) {
            if (current == player) {
                isPlayerPart = true;
                break;
            }
            current = current->GetParent(); // 親を辿ってプレイヤーかチェック
        }
        if (isPlayerPart) {
            continue;
        }

        // -----------------------------------------------------------------
        //  (C) 0,0,0付近に吸われる元凶「ブロック」を除外
        // -----------------------------------------------------------------
        std::string name = obj->GetName();
        if (name.find("Block") != std::string::npos || name.find("block") != std::string::npos) {
            continue;
        }

        // -----------------------------------------------------------------
        //  (D) 距離と角度のチェック（Y軸を無視した2D判定）
        // -----------------------------------------------------------------
        Vector3 enemyPos = obj->GetWorldPosition();
        Vector3 toEnemy = enemyPos - playerPos;
        float distance = math.Length(toEnemy);

        // 距離が遠すぎる、または近すぎる場合は除外
        if (distance > kMaxLockOnDistance_ || distance < 0.1f) continue;

        // 敵への方向を平面(2D)にする
        Vector3 toEnemy2D = toEnemy;
        toEnemy2D.y = 0.0f;
        float teLen = std::sqrt(toEnemy2D.x * toEnemy2D.x + toEnemy2D.z * toEnemy2D.z);
        if (teLen > 0.001f) {
            toEnemy2D.x /= teLen;
            toEnemy2D.z /= teLen;
        }

        // 平面同士の内積（角度チェック）
        float dot = math.Dot(cameraForward, toEnemy2D);

        // 画面前方180度(dot > 0.0f)にいて、かつ一番画面中央(maxDot)に近いものを探す
        if (dot > 0.0f && dot > maxDot) {

            // -----------------------------------------------------------------
            //  (E) 遮蔽物（壁）チェック
            // -----------------------------------------------------------------
            // ※壁チェックのレイ(光線)は、上下も含めた「本当の3D方向」に飛ばす
            Vector3 toEnemyNormalized = toEnemy / distance;
            RaycastHit hit = CollisionManager::GetInstance()->Raycast(
                playerPos,          // 開始点
                toEnemyNormalized,  // 本当の3D方向
                distance,           // 最大距離
                1                   // kGround (例: 地面・壁属性)
            );

            // 間に壁がなければ、最も良いターゲットとして更新
            if (!hit.isHit) {
                maxDot = dot;
                bestTarget = obj.get();
            }
        }
    }

    return bestTarget;
}