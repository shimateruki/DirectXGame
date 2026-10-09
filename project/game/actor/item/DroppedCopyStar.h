#pragma once
#include "game/actor/item/BaseItem.h"

// 記憶台とは異なり、任意解除した能力を一度だけ回収できる一時アイテムです。
class DroppedCopyStar final : public BaseItem {
  public:
    void InitializeDrop(Object3dCommon* common, const std::string& enemyType, bool unlimitedDuration,
                        const Vector3& start, const Vector3& destination);
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
    void CaptureReplayCustomState(json& state) const override;
    void RestoreReplayCustomState(const json& state) override;
    float GetLifetime() const { return lifetime_; }
    float GetBlinkDuration() const { return blinkDuration_; }
    float GetElapsed() const { return elapsed_; }
    void SetLifetime(float seconds) { lifetime_ = seconds; }
    void SetBlinkDuration(float seconds) { blinkDuration_ = seconds; }

  protected:
    void Collect(Player* player) override;

  private:
    std::string enemyType_;
    bool unlimitedDuration_ = false;
    Vector3 start_{};
    Vector3 destination_{};
    float elapsed_ = 0.0f;
    float lifetime_ = 10.0f;
    float blinkDuration_ = 3.0f;
};
