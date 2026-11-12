#include "CopyAbilitySupport.h"
#include <array>
#include <unordered_map>
#include "WindCopySettings.h"
#include "game/actor/enemy/boss/magma/EnemyMagmaSlime.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"
#include "game/actor/enemy/heavy/EnemyGiantSlime.h"

namespace {
using namespace CopyAbilityDetail;

// 左クリック竜巻、風跳び、独立して進む小型竜巻の状態を管理する。
// 移動はPlayerAttackMovement経由で既存Motorへ渡し、描画はMeshEffectManagerの専用スコープで管理する。
// PlayerCopyAbilityControllerが所有する風攻撃の実装。入力はProcessInput、進行はUpdate、一時表示の回収はCancelが担当する。
class WindSlimeCopyAbility final : public ICopyAbilitySession {
  public:
    explicit WindSlimeCopyAbility(EnemyAttackProfile profile) : ICopyAbilitySession(std::move(profile)) {
        // 各部品を一度だけ生成して保持し、表示時間と姿勢だけを評価します。
        for (auto& scope : tornadoScopes_)
            scope = MeshEffectManager::GetInstance()->CreateEffectScope();
        windScope_ = MeshEffectManager::GetInstance()->CreateEffectScope();
        for (auto& scope : liftLaunchScopes_)
            scope = MeshEffectManager::GetInstance()->CreateEffectScope();
        for (auto& scope : vortexScopes_)
            scope = MeshEffectManager::GetInstance()->CreateEffectScope();
    }

    ~WindSlimeCopyAbility() override {
        // コピー能力の破棄だけでも竜巻のメッシュ演出を止める。
        ClearTornadoEffects();
        ClearWindEffects();
    }

    const char* GetStateLabel() const override {
        // 竜巻の実行中と再使用待ちを区別して操作案内へ渡す。
        if (liftPhase_ == LiftPhase::Rising) return "風跳び・上昇";
        if (liftPhase_ == LiftPhase::Gliding) return "風跳び・緩降下";
        if (VortexCanHit()) return "小型竜巻を展開中";
        if (tornadoActive_) return "竜巻攻撃";
        return tornadoCooldown_ > 0.0f ? "竜巻・復帰待ち" : "風能力";
    }

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        // 同時入力は左・右・Eの順で一つを選び、風跳びから左攻撃へ即座に接続する。
        primaryHeld_ = input.primaryHeld;
        if (input.primaryTriggered && tornadoCooldown_ <= 0.0f && !tornadoActive_) {
            EndWindLift(player);
            actionMotionTimer_ = 0.0f;
            tornadoActive_ = true;
            tornadoTime_ = 0.0f;
            effectTimer_ = pushTimer_ = 0.0f;
            player.PlaySlimeAbilityMotion("player_ability_wind_tornado", true, 1.0f, 0.04f, 0.10f);
        } else if (input.secondaryTriggered && liftPhase_ == LiftPhase::Idle && liftCooldown_ <= 0.0f &&
                   player.IsGrounded() && player.GetVelocity().y <= 0.0f) {
            BeginWindLift(player);
        } else if (input.specialTriggered && vortexPhase_ == VortexPhase::Idle && vortexCooldown_ <= 0.0f) {
            BeginVortex(player);
        }
    }

    PlayerAttackMovement GetAttackMovement() const override {
        PlayerAttackMovement movement;
        if (liftPhase_ == LiftPhase::Gliding) {
            movement.maxFallSpeedOverride = liftSettings_.glideFallSpeed;
        }
        return movement;
    }

    void NotifyDamageStarted(Player& player) override {
        // 被弾中に能力がノックバック速度を上書きしない。空中使用権は被弾では戻さない。
        EndWindLift(player);
        StopTornado(player);
        StopVortex();
    }

