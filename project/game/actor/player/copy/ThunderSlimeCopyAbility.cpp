#include "CopyAbilitySupport.h"

// 入力・更新・キャンセルの順序と調整値は元実装を維持します。
namespace {
using namespace CopyAbilityDetail;
class ThunderSlimeCopyAbility final : public ICopyAbilitySession {
public:
    explicit ThunderSlimeCopyAbility(EnemyAttackProfile profile) : ICopyAbilitySession(std::move(profile)) {
        sparkScope_ = MeshEffectManager::GetInstance()->CreateEffectScope();
    }
    ~ThunderSlimeCopyAbility() override { ClearSparkEffects(); }
    const char* GetStateLabel() const override {
        return charging_ ? (chargeTime_ >= 1.0f ? "帯電・溜め完了 / 離して発射" : "帯電中") : "雷能力";
    }

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        primaryHeld_ = input.primaryHeld;
        if (input.secondaryTriggered && evadeCooldown_ <= 0.0f) {
            StopCharge(player);
            Evade(player);
        }
        else if (input.specialTriggered && lineCooldown_ <= 0.0f && lineTimer_ <= 0.0f) {
            StopCharge(player);
            lineDirection_ = NormalizePlanar(player.GetLockOnAttackDirection());
            lineTimer_ = 0.50f;
            strikeIndex_ = 0;
            lineCooldown_ = 1.65f;
            lineHitTargets_.clear();
            player.PlaySlimeAbilityMotion("player_ability_thunder_cast", false, 1.0f, 0.04f, 0.08f);
            SpawnEffect("Resources/json/effect/effect_player_thunder_warning.json",
                player.GetWorldPosition() + lineDirection_ * 2.8f);
        }
        else if (input.primaryTriggered && dischargeCooldown_ <= 0.0f && lineTimer_ <= 0.0f && !charging_) {
            charging_ = true;
            chargeTime_ = sparkEffectTimer_ = sparkDamageTimer_ = 0.0f;
            sparkVisualTime_ = 0.0f;
            player.PlaySlimeAbilityMotion("player_ability_thunder_charge", true, 1.0f, 0.04f, 0.08f);
        }
    }

    void Update(Player& player, float deltaTime) override {
        lineCooldown_ = std::max(0.0f, lineCooldown_ - deltaTime);
        dischargeCooldown_ = std::max(0.0f, dischargeCooldown_ - deltaTime);
        evadeCooldown_ = std::max(0.0f, evadeCooldown_ - deltaTime);
        if (charging_) UpdateCharge(player, deltaTime);
        if (lineTimer_ <= 0.0f) return;
        lineTimer_ = std::max(0.0f, lineTimer_ - deltaTime);
        const float elapsed = 0.50f - lineTimer_;
        while (strikeIndex_ < 5 && elapsed >= 0.10f + static_cast<float>(strikeIndex_) * 0.075f) {
            const Vector3 ground = player.GetWorldPosition() + lineDirection_ * (2.8f + 2.2f * static_cast<float>(strikeIndex_));
            SpawnStrike(player, ground);
            ++strikeIndex_;
        }
    }

