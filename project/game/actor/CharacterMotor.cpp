#define NOMINMAX
#include "CharacterMotor.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "Object3d.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinimumDistance = 0.0001f;
constexpr float kDegreesToRadians = 3.1415926535f / 180.0f;

float Dot(const Vector3& lhs, const Vector3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

float Length(const Vector3& value) {
    return std::sqrt(Dot(value, value));
}

Vector3 NormalizeOrZero(const Vector3& value) {
    const float length = Length(value);
    return length > kMinimumDistance ? value * (1.0f / length) : Vector3{};
}

Vector3 ResolveQueryCenter(const Object3d& owner, const Vector3& position) {
    const Object3d::ColliderConfig& collider = owner.GetColliderConfig();
    return position + collider.center;
}

float ResolveQueryRadius(const Object3d& owner, float skinWidth) {
    const Collider* collider = owner.GetCollider();
    const float radius = collider ? collider->GetRadius() : 0.5f;
    return (std::max)(0.05f, radius - skinWidth);
}
}

void CharacterMotor::SetSettings(const CharacterMotorSettings& settings) {
    settings_ = settings;
    settings_.maxSlopeDegrees = std::clamp(settings_.maxSlopeDegrees, 0.0f, 89.9f);
    settings_.stepHeight = (std::max)(0.0f, settings_.stepHeight);
    settings_.groundProbeDistance = (std::max)(0.0f, settings_.groundProbeDistance);
    settings_.skinWidth = std::clamp(settings_.skinWidth, 0.0f, 0.25f);
}

void CharacterMotor::BeginFrame(bool& grounded) {
    result_.grounded = false;
    result_.blocked = false;
    result_.steppedUp = false;
    result_.groundNormal = { 0.0f, 1.0f, 0.0f };
    result_.groundObject = nullptr;
    grounded = false;
}

void CharacterMotor::ApplyGravity(
    Vector3& velocity,
    float gravity,
    float maxFallSpeed,
    float deltaTime) const {
    velocity.y -= gravity * deltaTime;
    if (velocity.y < -maxFallSpeed) {
        velocity.y = -maxFallSpeed;
    }
}

void CharacterMotor::Move(
    Object3d& owner,
    Vector3& position,
    Vector3& velocity,
    bool& grounded,
    float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    const Vector3 displacement = velocity * deltaTime;
    if (!settings_.continuousCollision) {
        // 既存実装と同じ積分式を維持する互換経路です。
        position += displacement;
        return;
    }

    MoveSwept(owner, position, velocity, grounded, displacement);
}

void CharacterMotor::ResolveCollision(
    Vector3& position,
    Vector3& velocity,
    bool& grounded,
    const CollisionInfo& info,
    std::uint32_t attribute) {
    if (!(attribute & kAllSolid)) {
        return;
    }

    position += info.normal * info.penetration;

    const float velocityIntoSurface = Dot(velocity, info.normal);
    if (velocityIntoSurface < 0.0f) {
        velocity = velocity - info.normal * velocityIntoSurface;
    }

    result_.blocked = true;
    if (IsWalkable(info.normal)) {
        grounded = true;
        result_.grounded = true;
        result_.groundNormal = info.normal;
        if (velocity.y < 0.0f) {
            velocity.y = 0.0f;
        }
    }
}

bool CharacterMotor::IsWalkable(const Vector3& normal) const {
    const float minimumNormalY = std::cos(settings_.maxSlopeDegrees * kDegreesToRadians);
    return normal.y > minimumNormalY;
}

bool CharacterMotor::TryStep(
    Object3d& owner,
    Vector3& position,
    Vector3& velocity,
    bool& grounded,
    const Vector3& planarDisplacement,
    float queryRadius) {
    if (settings_.stepHeight <= 0.0f || Length(planarDisplacement) <= kMinimumDistance) {
        return false;
    }

    PhysicsQueryFilter filter;
    filter.mask = settings_.collisionMask;
    filter.ignoredObject = &owner;
    filter.includeTriggers = false;

    const float planarDistance = Length(planarDisplacement);
    const Vector3 direction = NormalizeOrZero(planarDisplacement);
    const Vector3 raisedPosition = position + Vector3{ 0.0f, settings_.stepHeight, 0.0f };
    const Vector3 raisedCenter = ResolveQueryCenter(owner, raisedPosition);
    const RaycastHit forwardHit = CollisionManager::GetInstance()->SphereCast(
        raisedCenter,
        queryRadius,
        direction,
        planarDistance + settings_.skinWidth,
        filter);
    if (forwardHit.isHit) {
        return false;
    }

    position = raisedPosition + planarDisplacement;
    SnapToGround(
        owner,
        position,
        velocity,
        grounded,
        queryRadius,
        settings_.stepHeight + settings_.groundProbeDistance);
    if (!grounded) {
        position = position - Vector3{ 0.0f, settings_.stepHeight, 0.0f };
        return false;
    }

    result_.steppedUp = true;
    return true;
}

