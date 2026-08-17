#pragma once
#include "BaseScene.h"
#include "AudioPlayer.h"
#include "BulletManager.h"
#include "Camera.h"
#include "CinematicPlayer.h"
#include "CinematicSequence.h"
#include "DebugEditor.h"
#include "Event.h"
#include "GoalClearPlayerAnimator.h"
#include "MeshRenderer.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ObjectManager.h"
#include "ParticleCommon.h"
#include "ParticleSystem.h"
#include "Player.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteDebugEditor.h"
#include "Text.h"
#include "game/ui/ControlsGuideOverlay.h"
#include "game/ui/PauseMenuOverlay.h"
#include "game/ui/SaveIndicatorOverlay.h"
#include "SceneController.h"
#include "game/ui/SettingsMenuOverlay.h"
#include <GhostRecorder.h>
#include <Skybox.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

// 前方宣言
class DirectXCommon;
class InputManager;
class SceneManager;
class LevelLoader;
class LockOnSystem;
class GameRule;
class BossCore;
struct StageData;

/// <summary>
/// ゲームプレイ本編のシーン。ステージ読み込み、プレイヤー、HUD、ゴール演出を統合する。
/// </summary>
class GamePlayScene : public BaseScene {
public:
    GamePlayScene();
    ~GamePlayScene() override;

    void Initialize() override;
    void BeginLoadingInitialize() override;
    bool InitializeLoadingStep() override;
    float GetLoadingInitializeProgress() const override;
    void OnActivated() override;
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void UpdateUI(float deltaTime);
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

    // --- IEditable ---
    std::string GetName() override { return "Game Play Scene"; }
    void DrawImGui() override;

    // --- ムービーイベント ---
    void StartBridgeDropMovie();

