#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "AttackSequenceTracks.h"

// 炎弾の共通描画器へ渡す値。外観の違いごとに敵専用の発射処理を増やさないための設定です。
struct AttackFireAppearance {
    float red = 1.0f, green = 0.34f, blue = 0.07f, alpha = 0.96f;
    float emissive = 2.8f;
    float effectScale = 1.15f, softness = 0.42f, intensity = 0.96f, billboardScale = 0.68f;
    float trailInterval = 0.030f, trailSpeedScale = 1.0f;
};

// 発射する弾の構成。未指定の旧JSONでは攻撃全体の設定を引き継ぎます。
struct AttackProjectileComponent {
    bool configured = false;
    bool collisionEnabled = true;
    std::string model = "Primitives/sphere";
    std::string style = "fire";
    float radius = 0.5f, lifetime = 2.65f, minSpeed = 15, maxSpeed = 28, damage = 1;
    float visualScale = 1.2f;
    AttackFireAppearance appearance;
    std::string trail = "fireball_trail_flame", secondaryTrail = "fireball_trail_embers";
    std::string impact = "fireball_impact_flame", secondaryImpact = "fireball_impact_sparks";
    bool Validate() const {
        if (!configured) return true;
        if (model.empty() || (style != "fire" && style != "plain")) return false;
        for (float value : {radius, lifetime, minSpeed, maxSpeed, damage, visualScale})
            if (!std::isfinite(value) || value < 0 || value > 1000) return false;
        if (radius <= 0 || lifetime <= 0 || visualScale <= 0 || visualScale > 20 || minSpeed > maxSpeed) return false;
        const auto& a = appearance;
        for (float value : {a.red, a.green, a.blue, a.alpha, a.emissive, a.effectScale, a.softness,
            a.intensity, a.billboardScale, a.trailInterval, a.trailSpeedScale})
            if (!std::isfinite(value) || value < 0 || value > 100) return false;
        return a.alpha <= 1 && a.trailInterval >= 0.001f;
    }
};

// 命令列の開始からの秒数で一度だけ実行します。開始する攻撃フェーズは接続先が決めます。
// 座標は術者の右・上・前を基準とし、yawDegreesでその基準方向を水平回転します。
struct AttackSequenceEvent {
    std::string name;
    bool enabled = true;
    AttackProjectileComponent projectile;
    float time = 0.0f;
    std::string operation = "projectile"; // projectile / gpu_vfx
    std::string condition = "always"; // always / grounded / airborne / target_in_range
    bool stopOnFalse = false;
    float right = 0.0f, up = 1.05f, forward = 0.65f;
    // 通常の位置に加算する、術者の基準サイズに比例した位置。伸縮アニメーションには追従しません。
    float sizeRight = 0.0f, sizeUp = 0.0f, sizeForward = 0.0f;
    float yawDegrees = 0.0f; // 術者の前方を基準に、発生位置と発射方向を水平回転します。
    bool aimAtTarget = true;
    float targetHeight = 0.35f;
    float distanceSpeedScale = 1.65f; // 0なら攻撃定義の最高速度を使います。
    float distanceSpeedBias = 0.0f; // 距離×倍率に加え、最後に攻撃の最低・最高速度で制限します。
    float verticalSpeed = 0.0f;
    int count = 1;
    float spreadDegrees = 0.0f;
    std::string preset;
    bool directed = false; // GPU命令だけに適用。プリセットの速度を発射方向へ揃えます。
    float particleSpeedScale = 1.0f;
};

