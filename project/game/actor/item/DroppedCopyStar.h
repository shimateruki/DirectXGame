#pragma once
#include "game/actor/item/BaseItem.h"

// 記憶台とは異なり、任意解除した能力を一度だけ回収できる一時アイテムです。
// 関連：コピー元の情報を保持し、Playerの再取得と寿命による消滅を扱う。
// 主な入口：InitializeDrop / Update
class DroppedCopyStar final : public BaseItem {
  public:
    // 投下中の移動を再現できるよう、起点と到達点を生成時に固定する。
    void InitializeDrop(engine::Object3DCommon* common, const std::string& enemyType, bool unlimitedDuration,
                        const engine::Vector3& start, const engine::Vector3& destination);

    // deltaTimeは秒。浮遊・点滅・寿命を進め、再取得可能な表示を更新する。
    void Update(float deltaTime) override;

    // 軌道と経過時間を引き継ぎ、複製直後の位置が巻き戻らないようにする。
    std::unique_ptr<engine::Object3D> Clone() const override;

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
    // 取得済みに切り替えて表示と衝突を止め、二重取得を防ぐ。
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
