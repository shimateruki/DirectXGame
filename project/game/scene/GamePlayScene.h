#pragma once
#include "BaseScene.h" 
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "AudioPlayer.h"
#include "ParticleSystem.h" 
#include "ParticleCommon.h" 
#include "SpriteDebugEditor.h"
#include "Player.h"
#include "Text.h"
#include "Event.h"
#include "BulletManager.h"
#include "Camera.h"
#include "MeshRenderer.h"
#include "game/ui/PauseMenuOverlay.h"
#include "game/ui/SettingsMenuOverlay.h"

#include "ObjectManager.h"
#include "DebugEditor.h" 
#include <GhostRecorder.h>

#include <memory>
#include <array>
#include <vector>
#include <Skybox.h>

// --- 前方宣言 ---
class DirectXCommon;
class InputManager;
class SceneManager;
class LevelLoader;
class LockOnSystem;
class GameRule;
class BossCore;
struct StageData;


/// <summary>
/// ゲームプレイシーン
/// </summary>
class GamePlayScene : public BaseScene {
public:
    GamePlayScene();
    ~GamePlayScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void UpdateUI(float deltaTime);
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

    // --- IEditableの実装 ---
    std::string GetName() override { return "Game Play Scene"; }
    void DrawImGui() override;

    // --- ムービーイベント ---
    void StartBridgeDropMovie();

    // --- BaseScene インターフェース実装 ---

    // オブジェクト管理は ObjectManager に委譲
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objectManager_->GetObjects(); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }

    // スプライトはシーンで保持 (ObjectManagerを拡張すれば移動可能)
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }

    // 各種コモンクラス
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    // プレイヤー連携
    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

    // ゴール判定
    void SetIsGoal(bool isGoal) { isGoal_ = isGoal; }
    bool IsGoal() const { return isGoal_; }

    // スターコイン
    void CollectStarCoin(int coinIndex);
    void CollectStarCoin(int coinIndex, const Vector3& worldPosition);
    void StartLifeLostPresentation(int beforeLives, int afterLives);
    bool IsLifeLostPresentationFinished() const { return lifeLostPresentationFinished_; }
    void HideLifeLostPresentationOverlay();

private:
    enum class MovieState {
        kNone,
        kBridgeDrop
    };
    MovieState movieState_ = MovieState::kNone;
    float movieTimer_ = 0.0f;
    Vector3 movieStartCameraEye_;
    Vector3 movieStartCameraTarget_;
    bool hasBridgeDropped_ = false;

