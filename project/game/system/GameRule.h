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
    /// <summary>ゲームルールの初期状態を作る。</summary>
    GameRule();
    /// <summary>購読したイベントを解除し、状態異常を片付ける。</summary>
    ~GameRule();
    GameRule(const GameRule&) = delete;
    GameRule& operator=(const GameRule&) = delete;
    /// <summary>監視対象シーンを借用し、接触・ダメージ・死亡イベントを購読する。</summary>
    /// <remarks>購読中はGameRuleとシーンの両方が有効である必要がある。</remarks>
    void Initialize(BaseScene* scene);
    /// <summary>チェックポイント演出と継続型状態異常を更新する。</summary>
    /// <param name="deltaTime">経過秒数。</param>
    void Update(float deltaTime);

    /// <summary>死亡後のシーン再生成で、旗を取得済みの姿へ戻す。</summary>
    void RestoreCheckpointPresentation(const std::string& checkpointObjectName);

  private:
    std::array<EventManager::SubscriptionId, 3> eventSubscriptions_{};
    /// <summary>指定対象へダメージを適用し、HP減少と撃破を判定する。</summary>
    /// <returns>ダメージを適用できた場合はtrue。</returns>
    bool ApplyDamage(engine::Object3d* target, float damage, bool bypassPlayerInvincibility = false);
    /// <summary>ダメージに伴う状態異常を対象へ付与する。</summary>
    void ApplyStatusEffect(engine::Object3d* target, const StatusEffectApplication& application, float damageScale);
    /// <summary>対象が状態異常を受けられる生存状態か返す。</summary>
    bool IsStatusTargetAlive(engine::Object3d* target) const;
    /// <summary>指定対象の位置へ状態異常の演出を発生させる。</summary>
    void EmitStatusVisual(engine::Object3d* target, const std::string& presetName) const;

    // 状態異常の残り秒と継続ダメージ・演出の独立した時計。target はシーン内 Object の借用参照。
    struct ActiveStatusEffect {
        engine::Object3d* target = nullptr;
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
