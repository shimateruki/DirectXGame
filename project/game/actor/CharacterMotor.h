#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>

class Object3d;
struct CollisionInfo;

/// Character共通の接地・Sweep・坂・段差設定です。
/// 高度な補正は明示的に有効化し、既存キャラクターの挙動を自動では変えません。
struct CharacterMotorSettings {
    bool continuousCollision = false;
    bool snapToGround = false;
    // 斜面は角度、段差・地面探索・skin はワールド空間の距離。
    float maxSlopeDegrees = 45.573f;
    float stepHeight = 0.0f;
    float groundProbeDistance = 0.18f;
    float skinWidth = 0.025f;
    std::uint32_t collisionMask = 0xffffffffu;
};

/// 最後に解決した接地面と移動結果を、ゲーム処理とデバッグ表示へ公開します。
struct CharacterMotorResult {
    bool grounded = false;
    bool blocked = false;
    bool steppedUp = false;
    Vector3 groundNormal = {0.0f, 1.0f, 0.0f};
    Object3d* groundObject = nullptr;
};

/// Characterの重力・移動・衝突補正を一か所へ集約する共通Motorです。
class CharacterMotor {
  public:
    void SetSettings(const CharacterMotorSettings& settings);
    const CharacterMotorSettings& GetSettings() const { return settings_; }
    const CharacterMotorResult& GetResult() const { return result_; }

    // 前フレームの接地結果を消し、このフレームの衝突結果を受け取る準備をする。
    void BeginFrame(bool& grounded);
    // gravity は下向き加速度、maxFallSpeed は下向き速度の上限、deltaTime は秒。
    void ApplyGravity(Vector3& velocity, float gravity, float maxFallSpeed, float deltaTime) const;
    // position/velocity はワールド空間。continuousCollision 無効時は単純積分のみ行う。
    void Move(Object3d& owner, Vector3& position, Vector3& velocity, bool& grounded, float deltaTime);
    void ResolveCollision(Vector3& position, Vector3& velocity, bool& grounded, const CollisionInfo& info,
                          std::uint32_t attribute);

  private:
    bool IsWalkable(const Vector3& normal) const;
    bool TryStep(Object3d& owner, Vector3& position, Vector3& velocity, bool& grounded,
                 const Vector3& planarDisplacement, float queryRadius);
    void MoveSwept(Object3d& owner, Vector3& position, Vector3& velocity, bool& grounded, const Vector3& displacement);
    void SnapToGround(Object3d& owner, Vector3& position, Vector3& velocity, bool& grounded, float queryRadius,
                      float maximumDistance);

    CharacterMotorSettings settings_{};
    CharacterMotorResult result_{};
};
