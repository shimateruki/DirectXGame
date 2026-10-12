// GameRule.h
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Object3d.h"
#include "Event.h"

class BaseScene;
class Player;

// 衝突イベントからダメージ、ゴール、クリア演出などのゲームルールを処理するクラス
class GameRule {
  public:
    // 監視対象のシーンを登録し、接触・ダメージ・死亡イベントを購読する。
    // EventManager が購読を保持する間は、この GameRule と scene を存続させる。
    void Initialize(BaseScene* scene);
    // deltaTime は秒。チェックポイント演出と時間経過型の状態異常を更新する。
    void Update(float deltaTime);

    // 死亡後のシーン再生成では取得演出を再生せず、旗を取得済みの姿に戻す。
    void RestoreCheckpointPresentation(const std::string& checkpointObjectName);

  private:
    // DamageEvent から HP 減算や撃破判定を行う
    bool ApplyDamage(Object3d* target, float damage, bool bypassPlayerInvincibility = false);
    void ApplyStatusEffect(Object3d* target, const StatusEffectApplication& application, float damageScale);
    bool IsStatusTargetAlive(Object3d* target) const;
    void EmitStatusVisual(Object3d* target, const std::string& presetName) const;
    void ActivateCheckpoint(Player* player, Object3d* checkpoint);
    void UpdateCheckpointPresentation(float deltaTime);

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
    Object3d* activeCheckpoint_ = nullptr;
    Object3d* raisingCheckpoint_ = nullptr;
    float checkpointBaseEmissive_ = 1.0f;
    float checkpointPresentationTimer_ = 0.0f;
    float checkpointParticleTimer_ = 0.0f;
    float checkpointAmbientRingTimer_ = 0.0f;
    float checkpointRaiseDelay_ = 0.0f;
};