    void Update(Player& player, float deltaTime) override {
        if (deltaTime <= 0.0f) return;
        liftCooldown_ = std::max(0.0f, liftCooldown_ - deltaTime);
        vortexCooldown_ = std::max(0.0f, vortexCooldown_ - deltaTime);
        tornadoCooldown_ = std::max(0.0f, tornadoCooldown_ - deltaTime);
        effectTimer_ -= deltaTime;
        pushTimer_ -= deltaTime;
        actionMotionTimer_ = std::max(0.0f, actionMotionTimer_ - deltaTime);
        UpdateWindLift(player, deltaTime);
        UpdateVortex(player, deltaTime);
        if (actionMotionTimer_ > 0.0f) return;

        if (!tornadoActive_)
            return;
        tornadoTime_ += deltaTime;
        const engine::Vector3 center = player.GetWorldPosition() + engine::Vector3{0.0f, 1.0f, 0.0f};

        if ((!primaryHeld_ && tornadoTime_ >= 0.20f) || tornadoTime_ >= 1.60f) {
            // 終了時だけ外へ吹き飛ばす。持続中は弱く巻き込み、攻撃範囲から逃がさない。
            DamageSphere(player, center, 3.3f, 8.0f, DamageType::Physical, 13.0f, 6.0f);
            if (!tornadoActive_) return; // 同期ダメージ通知で終了した場合は演出を再開しない。
            StopTornado(player);
            SpawnEffect("Resources/json/effect/effect_player_wind_tornado_finish.json",
                        player.GetWorldPosition() + engine::Vector3{0.0f, 0.25f, 0.0f}, {0.0f, tornadoTime_ * 15.0f, 0.0f});
            tornadoCooldown_ = 0.65f;
            actionMotionTimer_ = 0.24f;
            player.TriggerSlimeImpulse({1.7f, 0.55f, 1.7f}, 0.15f);
            return;
        }

        tornadoVisualAge_ += deltaTime;
        if (effectTimer_ <= 0.0f) {
            // 従来の発生間隔で姿勢とUVの起点を更新し、生成し直した見た目を保ちます。
            tornadoVisualAge_ = 0.0f;
            tornadoSampleTime_ = tornadoTime_;
            tornadoSamplePosition_ = player.GetWorldPosition();
            effectTimer_ = 0.045f;
        }

        auto* effects = MeshEffectManager::GetInstance();
        for (size_t i = 0; i < tornadoScopes_.size(); ++i) {
            const bool inner = i >= 5;
            const float layer = static_cast<float>(inner ? i - 5 : i);
            if (!tornadoVisualsReady_)
                effects->StartTimelineEffect(tornadoScopes_[i], inner
                    ? "Resources/json/effect/effect_player_wind_tornado_inner.json"
                    : "Resources/json/effect/effect_player_wind_tornado.json");

            const float radius = inner ? 1.6f + layer * 0.4f : 1.10f + layer * 0.45f;
            const auto position = tornadoSamplePosition_ + engine::Vector3{
                0.0f, inner ? 0.45f + layer * 0.75f : 0.18f + layer * 0.64f, 0.0f};
            const engine::Vector3 rotation = inner
                ? engine::Vector3{0.15f, -tornadoSampleTime_ * 23.0f + layer * kPi, 0.08f}
                : engine::Vector3{0.0f, tornadoSampleTime_ * 18.0f + layer * 1.8f, 0.0f};
            // 負の寿命指定ではJSONの寿命を使い、設定したスケールアニメーションも残します。
            effects->EvaluateTimelineEffect(tornadoScopes_[i], tornadoVisualAge_, -1.0f,
                position, rotation, {radius, inner ? 1.0f : 0.95f, radius}, true);
        }
        tornadoVisualsReady_ = true;

        if (pushTimer_ <= 0.0f) {
            DamageSphere(player, center, 3.0f, 3.0f, DamageType::Physical, -2.0f, 1.0f);
            if (!tornadoActive_) return;
            pushTimer_ = 0.20f;
        }
    }

    void Cancel(Player& player) override {
        EndWindLift(player);
        StopTornado(player);
        StopVortex();
        ClearWindEffects();
        player.ClearSlimeAbilityMotion();
        primaryHeld_ = false;
        actionMotionTimer_ = 0.0f;
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
    }

