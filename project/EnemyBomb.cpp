#include "EnemyBomb.h"
#include "game/actor/player/Player.h"
#include "Event.h"          
#include "EventManager.h" 
#include "CollisionConfig.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <MeshEffectManager.h>
#include "BossCore.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include "SceneManager.h"
#include "engine/system/scene/BaseScene.h"


void EnemyBomb::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    state_ = State::Chase;
    igniteTimer_ = 0.0f;
    hitCount_ = 0;
    isBlownAway_ = false;
    shakeTimer_ = 0.0f;
    stunTimer_ = 0.0f;
    lastShakeOffset_ = { 0,0,0 };
    defaultColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    isRolling_ = true;
    rollTimer_ = 0.0f;

    if (GetTransform()) {
        defaultScale_ = GetTransform()->scale;
    }
    SetColor(defaultColor_);
    SetEnemyType("Bomb");

    // コライダーを球体（Sphere）に設定
    Object3d::ColliderConfig colConfig;
    colConfig.type = ColliderType::kSphere;
    colConfig.size = { 1.0f, 1.0f, 1.0f }; // 半径 1.0f
    SetColliderConfig(colConfig);

    // kGroundは追加しない（壁や柱をすり抜けてスムーズに転がり、接地はY座標で制御する）
    SetCollisionMask(kPlayer | kAttributePlayerBullet | kPlayerAttack | kEnemy);


    // JSONファイルから攻撃パラメータを読み込んで攻撃力を設定
    float bombDamage = 30.0f; // デフォルト値
    float bossDamage = 20.0f; // デフォルト値 (ボスに与える跳ね返しダメージ)
    std::string filePath = "Resources/json/enemy/boss_attack_params.json";
    if (std::filesystem::exists(filePath)) {
        std::ifstream ifs(filePath);
        if (ifs.is_open()) {
            json j;
            ifs >> j;
            if (j.contains("damageBomb")) {
                bombDamage = j["damageBomb"];
            }
            if (j.contains("damageBombReflect")) {
                bossDamage = j["damageBombReflect"];
            }
        }
    }
    SetAttackDamage(bombDamage);
    deflectedBossDamage_ = bossDamage;
}

