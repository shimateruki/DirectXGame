#pragma once
#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include <cstdint>
#include <unordered_set>
#include <memory>
#include "engine/animation/BodyAnimationClip.h"
namespace engine { class Object3d; }

// 対象は更新中だけ借用します。役者種別をエディターへ持ち込まず実行条件を渡します。
struct AttackSequenceContext {
    engine::Object3d* owner = nullptr; // 更新中だけ借用。攻撃者の除外とDamageEventの発生元に使います。
    engine::Vector3 position{}, forward{0, 0, 1}, targetPosition{};
    engine::Vector3 ownerSize{1, 1, 1}; // 右・上・前に使う基準サイズ。見た目の瞬間的な伸縮とは分離します。
    float targetDistance = 0;
    bool hasTarget = false, grounded = false, playerOwned = false;
};

/// <summary>攻撃イベントから解決した発射位置、方向、速度を受け渡す。</summary>
struct AttackSequenceEmission {
    engine::Vector3 position{}, direction{};
    float speed = 0;
};

// 描画に依存しない追尾部品。対象を失った場合は最後の方向へ進みます。
struct AttackMotionState {
    engine::Vector3 position{}, direction{0, 0, 1};
    float elapsed = 0;
    bool launched = false;
    // 長さがほぼゼロのときは指定方向を維持する。
    static engine::Vector3 Normalize(engine::Vector3 value, engine::Vector3 fallback) {
        const float length = engine::Math::Length(value);
        return length > 0.00001f ? value * (1 / length) : fallback;
    }
    // 発射待ち・追尾区間を小刻みに積分する。巻き戻しは扱わない。
    void Advance(const AttackSequenceTrack& track, float time, engine::Vector3 target, bool hasTarget) {
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
                    if (engine::Math::Length(tangent) < 0.00001f) {
                        // 正反対の標的でも回転軸を失わないよう、直交する方向を選びます。
                        tangent = std::abs(direction.y) < 0.9f ? engine::Vector3{-direction.z, 0, direction.x} : engine::Vector3{1, 0, 0};
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
    auto normalize = [](engine::Vector3 value, engine::Vector3 fallback) {
        const float length = engine::Math::Length(value);
        return length > 0.0001f ? value * (1.0f / length) : fallback;
    };
    const float angle = event.yawDegrees * 0.01745329252f;
    auto rotate = [angle](engine::Vector3 value) {
        return engine::Vector3{value.x * std::cos(angle) + value.z * std::sin(angle), value.y,
            value.z * std::cos(angle) - value.x * std::sin(angle)};
    };
    const engine::Vector3 baseForward = normalize({context.forward.x, 0, context.forward.z}, {0, 0, 1});
    const engine::Vector3 forward = rotate(baseForward);
    const engine::Vector3 right{forward.z, 0, -forward.x};
    AttackSequenceEmission result;
    result.position = context.position + right * (event.right + context.ownerSize.x * event.sizeRight)
        + engine::Vector3{0, event.up + context.ownerSize.y * event.sizeUp, 0}
        + forward * (event.forward + context.ownerSize.z * event.sizeForward);
    result.direction = event.aimAtTarget && context.hasTarget
        ? rotate(normalize(context.targetPosition + engine::Vector3{0, event.targetHeight, 0} - result.position, baseForward))
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
    // 攻撃定義をコピーし、開始条件を満たせばシーケンスを起動する。
    bool Start(const EnemyAttackDefinition& definition, const AttackSequenceContext& context);
    // 経過時間を進め、条件を満たしたトラックとイベントを実行する。
    void Update(float dt, const AttackSequenceContext& context);
    // 実行中の効果と姿勢の参照を解放して停止する。
    void Cancel();
    bool IsRunning() const { return clock_.IsRunning(); }
    const EnemyAttackDefinition& GetDefinition() const { return definition_; }
    const char* GetStateLabel() const { return label_.c_str(); }
    float GetElapsed() const { return clock_.GetElapsed(); }
  private:
    // 攻撃条件文字列を現在の役者・標的状態で評価する。
    bool CheckCondition(const std::string& condition, const AttackSequenceContext& context) const;
    // 前回時刻から現在までに入ったトラックを更新する。
    void UpdateTracks(float previous, const AttackSequenceContext& context);
    // トラックが保持する効果のスコープを解放する。
    void ReleaseTrackEffects();
    std::shared_ptr<engine::Matrix4x4> bodyPose_; // Rendererは弱参照。Cancel/終了時に破棄して姿勢を解除します。
    /// <summary>各攻撃トラックの発動状況、当たり判定、姿勢と移動の実行状態を保持する。</summary>
    struct TrackState {
        bool entered = false, allowed = false;
        uint64_t effectScope = 0;
        engine::Vector3 previousPosition{};
        bool hasPreviousPosition = false;
        std::unordered_set<uint64_t> victims; // 再生ごと、トラックごとに一体一回。
        BodyAnimationClip bodyClip;
        AttackMotionState motion;
        bool motionInitialized = false;
        engine::Vector3 worldPosition{}, worldForward{0, 0, 1};
        engine::Vector3 frameStartPosition{}; // 追従判定の開始フレームでも通過した区間を問い合わせます。
    };
    std::vector<TrackState> trackStates_;
    AttackSequenceContext initialContext_;
    uint64_t generation_ = 0; // 同期DamageEventから中断・再開始された場合の通知打ち切りに使います。
    // 定義された攻撃イベントを現在の座標と対象へ適用する。
    void Execute(const AttackSequenceEvent& event, const AttackSequenceContext& context);
    EnemyAttackDefinition definition_;
    AttackSequenceClock clock_;
    std::string label_ = "待機";
};
