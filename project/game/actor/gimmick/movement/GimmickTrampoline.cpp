#include "GimmickTrampoline.h"
#include "Player.h"
#include "Math.h"
#include "CollisionConfig.h"
#include "DebugConsole.h"

void GimmickTrampoline::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("Trampoline");
    
    if (!param_.has_value()) param_.emplace();
    param_->jumpPower = 30.0f; // デフォルトのジャンプ力
    
    // 見た目と当たり判定
    ColliderConfig config = GetColliderConfig();
    config.type = ColliderType::kOBB;
    config.size = { 1.0f, 0.2f, 1.0f }; // 半径（中心から各辺まで）
    SetColliderConfig(config);
    
    // 衝突属性の設定 (地面属性を持たせることで足場として機能させる)
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b1111); // 全てと当たる
}

void GimmickTrampoline::Update(float deltaTime) {
    BaseGimmick::Update(deltaTime);
}

bool GimmickTrampoline::OnCollision(Object3d* other) {
    if (!other) return false;

    // 相手がプレイヤーかどうかを判定 (ClassName または dynamic_cast)
    bool isPlayer = (other->GetClassName() == "Player");
    
    if (isPlayer) {
        CollisionInfo info = CheckCollision(other);
        if (info.isColliding) {
            // プレイヤーが「上から」踏んでいるか判定
            // info.normal は「自分から相手へ」の向き（押し出す方向）
            if (info.normal.y > 0.3f) {
                Player* player = dynamic_cast<Player*>(other);
                if (player) {
                    Vector3 velocity = player->GetVelocity();
                    velocity.y = param_->jumpPower;
                    player->SetVelocity(velocity);
                    
                    DebugConsole::GetInstance()->AddLog("Trampoline JUMP! Power: " + std::to_string(param_->jumpPower));
                }
            }
        }
    }

    return true;
}
