#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.Internal.h"

using namespace EnemyFalseKingSlimeImplementation;

namespace {
constexpr const char* kUmbraFlameRedEnergyPreset = "false_king_umbra_flame_charge_red";

// 魔法陣、炎の描画、当たり判定が同じ扇形範囲を参照するための幾何情報です。
struct UmbraFlameGeometry {
    float nearDistance;
    float farDistance;
    float fanLength;
    float nearWidth;
    float farWidth;
    Vector3 forward;
    Vector3 right;
};

UmbraFlameGeometry ResolveUmbraFlameGeometry(const EnemyAttackDefinition& attack, const Vector3& direction) {
    // 攻撃の射程と半径から、近端・遠端を持つ扇形の炎領域へ換算する。
    constexpr float kNearDistance = 1.5f;
    constexpr float kMinFarDistance = 20.0f;
    constexpr float kReferenceRadius = 1.75f;
    const float farDistance = (std::max)(attack.maxRange * 0.90f, kMinFarDistance);
    const Vector3 forward = NormalizePlanar(direction);
    return {kNearDistance,
            farDistance,
            farDistance - kNearDistance,
            1.6f * attack.radius / kReferenceRadius,
            farDistance * 0.94f * attack.radius / kReferenceRadius,
            forward,
            {forward.z, 0.0f, -forward.x}};
}
} // namespace

// 扇形の術式と炎の進行を、他の攻撃シーケンスから分離します。

void EnemyFalseKingSlime::PrepareUmbraFlames() {
    umbraFlames_.clear();
    umbraFlameFanHit_ = false;
    const EnemyAttackDefinition& attack = GetAttackDefinition(kUmbraFlameAttackId);
    const UmbraFlameGeometry geometry = ResolveUmbraFlameGeometry(attack, lockedDirection_);
    const auto [nearDistance, farDistance, fanLength, nearWidth, farWidth, forward, right] = geometry;
    // 手前の一点から中段二点、奥三点へ広がる扇状の術式です。
    const std::array<Vector2, 6> nodes = {Vector2{nearDistance + fanLength * 0.148f, 0.0f},
                                          Vector2{nearDistance + fanLength * 0.49f, -farWidth * 0.193f},
                                          Vector2{nearDistance + fanLength * 0.49f, farWidth * 0.193f},
                                          Vector2{nearDistance + fanLength * 0.879f, -farWidth * 0.380f},
                                          Vector2{nearDistance + fanLength * 0.879f, 0.0f},
                                          Vector2{nearDistance + fanLength * 0.879f, farWidth * 0.380f}};
    umbraFlameNetwork_ = std::make_unique<EffectObject3d>();
    umbraFlameNetwork_->Initialize(common_);
    if (!umbraFlameNetwork_->LoadFromJson(kUmbraFlameNetworkEffect)) {
        umbraFlameNetwork_.reset();
    } else {
        umbraFlameNetwork_->SetName("FalseKing_UmbraFlameNetwork");
        umbraFlameNetwork_->SetRevealProgress(0.0f);
        umbraFlameNetwork_->SetIsVisible(false);
        umbraFlameNetwork_->Play(10.0f);
    }
    for (size_t index = 0; index < nodes.size(); ++index) {
        UmbraFlamePortal portal;
        portal.groundPosition =
            FindGroundPoint(attackStartPosition_ + forward * nodes[index].x + right * nodes[index].y);
        portal.delay = static_cast<float>(index) * 0.12f;
        portal.age = -portal.delay;
        portal.sigil = std::make_unique<EffectObject3d>();
        portal.sigil->Initialize(common_);
        if (!portal.sigil->LoadFromJson(kUmbraFlameSigilEffect)) {
            portal.sigil.reset();
        } else {
            portal.sigil->SetName("FalseKing_UmbraFlameSigil_" + std::to_string(index));
            portal.sigil->SetRevealProgress(0.0f);
            portal.sigil->SetIsVisible(false);
            portal.sigil->Play(10);
        }
        // 一つの閉じた体積で扇全体を描き、術式の六点は粒子の起点として使います。
        if (index == 0) {
            portal.flame = CreateRoyalVisual("FalseKing_UmbraFlameFanFlame", "Primitives/cylinder", kFireMaterialType,
                                             {0.72f, 0.38f, 1.0f, 0.0f}, 2.4f);
        }
        if (portal.flame) {
            portal.flame->SetBlendMode(BlendMode::kNormal);
            portal.flame->SetIsVisible(false);
            if (MeshRenderer* renderer = portal.flame->GetMeshRenderer()) {
                if (auto* fire = renderer->GetWaterParamData()) {
                    fire->effectType = 7.0f;
                    fire->waveSpeed = 1.45f;
                    fire->effectScale = 1.0f;
                    fire->effectSoftness = 0.0f;
                    fire->effectIntensity = 1.10f;
                    fire->billboardScale = 1.0f;
                    fire->effectScaleX = farWidth * 0.5f;
                    fire->effectScaleY = 4.8f;
                    fire->effectScaleZ = fanLength * 0.5f;
                    fire->uvOffsetX = 0.0f;
                    fire->uvOffsetY = 0.0f;
                }
            }
        }
        umbraFlames_.push_back(std::move(portal));
    }
}

