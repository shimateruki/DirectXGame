#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>

class Object3d;
struct CollisionInfo;

/// Character共通の接地、連続衝突、坂、段差補正をまとめた設定です。
/// 連続衝突と接地吸着は明示的に有効化し、既存キャラクターとの互換性を保ちます。
struct CharacterMotorSettings {
    bool continuousCollision = false;
    bool snapToGround = false;
    float maxSlopeDegrees = 45.573f;
    float stepHeight = 0.0f;
    float groundProbeDistance = 0.18f;
    float skinWidth = 0.025f;
    std::uint32_t collisionMask = 0xffffffffu;
};

/// 直近のMoveまたはResolveCollisionで得た接地面と移動結果です。
/// groundObjectはObjectManagerが所有し、この結果は所有権を持ちません。
struct CharacterMotorResult {
    bool grounded = false;
    bool blocked = false;
    bool steppedUp = false;
    Vector3 groundNormal = { 0.0f, 1.0f, 0.0f };
    Object3d* groundObject = nullptr;
};

/// Characterの重力、移動、衝突補正を一か所へ集約する共通Motorです。
/// 水平移動、段差、垂直移動、接地吸着の順に解決し、高速移動のすり抜けを抑えます。
class CharacterMotor {
public:
    void SetSettings(const CharacterMotorSettings& settings);
    const CharacterMotorSettings& GetSettings() const { return settings_; }
    const CharacterMotorResult& GetResult() const { return result_; }

    void BeginFrame(bool& grounded);
    void ApplyGravity(Vector3& velocity, float gravity, float maxFallSpeed, float deltaTime) const;
    void Move(Object3d& owner, Vector3& position, Vector3& velocity, bool& grounded, float deltaTime);
    void ResolveCollision(
        Vector3& position,
        Vector3& velocity,
        bool& grounded,
        const CollisionInfo& info,
        std::uint32_t attribute);

private:
    bool IsWalkable(const Vector3& normal) const;
    bool TryStep(
        Object3d& owner,
        Vector3& position,
        Vector3& velocity,
        bool& grounded,
        const Vector3& planarDisplacement,
        float queryRadius);
    void MoveSwept(
        Object3d& owner,
        Vector3& position,
        Vector3& velocity,
        bool& grounded,
        const Vector3& displacement);
    void SnapToGround(
        Object3d& owner,
        Vector3& position,
        Vector3& velocity,
        bool& grounded,
        float queryRadius,
        float maximumDistance);

    CharacterMotorSettings settings_{};
    CharacterMotorResult result_{};
};
