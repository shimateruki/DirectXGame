#pragma once
#include "BaseGimmick.h"
class RidingPlatformController;

/// <summary>移動床の軌道と、乗っているキャラクターへの移動量を管理します。</summary>
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnCollision
class GimmickMovingFloor : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "MovingFloor";
    static constexpr const char* kModelPath = "Stages/block";

    GimmickMovingFloor();

    ~GimmickMovingFloor() override;

    bool IsRidePlatform() const;

    // 複製時は配置と設定だけを引き継ぎ、乗員・周期・原点は再生時に再取得する。
    std::unique_ptr<engine::Object3D> Clone() const override;

    // 床として接地判定できる属性を与え、旧データの速度欠落を補う。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    // 全移動床で更新前に乗員を運ぶ。衝突処理は押し戻しだけにする。
    bool OnCollision(engine::Object3D* other) override;

    // 逃走ギミックで床を再利用するとき、移動周期を初期位置から再開します。
    void ResetEscapeCycle();

  private:
    std::unique_ptr<RidingPlatformController> ride_;
    engine::Vector3 startPosition_{};
    engine::Vector3 startRotation_{};
    engine::Vector3 frameDelta_{};
    float time_ = 0.0f;
    float phaseOffset_ = 0.0f;
    bool hasCapturedStartTransform_ = false;
};
