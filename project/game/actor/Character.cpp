#define NOMINMAX
#include "Character.h"
#include "CollisionConfig.h"
#include "Math.h"
#include <algorithm>
#include <cmath>
#include "GhostRecorder.h"

static Math math;

// =================================================================
// 更新・描画処理
// =================================================================

void Character::Update(float deltaTime) {
    Object3d::Update(deltaTime);

    if (!param_.has_value()) {
        return;
    }

    // 重力と落下速度の計算
    isGrounded_ = false;
    velocity_.y -= param_->gravity * deltaTime;

    if (velocity_.y < -param_->maxFallSpeed) {
        velocity_.y = -param_->maxFallSpeed;
    }

    // 死亡判定 (Player, BossCore, TutorialDoll以外はHP0で自動消滅フラグを立てる)
    if (param_->hp <= 0 && className_ != "Player" && className_ != "BossCore" && className_ != "TutorialDoll") {
        isDead = true;
    }

    // 速度を座標に適用
    transform_.translate += velocity_ * deltaTime;
}

void Character::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {

        Object3d::Draw(pointLightResource, spotLightResource);
    
}

// =================================================================
// 衝突判定・物理挙動
// =================================================================

bool Character::OnCollision(Object3d* other) {
    CollisionInfo info = CheckCollision(other);

    if (!info.isColliding) {
        return false;
    }

    // 物理的な押し戻し・速度補正を適用
    ApplyPhysicsCollision(info, other->GetCollisionAttribute());

    return info.isColliding;
}

void Character::ApplyPhysicsCollision(const CollisionInfo& info, uint32_t attribute) {
    // 地面や壁(ソリッド属性)以外は物理的な押し出しを行わない
    if (!(attribute & kAllSolid)) {
        return;
    }

    Vector3 pushNormal = info.normal;

    // 1. 座標の押し戻し (めり込み解消)
    transform_.translate += (pushNormal * info.penetration);

    // 2. 速度の補正 (壁ずり・床滑り)
    float dot = math.Dot(velocity_, pushNormal);
    if (dot < 0.0f) {
        // 壁に向かって進んでいる成分を打ち消し、表面に沿って滑らせる
        velocity_ = velocity_ - (pushNormal * dot);
    }

    // 3. 接地判定と重力リセット (坂道対応)
    const float kSlopeThreshold = 0.7f; // 法線のY成分が約45度以上なら地面と判定

    if (pushNormal.y > kSlopeThreshold) {
        isGrounded_ = true;

        // 坂を登る際に重力でずり落ちないよう、下方向の速度をリセット
        if (velocity_.y < 0.0f) {
            velocity_.y = 0.0f;
        }
    }
}

// =================================================================
// オブジェクト複製 (Clone)
// =================================================================

std::unique_ptr<Object3d> Character::Clone() const {
    auto newObj = std::make_unique<Character>();

    assert(common_ != nullptr);
    newObj->Initialize(common_);

    // 1. 基本設定とTransformのコピー
    if (!GetModelName().empty()) {
        newObj->SetModel(GetModelName());
    }
    newObj->transform_ = transform_;
    newObj->SetName(name_);
    newObj->SetClassName(GetClassName());
    newObj->SetEnemyType(GetEnemyType());

    // 2. 衝突判定・属性のコピー
    newObj->SetColliderConfig(GetColliderConfig());
    newObj->SetCollisionAttribute(GetCollisionAttribute());
    newObj->SetCollisionMask(GetCollisionMask());

    // 3. ステータスと状態フラグのコピー
    newObj->SetEventType(eventType_);
    if (param_.has_value()) {
        newObj->param_ = param_.value();
    }
    newObj->velocity_ = velocity_;
    newObj->isGrounded_ = isGrounded_;

    // 4. アニメーション設定のコピー
    newObj->animName_ = animName_;
    newObj->isAnimLoop_ = isAnimLoop_;

    // 5. ゴーストレコーダー(パス移動)のコピーと初期化
    newObj->recordPathName_ = recordPathName_;
    newObj->isRecordLoop_ = isRecordLoop_;
    newObj->isRecordRelative_ = isRecordRelative_;

    newObj->InitializeRecorder(nullptr);

    // パス名が設定されていれば自動再生を開始
    if (!newObj->recordPathName_.empty() && newObj->recorder_) {
        bool isCinematic = (newObj->GetClassName() == "CinematicCamera");
        newObj->recorder_->Play(
            newObj->recordPathName_,
            newObj->isRecordLoop_,
            newObj->isRecordRelative_,
            isCinematic
        );
    }

    return newObj;
}