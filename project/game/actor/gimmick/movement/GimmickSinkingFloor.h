#pragma once
#include "BaseGimmick.h"

/// <summary>接触後に沈み、一定の段階を経て戻る床を管理します。</summary>
class GimmickSinkingFloor : public BaseGimmick {
  public:
    virtual ~GimmickSinkingFloor() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(engine::Object3d* other) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    // 沈下段階ごとの位置と経過時間を進めます。
    void UpdateSubmerging(float deltaTime);
    int submergePhase_ = 0;
    float submergeTimer_ = 0.0f;
    engine::Vector3 startPos_ = {0.0f, 0.0f, 0.0f};
    float contactTimer_ = 0.0f;
    float sinkDepth_ = 2.0f;
    float sinkSpeed_ = 5.5f;
    bool initializedStart_ = false;
};
