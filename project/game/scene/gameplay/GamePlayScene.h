#pragma once
#include "LifeLostPresentationState.h"
#include "BaseScene.h"
#include "game/system/SceneGameplayAccess.h"
#include "game/system/IStageProgressScene.h"
#include "AudioPlayer.h"
#include "BulletManager.h"
#include "Camera.h"
#include "CinematicPlayer.h"
#include "CinematicSequence.h"
#include "DebugEditor.h"
#include "Event.h"
#include "game/scene/gameplay/GoalClearPlayerAnimator.h"
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
#include "game/ui/CopyStyleHud.h"
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
class BaseEnemy;
struct StageData;

/// <summary>
/// ゲームプレイ本編のシーン。ステージ読み込み、プレイヤー、HUD、ゴール演出を統合する。
/// </summary>
class GamePlayScene : public BaseScene, public IStageProgressScene, public ISceneGameplayAccess {
  public:
    GamePlayScene();
    ~GamePlayScene() override;

    // 非同期読み込みと通常起動は同じ初期化処理を段階的に共有します。
    void Initialize() override;
    void BeginLoadingInitialize() override;
    bool InitializeLoadingStep() override;
    float GetLoadingInitializeProgress() const override;

    void OnActivated() override;
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    void Finalize() override;

    // ワールドの更新と、操作可能なUIの更新を分けています。
    void Update(float deltaTime) override;
    void UpdateUI(float deltaTime);

    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

    // --- IEditable ---
    std::string GetName() override { return "Game Play Scene"; }
    void DrawImGui() override;

    // ステージ内のカメラ・ボス演出を開始します。
    void StartBridgeDropMovie();

    /// <summary>ボスとゲートの登場演出を開始します。</summary>
    /// <param name="bossObject">登場させるボスの描画物。</param>
    /// <param name="gateObject">登場演出に使うゲート。</param>
    /// <param name="bossRevealDelay">演出開始からボスを見せるまでの秒数。</param>
    void StartArenaBossIntro(Object3d* bossObject, Object3d* gateObject, float bossRevealDelay);
    void StartArenaBossDefeatReward(Object3d* rewardObject);

    /// <summary>最終ボスの王冠演出を開始します。</summary>
    /// <returns>演出を開始できた場合は true。</returns>
    bool StartCrownFinale(class EnemyFalseKingSlime* boss);
    // 編集中の複製ステージで、本番と同じムービーを固定刻みで評価します。
    bool BeginCrownMoviePreview(bool defeat = false);
    bool StartCrownDefeat(class EnemyFalseKingSlime* boss);
    void EndCrownMoviePreview();
    void RewindCrownMoviePreview();
    void StepCrownMoviePreview(float deltaTime);
    bool IsCrownMoviePreviewActive() const { return crownMoviePreview_.active; }
    // 結晶破壊の猶予が切れた後だけ、第二形態の全域槍を映像として見せます。
    bool StartSealBarrageMovie(class EnemyFalseKingSlime* boss, const Vector3& strike);

    // --- BaseScene インターフェース ---
    const std::vector<std::unique_ptr<Object3d>>& GetObjects() const override { return objectManager_->GetObjects(); }
    Object3d* AddObjectImmediate(std::unique_ptr<Object3d> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }
    std::unique_ptr<Object3d> TakeObjectImmediate(Object3d* object) override { return objectManager_->TakeObjectImmediate(object); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }
    std::unique_ptr<Object3d> CreateReplayObject(const json& descriptor) override;
    void OnReplayObjectsRecreated() override;

    const std::vector<std::unique_ptr<Sprite>>& GetSprites() const override { return sprites_; }
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

    // ゴール判定とステージクリア演出。
    void SetIsGoal(bool isGoal);
    void StartGoalPresentation(Object3d* crownObject);
    bool IsGoal() const { return isGoal_; }
    bool TryStartGoalFromContact(Object3d* actor, Object3d* goal) override;
    void CollectStarCoinFromContact(int coinIndex, const Vector3& worldPosition) override;

