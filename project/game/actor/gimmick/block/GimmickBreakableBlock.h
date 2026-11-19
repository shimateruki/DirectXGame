#pragma once
#include "BaseGimmick.h"

class Player;

// ブロックやガラスの破壊条件を判定し、破片と閃光を出します。
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickBreakableBlock : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "BreakableBlock";
    static constexpr const char* kModelPath = "Stages/bomb_break_block";

    // ダッシュパネルによる加速中の体当たり専用モードの保存値です。
    static constexpr int kDashPanelOnly = 8;

    virtual ~GimmickBreakableBlock() = default;

    // 初期化
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 更新処理
    void Update(float deltaTime) override;

    // 衝突判定
    bool OnCollision(engine::Object3D* other) override;

    // プレイヤーの押し戻し前に呼び、今回の衝突で破壊したときだけtrueを返します。
    bool TryBreakByPlayerImpact(const Player* player);

    // 同じモデルで新しい実体を初期化してから、配置と破壊設定を複製する。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    void Break(); // 破壊処理

  private:
    bool isBroken_ = false;
};
