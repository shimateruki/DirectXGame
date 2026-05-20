#pragma once
#include "engine/utility/math/Math.h"

// 前方宣言
class InputManager;
class Camera;
struct Transform; 
class Player; 

/// <summary>
/// プレイヤーの移動戦略（アルゴリズム）を定義するインターフェース
/// </summary>
class IMoveStrategy {
public:
    virtual ~IMoveStrategy() = default;

    /// <summary>
    /// 入力と状態に基づき、このフレームの移動ベクトルを計算する
    /// </summary>
    /// <param name="Characters/player">操作対象のプレイヤー</param>
    /// <returns>移動速度ベクトル (X, Z)</returns>
    virtual Vector3 CalculateVelocity(Player* player) = 0; // 純粋仮想関数
};