#include "PlayerClone.h"
#include "engine/graphics/3d/model/ModelManager.h"

void PlayerClone::Initialize(Object3dCommon* common, const Vector3& position, const Vector3& velocity) {
    Character::Initialize(common);
    SetClassName("PlayerClone"); // カメラフェード等の例外処理用
    
    // 重力などのパラメータを設定（これをしないとCharacter::Updateで重力が無視される）
    SetGravity(50.0f);
    SetMaxFallSpeed(60.0f);

    // プレイヤーと同じモデルを設定
    SetModel("Characters/slime"); 
    
    transform_.translate = position;
    velocity_ = velocity;
    
    // 色を少し青みがかった状態にする（薄くしない）
    SetColor({ 0.4f, 0.7f, 1.0f, 1.0f });
    
    // プレイヤーと同じサイズ
    SetScale({ 2.0f, 2.0f, 2.0f });

    // 衝突属性の設定
    // プレイヤー(0x01)とは当たらず、地面(kAllSolid)とは当たるように設定
    SetCollisionAttribute(0x00000001); 
    SetCollisionMask(0xFFFFFFFE); 

    // 当たり判定の形状やサイズは、生成元（Player）からコピーするためここでは設定しない
    
    // 生成直後のTransformを確定させる
    UpdateLocalMatrix();
    UpdateWorldMatrix();
}

void PlayerClone::Update(float deltaTime) {
    if (lifetimeTimer_ <= 0.0f) return;
    
    // 寿命を減らす
    lifetimeTimer_ -= deltaTime;

    // 地面に着いたら水平移動を止める
    if (isGrounded_) {
        velocity_.x = 0.0f;
        velocity_.z = 0.0f;
    }

    // 重力や移動の計算
    Character::Update(deltaTime);
}
