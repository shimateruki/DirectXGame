#include "GameRule.h"
#include "EventManager.h"
#include "DebugConsole.h"
#include "BaseScene.h"
#include "game/system/SceneGameplayAccess.h"
#include "Event.h"
#include "Player.h"
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/system/IStageProgressScene.h"
#include "GameDataManager.h"
#include "SceneManager.h"
#include "GPUParticleManager.h"
#include "VFXSequencer.h"
#include "HitEffectDirector.h"
#include "BaseEnemy.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "Math.h"
#include <game/actor/player/state/PlayerState.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kCheckpointLoweredModel = "Gimmicks/checkpoint_flag";
constexpr const char* kCheckpointRaisedModel = "Gimmicks/checkpoint_beacon_active";
constexpr float kCheckpointFlagRaiseDelay = 0.14f;

DamageType ResolveDamageType(const DamageEvent& event) {
    // 旧攻撃が属性を省略していても、敵の種類に合う耐性・状態異常判定へ渡す。
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
} // namespace

// 旗の取得状態と演出時計をまとめ、GameRule はイベントの振り分けだけを担当します。
class CheckpointRule {
  public:
    void Initialize(BaseScene* scene) {
        scene_ = scene;
        activeCheckpoint_ = nullptr;
        raisingCheckpoint_ = nullptr;
        checkpointBaseEmissive_ = 1.0f;
        checkpointPresentationTimer_ = 0.0f;
        checkpointParticleTimer_ = 0.0f;
        checkpointAmbientRingTimer_ = 0.0f;
        checkpointRaiseDelay_ = 0.0f;
    }
    void Activate(Player* player, Object3d* checkpoint);
    void Restore(const std::string& checkpointObjectName);
    void Update(float deltaTime);

  private:
    BaseScene* scene_ = nullptr;
    Object3d* activeCheckpoint_ = nullptr;
    Object3d* raisingCheckpoint_ = nullptr;
    float checkpointBaseEmissive_ = 1.0f;
    float checkpointPresentationTimer_ = 0.0f;
    float checkpointParticleTimer_ = 0.0f;
    float checkpointAmbientRingTimer_ = 0.0f;
    float checkpointRaiseDelay_ = 0.0f;
};

// チェックポイント、属性効果、ダメージ適用のゲーム規則を管理します。見た目の通知より先に有効性を判定します。

GameRule::GameRule() = default;

GameRule::~GameRule() {
    for (auto& id : eventSubscriptions_) {
        EventManager::GetInstance()->Unsubscribe(id);
        id = EventManager::kInvalidSubscriptionId;
    }
}