    // スターコイン取得と、残機減少時の一時演出。
    void CollectStarCoin(int coinIndex);
    void CollectStarCoin(int coinIndex, const Vector3& worldPosition);

    /// <summary>減少前後の残機数を使って、ライフ減少演出を開始します。</summary>
    /// <param name="beforeLives">減少前の残機数。</param>
    /// <param name="afterLives">減少後の残機数。</param>
    void StartLifeLostPresentation(int beforeLives, int afterLives);
    bool IsSealBarrageMovieActive() const { return sealBarrageMovie_.active; }
    bool IsLifeLostPresentationFinished() const { return lifeLostState_.IsFinished(); }
    void HideLifeLostPresentationOverlay();

  private:
    std::vector<std::unique_ptr<Sprite>>* GetMutableSprites() override { return &sprites_; }
    enum class MovieState { kNone, kBridgeDrop };
    MovieState movieState_ = MovieState::kNone;
    float movieTimer_ = 0.0f;
    Vector3 movieStartCameraEye_;
    Vector3 movieStartCameraTarget_;
    bool hasBridgeDropped_ = false;

  private:
    // --- エンジンシステムへのポインタ ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;

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

    // 最終演出で変更したオブジェクトの状態を、終了時の復元用に保持します。
    struct CrownFinaleObjectPose {
        Object3d* object = nullptr;
        Vector3 position{};
        Vector3 rotation{};
        uint32_t attribute = 0;
        uint32_t mask = 0;
        bool visible = true;
    };

    // 最終演出の進行状態と、演出中だけ使用するオブジェクトをまとめます。
    struct CrownFinaleState {
        bool active = false;
        bool prepared = false;
        bool granted = false;
        bool hadControl = true;
        bool impacted = false;
        bool fragmentBroken = false;
        bool fragmentLanded = false;
        bool pullStarted = false;
        bool bodyFormed = false;
        bool crownStarted = false;
        bool wingsStarted = false;
        std::string playerModel;
        Vector4 playerColor{};
        int playerMaterial = 25;
        bool launched = false;
        bool landed = false;
        float playerYaw = 0.0f;
        float time = 0.0f;
        float duration = 9.0f;
        float fov = 0.45f;
        float bars = 0.0f;
        Vector3 start{};
        Vector3 playerScale{};
        Vector3 bossStart{};
        Vector3 offset{};
        Vector3 eye{};
        Vector3 target{};
        Vector3 bossScale{};
        Vector3 bossRotation{};
        Vector3 fragmentStart{};
        Vector3 lastSealPosition{};
        Vector3 fragmentGround{};
        std::string bossModel;
        class EnemyFalseKingSlime* boss = nullptr;
        std::vector<CrownFinaleObjectPose> originalObjects;
        std::vector<CrownFinaleObjectPose> fractureChunks;
        std::vector<std::unique_ptr<Object3d>> floorCracks;
        std::vector<Object3d*> generatedObjects;
        std::unique_ptr<Object3d> fragment;
        std::unique_ptr<Object3d> fragmentCrack;
        std::unique_ptr<Object3d> lastSeal;
        std::vector<std::unique_ptr<Object3d>> fragmentChips;
        std::unique_ptr<Object3d> shockwave;
        std::unique_ptr<Object3d> wings;
        std::unique_ptr<Object3d> rightWing;
        std::unique_ptr<Object3d> innerRune;
        std::unique_ptr<Object3d> goldBody;
        std::unique_ptr<Object3d> regalia;
        std::unique_ptr<Object3d> groundFractures;
    } crownFinale_;

