#define NOMINMAX
#include "TitleScene.h"
#include "SceneManager.h"
#include "GamePlayScene.h" // ★ 次のシーン
#include "DirectXCommon.h" // ★
#include "SpriteCommon.h"  // ★
#include "Sprite.h"        // ★
#include <cassert>

void TitleScene::Initialize() {
    // --- 基盤クラスのポインタを保持 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    assert(sceneManager_); // SetSceneManagerが呼ばれているか確認

    // --- スプライト共通処理の初期化 ---
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    // --- タイトルスプライトの生成 ---
    // (例として monsterBall を中央に表示)
    uint32_t titleTexHandle = Sprite::LoadTexture("monsterBall");
    titleSprite_ = std::make_unique<Sprite>();
    titleSprite_->Initialize(spriteCommon_.get(), titleTexHandle);

    // 画面中央 (1280/2, 720/2) に設定
    titleSprite_->SetPosition({ 640.0f, 360.0f });
    titleSprite_->SetSize({ 1280.0f,720.0f });
    // (必要なら SetSize() でサイズ調整)

    // ★ 初期化完了時にコマンドをフラッシュ
    dxCommon_->FlushCommandQueue(false);
}

void TitleScene::Finalize() {
    // ★ スプライト関連の解放
    titleSprite_.reset();
    spriteCommon_.reset();
}

void TitleScene::Update() {
    // ★ BaseScene の sceneManager_ を使う
    assert(inputManager_ && sceneManager_);
    // ★ スプライトの座標計算などを実行
    if (titleSprite_) {
        titleSprite_->Update();
    }
    if (inputManager_->IsKeyTriggered(DIK_RETURN)) {

        // 1. 次のシーン (GamePlayScene) を new する
        BaseScene* nextScene = new GamePlayScene();

        // ★ 2. BaseScene の仮想関数 SetSceneManager を呼ぶだけ（キャスト不要）
        nextScene->SetSceneManager(sceneManager_);

        // 3. SceneManager に次のシーンを予約する
        sceneManager_->SetNextScene(nextScene);

        return;
    }
}

void TitleScene::Draw() {
    // (注: dxCommon_->PreDraw() は Game.cpp で呼ばれます)

    // --- スプライト描画 ---
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    if (titleSprite_) {
        titleSprite_->Draw();
    }

}