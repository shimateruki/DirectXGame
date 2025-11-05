#define NOMINMAX
#include "Character.h"
#include "engine/3d/CollisionConfig.h"
#include "engine/base/Math.h"
#include <algorithm> // std::min, std::max
#include <cmath>     // std::abs

// ★ Math のインスタンスを作成
static Math math;


void Character::Update() {
    isGrounded_ = false;

    velocity_.y -= gravity_; 

    if (velocity_.y < -maxFallSpeed_) { 
        velocity_.y = -maxFallSpeed_; 
    }

    transform_.translate += velocity_;
}
bool Character::OnCollision(Object3d* other) {

    if (!(other->GetCollisionAttribute() & kAllGround)) {
        return false;
    }

    CollisionInfo collision = CheckCollision(other);

    // --- 2. 衝突応答 ---
    if (collision.isColliding) {

        // ★ 座標を押し戻す (めり込んだ分だけ座標を補正)
        this->transform_.translate += (collision.normal * collision.penetration);

        //  速度を補正 (壁にめり込む速度成分を打ち消す)
        float dot = math.Dot(velocity_, collision.normal);

        // 速度が法線と逆向き (dot < 0) ＝ めり込もうとしている場合のみ
        if (dot < 0) {
            // 法線方向の速度成分（dot）を、速度ベクトルから差し引く
            velocity_ = velocity_ - (collision.normal * dot);
        }

        //接地判定
        if (collision.normal.y > 0.9f) {
            isGrounded_ = true;
        }
    }

    // 衝突したかどうかを返す
    return collision.isColliding;
}

std::unique_ptr<Object3d> Character::Clone() const {
    auto newObj = std::make_unique<Character>();

    assert(common_ != nullptr);
    newObj->Initialize(common_);

    //  modelName_ (文字列) を使ってモデルをセット
    if (!modelName_.empty()) {
        newObj->SetModel(this->modelName_);
    }

    // Object3d の Transform 情報をコピー
    newObj->transform_ = this->transform_;

    // Object3d の名前をコピー
    newObj->name_ = this->name_;

    // Object3d のコライダー情報をコピー
    newObj->collisionAttribute_ = this->collisionAttribute_;
    newObj->collisionMask_ = this->collisionMask_;
    newObj->colliderType_ = this->colliderType_;

    // collisionSize_ (または半径) をコピー
    newObj->collisionSize_ = this->collisionSize_;

    // ---Character 独自のメンバをコピー ---
    newObj->velocity_ = this->velocity_;
    newObj->isGrounded_ = this->isGrounded_;
    newObj->gravity_ = this->gravity_;
    newObj->maxFallSpeed_ = this->maxFallSpeed_;

    return newObj;
}