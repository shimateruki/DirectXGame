#pragma once
#include "BaseGimmick.h"

/// <summary>接触後に沈み、一定の段階を経て戻る床を管理します。</summary>
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickSinkingFloor : public BaseGimmick {
  public:
    // 表示名とは分け、既存シーンの生成IDを維持します。
    static constexpr const char* kTypeId = "SinkingFloor";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickSinkingFloor() = default;

    // プレイヤーが乗れる床として衝突属性を設定する。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // actionMode で単純な踏み込みと、段階的な水没演出を切り替える。
    void Update(float deltaTime) override;

    // プレイヤーが上面を踏んだ短い間だけ、目標の沈み位置を維持する。
    bool OnCollision(engine::Object3D* other) override;

    // 移動中の位相は複製せず、保存された配置と設定だけ引き継ぐ。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    // 沈下段階ごとの位置と経過時間を進めます。
    void UpdateSubmerging(float deltaTime);

    int submergePhase_ = 0; // 水没モードの段階番号。浅い沈み込みでは使用しない。
    float submergeTimer_ = 0.0f; // 水没床の段階切替に使う経過秒数。
    engine::Vector3 startPos_ = engine::Vector3::ZERO; // 沈み込みと復帰の基準となる配置位置。
    float contactTimer_ = 0.0f; // 床やスイッチの接触保持までの残り秒数。
    float sinkDepth_ = 2.0f; // 浅い沈み込みモードで下げる距離。
    float sinkSpeed_ = 5.5f; // 浅い沈み込みの指数追従係数。移動速度そのものではない。
    bool initializedStart_ = false; // 浅い沈み込みの基準位置を取得済みか。
};
