#define NOMINMAX
#include "GimmickFireCannon.h"
#include "Easing.h"
#include <numbers>
#include "ObjectPropertyDefinition.h"
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
constexpr float kPi = std::numbers::pi_v<float>;
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

    if (!GetParameters().has_value()) {
        ResetParameters();
    }

    SetParameter(&engine::EntityParameter::speed, kDefaultProjectileSpeed);
    SetParameter(&engine::EntityParameter::interval, kDefaultFireInterval);
    SetParameter(&engine::EntityParameter::moveAmount, kDefaultProjectileRadius);
    SetParameter(&engine::EntityParameter::moveSpeed, kDefaultTurnSpeedDegrees);
    SetParameter(&engine::EntityParameter::detectionRange, kDefaultDetectionRange);
    SetParameter(&engine::EntityParameter::projectileLifetime, kDefaultProjectileLifetime);
    SetParameter(&engine::EntityParameter::actionMode, 1);
    SetParameter(&engine::EntityParameter::startActive, true);
    SetParameter(&engine::EntityParameter::returnOnOff, true);

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickFireCannon;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "火球砲台",
        "編集プレビューは無害で最短0.8秒間隔、同時数制限付きです。放物線は予告時に着弾点を固定します。大型砲弾は砲口方向（読込後の-Z）へ直進し、衝突で早く消えることがあります。通常の前方固定はローカルZ+です。");
    using Parameter = engine::Object3d::EntityParameter;
    properties.Parameter("発射方向", "actionMode", &Parameter::actionMode, 0.0f, 3.0f,
        nullptr, {"前方固定", "プレイヤーを狙う", "放物線砲弾（着弾爆発）", "大型砲弾（砲口に沿って直進）"});
    properties.Parameter("弾速", "speed", &Parameter::speed, 1.0f, 120.0f);
    properties.Parameter("発射間隔", "interval", &Parameter::interval, 0.08f, 20.0f);
    properties.Parameter("弾の半径", "moveAmount", &Parameter::moveAmount, 0.1f, (std::numeric_limits<float>::max)(),
        nullptr, {}, {.speed = 0.01f});
    properties.Parameter("索敵範囲", "detectionRange", &Parameter::detectionRange, 0.0f, 300.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode != 3); });
    properties.Parameter("旋回速度（度/秒）", "moveSpeed", &Parameter::moveSpeed, 1.0f, 720.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 1); });
    properties.Parameter("開始時に有効", "startActive", &Parameter::startActive);
    properties.Parameter("OFFで元に戻す", "returnOnOff", &Parameter::returnOnOff);
    properties.Parameter("編集停止中に発射をプレビュー", "editorPreviewFiring", &Parameter::editorPreviewFiring);
    properties.Parameter("弾道の上昇高さ", "jumpPower", &Parameter::jumpPower, 3.0f, 20.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 2); });
    properties.Parameter("砲弾の重力", "gravity", &Parameter::gravity, 4.0f, 60.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 2); });
    properties.Parameter("爆風半径", "healAmount", &Parameter::healAmount, 1.0f, 8.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 2); });
    properties.Parameter("発射前の予告時間", "shakeDuration", &Parameter::shakeDuration, 0.35f, 4.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 2); });
    properties.Parameter("弾の最大持続時間", "projectileLifetime", &Parameter::projectileLifetime, 0.2f, 20.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 3); }, {}, {.help = +[](const Self& object) { return std::string("最大飛距離の目安: ") + std::to_string(object.GetParameters()->speed * object.GetParameters()->projectileLifetime) + " m"; }});
    properties.Parameter("爆風半径", "healAmount_straight", &Parameter::healAmount, 0.5f, 8.0f,
        +[](const Self& object) { return object.GetParameters() && (object.GetParameters()->actionMode == 3); }, {}, {.parameterId = "healAmount"});
    const auto straightOnly = +[](const Self& object) { return object.GetParameters() && object.GetParameters()->actionMode == 3; };
    properties.Expose("直進砲台のため時間", ENGINE_PROPERTY(bombardAnticipationTime_), 0.0f, 0.6f, straightOnly);
    properties.Expose("直進砲台の反動の強さ", ENGINE_PROPERTY(bombardRecoilStrength_), 0.0f, 0.45f, straightOnly);
    properties.Expose("直進砲台の戻り時間", ENGINE_PROPERTY(bombardRecoveryTime_), 0.12f, 1.0f, straightOnly);
}

