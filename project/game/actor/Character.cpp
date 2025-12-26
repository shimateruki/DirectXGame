#define NOMINMAX
#include "Character.h"
#include "CollisionConfig.h"
#include "Math.h"
#include <algorithm> // std::min, std::max
#include <cmath>     // std::abs

// ★ Math のインスタンスを作成
static Math math;




void Character::Update(float deltaTime) {

    if (!this->param_.has_value()) {
        return;
    }

    float gravity = this->param_->gravity;
    float maxFallSpeed = this->param_->maxFallSpeed;


    isGrounded_ = false;
    velocity_.y -= gravity * deltaTime;

    if (velocity_.y < -maxFallSpeed) {
        velocity_.y = -maxFallSpeed;
    }
    if (this->param_->hp <=0)
    {
        isDead = true;
    }

    transform_.translate += velocity_ * deltaTime;
}

bool Character::OnCollision(Object3d* other) {
    // ★ 1. 衝突情報を取得
    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) {
        return false;
    }

    // 新しい関数に処理を委譲
    ApplyPhysicsCollision(info, other->GetCollisionAttribute());

    return info.isColliding;
}


void Character::ApplyPhysicsCollision(const CollisionInfo& info, uint32_t attribute) {
    // 地面属性以外は物理処理しない
    if (!(attribute & kAllSolid)) {
        return;
    }


    // ★ 座標を押し戻す (めり込んだ分だけ座標を補正)
    this->transform_.translate += (info.normal * info.penetration);

    //  速度を補正 (壁にめり込む速度成分を打ち消す)
    float dot = math.Dot(velocity_, info.normal);

    // 速度が法線と逆向き (dot < 0) ＝ めり込もうとしている場合のみ
    if (dot < 0) {
        // 法線方向の速度成分（dot）を、速度ベクトルから差し引く
        velocity_ = velocity_ - (info.normal * dot);
    }

    //接地判定
    if (info.normal.y > 0.9f) {
        isGrounded_ = true;
    }
}
std::unique_ptr<Object3d> Character::Clone() const {
    // Character として生成
    auto newObj = std::make_unique<Character>();

    assert(common_ != nullptr);
    newObj->Initialize(common_);

    // モデル設定
    if (!modelName_.empty()) {
        newObj->SetModel(this->modelName_);
    }

    // Transform 情報
    newObj->transform_ = this->transform_;

    // 名前
    newObj->name_ = this->name_;

    newObj->SetColliderConfig(this->colliderConfig_);

    // 属性とマスク
    newObj->collisionAttribute_ = this->collisionAttribute_;
    newObj->collisionMask_ = this->collisionMask_;



    // 1. イベントIDとステータス(param_)をコピー
    newObj->eventType_ = this->eventType_;
    newObj->param_ = this->param_; 

    // 2. Character 独自のメンバをコピー
    newObj->velocity_ = this->velocity_;
    newObj->isGrounded_ = this->isGrounded_;


    return newObj;
}
void Character::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // 親の描画処理をそのまま実行する
    if (!isDead)
    {
        Object3d::Draw(pointLightResource, spotLightResource);
    }
   
}