void EnemyFalseKingSlime::UpdateUmbraFlameNetwork(float deltaTime, bool active) {
    if (!umbraFlameNetwork_)
        return;
    const EnemyAttackDefinition& attack = GetAttackDefinition(kUmbraFlameAttackId);
    const UmbraFlameGeometry geometry = ResolveUmbraFlameGeometry(attack, lockedDirection_);
    const auto [nearDistance, farDistance, fanLength, nearWidth, farWidth, forward, right] = geometry;
    const Vector3 center = FindGroundPoint(attackStartPosition_ + forward * (nearDistance + fanLength * 0.5f));
    const float progress =
        active ? 1.0f
               : SmoothStep01(std::clamp(
                     (1.0f - attackTimer_ / (std::max)(attackStateDuration_, 0.01f) - 0.04f) / 0.82f, 0.0f, 1.0f));
    const float fade = active ? SmoothStep01(std::clamp(attackTimer_ / 0.70f, 0.0f, 1.0f)) : 1.0f;
    umbraFlameNetwork_->Update(deltaTime);
    umbraFlameNetwork_->SetIsVisible(progress > 0.01f && fade > 0.01f);
    umbraFlameNetwork_->SetRevealProgress(progress);
    umbraFlameNetwork_->SetTranslate(center + Vector3{0.0f, 0.15f, 0.0f});
    umbraFlameNetwork_->SetRotation({0.0f, std::atan2(forward.x, forward.z), 0.0f});
    // PlaneSize は 2.4 m。術式の絵柄を実際の扇範囲へ合わせます。
    umbraFlameNetwork_->SetScale({farWidth / 2.4f, 1.0f, fanLength / 2.4f});
    umbraFlameNetwork_->SetColor({1.0f, 1.0f, 1.0f, fade * (0.18f + progress * 0.52f)});
    umbraFlameNetwork_->SetIntensity(1.10f + progress * 0.35f);
    umbraFlameNetwork_->UpdateLocalMatrix();
    umbraFlameNetwork_->UpdateWorldMatrix();
}