// 術者の攻撃中の振る舞い。敵の種類や攻撃名に依存しない共通部品です。
struct AttackActorControl {
    bool enabled = false; // 旧攻撃は従来のAI接続を保ち、移行済みだけ有効にします。
    bool stopMovement = true;
    bool faceTarget = true;
    float aimLockTime = 0.24f; // 開始からの秒。以後は方向・標的位置・距離を固定します。
    float modelYawOffset = 180.0f; // モデル正面の補正角（度）。
    bool telegraph = true;
    float telegraphEnd = 0.42f;
    float telegraphWidth = 1.0f;
    float cueTime = 0.24f;
    bool tint = true;
    std::array<float, 4> color{1, 0.72f, 0.48f, 1};
    std::array<float, 4> telegraphColor{1, 0.34f, 0.06f, 0.8f};
    bool Validate(float duration) const {
        if (!enabled) return true;
        for (float v : {aimLockTime, telegraphEnd, cueTime})
            if (!std::isfinite(v) || v < 0 || v > duration) return false;
        if (!std::isfinite(modelYawOffset) || std::abs(modelYawOffset) > 360 ||
            !std::isfinite(telegraphWidth) || telegraphWidth <= 0 || telegraphWidth > 100) return false;
        for (auto values : {color, telegraphColor})
            for (float v : values) if (!std::isfinite(v) || v < 0 || v > 1) return false;
        return true;
    }
};

struct AttackSequenceDefinition {
    AttackActorControl actorControl;
    std::string assetPath; // 空なら埋め込み。設定時はこのJSONを正本としてプロファイル読込時に解決します。
    bool enabled = false;
    std::string binding = "enemy"; // enemy / player_special
    int priority = 0; // 同じ入力・射程に複数候補がある場合は大きい値を優先します。
    float duration = 0.42f;
    std::string projectileStyle = "fire"; // fire / plain
    float visualScale = 1.2f;
    AttackFireAppearance fireAppearance;
    std::vector<AttackSequenceEvent> events;
    std::vector<AttackSequenceTrack> tracks;

    // 誤った命令を黙って実行せず、読込・保存・実行開始で同じ検証を使います。
    bool Validate(std::string* error = nullptr) const {
        auto fail = [&](const char* reason) { if (error) *error = reason; return false; };
        if (!enabled) return true;
        if (binding != "enemy" && binding != "player_special") return fail("実行先が不正です。");
        if (!actorControl.Validate(duration)) return fail("術者制御の時刻・幅・色が範囲外です。");
        if (actorControl.enabled && binding != "enemy") return fail("術者制御は敵用の共通アクションで使用します。");
        if (!std::isfinite(duration) || duration < 0.0f || duration > 30.0f) return fail("再生時間は0～30秒です。");
        if (projectileStyle != "fire" && projectileStyle != "plain") return fail("弾の外観が不正です。");
        if (!std::isfinite(visualScale) || visualScale <= 0 || visualScale > 20) return fail("表示倍率が不正です。");
        const auto& appearance = fireAppearance;
        for (float value : {appearance.red, appearance.green, appearance.blue, appearance.alpha,
            appearance.emissive, appearance.effectScale, appearance.softness, appearance.intensity,
            appearance.billboardScale, appearance.trailInterval, appearance.trailSpeedScale})
            if (!std::isfinite(value) || value < 0 || value > 100) return fail("炎弾の外観設定が不正です。");
        if (appearance.alpha > 1 || appearance.trailInterval < 0.001f) return fail("炎弾の透明度または軌跡間隔が範囲外です。");
        if ((events.empty() && tracks.empty()) || events.size() > 128 || tracks.size() > 64)
            return fail("命令またはトラックが必要です。命令は128件、トラックは64件までです。");
        for (const auto& track : tracks) if (!track.Validate(duration, error)) return false;
        for (size_t i = 0; i < tracks.size(); ++i) {
            const auto& track = tracks[i];
            if (track.parentTrack >= 0) {
                if (track.type != "hitbox" || static_cast<size_t>(track.parentTrack) >= i)
                    return fail("判定の親は一覧で自身より前にある表示Objectを指定してください。");
                const auto& parent = tracks[track.parentTrack];
                if (parent.type != "mesh_vfx" || !parent.enabled || parent.begin > track.begin || parent.end < track.end)
                    return fail("親Objectは有効で、判定の区間全体を含む必要があります。");
            }
            if (track.condition == "contact" || track.condition == "no_contact" || track.condition == "track_completed") {
                if (track.conditionTrack < 0 || static_cast<size_t>(track.conditionTrack) >= tracks.size())
                    return fail("分岐の参照元となる判定トラックを選んでください。");
                const auto& source = tracks[track.conditionTrack];
                if (source.type != "hitbox" || !source.enabled || source.end > track.begin)
                    return fail("分岐元は有効な判定トラックで、後続の開始までに終了している必要があります。");
            }
            // 同時に二つの本体姿勢を上書きしない。条件が排他的でも区間を分けます。
            if (track.enabled && track.type == "body_animation")
                for (size_t j = 0; j < i; ++j)
                    if (tracks[j].enabled && tracks[j].type == "body_animation" &&
                        tracks[j].begin < track.end && track.begin < tracks[j].end)
                        return fail("本体アニメーションの区間は重ねられません。");
        }
        for (const auto& event : events) {
            if (!event.projectile.Validate()) return fail("弾部品の半径・寿命・速度・見た目を確認してください。");
            if (!std::isfinite(event.time) || event.time < 0 || event.time > duration) return fail("イベント時刻が再生区間外です。");
            if (event.operation != "projectile" && event.operation != "gpu_vfx") return fail("未対応の命令です。");
            if (event.condition != "always" && event.condition != "grounded" && event.condition != "airborne" && event.condition != "target_in_range") return fail("未対応の条件です。");
            if (event.count < 1 || event.count > 32) return fail("同時発射数は1～32です。");
            for (float value : {event.right, event.up, event.forward, event.sizeRight, event.sizeUp, event.sizeForward,
                event.yawDegrees, event.targetHeight, event.distanceSpeedScale, event.distanceSpeedBias,
                event.verticalSpeed, event.spreadDegrees, event.particleSpeedScale})
                if (!std::isfinite(value) || std::abs(value) > 1000.0f) return fail("イベントの数値が不正です。");
            if (event.distanceSpeedScale < 0 || event.spreadDegrees < 0 || event.spreadDegrees > 360) return fail("速度倍率または拡散角が範囲外です。");
            if (event.particleSpeedScale < 0) return fail("粒子の速度倍率は0以上です。");
        }
        if (error) error->clear();
        return true;
    }
};

