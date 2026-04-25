#include "GimmickMovingFloor.h"
#include <cmath>
#include "CollisionConfig.h"

void GimmickMovingFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("MovingFloor");
    
    if (!param_.has_value()) param_.emplace();
    if (param_->speed == 0.0f) param_->speed = 2.0f; // デフォルト速度
    
    startY_ = GetTransform()->translate.y;
    
    // 当たり判定設定 (地面として扱う)
    SetCollisionAttribute(kGround);
}

void GimmickMovingFloor::Update(float deltaTime) {
    BaseGimmick::Update(deltaTime);
    
    time_ += deltaTime;
    
    // サイン波で上下に動く
    // エディタの Speed パラメータを使用
    float range = 3.0f;
    float speed = param_->speed;
    
    GetTransform()->translate.y = startY_ + std::sin(time_ * speed) * range;
}

bool GimmickMovingFloor::OnCollision(Object3d* other) {
    // 物理的な押し出しなどは親クラスやCharacter側が担当する
    (void)other;
    return true;
}
