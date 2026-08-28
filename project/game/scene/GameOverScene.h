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
#include "GameOverSlimeAnimator.h"

#include "ObjectManager.h"
#include "LevelLoader.h"
#include <GhostRecorder.h>
#include <GameRule.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

// --- 前方宣言 ---
class DirectXCommon;
class InputManager;

/// <summary>
/// ゲームオーバーシーン
/// </summary>
class GameOverScene : public BaseScene {
public:
    GameOverScene() = default;
    ~GameOverScene() override = default;

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize() override;
    void OnActivated() override;
    SceneLoadManifest BuildAsyncLoadManifest() const override;

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize() override;

    /// <summary>
    /// 更新
    /// </summary>
    void Update(float deltaTime) override;

    /// <summary>
    /// 描画
    /// </summary>
    void Draw() override;
    void DrawUI() override;
    void DrawImGui() override;

    //  シャドウマップ描画のオーバーライド
    void DrawShadow() override;

    // --- BaseScene インターフェース実装 (ObjectManagerへ委譲) ---
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objectManager_->GetObjects(); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }

    // 各種マネージャ・コモンの取得
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    // プレイヤー連携
    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

private:
    enum class MenuItem {
        Retry,
        Title,
        Count
    };

    struct MenuRow {
        Sprite* backdrop = nullptr;
        Sprite* label = nullptr;
        Vector2 backdropBaseSize = { 0.0f, 0.0f };
        Vector2 labelBaseSize = { 0.0f, 0.0f };
    };

    struct FallingCrown {
        Sprite* sprite = nullptr;
        Vector2 basePosition = { 0.0f, 0.0f };
        Vector2 size = { 0.0f, 0.0f };
        float timeOffset = 0.0f;
        float cycleDuration = 1.0f;
        float fallDistance = 0.0f;
        float driftAmplitude = 0.0f;
        float driftSpeed = 0.0f;
        float rotationSpeed = 0.0f;
        float phase = 0.0f;
        float alpha = 1.0f;
    };

    struct DizzyStar {
        Sprite* sprite = nullptr;
        float baseAngle = 0.0f;
        float size = 0.0f;
        float depthOffset = 0.0f;
    };

    struct PresentationTuning {
        float cameraSettleTime = 1.55f;
        float titleRevealStartTime = 1.20f;
        Vector3 introEyeOffset = { 0.75f, 1.28f, -5.10f };
        Vector3 impactEyeOffset = { 0.34f, 1.04f, -4.25f };
        Vector3 settleEyeOffset = { 1.45f, 1.62f, -8.10f };
        Vector3 introTargetOffset = { 0.0f, 0.50f, 0.0f };
        Vector3 settleTargetOffset = { -1.35f, 0.40f, 0.0f };
        float introFov = 0.50f;
        float impactFov = 0.43f;
        float settleFov = 0.58f;
        float impactShake = 0.085f;
    };

    void BindLayoutSprites();
    void RefreshLayoutSpritePointers();
    Sprite* FindSprite(const std::string& name) const;
    void InitializeFallingCrowns();
    void UpdateFallingCrowns(float deltaTime);
    void InitializeDizzyStars();
    void UpdateDizzyStars(float deltaTime);
    void StartRetryExit();
    void UpdateRetryExit(float deltaTime);
    void UpdateMenuInput();
    void UpdateMenuSprites(float deltaTime);
    void ChangeSelection(int direction);
    void ConfirmSelection();
    bool IsTitleRevealComplete() const;
    void InitializeGameOverPresentation();
    void FindGameOverSlimeObject();
    void UpdateGameOverPresentation(float deltaTime);
    void UpdateGameOverCamera();
    void UpdateGameOverPostEffects();
    void EmitGameOverImpactCue();
    void ResetGameOverPostEffects();
    void DrawBackgroundSprite();

    // --- エンジン基盤 ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    // --- サブシステム (機能を委譲するクラスたち) ---
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    // --- 共通基盤クラス ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    // --- オブジェクト・リソース ---
    std::vector<std::unique_ptr<Sprite>> sprites_;
    Object3d* gameOverSlimeObject_ = nullptr;
    GameOverSlimeAnimator gameOverSlimeAnimator_;
    Vector3 gameOverSlimeBasePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 gameOverSlimeBaseScale_ = { 1.0f, 1.0f, 1.0f };
    float gameOverPresentationTimer_ = 0.0f;
    PresentationTuning presentationTuning_;
    bool gameOverImpactCuePlayed_ = false;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    Player* player_ = nullptr;

    // --- BGM・オーディオ ---
    uint32_t bgmHandle_ = 0;

    // --- ライト ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;

    //  GPUパーティクル用画像ハンドル
    uint32_t gpuParticleTexHandle_ = 0;

    Sprite* backgroundSprite_ = nullptr;
    std::array<Sprite*, 8> titleLetters_ = {};
    std::array<Vector2, 8> titleLetterBasePositions_ = {};
    std::array<Vector2, 8> titleLetterBaseSizes_ = {};
    std::array<MenuRow, static_cast<size_t>(MenuItem::Count)> menuRows_ = {};
    std::vector<FallingCrown> fallingCrowns_;
    std::vector<DizzyStar> dizzyStars_;
    int selectedIndex_ = 0;
    float sceneTime_ = 0.0f;
    float titleRevealTimer_ = 0.0f;
    bool retryExitActive_ = false;
    bool retrySceneChangeRequested_ = false;
    float retryExitTimer_ = 0.0f;
};
