#include "game/scene/settings/SettingsScene.h"
#include "DirectXCommon.h"
#include "SceneManager.h"
#include "ScenePreloader.h"
#include "Sprite.h"
#include "GameSettingsManager.h"

SceneLoadManifest SettingsScene::BuildAsyncLoadManifest() const {
    SceneLoadManifest manifest;
    manifest.AddAudio(ResolveSceneBgmPath("Resources/audio/Alarm02.mp3"));
    return manifest;
}

void SettingsScene::Initialize() {
    GameSettingsManager::GetInstance()->Load();
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(DirectXCommon::GetInstance());
    menu_.Initialize(spriteCommon_.get());
    menu_.SetActive(true);
    bgmHandle_ = AudioPlayer::GetInstance()->LoadSoundFile(ResolveSceneBgmPath("Resources/audio/Alarm02.mp3"));
    AudioPlayer::GetInstance()->PlayBGM(bgmHandle_, true, .65f);
}

void SettingsScene::Finalize() {
    // 未適用値を復元してから描画リソースを破棄します。
    menu_.Finalize();
    AudioPlayer::GetInstance()->StopBGM();
    sprites_.clear();
    spriteCommon_.reset();
}

void SettingsScene::Update(float deltaTime) {
    if (menu_.Update(deltaTime) == SettingsMenuOverlay::Result::Closed)
        SceneManager::GetInstance()->ChangeScene("TITLE");
}

void SettingsScene::DrawUI() {
    if (!spriteCommon_)
        return;
    spriteCommon_->SetPipeline(DirectXCommon::GetInstance()->GetCommandList());
    menu_.Draw();
}