  private:
    // 接地中に一度だけ鉛直初速を与え、その後は通常の重力と衝突解決へ任せる。
    void BeginWindLift(Player& player) {
        StopTornado(player);
        liftSettings_ = WindCopySettings::Get();
        liftSettings_.Sanitize();
        liftPhase_ = LiftPhase::Rising;
        liftGlideRemaining_ = liftSettings_.glideDuration;
        liftVfxTimer_ = 0.0f;
        liftAge_ = 0.0f;
        actionMotionTimer_ = 0.0f;

        const auto position = player.GetWorldPosition();
        // 頭上に足場がある場合も、突風を接地中の足元から出す。
        const engine::Vector3 base{position.x, player.GetAabb().min.y + 0.07f, position.z};
        // 水平速度は踏み切り時だけゼロにし、次の更新から通常の空中操作に戻す。
        player.SetVelocity({0.0f, player.GetJumpPower() * liftSettings_.liftJumpScale, 0.0f});
        player.SetGrounded(false);
        player.StartEvasionInvincibility(liftSettings_.invincibleTime);
        player.PlaySlimeAbilityMotion("player_ability_wind_lift", false, 1.0f, 0.0f, 0.12f);
        SpawnScopedEffect(windScope_, "Resources/json/effect/effect_player_wind_lift_ring.json", base);
        StartWindLaunch(player, base);
        EmitPreset("player_wind_lift_burst", base);
    }

    void EndWindLift(Player& player) {
        if (liftPhase_ == LiftPhase::Idle) return;
        liftPhase_ = LiftPhase::Idle;
        liftCooldown_ = liftSettings_.liftCooldown;
        StopWindLaunch();
        player.StopSlimeAbilityMotion(0.12f);
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
        if (auto* effects = MeshEffectManager::GetInstance()) effects->StopEffectScope(windScope_);
    }

    // 物理更新後のY速度で頂点を検出する。天井で上向き速度が止まった時も下降へ移る。
    void UpdateWindLift(Player& player, float frameDeltaTime) {
        if (liftPhase_ == LiftPhase::Idle) return;
        const float verticalSpeed = player.GetVelocity().y;
        const bool landed = player.IsGrounded() && verticalSpeed <= 0.0f;
        // 着地したフレームにキノコ等で跳ね返った場合も、前の緩降下は終了する。
        const bool bounced = liftPhase_ == LiftPhase::Gliding && verticalSpeed > 0.0f;
        if (landed || bounced) {
            EndWindLift(player);
            return;
        }

        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, player.GetForwardDirection());
        if (liftPhase_ == LiftPhase::Rising) {
            if (verticalSpeed > 0.0f) {
                UpdateWindLaunch(player, frameDeltaTime);
                return;
            }
            StopWindLaunch();
            liftPhase_ = LiftPhase::Gliding;
            liftVfxTimer_ = 0.0f;
            liftAge_ = 0.0f;
            player.StopSlimeAbilityMotion(0.12f);
        } else {
            liftGlideRemaining_ -= frameDeltaTime;
            if (liftGlideRemaining_ <= 0.0f) {
                EndWindLift(player);
                return;
            }
        }

