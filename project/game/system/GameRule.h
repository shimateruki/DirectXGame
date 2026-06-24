// GameRule.h
#pragma once
#include <memory>
#include "Object3d.h"

class BaseScene;

// 衝突イベントからダメージ、ゴール、クリア演出などのゲームルールを処理するクラス
class GameRule {
public:
    // 監視対象のシーンを登録し、ルール処理を開始する
    void Initialize(BaseScene* scene);

private:
    // DamageEvent から HP 減算や撃破判定を行う
    void ApplyDamage(Object3d* target, float damage);

    BaseScene* scene_ = nullptr;
};
