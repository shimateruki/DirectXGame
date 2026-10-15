#pragma once

#include "BaseGimmick.h"

// プレイヤーの進入を検知し、中ボス・封鎖壁・撃破後の開放を一つの状態で管理します。
class GimmickArenaEncounter : public BaseGimmick {
  public:
    /// ボス、封鎖壁、報酬との紐付けを初期化する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 進入、封鎖、戦闘、撃破後の状態を進める。
    void Update(float deltaTime) override;
    /// 遭遇装置のモデルを描画する。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// プレイヤーの進入を検知して戦闘開始を判断する。
    bool OnCollision(engine::Object3d* other) override;
    /// 外部トリガーから遭遇開始を要求する。
    void OnTrigger() override;

    /// 配置用の遭遇装置複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;

    // 保存済みチェックポイントから再開する際、ボス戦を再開せず障壁と報酬をクリア状態へ戻します。
    void RestoreCheckpointClear();

  private:
    enum class State {
        Waiting,
        Sealing,
        Fighting,
        Cleared,
    };

    /// Play 開始時のボスと障壁を待機状態へ設定する。
    void InitializeForPlay();
    /// Editor に戻るとき配置と表示を復元する。
    void ResetForEditor();
    /// ボスを起動して戦闘中へ移る。
    void BeginEncounter();
    /// ボス撃破後に障壁を解除し報酬を有効にする。
    void CompleteEncounter();
    /// 紐付けたボスが撃破済みか返す。
    bool IsBossDefeated() const;
    /// ボス、障壁、報酬の実体を個別に有効化する。
    void SetBossActive(bool active);
    void SetBarriersActive(bool active);
    void SetRewardActive(bool active);
    /// ボス出現のカメラ演出を開始する。
    void StartBossPresentation();
    float GetBossRevealDelay() const;
    int GetBarrierCount() const;
    bool IsHighCrownMode() const;
    bool IsMagmaMode() const;

    State state_ = State::Waiting;
    float stateTimer_ = 0.0f;
    bool initializedForPlay_ = false;
};
