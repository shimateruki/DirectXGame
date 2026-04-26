#include "GameRule.h"
#include "EventManager.h"
#include "DebugConsole.h"
#include "BaseScene.h"
#include "Event.h" 
#include "Player.h"
#include "GamePlayScene.h"
#include "GameDataManager.h"
#include "SceneManager.h"
#include <PlayerState.h>

void GameRule::Initialize(BaseScene* scene) {
    scene_ = scene;

    // --------------------------------------------------------
    // ① プレイヤーがギミック等に触れたときのイベント (既存のまま)
    // --------------------------------------------------------
    EventManager::GetInstance()->Subscribe([this](const PlayerHitEvent& event) {

        Object3d* whoHit = event.me;           // プレイヤー
        Object3d* objectHit = event.hitObject; // 当たった物

        if (!whoHit || !objectHit) return;

        EventType type = objectHit->GetEventType();

        switch (type) {
        case EventType::Warp:
        {
            int targetID = objectHit->GetTargetID();
            if (scene_ && targetID != -1) {
                Object3d* exitPoint = scene_->FindObjectByEventID(targetID);
                if (exitPoint) {
                    Vector3 destPos = exitPoint->GetTransform()->translate;
                    whoHit->GetTransform()->translate = destPos;
                    DebugConsole::GetInstance()->AddLog("Warped to ID: " + std::to_string(targetID));
                }
            }
        }
        break;

        case EventType::Damage:
            ApplyDamage(whoHit, 10.0f);
            break;

        case EventType::Movie_Bridge:
            if (scene_) {
                if (GamePlayScene* gps = dynamic_cast<GamePlayScene*>(scene_)) {
                    gps->StartBridgeDropMovie();
                }
            }
            break;

        case EventType::Checkpoint:
        {
            if (Player* player = dynamic_cast<Player*>(whoHit)) {
                // 中間地点の座標をリスポーン地点として記憶
                player->SetRespawnPosition(objectHit->GetWorldPosition());
                DebugConsole::GetInstance()->AddLog("Checkpoint! Respawn point updated.");
            }
        }
        break;

        case EventType::Goal:
        {
            if (GamePlayScene* gps = dynamic_cast<GamePlayScene*>(scene_)) {
                gps->SetIsGoal(true);
                DebugConsole::GetInstance()->AddLog("GOAL! Stage Cleared. Press Space to Select.");
            }
        }
        break;

        case EventType::StarCoin:
        {
            if (GamePlayScene* gps = dynamic_cast<GamePlayScene*>(scene_)) {
                // TargetID をコインの番号 (0, 1, 2) として扱う
                int coinIdx = objectHit->GetTargetID();
                gps->CollectStarCoin(coinIdx);

                // 演出を開始して消す (Object3d側で上昇回転する)
                objectHit->StartCollectionAnimation();

                DebugConsole::GetInstance()->AddLog("Star Coin " + std::to_string(coinIdx) + " Collected!");
            }
        }
        break;

        case EventType::None:
        default:
            int tID = objectHit->GetTargetID();
            if (tID != -1 && scene_) {
                scene_->TriggerEvent(tID);
            }
            break;
        }

        // ========================================================
        // 🚨 ギミック個別の特殊処理 (Trampoline など)
        // ========================================================
        std::string gimmickType = objectHit->GetGimmickType();
        if (gimmickType == "Trampoline") {
            if (Player* player = dynamic_cast<Player*>(whoHit)) {
                // 上から踏んだ（地面からの押し返しが上方向）場合に発動
                if (event.normal.y > 0.5f) {
                    float jumpPower = 20.0f; // デフォルト値
                    if (objectHit->param_.has_value()) {
                        jumpPower = objectHit->param_->jumpPower;
                    }

                    Vector3 vel = player->GetVelocity();
                    vel.y = jumpPower;
                    player->SetVelocity(vel);
                    player->ChangeState(std::make_unique<PlayerStateJump>());

                    DebugConsole::GetInstance()->AddLog("Trampoline Jump!");
                }
            }
        }

        });

    // ========================================================
    // ★戦闘のダメージイベントを受信！
    // ========================================================
    EventManager::GetInstance()->Subscribe([this](const DamageEvent& event) {
        if (!event.target) return;

        // 汎用関数 ApplyDamage を呼ぶだけ！
        ApplyDamage(event.target, event.damageAmount);
        });

    // ========================================================
    // ★ プレイヤー死亡時の処理
    // ========================================================
    EventManager::GetInstance()->Subscribe([this](const PlayerDeathEvent& event) {
        GameDataManager::GetInstance()->SubtractLife();
        int lives = GameDataManager::GetInstance()->GetLives();

        DebugConsole::GetInstance()->AddLog("Player Died! Remaining Lives: " + std::to_string(lives));

        if (lives <= 0) {
            // 残機ゼロならゲームオーバー
            SceneManager::GetInstance()->ChangeScene("GAMEOVER");
        } else {
            // 残機があるならステージをリロードして再開
            SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
        }
    });
}

// ▼ 汎用ダメージ関数 
void GameRule::ApplyDamage(Object3d* target, float damage) {
    if (target->GetClassName() == "Player") {
        Player* player = static_cast<Player*>(target);
        // 被弾無敵（赤色）または回避ダッシュ（青色）中ならダメージを無効化！
        if (player->IsInvincible()) {
            return;
        }
    }
    if (target->param_.has_value()) {
        target->param_->hp -= damage;

        DebugConsole::GetInstance()->AddLog(target->GetName() + " に " + std::to_string(damage) + " のダメージ！ 残りHP: " + std::to_string(target->param_->hp));

        if (target->param_->hp <= 0.0f) {
            DebugConsole::GetInstance()->AddLog(target->GetName() + " を撃破！");
         /*   if (scene_) {
                scene_->RequestRemoveObject(target);
            }*/
        }
    }
}