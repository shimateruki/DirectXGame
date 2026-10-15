#pragma once
#include "BaseGimmick.h"

/// <summary>フックに引かれた位置へ移動するブロックです。</summary>
class GimmickHookPullBlock : public BaseGimmick {
  public:
    virtual ~GimmickHookPullBlock() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(engine::Object3d* other) override;
    std::unique_ptr<engine::Object3d> Clone() const override;

    /// <summary>フックを持つキャラクターへ向けてブロックの移動を開始します。</summary>
    /// <param name="hookOwnerPos">引き寄せ先となるフック使用者のワールド座標。</param>
    void StartHookPull(const engine::Vector3& hookOwnerPos);

  private:
    engine::Vector3 velocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 pullTarget_ = {0.0f, 0.0f, 0.0f};
    float pullTimer_ = 0.0f;
};
