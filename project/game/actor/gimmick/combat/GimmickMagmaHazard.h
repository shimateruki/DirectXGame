#pragma once

#include "BaseGimmick.h"

/// <summary>マグマ上の継続ダメージと表面の発光を管理します。</summary>
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickMagmaHazard : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "MagmaHazard";
    static constexpr const char* kModelPath = "Stages/block";

    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 配置データがない場合も既定値で危険域を更新できるようにする。
    void Update(float deltaTime) override;

    // 液面接触による死亡はPlayer側で波の高さも含めて判定します。
    bool OnCollision(engine::Object3D* other) override;

    // 複製先を初期化してから危険域の設定と配置を移す。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    // エディター設定から接触ダメージと再適用間隔を解決します。
    float GetDamage() const;

    float GetDamageInterval() const;

    float damageCooldownTimer_ = 0.0f;
    float visualTimer_ = 0.0f; // 見た目の周期に使う経過秒数。
    float baseEmissive_ = 1.8f;
    bool initializedForPlay_ = false;
};