void EnemyFalseKingSlime::UpdateUmbraFlames(float deltaTime, bool active) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kUmbraFlameAttackId);
    const UmbraFlameGeometry geometry = ResolveUmbraFlameGeometry(attack, lockedDirection_);
    const auto [nearDistance, farDistance, fanLength, nearWidth, farWidth, forward, right] = geometry;
    const float elapsed = active ? attackStateDuration_ - attackTimer_ : 0.0f;
    const float waveFront = SmoothStep01(std::clamp(elapsed / 1.55f, 0.0f, 1.0f));
    // 魔法陣だけで予告するため、従来の扇形ガイドは表示しません。
    HideAttackTelegraph();
    UpdateUmbraFlameNetwork(deltaTime, active);
    for (UmbraFlamePortal& portal : umbraFlames_) {
        if (active)
            portal.activeAge += deltaTime;
        else
            portal.age += deltaTime;
        const float age = active ? portal.activeAge : portal.age;
        const float assembly = active ? 1.0f : SmoothStep01(std::clamp(age / 0.90f, 0.0f, 1.0f));
        const float fade = active ? SmoothStep01(std::clamp(attackTimer_ / 0.70f, 0.0f, 1.0f)) : 1.0f;
        if (portal.sigil) {
            portal.sigil->Update(deltaTime);
            portal.sigil->SetIsVisible((active || age >= 0.0f) && fade > 0.01f);
            portal.sigil->SetRevealProgress(active ? 1.0f : assembly);
            portal.sigil->SetTranslate(portal.groundPosition + Vector3{0.0f, 0.16f, 0.0f});
            portal.sigil->SetRotation({0.0f, portal.age * 0.25f, 0.0f});
            const float circleScale = attack.radius * 1.03f * (0.68f + assembly * 0.32f);
            portal.sigil->SetScale({circleScale, 1.0f, circleScale});
            portal.sigil->SetColor({1.0f, 1.0f, 1.0f, fade * (0.42f + assembly * 0.48f)});
            portal.sigil->SetIntensity(1.25f + assembly * 0.55f);
            portal.sigil->UpdateLocalMatrix();
            portal.sigil->UpdateWorldMatrix();
        }
        if (age < 0.0f)
            continue;

        if (!active) {
            portal.particleTimer -= deltaTime;
            if (portal.particleTimer <= 0.0f && assembly > 0.18f) {
                // 赤と青の光を上空の別方向から魔法陣へ収束させます。
                const float orbit = age * 1.7f + portal.delay * 7.0f;
                const float radius = attack.radius * 1.22f;
                const Vector3 radial{std::cos(orbit) * radius, 0.0f, std::sin(orbit) * radius};
                const Vector3 redOrigin = portal.groundPosition + radial + Vector3{0.0f, 8.0f, 0.0f};
                const Vector3 blueOrigin = portal.groundPosition - radial + Vector3{0.0f, 7.4f, 0.0f};
                const Vector3 destination = portal.groundPosition + Vector3{0.0f, 0.14f, 0.0f};
                EmitDirectedPreset(kUmbraFlameRedEnergyPreset, redOrigin, destination - redOrigin, 1.0f);
                EmitDirectedPreset(kUmbraFlameChargePreset, blueOrigin, destination - blueOrigin, 1.0f);
                portal.particleTimer += 0.12f;
            }
            continue;
        }

        // 炎の本体は術者側から奥へ進む一つの扇形体積です。
        const float rise = SmoothStep01(std::clamp(age / 0.34f, 0.0f, 1.0f));
        const float height = 9.6f * rise;
        if (portal.flame) {
            portal.flame->SetIsVisible(rise > 0.01f && fade > 0.01f);
            const Vector3 fanCenter =
                FindGroundPoint(attackStartPosition_ + forward * (nearDistance + fanLength * 0.5f));
            portal.flame->SetTranslate(fanCenter + Vector3{0.0f, 4.95f, 0.0f});
            portal.flame->SetRotation({0.0f, std::atan2(forward.x, forward.z), 0.0f});
            portal.flame->SetScale({1.0f, 1.0f, 1.0f});
            portal.flame->SetColor({0.72f, 0.38f, 1.0f, fade * 0.86f});
            if (MeshRenderer* renderer = portal.flame->GetMeshRenderer()) {
                if (auto* fire = renderer->GetWaterParamData()) {
                    fire->effectSoftness = waveFront;
                }
            }
            portal.flame->Update(deltaTime);
            portal.flame->UpdateWorldMatrix(true);
        }
    }
    // 火の粉も六つの魔法陣ではなく、扇を進む炎の前縁から湧かせます。
    if (active && waveFront > 0.03f && attackTimer_ > 0.70f) {
        effectTimer_ -= deltaTime;
        if (effectTimer_ <= 0.0f) {
            const Vector3 right{forward.z, 0.0f, -forward.x};
            const float frontDistance = nearDistance + fanLength * waveFront;
            const float frontHalfWidth = (nearWidth + (farWidth - nearWidth) * waveFront) * 0.42f;
            for (int lane = -2; lane <= 2; ++lane) {
                const Vector3 ground = FindGroundPoint(attackStartPosition_ + forward * frontDistance +
                                                       right * (frontHalfWidth * lane * 0.5f));
                EmitDirectedPreset(kUmbraFlameFlamePreset, ground + Vector3{0.0f, 0.18f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                   0.84f);
            }
            effectTimer_ += 0.12f;
        }
    }
    // 炎の前縁が届いた時だけ判定し、画面に見える進行と被弾を一致させます。
    if (!active || umbraFlameFanHit_ || !target_ || target_->isDead || !target_->GetIsVisible())
        return;
    const Vector3 targetPosition = target_->GetWorldPosition();
    const Vector3 offset = targetPosition - attackStartPosition_;
    const float along = offset.x * forward.x + offset.z * forward.z;
    const float side = std::abs(offset.x * right.x + offset.z * right.z);
    const float collisionRadius = target_->GetCollisionRadius();
    const float fanRate = std::clamp((along - nearDistance) / fanLength, 0.0f, 1.0f);
    const float halfWidth = (nearWidth + (farWidth - nearWidth) * fanRate) * 0.5f;
    const Vector3 ground = FindGroundPoint(targetPosition);
    if (along < nearDistance - collisionRadius || along > farDistance + collisionRadius ||
        fanRate > waveFront + collisionRadius / fanLength || side > halfWidth + collisionRadius ||
        std::abs(targetPosition.y - ground.y) > 4.0f)
        return;
    const Vector3 knockback = NormalizePlanar(targetPosition - attackStartPosition_) * 5.5f;
    DamageTargetAt(ground, collisionRadius + 0.05f, 4.0f, attack.damage, DamageType::Explosion,
                   {knockback.x, 6.0f, knockback.z});
    umbraFlameFanHit_ = true;
}
