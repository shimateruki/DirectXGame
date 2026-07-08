#define NOMINMAX
#include "PlayerGateReturnAnimation.h"

#include "Player.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr float kDuration = 2.25f;
constexpr float kLandingPhaseEnd = 0.42f;
constexpr float kFirstBounceEnd = 0.72f;
constexpr float kGateExitNoGravityTime = 0.14f;
constexpr float kGravity = 18.0f;
constexpr float kLaunchHopHeight = 1.05f;
constexpr float kFirstBounceHeight = 0.48f;
constexpr float kSecondBounceHeight = 0.22f;
}

void PlayerGateReturnAnimation::Start(Player* player, const Route& route)
{
    if (player && collisionSuspended_) {
        RestoreCollision(player);
    }

    route_ = route;
    route_.direction = NormalizeDirection(route.direction);
    timer_ = 0.0f;
    active_ = true;
    finished_ = false;
    savedControlActive_ = player ? player->IsControlActive() : true;

    if (!player) {
        return;
    }

    player->SetIsVisible(true);
    player->SetIsControlActive(false);
    player->SetVelocity({ 0.0f, 0.0f, 0.0f });
    SuspendCollision(player);
    player->SetMoveYaw(std::atan2(route_.direction.x, route_.direction.z));
    ApplyPose(player, 0.0f);
}

void PlayerGateReturnAnimation::Update(Player* player, float deltaTime)
{
    if (!active_ || !player) {
        return;
    }

    timer_ += (std::max)(deltaTime, 0.0f);
    const float normalizedTime = Clamp01(timer_ / kDuration);
    ApplyPose(player, normalizedTime);

    if (normalizedTime >= 1.0f) {
        active_ = false;
        finished_ = true;
        ApplyFinishedPose(player, false);
    }
}

void PlayerGateReturnAnimation::Stop(Player* player, bool restoreControl)
{
    active_ = false;
    finished_ = false;
    ApplyFinishedPose(player, restoreControl);
}

Vector3 PlayerGateReturnAnimation::NormalizeDirection(const Vector3& direction) const
{
    Vector3 flat{ direction.x, 0.0f, direction.z };
    const float lengthSq = flat.x * flat.x + flat.z * flat.z;
    if (lengthSq <= 0.000001f) {
        return { 0.0f, 0.0f, 1.0f };
    }

    const float invLength = 1.0f / std::sqrt(lengthSq);
    return { flat.x * invLength, 0.0f, flat.z * invLength };
}

