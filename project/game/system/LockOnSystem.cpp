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
#include "CameraEditor.h"

namespace {
bool IsIgnoredForLockOnOcclusion(Object3d* object) {
    for (Object3d* current = object; current; current = current->GetParent()) {
        const std::string className = current->GetClassName();
        const std::string enemyType = current->GetEnemyType();
        const std::string name = current->GetName();

        if (className == "Enemy" || className == "BossCore") {
            return true;
        }
        if (!enemyType.empty()) {
            return true;
        }
        if (name.find("Armor") != std::string::npos ||
            name.find("armor") != std::string::npos ||
            name.find("Boss") != std::string::npos) {
            return true;
        }
    }

    return false;
}
}

LockOnSystem::LockOnSystem() {
    inputManager_ = nullptr;
    lockOnTarget_ = nullptr;
    isLockingOn_ = false;
}

LockOnSystem::~LockOnSystem() {
    // ポインタは外部管理なのでdeleteしない
}
void LockOnSystem::SetForceLockOn(Object3d* target, bool isForced) {
    isForced_ = isForced;
    if (isForced_) {
        lockOnTarget_ = target;
        isLockingOn_ = true;
    }
}
void LockOnSystem::Initialize(InputManager* inputManager) {
    inputManager_ = inputManager;
}

bool LockOnSystem::IsTargetOnScreen(Object3d* target, Camera* camera) const {
    if (!target || !camera) {
        return false;
    }

    static Math math;
    Matrix4x4 viewProjection = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    AABB aabb = target->GetAABB();
    Vector3 targetCenter = {
        (aabb.min.x + aabb.max.x) * 0.5f,
        (aabb.min.y + aabb.max.y) * 0.5f,
        (aabb.min.z + aabb.max.z) * 0.5f
    };

    float w =
        targetCenter.x * viewProjection.m[0][3] +
        targetCenter.y * viewProjection.m[1][3] +
        targetCenter.z * viewProjection.m[2][3] +
        viewProjection.m[3][3];
    if (w <= 0.001f) {
        return false;
    }

    float ndcX =
        (targetCenter.x * viewProjection.m[0][0] +
         targetCenter.y * viewProjection.m[1][0] +
         targetCenter.z * viewProjection.m[2][0] +
         viewProjection.m[3][0]) / w;
    float ndcY =
        (targetCenter.x * viewProjection.m[0][1] +
         targetCenter.y * viewProjection.m[1][1] +
         targetCenter.z * viewProjection.m[2][1] +
         viewProjection.m[3][1]) / w;
    float ndcZ =
        (targetCenter.x * viewProjection.m[0][2] +
         targetCenter.y * viewProjection.m[1][2] +
         targetCenter.z * viewProjection.m[2][2] +
         viewProjection.m[3][2]) / w;

    constexpr float kScreenMargin = 1.05f;
    return ndcX >= -kScreenMargin && ndcX <= kScreenMargin &&
           ndcY >= -kScreenMargin && ndcY <= kScreenMargin &&
           ndcZ >= 0.0f && ndcZ <= 1.0f;
}

