#include "CopyAbilitySupport.h"

namespace {
using namespace CopyAbilityDetail;
// 竜巻・上昇・緩やかな落下を制御する。空中の追加上昇は消費状態を持ち、接地で再使用可能にする。
class WindSlimeCopyAbility final : public ICopyAbilitySession {
  public:
    explicit WindSlimeCopyAbility(EnemyAttackProfile profile) : ICopyAbilitySession(std::move(profile)) {
        // 竜巻の再生成を一つの演出スコープへ束ね、終了時に残像を回収する。
        scope_ = MeshEffectManager::GetInstance()->CreateEffectScope();
    }
    ~WindSlimeCopyAbility() override {
        // コピー能力の破棄だけでも竜巻のメッシュ演出を止める。
        ClearTornadoEffects();
    }
    const char* GetStateLabel() const override {
        // 竜巻の実行中と再使用待ちを区別して操作案内へ渡す。
        return tornadoActive_ ? "竜巻攻撃" : (tornadoCooldown_ > 0.0f ? "竜巻・復帰待ち" : "風能力");
    }

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        // 竜巻、接地からの飛翔、空中で一度使える上昇を別条件で開始する。
        primaryHeld_ = input.primaryHeld;
        if (input.primaryTriggered && !soarActive_ && actionMotionTimer_ <= 0.0f && tornadoCooldown_ <= 0.0f &&
            !tornadoActive_) {
            tornadoActive_ = true;
            tornadoTime_ = 0.0f;
            effectTimer_ = pushTimer_ = 0.0f;
            player.PlaySlimeAbilityMotion("player_ability_wind_tornado", true, 1.0f, 0.04f, 0.10f);
        }
        if (input.secondaryTriggered && player.IsGrounded() && !soarActive_ && soarCooldown_ <= 0.0f) {
            BeginSoar(player);
        } else if (input.specialTriggered && !updraftConsumed_ && updraftCooldown_ <= 0.0f) {
            Updraft(player);
        }
    }