private:
    // --- エンジンシステムへのポインタ ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    // --- サブシステム (機能を委譲するクラスたち) ---
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;   // 配置読み込み
    std::unique_ptr<LockOnSystem> lockOnSystem_ = nullptr; // ロックオン管理
    std::unique_ptr<ObjectManager> objectManager_ = nullptr; //  オブジェクト管理 

    // --- ゲームオブジェクト共通基盤 ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    std::unique_ptr<Text>  debugText_;
    std::unique_ptr<GameRule> gameRule_;

    Player* player_ = nullptr;

    // --- BGM・SE ---
    uint32_t bgmHandle_ = 0;
    bool isBGMPlaying_ = false;
    uint32_t particleSEHandle_ = 0;

    // ライト
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    MeshRenderer::PointLight* pointLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    MeshRenderer::SpotLight* spotLightData_ = nullptr;
    uint32_t gpuParticleTexHandle_ = 0;
    std::unique_ptr<Sprite> lockOnSprite_;
    bool isDrawLockOn_ = false; // 描画するかどうかのスイッチ
    std::unique_ptr<Skybox> skybox_;
    uint32_t skyboxTextureHandle_ = 0;

    // アニメーションモデルのテスト用変数
    std::unique_ptr<Object3d> animatedCube_;

    bool isGoal_ = false;
    bool sessionStarCoins_[3] = { false, false, false };

    struct HudSpriteState {
        Sprite* sprite = nullptr;
        Vector2 basePosition = { 0.0f, 0.0f };
        Vector2 baseSize = { 0.0f, 0.0f };
        Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    HudSpriteState hudLifeMeter_;
    HudSpriteState hudLifeMeterDigit_;
    HudSpriteState hudLifeIcon_;
    HudSpriteState hudLifeXIcon_;
    std::array<HudSpriteState, 2> hudLifeDigits_;
    HudSpriteState hudCoinIcon_;
    HudSpriteState hudCoinXIcon_;
    std::array<HudSpriteState, 2> hudCoinDigits_;
    std::array<HudSpriteState, 3> hudStageStarSlots_;
    std::array<float, 3> hudStageStarPulseTimers_ = { 0.0f, 0.0f, 0.0f };
    std::array<bool, 3> hudStageStarVisualCollected_ = { false, false, false };
    struct StageStarUIFlyParticle {
        std::unique_ptr<Sprite> sprite;
        Vector2 start = { 0.0f, 0.0f };
        Vector2 control = { 0.0f, 0.0f };
        Vector2 end = { 0.0f, 0.0f };
        float timer = 0.0f;
        float duration = 0.6f;
        float baseSize = 20.0f;
        float rotationSpeed = 0.0f;
        int starIndex = -1;
        bool fillsSlot = false;
    };
    std::vector<StageStarUIFlyParticle> hudStageStarFlyParticles_;
    float hudLifeGainPulseTimer_ = 0.0f;
    float hudCoinPulseTimer_ = 0.0f;
    int hudPreviousLives_ = 0;
    int hudPreviousCoins_ = 0;
    float hudPreviousHp_ = 0.0f;
    float hudDamagePulseTimer_ = 0.0f;
    int hudDisplayedLife_ = 6;
    bool lifeLostPresentationActive_ = false;
    bool lifeLostPresentationFinished_ = true;
    bool lifeLostBlackHold_ = false;
    bool lifeLostNumberDropped_ = false;
    float lifeLostPresentationTimer_ = 0.0f;
    int lifeLostBeforeLives_ = 0;
    int lifeLostAfterLives_ = 0;
    Vector2 lifeLostIrisCenter_ = { 0.5f, 0.5f };
    HudSpriteState lifeLostIcon_;
    HudSpriteState lifeLostXIcon_;
    std::array<HudSpriteState, 2> lifeLostDigits_;
    HudSpriteState lifeLostBackdrop_;
    std::unique_ptr<Camera> lifeLostCamera_;
    std::unique_ptr<Object3d> lifeLostSlimeObject_;
    std::unique_ptr<Object3d> lifeLostStunObject_;
    bool lifeLostRevive_ = false;
    std::unique_ptr<PauseMenuOverlay> pauseMenuOverlay_;
    std::unique_ptr<SettingsMenuOverlay> settingsOverlay_;

    // 初期化・終了処理
    void InitializeGameplayHUD();
    void InitializeCoreSystems(const StageData& currentStage);
    void InitializeRenderCommons();
    void InitializeGameplaySystems();
    void LoadCurrentStageContent(const StageData& currentStage);
    void StartRespawnIrisInIfNeeded();
    void InitializeDebugAnimationPreview();
    void FinalizeGameplayResources();

    // フレーム更新
    bool HandleGoalClear(float& deltaTime);
    void UpdatePostEffectState(float deltaTime);
    void UpdateLockOnAndCamera(float deltaTime);
    void UpdateSceneSystems(float deltaTime);
    void UpdateEffectDebugShortcuts();
    void UpdateGameplayHUD(float deltaTime);
    void UpdateStageStarHUD(float deltaTime, bool visible);
    void UpdateLifeLostPresentation(float deltaTime);
    bool HandlePauseOverlay(float deltaTime);
    bool IsPauseOpenTriggered() const;

    // UI描画
    void DrawGameplayHUD();
    void DrawStageStarHUD();
    void DrawLifeLostPresentation();
    void InitializeLifeLostPresentationObjects();
    void UpdateLifeLostPresentationWorld(float deltaTime);
    void DrawLifeLostPresentationWorld(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    HudSpriteState BindGameplayHUDSprite(const std::string& name, const std::string& texturePath, const Vector2& position, const Vector2& size, const Vector2& anchor, const Vector4& color);
    void DrawGameplayHUDSprite(const HudSpriteState& state);
    bool IsGameplayHUDSprite(const Sprite* sprite) const;
    void SetGameplayHUDNumber(std::array<HudSpriteState, 2>& digits, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color, bool visible);
    void StartStageStarHUDCollectEffect(int starIndex, const Vector3& worldPosition);
    Vector2 ProjectWorldToScreen(const Vector3& worldPosition) const;

    // フラスタムカリング判定
    bool IsVisible(Object3d* obj);
};
