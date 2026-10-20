#define NOMINMAX
#include "GimmickFireCannon.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "BaseScene.h"
#include "game/system/combat/BulletManager.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
#include "Player.h"
#include "SceneManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDefaultProjectileSpeed = 13.0f;
constexpr float kDefaultFireInterval = 1.35f;
constexpr float kDefaultProjectileRadius = 0.55f;
constexpr float kDefaultProjectileLifetime = 2.0f;
constexpr float kDefaultDetectionRange = 45.0f;
constexpr float kDefaultTurnSpeedDegrees = 360.0f;
} // namespace

GimmickFireCannon::~GimmickFireCannon() {
    BulletManager::GetInstance()->ClearPreviewForOwner(this);
}

void GimmickFireCannon::Initialize(engine::Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // 砲台本体は地形扱い、発射する弾は BulletManager 側で別管理します。
    landingWarning_.Initialize(common);
    SetClassName("Gimmick");
    SetGimmickType("FireCannon");
    SetName("Gimmick_FireCannon");
    SetModel(modelName);
    SetColor({0.22f, 0.11f, 0.08f, 1.0f});
    SetRoughness(0.65f);
    SetMetallic(0.05f);
    SetEmissive(1.1f);
    SetScale({0.55f, 0.55f, 1.1f});
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetStatic(false);

    engine::Object3d::ColliderConfig colConfig;
    colConfig.type = ColliderType::kOBB;
    colConfig.size = {1.0f, 1.0f, 1.0f};
    SetColliderConfig(colConfig);

    if (!param_.has_value()) {
        param_.emplace();
    }

    param_->speed = kDefaultProjectileSpeed;
    param_->interval = kDefaultFireInterval;
    param_->moveAmount = kDefaultProjectileRadius;
    param_->moveSpeed = kDefaultTurnSpeedDegrees;
    param_->detectionRange = kDefaultDetectionRange;
    param_->projectileLifetime = kDefaultProjectileLifetime;
    param_->actionMode = 1;
    param_->startActive = true;
    param_->returnOnOff = true;
}