    // --- BaseScene インターフェース ---
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objectManager_->GetObjects(); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }

    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }
    void CollectReplaySprites(std::vector<Sprite*>& sprites) override;
    void CaptureReplaySceneState(json& state) const override;
    void RestoreReplaySceneState(const json& state) override;

    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    Skybox* GetSkybox() override { return skybox_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }
    GameRule* GetGameRule() override { return gameRule_.get(); }

    // ゴール判定とステージクリア演出
    void SetIsGoal(bool isGoal);
    void StartGoalPresentation(Object3d* crownObject);
    bool IsGoal() const { return isGoal_; }

    // スターコインとライフ減少演出
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

    // --- シーン内部システム ---
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<LockOnSystem> lockOnSystem_ = nullptr;
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;

    // --- 描画共通基盤 ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    std::unique_ptr<Text> debugText_;
    std::unique_ptr<GameRule> gameRule_;
    std::unique_ptr<ISceneController> sceneController_;

    Player* player_ = nullptr;

    // --- BGM / SE ---
    uint32_t bgmHandle_ = 0;
    bool isBGMPlaying_ = false;
    uint32_t particleSEHandle_ = 0;

    // --- ライト ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    MeshRenderer::PointLight* pointLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    MeshRenderer::SpotLight* spotLightData_ = nullptr;
    uint32_t gpuParticleTexHandle_ = 0;
    std::unique_ptr<Sprite> lockOnSprite_;
    bool isDrawLockOn_ = false;
    std::unique_ptr<Skybox> skybox_;
    uint32_t skyboxTextureHandle_ = 0;

    std::unique_ptr<Object3d> animatedCube_;

    // --- ゴール/クリア演出 ---
    struct GoalPresentationTuning {
        float crownFocusEndTime = 0.60f;
        float crownMoveStartTime = 0.88f;
        float crownDropHeight = 2.00f;
        float crownSeatDepth = 0.10f;
        float crownFocusDistance = 5.20f;
        float crownFocusSide = 0.85f;
        float crownFocusHeight = 1.05f;
        float landingCameraDistance = 7.00f;
        float landingCameraSide = 1.00f;
        float landingCameraHeight = 1.60f;
        float jumpCameraDistance = 8.50f;
        float jumpCameraSide = 0.75f;
        float jumpCameraHeight = 2.00f;
        float resultCameraDistance = 9.00f;
        float resultCameraSide = 0.30f;
        float resultCameraHeight = 1.40f;
        float resultTargetSide = 2.00f;
        float crownFocusFov = 0.50f;
        float landingFov = 0.58f;
        float jumpFov = 0.68f;
        float resultFov = 0.64f;
        float resultUiCenterX = 0.50f;
        float resultUiCenterY = 0.72f;
        float resultUiScale = 1.00f;
        float resultBackdropAlpha = 0.30f;
        float resultGlowAlpha = 0.18f;
    };

    bool isGoal_ = false;
    bool goalSavePerformed_ = false;
    bool sessionStarCoins_[3] = { false, false, false };

    // --- ステージ開始時のゲート登場演出 ---
    bool stageEntryPresentationActive_ = false;
    bool stageEntryPlayerEmergenceStarted_ = false;
    bool stageEntryHadPlayerControl_ = true;
    bool stageEntryCinemaBarOverrideActive_ = false;
    float stageEntryPresentationTimer_ = 0.0f;
    float stageEntryCinemaBarBaseHeight_ = 0.0f;
    Object3d* stageEntryGate_ = nullptr;
    Vector3 stageEntryDirection_ = { 1.0f, 0.0f, 0.0f };
    Vector3 stageEntryCameraStartEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 stageEntryCameraStartTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 stageEntryCameraFocusEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 stageEntryCameraFocusTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 stageEntryCameraRestoreEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 stageEntryCameraRestoreTarget_ = { 0.0f, 0.0f, 0.0f };

    enum class GoalPresentationState {
        Inactive,
        Celebrating,
        ReadyToReturn,
        Returning
    };
    GoalPresentationState goalPresentationState_ = GoalPresentationState::Inactive;
    float goalPresentationTimer_ = 0.0f;
    float goalStarEmitTimer_ = 0.0f;
    float goalBurstEmitTimer_ = 0.0f;
    float goalCrownIdleTime_ = 0.0f;
    float goalCrownSparkleTimer_ = 0.0f;
    size_t goalCrownSparklePatternIndex_ = 0;
    Vector3 goalCrownPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalPlayerBasePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalPlayerBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 goalPlayerBaseRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalPlayerPosePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalMoveForward_ = { 0.0f, 0.0f, 1.0f };
    Vector3 goalMoveRight_ = { 1.0f, 0.0f, 0.0f };
    Vector3 goalCrownBasePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCrownBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 goalCrownBaseRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCrownSpringPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCrownSpringVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCrownSpringRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCrownSpringRotationVelocity_ = { 0.0f, 0.0f, 0.0f };
    bool goalCrownSpringInitialized_ = false;
    Vector3 goalCameraGameplayEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCameraGameplayTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCameraFocusEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCameraFocusTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCameraLandingEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCameraLandingTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCameraJumpEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCameraJumpTarget_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCameraResultEye_ = { 0.0f, 0.0f, 0.0f };
    Vector3 goalCameraResultTarget_ = { 0.0f, 0.0f, 0.0f };
    float goalCameraGameplayFov_ = 0.45f;
    bool goalSavedPlayerControlActive_ = true;
    bool goalPlayerSnapshotValid_ = false;
    bool goalCrownSnapshotValid_ = false;
    bool goalCameraSnapshotValid_ = false;
    bool goalReturnFadeStarted_ = false;
    bool goalLandingCuePlayed_ = false;
    bool goalResultCuePlayed_ = false;
    bool goalEditorPreviewMode_ = false;
    Object3d* goalCrownObject_ = nullptr;
    Camera* goalLockedPrimaryCamera_ = nullptr;
    Camera* goalLockedSecondaryCamera_ = nullptr;
    GoalClearPlayerAnimator goalClearPlayerAnimator_;
    CinematicSequence goalCinematicSequence_;
    CinematicPlayer goalCinematicPlayer_;
    bool goalCinematicTimelineLoaded_ = false;
    GoalPresentationTuning goalPresentationTuning_;
    std::unique_ptr<Camera> goalPresentationCamera_;
    std::unique_ptr<Sprite> goalOverlayBackdrop_;
    std::unique_ptr<Sprite> goalOverlayFlash_;
    std::unique_ptr<Sprite> goalOverlayGlow_;
    std::unique_ptr<Sprite> goalOverlayTopLine_;
    std::unique_ptr<Sprite> goalOverlayBottomLine_;
    std::unique_ptr<Sprite> goalOverlayStageClearText_;
    std::unique_ptr<Sprite> goalOverlayReturnText_;
    std::array<std::unique_ptr<Sprite>, 8> goalOverlaySparkles_;

    // HUD の基準値。演出で拡縮しても戻せるよう保存する。
    struct HudSpriteState {
        Sprite* sprite = nullptr;
        Vector2 basePosition = { 0.0f, 0.0f };
        Vector2 baseSize = { 0.0f, 0.0f };
        Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // --- プレイ中 HUD ---
    HudSpriteState hudHpIcon_;
    HudSpriteState hudHpFrame_;
    HudSpriteState hudHpDamageFill_;
    HudSpriteState hudHpFill_;
    HudSpriteState hudHpHighlight_;
    HudSpriteState hudMorphGaugeBack_;
    HudSpriteState hudMorphGaugeFill_;
    HudSpriteState hudMorphGaugeIcon_;
    HudSpriteState hudMorphGaugeFrame_;
    HudSpriteState hudLifeIcon_;
    HudSpriteState hudLifeXIcon_;
    std::array<HudSpriteState, 2> hudLifeDigits_;
    HudSpriteState hudCoinIcon_;
    HudSpriteState hudCoinXIcon_;
    std::array<HudSpriteState, 2> hudCoinDigits_;
    std::array<HudSpriteState, 3> hudStageStarSlots_;
    std::array<float, 3> hudStageStarPulseTimers_ = { 0.0f, 0.0f, 0.0f };
    std::array<bool, 3> hudStageStarVisualCollected_ = { false, false, false };

    // スターコイン取得時、ワールド位置から HUD へ飛んでいく粒子
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
    float hudHurtIconTimer_ = 0.0f;
    float hudHpDamageHoldTimer_ = 0.0f;
    float hudHpDelayedRate_ = 1.0f;
    float hudHpAnimationTimer_ = 0.0f;
    float hudMorphGaugeTimer_ = 0.0f;
    float hudMorphGaugeVisibleTimer_ = 0.0f;

    // --- ライフ減少演出 ---
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

    // --- オーバーレイ ---
    std::unique_ptr<ControlsGuideOverlay> controlsGuideOverlay_;
    std::unique_ptr<PauseMenuOverlay> pauseMenuOverlay_;
    std::unique_ptr<SettingsMenuOverlay> settingsOverlay_;
    std::unique_ptr<SaveIndicatorOverlay> saveIndicatorOverlay_;

    // 初期化/終了処理
    void InitializeGameplayHUD();
    void InitializeCoreSystems(const StageData& currentStage);
    void InitializeRenderCommons();
    void InitializeGameplaySystems();
    void InitializeGameplayObjectSystems();
    void InitializeGameplayParticleRuntime();
    void InitializeGameplayDebrisRuntime();
    void InitializeGameplaySkybox();
    void ApplyGameplayRenderState(const StageData& currentStage);
    void LoadCurrentStageContent(const StageData& currentStage);
    void LoadCurrentStageObjects(const StageData& currentStage);
    void LoadCurrentStageSpritesAndView(const StageData& currentStage);
    void InitializeGameplayOverlays();
    void ApplyGoalCrownState();
    void ApplyStageStarCoinState();
    void UpdateGoalCrownIdleAnimation(float deltaTime);
    bool StartRespawnIrisInIfNeeded();
    void StartStageEntryPresentation();
    void UpdateStageEntryPresentation(float deltaTime);
    void FinishStageEntryPresentation();
    Object3d* FindStageEntryGate() const;
    void InitializeDebugAnimationPreview();
    void FinalizeGameplayResources();

    int loadingInitializePhase_ = 0;
    size_t loadingInitializeItemIndex_ = 0;
    size_t loadingInitializeCompletedUnits_ = 0;
    size_t loadingInitializeTotalUnits_ = 1;

    // フレーム更新
    bool HandleGoalClear(float& deltaTime);
    void InitializeGoalPresentationOverlay();
    void InitializeGoalCinematicTimeline();
    void ApplyGoalCinematicTimingFromSequence();
    void SyncGoalCinematicTimelineFromTuning();
    void HandleGoalCinematicAnimation(
        Object3d* target,
        const CinematicAnimationClipData& clip,
        float localTime,
        bool isPreview);
    void HandleGoalCinematicSignal(
        Object3d* target,
        const CinematicSignalMarker& signal,
        bool isPreview);
    void LoadGoalPresentationTuning();
    void SaveGoalPresentationTuning() const;
    void SanitizeGoalPresentationTuning();
    void DrawGoalPresentationEditor();
    Object3d* FindGoalCrownObject() const;
    void StartGoalPresentationPreview();
    void StopGoalPresentationPreview();
    void UpdateGoalPresentation(float deltaTime);
    void UpdateGoalPlayerCelebration(float deltaTime);
    void UpdateGoalCrownMotion(float deltaTime);
    void EmitGoalPresentationEffects(float deltaTime);
    void SetupGoalPresentationCamera();
    void UpdateGoalPresentationCamera();
    void LockGoalPresentationCameraInput();
    void RestoreGoalPresentationCameraInput();
    void UpdateGoalPresentationOverlay();
    void DrawGoalPresentationOverlay();
    void RequestGoalReturnToSelect();
    void UpdatePostEffectState(float deltaTime);
    void UpdateLockOnAndCamera(float deltaTime);
    void UpdateLockOnSprite(Camera* camera, Object3d* target, float deltaTime);
    void UpdateSceneSystems(float deltaTime);
    void UpdateEffectDebugShortcuts();
    void UpdateGameplayHUD(float deltaTime);
    void UpdateStageStarHUD(float deltaTime, bool visible);
    void UpdateLifeLostPresentation(float deltaTime);
    bool HandleControlsGuideOverlay(float deltaTime);
    bool IsControlsGuideOpenTriggered() const;
    bool HandlePauseOverlay(float deltaTime);
    bool IsPauseOpenTriggered() const;

    // UI 描画
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

    // 視錐台カリング判定
    bool IsVisible(Object3d* obj);
};
