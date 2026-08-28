#include "GameRule.h"
#include "EventManager.h"
#include "DebugConsole.h"
#include "BaseScene.h"
#include "Event.h" 
#include "Player.h"
#include "GamePlayScene.h"
#include "PreviewScene.h"
#include "GameDataManager.h"
#include "SceneManager.h"
#include "GPUParticleManager.h"
#include "VFXSequencer.h"
#include "HitEffectDirector.h"
#include "BaseEnemy.h"
#include <PlayerState.h>
#include <algorithm>
#include <cmath>

namespace {
void PlayCrownGetPresentation(Object3d* crownObject) {
    if (!crownObject) {
        return;
    }

    VFXSequencer::PlayOneShot("crown_get_cue", crownObject->GetWorldPosition());
}

DamageType ResolveDamageType(const DamageEvent& event) {
    if (event.damageType != DamageType::Physical || !event.attacker) {
        return event.damageType;
    }

    const std::string& enemyType = event.attacker->GetEnemyType();
    if (enemyType == "FireSlime") {
        return DamageType::Fire;
    }
    if (enemyType == "ThunderSlime") {
        return DamageType::Electric;
    }
    if (enemyType == "Bomb") {
        return DamageType::Explosion;
    }
    return DamageType::Physical;
}
}

void GameRule::Initialize(BaseScene* scene) {
    scene_ = scene;
    activeStatusEffects_.clear();

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
                Player* player = dynamic_cast<Player*>(whoHit);
                if (!player || player->isDead || player->GetHp() <= 0.0f) {
                    break;
                }
                if (!gps->IsGoal()) {
                    gps->StartGoalPresentation(objectHit);
                }
                DebugConsole::GetInstance()->AddLog("GOAL! Stage Cleared. Returning to Select.");
            }
            else if (PreviewScene* preview = dynamic_cast<PreviewScene*>(scene_)) {
                if (!preview->IsGoal()) {
                    PlayCrownGetPresentation(objectHit);
                    preview->SetIsGoal(true);
                    DebugConsole::GetInstance()->AddLog("PREVIEW GOAL! Press Space to Select.");
                }
            }
        }
        break;

        case EventType::StarCoin:
        {
            int coinIdx = objectHit->GetTargetID();

            if (GamePlayScene* gps = dynamic_cast<GamePlayScene*>(scene_)) {
                // TargetID をコインの番号 (0, 1, 2) として扱う
                gps->CollectStarCoin(coinIdx, objectHit->GetWorldPosition());

                // 演出を開始して消す (Object3d側で上昇回転する)
                objectHit->StartCollectionAnimation();

                // GPUパーティクルで取得演出を発生
                GPUParticleManager::GetInstance()->Emit("star_sparkleGet", objectHit->GetWorldPosition());

                DebugConsole::GetInstance()->AddLog("Star Coin " + std::to_string(coinIdx) + " Collected!");
            }
            else if (PreviewScene* preview = dynamic_cast<PreviewScene*>(scene_)) {
                preview->CollectStarCoin(coinIdx);
                objectHit->StartCollectionAnimation();
                GPUParticleManager::GetInstance()->Emit("star_sparkleGet", objectHit->GetWorldPosition());
                DebugConsole::GetInstance()->AddLog("Preview Star Coin " + std::to_string(coinIdx) + " Collected!");
            }
        }
        break;

        case EventType::None:
        default:
            // 敵のTargetIDは接触時の汎用イベントではなく、撃破完了通知として使います。
            // プレイヤーの攻撃が当たっただけでボス部屋の完了イベントを発火させません。
            if (dynamic_cast<BaseEnemy*>(objectHit)) {
                break;
            }
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

    // 戦闘のダメージイベントを受信
    // ========================================================
    EventManager::GetInstance()->Subscribe([this](const DamageEvent& event) {
        if (!event.target) return;
        Player* playerTarget = dynamic_cast<Player*>(event.target);
        if (playerTarget && playerTarget->IsCinematicLocked()) {
            return;
        }
        const DamageType damageType = ResolveDamageType(event);
        const bool isElectricPlayerHit = playerTarget && damageType == DamageType::Electric;

        float finalDamage = event.damageAmount;
        if (event.attacker && event.attacker->param_.has_value()) {
            finalDamage *= (std::max)(0.0f, event.attacker->param_->attackPower);
        }

        // 汎用関数 ApplyDamage を呼ぶ！
        const bool damageApplied = ApplyDamage(event.target, finalDamage);
        if (!damageApplied) {
            return;
        }

        HitEffectDirector::SpawnDamageEventHit(
            event.target,
            event.attacker,
            event.knockbackVelocity,
            damageType);

        if (BaseEnemy* enemyTarget = dynamic_cast<BaseEnemy*>(event.target)) {
            enemyTarget->PlayDamageReaction(event.attacker, event.knockbackVelocity, event.damageAmount);
        }

        if (event.statusEffect.IsValid()) {
            ApplyStatusEffect(event.target, event.statusEffect,
                event.attacker && event.attacker->param_.has_value()
                    ? (std::max)(0.0f, event.attacker->param_->attackPower)
                    : 1.0f);
        }

        if (isElectricPlayerHit) {
            playerTarget->StartElectricShockFeedback(0.78f, 1.0f);
        }
        else if (playerTarget) {
            const bool hasKnockback =
                std::abs(event.knockbackVelocity.x) > 0.001f ||
                std::abs(event.knockbackVelocity.y) > 0.001f ||
                std::abs(event.knockbackVelocity.z) > 0.001f;
            if (hasKnockback || damageType != DamageType::Physical) {
                Vector3 reactionDirection = event.knockbackVelocity;
                reactionDirection.y = 0.0f;
                if (Math::Length(reactionDirection) <= 0.001f && event.attacker) {
                    reactionDirection = event.target->GetWorldPosition() - event.attacker->GetWorldPosition();
                    reactionDirection.y = 0.0f;
                }
                playerTarget->StartDamageFeedback(
                    reactionDirection,
                    (std::max)(0.0f, event.invincibilityDuration));
            }
        }

        // ノックバック（吹き飛ばし）があれば適用
        if (!isElectricPlayerHit &&
            (std::abs(event.knockbackVelocity.x) > 0.001f ||
            std::abs(event.knockbackVelocity.y) > 0.001f ||
            std::abs(event.knockbackVelocity.z) > 0.001f))
        {
            if (Character* character = dynamic_cast<Character*>(event.target)) {
                character->SetVelocity(event.knockbackVelocity);
            }
        }
    });

    // プレイヤー死亡時の処理
    // ========================================================
    EventManager::GetInstance()->Subscribe([this](const PlayerDeathEvent& event) {
        GameDataManager::GetInstance()->SubtractLife();
        int lives = GameDataManager::GetInstance()->GetLives();

        DebugConsole::GetInstance()->AddLog("Player Died! Remaining Lives: " + std::to_string(lives));
        return;

        if (lives <= 0) {
            // 残機ゼロならゲームオーバー
            SceneManager::GetInstance()->ChangeScene("GAMEOVER");
        } else {
            // 残機があるならステージをリロードして再開
            SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
        }
    });
}

