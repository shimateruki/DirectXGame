#include "BaseEnemy.h"
#include "CollisionConfig.h" // kEnemyなどの定義を使うため

void BaseEnemy::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 1. 親クラス(Character)の初期化
    Character::Initialize(common);

    // 2. モデルをセット
    SetModel(modelName);

    // 3. 当たり判定の設定
    SetCollisionAttribute(kEnemy);       // 自分は「敵」グループ
    SetCollisionMask(kPlayer | kGround | kAttributePlayerBullet); 
    SetClassName("Enemy");
}

void BaseEnemy::Update(float deltaTime) {
    // 重力処理などは親クラス(Character)に任せる
    Character::Update(deltaTime);
}

bool BaseEnemy::OnCollision(Object3d* other) {
    // 1. 相手の属性（床なのか、弾なのか）を取得
    uint32_t attribute = other->GetCollisionAttribute();

    // 2. 詳細な衝突判定（めり込み量などを計算）
    CollisionInfo info = CheckCollision(other);

    // 実際には当たっていなければ終了
    if (!info.isColliding) {
        return false;
    }

    // 3. 地面や壁（kAllSolid）なら、物理的な押し戻しを実行！
    if (attribute & kAllSolid) {
        ApplyPhysicsCollision(info, attribute);
    }


    return true;
}