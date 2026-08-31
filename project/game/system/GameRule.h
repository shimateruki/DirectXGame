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
    // 監視対象のシーンを登録し、ルール処理を開始する
    void Initialize(BaseScene* scene);
    // 炎上など時間を持つ状態異常を更新する
    void Update(float deltaTime);

private:
    // DamageEvent から HP 減算や撃破判定を行う
    bool ApplyDamage(Object3d* target, float damage);
    void ApplyStatusEffect(Object3d* target, const StatusEffectApplication& application, float damageScale);
    bool IsStatusTargetAlive(Object3d* target) const;
    void EmitStatusVisual(Object3d* target, const std::string& presetName) const;
    void ActivateCheckpoint(Player* player, Object3d* checkpoint);
    void UpdateCheckpointPresentation(float deltaTime);

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
    float checkpointRaiseDelay_ = 0.0f;
};