    // 元の配置・能力へ触れず、プレビュー複製の寿命と元のカメラを保持します。
    struct CrownDefeatPart {
        std::unique_ptr<Object3d> visual;
        Vector3 pivot{};
        bool gem = false;
    };
    // 撃破中は元のボスを残し、分割モデルだけを動かして寿命と撃破通知を分離します。
    struct CrownDefeatState {
        bool active=false, completed=false, bossVisible=true, hadControl=true;
        float time=0, bars=0, playerYaw=0;
        class EnemyFalseKingSlime* boss=nullptr;
        Vector3 bossPosition{},bossScale{},bossRotation{},playerPosition{},playerScale{},playerRotation{};
        Vector3 eye{},target{},forward{},right{};
        Camera* previousCamera=nullptr;
        std::unique_ptr<Camera> camera;
        std::unique_ptr<Object3d> body,cracks,ring;
        std::vector<CrownDefeatPart> parts;
        std::vector<std::unique_ptr<Object3d>> rays;
    } crownDefeat_;
    void UpdateCrownDefeat(float deltaTime);
    void FinishCrownDefeat();
    void ResetCrownDefeat();
    void DrawCrownDefeatWorld();

    // 王冠演出プレビューの一時カメラを所有し、元の Player・カメラ・表示対象は復元用に借用する。
    struct CrownMoviePreviewState {
        bool defeat = false;
        bool active = false;
        Player* originalPlayer = nullptr;
        std::unique_ptr<Camera> camera;
        Camera* previousCameraOverride = nullptr;
        std::vector<Object3d*> hiddenObjects;
        std::vector<Object3d*> objects;
        Vector3 eye{}, target{}, rotation{};
        Camera::FollowMode followMode = Camera::FollowMode::kAimable;
        Object3d* follow = nullptr;
        float fov = 0.45f;
    } crownMoviePreview_;

    // 封印失敗演出の時計と、終了時に復元する操作権・視点・追従対象。
    struct SealBarrageMovieState {
        bool active = false;
        bool hadControl = true;
        float time = 0.0f;
        float fov = 0.7f;
        float bars = 0.0f;
        Vector3 eye{};
        Vector3 target{};
        Vector3 rotation{};
        Vector3 boss{};
        Vector3 strike{};
        Object3d* follow = nullptr;
        Object3d* lockOn = nullptr;
        Camera::FollowMode followMode = Camera::FollowMode::kAimable;
    } sealBarrageMovie_;

    void UpdateSealBarrageMovie(float deltaTime);
    void FinishSealBarrageMovie();

    std::array<std::unique_ptr<Sprite>, 6> crownFinaleMessages_;

    // 最終演出で一時的に変更したワールドの状態は Finish で復元します。
    void UpdateCrownFinale(float deltaTime);
    void FinishCrownFinale(bool completed);

    /// <summary>最終演出で変更したオブジェクトの姿勢と表示状態を戻します。</summary>
    void ResetCrownFinaleWorld();

    void DrawCrownFinaleWorld();
    void DrawCrownFinaleUI();

#ifdef USE_IMGUI
    enum class DebugTeleportDestination {
        None,
        PrismArena,
        GoalApproach,
    };

    // 能力呼び出しに必要な実敵インスタンスを、デバッグ変身中だけ所有します。
    std::unique_ptr<BaseEnemy> debugPlayerMorphSource_;
    // ImGui描画中にゲーム状態を直接変更せず、次の更新開始時に安全に処理します。
    DebugTeleportDestination pendingDebugTeleportDestination_ = DebugTeleportDestination::None;
#endif