void GameRule::Initialize(BaseScene* scene) {
    for (auto& id : eventSubscriptions_) {
        EventManager::GetInstance()->Unsubscribe(id);
        id = EventManager::kInvalidSubscriptionId;
    }
    scene_ = scene;
    activeStatusEffects_.clear();
    if (!checkpointRule_) checkpointRule_ = std::make_unique<CheckpointRule>();
    checkpointRule_->Initialize(scene);

    // 接触イベントでは対象の EventType を優先し、汎用 TargetID は個別処理の後に使う。
    eventSubscriptions_[0] = EventManager::GetInstance()->Subscribe([this](const PlayerHitEvent& event) {
        Object3d* whoHit = event.me;           // プレイヤー
        Object3d* objectHit = event.hitObject; // 当たった物

        if (!whoHit || !objectHit)
            return;

        EventType type = objectHit->GetEventType();

        switch (type) {
        case EventType::Warp: {
            int targetID = objectHit->GetTargetID();
            if (scene_ && targetID != -1) {
                Object3d* exitPoint = scene_->FindObjectByEventID(targetID);
                if (exitPoint) {
                    Vector3 destPos = exitPoint->GetTransform()->translate;
                    whoHit->SetTranslate(destPos);
                    DebugConsole::GetInstance()->AddLog("Warped to ID: " + std::to_string(targetID));
                }
            }
        } break;

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

        case EventType::Checkpoint: {
            if (Player* player = dynamic_cast<Player*>(whoHit)) {
                checkpointRule_->Activate(player, objectHit);
            }
        } break;

        case EventType::Goal: {
            if (auto* progress = dynamic_cast<IStageProgressScene*>(scene_)) {
                if (!progress->TryStartGoalFromContact(whoHit, objectHit)) break;
            }
        } break;

        case EventType::StarCoin: {
            int coinIdx = objectHit->GetTargetID();

            if (auto* progress = dynamic_cast<IStageProgressScene*>(scene_)) {
                progress->CollectStarCoinFromContact(coinIdx, objectHit->GetWorldPosition());
                objectHit->StartCollectionAnimation();
                GPUParticleManager::GetInstance()->Emit("star_sparkleGet", objectHit->GetWorldPosition());
            }
        } break;

        case EventType::None:
        default:
            // 敵のTargetIDは接触時の汎用イベントではなく、撃破完了通知として使います。
            // プレイヤーの攻撃が当たっただけでボス部屋の完了イベントを発火させません。
            if (dynamic_cast<BaseEnemy*>(objectHit)) {
                break;
            }
            // マグマ連動床のTarget IDは駆動元の参照であり、接触スイッチではない。
            // 着地で噴出口を反転すると、乗った瞬間に上昇が停止してしまう。
            if (objectHit->GetGimmickType() == "MovingFloor" && objectHit->param_ &&
                objectHit->param_->actionMode == 5) {
                break;
            }
            int tID = objectHit->GetTargetID();
            if (tID != -1 && scene_) {
                scene_->TriggerEvent(tID);
            }
            break;
        }

        // 跳ね返りは TargetID のイベントとは独立した接触法線で判定する。
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

    // HP 減算が成立したときだけ、打撃演出・状態異常・ノックバックへ進む。
    eventSubscriptions_[1] = EventManager::GetInstance()->Subscribe([this](const DamageEvent& event) {
        if (!event.target)
            return;
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

        // 即死指定は通常倍率の計算後に適用し、攻撃力0でもHPを確実に0へ落とします。
        if (event.lethalPlayerDamage && playerTarget)
            finalDamage = (std::max)(0.0f, playerTarget->GetHp());

        const bool damageApplied = ApplyDamage(event.target, finalDamage, event.bypassPlayerInvincibility);
        if (!damageApplied) {
            return;
        }

        HitEffectDirector::SpawnDamageEventHit(event.target, event.attacker, event.knockbackVelocity, damageType);

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
        } else if (playerTarget) {
            const bool hasKnockback = std::abs(event.knockbackVelocity.x) > 0.001f ||
                                      std::abs(event.knockbackVelocity.y) > 0.001f ||
                                      std::abs(event.knockbackVelocity.z) > 0.001f;
            if (hasKnockback || damageType != DamageType::Physical) {
                Vector3 reactionDirection = event.knockbackVelocity;
                reactionDirection.y = 0.0f;
                if (Math::Length(reactionDirection) <= 0.001f && event.attacker) {
                    reactionDirection = event.target->GetWorldPosition() - event.attacker->GetWorldPosition();
                    reactionDirection.y = 0.0f;
                }
                playerTarget->StartDamageFeedback(reactionDirection, (std::max)(0.0f, event.invincibilityDuration));
            }
        }

        // ノックバック（吹き飛ばし）があれば適用
        if (!isElectricPlayerHit &&
            (std::abs(event.knockbackVelocity.x) > 0.001f || std::abs(event.knockbackVelocity.y) > 0.001f ||
             std::abs(event.knockbackVelocity.z) > 0.001f)) {
            if (Character* character = dynamic_cast<Character*>(event.target)) {
                character->SetVelocity(event.knockbackVelocity);
            }
        }
    });

    // プレイヤー死亡時の処理
    // ========================================================
    eventSubscriptions_[2] = EventManager::GetInstance()->Subscribe([this](const PlayerDeathEvent& event) {
        // 隔離プレビューでHPを0にしても、実セーブの残機は消費しません。
        if (!event.player || event.player->IsEditorInternal())
            return;
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

    checkpointRule_->Update(deltaTime);

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

        // 大きな deltaTime でも、一度の更新で適用する継続ダメージを最大4回に制限する。
        int processedTicks = 0;
        while (status.tickTimer <= 0.0f && status.remainingTime > 0.0f && processedTicks < 4) {
            ApplyDamage(status.target, status.tickDamage);
            status.tickTimer += status.tickInterval;
            ++processedTicks;
        }

        if (status.remainingTime <= 0.0f || !IsStatusTargetAlive(status.target)) {
            it = activeStatusEffects_.erase(it);
        } else {
            ++it;
        }
    }
}

void CheckpointRule::Activate(Player* player, Object3d* checkpoint) {
    if (!player || !checkpoint) {
        return;
    }

    if (activeCheckpoint_ == checkpoint) {
        return;
    }

    player->ActivateCheckpoint(checkpoint->GetWorldPosition() + Vector3{0.0f, 0.35f, 0.0f}, checkpoint->GetName());

    if (activeCheckpoint_ && scene_ && scene_->IsAlive(activeCheckpoint_)) {
        activeCheckpoint_->SetEmissive(checkpointBaseEmissive_);
    }

    activeCheckpoint_ = checkpoint;
    checkpointBaseEmissive_ = checkpoint->GetEmissive();
    checkpointPresentationTimer_ = 0.0f;

    if (checkpoint->GetModelName() == kCheckpointLoweredModel) {
        raisingCheckpoint_ = checkpoint;
        checkpointRaiseDelay_ = kCheckpointFlagRaiseDelay;
    }
    // 素材色を残し、到達した瞬間だけ控えめに明るくします。
    checkpoint->SetEmissive((std::max)(0.28f, checkpointBaseEmissive_));
    VFXSequencer::PlayOneShot("checkpoint_activate_cue", checkpoint->GetWorldPosition(), checkpoint->GetScale(),
                              checkpoint->GetRotation());
    DebugConsole::GetInstance()->AddLog("Checkpoint activated. Respawn and copy memory updated.");
}

void GameRule::RestoreCheckpointPresentation(const std::string& checkpointObjectName) {
    if (checkpointRule_) checkpointRule_->Restore(checkpointObjectName);
}

void CheckpointRule::Restore(const std::string& checkpointObjectName) {
    if (!scene_ || checkpointObjectName.empty()) return;

    for (const auto& owned : scene_->GetObjects()) {
        Object3d* checkpoint = owned.get();
        if (!checkpoint || checkpoint->GetName() != checkpointObjectName) continue;

        const std::string modelName = checkpoint->GetModelName();
        if (modelName != kCheckpointLoweredModel && modelName != kCheckpointRaisedModel) return;

        // SetModelによるコライダー自動調整を戻し、取得済みの待機モーションから再開する。
        const Object3d::ColliderConfig collider = checkpoint->GetColliderConfig();
        if (modelName == kCheckpointLoweredModel) checkpoint->SetModel(kCheckpointRaisedModel);
        checkpoint->RestoreAnimationPlayback("Idle", 0.0f, true);
        checkpoint->SetColliderConfig(collider);

        activeCheckpoint_ = checkpoint;
        raisingCheckpoint_ = nullptr;
        checkpointRaiseDelay_ = 0.0f;
        checkpointBaseEmissive_ = checkpoint->GetEmissive();
        checkpointPresentationTimer_ = kCheckpointFlagRaiseDelay + 0.60f;
        checkpoint->SetEmissive((std::max)(0.28f, checkpointBaseEmissive_));
        return;
    }
}

void CheckpointRule::Update(float deltaTime) {
    // 未到達の旗も案内として光らせます。有限寿命の演出を間隔を空けて再生します。
    checkpointParticleTimer_ -= deltaTime;
    checkpointAmbientRingTimer_ -= deltaTime;
    if (scene_ && (checkpointParticleTimer_ <= 0.0f || checkpointAmbientRingTimer_ <= 0.0f)) {
        const bool emitMotes = checkpointParticleTimer_ <= 0.0f;
        const bool emitRing = checkpointAmbientRingTimer_ <= 0.0f;
        for (const auto& owned : scene_->GetObjects()) {
            Object3d* checkpoint = owned.get();
            if (!checkpoint || !checkpoint->GetIsVisible()) {
                continue;
            }
            const auto& modelName = checkpoint->GetModelName();
            if (modelName != kCheckpointLoweredModel && modelName != kCheckpointRaisedModel &&
                modelName != "Gimmicks/checkpoint_flag_active") {
                continue;
            }
            if (emitMotes) {
                if (auto* particles = GPUParticleManager::GetInstance(); particles && particles->IsInitialized()) {
                    particles->Emit("checkpoint_active_motes",
                                    Math::Transform(Vector3{0.0f, 0.55f, 0.0f}, checkpoint->GetWorldMatrix()));
                }
            }
            if (emitRing) {
                VFXSequencer::PlayOneShot("checkpoint_idle_cue", checkpoint->GetWorldPosition(), checkpoint->GetScale(),
                                          checkpoint->GetRotation());
            }
        }
        if (emitMotes)
            checkpointParticleTimer_ = 0.45f;
        if (emitRing)
            checkpointAmbientRingTimer_ = 1.35f;
    }

    if (raisingCheckpoint_) {
        if (!scene_ || !scene_->IsAlive(raisingCheckpoint_)) {
            raisingCheckpoint_ = nullptr;
        } else {
            checkpointRaiseDelay_ -= deltaTime;
            if (checkpointRaiseDelay_ <= 0.0f) {
                // SetModelはコライダーを自動調整するため、起動前の当たり判定を復元します。
                const Object3d::ColliderConfig collider = raisingCheckpoint_->GetColliderConfig();
                raisingCheckpoint_->SetModel(kCheckpointRaisedModel);
                raisingCheckpoint_->RestoreAnimationPlayback("Activate", 0.0f, false);
                raisingCheckpoint_->SetColliderConfig(collider);
                raisingCheckpoint_ = nullptr;
            }
        }
    }

    if (!activeCheckpoint_) {
        return;
    }
    if (!scene_ || !scene_->IsAlive(activeCheckpoint_)) {
        activeCheckpoint_ = nullptr;
        return;
    }

    const float previousTime = checkpointPresentationTimer_;
    checkpointPresentationTimer_ += deltaTime;
    if (previousTime < kCheckpointFlagRaiseDelay + 0.60f &&
        checkpointPresentationTimer_ >= kCheckpointFlagRaiseDelay + 0.60f &&
        activeCheckpoint_->GetModelName() == kCheckpointRaisedModel) {
        activeCheckpoint_->RestoreAnimationPlayback("Idle", 0.0f, true);
    }
    const float emissivePulse = 0.12f + std::sin(checkpointPresentationTimer_ * 2.0f) * 0.025f;
    activeCheckpoint_->SetEmissive((std::max)(checkpointBaseEmissive_, emissivePulse));
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
        // 同種の状態異常は重複させず、残り時間と威力を弱めないよう更新する。
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
    // シーンから消えた対象へ継続ダメージやパーティクルを送らない。
    if (!target || !scene_) {
        return false;
    }
    if (target != GetScenePlayer(scene_) && !scene_->IsAlive(target)) {
        return false;
    }
    if (target->isDead || !target->GetIsVisible()) {
        return false;
    }
    return !target->param_.has_value() || target->param_->hp > 0.0f;
}

void GameRule::EmitStatusVisual(Object3d* target, const std::string& presetName) const {
    // 身体の高さから中心を求め、サイズの異なる敵でも状態異常が体に重なるようにする。
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!target || !particles || !particles->IsInitialized()) {
        return;
    }

    const AABB aabb = target->GetAABB();
    Vector3 position = target->GetWorldPosition();
    const float height = (std::max)(0.25f, aabb.max.y - aabb.min.y);
    position.y = aabb.min.y + height * 0.48f;
    particles->EmitDirected(presetName, position, {0.0f, 1.0f, 0.0f}, 1.0f);
}

// ボス固有の軽減とプレイヤーの無敵判定を通した後、共通 HP を更新します。
bool GameRule::ApplyDamage(Object3d* target, float damage, bool bypassPlayerInvincibility) {
    if (!target) {
        return false;
    }
    if (auto* king = dynamic_cast<EnemyFalseKingSlime*>(target)) {
        damage = king->FilterFinaleDamage(damage);
        if (damage <= 0.0f)
            return false;
    }
    if (target->GetClassName() == "Player") {
        Player* player = static_cast<Player*>(target);
        // 被弾無敵または回避ダッシュ中ならダメージを無効化
        if ((player->IsInvincible() && !bypassPlayerInvincibility) || player->IsCinematicLocked()) {
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

        DebugConsole::GetInstance()->AddLog(target->GetName() + " に " + std::to_string(damage) +
                                            " のダメージ！ 残りHP: " + std::to_string(target->param_->hp));

        if (target->param_->hp <= 0.0f) {
            DebugConsole::GetInstance()->AddLog(target->GetName() + " を撃破！");
        }
        return true;
    }
    return false;
}
