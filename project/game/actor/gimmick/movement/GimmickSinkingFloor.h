#pragma once
#include "BaseGimmick.h"

/// <summary>接触後に沈み、一定の段階を経て戻る床を管理します。</summary>
class GimmickSinkingFloor : public BaseGimmick {
  public:
    // 表示名とは分け、既存シーンの生成IDを維持します。
    static constexpr const char* kTypeId = "SinkingFloor";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickSinkingFloor() = default;

    // プレイヤーが乗れる床として衝突属性を設定する。
    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // actionMode で単純な踏み込みと、段階的な水没演出を切り替える。
    void Update(float deltaTime) override;

    // プレイヤーが上面を踏んだ短い間だけ、目標の沈み位置を維持する。
    bool OnCollision(engine::Object3d* other) override;

    // 移動中の位相は複製せず、保存された配置と設定だけ引き継ぐ。
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
