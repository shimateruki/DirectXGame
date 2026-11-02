#pragma once
#include "IAnimationState.h"
#include "engine/utility/math/Math.h"
#include "Model.h"
#include "game/actor/CollisionConfig.h"
#include <array>
#include <memory>

// 形態に依存しない鞭の調整値。発射時に複製し、途中のEditor変更では軌道を変えません。
struct PlayerWhipSettings {
    float maxDistance = 30.0f;
    float shootSpeed = 450.0f;
    float returnSpeed = 650.0f;
    float hitRadius = 0.65f;
    float windupDuration = 0.08f;
    float sweepDuration = 0.18f;
    float recoverDuration = 0.14f;
    float sweepAngleDegrees = 140.0f;
    float bendDegrees = 28.0f;
    float bodyTwistDegrees = 32.0f;
    float bodySquash = 0.18f;
    float thickness = 0.65f;
};

// 見た目と判定が共有する曲線。時間は秒、各点は発射原点からの相対座標です。
struct PlayerWhipMotion {
    static constexpr size_t kSegments = 32;
    using Curve = std::array<engine::Vector3, kSegments + 1>;
    static Curve Evaluate(const PlayerWhipSettings& settings, const engine::Vector3& forward, float time);
    static float Duration(const PlayerWhipSettings& settings);
};

// 振りかぶり、横薙ぎ、復帰を一つの時計で進めます。
class PlayerStateWhip final : public IAnimationState {
public:
    explicit PlayerStateWhip(const engine::Vector3& direction) : direction_(direction) {}
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;
private:
    void UpdateVisual(Player* player);
    void UpdateBodyPose(Player* player);
    bool CheckCurveHit(Player* player, const PlayerWhipMotion::Curve& curve, const engine::Vector3& origin);
    bool HandleHit(Player* player, engine::Object3d* target, const engine::Vector3& point);
    void BeginRecovery();
    void CollectCandidates(Player* player);
    struct QueryCandidate { const engine::Object3d* object; AABB bounds; bool special; };
    std::vector<QueryCandidate> queryCandidates_;
    PlayerWhipSettings settings_;
    engine::Vector3 origin_{};
    engine::Vector3 direction_{};
    float elapsed_ = 0.0f;
    bool blocked_ = false;
    float blockedAt_ = 0.0f;

    PlayerWhipMotion::Curve curve_{};
    PlayerWhipMotion::Curve blockedCurve_{};
    std::unique_ptr<Model> mesh_;
    Model* originalModel_ = nullptr;
    std::vector<Model::VertexData> vertices_;
};
