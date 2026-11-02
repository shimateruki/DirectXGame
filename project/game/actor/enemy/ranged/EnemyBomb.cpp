#define NOMINMAX
#include "EnemyBomb.h"
#include "game/actor/player/Player.h"
#include "game/actor/player/state/PlayerState.h"
#include "game/actor/gimmick/block/GimmickBreakableBlock.h"
#include "Event.h"
#include "game/system/rules/EventManager.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "json.hpp"
#include "game/system/combat/HitEffectDirector.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <MeshEffectManager.h>
#include <DebugConsole.h>

namespace {
// 爆発判定と描画の大きさ、投擲後の接地・反射をそろえる調整値です。
constexpr const char* kExplosionEffectPath = "Resources/json/effect/effect_bakuhatu.json";
constexpr float kFallbackExplosionRadius = 3.0f;
// プレイヤーの設置・投擲は、編集した爆発エフェクトより1.5倍広い爆風でまとめて攻撃する。
constexpr float kPlayerExplosionScale = 1.5f;
constexpr float kGroundDragPerSecond = 10.0f;
constexpr float kStopSpeed = 0.12f;
constexpr float kBombVisualScale = 0.30f;
constexpr float kBombWorldCollisionRadius = 0.48f;
constexpr float kBombCollisionRadius = kBombWorldCollisionRadius / kBombVisualScale;

float MaxScaleComponent(const nlohmann::json& values) {
    // エフェクトの最大軸で球形の爆発判定を広げる。欠損時は等倍にする。
    if (!values.is_array() || values.size() < 3)
        return 1.0f;
    return std::max({values[0].get<float>(), values[1].get<float>(), values[2].get<float>()});
}

// 爆発エフェクトの衝突半径を読み、失敗時は既定半径へ戻します。
float LoadExplosionRadiusFromEffectJson() {
    // 表示用エフェクトの設定から半径を求め、読めない場合は既定半径へ戻す。
    std::ifstream file(kExplosionEffectPath);
    if (!file.is_open())
        return kFallbackExplosionRadius;

    nlohmann::json effectJson;
    try {
        file >> effectJson;
    } catch (...) {
        return kFallbackExplosionRadius;
    }

    const float endScale = effectJson.contains("EndScale") ? MaxScaleComponent(effectJson["EndScale"]) : 1.0f;

    if (effectJson.contains("Collision") && effectJson["Collision"].is_object()) {
        const auto& collision = effectJson["Collision"];

        if (collision.value("HasCollision", false) && collision.value("Shape", -1) == 0 && collision.contains("Size")) {
            return collision["Size"][0].get<float>() * endScale;
        }
    }

    if (effectJson.contains("SphereRadius")) {
        return effectJson["SphereRadius"].get<float>() * endScale;
    }

    return kFallbackExplosionRadius;
}
} // namespace

void EnemyBomb::Initialize(engine::Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    managedBaseScale_ = {kBombVisualScale, kBombVisualScale, kBombVisualScale};
    SetScale(managedBaseScale_);

    state_ = State::Chase;
    fuseTimer_ = 3.0f; // 初期状態では3秒の導火線を用意します。
    flashTimer_ = 0.0f;
    pulseTimer_ = 0.0f;
    flashState_ = false;
    isThrown_ = false;
    isAbilityExecuted_ = false;
    playerOwned_ = false;

    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(kBombCollisionRadius);
    auto collider = GetColliderConfig();
    collider.center = {0.0f, 0.0f, 0.0f};
    SetColliderConfig(collider);
    if (GetCollider())
        GetCollider()->SetScaleOverride(managedBaseScale_);

    // 通常時は敵として接触を受け付け、爆発時に属性を切り替えます。
    SetCollisionAttribute(CollisionCategories::Get("enemy"));
    SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
}

void EnemyBomb::ApplyManagedScale(const engine::Vector3& scale) {
    // 点滅による一時変形と区別できるよう、通常時の基準スケールを保持する。
    managedBaseScale_ = scale;
    SetScale(scale);
    // 点滅の伸縮と回転では取得・接地半径を変えない。
    if (GetCollider())
        GetCollider()->SetScaleOverride(managedBaseScale_);
}

