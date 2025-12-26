// GameRule.cpp
#include "GameRule.h"
#include "EventManager.h"
#include "DebugConsole.h"

void GameRule::Initialize() {

    // ------------------------------------------------
    //  PlayerHitEvent (プレイヤーや、操作キャラが衝突した時)
    // ------------------------------------------------
    EventManager::GetInstance()->Subscribe([this](const PlayerHitEvent& event) {

        Object3d* whoHit = event.me;           // ぶつかった人（Playerなど）
        Object3d* objectHit = event.hitObject; // ぶつかられた物（床や罠）

        // 相手がnullなら無視
        if (!whoHit || !objectHit) return;

        // 相手のタイプによって処理を変える
        EventType type = objectHit->GetEventType();

        switch (type) {
        case EventType::Damage:
            ApplyDamage(whoHit, 10.0f); 
            break;
        case EventType::None:
        default:
            // 何もしない
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

