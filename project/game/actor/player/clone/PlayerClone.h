#pragma once
#include "Character.h"

/// <summary>
/// プレイヤーが投げる分身オブジェクト
/// </summary>
// 関連：Playerから初期位置と投擲速度を受け取り、Characterの移動基盤で寿命まで進む。
// 主な入口：Initialize / Update
class PlayerClone : public Character {
  public:
    // positionは初期位置、velocityは投擲速度。Characterの移動と寿命タイマーを準備する。
    void Initialize(engine::Object3DCommon* common, const engine::Vector3& position, const engine::Vector3& velocity);

    // deltaTimeは秒。物理移動を進めながら寿命を減らす。
    void Update(float deltaTime) override;

    using Character::IsGrounded;

    bool IsExpired() const { return lifetimeTimer_ <= 0.0f; }

    // 寿命を負値へ落とし、呼出側のIsExpired判定で回収させる。ここでは自分自身をdeleteしない。
    void Destroy() { lifetimeTimer_ = -1.0f; }

  private:
    float lifetimeTimer_ = 3.0f; // 3秒で消滅
};
