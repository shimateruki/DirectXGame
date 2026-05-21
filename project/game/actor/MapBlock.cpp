#include "MapBlock.h"
#include "CollisionConfig.h"
#include "DebugConsole.h"
#include <algorithm> // ★ std::find を使うために追加
#include "SceneManager.h"
#include "BaseScene.h"
#include "CollisionManager.h"

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
    SetCollisionAttribute(kMapBlock);
    SetCollisionMask(kPlayer | kEnemy);
    SetClassName("MapBlock");
    s_activeBlocks.push_back(this);

    auto laserBeam = std::make_unique<Object3d>();
    laserBeam->Initialize(common);
    laserBeam->SetName("Beam_Cylinder");
    laserBeam->SetModel("Cylinder");
    laserBeam->SetParent(this);
    laserBeam->SetScale({ 0.0f, 0.0f, 0.0f });
    laserBeam->SetCollisionAttribute(0);

    // 自分の子供名簿に登録（攻撃クラスの検索用）
    children_.push_back(laserBeam.get());

    CollisionManager::GetInstance()->AddObject(laserBeam.get());

    // ==========================================
    // ★ ここがポイント！SceneManagerから現在のシーンを取得して叩き込む！
    // ==========================================
    // 🚨 注意： GetCurrentScene() の部分は、タイクラーさんの
    // SceneManagerクラスにある「現在のシーンを取得する関数名」に書き換えてください！
    if (BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene()) {
        currentScene->AddObject(std::move(laserBeam));
    }
}

void MapBlock::Update(float deltaTime) {
    // ==========================================
    // レーザーの更新は、吸収されていても常にやる！
    // これをやらないと座標や大きさが計算されません！
    // ==========================================
    if (laserBeam_) {
        laserBeam_->Update(deltaTime);
    }

    if (isAbsorbed_) return;

    Object3d::Update(deltaTime);
}

void MapBlock::OnAbsorbed() {
    isAbsorbed_ = true;
    SetIsVisible(false);
    // 衝突判定も無効化する
    SetCollisionAttribute(0);
}