// 編集停止中は無害な弾道プレビューだけを進め、Play 中は通常の攻撃を実行します。
void GimmickFireCannon::Update(float deltaTime) {
    // 編集配置を再生停止時に復元し、迫撃モードだけ専用の予告と弾道を使います。
    if (!param_.has_value()) {
        param_.emplace();
    }

    const bool playing = IsPlaying();
    bool editorPreview = false;
#ifdef USE_IMGUI
    editorPreview = !playing && param_->editorPreviewFiring;
#endif

    if (previewActive_ != editorPreview) {
        const bool wasPreview = previewActive_;
        if (wasPreview)
            BulletManager::GetInstance()->ClearPreviewForOwner(this);
        if (initializedForPlay_ && !wasPreview) {
            SetTranslate(basePosition_);
            SetRotation(baseRotation_);
            SetScale(baseScale_);
        }
        initializedForPlay_ = false;
        targetLocked_ = false;
        flightTimer_ = recoilTimer_ = 0.0f;
        landingWarning_.Hide();
        previewActive_ = editorPreview;
    }

    if (!playing && !editorPreview) {
        if (initializedForPlay_) {
            SetTranslate(basePosition_);
            SetRotation(baseRotation_);
            SetScale(baseScale_);
        }

        landingWarning_.Hide();
        targetLocked_ = false;
        flightTimer_ = recoilTimer_ = 0.0f;
        initializedForPlay_ = false;
        active_ = param_->startActive;
        fireTimer_ = 0.0f;
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        basePosition_ = GetTranslate();
        baseRotation_ = GetRotation();
        baseScale_ = GetScale();
        active_ = editorPreview || param_->startActive;
        fireTimer_ = editorPreview ? (std::min)(0.15f, GetFireInterval() * 0.35f)
                                   : GetFireInterval() * 0.35f;
        initializedForPlay_ = true;
    }

    if (editorPreview) {
        // 編集中の Transform は操作対象そのものなので、毎フレーム現在値を発射基準にします。
        basePosition_ = GetTranslate();
        baseRotation_ = GetRotation();
        baseScale_ = GetScale();
        active_ = true;
    }

    if (param_->actionMode == 2) {
        UpdateMortar(deltaTime);
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (param_->actionMode == 3) {
        // 大型砲台の向きは配置したTransformで固定し、撃つ瞬間だけ砲身を縮めます。
        const float dt = (std::max)(0.0f, deltaTime);
        recoilTimer_ = (std::max)(0.0f, recoilTimer_ - dt);
        const float recoil = std::sin(recoilTimer_ / 0.28f * kPi) * 0.06f;
        if (!editorPreview)
            SetScale({baseScale_.x * (1.0f + recoil * 0.35f),
                      baseScale_.y * (1.0f + recoil * 0.35f),
                      baseScale_.z * (1.0f - recoil)});
        landingWarning_.Hide();
        if (active_) {
            fireTimer_ -= dt;
            if (fireTimer_ <= 0.0f) {
                FireBombard();
                fireTimer_ = GetFireInterval();
            }
        } else {
            fireTimer_ = (std::min)(fireTimer_, GetFireInterval() * 0.35f);
        }
        BaseGimmick::Update(deltaTime);
        return;
    }

    landingWarning_.Hide();
    engine::Vector3 fireDirection = {};
    const bool canFire = active_ && FindFireDirection(fireDirection);

    if (canFire && param_->actionMode == 1) {
        if (!editorPreview) {
            RotateToward(fireDirection, deltaTime);
            fireDirection = GetForwardDirection();
        }
    }

    if (canFire) {
        fireTimer_ -= deltaTime;

        if (fireTimer_ <= 0.0f) {
            FireProjectile(fireDirection);
            fireTimer_ = GetFireInterval();
        }
    } else {
        fireTimer_ = (std::min)(fireTimer_, GetFireInterval() * 0.35f);
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickFireCannon::OnTrigger() {
    // 単発トリガーは現在の発射状態を反転します。
    OnSwitchEvent(!active_);
}

void GimmickFireCannon::OnSwitchEvent(bool active) {
    // 停止時には照準と着弾予告も消し、旧弾の視覚情報を残しません。
    if (!param_.has_value()) {
        param_.emplace();
    }

    if (!active && !param_->returnOnOff) {
        return;
    }

    active_ = active;

    if (!active) {
        targetLocked_ = false;
        landingWarning_.Hide();
    }
}

bool GimmickFireCannon::IsPlaying() const {
    // ゲーム用攻撃と編集用の無害な弾道プレビューを切り分けます。
    SceneManager* sceneManager = SceneManager::GetInstance();
    return sceneManager && sceneManager->IsPlaying();
}

bool GimmickFireCannon::FindFireDirection(engine::Vector3& outDirection) const {
    // 固定砲は前方へ、追尾砲は射程内のプレイヤー中心へ照準を向けます。
    if (!param_.has_value() || param_->actionMode == 0) {
        outDirection = GetForwardDirection();
        return true;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();

    if (!sceneManager) {
        return false;
    }

    BaseScene* scene = sceneManager->GetCurrentScene();

    if (!scene) {
        return false;
    }

    Player* player = GetScenePlayer(scene);

    if (!player) {
        if (previewActive_) {
            outDirection = GetForwardDirection();
            return true;
        }
        return false;
    }

    engine::Vector3 target = player->GetWorldPosition();
    target.y += 0.65f;

    const engine::Vector3 muzzle = GetMuzzlePosition(GetForwardDirection());
    engine::Vector3 diff = target - muzzle;
    const float distance = engine::Math::Length(diff);
    const float range = (param_->detectionRange > 0.0f) ? param_->detectionRange : kDefaultDetectionRange;

    if (distance > range || distance < 0.001f) {
        if (previewActive_) {
            outDirection = GetForwardDirection();
            return true;
        }
        return false;
    }

    outDirection = engine::Math::Normalize(diff);
    return true;
}

engine::Vector3 GimmickFireCannon::GetForwardDirection() const {
    // ピッチとヨーから発射方向を作り、零ベクトル時は前方を既定にします。
    const engine::Vector3 rot = GetRotation();
    const float cosPitch = std::cos(rot.x);
    engine::Vector3 forward = {std::sin(rot.y) * cosPitch, -std::sin(rot.x), std::cos(rot.y) * cosPitch};

    if (engine::Math::Length(forward) < 0.001f) {
        return {0.0f, 0.0f, 1.0f};
    }

    return engine::Math::Normalize(forward);
}

engine::Vector3 GimmickFireCannon::GetMuzzlePosition(const engine::Vector3& direction) const {
    // モデルの大きさに応じて砲口を前上方へずらし、本体から弾が出るようにします。
    const engine::Vector3 scale = GetScale();
    engine::Vector3 muzzle = GetWorldPosition();
    muzzle += direction * ((std::max)(scale.z, 0.5f) * 0.95f);
    muzzle.y += (std::max)(scale.y, 0.5f) * 0.18f;
    return muzzle;
}

void GimmickFireCannon::RotateToward(const engine::Vector3& direction, float deltaTime) {
    // 短い角度の経路で回転し、角度境界をまたいでも大回りしません。
    const float horizontalLength = std::sqrt(direction.x * direction.x + direction.z * direction.z);

    if (horizontalLength < 0.001f) {
        return;
    }

    const float targetYaw = std::atan2(direction.x, direction.z);
    const float targetPitch = std::atan2(-direction.y, horizontalLength);
    const float t = std::clamp(GetTurnSpeedRadians() * deltaTime, 0.0f, 1.0f);

    engine::Vector3 rot = GetRotation();
    rot.y = engine::Math::LerpShortAngle(rot.y, targetYaw, t);
    rot.x = engine::Math::LerpShortAngle(rot.x, targetPitch, t);
    SetRotation(rot);
}

void GimmickFireCannon::FireProjectile(const engine::Vector3& direction) {
    // 射程は標的選択に使い、弾の消滅時刻は Editor の持続時間に従います。
    const float radius = GetProjectileRadius();
    const float speed = GetProjectileSpeed();
    const float life = GetProjectileLifetime();

    BulletVisualConfig visual;
    visual.collisionRadiusIsWorld = true;
    visual.materialType = 0;
    visual.blendMode = BlendMode::kNormal;
    visual.color = {1.0f, 1.0f, 1.0f, 1.0f};
    visual.emissive = 1.7f;
    visual.visualScale = radius * 0.60f / 1.043f;
    visual.burningPreset = "cannon_burning_flame";
    visual.trailSecondaryPreset = "fireball_trail_embers";
    visual.impactPreset = "fireball_impact_flame";
    visual.impactSecondaryPreset = "fireball_impact_sparks";
    visual.impactDebrisPreset = "cannon_shell_basalt_shatter";
    visual.impactDebrisSecondaryPreset = "cannon_shell_ember_shatter";
    visual.impactDebrisScale = radius;
    visual.impactOnExpiry = true;
    visual.destroyOnSolidCastHit = true;
    visual.trailInterval = 0.06f;
    visual.trailReverseDirection = true;
    visual.impactDirected = false;
    visual.previewOnly = previewActive_;
    visual.previewOwner = this;

    // 攻撃属性とプレイヤー・地面のマスクを渡し、命中処理は弾管理へ任せます。
    BulletManager::GetInstance()->Fire(GetMuzzlePosition(direction) + direction * (radius + 0.16f), direction * speed,
                                       CollisionCategories::Get("enemy_attack"), CollisionCategories::Get("player") |
                                           CollisionCategories::Get("ground") | CollisionCategories::Get("map_block"),
                                       "Gimmicks/volcanic_bombard_shell", radius, life, visual, 1.0f, {}, DamageType::Fire);
}

void GimmickFireCannon::FireBombard() {
    // OBJ の +Z 砲口は左手座標への読み込みで -Z になるため、描画された砲口側へ発射します。
    UpdateWorldMatrix();
    const auto& world = GetWorldMatrix();
    engine::Vector3 forward = {-world.m[2][0], -world.m[2][1], -world.m[2][2]};
    if (engine::Math::Length(forward) <= 0.001f) {
        return;
    }
    forward = engine::Math::Normalize(forward);

    const float radius = GetProjectileRadius();
    const float speed = GetProjectileSpeed();
    const float life = GetProjectileLifetime();

    // 読み込み後の砲口中心はローカル(0, 2.73, -2.55)。球の後端が砲縁に重ならない位置から出します。
    const engine::Vector3 muzzle = {
        world.m[3][0] + world.m[1][0] * 2.73f - world.m[2][0] * 2.55f + forward.x * (radius + 0.16f),
        world.m[3][1] + world.m[1][1] * 2.73f - world.m[2][1] * 2.55f + forward.y * (radius + 0.16f),
        world.m[3][2] + world.m[1][2] * 2.73f - world.m[2][2] * 2.55f + forward.z * (radius + 0.16f)};

    BulletVisualConfig visual;
    visual.visualScale = radius * 0.60f / 1.043f; // 岩の芯を小さくし、外側の炎に輪郭を任せます。
    visual.collisionRadiusIsWorld = true;
    visual.explosionRadius = std::clamp(param_->healAmount, 0.5f, 8.0f);
    visual.materialType = 0;
    visual.blendMode = BlendMode::kNormal;
    visual.color = {1.0f, 1.0f, 1.0f, 1.0f};
    visual.emissive = 1.7f;
    visual.burningPreset = "cannon_burning_flame";
    visual.trailPreset = "fireball_trail_flame";
    visual.trailSecondaryPreset = "fireball_trail_embers";
    visual.impactPreset = "fireball_impact_flame";
    visual.impactSecondaryPreset = "fireball_impact_sparks";
    visual.impactDebrisPreset = "cannon_shell_basalt_shatter";
    visual.impactDebrisSecondaryPreset = "cannon_shell_ember_shatter";
    visual.impactDebrisScale = radius;
    visual.impactOnExpiry = true;
    visual.destroyOnSolidCastHit = true;
    visual.trailInterval = 0.04f;
    visual.trailReverseDirection = true;
    visual.impactDirected = false;
    visual.previewOnly = previewActive_;
    visual.previewOwner = this;
    BulletManager::GetInstance()->Fire(muzzle, forward * speed,
                                       CollisionCategories::Get("enemy_attack"),
                                       CollisionCategories::Get("player") | CollisionCategories::Get("ground") |
                                           CollisionCategories::Get("map_block"),
                                       "Gimmicks/volcanic_bombard_shell", radius, life, visual, 1.0f, {},
                                       DamageType::Explosion);

    // 編集停止中は演出時間も停止しているため、蓄積する発射エフェクトは生成しません。
    if (!previewActive_) {
        if (auto* effects = MeshEffectManager::GetInstance()) {
            effects->SpawnEffectAt("Resources/json/effect/effect_bomb_core_flash.json", muzzle,
                                   {0, 0, 0}, {0.5f, 0.5f, 0.5f});
        }
        if (auto* particles = GPUParticleManager::GetInstance(); particles && particles->IsInitialized()) {
            particles->EmitDirected("hit_bomb_fire_sparks", muzzle, forward, 0.7f);
        }
    }
    recoilTimer_ = 0.28f;
}

float GimmickFireCannon::GetProjectileSpeed() const {
    // 設定ミスで停止弾を作らないよう最小速度を保証します。
    return param_.has_value() ? (std::max)(1.0f, param_->speed) : kDefaultProjectileSpeed;
}

float GimmickFireCannon::GetFireInterval() const {
    // ゼロ秒連射による弾の増殖を防ぎます。
    const float interval = param_.has_value() ? (std::max)(0.08f, param_->interval) : kDefaultFireInterval;
    return previewActive_ ? (std::max)(0.8f, interval) : interval;
}

float GimmickFireCannon::GetProjectileRadius() const {
    // 大型化を妨げないよう上限は設けず、不正値と負値だけを除きます。
    const float configured = param_.has_value() ? param_->moveAmount : kDefaultProjectileRadius;
    return std::isfinite(configured) ? (std::max)(0.1f, configured) : kDefaultProjectileRadius;
}

float GimmickFireCannon::GetProjectileLifetime() const {
    const float configured = param_.has_value() ? param_->projectileLifetime : kDefaultProjectileLifetime;
    return std::isfinite(configured) ? std::clamp(configured, 0.2f, 20.0f) : kDefaultProjectileLifetime;
}

float GimmickFireCannon::GetTurnSpeedRadians() const {
    // エディター設定は度毎秒、回転計算はラジアンで扱います。
    const float degrees = param_.has_value() ? (std::max)(1.0f, param_->moveSpeed) : kDefaultTurnSpeedDegrees;
    return degrees * (kPi / 180.0f);
}

std::unique_ptr<engine::Object3d> GimmickFireCannon::Clone() const {
    // 複製先の予告表示と判定資源を初期化してから設定をコピーします。
    auto newObj = std::make_unique<GimmickFireCannon>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

void GimmickFireCannon::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // 砲台に続けて、迫撃弾の地面上の着弾予告を描画します。
    BaseGimmick::Draw(pointLightResource, spotLightResource);
    landingWarning_.DrawGround(pointLightResource, spotLightResource);
}

void GimmickFireCannon::UpdateMortar(float deltaTime) {
    // 台座は水平に保ち、砲弾の弾道だけを重力で計算する。
    const float dt = (std::max)(0.0f, deltaTime);
    const float blast = std::clamp(param_->healAmount, 1.0f, 8.0f);
    const float warning = std::clamp(param_->shakeDuration, 0.35f, 4.0f);
    const float gravity = std::clamp(param_->gravity, 4.0f, 60.0f);
    const float radius = GetProjectileRadius();
    recoilTimer_ = (std::max)(0.0f, recoilTimer_ - dt);
    const float squash = std::sin(recoilTimer_ / 0.28f * kPi) * 0.06f;
    if (!previewActive_)
        SetScale({baseScale_.x * (1.0f + squash), baseScale_.y * (1.0f - squash), baseScale_.z});
    landingWarning_.Update(dt);

    if (flightTimer_ > 0.0f) {
        flightTimer_ -= dt;
        if (flightTimer_ <= 0.0f)
            landingWarning_.Hide();
        return;
    }

    if (!active_) {
        landingWarning_.Hide();
        targetLocked_ = false;
        return;
    }

    if (!targetLocked_) {
        fireTimer_ -= dt;
        if (fireTimer_ > 0.0f)
            return;
        auto* scene = SceneManager::GetInstance()->GetCurrentScene();
        auto* player = GetScenePlayer(scene);
        if (!player && !previewActive_)
            return;
        const engine::Vector3 forwardTarget = GetWorldPosition() + GetForwardDirection() *
            (std::max)(12.0f, (std::min)(param_->detectionRange, 20.0f));
        engine::Vector3 target = player ? player->GetWorldPosition() : forwardTarget;
        const engine::Vector3 targetOffset = target - GetWorldPosition();
        if (previewActive_ && targetOffset.x * targetOffset.x + targetOffset.z * targetOffset.z < 100.0f)
            target = forwardTarget;
        if (!previewActive_ && engine::Math::Length(target - GetWorldPosition()) > param_->detectionRange)
            return;
        PhysicsQueryFilter filter;
        filter.mask = CollisionCategories::Get("ground");
        filter.ignoredObject = this;
        const auto ground =
            CollisionManager::GetInstance()->Raycast(target + engine::Vector3{0, 1, 0}, {0, -1, 0}, 18.0f, filter);
        if (!ground.isHit && !previewActive_)
            return;
        lockedTarget_ = ground.isHit ? ground.hitPoint : target;
        engine::Vector3 direction = lockedTarget_ - GetWorldPosition();
        if (direction.x * direction.x + direction.z * direction.z < 100.0f)
            return;

        // 予告開始時に狙いを固定し、回避後のプレイヤーを追いかけない。
        if (!previewActive_)
            SetRotation({0.0f, std::atan2(direction.x, direction.z), 0.0f});
        targetLocked_ = true;
        warningTimer_ = warning;
    }

    warningTimer_ -= dt;
    landingWarning_.ShowCircle(lockedTarget_ + engine::Vector3{0, 0.08f, 0}, blast,
                               std::clamp(1.0f - warningTimer_ / warning, 0.0f, 1.0f), {1.0f, 0.34f, 0.05f, 0.65f});
    if (warningTimer_ > 0.0f)
        return;
    const engine::Vector3 forward = GetForwardDirection();
    const engine::Vector3 muzzle = GetWorldPosition() + forward * (2.55f * baseScale_.z) + engine::Vector3{0, 2.73f * baseScale_.y, 0};
    const engine::Vector3 target = lockedTarget_ + engine::Vector3{0, radius + 0.03f, 0};

    // 頂点高度から上向き初速と飛行時間を逆算し、水平速度をロック済み着弾点へ合わせます。
    const float apex = (std::max)(muzzle.y, target.y) + std::clamp(param_->jumpPower, 3.0f, 20.0f);
    const float vy = std::sqrt(2.0f * gravity * (apex - muzzle.y));
    const float flight = vy / gravity + std::sqrt(2.0f * (apex - target.y) / gravity);
    const engine::Vector3 velocity = {(target.x - muzzle.x) / flight, vy, (target.z - muzzle.z) / flight};
    BulletVisualConfig visual;
    visual.visualScale = radius;
    visual.explosionRadius = blast;
    visual.color = {1, 1, 1, 1};
    visual.emissive = 1.0f;
    visual.previewOnly = previewActive_;
    visual.previewOwner = this;
    BulletManager::GetInstance()->Fire(muzzle, velocity, CollisionCategories::Get("enemy_attack"),
                                       CollisionCategories::Get("player") | CollisionCategories::Get("ground"), "Gimmicks/volcanic_shell", radius,
                                       flight + 2.0f, visual, 1.0f, {}, DamageType::Explosion, gravity);
    if (!previewActive_) {
        if (auto* effects = MeshEffectManager::GetInstance())
            effects->SpawnEffectAt("Resources/json/effect/effect_bomb_core_flash.json", muzzle, {0, 0, 0}, {.4f, .4f, .4f});
        if (auto* particles = GPUParticleManager::GetInstance(); particles && particles->IsInitialized())
            particles->EmitDirected("hit_bomb_fire_sparks", muzzle, forward, 0.6f);
    }
    targetLocked_ = false;
    flightTimer_ = flight;
    fireTimer_ = GetFireInterval();
    recoilTimer_ = 0.28f;
}
