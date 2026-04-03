#include "MapBlock.h"
#include "CollisionConfig.h"
#include <algorithm> // ★ std::find を使うために追加

// ==========================================
// 静的リスト（名簿）の実体を定義
// ==========================================
std::vector<MapBlock*> MapBlock::s_activeBlocks;

// デストラクタで自分を名簿から消す（エラー防止）
MapBlock::~MapBlock() {
    auto it = std::find(s_activeBlocks.begin(), s_activeBlocks.end(), this);
    if (it != s_activeBlocks.end()) {
        s_activeBlocks.erase(it);
    }
}

void MapBlock::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);

    // マップブロックとしての属性を設定
    SetCollisionAttribute(kMapBlock);
    // 地形（Ground）としても機能させたい場合は以下のようにビットORをとる
    // SetCollisionAttribute(kMapBlock | kGround);

    // デフォルトでは押し出し対象にする
    SetCollisionMask(kPlayer | kEnemy);

    SetClassName("MapBlock");

    // ==========================================
    // 自分が生まれたら名簿に登録する！
    // ==========================================
    s_activeBlocks.push_back(this);
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