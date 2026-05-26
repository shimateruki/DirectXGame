#pragma once
#include "BaseScene.h" 
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "AudioPlayer.h"
#include "ParticleSystem.h" 
#include "ParticleCommon.h" 
#include "Player.h"
#include "Text.h"
#include "BulletManager.h"
#include "Camera.h"

#include "ObjectManager.h"
#include "LevelLoader.h"
#include <GhostRecorder.h>
#include <GameRule.h>
#include "OptionUI.h"
#include <memory>
#include <string>
#include <vector>

#include <GPUParticleEmitter.h>

// --- 前方宣言 ---
class DirectXCommon;
class InputManager;

// タイトルメニュー、設定画面、背景演出を管理するシーン。
class TitleScene : public BaseScene {
public:
    TitleScene() = default;
    ~TitleScene() override = default;

    // --- 基本サイクル ---
    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;

    // --- 描画 ---
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

    // --- BaseScene インターフェース実装 ---
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objectManager_->GetObjects(); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }

    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }


private:
    // --- 内部ヘルパー ---
    void SetSpriteTexturePreserveSize(Sprite* sprite, const std::string& textureName);
    void ApplyInputUiIfNeeded();

    // --- 外部システム参照 ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    // --- シーン内サブシステム ---
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    // --- 描画・生成の共通基盤 ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    // --- シーン所有リソース ---
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    Player* player_ = nullptr;

    // --- BGM ---
    uint32_t bgmHandle_ = 0;

    // --- タイトルUI演出 ---
    bool spritesAppear_ = false;
    float spritesAppearTimer_ = 0.0f;
    const float spritesAppearDuration_ = 0.6f;
    std::vector<int> menuSpriteIndices_;
    std::vector<float> spriteBaseYs_;
    Sprite* enterTextSprite_ = nullptr;
    bool titleUiUsesGamepad_ = false;
    bool hasAppliedTitleInputUi_ = false;

    // --- ライト・描画補助リソース ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    uint32_t gpuParticleTexHandle_ = 0;

    // --- メニュー状態 ---
    enum class TitleState {
        MainMenu,
        OptionMenu
    };
    TitleState currentState_ = TitleState::MainMenu;

    enum class MenuIndex {
        GameStart,
        Setting,
        Max
    };
    int currentMenuIndex_ = (int)MenuIndex::GameStart;

    // 設定画面を一時的に閉じたい場合に使う。
    bool settingEnabled_ = false;

    // --- オプションUI ---
    enum class OptionIndex {
        Sound,
        KeyConfig,
        Max
    };
    int currentOptionIndex_ = (int)OptionIndex::Sound;

    OptionUI optionUI_;


    // --- 背景ボスコア演出 ---
    std::vector<Object3d*> enemyCores_;
    std::vector<float> enemyCoreBaseYs_;
    float enemyCoreAmplitude_ = 0.5f;
    float enemyCoreSpeed_ = 1.5f;
    float enemyCoreTimer_ = 0.0f;

    std::vector<std::unique_ptr<GPUParticleEmitter>> bossContainerEmitters_;

    // --- 入力エッジ検出 ---
    bool prevStickUp_ = false;
    bool prevStickDown_ = false;
};
