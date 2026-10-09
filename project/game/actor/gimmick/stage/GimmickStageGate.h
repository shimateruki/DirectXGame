#pragma once

#include "BaseGimmick.h"

/// <summary>ステージ選択ノードと入場ゲートの表示・遷移を管理します。</summary>
class GimmickStageGate : public BaseGimmick {
  public:
    ~GimmickStageGate() override = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    std::unique_ptr<Object3d> Clone() const override;

    // エディターで設定された行き先情報とノードの動作モードを取得します。
    int GetStageIndex() const;
    int GetGateMode() const;
    const std::string& GetTargetSceneName() const;
    bool IsStageSelectNodeMode() const;
    /// <summary>選択・解放・クリア状態を設定し、次の更新でゲート表示へ反映します。</summary>
    /// <param name="selected">現在選択中かどうか。</param>
    /// <param name="unlocked">入場可能かどうか。</param>
    /// <param name="cleared">クリア済みかどうか。</param>
    /// <param name="unlocking">解放演出中かどうか。</param>
    void SetGateState(bool selected, bool unlocked, bool cleared, bool unlocking = false);

    // 発光の目標値と、ゲート遷移の許可を外部から指定します。
    void SetGateActivation(float activation);
    void SetTransitionEnabled(bool enabled);

    // ゲートに入った際の発光反応を開始します。
    void TriggerEntryReaction();
    bool IsUnlocked() const { return isUnlocked_; }

  private:
    // 元の縮尺を保存し、点滅演出による縮尺の累積を防ぎます。
    void CaptureBaseScale();
    void UpdatePortalMaterial();

    // 選択状態と演出状態を満たした場合にだけシーン遷移します。
    bool CanTriggerTransition() const;
    void TriggerTransition();
    Vector4 GetTargetColor() const;

    Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    float pulseTimer_ = 0.0f;
    float activation_ = 1.0f;
    float targetActivation_ = 1.0f;
    float activationBurst_ = 0.0f;
    float lastAppliedPulse_ = 1.0f;
    bool hasBaseScale_ = false;
    bool hasTriggeredTransition_ = false;
    bool isSelected_ = false;
    bool isUnlocked_ = true;
    bool isCleared_ = false;
    bool isUnlocking_ = false;
};
