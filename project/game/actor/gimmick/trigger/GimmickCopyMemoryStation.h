#pragma once

#include "BaseGimmick.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>

class Player;

// ボス前で最大4種類のコピー能力から一つを、実物のコアへ触れて選択する記憶台です。
class GimmickCopyMemoryStation final : public BaseGimmick {
  public:
    /// コアの種類と配置設定を読み込み、表示を準備する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// コアの浮遊表示と接触選択の状態を更新する。
    void Update(float deltaTime) override;
    /// 記憶台と各コアの表示を描画する。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// プレイヤーが触れたコアを判定する。
    bool OnCollision(engine::Object3d* other) override;
    /// 外部トリガーで記憶台を起動する。
    void OnTrigger() override;
    /// スイッチ状態に合わせてコア選択を有効・無効にする。
    void OnSwitchEvent(bool active) override;

    /// 配置用の記憶台複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    static constexpr size_t kCoreCount = 4;

    // 旧Scene JSONのparamを、このギミックが使う設定だけへ写す境界です。
    struct Settings {
        std::array<std::string_view, kCoreCount> types{};
        float activationRadius = 0.9f;
        float essenceScale = 1.0f;
        float hoverAmount = 0.10f;
        float glow = 0.45f;
        bool unlimitedDuration = true;
        bool startActive = true;
    };
    /// Scene JSON の設定を記憶台用の値へ正規化する。
    Settings ReadSettings() const;

    /// Play 中に使用するコアと選択状態を準備する。
    void BeginPlayState();
    /// Editor に戻るときコアの表示を配置状態へ戻す。
    void ResetEditorState();
    /// コアの浮遊位置と発光を更新する。
    void UpdateCoreVisuals(float deltaTime);
    /// 指定コアの周囲エフェクトを近接度へ同期する。
    void UpdateCoreEffects(size_t index, float deltaTime, float approach, float scale);
    /// コアに敵種別と対応する見た目を設定する。
    void ConfigureCore(size_t index, const std::string& enemyType);
    /// 表示するコアが一種類だけか返す。
    bool IsSingleCore() const;
    engine::Vector3 GetCoreLocalPosition(size_t index) const;
    engine::Vector3 GetCoreWorldPosition(size_t index) const;
    std::string GetConfiguredType(size_t index) const;
    /// プレイヤー位置に触れているコア番号を返す。
    int FindTouchedCore(const engine::Vector3& playerPosition) const;
    /// 指定コアのコピー能力をプレイヤーへ付与する。
    void ActivateCore(Player& player, size_t index);

    std::array<std::unique_ptr<engine::Object3d>, kCoreCount> coreVisuals_{};
    std::array<std::string, kCoreCount> displayedTypes_{};
    std::array<float, kCoreCount> corePulseTimers_{};
    std::array<engine::Vector3, kCoreCount> transferTargets_{};
    std::array<std::unique_ptr<engine::Object3d>, kCoreCount * 4> effects_{};
    float visualTimer_ = 0.0f;
    float selectionCooldown_ = 0.0f;
    int latchedCore_ = -1;
    bool initializedForPlay_ = false;
    bool runtimeEnabled_ = true;
    bool collisionObserved_ = false;
};
