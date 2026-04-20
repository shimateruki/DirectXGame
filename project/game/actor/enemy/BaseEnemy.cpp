#include "BaseEnemy.h"
#include "CollisionConfig.h" // kEnemyなどの定義を使うため
#include "Event.h"           //  DamageEventを使うため
#include "EventManager.h"    //  イベントを発行(Dispatch)するため
#include "Player.h"          //  プレイヤーの状態を見るため

void BaseEnemy::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 1. 親クラス(Character)の初期化
    Character::Initialize(common);

    // 2. モデルをセット
    SetModel(modelName);

    // 3. 当たり判定の設定
    SetCollisionAttribute(kEnemy);      
    SetCollisionMask(kPlayer | kGround | kAttributePlayerBullet | kPlayerAttack);
    SetClassName("Enemy");
    defaultColor_ = GetColor();
}

void BaseEnemy::Update(float deltaTime) {
    // 重力処理などは親クラス(Character)に任せる
    Character::Update(deltaTime);
    // ① 連続ヒット防止タイマーを減らす
    if (damageCooldownTimer_ > 0.0f) {
        damageCooldownTimer_ -= deltaTime;
    }

    // ② 赤色演出タイマーを減らす
    if (colorResetTimer_ > 0.0f) {
        colorResetTimer_ -= deltaTime;
        // タイマーが0以下になった瞬間に元の色に戻す！
        if (colorResetTimer_ <= 0.0f) {
            SetColor(defaultColor_);


        }
    }
}

bool BaseEnemy::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();
    CollisionInfo info = CheckCollision(other);

    if (!info.isColliding) {
        return false;
    }

    // ========================================================
    // プレイヤーの攻撃（剣など）に当たった時の処理
    // ========================================================
    if (attribute & kPlayerAttack) {

        // ★ クールダウン中（無敵時間中）ならダメージ処理を無視して抜ける！
        if (damageCooldownTimer_ > 0.0f) {
            return true;
        }

        // ダメージイベントの発行
        DamageEvent dmgEvent;
        dmgEvent.target = this;
        dmgEvent.attacker = other;
        dmgEvent.damageAmount = 10.0f;
        EventManager::GetInstance()->Dispatch(dmgEvent);

   
        damageCooldownTimer_ = 0.5f; // 0.5秒間は次の攻撃を食らわない（剣の持続ヒット防止）
        colorResetTimer_ = 0.15f;    // 0.15秒間だけ赤くする

        SetColor({ 1.0f, 0.0f, 0.0f, 1.0f }); // 真っ赤にする

        // ※もし敵が複数のパーツ(子オブジェクト)でできている場合は以下も有効化してください
        // for (Object3d* child : GetChildren()) { child->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f }); }

        return true;
    }

    // 地面や壁（kAllSolid）なら、物理的な押し戻しを実行
    if (attribute & kAllSolid) {
        ApplyPhysicsCollision(info, attribute);
    }

    return true;
}