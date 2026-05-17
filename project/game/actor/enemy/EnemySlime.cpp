#include "EnemySlime.h"
#include "engine/utility/math/Math.h"
#include "CollisionConfig.h"
#include "EffectObject3d.h"
#include "SceneManager.h"
#include "engine/system/scene/BaseScene.h"
#include "BossCore.h"
#include "game/actor/player/Player.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <algorithm>

static Math math;

void EnemySlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 1. メンバ変数の初期化（親の初期化を呼ぶ前にリセット）
    hitCount_ = 0;
    isBlownAway_ = false;
    shakeTimer_ = 0.0f;
    stunTimer_ = 0.0f;
    jumpTimer_ = 0.0f;
    lastShakeOffset_ = { 0,0,0 };
    defaultColor_ = { 0.0f, 1.0f, 0.0f, 1.0f }; // デフォルトは緑
    
    // スケールアニメーション初期化
    transform_.scale = { 1,1,1 };
    targetScale_ = { 1,1,1 };
    prevIsGrounded_ = true;

    // 2. 親クラスの初期化
    BaseEnemy::Initialize(common, modelName);
    
    // 3. スライム固有の設定
    SetColliderType(ColliderType::kOBB);
    SetCollisionSize({ 1.0f, 1.0f, 1.0f });
    SetCollisionAttribute(kEnemy | kEnemyAttack); // 接触ダメージを有効にする
    SetCollisionMask(kPlayer | kGround | kAttributePlayerBullet | kPlayerAttack | kEnemy);

    defaultColor_ = { 0.0f, 1.0f, 0.0f, 1.0f };
    SetColor(defaultColor_);
    SetEnemyType("Slime");

    // JSONファイルから攻撃パラメータを読み込んで攻撃力を設定
    float slimeDamage = 8.0f; // デフォルト値
    std::string filePath = "Resources/json/enemy/boss_attack_params.json";
    if (std::filesystem::exists(filePath)) {
        std::ifstream ifs(filePath);
        if (ifs.is_open()) {
            json j;
            ifs >> j;
            if (j.contains("damageSlime")) {
                slimeDamage = j["damageSlime"];
            }
        }
    }
    SetAttackDamage(slimeDamage);
}

void EnemySlime::Update(float deltaTime) {
    // 1. 前回のシェイク分を差し引いて、論理的な座標に戻す
    // これにより、OnCollisionでの押し戻しなどが正しく保持される
    transform_.translate -= lastShakeOffset_;
    lastShakeOffset_ = { 0,0,0 };

    // 2. 吹き飛び中（ホーミングなし）
    // ホーミング機能は一時削除されました。

    // 4. ノックバック・硬直処理 (AI移動を上書き)
    if (stunTimer_ > 0.0f) {
        stunTimer_ -= deltaTime;
        // 横方向の摩擦を適用
        float friction = 0.9f;
        velocity_.x *= friction;
        velocity_.z *= friction;
    }
    // 5. 通常の移動AI（吹き飛び中・硬直中でない場合）
    else if (!isBlownAway_ && IsTargetValid()) {
        // 常にターゲットの方を向く（線形補間によるスムーズな回転）
        Vector3 myPos = transform_.translate;
        Vector3 targetPos = target_->GetWorldPosition();
        Vector3 toTarget = targetPos - myPos;
        toTarget.y = 0.0f;

        if (math.Length(toTarget) > 0.1f) {
            Vector3 dir = math.Normalize(toTarget);
            float targetAngle = std::atan2(dir.x, dir.z);

            // 最短角度での線形補間（0〜360度の跨ぎを考慮）
            float diff = targetAngle - transform_.rotate.y;
            while (diff > 3.14159265f) diff -= 6.2831853f;
            while (diff < -3.14159265f) diff += 6.2831853f;

            float rotateLerpRate = 8.0f; // 回転速度
            transform_.rotate.y += diff * rotateLerpRate * deltaTime;
            transform_.isQuaternionMaster = false;
        }

        if (isGrounded_) {
            // 接地中は停止してジャンプを待つ
            velocity_.x = 0.0f;
            velocity_.z = 0.0f;
            jumpTimer_ += deltaTime;

            if (jumpTimer_ >= 0.5f) {
                // ターゲットへの方向を再計算（最新の向きで飛ぶ）
                Vector3 dir = { std::sin(transform_.rotate.y), 0, std::cos(transform_.rotate.y) };
                float horizontalSpeed = 5.0f;
                float jumpPower = 12.0f; // プレイヤーより少し低め

                velocity_.x = dir.x * horizontalSpeed;
                velocity_.z = dir.z * horizontalSpeed;
                velocity_.y = jumpPower;

                jumpTimer_ = 0.0f;
            }
        }
        else {
            // 空中では水平速度を維持（AIによる更新は行わない）
        }
    }

    // 6. キャラクターとしての物理更新（論理座標の確定）
    // ※内部の Character::Update で isGrounded_ が一旦リセットされるため、アニメーション用に現在の状態を保持する
    bool currentGrounded = isGrounded_;
    BaseEnemy::Update(deltaTime);

    // 接地判定の安定化（ぶるぶる防止）
    // 「物理的に接地している」または「ジャンプ後の待機時間中（0.5秒間）」なら接地中とみなす
    // ただし、ジャンプして上方向に勢いよく動いている時は除く
    bool isStableGrounded = (currentGrounded || (jumpTimer_ > 0.0f && jumpTimer_ < 0.5f));
    if (velocity_.y > 0.1f) {
        isStableGrounded = false;
    }

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

    // 落下死判定（Y座標が-5.0f以下になったら消滅し、実体も即座に消す）
    if (transform_.translate.y <= -5.0f) {
        SetIsVisible(false);
        SetCollisionAttribute(0);
        isDead = true;
    }

    // =========================================================
    // 7. もちっとしたアニメーション（Squash & Stretch）
    // =========================================================
    if (!isStableGrounded) {
        // 空中：縦長に伸びる（Stretch）
        targetScale_ = { 0.8f, 1.3f, 0.8f };
    }
    else {
        // 着地した瞬間：横に潰れる（Squash）
        if (!prevIsGrounded_) {
            transform_.scale = { 1.4f, 0.5f, 1.4f }; // 瞬時に潰す
        }
        // 接地中：徐々に元に戻る
        targetScale_ = { 1.0f, 1.0f, 1.0f };
    }

    // スケールを目標に近づける（線形補間）
    float lerpRate = 10.0f; // 戻る速さ
    transform_.scale.x = math.Lerp(transform_.scale.x, targetScale_.x, lerpRate * deltaTime);
    transform_.scale.y = math.Lerp(transform_.scale.y, targetScale_.y, lerpRate * deltaTime);
    transform_.scale.z = math.Lerp(transform_.scale.z, targetScale_.z, lerpRate * deltaTime);

    prevIsGrounded_ = isStableGrounded;
}

