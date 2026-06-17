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
#include "game/ui/SettingsMenuOverlay.h"

#include <array>
#include <memory>
#include <vector>
#include <Skybox.h>

// --- 前方宣言 ---
class DirectXCommon;
class InputManager;

/// <summary>
/// タイトルシーン
/// </summary>
class TitleScene : public BaseScene {
public:
    TitleScene() = default;
    ~TitleScene() override = default;

    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void Draw() override;
    void DrawUI() override;

    //  シャドウマップ描画のオーバーライド
    void DrawShadow() override;

    // --- BaseScene インターフェース実装 (ObjectManagerへ委譲) ---
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
    enum class TitleMode {
        MainMenu,
        SaveSelect
    };

    enum class SaveSelectMode {
        Browse,
        DeleteConfirm
    };

    void InitializeSaveSlotUI();
    void InitializeTitleLogoUI();
    void UpdateMainMenu();
    void UpdateSaveSelect();
    void UpdateTitleLogoIntro(float deltaTime);
    void UpdateSaveSlotUI();
    void DrawSaveSlotUI();
    void StartSelectedSaveSlot();
    void DeleteSelectedSaveSlot();
    Sprite* CreateUISprite(const std::string& texturePath, const Vector2& position, const Vector2& size, const Vector4& color);
    void SetNumberSprites(std::array<Sprite*, 2>& digits, int value, const Vector4& color, bool visible);

    // --- システムポインタ ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    // --- サブシステム (管理クラス) ---
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    // --- 共通基盤クラス ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    // --- リソース ---
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    std::unique_ptr<Skybox> skybox_ = nullptr;
    Player* player_ = nullptr;

    uint32_t bgmHandle_ = 0;
    uint32_t skyboxTextureHandle_ = 0;

    // --- ライト・GPUリソース ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;


    //  GPUパーティクル用画像ハンドル
    uint32_t gpuParticleTexHandle_ = 0;

    // メニューの選択肢
    enum class MenuIndex {
        GameStart,
        Setting,
        Max // 項目数を取るためのダミー
    };

    int currentMenuIndex_ = (int)MenuIndex::GameStart; // 現在の選択番号
    TitleMode titleMode_ = TitleMode::MainMenu;
    SaveSelectMode saveSelectMode_ = SaveSelectMode::Browse;
    int currentSaveSlotIndex_ = 0;
    int saveSelectFocusIndex_ = 0;
    int deleteConfirmIndex_ = 1;
    float titleUiTime_ = 0.0f;

    // スプライトのポインタを保持しておく
    Sprite* titleTextSprite_ = nullptr;
    Sprite* startTextSprite_ = nullptr;
    Sprite* settingTextSprite_ = nullptr;
    Vector2 startTextBaseSize_ = { 0.0f, 0.0f };
    Vector2 settingTextBaseSize_ = { 0.0f, 0.0f };
    struct TitleLogoGlyph {
        Sprite* sprite = nullptr;
        Vector2 basePosition = { 0.0f, 0.0f };
        Vector2 baseSize = { 0.0f, 0.0f };
        float delay = 0.0f;
    };
    std::array<TitleLogoGlyph, 8> titleLogoGlyphs_{};
    float titleIntroTime_ = 0.0f;
    bool titleIntroComplete_ = false;
    std::vector<std::unique_ptr<Sprite>> titleUiSprites_;
    std::array<Sprite*, 3> saveSlotCards_ = { nullptr, nullptr, nullptr };
    std::array<std::array<Sprite*, 4>, 3> saveSlotFrames_{};
    std::array<Sprite*, 3> saveSlotIcons_ = { nullptr, nullptr, nullptr };
    std::array<Sprite*, 3> saveSlotNumberSprites_ = { nullptr, nullptr, nullptr };
    std::array<Sprite*, 3> saveSlotFileNameTexts_ = { nullptr, nullptr, nullptr };
    std::array<Sprite*, 3> saveSlotStatusTexts_ = { nullptr, nullptr, nullptr };
    std::array<Sprite*, 3> saveSlotLifeIcons_ = { nullptr, nullptr, nullptr };
    std::array<Sprite*, 3> saveSlotLifeXIcons_ = { nullptr, nullptr, nullptr };
    std::array<std::array<Sprite*, 2>, 3> saveSlotLifeDigits_{};
    std::array<Sprite*, 3> saveSlotCrownIcons_ = { nullptr, nullptr, nullptr };
    std::array<Sprite*, 3> saveSlotCrownXIcons_ = { nullptr, nullptr, nullptr };
    std::array<std::array<Sprite*, 2>, 3> saveSlotCrownDigits_{};
    std::array<Sprite*, 3> saveSlotStarIcons_ = { nullptr, nullptr, nullptr };
    std::array<Sprite*, 3> saveSlotStarXIcons_ = { nullptr, nullptr, nullptr };
    std::array<std::array<Sprite*, 2>, 3> saveSlotStarDigits_{};
    std::array<Sprite*, 3> saveSlotPlayTimeLabels_ = { nullptr, nullptr, nullptr };
    std::array<std::array<Sprite*, 5>, 3> saveSlotPlayTimeDigits_{};
    Sprite* saveSelectHeader_ = nullptr;
    Sprite* saveDeleteButtonBack_ = nullptr;
    Sprite* saveDeleteButtonText_ = nullptr;
    Sprite* savePromptBubble_ = nullptr;
    Sprite* savePromptText_ = nullptr;
    Sprite* saveDeleteQuestionText_ = nullptr;
    Sprite* saveConfirmYesText_ = nullptr;
    Sprite* saveConfirmBackText_ = nullptr;
    std::unique_ptr<SettingsMenuOverlay> settingsOverlay_ = nullptr;
};
