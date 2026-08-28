#pragma once

#include "BaseGimmick.h"

// プレイヤーの進入を検知し、中ボス・封鎖壁・撃破後の開放を一つの状態で管理します。
class GimmickArenaEncounter : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    enum class State {
        Waiting,
        Sealing,
        Fighting,
        Cleared,
    };

    void InitializeForPlay();
    void ResetForEditor();
    void BeginEncounter();
    void CompleteEncounter();
    bool IsBossDefeated() const;
    void SetBossActive(bool active);
    void SetBarriersActive(bool active);
    void SetRewardActive(bool active);
    void StartBossPresentation();
    float GetBossRevealDelay() const;
    int GetBarrierCount() const;
    bool IsHighCrownMode() const;
    bool IsMagmaMode() const;

    State state_ = State::Waiting;
    float stateTimer_ = 0.0f;
    bool initializedForPlay_ = false;
};