bool EnemySlime::OnCollision(Object3d* other) {
    // 吹き飛ばし中にボス本体に当たったら、何よりも優先してダメージを与えて消滅する
    if (isBlownAway_) {
        BossCore* boss = dynamic_cast<BossCore*>(other);
        if (boss) {
            boss->TakeBodyDamage(10.0f);
            isDead = true;
            return true;
        }
    }

    uint32_t attribute = other->GetCollisionAttribute();
    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) return false;

    // 0. スライム同士の衝突：上に重ならないように水平方向にのみ押し出す
    if ((attribute & kEnemy) && !(attribute & kGround)) {
        // 自分が「下」にいる場合（相手に踏まれている状態：法線が下向き）は移動しない
        if (info.normal.y < -0.1f) {
            return true;
        }

        // 自分が「上」または「横」にいる場合は、重なりを解消するために横に逃げる
        Vector3 pushDir = info.normal;
        pushDir.y = 0.0f; // 垂直方向の押し出しを無効化
        if (math.Length(pushDir) < 0.01f) {
            // 真上から当たった場合は、適当な横方向に逃がす
            pushDir = { 1.0f, 0.0f, 0.0f };
        }
        pushDir = math.Normalize(pushDir);
        transform_.translate += pushDir * info.penetration;
        return true;
    }

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
            // ★ エフェクトからの被弾を確定させてヒットリストに記録
            if (EffectObject3d* effect = dynamic_cast<EffectObject3d*>(other)) {
                effect->AddHitObject(this);
            }
            hitCount_++;
            shakeTimer_ = 0.5f; // シェイク開始
            damageCooldownTimer_ = 0.5f; // 連続ヒット防止
            
            // 攻撃の飛んできた方向を計算（カメラの向きをベースにする）
            Vector3 hitDir = { 0, 0, 1 };
            Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
            if (camera) {
                hitDir = camera->GetTargetPoint() - camera->GetEye();
                hitDir.y = 0.0f;
                if (math.Length(hitDir) > 0.001f) hitDir = math.Normalize(hitDir);
            }

            if (hitCount_ >= 3) {
                // 吹き飛ばし開始
                isBlownAway_ = true;
                
                // 視点の向いている方向に吹っ飛ばす
                Vector3 dir = hitDir;
                
                // ボスが近くにいればホーミング（エイムアシスト）
                BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();
                if (scene) {
                    auto& objects = scene->GetObjects();
                    for (auto& obj : objects) {
                        BossCore* boss = dynamic_cast<BossCore*>(obj.get());
                        if (boss) {
                            Vector3 toBoss = boss->GetTranslate() - GetTranslate();
                            toBoss.y = 0.0f;
                            if (math.Length(toBoss) > 0.1f) {
                                Vector3 normToBoss = math.Normalize(toBoss);
                                float dot = math.Dot(dir, normToBoss);
                                // 約36度以内ならボスの方へ補正
                                if (dot > 0.8f) {
                                    dir = normToBoss;
                                    break;
                                }
                            }
                        }
                    }
                }
                
                float blowSpeed = 40.0f;
                // 高さを 15.0f -> 25.0f に引き上げ
                velocity_ = { dir.x * blowSpeed, 25.0f, dir.z * blowSpeed };

                // --- 貫通設定 ---
                // 当たり判定を元のサイズに維持（ボス側の属性修正によりこれでも当たるはず）
                SetCollisionSize({ 1.0f, 1.0f, 1.0f });

                // プレイヤーの攻撃属性を付与してボスにダメージを与えられるようにし、
                // 衝突対象を「敵（BossCore本体含む）」のみに絞ることで地形や装甲を貫通させる
                SetCollisionAttribute(GetCollisionAttribute() | kPlayerAttack);
                SetCollisionMask(kEnemy);
            }
            else {
                // 1~2発目は軽いノックバック
                float kbSpeed = 15.0f;
                velocity_.x = hitDir.x * kbSpeed;
                velocity_.z = hitDir.z * kbSpeed;
                stunTimer_ = 0.2f; // 0.2秒間は硬直（AI移動を停止）
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
    // 負の数や異常値へのガード
    if (hitCount_ < 0) hitCount_ = 0;

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
    SetColor(defaultColor_);
}

std::unique_ptr<Object3d> EnemySlime::Clone() const {
    auto newSlime = std::make_unique<EnemySlime>();
    newSlime->Initialize(common_, this->GetModelName());
    newSlime->CopyFrom(this);
    newSlime->SetTarget(this->target_);
    return newSlime;
}