#pragma once

class Player;

class IAnimationState {
public:
    virtual ~IAnimationState() = default;

    // その状態に入った瞬間に呼ばれる (例: アニメーション再生開始)
    virtual void Enter(Player* player) = 0;

    // その状態の間、毎フレーム呼ばれる (例: 移動入力の監視、遷移判定)
    virtual void Update(Player* player) = 0;

    // その状態から出る瞬間に呼ばれる (例: エフェクト停止)
    virtual void Exit(Player* player) = 0;
};