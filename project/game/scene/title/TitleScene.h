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
#include <string>
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
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void Draw() override;
    void DrawUI() override;

    //  シャドウマップ描画のオーバーライド
    void DrawShadow() override;

    // --- BaseScene インターフェース実装 (ObjectManagerへ委譲) ---
    const std::vector<std::unique_ptr<Object3d>>& GetObjects() const override { return objectManager_->GetObjects(); }
    Object3d* AddObjectImmediate(std::unique_ptr<Object3d> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }
    std::unique_ptr<Object3d> TakeObjectImmediate(Object3d* object) override { return objectManager_->TakeObjectImmediate(object); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }

    const std::vector<std::unique_ptr<Sprite>>& GetSprites() const override { return sprites_; }
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    Skybox* GetSkybox() override { return skybox_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

  private:
    std::vector<std::unique_ptr<Sprite>>* GetMutableSprites() override { return &sprites_; }
    enum class TitleMode { MainMenu, SaveSelect };

    enum class SaveSelectMode { Browse, DeleteConfirm, Launching };

    void InitializeSaveSlotUI();
    void InitializeTitleLogoUI();
    void InitializeMainMenuUI();
    void UpdateMainMenu();
    void UpdateSaveSelect(float deltaTime);
    void UpdateTitleLogoIntro(float deltaTime);
    void UpdateTitleHeroAnimation(float deltaTime);
    void UpdateSaveSlotUI(float deltaTime);
    void DrawSaveSlotUI();
    void StartSelectedSaveSlot();
    void DeleteSelectedSaveSlot();
    Sprite* CreateUISprite(const std::string& texturePath, const Vector2& position, const Vector2& size,
                           const Vector4& color);
    void SetNumberSprites(std::array<Sprite*, 2>& digits, int value, const Vector4& color, bool visible);

    // --- システムポインタ ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;

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
    float saveSelectOpenTime_ = 0.0f;
    float saveLaunchTime_ = 0.0f;
    bool saveLaunchTransitionRequested_ = false;
    float saveSelectionGlowY_ = 0.0f;
    std::array<float, 3> saveSlotSelectionWeights_{};

    // スプライトのポインタを保持しておく
    Sprite* titleTextSprite_ = nullptr;
    Sprite* startTextSprite_ = nullptr;
    Sprite* settingTextSprite_ = nullptr;
    Sprite* mainMenuStartFrameSprite_ = nullptr;
    Sprite* mainMenuSettingFrameSprite_ = nullptr;
    Sprite* mainMenuArrowSprite_ = nullptr;
    Sprite* mainMenuCursorSprite_ = nullptr;
    Sprite* mainMenuPromptSprite_ = nullptr;
    Vector2 startTextBaseSize_ = {0.0f, 0.0f};
    Vector2 settingTextBaseSize_ = {0.0f, 0.0f};
    Vector2 startTextBasePosition_ = {0.0f, 0.0f};
    Vector2 settingTextBasePosition_ = {0.0f, 0.0f};
    Vector2 mainMenuArrowBaseSize_ = {0.0f, 0.0f};
    Vector2 mainMenuCursorBaseSize_ = {0.0f, 0.0f};
    Vector2 mainMenuPromptBaseSize_ = {0.0f, 0.0f};
    Vector2 mainMenuArrowOffset_ = {0.0f, 0.0f};
    Vector2 mainMenuCursorOffset_ = {0.0f, 0.0f};
    Vector2 mainMenuPromptBasePosition_ = {0.0f, 0.0f};
    Object3d* titleHeroSlime_ = nullptr;
    std::unique_ptr<Object3d> titleCameraAnchor_ = nullptr;
    Vector3 titleHeroBasePosition_ = {0.0f, 0.0f, 0.0f};
    Vector3 titleHeroBaseScale_ = {1.0f, 1.0f, 1.0f};
    Vector3 titleHeroBaseRotation_ = {0.0f, 0.0f, 0.0f};
    bool titleHeroBaseCaptured_ = false;
    float titleHeroSequenceTime_ = 0.0f;
    // 文字 Sprite の借用参照と基準配置。導入アニメーションはこの基準へオフセットを加える。
    struct TitleLogoGlyph {
        Sprite* sprite = nullptr;
        Vector2 basePosition = {0.0f, 0.0f};
        Vector2 baseSize = {0.0f, 0.0f};
        float delay = 0.0f;
    };
    std::array<TitleLogoGlyph, 8> titleLogoGlyphs_{};
    float titleIntroTime_ = 0.0f;
    bool titleIntroComplete_ = false;
    std::vector<std::unique_ptr<Sprite>> titleUiSprites_;
    Sprite* saveSelectBackdrop_ = nullptr;
    Sprite* saveDetailPanel_ = nullptr;
    Sprite* saveDetailFileName_ = nullptr;
    Sprite* saveDetailEmptyLabel_ = nullptr;
    Sprite* saveLaunchLabel_ = nullptr;
    Sprite* saveSelectTitle_ = nullptr;
    Sprite* saveSelectionGlow_ = nullptr;
    Sprite* saveHintBackdrop_ = nullptr;
    Sprite* saveKeyboardHint_ = nullptr;
    Sprite* saveGamepadHint_ = nullptr;
    std::array<Sprite*, 3> saveSlotCards_ = {nullptr, nullptr, nullptr};
    // JSONで調整した枠サイズを保持し、選択時の拡大を毎フレーム累積させない。
    std::array<Vector2, 3> saveSlotCardBaseSizes_{};
    std::array<Vector2, 3> saveSlotCardBasePositions_{};
    std::array<Vector2, 3> saveSlotNumberBasePositions_{};
    std::array<Vector2, 3> saveSlotFileNameBasePositions_{};
    std::array<Vector2, 3> saveSlotStatusBasePositions_{};
    Vector2 saveDetailPanelBasePosition_{};
    Vector2 saveSelectTitleBasePosition_{};
    std::array<std::array<Sprite*, 4>, 3> saveSlotFrames_{};
    std::array<Sprite*, 3> saveSlotNumberSprites_ = {nullptr, nullptr, nullptr};
    std::array<Sprite*, 3> saveSlotFileNameTexts_ = {nullptr, nullptr, nullptr};
    std::array<Sprite*, 3> saveSlotStatusTexts_ = {nullptr, nullptr, nullptr};
    std::array<Sprite*, 3> saveSlotLifeIcons_ = {nullptr, nullptr, nullptr};
    std::array<Sprite*, 3> saveSlotLifeXIcons_ = {nullptr, nullptr, nullptr};
    std::array<std::array<Sprite*, 2>, 3> saveSlotLifeDigits_{};
    std::array<Sprite*, 3> saveSlotCrownIcons_ = {nullptr, nullptr, nullptr};
    std::array<Sprite*, 3> saveSlotCrownXIcons_ = {nullptr, nullptr, nullptr};
    std::array<std::array<Sprite*, 2>, 3> saveSlotCrownDigits_{};
    std::array<Sprite*, 3> saveSlotStarIcons_ = {nullptr, nullptr, nullptr};
    std::array<Sprite*, 3> saveSlotStarXIcons_ = {nullptr, nullptr, nullptr};
    std::array<std::array<Sprite*, 2>, 3> saveSlotStarDigits_{};
    std::array<Sprite*, 3> saveSlotPlayTimeLabels_ = {nullptr, nullptr, nullptr};
    std::array<std::array<Sprite*, 5>, 3> saveSlotPlayTimeDigits_{};
    Sprite* saveSelectHeader_ = nullptr;
    Sprite* saveDeleteButtonBack_ = nullptr;
    Sprite* saveDeleteButtonText_ = nullptr;
    Sprite* savePromptBubble_ = nullptr;
    Sprite* saveDeleteQuestionText_ = nullptr;
    Sprite* saveConfirmYesText_ = nullptr;
    Sprite* saveConfirmBackText_ = nullptr;
    std::unique_ptr<SettingsMenuOverlay> settingsOverlay_ = nullptr;
};
