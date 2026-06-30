#pragma once

#include "engine/utility/math/Math.h"
#include <vector>

class Player;

// プレイヤー死亡時の見た目だけを担当するアニメーション制御です。
// 状態遷移、残機表示、暗転は PlayerStateDead 側に残し、ここでは
// 「ヒット停止 -> ふくらみ -> 破裂 -> 飛沫」の流れに集中します。
class PlayerDeathAnimation {
public:
    void Start(Player* player, bool finalDeath);
    void Update(Player* player, float deltaTime);
    void RestoreVisual(Player* player);

    bool IsReadyForFade() const;
    bool IsFinished() const;

private:
    void ApplyPreBurstPose(Player* player);
    void EmitBurst(Player* player);
    void EmitPuddle(Player* player);
    void HidePlayerBody(Player* player);

    float timer_ = 0.0f;
    float puddleEmitTimer_ = 0.0f;
    bool active_ = false;
    bool finalDeath_ = false;
    bool burstEmitted_ = false;
    bool baseVisible_ = true;
    bool collisionStored_ = false;
    uint32_t baseCollisionAttribute_ = 0;
    uint32_t baseCollisionMask_ = 0xFFFFFFFF;
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f };
    std::vector<bool> childVisible_;
};
