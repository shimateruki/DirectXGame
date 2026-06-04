#define NOMINMAX
#include "EnemyBomb.h"
#include "game/actor/player/Player.h"
#include "PlayerState.h"
#include "Event.h"          
#include "EventManager.h" 
#include "CollisionConfig.h"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <MeshEffectManager.h>
#include <DebugConsole.h>

namespace {
constexpr const char* kExplosionEffectPath = "Resources/json/effect/effect_bakuhatu.json";
constexpr float kFallbackExplosionRadius = 3.0f;
constexpr float kGroundFriction = 0.985f;
constexpr float kStopSpeed = 0.02f;

float MaxScaleComponent(const nlohmann::json& values) {
    if (!values.is_array() || values.size() < 3) return 1.0f;
    return std::max({ values[0].get<float>(), values[1].get<float>(), values[2].get<float>() });
}

float LoadExplosionRadiusFromEffectJson() {
    std::ifstream file(kExplosionEffectPath);
    if (!file.is_open()) return kFallbackExplosionRadius;

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
}

void EnemyBomb::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    
    state_ = State::Chase;
    fuseTimer_ = 3.0f; // 爆発する時間を少し延長（3秒）
    flashTimer_ = 0.0f;
    pulseTimer_ = 0.0f;
    flashState_ = false;
    isThrown_ = false;
    isAbilityExecuted_ = false;

    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(1.0f);
    // 当たり判定の初期属性とマスク設定（野生の敵属性）
    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kPlayerAttack);
}

void EnemyBomb::Update(float deltaTime) {
    if (state_ == State::Exploded) {
        // 爆発完了後は当たり判定を完全に無効化して非表示にする
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        return;
    }

    // プレイヤーに掴まれた瞬間、即座に点火（Ignited）状態へ移行する
    if (isCarried_ && state_ != State::Ignited) {
        state_ = State::Ignited;
        fuseTimer_ = 3.0f; // 爆発時間を3秒にリセット
        pulseTimer_ = 0.0f;
        flashTimer_ = 0.0f;
        flashState_ = false;
    }

    if (isCarried_) {
        // 掴まれている間は移動や重力処理は行わないが、カウントダウンと演出は更新する
        UpdateIgnited(deltaTime);
    } else if (IsThrownPhysics()) {
        UpdateIgnited(deltaTime);
        if (state_ != State::Exploded) {
            BaseEnemy::Update(deltaTime);
        }
    } else {
        // 通常時（地面にいる、または投げられた状態）
        // 速度に重力を適用する
        if (param_.has_value()) {
            velocity_.y -= param_.value().gravity * deltaTime;
        } else {
            velocity_.y -= 50.0f * deltaTime; // フォールバック
        }

        // 接地している場合かつ、投げられた後のみ、フリクション（地面の摩擦）を適用して少し転がってから自然に止まるようにする
        // ※野生の追尾歩行時（speed = 0.04f）は摩擦でかき消されないようにします。
        if (isGrounded_ && isThrown_) {
            // 着地後に少し長めに転がす
            velocity_.x *= kGroundFriction;
            velocity_.z *= kGroundFriction;

            if (std::abs(velocity_.x) < kStopSpeed) velocity_.x = 0.0f;
            if (std::abs(velocity_.z) < kStopSpeed) velocity_.z = 0.0f;

            // 完全に静止したら投擲状態を解除する
            if (velocity_.x == 0.0f && velocity_.z == 0.0f) {
                isThrown_ = false;
            }

            // 地面を転がる球体のビジュアル回転演出
            float speedXZ = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
            if (speedXZ > 0.05f) {
                // 進行方向のベクトルを取得
                float dirX = velocity_.x / speedXZ;
                float dirZ = velocity_.z / speedXZ;

                // 進行方向を向かせる (Y軸回転)
                SetRotationY(std::atan2(dirX, dirZ));

                // 転がる回転アニメーション (X軸回転に進行速度分の回転を加算)
                float rollDelta = speedXZ * deltaTime * 0.5f; // 球がゴロゴロ転がる速さ
                Vector3 rot = GetRotation();
                rot.x += rollDelta;
                SetRotation(rot);
            }
        }

        // ステートマシン更新
        switch (state_) {
        case State::Chase:
            UpdateChase(deltaTime);
            break;
        case State::Ignited:
            UpdateIgnited(deltaTime);
            break;
        }

        // 移動制限や座標更新は BaseEnemy::Update が行ってくれる
        BaseEnemy::Update(deltaTime);
    }
}

void EnemyBomb::UpdateChase(float deltaTime) {
    if (!target_) return;

    // プレイヤーへのベクトルを計算
    Vector3 playerPos = target_->GetTranslate();
    Vector3 myPos = GetTranslate();
    Vector3 toPlayer = playerPos - myPos;
    toPlayer.y = 0.0f; // 高低差は無視して水平移動

    float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);

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

    // 一定距離内に入ったら点火（Ignited）に移行
    if (distance <= 4.0f) {
        state_ = State::Ignited;
        fuseTimer_ = 3.0f; // 爆発時間を3秒に設定
        pulseTimer_ = 0.0f;
        flashTimer_ = 0.0f;
        flashState_ = false;
        velocity_.x = 0.0f;
        velocity_.z = 0.0f;
        return;
    }

    // じわじわプレイヤーへ近づく
    if (distance > 0.1f && param_.has_value()) {
        toPlayer.x /= distance;
        toPlayer.z /= distance;
        velocity_.x = toPlayer.x * param_.value().speed;
        velocity_.z = toPlayer.z * param_.value().speed;

        // 進行方向を向く
        SetRotationY(std::atan2(toPlayer.x, toPlayer.z));
    }
}

