#include "EnemySlime.h"
#include "engine/utility/math/Math.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include "engine/system/scene/BaseScene.h"
#include "BossCore.h"
#include <cmath>
#include <algorithm>

static Math math;

void EnemySlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    
    // 当たり判定の設定 (OBB)
    SetColliderType(ColliderType::kOBB);
    SetCollisionSize({ 1.0f, 1.0f, 1.0f });
    // 属性に kGround を加えることで、他の敵やプレイヤーから「地形（進入不可）」として扱われるようになる
    SetCollisionAttribute(kEnemy | kGround);
    SetCollisionMask(kPlayer | kGround | kAttributePlayerBullet | kPlayerAttack | kEnemy);

    // 初期設定
    hitCount_ = 0;
    isBlownAway_ = false;
    shakeTimer_ = 0.0f;
    bossTarget_ = nullptr;
    lastShakeOffset_ = { 0,0,0 };

    // 初期色は緑
    defaultColor_ = { 0.0f, 1.0f, 0.0f, 1.0f };
    SetColor(defaultColor_);
    SetEnemyType("Slime");
}

void EnemySlime::Update(float deltaTime) {
    // 1. 前回のシェイク分を差し引いて、論理的な座標に戻す
    // これにより、OnCollisionでの押し戻しなどが正しく保持される
    transform_.translate -= lastShakeOffset_;
    lastShakeOffset_ = { 0,0,0 };

    // 2. 吹き飛び中のホーミング処理
    if (isBlownAway_ && bossTarget_) {
        Vector3 myPos = GetTranslate();
        Vector3 targetPos = bossTarget_->GetWorldPosition();
        Vector3 toTarget = targetPos - myPos;
        float dist = math.Length(toTarget);

        if (dist < 50.0f) { // 射程内ならホーミング
            Vector3 dir = math.Normalize(toTarget);
            float currentSpeed = math.Length(velocity_);
            
            // 速度ベクトルを徐々にターゲット方向へ向ける（ホーミング強度 5.0）
            Vector3 targetVelocity = dir * currentSpeed;
            velocity_ = math.Lerp(velocity_, targetVelocity, 5.0f * deltaTime);
        }
    }

    // 4. 通常の移動AI（吹き飛び中でない場合）
    if (!isBlownAway_ && target_) {
        Vector3 myPos = transform_.translate;
        Vector3 targetPos = target_->GetWorldPosition();
        Vector3 toTarget = targetPos - myPos;
        toTarget.y = 0.0f;

        float length = math.Length(toTarget);

        if (length < 20.0f && length > 1.0f) {
            Vector3 dir = math.Normalize(toTarget);
            float speed = 3.0f;

            velocity_.x = dir.x * speed;
            velocity_.z = dir.z * speed;

            transform_.rotate.y = std::atan2(dir.x, dir.z);
        } else {
            velocity_.x = 0.0f;
            velocity_.z = 0.0f;
        }
    }

    // 5. キャラクターとしての物理更新（論理座標の確定）
    BaseEnemy::Update(deltaTime);

    // 6. シェイク演出（描画用の座標オフセットを計算・適用）
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
        transform_.translate += lastShakeOffset_;
    }
}

bool EnemySlime::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();
    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) return false;

    // 1. 吹き飛び中にボス（敵属性）に当たったらダメージ
    if (isBlownAway_ && (attribute & kEnemy)) {
        BossCore* boss = dynamic_cast<BossCore*>(other);
        if (boss) {
            boss->TakeBodyDamage(20.0f); // 20ダメージ
            isDead = true;
            return true;
        }
    }

    // 2. プレイヤーの攻撃を受けた場合（HPを減らさずヒットカウントのみ管理）
    if (attribute & kPlayerAttack) {
        // クールダウン中でなければヒット
        if (damageCooldownTimer_ <= 0.0f) {
            hitCount_++;
            shakeTimer_ = 0.5f; // シェイク開始
            damageCooldownTimer_ = 0.5f; // 連続ヒット防止
            
            if (hitCount_ >= 3) {
                // 吹き飛ばし開始
                isBlownAway_ = true;
                
                Vector3 diff = GetTranslate() - other->GetWorldPosition();
                diff.y = 0.0f;
                if (math.Length(diff) > 0.001f) {
                    Vector3 dir = math.Normalize(diff);
                    float blowSpeed = 40.0f;
                    velocity_ = { dir.x * blowSpeed, 15.0f, dir.z * blowSpeed };
                }

                // ホーミング対象（ボス）を検索
                BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();
                if (scene) {
                    for (auto& obj : scene->GetObjects()) {
                        BossCore* b = dynamic_cast<BossCore*>(obj.get());
                        if (b) {
                            float dist = math.Length(b->GetWorldPosition() - GetTranslate());
                            if (dist < 40.0f) { // 40m以内ならホーミング
                                bossTarget_ = b;
                            }
                            break;
                        }
                    }
                }
            }
            
            UpdateColorByHitCount();
        }
        return true;
    }

    // 3. 地面や壁（kAllSolid / kGround属性を持つ敵）なら、物理的な押し出しを実行
    if (attribute & kAllSolid) {
        // 吹き飛び状態のときは、自分以外の敵(kEnemy)はすり抜けてボスに当てたいので無視する
        // ただし、ボス自身もkGroundを持っているので、ここでは「kEnemy属性のみ」を持つ相手（＝雑魚敵）を弾く
        if (isBlownAway_ && (attribute & kEnemy) && !(attribute & kPlayer)) {
             // 雑魚敵同士ならスルー
        } else {
            ApplyPhysicsCollision(info, attribute);

            // 吹き飛び中に地面（下方向）に激突したら消滅
            if (isBlownAway_ && isGrounded_ && velocity_.y <= 0.0f) {
                isDead = true;
            }
        }
    }

    return true;
}

void EnemySlime::UpdateColorByHitCount() {
    switch (hitCount_) {
    case 0:
        defaultColor_ = { 0.0f, 1.0f, 0.0f, 1.0f }; // 緑
        break;
    case 1:
        defaultColor_ = { 1.0f, 1.0f, 0.0f, 1.0f }; // 黄
        break;
    case 2:
        defaultColor_ = { 1.0f, 0.5f, 0.0f, 1.0f }; // 橙
        break;
    case 3:
    default:
        defaultColor_ = { 1.0f, 0.0f, 0.0f, 1.0f }; // 赤
        break;
    }
    // BaseEnemy::Update で defaultColor_ に戻されるので、ここでは即座にセットしなくて良いが、
    // 即時反映のために呼んでおく
    SetColor(defaultColor_);
}

std::unique_ptr<Object3d> EnemySlime::Clone() const {
    auto newSlime = std::make_unique<EnemySlime>();
    newSlime->Initialize(common_, this->GetModelName());
    newSlime->CopyFrom(this);
    newSlime->SetTarget(this->target_);
    return newSlime;
}