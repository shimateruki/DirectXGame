#pragma once
#include "BaseGimmick.h"

/// <summary>トリガー後に一定時間だけ出現し、消滅前に点滅する床です。</summary>
// 関連：BaseGimmickの配置・衝突基盤を使い、固有の状態と表示をこのクラスで更新する。調整項目はInitializeでProperty登録する。
// 主な入口：Initialize / Update / OnTrigger
class GimmickAppearingFloor : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "AppearingFloor";
    static constexpr const char* kModelPath = "Stages/block";

    virtual ~GimmickAppearingFloor() = default;

    // 表示と当たり判定を同時に切り替える床として登録する。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    // 再起動時は表示時間を延長し、床を再び有効にする。
    void OnTrigger() override;

    // 新しい実体を初期化してから、出現時間と配置を引き継ぐ。
    std::unique_ptr<engine::Object3D> Clone() const override;

  private:
    // 表示だけでなく衝突判定も同時に切り替えます。
    void SetFloorActive(bool active);

    float visibleTimer_ = 0.0f;
    float appearDuration_ = 3.0f;
    float blinkWarningTime_ = 1.0f;
    float blinkInterval_ = 0.12f;
    uint32_t originalCollisionAttribute_ = 0;
    uint32_t originalCollisionMask_ = 0;
    bool initializedForPlay_ = false;
    bool isActive_ = true;
};