void EnemyBomb::UpdateIgnited(float deltaTime) {
    // 速度を徐々に減衰（点火したら立ち止まる、または投げられた慣性で滑る）
    // 接地時（isGrounded_ == true）の転がりフリクションは Update() 側でより滑らかに行うため、
    // ここでは空中など非接地時かつ未投擲時のブレーキのみ行います。
    if (!isCarried_ && !isThrown_ && !isGrounded_) {
        velocity_.x *= 0.8f;
        velocity_.z *= 0.8f;
    }

    // カウントダウン進行
    fuseTimer_ -= deltaTime;

    if (fuseTimer_ <= 0.0f) {
        Explode();
        return;
    }

    // --- 演出部 1: 導火線点火を表現する赤点滅演出（3.0秒スケール対応） ---
    flashTimer_ += deltaTime;
    // 爆発が迫るほど点滅が速くなる
    float flashLimit = (fuseTimer_ / 3.0f) * 0.2f + 0.03f;
    if (flashTimer_ >= flashLimit) {
        flashTimer_ = 0.0f;
        flashState_ = !flashState_;
        
        if (flashState_) {
            // ドクンと光る瞬間（強い赤色にオーバーライト）
            SetColor({2.0f, 0.2f, 0.2f, 1.0f});
        } else {
            // 通常の色に戻す
            SetColor(defaultColor_);
        }
    }

    // --- 演出部 2: 爆発寸前のドクンドクンと波打つ鼓動（3.0秒スケール対応） ---
    // 爆発に近づくにつれて鼓動周波数を上げる
    float pulseSpeed = (3.4f - fuseTimer_) * 3.0f;
    pulseTimer_ += deltaTime * pulseSpeed;

    // 爆発直前ほど少しだけ膨らむ。大きな潰れ変形は転がりと干渉して不自然に見える。
    float progress = 1.0f - (fuseTimer_ / 3.0f);
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    float amplitude = 0.015f + 0.045f * progress;
    float offset = std::sin(pulseTimer_) * amplitude;
    float scale = 1.0f + offset;
    
    // 球体モデルが転がる回転と干渉しないよう、スケール（鼓動の伸縮）のみを更新します
    SetScale({ scale, scale, scale });
}

void EnemyBomb::Explode() {
    state_ = State::Exploded;
    SetScale({ 1.0f, 1.0f, 1.0f });

    // モデルを即座に非表示にする（投擲爆発後や自爆後にモデルがその場に残るバグを完全に解消）
    SetIsVisible(false);

    // --- 爆風による衝突判定の生成と属性切り替え ---
    // 爆発したその瞬間の1フレームだけ判定を広げる
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(LoadExplosionRadiusFromEffectJson());

    // プレイヤー（kPlayer）および他の敵（kEnemy）の双方に爆発が当たるように属性・マスクを無差別化！
    // これにより、自爆でも敵に当たり、投げられた爆弾でも近すぎるとプレイヤーにダメージが入るスリリングな仕様になります。
    SetCollisionAttribute(kPlayerAttack | kEnemyAttack);
    SetCollisionMask(kPlayer | kEnemy | kGround | kMapBlock);

    // --- 新しい美麗な「爆発用エフェクト」の発生 ---
    Vector3 myPos = GetTranslate();
    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            kExplosionEffectPath,
            myPos,
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 1.0f }
        );
    }

    // ボム自身のHPを0にして消滅させる
    if (param_.has_value()) {
        param_.value().hp = 0.0f;
    }
    isDead = true; // デッドフラグを立てて、ゲームシステム側でのクリーンアップを促す
}

void EnemyBomb::SetCarried(bool isCarried) {
    BaseEnemy::SetCarried(isCarried);

    if (isCarried) {
        // プレイヤーに掴まれた瞬間、強制的に点火状態にする
        Ignite(3.0f);
        isThrown_ = false;
    } else {
        // 投げられた（isCarried_ が false になり、プレイヤーから速度を与えられた状態）
        isThrown_ = true;
        throwRecoveryTimer_ = 0.0f;
    }
}

void EnemyBomb::Ignite(float fuseTime) {
    state_ = State::Ignited;
    fuseTimer_ = fuseTime;
    pulseTimer_ = 0.0f;
    flashTimer_ = 0.0f;
    flashState_ = false;
}

void EnemyBomb::ExecuteAbility(Player* player) {
    if (isAbilityExecuted_) return;
    isAbilityExecuted_ = true;

    // 頭の上での自爆能力：プレイヤーを巻き込んで大爆発する
    Vector3 myPos = GetTranslate();
    Vector3 playerPos = player->GetTranslate();
    
    // 吹き飛ばし方向の計算（少し斜め上に飛ばす）
    Vector3 throwBackDir = playerPos - myPos;
    throwBackDir.y = 0.0f;
    float dist = std::sqrt(throwBackDir.x * throwBackDir.x + throwBackDir.z * throwBackDir.z);
    if (dist > 0.001f) {
        throwBackDir.x /= dist;
        throwBackDir.z /= dist;
    } else {
        throwBackDir = {0.0f, 0.0f, -1.0f}; // デフォルト方向
    }
    throwBackDir.y = 0.7f; // 斜め上にノックバック

    // プレイヤーにダメージと吹き飛ばしステートを適用
    player->ChangeState(std::make_unique<PlayerStateDamage>(throwBackDir));
    
    // 爆発を発生させて自身は消滅
    Explode();
}

bool EnemyBomb::OnCollision(Object3d* other) {
    // 投げられた時の即起爆は廃止し、カウントダウンによる時限爆破のみにするため、即爆発処理を削除。
    // 純粋に物理的な壁や床、他の敵との跳ね返りや押し戻しのみを行います。
    return BaseEnemy::OnCollision(other);
}