// 運搬、投擲、通常移動では位置の更新担当が異なるため、処理を分けます。
void EnemyBomb::Update(float deltaTime) {
    if (state_ == State::Exploded) {
        // 爆発判定を出した次の更新で、再接触と再描画を止めます。
        HideAttackTelegraph();
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        return;
    }

    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        return;
    if (ShouldHandleDefeatEffect() || IsDormant()) {
        BaseEnemy::Update(deltaTime);
        return;
    }

    if (IsCarried()) {
        // 運搬中はプレイヤーが位置を管理し、導火線だけを進める。
        if (state_ != State::Ignited)
            Ignite(3.0f);
        UpdateIgnited(deltaTime);
        return;
    }

    if (state_ == State::Chase)
        UpdateChase(deltaTime);
    else
        UpdateIgnited(deltaTime);
    if (state_ == State::Exploded)
        return;

    // 感知・被弾・表示の共通処理は残し、移動の積分は専用Motorへ一本化する。
    SetMotorSuspended(true);
    BaseEnemy::Update(deltaTime);
    SetMotorSuspended(false);
    if (IsDead() || ShouldHandleDefeatEffect())
        return;
    UpdateBombPhysics(deltaTime);

    // 実際の爆発位置は接地後の移動で決まる。現在の爆弾を中心に判定半径を示す。
    if (state_ == State::Ignited && !IsCarried() && !playerOwned_ && IsGrounded()) {
        const float progress = std::clamp(1.0f - fuseTimer_ / 2.8f, 0.0f, 1.0f);

        // 爆弾の中心は床より高い。予告円だけ足元の接地面へ投影する。
        const engine::Vector3 bombPosition = GetTranslate();
        const RaycastHit ground = CollisionManager::GetInstance()->Raycast(
            bombPosition + engine::Vector3{0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 4.0f,
            CollisionCategories::Get("ground"));
        if (ground.isHit) {
            ShowAttackTelegraphCircle({bombPosition.x, ground.hitPoint.y + 0.02f, bombPosition.z},
                                      explosionRadius_, progress, {1.0f, 0.30f, 0.08f, 0.72f});
        } else {
            HideAttackTelegraph();
        }
    } else {
        HideAttackTelegraph();
    }
}

// 爆弾はキャラクター用の投げ反射を使わず、一つのMotorで床・壁を解決する。
void EnemyBomb::UpdateBombPhysics(float deltaTime) {
    if (deltaTime <= 0.0f)
        return;
    SyncCharacterMotorSettings();
    auto& motor = GetCharacterMotor();
    auto settings = motor.GetSettings();
    settings.continuousCollision = true;
    settings.snapToGround = true;
    settings.stepHeight = 0.0f;
    settings.groundProbeDistance = 0.06f;
    settings.skinWidth = 0.012f;
    motor.SetSettings(settings);

    // 大きなフレームでも同じ軌道と減速になるよう、計算量を制限して分割する。
    const int steps = std::clamp(static_cast<int>(std::ceil(std::min(deltaTime, 0.25f) * 120.0f)), 1, 32);
    const float step = std::min(deltaTime, 0.25f) / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        if (IsGrounded() && state_ == State::Ignited) {
            const float keep = std::exp(-kGroundDragPerSecond * step);
            SetVelocityX(GetVelocity().x * (keep));
            SetVelocityZ(GetVelocity().z * (keep));
            if (GetVelocity().x * GetVelocity().x + GetVelocity().z * GetVelocity().z < kStopSpeed * kStopSpeed) {
                SetVelocityX(SetVelocityZ(0.0f));
                isThrown_ = false;
            }
        }
        auto position = GetTranslate();
        SimulateMotorStep(position, GetParameters() ? GetParameters()->gravity : 60.0f,
                          GetParameters() ? GetParameters()->maxFallSpeed : 60.0f, step);
        SetTranslate(position);
        UpdateWorldMatrix(false);
        if (IsGrounded()) {
            const float radius = std::max(0.05f, GetCollider()->GetRadius());
            auto rotation = GetRotation();
            rotation.x += GetVelocity().z * step / radius;
            rotation.z -= GetVelocity().x * step / radius;
            SetRotation(rotation);
        }
    }
}

