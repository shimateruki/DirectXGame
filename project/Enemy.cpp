#include "Enemy.h"
#include "CollisionConfig.h" // kEnemyなどの定義用

void Enemy::Initialize(Object3dCommon* common, const Vector3& position) {
    // 親クラスの初期化
    Character::Initialize(common);

    // モデルと座標設定
    SetModel("bunny"); // ※ウサギのモデル名に合わせてください
    SetTranslate(position);
    SetName("Enemy");

    // 衝突判定の設定
    SetCollisionAttribute(kEnemy);
    SetCollisionMask(~kEnemy); // 敵同士以外と当たる
    SetColliderType(ColliderType::kAABB);
    SetCollisionSize({ 1.0f, 1.0f, 1.0f });
    SetGravity(0.0f);
    // パラメータ初期化
    hp_ = 3;
    isDead_ = false;
    invincibleTimer_ = 0.0f;
}

void Enemy::Update(float deltaTime) {
    if (isDead_) return;
    if (invincibleTimer_ > 0.0f) {
        invincibleTimer_ -= deltaTime;
        if (invincibleTimer_ < 0.0f) {
            invincibleTimer_ = 0.0f;
        }
    }
    // 親クラス更新（重力などが適用される）
    Character::Update(deltaTime);
}


void Enemy::Draw() {
    if (isDead_) return;

    // 無敵時間中はチカチカさせる
    if (invincibleTimer_ > 0.0f) {
        // 0.1秒周期で点滅 
        if (std::fmod(invincibleTimer_, 0.1f) > 0.05f) {
            return; // 今回は描画スキップ
        }
    }

    // 通常描画
    Character::Draw();
}

void Enemy::OnHit(int damage) {
    if (isDead_) return;
    if (invincibleTimer_ > 0.0f) {
        return;
    }
    invincibleTimer_ = kInvincibleTime_;
    hp_ -= damage;
    if (hp_ <= 0) {
        hp_ = 0;
        isDead_ = true;
    }
}