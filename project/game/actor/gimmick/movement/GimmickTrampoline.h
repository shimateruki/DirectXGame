#pragma once
#include "BaseGimmick.h"

class Player;

// 上から踏むと大ジャンプするジャンプ台ギミック
class GimmickTrampoline : public BaseGimmick {
  public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(engine::Object3d* other) override;
    std::unique_ptr<engine::Object3d> Clone() const override;
    // プレイヤーから見た接触法線を受け取り、上面への着地だけを反発へ変える。
    bool TryBounce(Player* player, const engine::Vector3& playerGroundNormal);

  private:
    // 配置時の Transform を基準に揺らし、拡縮が累積しないようにする。
    engine::Vector3 authoredScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 authoredRotation_ = {};
    float squash_ = 0.0f;
    float squashVelocity_ = 0.0f;
    float wobbleTime_ = 0.0f;
    float bounceCooldown_ = 0.0f;
    bool authoredTransformCaptured_ = false;
};