void EnemyBomb::BeginThrown(const engine::Vector3& initialVelocity) {
    // スライム共通の着地バウンド・起き上がりへ入らず、残りの導火線を保つ。
    SetCarried(false);
    CancelNoticeReaction();
    if (state_ != State::Ignited)
        Ignite(3.0f);
    SetGrounded(false);
    SetVelocity(initialVelocity);
}

void EnemyBomb::UpdateChase(float deltaTime) {
    if (!GetTarget())
        return;
    engine::Vector3 playerPos = GetTarget()->GetTranslate();
    engine::Vector3 myPos = GetTranslate();
    engine::Vector3 toPlayer = playerPos - myPos;
    toPlayer.y = 0.0f; // 追跡は水平面で行い、高低差では点火距離を変えません。

    float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);

    if (UpdateNoticeReaction(deltaTime, distance, GetDetectionRange(), toPlayer)) {
        SetVelocityX(0.0f);
        SetVelocityZ(0.0f);

        if (distance > 0.001f) {
            SetRotationY(std::atan2(toPlayer.x, toPlayer.z));
        }

        return;
    }

    if (distance > GetDetectionRange()) {
        const float wanderSpeed = GetParameters().has_value() ? (std::max)(0.35f, GetParameters().value().speed * 4.0f) : 0.55f;
        engine::Vector3 wanderVelocity = CalculateWanderVelocity(deltaTime, wanderSpeed, 0.65f);
        SetVelocityX(wanderVelocity.x);
        SetVelocityZ(wanderVelocity.z);

        const float speedXZ = std::sqrt(GetVelocity().x * GetVelocity().x + GetVelocity().z * GetVelocity().z);

        if (speedXZ > 0.05f) {
            SetRotationY(std::atan2(GetVelocity().x, GetVelocity().z));
        }

        return;
    }

    // プレイヤーが点火距離に入った時だけ追跡から導火線へ移ります。
    if (distance <= 4.0f) {
        Ignite(3.0f); // 近接点火時も予告と爆発で同じ半径を使います。
        SetVelocityX(0.0f);
        SetVelocityZ(0.0f);
        return;
    }

    // 検知範囲内ではプレイヤーへ向かって移動します。
    if (distance > 0.1f && GetParameters().has_value()) {
        toPlayer.x /= distance;
        toPlayer.z /= distance;
        SetVelocityX(toPlayer.x * GetParameters().value().speed);
        SetVelocityZ(toPlayer.z * GetParameters().value().speed);

        // 進行方向へ向きを合わせます。
        SetRotationY(std::atan2(toPlayer.x, toPlayer.z));
    }
}

// 点火後の減速、導火線、点滅と脈動を更新します。
void EnemyBomb::UpdateIgnited(float deltaTime) {
    // 空中の水平速度は保ち、接地時の減速はUpdateBombPhysicsで扱う。
    // 導火線が尽きたら一度だけ爆発へ移行します。
    fuseTimer_ -= deltaTime;

    if (fuseTimer_ <= 0.0f) {
        Explode();
        return;
    }

    // 残り時間が短いほど赤い点滅の間隔を詰め、爆発の近さを知らせます。
    flashTimer_ += deltaTime;

    // 導火線の残り時間から次の点滅までの間隔を決めます。
    float flashLimit = (fuseTimer_ / 3.0f) * 0.2f + 0.03f;

    if (flashTimer_ >= flashLimit) {
        flashTimer_ = 0.0f;
        flashState_ = !flashState_;

        if (flashState_) {
            // 点灯中は赤い発光色を使います。
            SetColor({2.0f, 0.2f, 0.2f, 1.0f});
        } else {
            // 消灯中は元の色へ戻します。
            SetColor(GetDefaultColor());
        }
    }

    // 点滅とは別に小さな脈動を重ね、点火状態を読み取りやすくします。
    // 残り時間に応じて脈動の速度を上げます。
    float pulseSpeed = (3.4f - fuseTimer_) * 3.0f;
    pulseTimer_ += deltaTime * pulseSpeed;

    // 大きな変形は転がりと競合するため、脈動の振幅は控えめにします。
    float progress = 1.0f - (fuseTimer_ / 3.0f);
    if (progress < 0.0f)
        progress = 0.0f;
    if (progress > 1.0f)
        progress = 1.0f;
    float amplitude = 0.015f + 0.045f * progress;
    float offset = std::sin(pulseTimer_) * amplitude;
    const float scaleRate = 1.0f + offset;

    // 見た目のスケールだけを変え、衝突半径を揺らしません。
    engine::Vector3 targetScale = {managedBaseScale_.x * scaleRate, managedBaseScale_.y * scaleRate,
                           managedBaseScale_.z * scaleRate};
    engine::Vector3 reactionRotation = engine::Vector3::ZERO;
    ApplyDamageReactionPose(targetScale, reactionRotation, nullptr, 0.90f);
    SetScale(targetScale);
}

