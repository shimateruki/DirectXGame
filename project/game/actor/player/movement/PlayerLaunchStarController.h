#pragma once
#include "engine/utility/math/Math.h"
#include <cstdint>

class Player;
class PlayerSlimeAnimator;

// スター移動の軌道・時間・衝突復元情報を所有します。
// 通常移動やコピー演出の状態は所有せず、アニメーション更新の可否を呼び出し側から受け取ります。
class PlayerLaunchStarController {
public:
    void Start(Player& player, PlayerSlimeAnimator& animator,
        const Vector3& destination, float arcHeight, float duration);
    void Cancel(Player& player, bool restoreControl);
    void Update(Player& player, PlayerSlimeAnimator& animator, float deltaTime, bool updateAnimation);
    bool IsActive() const { return launchStarActive_; }

private:
    bool launchStarActive_ = false;
    bool launchStarSavedControlActive_ = true;
    uint32_t launchStarSavedCollisionAttribute_ = 0;
    uint32_t launchStarSavedCollisionMask_ = 0;
    Vector3 launchStarStart_ = { 0.0f, 0.0f, 0.0f };
    Vector3 launchStarDestination_ = { 0.0f, 0.0f, 0.0f };
    float launchStarArcHeight_ = 12.0f;
    float launchStarDuration_ = 1.2f;
    float launchStarTimer_ = 0.0f;
    float launchStarTrailTimer_ = 0.0f;
};
