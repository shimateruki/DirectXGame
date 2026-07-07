#include "GimmickBreakableBlock.h"
#include "CollisionConfig.h"
#include <MeshEffectManager.h>
#include <GPUParticleManager.h>
#include <DebugConsole.h>

void GimmickBreakableBlock::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    if (modelName == "Stages/bomb_break_block") {
        SetTexture("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_albedo.png");
        SetEnableNormalMap(true);
        SetNormalMap("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_normal.png");
        SetOrmMap("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_orm.png");
        SetMaterialType(0);
        SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        SetMetallic(0.0f);
        SetRoughness(0.72f);
        SetEnableEnvMap(false);
        SetEmissive(1.0f);
    }
    
    // 地形（足場）として振る舞うため、属性を kGround に設定
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111); // すべてのオブジェクトと衝突して押し戻す
    
    // OBBコライダーでモデルの大きさに自動フィットさせる
    SetColliderType(ColliderType::kOBB);
    
    isBroken_ = false;
    SetClassName("Gimmick");
    SetGimmickType("BreakableBlock");
}

void GimmickBreakableBlock::Update(float deltaTime) {
    if (isBroken_) {
        // 破壊されたら速やかに当たり判定を消滅させ、次のフレームで消滅
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        isDead = true;
        return;
    }
    
    BaseGimmick::Update(deltaTime);
}

bool GimmickBreakableBlock::OnCollision(Object3d* other) {
    if (isBroken_) return false;

    uint32_t attribute = other->GetCollisionAttribute();

    // ボムの爆風（属性：kPlayerAttack | kEnemyAttack ＆ コライダー形状：kSphere）に当たった場合
    if ((attribute & (kPlayerAttack | kEnemyAttack)) && 
        (other->GetColliderType() == ColliderType::kSphere)) {
        
        Break();
        return true;
    }

    // 地形としての押し戻し処理を実行
    return BaseGimmick::OnCollision(other);
}

void GimmickBreakableBlock::Break() {
    isBroken_ = true;

    // 破壊時の豪華なビジュアルエフェクトの発生
    Vector3 myPos = GetTranslate();
    
    // 1. 火花や煙のエフェクト (effect_bakuhatu.json を少し小さめに出す、あるいは破片エフェクト)
    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            "Resources/json/effect/effect_bakuhatu.json", 
            myPos, 
            { 0.0f, 0.0f, 0.0f }, 
            { 0.5f, 0.5f, 0.5f } // 少し小規模な爆発破片として演出
        );
    }

    // 2. GPUパーティクルで破片をまき散らす (プレミアム演出！)
    if (GPUParticleManager::GetInstance()) {
        GPUParticleManager::GetInstance()->Emit("star_sparkleGet", myPos);
    }
}

std::unique_ptr<Object3d> GimmickBreakableBlock::Clone() const {
    auto newObj = std::make_unique<GimmickBreakableBlock>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