void GameRule::Update(float deltaTime) {
    if (!scene_ || deltaTime <= 0.0f) {
        return;
    }

    for (auto it = activeStatusEffects_.begin(); it != activeStatusEffects_.end();) {
        ActiveStatusEffect& status = *it;
        if (!IsStatusTargetAlive(status.target)) {
            it = activeStatusEffects_.erase(it);
            continue;
        }

        status.remainingTime -= deltaTime;
        status.tickTimer -= deltaTime;
        status.visualTimer -= deltaTime;

        if (status.visualTimer <= 0.0f && !status.vfxPreset.empty()) {
            EmitStatusVisual(status.target, status.vfxPreset);
            status.visualTimer += 0.12f;
        }

        int processedTicks = 0;
        while (status.tickTimer <= 0.0f && status.remainingTime > 0.0f && processedTicks < 4) {
            ApplyDamage(status.target, status.tickDamage);
            status.tickTimer += status.tickInterval;
            ++processedTicks;
        }

        if (status.remainingTime <= 0.0f || !IsStatusTargetAlive(status.target)) {
            it = activeStatusEffects_.erase(it);
        }
        else {
            ++it;
        }
    }
}

void GameRule::ApplyStatusEffect(Object3d* target, const StatusEffectApplication& application, float damageScale) {
    if (!target || !application.IsValid()) {
        return;
    }

    const float interval = (std::max)(0.05f, application.tickInterval);
    const float tickDamage = (std::max)(0.0f, application.tickDamage * damageScale);
    const auto existing = std::find_if(activeStatusEffects_.begin(), activeStatusEffects_.end(),
        [target, &application](const ActiveStatusEffect& status) {
            return status.target == target && status.type == application.type;
        });

    if (existing != activeStatusEffects_.end()) {
        existing->remainingTime = (std::max)(existing->remainingTime, application.duration);
        existing->tickInterval = interval;
        existing->tickTimer = (std::min)(existing->tickTimer, interval);
        existing->tickDamage = (std::max)(existing->tickDamage, tickDamage);
        if (!application.vfxPreset.empty()) {
            existing->vfxPreset = application.vfxPreset;
        }
        return;
    }

    ActiveStatusEffect status;
    status.target = target;
    status.type = application.type;
    status.remainingTime = application.duration;
    status.tickTimer = interval;
    status.tickInterval = interval;
    status.tickDamage = tickDamage;
    status.visualTimer = 0.0f;
    status.vfxPreset = application.vfxPreset;
    activeStatusEffects_.push_back(std::move(status));
}