void EnemyBomb::Update(float deltaTime) {
    if (!GetTransform()) {
        BaseEnemy::Update(deltaTime);
        return;
    }

    // 1. 前回のシェイク分を差し引いて、論理的な座標に戻す
    GetTransform()->translate -= lastShakeOffset_;
    lastShakeOffset_ = { 0,0,0 };

    if (!target_ || !target_->GetTransform()) {
        BaseEnemy::Update(deltaTime);
        return;
    }

    // 2. ノックバック・硬直処理 (AI移動を上書き)
    if (stunTimer_ > 0.0f) {
        stunTimer_ -= deltaTime;
        float friction = 0.9f;
        velocity_.x *= friction;
        velocity_.z *= friction;
    }

    // 3. コロコロ転がり挙動（投げられて着地するまで、および着地後しばらく転がる）
    if (isRolling_) {
        bool onGround = (GetTransform()->translate.y <= 1.01f);

        if (onGround) {
            // 接地：Y座標を 1.0f (球体の半径) に強制吸着し、落下速度をゼロにする
            GetTransform()->translate.y = 1.0f;
            if (velocity_.y < 0.0f) {
                velocity_.y = 0.0f;
            }

            // 滑りながら減衰し、速度に応じた回転をする
            float groundFriction = 0.96f; // 摩擦
            velocity_.x *= groundFriction;
            velocity_.z *= groundFriction;

            float speed = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
            float rollAngle = speed * 4.0f * deltaTime; // 転がる回転速度
            GetTransform()->rotate.x += rollAngle;

            rollTimer_ += deltaTime;
            if (rollTimer_ >= 1.2f || speed < 0.2f) {
                isRolling_ = false;
                velocity_.x = 0.0f;
                velocity_.z = 0.0f;
            }
        }
        else {
            // 空中：ランダムまたは一定方向に回転（タンブリング）
            GetTransform()->rotate.x += 8.0f * deltaTime;
            GetTransform()->rotate.y += 3.0f * deltaTime;
        }
    }

    Vector3 myPos = GetTransform()->translate;
    Vector3 targetPos = target_->GetTransform()->translate;

    Vector3 dir = { targetPos.x - myPos.x, targetPos.y - myPos.y, targetPos.z - myPos.z };
    float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

    switch (state_) {
    case State::Chase:
        if (!isBlownAway_ && stunTimer_ <= 0.0f && !isRolling_) {
            // 追従移動は完全に排除！転がり終わったら即座にその場で点火カウントダウンを開始する
            state_ = State::Ignited;
            igniteTimer_ = 0.0f;
        }
        break;

    case State::Ignited:
        // 吹き飛ばされている場合はカウントダウンさせない（OnCollision側でも解除するが一応ガード）
        if (!isBlownAway_) {
            igniteTimer_ += deltaTime;

            // 【1. カラーの点滅処理】
            // 爆発間際 (全体の70%の時間が経過) は点滅を速くする
            {
                float blinkInterval = (igniteTimer_ > igniteDuration_ * 0.7f) ? 0.08f : 0.2f;
                if (std::fmod(igniteTimer_, blinkInterval) < blinkInterval / 2.0f) {
                    SetColor({ 1.0f, 0.0f, 0.0f, 1.0f }); // 赤色
                }
                else {
                    SetColor(defaultColor_);              // 元の色
                }
            }

            // 【2. スケールの増減 (鼓動)】
            // sin波を使って、元のスケールから最大1.3倍まで「ドクンドクン」と大きくする
            {
                float pulse = 0.3f * std::abs(std::sin(igniteTimer_ * 15.0f));
                Vector3 newScale = {
                    defaultScale_.x + pulse,
                    defaultScale_.y + pulse,
                    defaultScale_.z + pulse
                };
                SetScale(newScale);

                // 接地中（吹き飛ばされていない）ならば、拡大したスケールに応じて球体の底面が地面（Y=0.0f）に接し続けるようにY座標を持ち上げる
                GetTransform()->translate.y = newScale.y * 1.0f;
            }

            // カウントダウン終了で爆発状態へ
            if (igniteTimer_ >= igniteDuration_) {
                state_ = State::Exploded;

                // 爆発の瞬間に見た目をリセットしておく
                SetColor(defaultColor_);
                SetScale(defaultScale_);
            }
        }
        break;

    case State::Exploded:
        if (!hasExploded_) {
            hasExploded_ = true;

            // 1. 最初はごく小さな当たり判定(0.1f)からスタート
            SetCollisionRadius(0.1f);
            SetCollisionAttribute(kEnemyAttack);
            SetCollisionMask(kPlayer); // 床との判定が切れる

            // ★床をすり抜けて落下しないように、速度と重力をゼロにする！
            velocity_ = { 0.0f, 0.0f, 0.0f };
            if (param_.has_value()) {
                param_->gravity = 0.0f;
            }

            SetIsVisible(false);

            // エフェクト発生 (ボム自身の位置で正確に爆発させるために SpawnEffectAt を使用。スケールを2倍にして迫力を出し、当たり判定と一致させます)
            UpdateWorldMatrix();
            MeshEffectManager::GetInstance()->SpawnEffectAt("Resources/json/effect/effect_bakuhatu1.json", GetWorldPosition(), { 0,0,0 }, { 2.0f, 2.0f, 2.0f }, GetAttackDamage());
        }

        explosionTimer_ += deltaTime;

        // ★ 当たり判定を 0.1f から 4.0f へ徐々に広げる
        float t = std::min(explosionTimer_ / explosionDuration_, 1.0f);
        float currentRadius = Math::Lerp(0.1f, explosionRadius_, t);
        SetCollisionRadius(currentRadius);

        // エフェクトの終了と同時に完全に消滅
        if (explosionTimer_ >= explosionDuration_) {
            isDead = true;
            SetCollisionAttribute(0);    // 攻撃属性を消す
            SetCollisionMask(0);         // 誰とも当たらないようにする
            SetCollisionRadius(0.0f);    // サイズをゼロにする
        }
        break;
    }

    BaseEnemy::Update(deltaTime);

    // 点火待機中（吹き飛ばされていない）ならば、重力による毎フレームの沈み込みをシャットアウトし、
    // 拡大したスケールに応じて球体の底面が地面（Y=0.0f）に完全に接地し続けるように補正
    if (!isBlownAway_ && state_ == State::Ignited) {
        float currentRadius = GetScale().y * 1.0f;
        GetTransform()->translate.y = currentRadius;
        velocity_.y = 0.0f; // 重力落下速度を完全に相殺
    }

    // 3. シェイク演出（描画用の座標オフセットを計算・適用）
    if (shakeTimer_ > 0.0f) {
        shakeTimer_ -= deltaTime;
        if (shakeTimer_ < 0.0f) shakeTimer_ = 0.0f;

        // 残り時間に比例して振幅を小さくする
        float amplitude = 0.5f * (shakeTimer_ / 0.5f);
        lastShakeOffset_ = {
            ((float)rand() / RAND_MAX * 2.0f - 1.0f) * amplitude,
            ((float)rand() / RAND_MAX * 2.0f - 1.0f) * amplitude,
            ((float)rand() / RAND_MAX * 2.0f - 1.0f) * amplitude
        };
        // 座標にオフセットを乗せる（次のUpdateの冒頭で差し引かれる）
        GetTransform()->translate += lastShakeOffset_;
    }

    // 落下死判定（Y座標が-5.0f以下になったら消滅し、実体も即座に消す）
    if (GetTransform()->translate.y <= -5.0f) {
        SetIsVisible(false);
        SetCollisionAttribute(0);
        isDead = true;
    }

}
std::unique_ptr<Object3d> EnemyBomb::Clone() const {
    auto clone = std::make_unique<EnemyBomb>();
    clone->Initialize(common_, this->GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(this->target_);
    return clone;
}

bool EnemyBomb::OnCollision(Object3d* other) {
    // 既に爆発している場合は衝突処理を行わない
    if (state_ == State::Exploded) {
        return true;
    }

    // 吹き飛ばし中にボス本体に当たったら、ダメージを与えて爆発する！
    if (isBlownAway_) {
        BossCore* boss = dynamic_cast<BossCore*>(other);
        if (boss) {
            boss->TakeBodyDamage(deflectedBossDamage_); // 跳ね返し用のボスダメージを適用
            state_ = State::Exploded;     // 爆発状態へ移行してエフェクト＆攻撃判定を出す
            return true;
        }
    }

    uint32_t attribute = other->GetCollisionAttribute();
    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) return false;

    // 0. ボム兵同士の衝突：上に重ならないように水平方向にのみ押し出す
    if ((attribute & kEnemy) && !(attribute & kGround)) {
        if (info.normal.y < -0.1f) return true;

        Vector3 pushDir = info.normal;
        pushDir.y = 0.0f;
        if (Math::Length(pushDir) < 0.01f) pushDir = { 1.0f, 0.0f, 0.0f };
        pushDir = Math::Normalize(pushDir);
        GetTransform()->translate += pushDir * info.penetration;
        return true;
    }

    // 1. 吹き飛び中に他の敵属性（装甲など）に当たった場合も爆発してボスにダメージ
    if (isBlownAway_ && (attribute & kEnemy)) {
        BossCore* boss = dynamic_cast<BossCore*>(other);
        if (boss) {
            boss->TakeBodyDamage(deflectedBossDamage_);
            state_ = State::Exploded;
            return true;
        }
    }

    // 2. プレイヤーの攻撃を受けた場合（スライム同様に跳ね返せる）
    if (attribute & kPlayerAttack) {
        if (damageCooldownTimer_ <= 0.0f) {
            if (EffectObject3d* effect = dynamic_cast<EffectObject3d*>(other)) {
                effect->AddHitObject(this);
            }
            hitCount_++;
            shakeTimer_ = 0.5f;
            damageCooldownTimer_ = 0.5f;

            // 攻撃の飛んできた方向を計算
            Vector3 hitDir = { 0, 0, 1 };
            Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
            if (camera) {
                hitDir = camera->GetTargetPoint() - camera->GetEye();
                hitDir.y = 0.0f;
                if (Math::Length(hitDir) > 0.001f) hitDir = Math::Normalize(hitDir);
            }

            if (hitCount_ >= 3) {
                // 吹き飛ばし開始
                isBlownAway_ = true;
                state_ = State::Chase; // 点火カウントダウンを解除して飛ぶ
                igniteTimer_ = 0.0f;

                Vector3 dir = hitDir;

                // ボスが近くにいればエイムアシスト（ボスに向かってホーミング）
                BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();
                if (scene) {
                    auto& objects = scene->GetObjects();
                    for (auto& obj : objects) {
                        BossCore* boss = dynamic_cast<BossCore*>(obj.get());
                        if (boss) {
                            Vector3 toBoss = boss->GetTranslate() - GetTranslate();
                            toBoss.y = 0.0f;
                            if (Math::Length(toBoss) > 0.1f) {
                                Vector3 normToBoss = Math::Normalize(toBoss);
                                float dot = Math::Dot(dir, normToBoss);
                                // 約36度以内ならボスの方へ補正
                                if (dot > 0.8f) {
                                    dir = normToBoss;
                                    break;
                                }
                            }
                        }
                    }
                }

                float blowSpeed = 45.0f;
                velocity_ = { dir.x * blowSpeed, 25.0f, dir.z * blowSpeed };

                // プレイヤー攻撃属性を付与してボスにダメージを通せるようにし、
                // 衝突対象を敵に絞って他の物をすり抜けるようにする
                SetCollisionAttribute(GetCollisionAttribute() | kPlayerAttack);
                SetCollisionMask(kEnemy);
            }
            else {
                // 1〜2発目は軽いノックバック
                float kbSpeed = 15.0f;
                velocity_.x = hitDir.x * kbSpeed;
                velocity_.z = hitDir.z * kbSpeed;
                stunTimer_ = 0.2f;
            }

            UpdateColorByHitCount();
        }
        return true;
    }

    // 3. 地面や壁なら物理押し戻し
    if (attribute & kAllSolid) {
        if (isBlownAway_ && (attribute & kEnemy) && !(attribute & kPlayer)) {
            // 雑魚敵同士ならスルー
        }
        else {
            ApplyPhysicsCollision(info, attribute);

            // 吹き飛び中に地面に激突したらその場で起爆！
            if (isBlownAway_ && isGrounded_ && velocity_.y <= 0.0f) {
                state_ = State::Exploded;
            }
        }
    }

    return true;
}

void EnemyBomb::UpdateColorByHitCount() {
    if (hitCount_ < 0) hitCount_ = 0;

    switch (hitCount_) {
    case 0:
        defaultColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // 白
        break;
    case 1:
        defaultColor_ = { 1.0f, 1.0f, 0.0f, 1.0f }; // 黄色
        break;
    case 2:
        defaultColor_ = { 1.0f, 0.5f, 0.0f, 1.0f }; // 橙色
        break;
    case 3:
    default:
        defaultColor_ = { 1.0f, 0.0f, 0.0f, 1.0f }; // 赤色
        break;
    }
    SetColor(defaultColor_);
}
