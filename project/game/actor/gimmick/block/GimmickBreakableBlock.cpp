#include "GimmickBreakableBlock.h"
#include "ObjectPropertyDefinition.h"
#include "CollisionConfig.h"
#include "EnemyBomb.h"
#include "Player.h"
#include <DebrisEffectManager.h>
#include <MeshEffectManager.h>
#include <GpuParticleManager.h>
#include <DebugConsole.h>
#include <cmath>

void GimmickBreakableBlock::Initialize(engine::Object3DCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // 専用ブロックのテクスチャと材質はモデルに対応する場合だけ上書きします。
    if (modelName == "Stages/bomb_break_block") {
        SetTexture("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_albedo.png");
        SetEnableNormalMap(true);
        SetNormalMap("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_normal.png");
        SetOrmMap("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_orm.png");
        SetMaterialType(0);
        SetColor({1.0f, 1.0f, 1.0f, 1.0f});
        SetMetallic(0.0f);
        SetRoughness(0.72f);
        SetEnableEnvMap(false);
        SetEmissive(1.0f);
    }

    // 地形（足場）として振る舞うため、属性を ground に設定
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask()); // すべてのオブジェクトと衝突して押し戻す

    // OBBコライダーでモデルの大きさに自動フィットさせる
    SetColliderType(ColliderType::Obb);

    isBroken_ = false;
    SetClassName("Gimmick");
    SetGimmickType("BreakableBlock");

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickBreakableBlock;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "破壊ブロック",
        "爆風や体当たりで壊れる条件を指定します。");
    using Parameter = engine::Object3D::EntityParameter;
    properties.Parameter("破壊条件", "actionMode", &Parameter::actionMode, 0.0f, static_cast<float>(kDashPanelOnly),
        nullptr, {"爆風のみ", "汎用衝撃でも破壊", "ピンクのバウンド落下でも破壊", "ダッシュパネル加速中のみ"},
        {.values = {0, 6, 7, kDashPanelOnly}});
}

void GimmickBreakableBlock::Update(float deltaTime) {
    if (isBroken_) {
        // 破壊されたら速やかに当たり判定を消滅させ、次のフレームで消滅
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        SetDead(true);
        return;
    }

    BaseGimmick::Update(deltaTime);
}

bool GimmickBreakableBlock::OnCollision(engine::Object3D* other) {
    if (isBroken_)
        return false;

    // 専用モードは爆風を無視し、プレイヤー本体の衝突でのみ破壊を判定します。
    const auto& parameters = GetParameters();
    if (parameters && parameters->actionMode == kDashPanelOnly) {
        return BaseGimmick::OnCollision(other);
    }

    const uint32_t attribute = other->GetCollisionAttribute();
    const auto* bomb = dynamic_cast<const EnemyBomb*>(other);

    // プレイヤーが生成したボムの爆風だけを破壊条件にする。
    if (bomb && bomb->IsPlayerOwned() && bomb->IsExploded() &&
        (attribute & CollisionCategories::Get("player_attack")) &&
        other->GetColliderType() == ColliderType::Sphere) {

        Break();
        return true;
    }

    // 地形としての押し戻し処理を実行
    return BaseGimmick::OnCollision(other);
}

bool GimmickBreakableBlock::TryBreakByPlayerImpact(const Player* player) {
    // 破壊済みなら再実行せず、Playerの衝突条件を満たした一回だけ破壊する。
    if (isBroken_ || !player || !GetParameters().has_value()) {
        return false;
    }

    // 配置時の破壊条件ごとにプレイヤーのどの技を有効とするか判定します。爆風は OnCollision() が確認します。
    const int actionMode = GetParameters()->actionMode;

    if (actionMode == kDashPanelOnly) {
        // パネルの加速状態を確認し、通常の移動や別の技による衝突は受け付けません。
        if (!player->IsDashPanelBoostActive()) {
            return false;
        }
    } else if (actionMode == 7) {
        // ピンクのバウンドで地面へ向かう衝撃だけを受け付けます。
        if (!player->IsPinkBounceSlamImpactActive()) {
            return false;
        }
    } else if (actionMode == 6) {
        // プレイヤー側が汎用の破壊フラグを有効にしている間だけ受け付けます。
        if (!player->IsImpactBreakActive()) {
            return false;
        }
    // ほかの設定ではプレイヤー本体に当たっても足場のまま扱います。
    } else {
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
    SetDead(true);

    // 親の地形を含む座標から破片と閃光を出し、ローカル原点へのずれを防ぎます。
    engine::Vector3 myPos = GetWorldPosition();

    // 屈折マテリアルのガラスは、炎を残さず透明破片と短い閃光で割れた瞬間を見せる。
    if (GetMaterialType() == 10 || GetModelName() == "Stages/star_garden_glass_panel") {
        const bool isGiantGate = GetModelName() == "Stages/star_garden_giant_gate";

        // 巨大な門だけは破片の着地高さをモデルの下端に合わせます。
        if (isGiantGate) {
            const Aabb bounds = GetModelWorldAabb();
            const float groundY = std::isfinite(bounds.min.y) ? bounds.min.y : myPos.y - 4.2f;
            DebrisEffectManager::GetInstance()->SpawnOnGround("shared/star_garden_gate_glass_shatter", myPos, groundY);
        } else {
            DebrisEffectManager::GetInstance()->Spawn("shared/star_garden_glass_shatter", myPos);
        }

        if (GpuParticleManager::GetInstance()) {
            GpuParticleManager::GetInstance()->Emit("shared/star_garden_glass_shards", myPos);
            GpuParticleManager::GetInstance()->Emit("shared/hit_bomb_flash_core", myPos);
        }

        // ガラスの演出が終わったら戻り、石ブロック用の爆発を重ねません。
        return;
    }

    // 石ブロックは従来どおり小規模な爆発と火花で破壊する。
    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt("Resources/json/effect/shared/effect_bakuhatu.json", myPos,
                                                        {0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f});
    }

    DebrisEffectManager::GetInstance()->Spawn("shared/bomb_hit_fragment_burst", myPos);

    if (GpuParticleManager::GetInstance()) {
        GpuParticleManager::GetInstance()->Emit("shared/star_sparkleGet", myPos);
    }
}

std::unique_ptr<engine::Object3D> GimmickBreakableBlock::Clone() const {
    // 同じモデルで新しい実体を初期化してから、配置と破壊設定を複製する。
    auto newObj = std::make_unique<GimmickBreakableBlock>();
    assert(GetObjectCommon() != nullptr);
    newObj->Initialize(GetObjectCommon(), GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
