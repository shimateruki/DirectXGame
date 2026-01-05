// GameRule.h
#pragma once
#include <memory>
#include "Object3d.h"
class BaseScene;
// ゲームのルール（当たり判定の結果どうするか）を一元管理するクラス
class GameRule {
public:
    // ルールの初期化
    void Initialize(BaseScene* scene);

private:
    // 汎用的なダメージ処理
    void ApplyDamage(Object3d* target, float damage);

    BaseScene* scene_ = nullptr;
};