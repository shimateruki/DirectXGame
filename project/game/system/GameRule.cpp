
#include "GameRule.h"
#include "EventManager.h"
#include "DebugConsole.h"
#include "BaseScene.h"

// 引数を受け取るように変更
// GameRule.cpp

void GameRule::Initialize(BaseScene* scene) {
    scene_ = scene;

    EventManager::GetInstance()->Subscribe([this](const PlayerHitEvent& event) {

        Object3d* whoHit = event.me;           // プレイヤー
        Object3d* objectHit = event.hitObject; // 当たった物

        if (!whoHit || !objectHit) return;


        // --------------------------------------------------------
        // タイプ判定：当たったのは何？
        // --------------------------------------------------------
        EventType type = objectHit->GetEventType(); // ここでタイプを見る！

        switch (type) {

            // パターンA：ワープ入口に触れた！
        case EventType::Warp:
        {
            // 1. 行き先のIDを取得
            int targetID = objectHit->GetTargetID();

            // 2. シーンの中から、そのIDを持つ「出口」を探す
            if (scene_ && targetID != -1) {
                Object3d* exitPoint = scene_->FindObjectByEventID(targetID);

                // 3. 出口が見つかったら、プレイヤーをそこに飛ばす！
                if (exitPoint) {
                    // 出口の座標を取得
                    Vector3 destPos = exitPoint->GetTransform()->translate;

                    // プレイヤーの座標を上書き（ワープ！）
                    whoHit->GetTransform()->translate = destPos;

                    DebugConsole::GetInstance()->AddLog("Warped to ID: " + std::to_string(targetID));
                }
            }
        }
        break;


        // パターンB：ダメージ床
        case EventType::Damage:
            ApplyDamage(whoHit, 10.0f);
            break;


            // パターンC：それ以外（普通のスイッチなど）
        case EventType::None:
        default:
            // ワープでもダメージでもないなら、従来の「遠隔スイッチ」として扱う
            int tID = objectHit->GetTargetID();
            if (tID != -1 && scene_) {
                scene_->TriggerEvent(tID); // ドアが開く等の処理
            }
            break;
        }

        });
}

// ▼ 汎用ダメージ関数
void GameRule::ApplyDamage(Object3d* target, float damage) {
    // ステータス(param_)を持っているか確認
    if (target->param_.has_value()) {
        target->param_->hp -= damage;
        DebugConsole::GetInstance()->AddLog("Ouch! Taken Damage! HP: " + std::to_string(target->param_->hp));

        // HPが尽きたら...などの処理
        if (target->param_->hp <= 0) {
            target->param_->hp = 0;
            DebugConsole::GetInstance()->AddLog("Dead!");

        }
    }
}

