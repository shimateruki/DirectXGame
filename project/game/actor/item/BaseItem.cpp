#include "BaseItem.h"
#include "CollisionConfig.h"
#include "ModelManager.h"
#include "Player.h"
#include <cassert>

void BaseItem::Initialize(Object3dCommon* common, const std::string& modelName) {
    engine::Object3d::Initialize(common);

    // アイテムの見た目は派生型ごとに選び、取得判定の土台だけを共通化します。
    ModelManager::GetInstance()->LoadModel(modelName);
    SetModel(modelName);

    SetClassName("Item");
    SetSaveCategory("Object");
    SetItemType("");
    SetStatic(false);

    // プレイヤーとの接触だけを拾うトリガーにし、地形の物理衝突には参加させません。
    ColliderConfig config;
    config.type = ColliderType::kSphere;
    config.size = {1.0f, 1.0f, 1.0f};
    SetColliderConfig(config);
    SetCollisionRadius(1.0f);

    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));
}

void BaseItem::Update(float deltaTime) {
    // 取得済みのアイテムは同じフレーム中にも再取得できないようにします。
    if (isCollected_) {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        isDead = true;
        return;
    }

    // 待機中は経過秒数に応じて回転し、フレームレートによる速度差をなくします。
    engine::Vector3 rotate = GetRotation();
    rotate.y += rotationSpeed_ * deltaTime;
    if (rotate.y > 6.283185f) {
        rotate.y -= 6.283185f;
    }
    SetRotation(rotate);

    engine::Object3d::Update(deltaTime);
}

// 対象がプレイヤーで実際に重なった時だけ、派生クラスの取得処理を呼びます。
bool BaseItem::OnCollision(engine::Object3d* other) {
    // 属性だけで確定せず、相手の型と実際の重なりを確認してから取得します。
    if (isCollected_ || !other) {
        return false;
    }

    Player* player = dynamic_cast<Player*>(other);
    if (!player) {
        return false;
    }

    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) {
        return false;
    }

    Collect(player);
    return true;
}

void BaseItem::Collect(Player* player) {
    // 派生アイテムは効果を適用してから、この共通の取得確定処理を呼べます。
    (void)player;
    MarkCollected();
}

// 同じフレームの二重取得を防ぐため、取得直後に衝突と表示を無効化します。
void BaseItem::MarkCollected() {
    // 死亡フラグが反映される前にも、表示と判定を即時に止めます。
    isCollected_ = true;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetIsVisible(false);
    isDead = true;
}

std::unique_ptr<engine::Object3d> BaseItem::Clone() const {
    // 初期化で描画・判定資源を作り直してから、編集可能な設定値を引き継ぎます。
    auto newObj = std::make_unique<BaseItem>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
