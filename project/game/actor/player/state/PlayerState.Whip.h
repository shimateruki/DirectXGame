#pragma once
#include "IAnimationState.h"
#include "engine/utility/math/Math.h"
#include "Model.h"
#include "engine/animation/BodyAnimationClip.h"
#include "engine/system/collision/CollisionConfig.h"
#include <array>
#include <memory>
#include <string>

class PlayerWhipDeformer;

// 形態に依存しない鞭の調整値。発射時に複製し、途中のEditor変更では軌道を変えません。
struct PlayerWhipSettings {
    float maxDistance = 8.0f;
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
    float tipDelay = 0.045f;
    float bindBlendDuration = 0.16f;
    float pullBendRatio = 0.10f;
    float groundSkimTolerance = 0.30f;
    float bodyShear = 0.55f;
    float bodyStretch = 0.28f;
    float captureBrace = 0.40f;
    float bodyRebound = 0.12f;
    float surfaceRootAngle = 55.0f;
    float surfaceLift = 1.25f;
    float tipRoundness = 0.22f;
    float contactSquash = 0.55f;
    float contactDuration = 0.16f;
    float returnBulge = 0.30f;
    float returnReceive = 0.22f;
    float returnSettleDuration = 0.20f;
    float launchMound = 0.18f;
    float launchDraw = 0.10f;
    float launchFlow = 0.24f;
};

// 見た目と判定が共有する曲線。時間は秒、各点は発射原点からの相対座標です。
// 関連：PlayerStateWhipの判定とPlayerWhipVisualの表示へ、同じ時刻の曲線を渡す。
// 主な入口：Evaluate / EvaluateBody
struct PlayerWhipMotion {
    static constexpr size_t kSegments = 32;
    using Curve = std::array<engine::Vector3, kSegments + 1>;
    static Curve Evaluate(const PlayerWhipSettings& settings, const engine::Vector3& forward, float time);
    static float Duration(const PlayerWhipSettings& settings);
    static float LaunchDuration(const PlayerWhipSettings& settings);
    static BodyAnimationClip::Sample EvaluateBody(const PlayerWhipSettings& settings, float time);
};

// 攻撃状態と捕獲状態が同じメッシュを所有し、命中時の形を途切れさせません。
// 関連：鞭攻撃から捕獲・復帰状態へ引き継ぎ、PlayerWhipDeformerで本体の見た目を更新する。
// 主な入口：Initialize / Draw / BeginPull / Finish
class PlayerWhipVisual final {
public:
    void Initialize(Player* player, const PlayerWhipSettings& settings);
    void Draw(Player* player, const PlayerWhipMotion::Curve& curve, const engine::Vector3& origin);
    void BeginPull(const PlayerWhipMotion::Curve& curve, const engine::Vector3& origin,
                   const engine::Vector3& targetPosition, const engine::Vector3& contactPoint);
    void UpdatePull(Player* player, const engine::Vector3& targetPosition, float deltaTime);
    void Finish(Player* player);
    void BeginReturn(Player* player);
    bool UpdateReturn(Player* player, float deltaTime);
    void SetReturning(bool returning) { recovering_ = returning; }
    void SetLaunchProgress(float progress, float weight = 1.0f);
    void RefreshAttachment(Player* player);
    void CaptureBodyPose(const BodyAnimationClip::Sample& pose) { capturePose_ = pose; }
    const PlayerWhipMotion::Curve& GetRenderCurve() const { return renderCurve_; }
    const PlayerWhipMotion::Curve& GetCurve() const { return curve_; }
private:
    PlayerWhipSettings settings_;
    PlayerWhipMotion::Curve curve_{}, hitCurve_{}, renderCurve_{};
    engine::Vector3 renderOrigin_{};
    BodyAnimationClip::Sample capturePose_;
    engine::Vector3 targetOffset_{}, contactOffset_{};
    float pullElapsed_ = 0.0f;
    bool contacting_ = false;
    float contactWeight_ = 0.0f;
    engine::Vector3 contactNormal_{};
    // 長さはワールド単位。進行度と強度は0～1、終了時計は秒です。
    // 初動の進行度と引き継ぎ強度。命中後は進行度を固定して強度だけを薄めます。
    float launchProgress_ = 1.0f, launchWeight_ = 1.0f, returnStartLaunchWeight_ = 0.0f;
    engine::Vector3 launchDirection_{0,0,1};
    bool recovering_ = false;
    float peakReach_ = 0.0f, returnProgress_ = 0.0f, returnAmount_ = 0.0f;
    float returnElapsed_ = 0.0f, returnStartProgress_ = 0.0f, returnFade_ = 1.0f;
    PlayerWhipMotion::Curve returnStartCurve_{};
    BodyAnimationClip::Sample returnStartPose_;
    std::shared_ptr<class PlayerWhipDeformer> deformer_;
};

// 振りかぶり、横薙ぎ、復帰を一つの時計で進めます。
// 関連：Playerから発射入力を受け、鞭の曲線・見た目・捕捉候補を同じ進行で更新する。
// 主な入口：Enter / Update / Exit
class PlayerStateWhip final : public IAnimationState {
public:
    explicit PlayerStateWhip(const engine::Vector3& direction) : direction_(direction) {}
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;
private:
    void UpdateVisual(Player* player);
    void UpdateBodyPose(Player* player, float time);
    bool CheckCurveHit(Player* player, const PlayerWhipMotion::Curve& curve, const engine::Vector3& origin, float time);
    bool HandleHit(Player* player, engine::Object3D* target, const engine::Vector3& point);
    void BeginRecovery();
    void CollectCandidates(Player* player);
    // 捕捉候補の借用参照と探索時の境界。specialは箱境界だけで候補を除外できない形状を示す。
    struct QueryCandidate { const engine::Object3D* object; Aabb bounds; bool special; };
    std::vector<QueryCandidate> queryCandidates_;
    PlayerWhipSettings settings_;
    engine::Vector3 origin_{};
    engine::Vector3 direction_{};
    float elapsed_ = 0.0f;
    bool blocked_ = false;
    float blockedAt_ = 0.0f;

    PlayerWhipMotion::Curve curve_{};
    PlayerWhipMotion::Curve blockedCurve_{};
    std::shared_ptr<PlayerWhipVisual> visual_;
};
