#pragma once
#include "game/actor/item/BaseItem.h"

// 記憶台とは異なり、任意解除した能力を一度だけ回収できる一時アイテムです。
class DroppedCopyStar final : public BaseItem {
  public:
    // 投下中の移動を再現できるよう、起点と到達点を生成時に固定する。
    void InitializeDrop(engine::Object3dCommon* common, const std::string& enemyType, bool unlimitedDuration,
                        const engine::Vector3& start, const engine::Vector3& destination);

    void Update(float deltaTime) override;

    // 軌道と経過時間を引き継ぎ、複製直後の位置が巻き戻らないようにする。
    std::unique_ptr<engine::Object3d> Clone() const override;

    // コピー能力の種類と投下時計を、共通の位置情報に追加して記録する。
    void CaptureReplayCustomState(json& state) const override;

    // 旧記録に項目がない場合は現在値を保持して復元する。
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
    engine::Vector3 start_{};
    engine::Vector3 destination_{};
    float elapsed_ = 0.0f;
    float lifetime_ = 10.0f;
    float blinkDuration_ = 3.0f;
};