void LockOnSystem::Update(const std::vector<std::unique_ptr<Object3d>>& objects, Camera* camera, Player* player) {
    if (!inputManager_ || !camera || !player) return;

    // システムが無効な場合はロックオンを解除して終了
    if (!isEnabled_) {
        if (isLockingOn_) {
            isLockingOn_ = false;
            lockOnTarget_ = nullptr;
            camera->SetFollowMode(Camera::FollowMode::kAimable);
            camera->SetLockOnTarget(nullptr);
            camera->SyncRotationToCurrentView();
            CameraEditor::GetInstance()->SyncSettingsFromCamera();
            lostSightTimer_ = 0.0f;
        }
        return;
    }

    Object3d* forceTarget = player->GetForceLockOnTarget();
    if (forceTarget) {
        // カメラに強制ターゲットを教える
        camera->SetFollowMode(Camera::FollowMode::kLockOn);
        camera->SetLockOnTarget(forceTarget);

        // 内部のロックオン状態も強制的に上書き
        lockOnTarget_ = forceTarget;
        isLockingOn_ = true;
		DebugConsole::GetInstance()->AddLog("LockOn Forced: Target set by Player.");
        return;
    }

    // 強制解除リクエストがあればクリアする
    if (player->ConsumeClearLockOnRequest()) {
        isLockingOn_ = false;
        lockOnTarget_ = nullptr;
        camera->SetFollowMode(Camera::FollowMode::kAimable);
        camera->SetLockOnTarget(nullptr);
        camera->SyncRotationToCurrentView();
        CameraEditor::GetInstance()->SyncSettingsFromCamera(); // ★ エディタ設定を同期
        DebugConsole::GetInstance()->AddLog("LockOn Cleared by Player Request.");
    }
    // ========================================================
    // シネマティックカメラ（演出）起動時や自由カメラモード時の強制解除
    // ========================================================
    if (camera->IsOverridden() || CameraEditor::GetInstance()->IsEditorMode()) {
        if (isLockingOn_) {
            isLockingOn_ = false;
            lockOnTarget_ = nullptr;
            camera->SetFollowMode(Camera::FollowMode::kAimable);
            camera->SetLockOnTarget(nullptr);
            camera->SyncRotationToCurrentView(); // 視点のガクつき防止
            CameraEditor::GetInstance()->SyncSettingsFromCamera(); // エディタ設定を同期
            lostSightTimer_ = 0.0f;

            DebugConsole::GetInstance()->AddLog("LockOn Canceled: Cinematic or Editor Camera Active.");
        }
        return; // 以降のロックオン制御を行わない
    }

    // ========================================================
    // (1) ボタン入力による手動ロックオン切り替え
    // ========================================================
    // ムービー中（カメラのオーバーライド中・自由カメラモード中）はロックオンを開始できないようにする
    if (!camera->IsOverridden() && !CameraEditor::GetInstance()->IsEditorMode() && inputManager_->IsActionTriggered("LockOn")) {
        isLockingOn_ = !isLockingOn_;

        if (isLockingOn_) {
            // ロックオン開始 -> 対象検索
            lockOnTarget_ = FindBestTarget(objects, camera, player);
            if (!lockOnTarget_) {
                isLockingOn_ = false; // 見つからなかったら即解除
            }
            else {
                lostSightTimer_ = 0.0f;
            }
        }

        if (!isLockingOn_) {
            // 手動でのロックオン解除
            lockOnTarget_ = nullptr;
            camera->SetFollowMode(Camera::FollowMode::kAimable);
            camera->SetLockOnTarget(nullptr);
            camera->SyncRotationToCurrentView(); // 視点のガクつき防止
            CameraEditor::GetInstance()->SyncSettingsFromCamera(); // ★ エディタ設定を同期
            lostSightTimer_ = 0.0f;
        }
    }

    // ========================================================
    // (2) ロックオン中の挙動
    // ========================================================
    if (isLockingOn_) {
        // ターゲット消失(死亡や削除など)の場合の安全対策
        if (!lockOnTarget_) {
            isLockingOn_ = false;
            camera->SetFollowMode(Camera::FollowMode::kAimable);
            camera->SetLockOnTarget(nullptr);
            camera->SyncRotationToCurrentView();
            CameraEditor::GetInstance()->SyncSettingsFromCamera(); // ★ エディタ設定を同期
            return;
        }

        // --- ★ 追加: ターゲットが既に ObjectManager から削除されていないか(ダングリングポインタか)チェック ---
        bool isTargetValid = false;
        for (const auto& obj : objects) {
            if (obj.get() == lockOnTarget_) {
                isTargetValid = true;
                break;
            }
        }
        
        // 見つからなかった（削除された）場合、または死亡フラグが立っている場合
        if (!isTargetValid || lockOnTarget_->isDead) {
            isLockingOn_ = false;
            lockOnTarget_ = nullptr;
            camera->SetFollowMode(Camera::FollowMode::kAimable);
            camera->SetLockOnTarget(nullptr);
            camera->SyncRotationToCurrentView();
            CameraEditor::GetInstance()->SyncSettingsFromCamera();
            lostSightTimer_ = 0.0f;
            DebugConsole::GetInstance()->AddLog("LockOn Lost: Target destroyed.");
            return;
        }

        Vector3 playerPos = player->GetWorldPosition();
        Vector3 enemyPos = lockOnTarget_->GetWorldPosition();
        static Math math;

        // --------------------------------------------------------
        // 距離による強制解除 (捕捉距離 + 5.0f の遊びを持たせる)
        // --------------------------------------------------------
        Vector3 toEnemyDist = { enemyPos.x - playerPos.x, enemyPos.y - playerPos.y, enemyPos.z - playerPos.z };
        float currentDist = math.Length(toEnemyDist);

        if (currentDist > kMaxLockOnDistance_) {
            isLockingOn_ = false;
            lockOnTarget_ = nullptr;
            camera->SetFollowMode(Camera::FollowMode::kAimable);
            camera->SetLockOnTarget(nullptr);
            camera->SyncRotationToCurrentView();
            CameraEditor::GetInstance()->SyncSettingsFromCamera(); // ★ エディタ設定を同期
            lostSightTimer_ = 0.0f;

            DebugConsole::GetInstance()->AddLog("LockOn Lost: Target exceeded release distance.");
            return;
        }

        // --------------------------------------------------------
        // 障害物による視線切れチェック (間に壁があれば即解除)
        // --------------------------------------------------------
        // 足元だと地面をすってしまうので、胸の高さ(Y+1.0f)からレイを飛ばす
        Vector3 rayStart = { playerPos.x, playerPos.y + 1.0f, playerPos.z };
        Vector3 rayEnd = { enemyPos.x, enemyPos.y + 1.0f, enemyPos.z };
        Vector3 toEnemy = { rayEnd.x - rayStart.x, rayEnd.y - rayStart.y, rayEnd.z - rayStart.z };

        float dist = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.y * toEnemy.y + toEnemy.z * toEnemy.z);
        if (dist > 0.0f) {
            Vector3 dir = { toEnemy.x / dist, toEnemy.y / dist, toEnemy.z / dist };

            // 障害物チェック (属性1: kGround などの壁)
            RaycastHit hit = CollisionManager::GetInstance()->RaycastFiltered(
                rayStart, dir, dist, kGround | kMapBlock, IsIgnoredForLockOnOcclusion);

            if (hit.isHit) {
                // 壁に遮られたらタイマーを進める
                lostSightTimer_ += 0.016f;

                // 0.1秒(約6フレーム) 遮られたら即座に解除！
                if (lostSightTimer_ >= 0.1f) {
                    isLockingOn_ = false;
                    lockOnTarget_ = nullptr;
                    camera->SetFollowMode(Camera::FollowMode::kAimable);
                    camera->SetLockOnTarget(nullptr);
                    camera->SyncRotationToCurrentView();
                    CameraEditor::GetInstance()->SyncSettingsFromCamera(); // ★ エディタ設定を同期
                    lostSightTimer_ = 0.0f;

                    DebugConsole::GetInstance()->AddLog("LockOn Lost: Target behind wall.");
                    return;
                }
            }
            else {
                // 見えているならタイマーをリセット
                lostSightTimer_ = 0.0f;
            }
        }

        // --------------------------------------------------------
        // カメラ設定 (毎フレーム更新)
        // --------------------------------------------------------
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
        if (!IsTargetOnScreen(obj.get(), camera)) continue;

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
        // ★ 修正3: カメラの正面かつ画面内にいて、一番近い敵を探す！
        // ========================================================
        if (dot > kMinLockOnDot_ && distance < minDistance) {

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

                RaycastHit hit = CollisionManager::GetInstance()->RaycastFiltered(
                    rayStart,
                    toEnemyNormalized,
                    trueDist,
                    kGround | kMapBlock,
                    IsIgnoredForLockOnOcclusion
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
