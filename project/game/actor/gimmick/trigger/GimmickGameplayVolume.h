#pragma once

#include "BaseGimmick.h"
#include "LightManager.h"

class Player;

// 配置した範囲への進入・退出を、イベントやチェックポイントなどのゲーム処理へ変換します。
class GimmickGameplayVolume : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    enum class Mode {
        Event = 0,
        Checkpoint,
        FallDeath,
        FeedbackCue,
        Bgm,
        Environment,
    };

    void InitializeForPlay();
    void ResetForEditor();
    void HandleEnter(Player* player);
    void HandleExit(Player* player);
    void Activate(Player* player);
    void Deactivate();
    bool CanActivate() const;
    Mode GetMode() const;

    bool initializedForPlay_ = false;
    bool runtimeEnabled_ = true;
    bool occupied_ = false;
    bool collisionObserved_ = false;
    bool triggered_ = false;
    float rearmTimer_ = 0.0f;
    Player* occupant_ = nullptr;
    LightManager::EnvironmentProfileState previousEnvironment_{};
    bool hasPreviousEnvironment_ = false;
};
