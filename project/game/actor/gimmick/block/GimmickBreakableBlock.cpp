#include "GimmickBreakableBlock.h"
#include "CollisionConfig.h"
#include "EnemyBomb.h"
#include "Player.h"
#include <DebrisEffectManager.h>
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

    const uint32_t attribute = other->GetCollisionAttribute();
    const auto* bomb = dynamic_cast<const EnemyBomb*>(other);

    // プレイヤーが生成したボムの爆風だけを破壊条件にする。
    if (bomb && bomb->IsPlayerOwned() &&
        (attribute & kPlayerAttack) &&
        other->GetColliderType() == ColliderType::kSphere) {
        
        Break();
        return true;
    }

    // 地形としての押し戻し処理を実行
    return BaseGimmick::OnCollision(other);
}

bool GimmickBreakableBlock::TryBreakByPlayerImpact(const Player* player) {
    if (isBroken_ || !player || !param_.has_value()) {
        return false;
    }

    const int actionMode = param_->actionMode;
    if (actionMode == 7) {
        if (!player->IsPinkBounceSlamImpactActive()) {
            return false;
        }
    }
    else if (actionMode == 6) {
        if (!player->IsImpactBreakActive()) {
            return false;
        }
    }
    else {
        return false;
    }

    Break();
    return true;
}

void GimmickBreakableBlock::Break() {
    isBroken_ = true;

    // 突進のSphereCastと同じフレームで通過できるよう、破壊時点で判定を外す。
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    isDead = true;

    Vector3 myPos = GetTranslate();

    // 屈折マテリアルのガラスは、炎を残さず透明破片と短い閃光で割れた瞬間を見せる。
    if (GetMaterialType() == 10 || GetModelName() == "Stages/star_garden_glass_panel") {
        DebrisEffectManager::GetInstance()->Spawn("star_garden_glass_shatter", myPos);
        if (GPUParticleManager::GetInstance()) {
            GPUParticleManager::GetInstance()->Emit("star_garden_glass_shards", myPos);
            GPUParticleManager::GetInstance()->Emit("hit_bomb_flash_core", myPos);
        }
        return;
    }

    // 石ブロックは従来どおり小規模な爆発と火花で破壊する。
    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            "Resources/json/effect/effect_bakuhatu.json", 
            myPos, 
            { 0.0f, 0.0f, 0.0f }, 
            { 0.5f, 0.5f, 0.5f }
        );
    }

    DebrisEffectManager::GetInstance()->Spawn("bomb_hit_fragment_burst", myPos);
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
