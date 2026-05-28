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
#include "TimeAttackUI.h"
#include "ObjectManager.h"
#include "LevelLoader.h"
#include <GhostRecorder.h>
#include <GameRule.h>
#include <memory>
#include <string>
#include <vector>

class DirectXCommon;
class InputManager;

// クリア演出、リザルト表示、リトライ/タイトル選択を管理するシーン。
class GameClearScene : public BaseScene {
public:
    GameClearScene() = default;
    ~GameClearScene() override = default;

    // --- 基本サイクル ---
    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;

    // --- 描画 ---
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;
    void DrawImGui() override;

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
    void ResetVictoryPoseParticles();
    void UpdateVictoryPoseParticles(float deltaTime);
    void EmitVictoryParticle(const std::string& presetName, const Vector3& offset);
    void InitializeResultUiSprites();
    std::unique_ptr<Sprite> CreateUiSprite(const Vector2& position, const Vector2& size, const Vector4& color);
    void UpdateResultUiVisuals(float deltaTime);
    void PreviewNewBestEffect();

    struct ResultTextStrip {
        std::vector<std::unique_ptr<Sprite>> pieces;
        Vector2 basePosition = { 0.0f, 0.0f };
        Vector2 baseSize = { 0.0f, 0.0f };
        Vector2 sourceTextureSize = { 0.0f, 0.0f };
        int pieceCount = 0;
        float animationTimer = 0.0f;
        float stepDelay = 0.045f;
        float popDuration = 0.36f;
        bool initialized = false;
    };

    struct ResultGlyphStrip {
        std::vector<std::unique_ptr<Sprite>> glyphs;
        std::vector<Vector2> baseOffsets;
        std::vector<Vector2> baseSizes;
        Vector2 basePosition = { 0.0f, 0.0f };
        float animationTimer = 0.0f;
        float stepDelay = 0.10f;
        float popDuration = 0.48f;
        bool idleWaveEnabled = false;
        float idleWaveStartDelay = 1.0f;
        float idleWaveInterval = 2.35f;
        float idleWaveStepDelay = 0.08f;
        float idleWaveDuration = 0.34f;
        bool initialized = false;
    };

    void InitializeTextStrip(ResultTextStrip& strip, Sprite* sourceSprite, int pieceCount);
    void ResetTextStrip(ResultTextStrip& strip);
    void UpdateTextStrip(ResultTextStrip& strip, float deltaTime, const Vector4& color, float scale, float idleAmount);
    void DrawTextStrip(const ResultTextStrip& strip);
    void InitializeClearTitleGlyphStrip();
    void InitializePlayerTimeGlyphStrip();
    void InitializeBestTimeGlyphStrip();
    void ResetGlyphStrip(ResultGlyphStrip& strip);
    void UpdateGlyphStrip(ResultGlyphStrip& strip, float deltaTime, const Vector4& color, float scale);
    void DrawGlyphStrip(const ResultGlyphStrip& strip);

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

    // --- ライト・描画補助リソース ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    uint32_t gpuParticleTexHandle_ = 0;

    // --- リザルト表示 ---
    std::unique_ptr<TimeAttackUI> clearTimeUI_;
    std::unique_ptr<TimeAttackUI> bestTimeUI_;
    std::unique_ptr<TimeAttackUI> diffTimeUI_;
    std::unique_ptr<Sprite> resultPanelSprite_;
    std::unique_ptr<Sprite> resultPanelTopLineSprite_;
    std::unique_ptr<Sprite> resultPanelBottomLineSprite_;
    std::unique_ptr<Sprite> bestHighlightSprite_;
    std::unique_ptr<Sprite> bestHighlightTopLineSprite_;
    std::unique_ptr<Sprite> bestHighlightBottomLineSprite_;
    std::unique_ptr<Sprite> diffSignHorizontalSprite_;
    std::unique_ptr<Sprite> diffSignVerticalSprite_;
    ResultGlyphStrip clearTitleGlyphStrip_;
    ResultGlyphStrip playerTimeGlyphStrip_;
    ResultGlyphStrip bestTimeGlyphStrip_;

    // エディター配置のリザルトUIを名前で検索して保持する。
    Sprite* gameClearSprite_ = nullptr;
    Sprite* retryTextSprite_ = nullptr;
    Sprite* titleTextSprite_ = nullptr;
    Sprite* playerTimeSprite_ = nullptr;
    Sprite* bestTimeSprite_ = nullptr;
    Sprite* enterTextSprite_ = nullptr;
    bool clearUiUsesGamepad_ = false;
    bool hasAppliedClearInputUi_ = false;
    Vector2 enterTextBaseSize_ = { 220.0f, 36.0f };
    Vector2 enterTextBasePosition_ = { 1375.0f, 825.0f };

    // --- クリア演出フロー ---
    enum class ClearState {
        kRunIn,
        kVictoryMotion,
        kShowClearTime, // 今回のタイム表示＆ドラムロール
        kShowBestTime,  // ベストタイム表示＆ドラムロール
        kWaitInput,     // リザルト完了、入力待ち
        kShowMenu,
        kRunOut
    };
    ClearState clearState_ = ClearState::kVictoryMotion;
    float stateTimer_ = 0.0f;

    enum class MenuIndex { Retry, Title, Max };
    int currentMenuIndex_ = (int)MenuIndex::Retry;

    float resultAlpha_ = 0.0f;
    float clearTimeAlpha_ = 0.0f;
    float menuAlpha_ = 0.0f;
    float bestTimeAlpha_ = 0.0f;
    float resultPanelAlpha_ = 0.0f;
    float diffAlpha_ = 0.0f;
    float inputGuideAlpha_ = 0.0f;
    float newBestAlpha_ = 0.0f;
    float clearTimePopTimer_ = -1.0f;
    float bestTimePopTimer_ = -1.0f;
    float clearTimeValue_ = 0.0f;
    float bestTimeValue_ = 0.0f;
    float diffTimeValue_ = 0.0f;
    bool isNewBest_ = false;
    bool diffIsPositive_ = false;
    bool victoryParticleBurstEmitted_ = false;
    float victoryParticleTimer_ = 0.0f;
    // --- プレイヤーのクリア演出位置 ---
    Vector3 targetPlayerPos_;
    Vector3 targetPlayerRot_;
};