        liftAge_ += frameDeltaTime;
        const float poseStrength = std::min(SmoothStep(liftAge_ / 0.16f), SmoothStep(liftGlideRemaining_ / 0.22f));
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::SlowFall, poseStrength);

        liftVfxTimer_ -= frameDeltaTime;
        if (liftVfxTimer_ > 0.0f) return;
        liftVfxTimer_ = 0.14f;
        const auto bounds = player.GetModelWorldAabb();
        const auto center = (bounds.min + bounds.max) * 0.5f;
        const float radius = std::max(0.42f, (bounds.max.x - bounds.min.x) * 0.52f);
        const float yaw = player.GetMoveYaw() + liftAge_ * 2.4f;
        const engine::Vector3 offset{std::cos(yaw) * radius, -0.38f, std::sin(yaw) * radius};
        const engine::Vector3 opposite{-offset.x, offset.y, -offset.z};
        const engine::Vector3 up{0.0f, 1.0f, 0.0f};
        const char* ribbon = "Resources/json/effect/effect_player_wind_lift_glide.json";
        SpawnScopedEffect(windScope_, ribbon, center + offset, {0.0f, -yaw, 0.0f});
        SpawnScopedEffect(windScope_, ribbon, center + opposite, {0.0f, -yaw + kPi, 0.0f});
        EmitDirectedPreset("player_wind_lift_glide", center + offset, up);
        EmitDirectedPreset("player_wind_lift_glide", center + opposite, up);
    }

    // 左クリックの外周と内周のプリセットを、命中判定を持たない離陸演出として再生する。
    void StartWindLaunch(const Player& player, const engine::Vector3& base) {
        liftLaunchBase_ = base;
        liftLaunchStartY_ = player.GetTranslate().y;
        liftLaunchAge_ = 0.0f;
        if (auto* effects = MeshEffectManager::GetInstance()) {
            for (std::size_t i = 0; i < liftLaunchScopes_.size(); ++i) {
                const bool inner = i == liftLaunchScopes_.size() - 1;
                effects->StartTimelineEffect(liftLaunchScopes_[i], inner
                    ? "Resources/json/effect/effect_player_wind_tornado_inner.json"
                    : "Resources/json/effect/effect_player_wind_tornado.json");
            }
        }
    }

    // 根元を踏み切り位置に残し、上側の風帯を実際に上がった高さへ追従させる。
    void UpdateWindLaunch(const Player& player, float frameDeltaTime) {
        if (liftLaunchAge_ >= kLiftLaunchDuration) return;
        if (liftLaunchAge_ + frameDeltaTime >= kLiftLaunchDuration) {
            StopWindLaunch();
            return;
        }
        liftLaunchAge_ += frameDeltaTime;

        auto* effects = MeshEffectManager::GetInstance();
        if (!effects) return;
        const float climb = std::clamp(player.GetTranslate().y - liftLaunchStartY_, 0.0f, 5.4f);
        const float burst = SmoothStep(liftLaunchAge_ / 0.070f);
        const float dissipate = SmoothStep((kLiftLaunchDuration - liftLaunchAge_) / 0.12f);
        const float envelope = (0.65f + 0.35f * burst) * dissipate;
        constexpr std::array<float, 4> kHeightFollow{0.0f, 0.29f, 0.66f, 0.54f};
        constexpr std::array<float, 4> kRadius{2.10f, 1.80f, 1.48f, 1.12f};
        constexpr std::array<float, 4> kHeight{1.08f, 1.72f, 1.70f, 1.42f};
        for (std::size_t i = 0; i < liftLaunchScopes_.size(); ++i) {
            const bool inner = i == liftLaunchScopes_.size() - 1;
            const float layer = static_cast<float>(i);
            const float radius = kRadius[i] * envelope;
            const auto position = liftLaunchBase_ + engine::Vector3{
                0.0f, kHeightFollow[i] * climb + layer * 0.14f, 0.0f};
            const float spin = liftLaunchAge_ * (inner ? -25.0f : 22.0f) + layer * 1.65f;
            effects->EvaluateTimelineEffect(liftLaunchScopes_[i], liftLaunchAge_, kLiftLaunchDuration,
                position, {0.0f, spin, 0.0f}, {radius, kHeight[i] * envelope, radius}, true);
        }
    }

    void StopWindLaunch() {
        if (liftLaunchAge_ >= kLiftLaunchDuration) return;
        liftLaunchAge_ = kLiftLaunchDuration;
        if (auto* effects = MeshEffectManager::GetInstance())
            for (const auto scope : liftLaunchScopes_)
                effects->StopEffectScope(scope);
    }

    void BeginVortex(Player& player) {
        vortexSettings_ = WindCopySettings::Get(); vortexSettings_.Sanitize();
        vortexDirection_ = NormalizePlanar(player.GetLockOnAttackDirection());
        vortexPosition_ = player.GetWorldPosition() + engine::Vector3{0,1.0f,0};
        vortexPhase_ = VortexPhase::Traveling;
        vortexPhaseRemaining_ = vortexSettings_.vortexLifetime;
        vortexAge_ = vortexVfxTimer_ = 0.0f;
        vortexNextHit_.clear();
        vortexCooldown_ = vortexSettings_.vortexCooldown;
        // 本体の縮みと足元の風を、離れて進む竜巻の起点にする。
        player.TriggerSlimeImpulse({1.22f, 0.72f, 1.22f}, 0.13f);
        auto* effects = MeshEffectManager::GetInstance();
        for (std::size_t layer = 0; layer < vortexScopes_.size(); ++layer) {
            const char* path = "Resources/json/effect/effect_player_wind_vortex_core.json";
            if (layer < kVortexOuterLayers)
                path = "Resources/json/effect/effect_player_wind_tornado.json";
            else if (layer == kVortexOuterLayers)
                path = "Resources/json/effect/effect_player_wind_tornado_inner.json";
            effects->StartTimelineEffect(vortexScopes_[layer], path);
        }
        EmitDirectedPreset("wind_slime_charge", vortexPosition_ - engine::Vector3{0.0f, 0.65f, 0.0f},
                           vortexDirection_ + engine::Vector3{0.0f, 0.65f, 0.0f}, 0.65f);
        EmitDirectedPreset("wind_slime_breath_stream", vortexPosition_, vortexDirection_, 0.75f);
    }

    // 被弾や解除では、飛行中・消散中にかかわらず表示を即座に回収する。
    void StopVortex() {
        vortexPhase_ = VortexPhase::Idle;
        vortexPhaseRemaining_ = 0.0f;
        vortexNextHit_.clear();
        if (auto* effects = MeshEffectManager::GetInstance()) {
            for (auto scope : vortexScopes_)
                effects->StopEffectScope(scope);
        }
    }

    bool VortexCanHit() const {
        return vortexPhase_ == VortexPhase::Traveling || vortexPhase_ == VortexPhase::Holding;
    }

    // 壁では滞留を省き、最後の有効座標で描画だけを残す。
    void BeginVortexDissipation(bool hitWall) {
        vortexPhase_ = VortexPhase::Dissipating;
        vortexPhaseRemaining_ = kVortexDissipationDuration;
        const auto direction = hitWall ? engine::Vector3{0.0f, 1.0f, 0.0f} : vortexDirection_;
        EmitDirectedPreset("wind_slime_breath_stream", vortexPosition_, direction, 0.65f);
    }

    // 判定中心にクリスタル由来の風帯とE専用の芯を重ねる。releaseは消散の進行率（0～1）。
    void DrawVortexLayers(float release) {
        auto* effects = MeshEffectManager::GetInstance();
        const float spinTime = vortexAge_ + release * kVortexDissipationDuration;
        const float dissolve = SmoothStep(release);
        const float height = vortexSettings_.vortexVisualHeight;
        const float width = vortexSettings_.vortexRadius * vortexSettings_.vortexVisualWidth;
        const float sinceArrival = std::max(0.0f, vortexAge_ - vortexSettings_.vortexLifetime);
        const float tighten = 1.0f - 0.16f * SmoothStep(sinceArrival / 0.10f);
        const float shrink = (1.0f + 0.28f * dissolve) * (1.0f - dissolve);

        for (std::size_t layer = 0; layer < vortexScopes_.size(); ++layer) {
            const bool core = layer == kVortexOuterLayers + 1;
            const bool inner = layer == kVortexOuterLayers;
            const float level = (inner || core) ? 0.5f : static_cast<float>(layer) / (kVortexOuterLayers - 1);
            const float grow = std::max(0.01f, SmoothStep((vortexAge_ - 0.065f * level) / 0.18f));
            const float radiusScale = core ? 0.52f : inner ? 0.68f : 0.48f + level * 0.47f;
            const float radius = width * radiusScale * grow * tighten * shrink;
            const float lift = height * (0.15f + level * 0.67f) * grow +
                               dissolve * (0.12f + level * 0.32f);
            const auto position = vortexPosition_ + engine::Vector3{0.0f, lift - 1.0f, 0.0f};
            const float yaw = spinTime * vortexSettings_.vortexSpinRate * (inner ? -1.35f : 1.0f) +
                              static_cast<float>(layer) * 1.65f;
            const float heightScale = core ? 0.66f : inner ? 0.69f : 0.53f;
            const float bandHeight = height * heightScale * grow * (1.0f - dissolve);
            effects->EvaluateTimelineEffect(vortexScopes_[layer], spinTime, 5.0f,
                position, {0.0f, yaw, dissolve * 0.32f}, {radius, bandHeight, radius});
        }
    }

    PhysicsQueryFilter MakeVortexTerrainFilter(Player& player) const {
        PhysicsQueryFilter terrain;
        terrain.mask = CollisionCategories::Get("ground");
        terrain.ignoredObject = &player;
        return terrain;
    }

    // ワールド距離distanceだけ進む前に壁を調べる。停止した場合はfalseを返す。
    bool AdvanceVortex(Player& player, float distance) {
        auto wall = MakeVortexTerrainFilter(player);
        wall.refineSphereCastHit = true;
        wall.acceptsSphereContact = [](const engine::Object3D*, const CollisionInfo& c) {
            return std::abs(c.normal.y) < 0.65f;
        };
        auto* collisions = CollisionManager::GetInstance();
        if (collisions->SphereCast(vortexPosition_, 0.4f, vortexDirection_, distance, wall).isHit)
            return false;

        vortexPosition_ += vortexDirection_ * distance;
        const auto floor = collisions->Raycast(
            vortexPosition_ + engine::Vector3{0,0.5f,0}, {0,-1,0}, 2.0f, wall);
        if (floor.isHit && floor.normal.y > 0.65f)
            vortexPosition_.y = floor.hitPoint.y + 1.0f;
        return true;
    }

    // 実体の型でボスを除外し、既存のボス分類と封印結晶も引き寄せ対象から外す。
    static bool CanPullVortexTarget(const BaseEnemy& target) {
        if (dynamic_cast<const EnemyGiantSlime*>(&target) || dynamic_cast<const EnemyPrismSlime*>(&target) ||
            dynamic_cast<const EnemyMagmaSlime*>(&target) || dynamic_cast<const EnemyFalseKingSlime*>(&target))
            return false;
        const auto& type = target.GetEnemyType();
        return type != "GiantSlime" && type != "PrismSlime" && type != "MagmaSlime" &&
               type != "FalseKingSlime" && type != "FalseKingSeal";
    }

    // 範囲内にいる間は短い外力を更新し、離れた後はMotor側で減衰させる。
    void ApplyVortexPull(BaseEnemy& enemy, float pullScale) const {
        if (enemy.IsDormant() || !CanPullVortexTarget(enemy)) return;

        auto offset = vortexPosition_ - enemy.GetWorldPosition();
        offset.y = 0.0f;
        const float distance = engine::Math::Length(offset);
        const float speed = vortexSettings_.vortexPull * pullScale;
        constexpr float kImpulseSeconds = 0.075f;
        if (speed <= 0.0f || distance <= 0.10f) return;

        const auto horizontal = offset * (std::min(speed, distance / kImpulseSeconds) / distance);
        enemy.ApplyExternalImpulse({horizontal.x, enemy.GetVelocity().y, horizontal.z}, kImpulseSeconds);
    }

    // 足元から風帯上端までを円柱で照会し、同じ敵の複数Colliderは一体分として処理する。
    void UpdateVortexContacts(Player& player, float pullScale) {
        const float radius = vortexSettings_.vortexRadius;
        const float bottom = vortexPosition_.y - 1.0f;
        const float height = vortexSettings_.vortexVisualHeight;
        const auto center = engine::Vector3{vortexPosition_.x, bottom + height * 0.5f, vortexPosition_.z};
        const auto extent = engine::Vector3{radius, height * 0.5f, radius};
        Transform transform;
        transform.translate = center;
        transform.UpdateMatrix();
        Collider cylinder(&transform);
        ColliderConfig config;
        config.type = ColliderType::Cylinder;
        config.size = extent;
        cylinder.SetConfig(config);

        auto* collisions = CollisionManager::GetInstance();
        const auto wall = MakeVortexTerrainFilter(player);
        PhysicsQueryFilter enemies;
        enemies.mask = CollisionCategories::Get("enemy");
        enemies.ignoredObject = &player;
        std::unordered_set<BaseEnemy*> processed;
        for (const auto& hit : collisions->OverlapAabb({center - extent, center + extent}, enemies)) {
            auto* enemy = static_cast<BaseEnemy*>(FindEnemyRoot(hit.object));
            if (!enemy || processed.contains(enemy) || enemy->IsDead() ||
                enemy->IsCarried() || !enemy->GetParameters() || enemy->GetParameters()->hp <= 0.0f ||
                !cylinder.CheckCollision(hit.object->GetCollider()).isColliding)
                continue;

            // 触れたColliderの高さを使い、低い敵や空中の敵に根元から誤って射線を引かない。
            const auto bounds = hit.object->GetCollider()->GetAabb();
            // 箱形Colliderに対する円柱近似で、渦の横幅を外れた角に当たらないようにする。
            const float dx = std::clamp(vortexPosition_.x, bounds.min.x, bounds.max.x) - vortexPosition_.x;
            const float dz = std::clamp(vortexPosition_.z, bounds.min.z, bounds.max.z) - vortexPosition_.z;
            if (dx * dx + dz * dz > radius * radius) continue;
            auto aim = (bounds.min + bounds.max) * 0.5f;
            aim.y = std::clamp(vortexPosition_.y, bounds.min.y, bounds.max.y);
            const auto offset = aim - vortexPosition_;
            const float length = engine::Math::Length(offset);
            if (length > 0.001f &&
                collisions->Raycast(vortexPosition_, offset * (1.0f / length), length, wall).isHit)
                continue;

            processed.insert(enemy);
            ApplyVortexPull(*enemy, pullScale);
            if (vortexSettings_.vortexDamage <= 0.0f) continue;

            // 初めて触れた敵は即時命中。飛行から滞留へ移っても再命中時刻は持ち越す。
            auto& nextHit = vortexNextHit_[enemy];
            if (vortexAge_ < nextHit) continue;
            nextHit = vortexAge_ + vortexSettings_.vortexHitInterval;
            DamageEnemy(player, enemy, vortexSettings_.vortexDamage, DamageType::Physical, {});
            // 同期通知で被弾やコピー解除が起きた場合は、次の対象を処理しない。
            if (!VortexCanHit()) return;
        }
    }

    // らせんの放出点から上向きの風筋を流し、滞留中は根元から巻き上げる。
    void EmitVortexWind(float frameDeltaTime) {
        vortexVfxTimer_ -= frameDeltaTime;
        if (vortexVfxTimer_ > 0.0f) return;
        const bool holding = vortexPhase_ == VortexPhase::Holding;
        const float angle = vortexAge_ * vortexSettings_.vortexSpinRate;
        const float radius = vortexSettings_.vortexRadius * vortexSettings_.vortexVisualWidth *
                             (holding ? 0.46f : 0.72f);
        const float height = vortexSettings_.vortexVisualHeight;
        for (int i = 0; i < 2; ++i) {
            const float azimuth = angle + i * 3.14159265f;
            const float lift = holding ? 0.2f : 0.2f + height * std::fmod(vortexAge_ * 1.7f + i * 0.5f, 1.0f);
            const auto position = vortexPosition_ + engine::Vector3{
                std::cos(azimuth) * radius, lift - 1.0f, std::sin(azimuth) * radius};
            EmitDirectedPreset("player_wind_vortex_rise", position,
                               {-std::sin(azimuth), 1.65f, std::cos(azimuth)}, holding ? 1.1f : 0.9f);
        }
        vortexVfxTimer_ = holding ? 0.040f : 0.060f;
    }

    void UpdateVortex(Player& player, float frameDeltaTime) {
        // 段階の終端を跨がない刻みで進め、余った時間を次の段階へ渡す。
        float remaining = frameDeltaTime;
        float visualDt = 0.0f;
        while (remaining > 0.0f && vortexPhase_ != VortexPhase::Idle) {
            const float step = std::min({remaining, vortexPhaseRemaining_, 1.0f / 60.0f});
            remaining -= step;
            vortexPhaseRemaining_ -= step;

            if (vortexPhase_ == VortexPhase::Dissipating) {
                if (vortexPhaseRemaining_ <= 0.0f) StopVortex();
                continue;
            }

            const bool traveling = vortexPhase_ == VortexPhase::Traveling;
            if (traveling && !AdvanceVortex(player, vortexSettings_.vortexSpeed * step)) {
                BeginVortexDissipation(true);
                break;
            }

            vortexAge_ += step;
            visualDt += step;
            UpdateVortexContacts(player, traveling ? 1.0f : vortexSettings_.vortexHoldPullScale);
            if (!VortexCanHit()) return;

            if (vortexPhaseRemaining_ <= 0.0f) {
                if (traveling && vortexSettings_.vortexHoldDuration > 0.0f) {
                    vortexPhase_ = VortexPhase::Holding;
                    vortexPhaseRemaining_ = vortexSettings_.vortexHoldDuration;
                    vortexVfxTimer_ = 0.0f;
                } else {
                    BeginVortexDissipation(false);
                }
            }
        }

        if (VortexCanHit()) {
            DrawVortexLayers(0.0f);
            EmitVortexWind(visualDt);
        } else if (vortexPhase_ == VortexPhase::Dissipating) {
            DrawVortexLayers(1.0f - vortexPhaseRemaining_ / kVortexDissipationDuration);
        }
    }

    void ClearWindEffects() {
        StopWindLaunch();
        StopVortex();
        if (auto* effects = MeshEffectManager::GetInstance()) effects->StopEffectScope(windScope_);
    }

    void StopTornado(Player& player) {
        // 竜巻の停止とモーションの停止を同時に行い、再使用待ちへ入れる。
        if (tornadoActive_) {
            tornadoActive_ = false;
            tornadoCooldown_ = 0.65f;
            ClearTornadoEffects();
            player.StopSlimeAbilityMotion(0.10f);
        }


    }

    void ClearTornadoEffects() {
        // 能力専用スコープを閉じ、他のプレイヤー演出へ影響させない。
        if (auto* effects = MeshEffectManager::GetInstance())
            for (auto scope : tornadoScopes_)
                effects->StopEffectScope(scope);
        tornadoVisualsReady_ = false;
        tornadoVisualAge_ = 0.0f;
    }

    std::array<MeshEffectManager::EffectScopeId, 7> tornadoScopes_{};
    engine::Vector3 tornadoSamplePosition_{};
    float tornadoSampleTime_ = 0.0f;
    float tornadoVisualAge_ = 0.0f;
    bool tornadoVisualsReady_ = false;
    bool tornadoActive_ = false;
    float tornadoTime_ = 0.0f;
    float tornadoCooldown_ = 0.0f;
    WindCopySettings liftSettings_{}, vortexSettings_{}; // 発動時の設定。Editor操作中も実行中の値を固定する。
    static constexpr std::size_t kVortexOuterLayers = 3;
    static constexpr float kVortexDissipationDuration = 0.22f; // 判定終了から表示を回収するまでの秒数。
    enum class VortexPhase { Idle, Traveling, Holding, Dissipating };
    VortexPhase vortexPhase_ = VortexPhase::Idle;
    float vortexPhaseRemaining_ = 0.0f; // 現在の段階の残り秒数。
    std::array<MeshEffectManager::EffectScopeId, kVortexOuterLayers + 2> vortexScopes_{};
    MeshEffectManager::EffectScopeId windScope_{};
    static constexpr float kLiftLaunchDuration = 0.34f; // 打ち上げ竜巻の表示時間（秒）。
    std::array<MeshEffectManager::EffectScopeId, 4> liftLaunchScopes_{};
    engine::Vector3 liftLaunchBase_{}; // ワールド座標の踏み切り位置。
    float liftLaunchStartY_ = 0.0f; // 高さ追従に使う踏み切り時のプレイヤー座標。
    float liftLaunchAge_ = kLiftLaunchDuration; // 表示開始からの秒数。寿命と同値なら停止中。
    engine::Vector3 vortexDirection_{0,0,1}, vortexPosition_{}; // 方向と竜巻中心はワールド座標。
    enum class LiftPhase { Idle, Rising, Gliding };
    LiftPhase liftPhase_ = LiftPhase::Idle;
    float liftGlideRemaining_ = 0.0f; // 頂点に達してからの緩降下残り時間（秒）。
    float liftCooldown_ = 0.0f; // 技終了からの再使用待ち（秒）。
    float liftAge_ = 0.0f, liftVfxTimer_ = 0.0f; // 緩降下の経過時間と次の風筋までの秒数。
    float vortexCooldown_ = 0, vortexAge_ = 0, vortexVfxTimer_ = 0; // 小型竜巻の時計（秒）。
    std::unordered_map<const BaseEnemy*, float> vortexNextHit_; // 発動中だけ保持する敵ごとの次回命中時刻（秒）。
    float effectTimer_ = 0, pushTimer_ = 0, actionMotionTimer_ = 0;
    bool primaryHeld_ = false;
};
} // namespace

namespace CopyAbilityDetail {
std::unique_ptr<ICopyAbilitySession> CreateWindSlimeCopyAbility(const EnemyAttackProfile& profile) {
    // コピー獲得ごとに風跳びと二種類の竜巻の状態を初期化する。
    return std::make_unique<WindSlimeCopyAbility>(profile);
}
} // namespace CopyAbilityDetail