    void Cancel(Player& player) override {
        StopCharge(player);
        primaryHeld_ = false;
        player.ClearSlimeAbilityMotion();
        lineTimer_ = 0.0f;
        lineHitTargets_.clear();
    }

private:
    void ClearSparkEffects() {
        if (auto* effects = MeshEffectManager::GetInstance()) effects->StopEffectScope(sparkScope_);
    }
    void StopCharge(Player& player) {
        if (charging_) player.StopSlimeAbilityMotion(0.08f);
        charging_ = false;
        ClearSparkEffects();
    }
    void UpdateCharge(Player& player, float dt) {
        // 溜め量は上限で止めても、電撃の走る時間は止めない。
        sparkVisualTime_ += dt;
        chargeTime_ = std::min(1.0f, chargeTime_ + dt);
        sparkEffectTimer_ -= dt;
        sparkDamageTimer_ -= dt;
        const Vector3 center = player.GetWorldPosition() + Vector3{0.0f, 0.85f, 0.0f};
        if (!primaryHeld_) {
            // 弾の寿命と衝突は共通BulletManagerに任せ、能力解除後も安全に完走させます。
            const Vector3 direction = NormalizePlanar(player.GetLockOnAttackDirection());
            BulletVisualConfig visual;
            visual.materialType = 18;
            visual.effectType = 2.0f;
            visual.color = {1.0f, 0.95f, 0.3f, 1.0f};
            visual.emissive = 2.0f;
            visual.effectIntensity = 2.4f;
            visual.visualScale = 2.0f;
            visual.billboardScale = 1.2f;
            visual.trailPreset = "player_thunder_evade_sparks";
            visual.impactPreset = "player_thunder_strike_impact";
            BulletManager::GetInstance()->Fire(center, direction * 28.0f,
                kPlayerAttack, kEnemy | kAllSolid, "Primitives/sphere", 0.45f + chargeTime_ * 0.40f,
                0.9f, visual, 7.0f + 13.0f * chargeTime_, {}, DamageType::Electric);
            StopCharge(player);
            player.PlaySlimeAbilityMotion("player_ability_thunder_cast", false, 1.0f, 0.02f, 0.08f);
            player.TriggerSlimeImpulse({1.6f, 0.55f, 1.6f}, 0.16f);
            EmitPreset("player_thunder_discharge_burst", center);
            dischargeCooldown_ = 0.45f;
            return;
        }
        if (sparkEffectTimer_ <= 0.0f) {
            ClearSparkEffects();
            const float radius = 1.05f;
            const int arcCount = 3;
            // 規則的に回る飾りではなく、身体の表面で短い電撃が場所を変えて走る。
            const float phase = std::floor(sparkVisualTime_ * 10.0f);
            const float pulse = chargeTime_ >= 1.0f
                ? 0.5f + 0.5f * std::sin(sparkVisualTime_ * 8.0f) : 0.0f;
            for (int i = 0; i < arcCount; ++i) {
                const float angle = i * 2.15f + phase * 2.4f;
                const float flicker = std::sin(phase * 1.7f + i * 2.3f);
                SpawnScopedEffect(sparkScope_, "Resources/json/effect/effect_player_thunder_spark_arc.json",
                    center + Vector3{std::sin(angle) * radius, 0.10f + flicker * 0.20f, std::cos(angle) * radius},
                    {0.12f * flicker, angle, kPi * 0.5f + flicker * 0.28f},
                    {0.8f + pulse * 0.12f, 0.85f + chargeTime_ * 0.15f, 1.0f});
            }
            // 完了時も大きく増殖させず、頭上の小さな明滅だけで知らせる。
            if (chargeTime_ >= 1.0f && pulse > 0.70f) {
                SpawnScopedEffect(sparkScope_, "Resources/json/effect/effect_player_thunder_spark_arc.json",
                    center + Vector3{0.0f, 0.65f, 0.0f}, {kPi * 0.5f, phase * 1.3f, 0.0f},
                    {0.75f, 0.85f, 1.0f});
            }
            sparkEffectTimer_ = 0.10f;
        }
        if (sparkDamageTimer_ <= 0.0f) {
            DamageSphere(player, center, 2.2f, 3.0f, DamageType::Electric, 2.0f, 0.5f);
            sparkDamageTimer_ = 0.28f;
        }
    }
    void SpawnStrike(Player& player, const Vector3& ground) {
        const float yaw = std::atan2(lineDirection_.x, lineDirection_.z);
        SpawnEffect("Resources/json/effect/effect_player_thunder_bolt.json", ground + Vector3{ 0.0f, 4.75f, 0.0f },
            { kPi * 0.5f, yaw, 0.0f });
        SpawnEffect("Resources/json/effect/effect_player_thunder_impact_ring.json", ground + Vector3{ 0.0f, 0.06f, 0.0f });
        EmitPreset("player_thunder_strike_impact", ground + Vector3{ 0.0f, 0.22f, 0.0f });
        PhysicsQueryFilter filter;
        filter.mask = kEnemy;
        filter.ignoredObject = &player;
        for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(
            ground + Vector3{ 0.0f, 1.1f, 0.0f }, 1.45f, filter)) {
            Object3d* target = FindEnemyRoot(hit.object);
            if (!target || !lineHitTargets_.insert(target).second) continue;
            DamageEnemy(player, target, 10.0f, DamageType::Electric,
                { lineDirection_.x * 6.5f, 7.0f, lineDirection_.z * 6.5f });
        }
    }

    void Evade(Player& player) {
        const Vector3 direction = NormalizePlanar(player.GetForwardDirection());
        const Vector3 start = player.GetWorldPosition();
        PhysicsQueryFilter filter;
        filter.mask = kAllSolid;
        filter.ignoredObject = &player;
        const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(start + Vector3{ 0.0f, 0.65f, 0.0f },
            0.48f, direction, 9.5f, filter);
        const float distance = hit.isHit ? std::max(1.15f, hit.distance - 0.75f) : 9.5f;
        const Vector3 destination = start + direction * distance;
        SpawnEffect("Resources/json/effect/effect_player_thunder_evade_cross.json", start + Vector3{ 0.0f, 0.62f, 0.0f });
        SpawnEffect("Resources/json/effect/effect_player_thunder_evade_seal.json", destination + Vector3{ 0.0f, 0.08f, 0.0f });
        EmitDirectedPreset("player_thunder_evade_sparks", start, direction, 1.0f);
        player.SetTranslate(destination);
        player.SetMoveYaw(std::atan2(direction.x, direction.z));
        player.PlaySlimeAbilityMotion("player_ability_thunder_evade", false, 1.0f, 0.02f, 0.06f);
        player.StartEvasionInvincibility(0.24f);
        player.TriggerSlimeImpulse({ 1.95f, 0.68f, 1.95f }, 0.13f);
        DamageSphere(player, destination + Vector3{ 0.0f, 0.55f, 0.0f }, 2.25f,
            5.0f, DamageType::Electric, 9.0f, 5.0f);
        evadeCooldown_ = 0.85f;
    }

    Vector3 lineDirection_{ 0.0f, 0.0f, 1.0f };
    std::unordered_set<Object3d*> lineHitTargets_;
    float lineCooldown_ = 0.0f;
    float dischargeCooldown_ = 0.0f;
    float evadeCooldown_ = 0.0f;
    float lineTimer_ = 0.0f;
    MeshEffectManager::EffectScopeId sparkScope_ = MeshEffectManager::kInvalidEffectScope;
    bool primaryHeld_ = false;
    bool charging_ = false;
    float chargeTime_ = 0.0f;
    float sparkVisualTime_ = 0.0f;
    float sparkEffectTimer_ = 0.0f;
    float sparkDamageTimer_ = 0.0f;
    int strikeIndex_ = 0;
};
}

namespace CopyAbilityDetail {
std::unique_ptr<ICopyAbilitySession> CreateThunderSlimeCopyAbility(const EnemyAttackProfile& profile) {
    return std::make_unique<ThunderSlimeCopyAbility>(profile);
}
}
