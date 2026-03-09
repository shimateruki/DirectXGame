#include "MapBlock.h"
#include "CollisionConfig.h"

void MapBlock::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);
    
    // マップブロックとしての属性を設定
    SetCollisionAttribute(kMapBlock);
    // 地形（Ground）としても機能させたい場合は以下のようにビットORをとる
    // SetCollisionAttribute(kMapBlock | kGround);
    
    // デフォルトでは押し出し対象にする
    SetCollisionMask(kPlayer | kEnemy); 
    
    SetClassName("MapBlock");
}

void MapBlock::Update(float deltaTime) {
    if (isAbsorbed_) return;

    Object3d::Update(deltaTime);
}

void MapBlock::OnAbsorbed() {
    isAbsorbed_ = true;
    SetIsVisible(false);
    // 衝突判定も無効化する
    SetCollisionAttribute(0);
}
