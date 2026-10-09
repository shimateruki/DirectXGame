#pragma once
#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include <cstdint>
#include <unordered_set>
#include <memory>
#include "engine/animation/BodyAnimationClip.h"
class Object3d;

// 対象は更新中だけ借用します。役者種別をエディターへ持ち込まず実行条件を渡します。
struct AttackSequenceContext {
    Object3d* owner = nullptr; // 更新中だけ借用。攻撃者の除外とDamageEventの発生元に使います。
    Vector3 position{}, forward{0, 0, 1}, targetPosition{};
    Vector3 ownerSize{1, 1, 1}; // 右・上・前に使う基準サイズ。見た目の瞬間的な伸縮とは分離します。
    float targetDistance = 0;
    bool hasTarget = false, grounded = false, playerOwned = false;
};

struct AttackSequenceEmission {
    Vector3 position{}, direction{};
    float speed = 0;
};

// 描画に依存しない追尾部品。対象を失った場合は最後の方向へ進みます。
struct AttackMotionState {
    Vector3 position{}, direction{0, 0, 1};
    float elapsed = 0;
    bool launched = false;
    static Vector3 Normalize(Vector3 value, Vector3 fallback) {
        const float length = Math::Length(value);
        return length > 0.00001f ? value * (1 / length) : fallback;
    }
    void Advance(const AttackSequenceTrack& track, float time, Vector3 target, bool hasTarget) {
        if (time < elapsed) return; // 巻き戻しはインスタンスの再生成で行います。
        if (!launched && hasTarget)
            direction = Normalize(target - position, direction);
        if (time >= track.launchDelay) launched = true;
        float cursor = (std::max)(elapsed, track.launchDelay);
        while (cursor < time) {
            float step = (std::min)(1.0f / 120.0f, time - cursor);
            const float trackingEnd = track.launchDelay + track.homingDuration;
            if (cursor < trackingEnd) step = (std::min)(step, trackingEnd - cursor);
            if (track.motion == "homing" && cursor < trackingEnd && hasTarget) {
                const auto desired = Normalize(target - position, direction);
                const float dot = std::clamp(direction.x * desired.x + direction.y * desired.y + direction.z * desired.z, -1.0f, 1.0f);
                const float angle = std::acos(dot);
                const float turn = (std::min)(angle, track.turnDegreesPerSecond * 0.01745329252f * step);
                if (angle > 0.00001f) {
                    auto tangent = desired - direction * dot;
                    if (Math::Length(tangent) < 0.00001f) {
                        // 正反対の標的でも回転軸を失わないよう、直交する方向を選びます。
                        tangent = std::abs(direction.y) < 0.9f ? Vector3{-direction.z, 0, direction.x} : Vector3{1, 0, 0};
                    }
                    tangent = Normalize(tangent, {1, 0, 0});
                    direction = Normalize(direction * std::cos(turn) + tangent * std::sin(turn), direction);
                }
            }
            position = position + direction * (track.moveSpeed * step);
            cursor += step;
        }
        elapsed = time;
    }
};

// 描画・弾生成から座標計算を分離し、本編とテストで同じ発射式を検証できるようにします。
inline AttackSequenceEmission ResolveAttackSequenceEmission(const AttackSequenceEvent& event,
    const AttackSequenceContext& context, float minSpeed, float maxSpeed) {
    auto normalize = [](Vector3 value, Vector3 fallback) {
        const float length = Math::Length(value);
        return length > 0.0001f ? value * (1.0f / length) : fallback;
    };
    const float angle = event.yawDegrees * 0.01745329252f;
    auto rotate = [angle](Vector3 value) {
        return Vector3{value.x * std::cos(angle) + value.z * std::sin(angle), value.y,
            value.z * std::cos(angle) - value.x * std::sin(angle)};
    };
    const Vector3 baseForward = normalize({context.forward.x, 0, context.forward.z}, {0, 0, 1});
    const Vector3 forward = rotate(baseForward);
    const Vector3 right{forward.z, 0, -forward.x};
    AttackSequenceEmission result;
    result.position = context.position + right * (event.right + context.ownerSize.x * event.sizeRight)
        + Vector3{0, event.up + context.ownerSize.y * event.sizeUp, 0}
        + forward * (event.forward + context.ownerSize.z * event.sizeForward);
    result.direction = event.aimAtTarget && context.hasTarget
        ? rotate(normalize(context.targetPosition + Vector3{0, event.targetHeight, 0} - result.position, baseForward))
        : forward;
    result.speed = event.distanceSpeedScale > 0 && context.hasTarget
        ? std::clamp(context.targetDistance * event.distanceSpeedScale + event.distanceSpeedBias, minSpeed, maxSpeed)
        : maxSpeed;
    return result;
}

// 本編とプレビューで同じ命令を実行する。実行中の設定はコピーしてリロードから保護します。
class AttackSequence {
  public:
    ~AttackSequence();
    AttackSequence() = default;
    AttackSequence(const AttackSequence&) = delete;
    AttackSequence& operator=(const AttackSequence&) = delete;
    bool Start(const EnemyAttackDefinition& definition, const AttackSequenceContext& context);
    void Update(float dt, const AttackSequenceContext& context);
    void Cancel();
    bool IsRunning() const { return clock_.IsRunning(); }
    const EnemyAttackDefinition& GetDefinition() const { return definition_; }
    const char* GetStateLabel() const { return label_.c_str(); }
    float GetElapsed() const { return clock_.GetElapsed(); }
  private:
    bool CheckCondition(const std::string& condition, const AttackSequenceContext& context) const;
    void UpdateTracks(float previous, const AttackSequenceContext& context);
    void ReleaseTrackEffects();
    std::shared_ptr<Matrix4x4> bodyPose_; // Rendererは弱参照。Cancel/終了時に破棄して姿勢を解除します。
    struct TrackState {
        bool entered = false, allowed = false;
        uint64_t effectScope = 0;
        Vector3 previousPosition{};
        bool hasPreviousPosition = false;
        std::unordered_set<uint64_t> victims; // 再生ごと、トラックごとに一体一回。
        BodyAnimationClip bodyClip;
        AttackMotionState motion;
        bool motionInitialized = false;
        Vector3 worldPosition{}, worldForward{0, 0, 1};
        Vector3 frameStartPosition{}; // 追従判定の開始フレームでも通過した区間を問い合わせます。
    };
    std::vector<TrackState> trackStates_;
    AttackSequenceContext initialContext_;
    uint64_t generation_ = 0; // 同期DamageEventから中断・再開始された場合の通知打ち切りに使います。
    void Execute(const AttackSequenceEvent& event, const AttackSequenceContext& context);
    EnemyAttackDefinition definition_;
    AttackSequenceClock clock_;
    std::string label_ = "待機";
};