    void Update(Player& player, float deltaTime) override {
        // 飛翔後の落下制限を着地まで保ち、竜巻は押し続けた時間で終了を決める。
        updraftCooldown_ = std::max(0.0f, updraftCooldown_ - deltaTime);
        tornadoCooldown_ = std::max(0.0f, tornadoCooldown_ - deltaTime);
        soarCooldown_ = std::max(0.0f, soarCooldown_ - deltaTime);
        effectTimer_ -= deltaTime;
        pushTimer_ -= deltaTime;
        actionMotionTimer_ = std::max(0.0f, actionMotionTimer_ - deltaTime);
        if (!soarActive_ && player.IsGrounded() && player.GetVelocity().y <= 0.1f) {
            updraftConsumed_ = false;
        }
        if (soarActive_) {
            Vector3 velocity = player.GetVelocity();
            if (velocity.y < 0.0f) {
                if (!descending_) {
                    player.PlaySlimeAbilityMotion("player_ability_wind_slow_fall", true, 1.0f, 0.10f, 0.10f);
                    const Vector3 transition = player.GetWorldPosition() + Vector3{0.0f, 0.58f, 0.0f};
                    SpawnEffect("Resources/json/effect/effect_player_wind_slow_fall.json", transition);
                    EmitDirectedPreset("player_wind_dash", transition, Vector3{0.0f, 1.0f, 0.0f}, 0.92f);
                    player.TriggerSlimeImpulse({2.24f, 0.72f, 2.24f}, 0.12f);
                    effectTimer_ = 0.11f;
                }
                descending_ = true;
                velocity.y = std::max(velocity.y, -5.2f);
                player.SetVelocity(velocity);
                player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::SlowFall);
                if (effectTimer_ <= 0.0f) {
                    const Vector3 position = player.GetWorldPosition() + Vector3{0.0f, 0.58f, 0.0f};
                    SpawnEffect("Resources/json/effect/effect_player_wind_slow_fall.json", position);
                    EmitDirectedPreset("player_wind_dash", position, Vector3{0.0f, 1.0f, 0.0f}, 0.78f);
                    effectTimer_ = 0.11f;
                }
            }
            player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, soarDirection_);
            // 落下速度低下は時間切れで空中解除せず、着地するまで維持します。
            if (descending_ && player.IsGrounded()) {
                FinishSoar(player);
            }
            return;
        }
        if (actionMotionTimer_ > 0.0f) {
            return;
        }
        if (!tornadoActive_)
            return;
        tornadoTime_ += deltaTime;
        const Vector3 center = player.GetWorldPosition() + Vector3{0.0f, 1.0f, 0.0f};
        if ((!primaryHeld_ && tornadoTime_ >= 0.20f) || tornadoTime_ >= 1.60f) {
            // 終了時だけ外へ吹き飛ばす。持続中は弱く巻き込み、攻撃範囲から逃がさない。
            DamageSphere(player, center, 3.3f, 8.0f, DamageType::Physical, 13.0f, 6.0f);
            StopBreathMotion(player);
            SpawnEffect("Resources/json/effect/effect_player_wind_tornado_finish.json",
                        player.GetWorldPosition() + Vector3{0.0f, 0.25f, 0.0f}, {0.0f, tornadoTime_ * 15.0f, 0.0f});
            tornadoCooldown_ = 0.65f;
            actionMotionTimer_ = 0.24f;
            player.TriggerSlimeImpulse({1.7f, 0.55f, 1.7f}, 0.15f);
            return;
        }
        if (effectTimer_ <= 0.0f) {
            ClearTornadoEffects();
            for (int i = 0; i < 5; ++i) {
                const float radius = 1.10f + i * 0.45f;
                SpawnScopedEffect(scope_, "Resources/json/effect/effect_player_wind_tornado.json",
                                  player.GetWorldPosition() + Vector3{0.0f, 0.18f + i * 0.64f, 0.0f},
                                  {0.0f, tornadoTime_ * 18.0f + i * 1.8f, 0.0f}, {radius, 0.95f, radius});
            }
            // 太い主流の内側を細い逆流が走り、単なる輪の積み重ねに見せない。
            for (int i = 0; i < 2; ++i) {
                SpawnScopedEffect(scope_, "Resources/json/effect/effect_player_wind_tornado_inner.json",
                                  player.GetWorldPosition() + Vector3{0.0f, 0.45f + i * 0.75f, 0.0f},
                                  {0.15f, -tornadoTime_ * 23.0f + i * kPi, 0.08f},
                                  {1.6f + i * 0.4f, 1.0f, 1.6f + i * 0.4f});
            }
            effectTimer_ = 0.045f;
        }
        if (pushTimer_ <= 0.0f) {
            DamageSphere(player, center, 3.0f, 3.0f, DamageType::Physical, -2.0f, 1.0f);
            pushTimer_ = 0.20f;
        }
    }

    void Cancel(Player& player) override {
        // コピー解除後に風の姿勢や継続エフェクトが残らないよう状態を戻す。
        tornadoActive_ = false;
        ClearTornadoEffects();
        player.ClearSlimeAbilityMotion();
        primaryHeld_ = false;
        breathMotionActive_ = false;
        soarActive_ = false;
        descending_ = false;
        updraftConsumed_ = false;
        actionMotionTimer_ = 0.0f;
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
    }

  private:
    void Updraft(Player& player) {
        // 現在の上昇速度を下げずに加速し、再使用は次の接地まで封じる。
        StopBreathMotion(player);
        Vector3 velocity = player.GetVelocity();
        velocity.y = std::max(velocity.y, 18.0f);
        player.SetVelocity(velocity);
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, player.GetForwardDirection());
        player.PlaySlimeAbilityMotion("player_ability_wind_takeoff", false, 1.0f, 0.03f, 0.08f);
        player.TriggerSlimeImpulse({0.74f, 1.62f, 0.74f}, 0.22f);
        const Vector3 center = player.GetWorldPosition();
        SpawnEffect("Resources/json/effect/effect_player_wind_updraft_spiral.json",
                    center + Vector3{0.0f, 1.72f, 0.0f});
        SpawnEffect("Resources/json/effect/effect_player_wind_updraft_spiral_counter.json",
                    center + Vector3{0.0f, 1.18f, 0.0f});
        EmitPreset("player_wind_updraft", center);
        DamageSphere(player, center + Vector3{0.0f, 1.0f, 0.0f}, 4.2f, 10.0f, DamageType::Physical, 5.0f, 16.5f);
        actionMotionTimer_ = 0.52f;
        updraftConsumed_ = true;
        updraftCooldown_ = 1.0f;
    }

    void BeginSoar(Player& player) {
        // 水平移動が小さい時は現在の向きを使い、飛翔方向を確定する。
        StopBreathMotion(player);
        soarDirection_ = NormalizePlanar(player.GetVelocity());
        if (std::abs(player.GetVelocity().x) + std::abs(player.GetVelocity().z) < 1.2f) {
            soarDirection_ = NormalizePlanar(player.GetForwardDirection());
        }
        soarActive_ = true;
        descending_ = false;
        soarCooldown_ = 1.15f;
        effectTimer_ = 0.0f;
        player.SetVelocity({soarDirection_.x * 9.0f, std::max(player.GetVelocity().y, 19.5f), soarDirection_.z * 9.0f});
        player.SetMoveYaw(std::atan2(soarDirection_.x, soarDirection_.z));
        player.PlaySlimeAbilityMotion("player_ability_wind_takeoff", false, 1.0f, 0.03f, 0.08f);
        player.StartEvasionInvincibility(0.28f);
        player.TriggerSlimeImpulse({1.18f, 3.42f, 1.18f}, 0.20f);
        SpawnEffect("Resources/json/effect/effect_player_wind_soar_launch.json", player.GetWorldPosition());
        EmitPreset("player_wind_dash", player.GetWorldPosition());
    }

    void FinishSoar(Player& player) {
        // 着地を一度だけ処理し、落下用姿勢から着地モーションへ移す。
        if (!soarActive_)
            return;
        soarActive_ = false;
        descending_ = false;
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
        player.PlaySlimeAbilityMotion("player_ability_wind_land", false, 1.0f, 0.02f, 0.07f);
        actionMotionTimer_ = 0.36f;
        VFXSequencer::PlayOneShot("player_wind_soar_land_cue", player.GetWorldPosition());
        player.TriggerSlimeImpulse({2.68f, 1.26f, 2.68f}, 0.15f);
    }

    void StopBreathMotion(Player& player) {
        // 竜巻の停止とモーションの停止を同時に行い、再使用待ちへ入れる。
        if (tornadoActive_) {
            tornadoActive_ = false;
            tornadoCooldown_ = 0.65f;
            ClearTornadoEffects();
            player.StopSlimeAbilityMotion(0.10f);
        }
        if (!breathMotionActive_) {
            return;
        }
        player.StopSlimeAbilityMotion(0.08f);
        breathMotionActive_ = false;
    }

    void ClearTornadoEffects() {
        // 能力専用スコープを閉じ、他のプレイヤー演出へ影響させない。
        if (auto* effects = MeshEffectManager::GetInstance())
            effects->StopEffectScope(scope_);
    }

    MeshEffectManager::EffectScopeId scope_ = MeshEffectManager::kInvalidEffectScope;
    bool tornadoActive_ = false;
    float tornadoTime_ = 0.0f;
    float tornadoCooldown_ = 0.0f;
    Vector3 soarDirection_{0.0f, 0.0f, 1.0f};
    float updraftCooldown_ = 0.0f;
    float soarCooldown_ = 0.0f;
    float effectTimer_ = 0.0f;
    float pushTimer_ = 0.0f;
    float actionMotionTimer_ = 0.0f;
    bool primaryHeld_ = false;
    bool breathMotionActive_ = false;
    bool soarActive_ = false;
    bool descending_ = false;
    bool updraftConsumed_ = false;
};
} // namespace

namespace CopyAbilityDetail {
std::unique_ptr<ICopyAbilitySession> CreateWindSlimeCopyAbility(const EnemyAttackProfile& profile) {
    // コピー獲得ごとに飛翔の消費状態と竜巻時計を分ける。
    return std::make_unique<WindSlimeCopyAbility>(profile);
}
} // namespace CopyAbilityDetail
