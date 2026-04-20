#include "GameRule.h"
#include "EventManager.h"
#include "DebugConsole.h"
#include "BaseScene.h"
#include "Event.h" 
#include "Player.h"
#include "GamePlayScene.h"

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
        case EventType::Movie_Boss:
            if (scene_) {
                if (GamePlayScene* gps = dynamic_cast<GamePlayScene*>(scene_)) {
                    gps->StartBossAppearanceMovie(); // シーンに合図を送る！
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

        });

    // ========================================================
    // ★戦闘のダメージイベントを受信！
    // ========================================================
    EventManager::GetInstance()->Subscribe([this](const DamageEvent& event) {
        if (!event.target) return;

        // 汎用関数 ApplyDamage を呼ぶだけ！
        ApplyDamage(event.target, event.damageAmount);
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