void CharacterMotor::MoveSwept(
    Object3d& owner,
    Vector3& position,
    Vector3& velocity,
    bool& grounded,
    const Vector3& displacement) {
    PhysicsQueryFilter filter;
    filter.mask = settings_.collisionMask;
    filter.ignoredObject = &owner;
    filter.includeTriggers = false;

    const float queryRadius = ResolveQueryRadius(owner, settings_.skinWidth);
    const Vector3 planarDisplacement = { displacement.x, 0.0f, displacement.z };
    const float planarDistance = Length(planarDisplacement);
    if (planarDistance > kMinimumDistance) {
        const Vector3 planarDirection = planarDisplacement * (1.0f / planarDistance);
        const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(
            ResolveQueryCenter(owner, position),
            queryRadius,
            planarDirection,
            planarDistance + settings_.skinWidth,
            filter);
        if (!hit.isHit) {
            position += planarDisplacement;
        } else if (!TryStep(owner, position, velocity, grounded, planarDisplacement, queryRadius)) {
            const float safeDistance = (std::max)(0.0f, hit.distance - settings_.skinWidth);
            const float travelledDistance = (std::min)(safeDistance, planarDistance);
            position += planarDirection * travelledDistance;

            Vector3 remaining = planarDisplacement - planarDirection * travelledDistance;
            const float intoSurface = Dot(remaining, hit.normal);
            if (intoSurface < 0.0f) {
                remaining = remaining - hit.normal * intoSurface;
            }
            position += remaining;

            const float velocityIntoSurface = Dot(velocity, hit.normal);
            if (velocityIntoSurface < 0.0f) {
                velocity = velocity - hit.normal * velocityIntoSurface;
            }
            result_.blocked = true;
        }
    }

    const float verticalDistance = std::abs(displacement.y);
    if (verticalDistance > kMinimumDistance) {
        const Vector3 verticalDirection = { 0.0f, displacement.y > 0.0f ? 1.0f : -1.0f, 0.0f };
        const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(
            ResolveQueryCenter(owner, position),
            queryRadius,
            verticalDirection,
            verticalDistance + settings_.skinWidth,
            filter);
        if (!hit.isHit) {
            position.y += displacement.y;
        } else {
            const float safeDistance = (std::max)(0.0f, hit.distance - settings_.skinWidth);
            position.y += verticalDirection.y * (std::min)(safeDistance, verticalDistance);
            if (verticalDirection.y < 0.0f && IsWalkable(hit.normal)) {
                grounded = true;
                result_.grounded = true;
                result_.groundNormal = hit.normal;
                result_.groundObject = hit.hitObject;
            }
            velocity.y = 0.0f;
            result_.blocked = true;
        }
    }

    if (settings_.snapToGround && velocity.y <= 0.0f) {
        SnapToGround(
            owner,
            position,
            velocity,
            grounded,
            queryRadius,
            settings_.groundProbeDistance);
    }
}

void CharacterMotor::SnapToGround(
    Object3d& owner,
    Vector3& position,
    Vector3& velocity,
    bool& grounded,
    float queryRadius,
    float maximumDistance) {
    if (maximumDistance <= 0.0f) {
        return;
    }

    PhysicsQueryFilter filter;
    filter.mask = settings_.collisionMask;
    filter.ignoredObject = &owner;
    filter.includeTriggers = false;

    const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(
        ResolveQueryCenter(owner, position),
        queryRadius,
        { 0.0f, -1.0f, 0.0f },
        maximumDistance + settings_.skinWidth,
        filter);
    if (!hit.isHit || !IsWalkable(hit.normal)) {
        return;
    }

    const float snapDistance = (std::max)(0.0f, hit.distance - settings_.skinWidth);
    position.y -= (std::min)(snapDistance, maximumDistance);
    grounded = true;
    result_.grounded = true;
    result_.groundNormal = hit.normal;
    result_.groundObject = hit.hitObject;
    if (velocity.y < 0.0f) {
        velocity.y = 0.0f;
    }
}
