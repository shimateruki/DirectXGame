// GameRule.h
#pragma once
#include <memory>
#include "Object3d.h"

// ゲームのルール（当たり判定の結果どうするか）を一元管理するクラス
class GameRule {
public:
    // ルールの初期化
    void Initialize();

private:
    // 汎用的なダメージ処理
    void ApplyDamage(Object3d* target, float damage);


};