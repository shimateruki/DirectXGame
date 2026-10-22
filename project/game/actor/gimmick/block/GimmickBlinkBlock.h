#pragma once
#include "BaseGimmick.h"
#include "game/system/rules/EventManager.h"

/// <summary>プレイヤーのジャンプに合わせて出現状態が切り替わるブロックです。</summary>
class GimmickBlinkBlock : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "BlinkBlock";
    static constexpr const char* kModelPath = "Stages/block";

    ~GimmickBlinkBlock() override;

    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    void Update(float deltaTime) override;

    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    // ジャンプ通知を受けて、ブロックの表示と衝突を切り替えます。
    void OnPlayerJump(const PlayerJumpEvent& event);

    void UpdateAppearance();
    void ResetFlipPair();
    void UpdateFlipPair(float deltaTime);
    void ApplyFlipPairAppearance();
    bool IsFlipPairModel() const;

    bool isActive_ = true;
    int colorType_ = 0; // 0: Blue, 1: Red
    bool flipPairInitialized_ = false;
    bool flipPairWasPlaying_ = false;
    int flipPairInitialSide_ = 0;
    float flipAngle_ = 0.0f;
    float flipFromAngle_ = 0.0f;
    float flipTargetAngle_ = 0.0f;
    float flipElapsed_ = 0.0f;
    float flipDuration_ = 0.48f;
    int flipFrame_ = -1;
    // 左手座標系へ変換済みの端点モデルから求めた、青・赤それぞれの床判定。
    ColliderConfig flipPairColliders_[2]{};
    EventManager::SubscriptionId jumpSubscription_ = EventManager::kInvalidSubscriptionId;
};
