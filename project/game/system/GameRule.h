// GameRule.h
#pragma once
#include <memory>
#include <array>
#include <string>
#include <vector>
#include "Object3d.h"
#include "Event.h"
#include "EventManager.h"

class BaseScene;
class Player;
class CheckpointRule;

// 衝突イベントからダメージ、ゴール、クリア演出などのゲームルールを処理するクラス
class GameRule {
  public:
    GameRule();
    ~GameRule();
    GameRule(const GameRule&) = delete;
    GameRule& operator=(const GameRule&) = delete;
    // 監視対象のシーンを登録し、接触・ダメージ・死亡イベントを購読する。
    // EventManager が購読を保持する間は、この GameRule と scene を存続させる。
    void Initialize(BaseScene* scene);
    // deltaTime は秒。チェックポイント演出と時間経過型の状態異常を更新する。
    void Update(float deltaTime);

    // 死亡後のシーン再生成では取得演出を再生せず、旗を取得済みの姿に戻す。
    void RestoreCheckpointPresentation(const std::string& checkpointObjectName);

  private:
    std::array<EventManager::SubscriptionId, 3> eventSubscriptions_{};
    // DamageEvent から HP 減算や撃破判定を行う
    bool ApplyDamage(Object3d* target, float damage, bool bypassPlayerInvincibility = false);
    void ApplyStatusEffect(Object3d* target, const StatusEffectApplication& application, float damageScale);
    bool IsStatusTargetAlive(Object3d* target) const;
    void EmitStatusVisual(Object3d* target, const std::string& presetName) const;

    // 状態異常の残り秒と継続ダメージ・演出の独立した時計。target はシーン内 Object の借用参照。
    struct ActiveStatusEffect {
        Object3d* target = nullptr;
        StatusEffectType type = StatusEffectType::None;
        float remainingTime = 0.0f;
        float tickTimer = 0.0f;
        float tickInterval = 0.5f;
        float tickDamage = 0.0f;
        float visualTimer = 0.0f;
        std::string vfxPreset;
    };

    BaseScene* scene_ = nullptr;
    std::vector<ActiveStatusEffect> activeStatusEffects_;
    std::unique_ptr<CheckpointRule> checkpointRule_;
};
