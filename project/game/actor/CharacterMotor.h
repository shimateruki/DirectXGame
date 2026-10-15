#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>

namespace engine { class Object3d; }
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
    engine::Vector3 groundNormal = {0.0f, 1.0f, 0.0f};
    engine::Object3d* groundObject = nullptr;
};

/// Characterの重力・移動・衝突補正を一か所へ集約する共通Motorです。
class CharacterMotor {
  public:
    /// <summary>Sweep、接地吸着、坂、段差の移動設定を適用する。</summary>
    void SetSettings(const CharacterMotorSettings& settings);
    /// <summary>現在の移動設定を返す。</summary>
    const CharacterMotorSettings& GetSettings() const { return settings_; }
    /// <summary>最後の移動で得た接地・遮断結果を返す。</summary>
    const CharacterMotorResult& GetResult() const { return result_; }

    /// <summary>前フレームの接地結果を消し、今回の衝突を受け取る準備をする。</summary>
    void BeginFrame(bool& grounded);
    /// <summary>速度へ重力を加え、下向き速度を上限内に収める。</summary>
    /// <param name="deltaTime">経過秒数。</param>
    void ApplyGravity(engine::Vector3& velocity, float gravity, float maxFallSpeed, float deltaTime) const;
    /// <summary>ワールド座標で位置と速度を更新する。</summary>
    /// <remarks>continuousCollisionが無効ならSweepをせず単純積分する。</remarks>
    void Move(engine::Object3d& owner, engine::Vector3& position, engine::Vector3& velocity, bool& grounded, float deltaTime);
    /// <summary>接触情報から位置、速度、接地状態を補正する。</summary>
    void ResolveCollision(engine::Vector3& position, engine::Vector3& velocity, bool& grounded, const CollisionInfo& info,
                          std::uint32_t attribute);

  private:
    /// <summary>接触面の法線が歩行できる傾斜か返す。</summary>
    bool IsWalkable(const engine::Vector3& normal) const;
    /// <summary>移動経路にある段差を登れるか試す。</summary>
    bool TryStep(engine::Object3d& owner, engine::Vector3& position, engine::Vector3& velocity, bool& grounded,
                 const engine::Vector3& planarDisplacement, float queryRadius);
    /// <summary>水平・垂直方向をSweepし、段差と接触面に合わせて位置と速度を補正する。</summary>
    void MoveSwept(engine::Object3d& owner, engine::Vector3& position, engine::Vector3& velocity, bool& grounded, const engine::Vector3& displacement);
    /// <summary>近い地面へ位置を吸着し、接地状態を更新する。</summary>
    void SnapToGround(engine::Object3d& owner, engine::Vector3& position, engine::Vector3& velocity, bool& grounded, float queryRadius,
                      float maximumDistance);

    CharacterMotorSettings settings_{};
    CharacterMotorResult result_{};
};