void EnemyBomb::Explode() {
    // 接触と導火線が同じフレームに成立しても、爆風を二度発生させない。
    if (state_ == State::Exploded)
        return;
    state_ = State::Exploded;
    SetScale(managedBaseScale_);
    UpdateWorldMatrix(false);

    // 爆発エフェクトと判定を出した後は、本体を表示しません。
    SetIsVisible(false);

    // プレイヤーの爆風はワールド単位で扱い、小さな爆弾モデルの縮小率を掛けない。
    // 敵の爆弾は既存の予告・攻撃設定を維持する。
    const float effectScale = playerOwned_ ? kPlayerExplosionScale : 1.0f;
    SetColliderType(ColliderType::kSphere);
    if (playerOwned_ && GetCollider())
        GetCollider()->SetScaleOverride({1.0f, 1.0f, 1.0f});
    SetCollisionRadius(explosionRadius_ * effectScale);

    // 所有者によって攻撃属性と対象マスクを切り替えます。
    // 投げ返した爆弾がプレイヤーへ即座に当たるのを防ぎます。
    if (playerOwned_) {
        SetCollisionAttribute(CollisionCategories::Get("player_attack"));
        SetCollisionMask(CollisionCategories::Get("enemy") | CollisionCategories::Get("ground"));
    } else {
        SetCollisionAttribute(CollisionCategories::Get("player_attack") | CollisionCategories::Get("enemy_attack"));
        SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("enemy") | CollisionCategories::Get("ground"));
    }

    // 命中演出とメッシュエフェクトは同じ爆発位置から発生させます。
    engine::Vector3 myPos = GetWorldPosition();
    auto* effects = MeshEffectManager::GetInstance();
    const size_t firstNewEffect = effects ? effects->GetActiveEffects().size() : 0;
    HitEffectDirector::SpawnBombExplosionHit(myPos);

    if (effects) {
        effects->SpawnEffectAt(kExplosionEffectPath, myPos, {0.0f, 0.0f, 0.0f},
                               {effectScale, effectScale, effectScale});
        if (playerOwned_) {
            // 表示の膨張で後から命中させず、この下の瞬間的な爆風だけを攻撃判定にする。
            const auto& activeEffects = effects->GetActiveEffects();
            for (size_t i = firstNewEffect; i < activeEffects.size(); ++i) {
                auto* visual = activeEffects[i].get();
                if (visual->GetMeshSettings().hasCollision)
                    CollisionManager::GetInstance()->RemoveObject(visual);
                visual->SetMeshSetting(&EffectObject3d::MeshSettings::hasCollision, false);
                visual->SetCollisionAttribute(0);
                visual->SetCollisionMask(0);
            }
        }
    }

    if (playerOwned_ && GetCollider()) {
        // 導火線と直撃を同じ経路へ集約する。消滅前に爆風内の敵と破壊壁へ一度だけ通知する。
        PhysicsQueryFilter filter;
        filter.mask = CollisionCategories::Get("enemy") | CollisionCategories::Get("ground");
        filter.ignoredObject = this;
        for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(
                 myPos, GetCollider()->GetRadius(), filter)) {
            engine::Object3d* target = hit.object;
            if ((dynamic_cast<BaseEnemy*>(target) || dynamic_cast<GimmickBreakableBlock*>(target)) &&
                (GetCollisionMask() & target->GetCollisionAttribute()) &&
                (target->GetCollisionMask() & GetCollisionAttribute())) {
                target->OnCollision(this);
            }
        }
        // 既に通知した爆風を通常の衝突パスへ残さず、同フレームの二重命中を防ぐ。
        SetCollisionAttribute(0);
        SetCollisionMask(0);
    }

    // 爆発済みの本体を敵の生存判定に残さないよう、HPをゼロへします。
    if (GetParameters().has_value()) {
        SetParameter(&engine::EntityParameter::hp, 0.0f);
    }

    // 共通の敵管理にも死亡を知らせ、後続の更新対象から外します。
    SetDead(true);
}

