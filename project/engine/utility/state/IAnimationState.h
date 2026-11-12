#pragma once

class Player;

// 担当：プレイヤーのアニメーション状態の開始・更新・終了を定義する。
// 関連：Player（状態の所有・切替）、各PlayerState（実装）
class IAnimationState {
public:
    virtual ~IAnimationState() = default;

    // その状態に入った瞬間に呼ばれる (例: アニメーション再生開始)
    virtual void Enter(Player* player) = 0;

    // その状態の間、毎フレーム呼ばれる (例: 移動入力の監視、遷移判定)
    // playerは借用参照、deltaTimeは経過秒数。状態を抜ける後片付けはExitへ分ける。
    virtual void Update(Player* player, float deltaTime) = 0;

    // その状態から出る瞬間に呼ばれる (例: エフェクト停止)
    virtual void Exit(Player* player) = 0;
};
