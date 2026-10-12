#define NOMINMAX
#include "EnemyBomb.h"
#include "game/actor/player/Player.h"
#include "game/actor/player/state/PlayerState.h"
#include "Event.h"
#include "EventManager.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "json.hpp"
#include "HitEffectDirector.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <MeshEffectManager.h>
#include <DebugConsole.h>

namespace {
// 爆発判定と描画の大きさ、投擲後の接地・反射をそろえる調整値です。
constexpr const char* kExplosionEffectPath = "Resources/json/effect/effect_bakuhatu.json";
constexpr float kFallbackExplosionRadius = 3.0f;
constexpr float kGroundFriction = 0.985f;
constexpr float kStopSpeed = 0.02f;
constexpr float kBombVisualScale = 0.30f;
constexpr float kBombWorldCollisionRadius = 0.48f;
constexpr float kBombCollisionRadius = kBombWorldCollisionRadius / kBombVisualScale;
constexpr float kBombSweepSkin = 0.08f;
constexpr float kBombLandingBounce = 0.22f;
constexpr float kBombLandingFriction = 0.72f;
constexpr float kBombWallBounce = 0.35f;

float MaxScaleComponent(const nlohmann::json& values) {
    // エフェクトの最大軸で球形の爆発判定を広げる。欠損時は等倍にする。
    if (!values.is_array() || values.size() < 3)
        return 1.0f;
    return std::max({values[0].get<float>(), values[1].get<float>(), values[2].get<float>()});
}

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
void EnemyBomb::Initialize(Object3dCommon* common, const std::string& modelName) {
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
    // 通常時は敵として接触を受け付け、爆発時に属性を切り替えます。
    SetCollisionAttribute(CollisionCategories::Get("enemy"));
    SetCollisionMask(CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet"));
}

void EnemyBomb::ApplyManagedScale(const Vector3& scale) {
    // 点滅による一時変形と区別できるよう、通常時の基準スケールを保持する。
    managedBaseScale_ = scale;
    SetScale(scale);
}

// 運搬、投擲、通常移動では位置の更新担当が異なるため、処理を分けます。
void EnemyBomb::Update(float deltaTime) {
    if (state_ == State::Exploded) {
        // 爆発判定を出した次の更新で、再接触と再描画を止めます。
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        return;
    }

    const Vector3 previousPosition = GetTranslate();

    // 運搬が始まった瞬間だけ点火し、毎フレーム導火線を戻さないようにします。
    if (isCarried_ && state_ != State::Ignited) {
        state_ = State::Ignited;
        fuseTimer_ = 3.0f; // 運搬開始時に導火線を3秒へ戻します。
        pulseTimer_ = 0.0f;
        flashTimer_ = 0.0f;
        flashState_ = false;
    }

    if (isCarried_) {
        // 運搬中の位置はプレイヤー側が決めるため、導火線と警告演出だけ進めます。
        UpdateIgnited(deltaTime);
    } else if (IsThrownPhysics()) {
        UpdateIgnited(deltaTime);
        if (state_ != State::Exploded) {
            BaseEnemy::Update(deltaTime);
            ResolveSweptCollision(previousPosition);
        }
    } else {
        // 運搬中と投擲物理中以外は、ここで重力と地上移動を扱います。
        // 敵パラメータの重力を優先し、未設定なら固定値を使います。
        if (param_.has_value()) {
            velocity_.y -= param_.value().gravity * deltaTime;
        } else {
            velocity_.y -= 50.0f * deltaTime; // 敵パラメータ未設定時の重力です。
        }

        // 接地した投擲体は水平速度を少しずつ落とし、自然に停止させます。
        // 極小速度をゼロにすることで、摩擦後の微小な移動が残るのを防ぎます。
        // この減速は投擲後の接地移動だけに適用します。
        if (isGrounded_ && isThrown_) {
            // 地上では水平速度を減衰させます。
            velocity_.x *= kGroundFriction;
            velocity_.z *= kGroundFriction;

            if (std::abs(velocity_.x) < kStopSpeed)
                velocity_.x = 0.0f;
            if (std::abs(velocity_.z) < kStopSpeed)
                velocity_.z = 0.0f;

            // 停止後は投擲状態を解除し、通常の状態更新へ戻します。
            if (velocity_.x == 0.0f && velocity_.z == 0.0f) {
                isThrown_ = false;
            }

            // 移動方向を向きに反映し、地面を転がる見た目だけを加えます。
            float speedXZ = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
            if (speedXZ > 0.05f) {
                // 水平速度から進行方向を求めます。
                float dirX = velocity_.x / speedXZ;
                float dirZ = velocity_.z / speedXZ;

                // 進行方向に胴体を向けます。
                SetRotationY(std::atan2(dirX, dirZ));

                // 水平移動距離に応じて転がりの角度を進めます。
                // モデルのピッチだけを変え、衝突半径には影響させません。
                float rollDelta = speedXZ * deltaTime * 0.5f; // 転がり角度は速度と経過時間に比例します。
                Vector3 rot = GetRotation();
                rot.x += rollDelta;
                SetRotation(rot);
            }
        }

        // 追跡または点火の状態処理を共通移動より先に進めます。
        switch (state_) {
        case State::Chase:
            UpdateChase(deltaTime);
            break;
        case State::Ignited:
            UpdateIgnited(deltaTime);
            break;
        }

        if (state_ == State::Chase) {
            Vector3 targetScale = managedBaseScale_;
            Vector3 reactionRotation = {0.0f, 0.0f, 0.0f};
            ApplyDamageReactionPose(targetScale, reactionRotation, nullptr, 0.90f);
            SetScale(Math::Lerp(GetScale(), targetScale, (std::min)(1.0f, deltaTime * 15.0f)));
        }

        // 共通の敵更新で移動を確定してから、掃引で床や壁との衝突を補正します。
        BaseEnemy::Update(deltaTime);
        ResolveSweptCollision(previousPosition);
    }
}

// 前フレームからの移動区間を掃引し、高速の投擲体が薄い床や壁を抜けるのを防ぎます。
void EnemyBomb::ResolveSweptCollision(const Vector3& previousPosition) {
    // 現在位置だけの重なり判定では取り逃す床・壁を移動線分から探す。
    if (state_ == State::Exploded || isCarried_) {
        return;
    }

    CollisionManager* collisionManager = CollisionManager::GetInstance();
    if (!collisionManager) {
        return;
    }

    const Vector3 currentPosition = GetTranslate();
    Vector3 movement = {currentPosition.x - previousPosition.x, currentPosition.y - previousPosition.y,
                        currentPosition.z - previousPosition.z};

    const float movementLength = std::sqrt(movement.x * movement.x + movement.y * movement.y + movement.z * movement.z);
    if (movementLength <= 0.001f) {
        return;
    }

    Vector3 direction = {movement.x / movementLength, movement.y / movementLength, movement.z / movementLength};

    const float sweepLength = movementLength + kBombWorldCollisionRadius + kBombSweepSkin;
    RaycastHit hit = collisionManager->Raycast(previousPosition, direction, sweepLength, CollisionCategories::Get("ground"));
    if (!hit.isHit) {
        return;
    }

    Vector3 normal = hit.normal;
    float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (normalLength <= 0.001f) {
        if (direction.y < -0.15f) {
            normal = {0.0f, 1.0f, 0.0f};
            normalLength = 1.0f;
        } else {
            normal = {-direction.x, 0.0f, -direction.z};
            normalLength = std::sqrt(normal.x * normal.x + normal.z * normal.z);
            if (normalLength <= 0.001f) {
                normal = {0.0f, 1.0f, 0.0f};
                normalLength = 1.0f;
            }
        }
    }

    normal.x /= normalLength;
    normal.y /= normalLength;
    normal.z /= normalLength;

    const float separation = kBombWorldCollisionRadius + kBombSweepSkin;
    SetTranslate({hit.hitPoint.x + normal.x * separation, hit.hitPoint.y + normal.y * separation,
                  hit.hitPoint.z + normal.z * separation});

    const float velocityAlongNormal = velocity_.x * normal.x + velocity_.y * normal.y + velocity_.z * normal.z;

    if (normal.y > 0.35f) {
        isGrounded_ = true;
        if (velocity_.y < 0.0f) {
            velocity_.y = -velocity_.y * kBombLandingBounce;
        }
        if (std::abs(velocity_.y) < 0.85f) {
            velocity_.y = 0.0f;
        }
        velocity_.x *= kBombLandingFriction;
        velocity_.z *= kBombLandingFriction;
    } else if (velocityAlongNormal < 0.0f) {
        const float reflectionScale = (1.0f + kBombWallBounce) * velocityAlongNormal;
        velocity_.x -= normal.x * reflectionScale;
        velocity_.y -= normal.y * reflectionScale;
        velocity_.z -= normal.z * reflectionScale;
    }
}

void EnemyBomb::UpdateChase(float deltaTime) {
    if (!target_)
        return;
    Vector3 playerPos = target_->GetTranslate();
    Vector3 myPos = GetTranslate();
    Vector3 toPlayer = playerPos - myPos;
    toPlayer.y = 0.0f; // 追跡は水平面で行い、高低差では点火距離を変えません。

    float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);

    if (UpdateNoticeReaction(deltaTime, distance, detectionRange_, toPlayer)) {
        velocity_.x = 0.0f;
        velocity_.z = 0.0f;
        if (distance > 0.001f) {
            SetRotationY(std::atan2(toPlayer.x, toPlayer.z));
        }
        return;
    }

    if (distance > detectionRange_) {
        const float wanderSpeed = param_.has_value() ? (std::max)(0.35f, param_.value().speed * 4.0f) : 0.55f;
        Vector3 wanderVelocity = CalculateWanderVelocity(deltaTime, wanderSpeed, 0.65f);
        velocity_.x = wanderVelocity.x;
        velocity_.z = wanderVelocity.z;

        const float speedXZ = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
        if (speedXZ > 0.05f) {
            SetRotationY(std::atan2(velocity_.x, velocity_.z));
        }
        return;
    }

    // プレイヤーが点火距離に入った時だけ追跡から導火線へ移ります。
    if (distance <= 4.0f) {
        state_ = State::Ignited;
        fuseTimer_ = 3.0f; // 近接点火時の導火線を3秒に設定します。
        pulseTimer_ = 0.0f;
        flashTimer_ = 0.0f;
        flashState_ = false;
        velocity_.x = 0.0f;
        velocity_.z = 0.0f;
        return;
    }

    // 検知範囲内ではプレイヤーへ向かって移動します。
    if (distance > 0.1f && param_.has_value()) {
        toPlayer.x /= distance;
        toPlayer.z /= distance;
        velocity_.x = toPlayer.x * param_.value().speed;
        velocity_.z = toPlayer.z * param_.value().speed;

        // 進行方向へ向きを合わせます。
        SetRotationY(std::atan2(toPlayer.x, toPlayer.z));
    }
}

// 点火後の減速、導火線、点滅と脈動を更新します。
void EnemyBomb::UpdateIgnited(float deltaTime) {
    // 接地後の転がりはUpdate側の摩擦に任せます。
    // ここでは空中や未投擲の状態だけ水平速度を抑えます。
    // これにより点火後の急停止と投擲後の自然な減速を両立します。
    // 運搬中は位置がプレイヤーに管理されるため、速度を変更しません。
    if (!isCarried_ && !isThrown_ && !isGrounded_) {
        velocity_.x *= 0.8f;
        velocity_.z *= 0.8f;
    }

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
            SetColor(defaultColor_);
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
    Vector3 targetScale = {managedBaseScale_.x * scaleRate, managedBaseScale_.y * scaleRate,
                           managedBaseScale_.z * scaleRate};
    Vector3 reactionRotation = {0.0f, 0.0f, 0.0f};
    ApplyDamageReactionPose(targetScale, reactionRotation, nullptr, 0.90f);
    SetScale(targetScale);
}

void EnemyBomb::Explode() {
    state_ = State::Exploded;
    SetScale(managedBaseScale_);

    // 爆発エフェクトと判定を出した後は、本体を表示しません。
    SetIsVisible(false);

    // 爆発判定の半径はエフェクト設定から読み、見た目と攻撃範囲をそろえます。
    // 判定はこのフレームにだけ出し、次のUpdateで無効化します。
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(LoadExplosionRadiusFromEffectJson());

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
    Vector3 myPos = GetTranslate();
    HitEffectDirector::SpawnBombExplosionHit(myPos);
    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(kExplosionEffectPath, myPos, {0.0f, 0.0f, 0.0f},
                                                        {1.0f, 1.0f, 1.0f});
    }

    // 爆発済みの本体を敵の生存判定に残さないよう、HPをゼロへします。
    if (param_.has_value()) {
        param_.value().hp = 0.0f;
    }
    // 共通の敵管理にも死亡を知らせ、後続の更新対象から外します。
    isDead = true;
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
        throwRecoveryTimer_ = 0.0f;
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
    pulseTimer_ = 0.0f;
    flashTimer_ = 0.0f;
    flashState_ = false;
}

void EnemyBomb::ExecuteAbility(Player* player) {
    if (isAbilityExecuted_)
        return;
    isAbilityExecuted_ = true;

    // ボムを吸い込んだ際の能力は、プレイヤーを巻き込む自爆です。
    Vector3 myPos = GetTranslate();
    Vector3 playerPos = player->GetTranslate();

    // 爆発時のノックバック方向をボムからプレイヤーへ向けます。
    Vector3 throwBackDir = playerPos - myPos;
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

bool EnemyBomb::OnCollision(Object3d* other) {
    // 投擲物理の床・壁反射はBaseEnemyと掃引処理が担当します。
    // ここでは共通の接触処理だけを呼び、爆発の多重発動を避けます。
    return BaseEnemy::OnCollision(other);
}
