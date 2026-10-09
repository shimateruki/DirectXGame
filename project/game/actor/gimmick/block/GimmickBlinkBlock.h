#pragma once
#include "BaseGimmick.h"
#include "EventManager.h"

/// <summary>プレイヤーのジャンプに合わせて出現状態が切り替わるブロックです。</summary>
class GimmickBlinkBlock : public BaseGimmick {
  public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;

    std::unique_ptr<Object3d> Clone() const override;

  private:
    // ジャンプ通知を受けて、ブロックの表示と衝突を切り替えます。
    void OnPlayerJump(const PlayerJumpEvent& event);
    void UpdateAppearance();

    bool isActive_ = true;
    int colorType_ = 0; // 0: Blue, 1: Red
};
