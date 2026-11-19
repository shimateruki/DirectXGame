#pragma once
#include "engine/utility/math/Math.h"

// 前方宣言
namespace engine { class InputManager; }
namespace engine { class Camera; }
struct Transform;
class Player;

/// <summary>
/// プレイヤーの移動戦略（アルゴリズム）を定義するインターフェース
/// </summary>
// 関連：PlayerMoverが現在の操作方式に合わせて移動速度の計算を呼び出す。
// 主な入口：CalculateVelocity
class IMoveStrategy {
  public:
    virtual ~IMoveStrategy() = default;

    /// <summary>
    /// 入力と状態に基づき、このフレームの移動ベクトルを計算する
    /// </summary>
    /// <param name="player">操作対象のプレイヤー</param>
    /// <returns>水平面での移動速度。横移動用の実装では X 成分だけを使います。</returns>
    virtual engine::Vector3 CalculateVelocity(Player* player) = 0; // 純粋仮想関数
};