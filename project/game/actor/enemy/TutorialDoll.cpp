#define NOMINMAX
#include "TutorialDoll.h"
#include "CollisionConfig.h"
//#include "engine/graphics/particle/GPUParticleManager.h"
#include <algorithm>
#include <cmath>

void TutorialDoll::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 1. 親クラスの初期化
    BaseEnemy::Initialize(common, modelName);
    SetClassName("TutorialDoll");

    // 地面属性を追加して、プレイヤーがすり抜けないようにする
    SetCollisionAttribute(GetCollisionAttribute() | kGround);

    // 2. 初期状態の保存
    // 注: 読み込み直後はJSONのスケールが反映されていない可能性があるため、Updateの初回でキャプチャします
    // baseScale_ = transform_.scale;

    // 3. パラメータの確定
    if (!param_.has_value()) {
        param_.emplace();
    }
    // デフォルト値の設定（JSONで指定がない場合）
    if (param_->maxHp <= 0.0f) param_->maxHp = 100.0f;
    param_->hp = param_->maxHp;
}

void TutorialDoll::Update(float deltaTime) {
    if (deltaTime <= 0.0f) return;

    // 初回UpdateでJSONパース後の座標・スケールを確定
    if (!isInitialized_) {
        basePosition_ = transform_.translate;
        baseScale_ = transform_.scale;
        isInitialized_ = true;
    }

    // -------------------------------------------------------
    // A. 死亡・リスポーン管理
    // -------------------------------------------------------
    if (isDead_) {
        respawnTimer_ -= deltaTime;
        if (respawnTimer_ <= 0.0f) {
            Respawn();
        }

        // 消滅演出（スケールダウン）
        if (deathAnimTimer_ > 0.0f) {
            deathAnimTimer_ -= deltaTime;
            float t = std::max(0.0f, deathAnimTimer_ / 0.5f);
            transform_.scale = baseScale_ * t;
            if (deathAnimTimer_ <= 0.0f) {
                SetIsVisible(false);
                SetCollisionAttribute(0); // 当たり判定を抹消
            }
        }
        UpdateWorldMatrix();
        return;
    }

    // HPが尽きたら死亡状態へ遷移
    if (param_->hp <= 0.0f) {
        isDead_ = true;
        hasBeenDefeatedAtLeastOnce_ = true;
        respawnTimer_ = 5.0f; // 5秒後に復活
        deathAnimTimer_ = 0.5f;

        // 死亡エフェクトの発生
        //GPUParticleManager::GetInstance()->Emit("FirePreset", GetWorldPosition(), Math::MakeIdentity4x4());
        return;
    }

    // 重力や物理で動かないように速度をリセット
    velocity_ = { 0.0f, 0.0f, 0.0f };

    // 親クラスの更新（ダメージタイマーの更新など）
    BaseEnemy::Update(deltaTime);

    // 座標を強制的に初期位置に固定し、行列を確定させる
    transform_.translate = basePosition_;
    UpdateWorldMatrix();
}

bool TutorialDoll::OnCollision(Object3d* other) {
    if (isDead_) return false;

    // プレイヤーの攻撃に当たったか判定
    bool hit = BaseEnemy::OnCollision(other);
    return hit;
}

void TutorialDoll::Respawn() {
    isDead_ = false;
    param_->hp = param_->maxHp;
    transform_.scale = baseScale_;
    SetIsVisible(true);
    SetCollisionAttribute(kEnemy | kGround); // 当たり判定を元に戻す
    SetColor(defaultColor_);       // 色を元に戻す（BaseEnemyの被弾赤色などをリセット）

    // 復活エフェクト（任意）
}
