#pragma once

#include "engine/utility/math/Math.h"
#include <cstdint>

class Player;

// クリア後にステージセレクトへ戻った時、ゲートからプレイヤーを飛び出させる専用アニメーション。
class PlayerGateReturnAnimation {
public:
    struct Route {
        Vector3 start{};
        Vector3 gateCenter{};
        Vector3 end{};
        Vector3 direction{ 0.0f, 0.0f, 1.0f };
        Vector3 baseScale{ 2.0f, 2.0f, 2.0f };
    };

    void Start(Player* player, const Route& route);
    void Update(Player* player, float deltaTime);
    void Stop(Player* player, bool restoreControl);

    bool IsActive() const { return active_; }
    bool IsFinished() const { return finished_; }
    const Vector3& GetEndPosition() const { return route_.end; }

private:
    Vector3 NormalizeDirection(const Vector3& direction) const;
    Vector3 Lerp(const Vector3& a, const Vector3& b, float t) const;
    Vector3 Bezier(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) const;
    float Clamp01(float value) const;
    float EaseOutCubic(float value) const;
    float EaseInOutCubic(float value) const;
    void ApplyPose(Player* player, float normalizedTime);
    void ApplyFinishedPose(Player* player, bool restoreControl);
    void SuspendCollision(Player* player);
    void RestoreCollision(Player* player);

    Route route_;
    float timer_ = 0.0f;
    bool active_ = false;
    bool finished_ = false;
    bool savedControlActive_ = true;
    uint32_t savedCollisionAttribute_ = 0;
    uint32_t savedCollisionMask_ = 0;
    bool collisionSuspended_ = false;
};