Vector3 PlayerGateReturnAnimation::Lerp(const Vector3& a, const Vector3& b, float t) const
{
    t = Clamp01(t);
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

Vector3 PlayerGateReturnAnimation::Bezier(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) const
{
    t = Clamp01(t);
    const float inv = 1.0f - t;
    const float b0 = inv * inv * inv;
    const float b1 = 3.0f * inv * inv * t;
    const float b2 = 3.0f * inv * t * t;
    const float b3 = t * t * t;
    return {
        p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3,
        p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3,
        p0.z * b0 + p1.z * b1 + p2.z * b2 + p3.z * b3
    };
}

float PlayerGateReturnAnimation::Clamp01(float value) const
{
    return std::clamp(value, 0.0f, 1.0f);
}

float PlayerGateReturnAnimation::EaseOutCubic(float value) const
{
    value = Clamp01(value);
    const float inv = 1.0f - value;
    return 1.0f - inv * inv * inv;
}

float PlayerGateReturnAnimation::EaseInOutCubic(float value) const
{
    value = Clamp01(value);
    if (value < 0.5f) {
        return 4.0f * value * value * value;
    }
    const float f = -2.0f * value + 2.0f;
    return 1.0f - (f * f * f) * 0.5f;
}

void PlayerGateReturnAnimation::ApplyPose(Player* player, float normalizedTime)
{
    if (!player) {
        return;
    }

    Vector3 landing = Lerp(route_.gateCenter, route_.end, 0.38f);
    landing.y = route_.end.y;
    Vector3 firstBounceEnd = Lerp(landing, route_.end, 0.55f);
    firstBounceEnd.y = route_.end.y;

    Vector3 position = route_.start;
    Vector3 scale = route_.baseScale;
    float pitch = 0.0f;
    float roll = 0.0f;

    if (normalizedTime < kLandingPhaseEnd) {
        const float phaseT = Clamp01(normalizedTime / kLandingPhaseEnd);
        const float moveT = EaseOutCubic(phaseT);
        const Vector3 controlA = Lerp(route_.start, route_.gateCenter, 0.46f);
        const Vector3 controlB = Lerp(route_.gateCenter, landing, 0.58f);
        position = Bezier(
            route_.start,
            { controlA.x, controlA.y + 0.30f, controlA.z },
            { controlB.x, controlB.y + 0.55f, controlB.z },
            landing,
            moveT
        );
        const float groundY = route_.end.y;
        const float fallStartY = (std::max)({ route_.start.y, route_.gateCenter.y, groundY + kLaunchHopHeight });
        if (phaseT < kGateExitNoGravityTime) {
            const float gateExitT = EaseOutCubic(Clamp01(phaseT / kGateExitNoGravityTime));
            position.y = route_.start.y + (fallStartY - route_.start.y) * gateExitT;
        } else {
            const float fallT = Clamp01((phaseT - kGateExitNoGravityTime) / (1.0f - kGateExitNoGravityTime));
            const float fallDuration = (std::max)(kDuration * kLandingPhaseEnd * (1.0f - kGateExitNoGravityTime), 0.01f);
            const float fallTime = fallT * fallDuration;
            const float fallHeight = (std::max)(fallStartY - groundY, 0.0f);
            const float landingGravity = (std::max)(kGravity, (fallHeight * 2.0f) / (fallDuration * fallDuration));

            // ゲートを抜けた後は上向き初速を入れず、下向き重力だけで地面まで落とします。
            // 接地後は地面に固定し、以降の跳ね演出が空中で始まらないようにします。
            position.y = fallStartY - 0.5f * landingGravity * fallTime * fallTime;
            if (position.y < groundY) {
                position.y = groundY;
            }
        }

        const float launchSquash = 1.0f - Clamp01(phaseT / 0.22f);
        const float airborneStretch = std::sin(phaseT * std::numbers::pi_v<float>);
        const float landingSquash = std::sin(Clamp01((phaseT - 0.78f) / 0.22f) * std::numbers::pi_v<float>);
        scale = {
            route_.baseScale.x * (1.0f + launchSquash * 0.20f - airborneStretch * 0.06f + landingSquash * 0.32f),
            route_.baseScale.y * (1.0f - launchSquash * 0.22f + airborneStretch * 0.20f - landingSquash * 0.38f),
            route_.baseScale.z * (1.0f + launchSquash * 0.24f - airborneStretch * 0.05f + landingSquash * 0.26f)
        };
        pitch = -route_.direction.z * std::sin(phaseT * std::numbers::pi_v<float>) * 0.18f;
        roll = route_.direction.x * std::sin(phaseT * std::numbers::pi_v<float>) * 0.16f;
    } else if (normalizedTime < kFirstBounceEnd) {
        const float phaseT = Clamp01((normalizedTime - kLandingPhaseEnd) / (kFirstBounceEnd - kLandingPhaseEnd));
        const float moveT = EaseInOutCubic(phaseT);
        position = Lerp(landing, firstBounceEnd, moveT);
        position.y += std::sin(phaseT * std::numbers::pi_v<float>) * kFirstBounceHeight;

        const float stretch = std::sin(phaseT * std::numbers::pi_v<float>);
        const float landingSquash = std::sin(Clamp01((phaseT - 0.72f) / 0.28f) * std::numbers::pi_v<float>);
        scale = {
            route_.baseScale.x * (1.0f - stretch * 0.08f + landingSquash * 0.22f),
            route_.baseScale.y * (1.0f + stretch * 0.18f - landingSquash * 0.28f),
            route_.baseScale.z * (1.0f - stretch * 0.06f + landingSquash * 0.18f)
        };
        pitch = -route_.direction.z * std::sin(phaseT * std::numbers::pi_v<float>) * 0.12f;
        roll = route_.direction.x * std::sin(phaseT * std::numbers::pi_v<float>) * 0.10f;
    } else {
        const float phaseT = Clamp01((normalizedTime - kFirstBounceEnd) / (1.0f - kFirstBounceEnd));
        const float moveT = EaseInOutCubic(phaseT);
        position = Lerp(firstBounceEnd, route_.end, moveT);
        position.y += std::sin(phaseT * std::numbers::pi_v<float>) * kSecondBounceHeight;

        const float stretch = std::sin(phaseT * std::numbers::pi_v<float>);
        const float landingSquash = std::sin(Clamp01((phaseT - 0.70f) / 0.30f) * std::numbers::pi_v<float>);
        const float recover = EaseInOutCubic(phaseT);
        scale = {
            route_.baseScale.x * (1.0f - stretch * 0.05f + landingSquash * 0.12f) * (1.0f - recover * 0.00f),
            route_.baseScale.y * (1.0f + stretch * 0.10f - landingSquash * 0.16f),
            route_.baseScale.z * (1.0f - stretch * 0.04f + landingSquash * 0.10f)
        };
        pitch = -route_.direction.z * std::sin(phaseT * std::numbers::pi_v<float>) * 0.06f;
        roll = route_.direction.x * std::sin(phaseT * std::numbers::pi_v<float>) * 0.05f;
    }

    player->SetIsVisible(true);
    player->SetIsControlActive(false);
    player->SetVelocity({ 0.0f, 0.0f, 0.0f });
    player->SetMoveYaw(std::atan2(route_.direction.x, route_.direction.z));

    if (Transform* transform = player->GetTransform()) {
        transform->translate = position;
        transform->scale = scale;
        transform->rotate.x = pitch;
        transform->rotate.z = roll;
        transform->isQuaternionMaster = false;
    }
    player->UpdateLocalMatrix();
    player->UpdateWorldMatrix();
}

void PlayerGateReturnAnimation::ApplyFinishedPose(Player* player, bool restoreControl)
{
    if (!player) {
        return;
    }

    player->SetVelocity({ 0.0f, 0.0f, 0.0f });
    player->SetIsVisible(true);
    RestoreCollision(player);
    if (restoreControl) {
        player->SetIsControlActive(savedControlActive_);
    }

    if (Transform* transform = player->GetTransform()) {
        transform->translate = route_.end;
        transform->scale = route_.baseScale;
        transform->rotate.x = 0.0f;
        transform->rotate.z = 0.0f;
        transform->isQuaternionMaster = false;
    }
    player->UpdateLocalMatrix();
    player->UpdateWorldMatrix();
}

void PlayerGateReturnAnimation::SuspendCollision(Player* player)
{
    if (!player || collisionSuspended_) {
        return;
    }

    // 帰還演出中はTransformを演出側で直接制御します。
    // ゲートの縁やトリガー判定に押し戻されると空中で跳ねて見えるため、一時的に衝突を止めます。
    savedCollisionAttribute_ = player->GetCollisionAttribute();
    savedCollisionMask_ = player->GetCollisionMask();
    player->SetCollisionAttribute(0);
    player->SetCollisionMask(0);
    collisionSuspended_ = true;
}

void PlayerGateReturnAnimation::RestoreCollision(Player* player)
{
    if (!player || !collisionSuspended_) {
        return;
    }

    player->SetCollisionAttribute(savedCollisionAttribute_);
    player->SetCollisionMask(savedCollisionMask_);
    collisionSuspended_ = false;
}