    // --- BGM / SE ---
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
        float crownFocusEndTime = 0.72f;
        float crownMoveStartTime = 0.98f;
        float crownDropHeight = 2.00f;
        float crownSeatDepth = 0.10f;
        float crownFocusDistance = 5.80f;
        float crownFocusSide = 1.25f;
        float crownFocusHeight = 1.25f;
        float landingCameraDistance = 7.60f;
        float landingCameraSide = 1.60f;
        float landingCameraHeight = 1.90f;
        float jumpCameraDistance = 8.20f;
        float jumpCameraSide = 1.85f;
        float jumpCameraHeight = 2.35f;
        float resultCameraDistance = 8.60f;
        float resultCameraSide = 1.55f;
        float resultCameraHeight = 1.85f;
        float resultTargetSide = 0.55f;
        float crownFocusFov = 0.46f;
        float landingFov = 0.54f;
        float jumpFov = 0.61f;
        float resultFov = 0.54f;
        float resultUiCenterX = 0.50f;
        float resultUiCenterY = 0.24f;
        float resultUiScale = 1.00f;
        float resultBackdropAlpha = 1.00f;
        float resultGlowAlpha = 0.72f;
        float resultLetterStagger = 0.085f;
        float resultLetterFallDuration = 0.34f;
        float resultLetterDropHeight = 260.0f;
        float resultLetterBounceHeight = 48.0f;
        float resultLetterBounceDamping = 2.90f;
        float resultLetterBounceFrequency = 9.80f;
    };

    bool isGoal_ = false;
    bool goalSavePerformed_ = false;
    bool goalWasStageCleared_ = false;
    bool sessionStarCoins_[3] = {false, false, false};

    // --- ステージ開始時のゲート登場演出 ---
    bool stageEntryPresentationActive_ = false;
    bool stageEntryPresentationPending_ = true;
    bool stageEntryPresentationCompleted_ = false;
    bool stageEntryRuntimeWasPlaying_ = false;
    bool stageEntryPlayerEmergenceStarted_ = false;
    bool stageEntryHadPlayerControl_ = true;
    bool stageEntryCinemaBarOverrideActive_ = false;
    float stageEntryPresentationTimer_ = 0.0f;
    float stageEntryPresentationRetryTimer_ = 0.0f;
    float stageEntryCinemaBarBaseHeight_ = 0.0f;
    Object3d* stageEntryGate_ = nullptr;
    Vector3 stageEntryDirection_ = {1.0f, 0.0f, 0.0f};
    Vector3 stageEntryCameraStartEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 stageEntryCameraStartTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 stageEntryCameraFocusEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 stageEntryCameraFocusTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 stageEntryCameraRestoreEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 stageEntryCameraRestoreTarget_ = {0.0f, 0.0f, 0.0f};
    float stageEntryCameraBaseFov_ = 0.45f;
    float stageEntryCameraFocusFov_ = 0.45f;

    // --- ステージ3 ボス登場演出 ---
    bool arenaBossIntroActive_ = false;
    bool arenaBossIntroHadPlayerControl_ = true;
    bool arenaBossIntroCinemaBarOverrideActive_ = false;
    float arenaBossIntroTimer_ = 0.0f;
    float arenaBossIntroRevealDelay_ = 0.72f;
    float arenaBossIntroBaseFov_ = 0.45f;
    float arenaBossIntroCinemaBarBaseHeight_ = 0.0f;
    Object3d* arenaBossIntroBoss_ = nullptr;
    Object3d* arenaBossIntroGate_ = nullptr;
    Vector3 arenaBossIntroGameplayEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossIntroGameplayTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossIntroRestoreEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossIntroRestoreTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossIntroGateEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossIntroGateTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossIntroBossEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossIntroBossTarget_ = {0.0f, 0.0f, 0.0f};

    // --- ステージ3 ボス撃破・王冠落下演出 ---
    bool arenaBossRewardActive_ = false;
    bool arenaBossRewardHadPlayerControl_ = true;
    bool arenaBossRewardCinemaBarOverrideActive_ = false;
    float arenaBossRewardTimer_ = 0.0f;
    float arenaBossRewardBaseFov_ = 0.45f;
    float arenaBossRewardCinemaBarBaseHeight_ = 0.0f;
    Object3d* arenaBossRewardObject_ = nullptr;
    Vector3 arenaBossRewardGameplayEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossRewardGameplayTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossRewardFocusEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 arenaBossRewardFocusTarget_ = {0.0f, 0.0f, 0.0f};

    enum class GoalPresentationState { Inactive, Celebrating, ReadyToReturn, Returning };
    GoalPresentationState goalPresentationState_ = GoalPresentationState::Inactive;
    float goalPresentationTimer_ = 0.0f;
    float goalStarEmitTimer_ = 0.0f;
    float goalBurstEmitTimer_ = 0.0f;
    float goalCrownIdleTime_ = 0.0f;
    float goalCrownSparkleTimer_ = 0.0f;
    size_t goalCrownSparklePatternIndex_ = 0;
    Vector3 goalCrownPosition_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalPlayerBasePosition_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalPlayerBaseScale_ = {1.0f, 1.0f, 1.0f};
    Vector3 goalPlayerBaseRotation_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalPlayerPosePosition_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalMoveForward_ = {0.0f, 0.0f, 1.0f};
    Vector3 goalMoveRight_ = {1.0f, 0.0f, 0.0f};
    Vector3 goalCrownBasePosition_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCrownBaseScale_ = {1.0f, 1.0f, 1.0f};
    Vector3 goalCrownBaseRotation_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCrownSpringPosition_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCrownSpringVelocity_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCrownSpringRotation_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCrownSpringRotationVelocity_ = {0.0f, 0.0f, 0.0f};
    bool goalCrownSpringInitialized_ = false;
    Vector3 goalCameraGameplayEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCameraGameplayTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCameraFocusEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCameraFocusTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCameraLandingEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCameraLandingTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCameraJumpEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCameraJumpTarget_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCameraResultEye_ = {0.0f, 0.0f, 0.0f};
    Vector3 goalCameraResultTarget_ = {0.0f, 0.0f, 0.0f};
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
    std::unique_ptr<Sprite> goalOverlayFlash_;
    std::unique_ptr<Sprite> goalOverlayReturnText_;
    std::array<std::unique_ptr<Sprite>, 10> goalOverlayStageClearLetters_;
    std::array<std::unique_ptr<Sprite>, 10> goalOverlaySparkles_;

    // HUD の基準値。演出で拡縮しても戻せるよう保存する。
    struct HudSpriteState {
        Sprite* sprite = nullptr;
        Vector2 basePosition = {0.0f, 0.0f};
        Vector2 baseSize = {0.0f, 0.0f};
        Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    // --- プレイ中 HUD ---
    HudSpriteState hudHpIcon_;
    HudSpriteState hudCopyStyle_;
    CopyStyleHud copyStyleHud_;
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
    std::array<float, 3> hudStageStarPulseTimers_ = {0.0f, 0.0f, 0.0f};
    std::array<bool, 3> hudStageStarVisualCollected_ = {false, false, false};

    // スターコイン取得時、ワールド位置から HUD へ飛んでいく粒子
    struct StageStarUIFlyParticle {
        std::unique_ptr<Sprite> sprite;
        Vector2 start = {0.0f, 0.0f};
        Vector2 control = {0.0f, 0.0f};
        Vector2 end = {0.0f, 0.0f};
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

    // --- ボス／中ボス共通 HUD ---
    enum class PrismBossHudPhase {
        Hidden,
        Introducing,
        Active,
        Dismissing,
    };
    enum class BossHudTheme {
        Prism,
        Magma,
        FalseKing,
    };
    HudSpriteState prismBossHudName_;
    HudSpriteState prismBossHudGlow_;
    HudSpriteState prismBossHudBack_;
    HudSpriteState prismBossHudTrack_;
    HudSpriteState prismBossHudDamageFill_;
    HudSpriteState prismBossHudFill_;
    HudSpriteState prismBossHudHighlight_;
    HudSpriteState prismBossHudFrameTop_;
    HudSpriteState prismBossHudFrameBottom_;
    HudSpriteState prismBossHudShardLeft_;
    HudSpriteState prismBossHudShardRight_;
    std::array<HudSpriteState, 6> prismBossHudGlints_;
    std::array<HudSpriteState, 3> falseKingBossHudPhaseCrowns_;
    std::array<HudSpriteState, 2> falseKingBossHudPhaseDividers_;
    HudSpriteState falseKingCastTrack_;
    HudSpriteState falseKingCastFill_;
    PrismBossHudPhase prismBossHudPhase_ = PrismBossHudPhase::Hidden;
    float prismBossHudTimer_ = 0.0f;
    float prismBossHudDisplayedRate_ = 0.0f;
    float prismBossHudDelayedRate_ = 0.0f;
    float prismBossHudPreviousHp_ = 0.0f;
    float prismBossHudDamagePulseTimer_ = 0.0f;
    float prismBossHudDamageHoldTimer_ = 0.0f;
    float prismBossHudAnimationTimer_ = 0.0f;
    BossHudTheme prismBossHudTheme_ = BossHudTheme::Prism;
    int falseKingBossHudPreviousPhase_ = 1;
    float falseKingBossHudPhasePulseTimer_ = 0.0f;

    // --- ライフ減少演出 ---
    LifeLostPresentationState lifeLostState_;
    HudSpriteState lifeLostIcon_;
    HudSpriteState lifeLostXIcon_;
    std::array<HudSpriteState, 2> lifeLostDigits_;
    HudSpriteState lifeLostBackdrop_;
    std::unique_ptr<Camera> lifeLostCamera_;
    std::unique_ptr<Object3d> lifeLostSlimeObject_;
    std::unique_ptr<Object3d> lifeLostStunObject_;

    // --- オーバーレイ ---
    std::unique_ptr<ControlsGuideOverlay> controlsGuideOverlay_;
    std::unique_ptr<PauseMenuOverlay> pauseMenuOverlay_;
    std::unique_ptr<SettingsMenuOverlay> settingsOverlay_;
    std::unique_ptr<SaveIndicatorOverlay> saveIndicatorOverlay_;

    // 読み込みの段階ごとに、描画基盤・ゲームシステム・ステージ内容を準備します。
    void InitializeGameplayHUD();

    /// <summary>選択中のステージ設定を使い、シーンの基幹システムを初期化します。</summary>
    /// <param name="currentStage">読み込むステージの設定。</param>
    void InitializeCoreSystems(const StageData& currentStage);
    bool InitializeRenderCommons();

    void InitializeGameplaySystems();
    void InitializeGameplayObjectSystems();
    void InitializeGameplayParticleRuntime();
    void InitializeGameplayDebrisRuntime();
    void InitializeGameplaySkybox();

    /// <summary>ステージ設定に応じた描画状態を適用します。</summary>
    /// <param name="currentStage">描画設定の元になるステージ。</param>
    void ApplyGameplayRenderState(const StageData& currentStage);

    void LoadCurrentStageContent(const StageData& currentStage);
    void LoadCurrentStageObjects(const StageData& currentStage);
    void LoadCurrentStageSpritesAndView(const StageData& currentStage);

    void InitializeGameplayOverlays();
    void ApplyGoalCrownState();
    void ApplyStageStarCoinState();
    void UpdateGoalCrownIdleAnimation(float deltaTime);

    // 保存されたチェックポイントからの復帰、またはステージ入場を開始します。
    /// <summary>復帰演出が必要なときだけ開始します。</summary>
    /// <returns>復帰演出を開始した場合は true。</returns>
    bool StartRespawnIrisInIfNeeded();

    /// <summary>ステージ入場演出の開始条件を確認し、成立したら開始します。</summary>
    /// <returns>入場演出を開始した場合は true。</returns>
    bool StartStageEntryPresentation();
    void UpdateStageEntryPresentation(float deltaTime);
    void FinishStageEntryPresentation();
    Object3d* FindStageEntryGate() const;

    // ボス登場と撃破報酬の演出は、開始時の対象を保持して終了時に解除します。
    void UpdateArenaBossIntro(float deltaTime);
    void FinishArenaBossIntro();
    void UpdateArenaBossDefeatReward(float deltaTime);
    void FinishArenaBossDefeatReward();
    void InitializeDebugAnimationPreview();

    /// <summary>シーンが所有する描画・音声・演出リソースを終了時に解放します。</summary>
    void FinalizeGameplayResources();

#ifdef USE_IMGUI
    // ImGuiからの要求を次のゲーム更新で処理し、フレーム途中の状態変更を避けます。
    void ApplyDebugPlayerMorph(const char* enemyType);
    void ClearDebugPlayerMorph();
    /// <summary>フレーム更新の開始時に実行するデバッグ移動先を予約します。</summary>
    /// <param name="destination">予約する移動先。</param>
    void QueueDebugTeleport(DebugTeleportDestination destination);

    void ProcessPendingDebugTeleport();
    Object3d* FindDebugObjectByName(const char* objectName) const;

    /// <summary>アンカーを基準にプレイヤーを移動し、指定オブジェクトへ向けます。</summary>
    /// <param name="anchorName">移動基準となるオブジェクト名。</param>
    /// <param name="anchorOffset">基準位置からの移動量。</param>
    /// <param name="facingObjectName">向きの基準となるオブジェクト名。</param>
    /// <param name="destinationLabel">デバッグ表示に使う移動先名。</param>
    bool TeleportPlayerToDebugTarget(const char* anchorName, const Vector3& anchorOffset, const char* facingObjectName,
                                     const char* destinationLabel);
#endif

    int loadingInitializePhase_ = 0;
    size_t loadingInitializeItemIndex_ = 0;
    size_t loadingInitializeCompletedUnits_ = 0;
    size_t loadingInitializeTotalUnits_ = 1;
    bool loadingStageObjectsStarted_ = false;
    int loadingRenderCommonPhase_ = 0;

    // ゴール処理中は通常のゲーム更新を止め、演出側で時間を進めます。
    /// <summary>ゴール中の入力と演出更新を処理し、通常更新を続けるか判定します。</summary>
    /// <param name="deltaTime">このフレームの演出更新に用いる時間。</param>
    bool HandleGoalClear(float& deltaTime);

    // ゴール演出のタイムライン・設定値・Editorプレビュー。
    void InitializeGoalPresentationOverlay();
    void InitializeGoalCinematicTimeline();
    /// <summary>シーケンスに保存された時刻をゴール演出の設定値へ反映します。</summary>
    void ApplyGoalCinematicTimingFromSequence();

    /// <summary>現在の設定値をシーケンスの時刻へ反映します。</summary>
    void SyncGoalCinematicTimelineFromTuning();

    /// <summary>シーケンス内のアニメーションクリップを対象へ適用します。</summary>
    /// <param name="target">アニメーションの対象。</param>
    /// <param name="clip">適用するクリップ。</param>
    /// <param name="localTime">クリップ内の再生時刻。</param>
    /// <param name="isPreview">Editor プレビューからの呼び出しかどうか。</param>
    void HandleGoalCinematicAnimation(Object3d* target, const CinematicAnimationClipData& clip, float localTime,
                                      bool isPreview);

    /// <summary>ゴール演出のシグナルを受け取り、終了待ち状態へ進めます。</summary>
    /// <param name="target">シーケンスが指定した対象。現在のシグナルでは使用しません。</param>
    /// <param name="signal">発火したシグナル。</param>
    /// <param name="isPreview">Editor プレビューからの呼び出しかどうか。現在のシグナルでは使用しません。</param>
    void HandleGoalCinematicSignal(Object3d* target, const CinematicSignalMarker& signal, bool isPreview);
    void LoadGoalPresentationTuning();
    void SaveGoalPresentationTuning() const;
    void SanitizeGoalPresentationTuning();
    void DrawGoalPresentationEditor();
    Object3d* FindGoalCrownObject() const;
    void StartGoalPresentationPreview();
    void StopGoalPresentationPreview();

    // ゴール演出本体。プレイヤー・王冠・カメラ・UIを個別に更新します。
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

    // 通常プレイ中のカメラ、ポストエフェクト、HUDを更新します。
    void UpdatePostEffectState(float deltaTime);
    void UpdateLockOnAndCamera(float deltaTime);
    /// <summary>ロックオン対象を投影し、マーカーの位置と表示を更新します。</summary>
    /// <param name="camera">ワールド座標を投影するカメラ。</param>
    /// <param name="target">現在のロックオン対象。</param>
    /// <param name="deltaTime">マーカー更新に使う経過秒数。</param>
    void UpdateLockOnSprite(Camera* camera, Object3d* target, float deltaTime);
    void UpdateSceneSystems(float deltaTime);
    void UpdateEffectDebugShortcuts();
    void UpdateGameplayHUD(float deltaTime);
    void UpdateStageStarHUD(float deltaTime, bool visible);
    void UpdatePrismBossHUD(float deltaTime);
    void UpdateLifeLostPresentation(float deltaTime);

    // 操作ガイドとポーズは入力を消費するため、他のUIより先に処理します。
    bool HandleControlsGuideOverlay(float deltaTime);
    bool IsControlsGuideOpenTriggered() const;
    bool HandlePauseOverlay(float deltaTime);
    bool IsPauseOpenTriggered() const;

    // HUDとライフ減少演出の描画。
    void DrawGameplayHUD();
    void DrawStageStarHUD();
    void DrawPrismBossHUD();
    void DrawLifeLostPresentation();
    void InitializeLifeLostPresentationObjects();
    void UpdateLifeLostPresentationWorld(float deltaTime);
    /// <summary>ライフ減少演出のワールド側オブジェクトをライト付きで描画します。</summary>
    void DrawLifeLostPresentationWorld(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>HUD用スプライトを取得または補完し、演出前の基準位置・色を記録します。</summary>
    /// <param name="name">シーン内の識別名。</param>
    /// <param name="texturePath">使用する画像のパス。</param>
    /// <param name="position">画面上の基準位置。</param>
    /// <param name="size">基準サイズ。</param>
    /// <param name="anchor">スプライトのアンカー。</param>
    /// <param name="color">基準色。</param>
    HudSpriteState BindGameplayHUDSprite(const std::string& name, const std::string& texturePath,
                                         const Vector2& position, const Vector2& size, const Vector2& anchor,
                                         const Vector4& color);
    void DrawGameplayHUDSprite(const HudSpriteState& state);
    bool IsGameplayHUDSprite(const Sprite* sprite) const;
    /// <summary>二桁の HUD 数字を、指定した右端位置に合わせて更新します。</summary>
    /// <param name="digits">更新する十の位と一の位のスプライト。</param>
    /// <param name="value">表示する数値。</param>
    /// <param name="rightAlignedPosition">数字表示の右端座標。</param>
    /// <param name="digitHeight">表示する数字の高さ。</param>
    /// <param name="color">数字の描画色。</param>
    /// <param name="visible">数字を表示するかどうか。</param>
    void SetGameplayHUDNumber(std::array<HudSpriteState, 2>& digits, int value, const Vector2& rightAlignedPosition,
                              float digitHeight, const Vector4& color, bool visible);

    // 取得したスターコインをワールド座標からHUDへ移動させます。
    void StartStageStarHUDCollectEffect(int starIndex, const Vector3& worldPosition);
    Vector2 ProjectWorldToScreen(const Vector3& worldPosition) const;
    class EnemyPrismSlime* FindPrismBossForHUD();
    class EnemyMagmaSlime* FindMagmaBossForHUD();
    class EnemyFalseKingSlime* FindFalseKingBossForHUD();

    // 視錐台カリング判定
    bool IsVisible(Object3d* obj);
};