void EnemyBomb::SetCarried(bool isCarried) {
    BaseEnemy::SetCarried(isCarried);

    if (isCarried) {
        // 運搬開始時に必ず点火し、投擲直後の状態を解除します。
        Ignite(3.0f);
        isThrown_ = false;
    } else {
        // 運搬解除後はプレイヤーから与えられた速度で投擲物理を進めます。
        // 投げ返し後は所有者に応じて接触対象を更新します。
        isThrown_ = true;
        SetThrowRecoveryTime(0.0f);

        if (playerOwned_) {
            SetCollisionAttribute(CollisionCategories::Get("player_attack"));
            SetCollisionMask(CollisionCategories::Get("enemy") | CollisionCategories::Get("ground"));
        } else {
            SetCollisionAttribute(CollisionCategories::Get("enemy"));
            SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
        }
    }
}

void EnemyBomb::SetPlayerOwned(bool playerOwned) {
    // 投げ返し後は攻撃の所有者に合わせ、接触属性と対象マスクを切り替える。
    playerOwned_ = playerOwned;

    if (playerOwned_) {
        SetCollisionAttribute(CollisionCategories::Get("player_attack"));
        SetCollisionMask(CollisionCategories::Get("enemy") | CollisionCategories::Get("ground"));
    } else {
        SetCollisionAttribute(CollisionCategories::Get("enemy"));
        SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
    }
}

void EnemyBomb::Ignite(float fuseTime) {
    // 再点火時に光の周期も戻し、導火線の開始と警告表示を同期する。
    state_ = State::Ignited;
    fuseTimer_ = fuseTime;
    explosionRadius_ = LoadExplosionRadiusFromEffectJson();
    pulseTimer_ = 0.0f;
    flashTimer_ = 0.0f;
    flashState_ = false;
}

void EnemyBomb::ExecuteAbility(Player* player) {
    if (isAbilityExecuted_)
        return;
    isAbilityExecuted_ = true;

    // ボムを吸い込んだ際の能力は、プレイヤーを巻き込む自爆です。
    engine::Vector3 myPos = GetTranslate();
    engine::Vector3 playerPos = player->GetTranslate();

    // 爆発時のノックバック方向をボムからプレイヤーへ向けます。
    engine::Vector3 throwBackDir = playerPos - myPos;
    throwBackDir.y = 0.0f;
    float dist = std::sqrt(throwBackDir.x * throwBackDir.x + throwBackDir.z * throwBackDir.z);

    if (dist > 0.001f) {
        throwBackDir.x /= dist;
        throwBackDir.z /= dist;
    } else {
        throwBackDir = {0.0f, 0.0f, -1.0f}; // 位置が重なる場合のノックバック方向です。
    }

    throwBackDir.y = 0.7f; // 少し上向きにして吹き飛びが見えるようにします。

    // ダメージ状態への遷移を先に通知します。
    player->ChangeState(std::make_unique<PlayerStateDamage>(throwBackDir));

    // その後に爆発演出と本体の消滅を処理します。
    Explode();
}

bool EnemyBomb::OnCollision(engine::Object3d* other) {
    // プレイヤーの爆弾は敵との実際の接触で導火線を待たずに爆発させます。
    // 爆発後に同じフレームで再接触しても、演出や攻撃判定を作り直しません。
    if (playerOwned_ && !IsCarried() && state_ == State::Ignited &&
        dynamic_cast<BaseEnemy*>(other) && (other->GetCollisionAttribute() & CollisionCategories::Get("enemy"))) {
        Explode();


        return true;
    }

    // 床・壁は専用Motorで接地と速度を解決し、汎用の投げ反射へ入らない。
    return BaseEnemy::OnCollision(other);
}