// 描画やPlayer型に依存しない時計。大きなdtで通過した命令も時刻順に一度ずつ通知します。
class AttackSequenceClock {
  public:
    bool Start(const AttackSequenceDefinition& definition) {
        Reset();
        if (!definition.enabled || !definition.Validate()) return false;
        definition_ = definition;
        std::stable_sort(definition_.events.begin(), definition_.events.end(),
            [](const auto& a, const auto& b) { return a.time < b.time; });
        running_ = true;
        return true;
    }
    template<class Condition, class Execute>
    void Update(float dt, Condition condition, Execute execute) {
        if (!running_ || !std::isfinite(dt) || dt < 0) return;
        elapsed_ = (std::min)(definition_.duration, elapsed_ + dt);
        while (running_ && next_ < definition_.events.size() && definition_.events[next_].time <= elapsed_ + 0.000001f) {
            const auto& event = definition_.events[next_++];
            if (!event.enabled) { ++skipped_; continue; } // 無効な部品は後続命令を止めません。
            if (condition(event.condition)) {
                execute(event);
                ++executed_;
            } else {
                ++skipped_;
                if (event.stopOnFalse) {
                    running_ = false;
                    stoppedByCondition_ = true;
                    elapsed_ = event.time; // 大きなdtでも停止イベントより後の区間を評価しません。
                }
            }
        }
        if (elapsed_ >= definition_.duration) running_ = false;
    }
    void Reset() { running_ = false; stoppedByCondition_ = false; elapsed_ = 0; next_ = executed_ = skipped_ = 0; }
    bool WasStoppedByCondition() const { return stoppedByCondition_; }
    bool IsRunning() const { return running_; }
    float GetElapsed() const { return elapsed_; }
    size_t GetExecutedCount() const { return executed_; }
    size_t GetSkippedCount() const { return skipped_; }
  private:
    AttackSequenceDefinition definition_;
    float elapsed_ = 0;
    size_t next_ = 0, executed_ = 0, skipped_ = 0;
    bool running_ = false;
    bool stoppedByCondition_ = false;
};