bool GameRule::IsStatusTargetAlive(Object3d* target) const {
    if (!target || !scene_) {
        return false;
    }
    if (target != scene_->GetPlayer() && !scene_->IsAlive(target)) {
        return false;
    }
    if (target->isDead || !target->GetIsVisible()) {
        return false;
    }
    return !target->param_.has_value() || target->param_->hp > 0.0f;
}

void GameRule::EmitStatusVisual(Object3d* target, const std::string& presetName) const {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!target || !particles || !particles->IsInitialized()) {
        return;
    }

    const AABB aabb = target->GetAABB();
    Vector3 position = target->GetWorldPosition();
    const float height = (std::max)(0.25f, aabb.max.y - aabb.min.y);
    position.y = aabb.min.y + height * 0.48f;
    particles->EmitDirected(presetName, position, { 0.0f, 1.0f, 0.0f }, 1.0f);
}

// ▼ 汎用ダメージ関数 
bool GameRule::ApplyDamage(Object3d* target, float damage) {
    if (!target) {
        return false;
    }
    if (target->GetClassName() == "Player") {
        Player* player = static_cast<Player*>(target);
        // 被弾無敵または回避ダッシュ中ならダメージを無効化
        if (player->IsInvincible() || player->IsCinematicLocked()) {
            return false;
        }
    }
    if (target->param_.has_value()) {
        target->param_->maxHp = (std::max)(target->param_->maxHp, 1.0f);
        target->param_->hp = (std::max)(target->param_->hp, 0.0f);
        if (target->param_->hp > target->param_->maxHp) {
            target->param_->maxHp = target->param_->hp;
        }
        target->param_->hp -= damage;
        target->param_->hp = std::clamp(target->param_->hp, 0.0f, target->param_->maxHp);

        DebugConsole::GetInstance()->AddLog(target->GetName() + " に " + std::to_string(damage) + " のダメージ！ 残りHP: " + std::to_string(target->param_->hp));

        if (target->param_->hp <= 0.0f) {
            DebugConsole::GetInstance()->AddLog(target->GetName() + " を撃破！");
         /*   if (scene_) {
                scene_->RequestRemoveObject(target);
            }*/
        }
        return true;
    }
    return false;
}
