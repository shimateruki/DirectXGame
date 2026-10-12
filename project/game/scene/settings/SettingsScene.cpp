#include "game/scene/settings/SettingsScene.h"
#include "DirectXCommon.h"
#include "SceneManager.h"
#include "ScenePreloader.h"
#include "Sprite.h"
#include "GameSettingsManager.h"

SceneLoadManifest SettingsScene::BuildAsyncLoadManifest() const {
    // 設定画面には先読み対象のシーンAssetを持たせない。
    SceneLoadManifest manifest;
    return manifest;
}

void SettingsScene::Initialize() {
    // 保存済み設定を読み込んでからSpriteメニューへ反映する。
    GameSettingsManager::GetInstance()->Load();
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(DirectXCommon::GetInstance());
    menu_.Initialize(spriteCommon_.get());
    menu_.SetActive(true);
}

void SettingsScene::Finalize() {
    // 未適用値を復元してから描画リソースを破棄します。
    menu_.Finalize();
    sprites_.clear();
    spriteCommon_.reset();
}

void SettingsScene::Update(float deltaTime) {
    // 設定メニューが閉じた時だけタイトルへ戻す。
    if (menu_.Update(deltaTime) == SettingsMenuOverlay::Result::Closed)
        SceneManager::GetInstance()->ChangeScene("TITLE");
}

void SettingsScene::DrawUI() {
    // Sprite共通リソースが未生成ならUI描画をスキップする。
    if (!spriteCommon_)
        return;
    spriteCommon_->SetPipeline(DirectXCommon::GetInstance()->GetCommandList());
    menu_.Draw();
}