// 編集停止中は無害な弾道プレビューだけを進め、Play 中は通常の攻撃を実行します。
void GimmickFireCannon::Update(float deltaTime) {
    // 編集配置を再生停止時に復元し、迫撃モードだけ専用の予告と弾道を使います。
    if (!GetParameters().has_value()) {
        ResetParameters();
    }

    // モード変更やプレビュー停止時に、前フレームの描画補正を残しません。
    if (auto* renderer = GetMeshRenderer()) renderer->ResetVisualTransform();
    const bool playing = IsPlaying();
    bool editorPreview = false;
#ifdef USE_IMGUI
    editorPreview = !playing && GetParameters()->editorPreviewFiring;
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
        active_ = GetParameters()->startActive;
        fireTimer_ = 0.0f;
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        basePosition_ = GetTranslate();
        baseRotation_ = GetRotation();
        baseScale_ = GetScale();
        active_ = editorPreview || GetParameters()->startActive;
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

    if (GetParameters()->actionMode == 2) {
        UpdateMortar(deltaTime);
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (GetParameters()->actionMode == 3) {
        // 発射周期は維持し、その終わりにため、発射直後に反動を重ねます。
        const float dt = (std::max)(0.0f, deltaTime);
        recoilTimer_ = (std::max)(0.0f, recoilTimer_ - dt);
        landingWarning_.Hide();
        bool fireNow = false;
        if (active_) {
            fireTimer_ -= dt;
            if (fireTimer_ <= 0.0f) {
                fireNow = true;
                fireTimer_ = GetFireInterval();
                recoilTimer_ = GetBombardRecoveryTime();
            }
        } else {
            fireTimer_ = (std::min)(fireTimer_, GetFireInterval() * 0.35f);
        }
        UpdateBombardPose();
        if (fireNow) FireBombard();
        BaseGimmick::Update(deltaTime);
        return;
    }

    landingWarning_.Hide();
    engine::Vector3 fireDirection = {};
    const bool canFire = active_ && FindFireDirection(fireDirection);

    if (canFire && GetParameters()->actionMode == 1) {
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
    if (!GetParameters().has_value()) {
        ResetParameters();
    }

    if (!active && !GetParameters()->returnOnOff) {
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
    if (!GetParameters().has_value() || GetParameters()->actionMode == 0) {
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
    const float range = (GetParameters()->detectionRange > 0.0f) ? GetParameters()->detectionRange : kDefaultDetectionRange;

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
    visual.trailInterval = 0.06f;
    visual.trailReverseDirection = true;
    visual.impactDirected = false;
    visual.previewOnly = previewActive_;
    visual.previewOwner = this;

    // 壁や地形では弾を壊さず、プレイヤー命中か寿命終了で消滅させます。
    BulletManager::GetInstance()->Fire(GetMuzzlePosition(direction) + direction * (radius + 0.16f), direction * speed,
                                       CollisionCategories::Get("enemy_attack"), CollisionCategories::Get("player"),
                                       "Gimmicks/volcanic_bombard_shell", radius, life, visual, 1.0f, {}, DamageType::Fire);
}

float GimmickFireCannon::GetBombardRecoveryTime() const {
    // 連射時にも次弾までに元へ戻り、停止中のプレビューで変形を蓄積しません。
    const float configured = std::isfinite(bombardRecoveryTime_) ? bombardRecoveryTime_ : 0.42f;
    return (std::min)(std::clamp(configured, 0.12f, 1.0f), GetFireInterval() * 0.65f);
}

void GimmickFireCannon::UpdateBombardPose() {
    auto* renderer = GetMeshRenderer();
    if (!renderer) return;
    const float strength = std::isfinite(bombardRecoilStrength_) ? std::clamp(bombardRecoilStrength_, 0.0f, 0.45f) : 0.22f;
    float squash = 0.0f;
    float retreat = 0.0f;
    if (recoilTimer_ > 0.0f) {
        const float t = std::clamp(1.0f - recoilTimer_ / GetBombardRecoveryTime(), 0.0f, 1.0f);
        // 一度だけ小さく伸び返し、末尾の速度も0へ収束させます。
        const float spring = (1.0f - t) * (1.0f - t) * std::cos(2.0f * kPi * t);
        squash = strength * spring;
        retreat = strength * 2.0f * spring;
    } else if (active_) {
        const float configured = std::isfinite(bombardAnticipationTime_) ? bombardAnticipationTime_ : 0.20f;
        const float duration = (std::min)(std::clamp(configured, 0.0f, 0.6f), GetFireInterval() * 0.30f);
        if (duration > 0.0001f) {
            float t = std::clamp(1.0f - fireTimer_ / duration, 0.0f, 1.0f);
            t = Easing::SmoothStep(t);
            squash = strength * 0.35f * t;
            retreat = strength * 0.45f * t;
        }
    }
    // 読込後の砲口は-Z。+Z側へ引き、底面の高さは固定して接地を保ちます。
    renderer->SetVisualTransform({1.0f + squash * 0.35f, 1.0f + squash * 0.35f, 1.0f - squash}, {}, {0.0f, 0.0f, retreat});
}

void GimmickFireCannon::FireBombard() {
    // OBJ の +Z 砲口は左手座標への読み込みで -Z になるため、描画された砲口側へ発射します。
    UpdateWorldMatrix();
    auto world = GetWorldMatrix();
    if (const auto* renderer = GetMeshRenderer()) {
        // 弾と発射光は、反動中も実際に描かれる砲口から出します。
        const auto pose = engine::Math::MakeAffineMatrix(renderer->GetVisualScale(), renderer->GetVisualRotation(), renderer->GetVisualOffset());
        world = engine::Math::Multiply(pose, world);
    }
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
        world.m[3][0] + world.m[1][0] * 2.73f - world.m[2][0] * 2.55f,
        world.m[3][1] + world.m[1][1] * 2.73f - world.m[2][1] * 2.55f,
        world.m[3][2] + world.m[1][2] * 2.73f - world.m[2][2] * 2.55f};

    BulletVisualConfig visual;
    visual.visualScale = radius * 0.60f / 1.043f; // 岩の芯を小さくし、外側の炎に輪郭を任せます。
    visual.collisionRadiusIsWorld = true;
    visual.explosionRadius = std::clamp(GetParameters()->healAmount, 0.5f, 8.0f);
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
    visual.trailInterval = 0.04f;
    visual.trailReverseDirection = true;
    visual.impactDirected = false;
    visual.previewOnly = previewActive_;
    visual.previewOwner = this;
    BulletManager::GetInstance()->Fire(muzzle + forward * (radius + 0.16f), forward * speed,
                                       CollisionCategories::Get("enemy_attack"),
                                       CollisionCategories::Get("player"),
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
}

float GimmickFireCannon::GetProjectileSpeed() const {
    // 設定ミスで停止弾を作らないよう最小速度を保証します。
    return GetParameters().has_value() ? (std::max)(1.0f, GetParameters()->speed) : kDefaultProjectileSpeed;
}

float GimmickFireCannon::GetFireInterval() const {
    // ゼロ秒連射による弾の増殖を防ぎます。
    const float interval = GetParameters().has_value() ? (std::max)(0.08f, GetParameters()->interval) : kDefaultFireInterval;
    return previewActive_ ? (std::max)(0.8f, interval) : interval;
}

float GimmickFireCannon::GetProjectileRadius() const {
    // 大型化を妨げないよう上限は設けず、不正値と負値だけを除きます。
    const float configured = GetParameters().has_value() ? GetParameters()->moveAmount : kDefaultProjectileRadius;
    return std::isfinite(configured) ? (std::max)(0.1f, configured) : kDefaultProjectileRadius;
}

float GimmickFireCannon::GetProjectileLifetime() const {
    const float configured = GetParameters().has_value() ? GetParameters()->projectileLifetime : kDefaultProjectileLifetime;
    return std::isfinite(configured) ? std::clamp(configured, 0.2f, 20.0f) : kDefaultProjectileLifetime;
}

float GimmickFireCannon::GetTurnSpeedRadians() const {
    // エディター設定は度毎秒、回転計算はラジアンで扱います。
    const float degrees = GetParameters().has_value() ? (std::max)(1.0f, GetParameters()->moveSpeed) : kDefaultTurnSpeedDegrees;
    return degrees * (kPi / 180.0f);
}

std::unique_ptr<engine::Object3d> GimmickFireCannon::Clone() const {
    // 複製先の予告表示と判定資源を初期化してから設定をコピーします。
    auto newObj = std::make_unique<GimmickFireCannon>();
    assert(GetObjectCommon() != nullptr);
    newObj->Initialize(GetObjectCommon(), GetModelName());
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
    const float blast = std::clamp(GetParameters()->healAmount, 1.0f, 8.0f);
    const float warning = std::clamp(GetParameters()->shakeDuration, 0.35f, 4.0f);
    const float gravity = std::clamp(GetParameters()->gravity, 4.0f, 60.0f);
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
            (std::max)(12.0f, (std::min)(GetParameters()->detectionRange, 20.0f));
        engine::Vector3 target = player ? player->GetWorldPosition() : forwardTarget;
        const engine::Vector3 targetOffset = target - GetWorldPosition();
        if (previewActive_ && targetOffset.x * targetOffset.x + targetOffset.z * targetOffset.z < 100.0f)
            target = forwardTarget;
        if (!previewActive_ && engine::Math::Length(target - GetWorldPosition()) > GetParameters()->detectionRange)
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
    const float apex = (std::max)(muzzle.y, target.y) + std::clamp(GetParameters()->jumpPower, 3.0f, 20.0f);
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
