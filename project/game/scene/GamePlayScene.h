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
#include "TimeAttackUI.h"
#include "ObjectManager.h"
#include "DebugEditor.h" 
#include "OptionUI.h"
#include <GhostRecorder.h>

#include <memory>
#include <vector>
#include <GPUParticleEmitter.h>
#include "engine/utility/math/Math.h"

// --- 前方宣言 ---
class DirectXCommon;
class InputManager;
class SceneManager;
class LevelLoader;
class LockOnSystem;
class GameRule;
class BossCore;


// メインゲームの進行、UI、演出、ボス戦をまとめるシーン。
class GamePlayScene : public BaseScene {
public:
    static bool s_isRebooting_;
    GamePlayScene();
    ~GamePlayScene() override;

    // --- 基本サイクル ---
    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void UpdateUI(float deltaTime);

    // --- 描画 ---
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;
    void DrawImGui() override;

    // --- ムービーイベント ---
    void StartBridgeDropMovie();
    void StartBossAppearanceMovie(); // ボス登場ムービーを開始

    // --- BaseScene インターフェース実装 ---
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objectManager_->GetObjects(); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }

    // スプライトはシーン側で保持する。
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }

    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

private:
    // --- ムービー・シネマティック状態 ---
    enum class MovieState {
        kNone,
        kBridgeDrop,
        kBossAppearance,
        kTutorialPlatformDescent,
        kTutorialDoorOpen        // チュートリアルドア開放
    };
    MovieState movieState_ = MovieState::kNone;
    float movieTimer_ = 0.0f;
    Vector3 movieStartCameraEye_;
    Vector3 movieStartCameraTarget_;
    bool hasBridgeDropped_ = false;
    bool bridgeCenterMagmaImpactPlayed_ = false;
    bool bridgeBackMagmaImpactPlayed_ = false;
    bool bridgeFrontMagmaImpactPlayed_ = false;
    bool isBridgeMagmaSePlaying_ = false;
#ifdef USE_IMGUI
    bool isBridgeDropPreviewForDebug_ = false;
#endif
    Vector3 movieStoredPlayerPos_;

    // --- チュートリアル進行とドア制御 ---
    bool hasFinishedTutorial_ = false;
    float doorOpenProgress_ = 0.0f;
    bool tutorialMovieStarted_ = false;
    float tutorialMovieTimer_ = 0.0f;
    bool hasTutorialMovieFinished_ = false;

    // --- Update 分割処理 ---
    bool IsCinematicMode() const;
    bool HandleEscapeKey();
    bool UpdatePauseAndOptionMenus(float deltaTime, float originalDeltaTime, bool isGameOver, bool isCinematicMode);
    bool UpdateSceneTransition(float originalDeltaTime);
    void UpdateTutorialDoor(float deltaTime);
    void UpdateMovieState(float deltaTime);
    void UpdateTutorialGuide(float deltaTime);
    void UpdateLockOnAndCamera(float deltaTime, bool isCinematicMode, Camera* camera, Math& math);
    void UpdateSceneObjects(float deltaTime);
    void UpdateGameOver(float originalDeltaTime);
    void InitializeGameOverTitleGlyphs();
    void ResetGameOverUiVisuals();
    void UpdateGameOverTitleGlyphs(float deltaTime, float alpha);
    void UpdateGameOverMenuVisuals(float deltaTime, float alpha);
    void DrawGameOverTitleGlyphs();
    void HideGameOverUi();
    void UpdateGameplaySystems(float deltaTime);
    void UpdateBossMovie(float deltaTime);
    void UpdateClearSequence(float deltaTime);
    void PlayBridgeMagmaSeIfNeeded();
    void StopBridgeMagmaSe();
    void UpdatePauseMenuVisuals(float deltaTime);
#ifdef USE_IMGUI
    bool PrepareBridgeDropPreviewForDebug();
#endif

    // --- UI入力表示 ---
    void ApplyPauseInputUiIfNeeded();
    void ApplyTutorialInputUiIfNeeded();
    void SetSpriteTexturePreserveSize(Sprite* sprite, const std::string& textureName);

private:
    // --- 外部システム参照 ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    // --- シーン内サブシステム ---
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;   // 配置読み込み
    std::unique_ptr<LockOnSystem> lockOnSystem_ = nullptr; // ロックオン管理
    std::unique_ptr<ObjectManager> objectManager_ = nullptr; // オブジェクト管理

    // --- 描画・生成の共通基盤 ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    // --- シーン所有リソース ---
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    std::unique_ptr<Text>  debugText_;
    std::unique_ptr<GameRule> gameRule_;
    std::unique_ptr<TimeAttackUI> timeAttackUI_;
    Player* player_ = nullptr;

    // --- BGM・SE ---
    uint32_t bgmHandle_ = 0;
    uint32_t bgmTutorialHandle_ = 0;
    uint32_t bgmWindHandle_ = 0;
    uint32_t bgmBattle01Handle_ = 0;
    uint32_t bgmBattle02Handle_ = 0;
    uint32_t bgmDefeatHandle_ = 0;
    bool isBGMPlaying_ = false;
    uint32_t particleSEHandle_ = 0;
    uint32_t seElevatorHandle_ = 0;
    uint32_t seOpenDoor1Handle_ = 0;
    uint32_t seOpenDoor2Handle_ = 0;
    uint32_t seBridgeMagmaHandle_ = 0;
    uint32_t seMissionHandle_ = 0;
    uint32_t seMissionClear3Handle_ = 0;
    uint32_t seCursorMove_ = 0;
    uint32_t seDecide_ = 0;
    uint32_t seCancel_ = 0;

    // --- ライト・描画補助リソース ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    MeshRenderer::PointLight* pointLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    MeshRenderer::SpotLight* spotLightData_ = nullptr;
    uint32_t gpuParticleTexHandle_ = 0;

    // --- ロックオンUI ---
    std::unique_ptr<Sprite> lockOnSprite_;
    bool isDrawLockOn_ = false;

    // --- プレイヤーHP UI ---
    Sprite* playerHpBarSprite_ = nullptr;
    Sprite* playerDamageBarSprite_ = nullptr;
    float playerHpBarMaxWidth_ = 0.0f;
    float playerVisualHp_ = 0.0f;
    float playerDamageDelayTimer_ = 0.0f;
    float playerPrevHpRatio_ = 0.0f;

    // --- 回避クールタイム UI ---
    Sprite* playerDashBarSprite_ = nullptr;
    Sprite* playerDashBackSprite_ = nullptr;
    float playerDashBarMaxWidth_ = 0.0f;

    // --- チュートリアル足場 ---
    Object3d* tutorialPlatform_ = nullptr;
    float tutorialPlatformOffset_ = 0.0f;

    // --- ボスHP・バリア UI ---
    BossCore* boss_ = nullptr;

    Sprite* bossHpBarSprite_ = nullptr;
    Sprite* bossDamageBarSprite_ = nullptr;
    float bossHpBarMaxWidth_ = 0.0f;
    float bossVisualHp_ = 0.0f;
    float bossDamageDelayTimer_ = 0.0f;
    float bossPrevHpRatio_ = 0.0f;

    Sprite* bossHpBackSprite_ = nullptr;
    Sprite* barrierHpBarSprite_ = nullptr;
    Sprite* barrierDamageBarSprite_ = nullptr;
    float barrierHpBarMaxWidth_ = 0.0f;
    float barrierVisualMain_ = 0.0f;
    float barrierVisualDamage_ = 0.0f;
    float barrierDamageDelayTimer_ = 0.0f;
    float barrierPrevHpRatio_ = 0.0f;
    Sprite* bossNameSprite_ = nullptr;

    // --- ゲームオーバー UI ---
    enum class GameOverMenuIndex {
        Restart,
        Title,
        Max
    };
    int currentGameOverMenuIndex_ = (int)GameOverMenuIndex::Restart;
    bool isGameOverUiReady_ = false;

    struct GameOverGlyphStrip {
        std::vector<std::unique_ptr<Sprite>> glyphs;
        std::vector<Vector2> baseOffsets;
        std::vector<Vector2> baseSizes;
        Vector2 basePosition = { 0.0f, 0.0f };
        float animationTimer = 0.0f;
        bool initialized = false;
    };

    Sprite* gameOverTextSprite_ = nullptr;
    Sprite* restartTextSprite_ = nullptr;
    Sprite* titleTextSprite_ = nullptr;
    GameOverGlyphStrip gameOverTitleGlyphStrip_;
    std::unique_ptr<Sprite> gameOverEnterTextSprite_;
    Vector2 gameOverRestartBaseSize_ = {};
    Vector2 gameOverTitleBaseSize_ = {};
    Vector2 gameOverEnterTextBaseSize_ = { 275.0f, 46.0f };
    float gameOverUiTimer_ = 0.0f;
    float gameOverMenuBlinkTimer_ = 0.0f;
    bool isGameOverUiStarted_ = false;
    bool gameOverUiUsesGamepad_ = false;
    std::unique_ptr<GPUParticleEmitter> emitterA_;
    std::unique_ptr<GPUParticleEmitter> emitterB_;
    std::unique_ptr<GPUParticleEmitter> emitterC_;

    // --- ポーズ・オプション UI ---
    bool isPaused_ = false;

    enum class PauseMenuIndex {
        Restart,
        Option,
        Title,
        Max
    };
    int currentPauseMenuIndex_ = (int)PauseMenuIndex::Restart;

    OptionUI optionUI_;
    bool isOptionMenu_ = false;

    Sprite* poseBackSprite_ = nullptr;
    Sprite* poseTextSprite_ = nullptr;
    Sprite* restartPoseTextSprite_ = nullptr;
    Sprite* optionPoseTextSprite_ = nullptr;
    Sprite* titleTextPoseSprite_ = nullptr;
    Sprite* tabPauseTextSprite_ = nullptr;
    Sprite* optionControlsSprite_ = nullptr;
    Vector2 pauseRestartTextBaseSize_ = {};
    Vector2 pauseOptionTextBaseSize_ = {};
    Vector2 pauseTitleTextBaseSize_ = {};
    float pauseMenuBlinkTimer_ = 0.0f;
    bool pauseUiUsesGamepad_ = false;
    bool hasAppliedPauseInputUi_ = false;
    bool isGameClearSequence_ = false;
    float gameClearTimer_ = 0.0f;
    bool isBossMoviePlaying_ = false;

    bool hasBossAppeared_ = false;

    // --- チュートリアル表示制御 ---
    enum class TutorialStep {
        kNone,
        kShowMove,          // 初回：移動表示
        kWaitForMove,       // 移動入力待ち
        kShowCamera,        // カメラ説明表示
        kWaitForCamera,     // カメラ操作待ち
        kShowJump,          // ジャンプ表示
        kWaitForJump,       // ジャンプ待ち
        kShowLockOn,        // ロックオン表示
        kWaitForLockOn,     // ロックオン待ち
        kShowAttack,        // 攻撃表示
        kWaitForAttack,     // 攻撃待ち
        kShowFallAttack,    // 落下攻撃表示
        kWaitForFallAttack, // 落下攻撃待ち
        kShowDodge,         // 回避表示
        kWaitForDodge,      // 回避待ち
        kCompleted
    };
    TutorialStep tutorialStep_ = TutorialStep::kNone;

    // チュートリアル説明画像
    Sprite* tutorialMoveSprite_ = nullptr;
    Sprite* tutorialCameraSprite_ = nullptr;
    Sprite* tutorialJumpSprite_ = nullptr;
    Sprite* tutorialLockOnSprite_ = nullptr;
    Sprite* tutorialAttackSprite_ = nullptr;
    Sprite* tutorialFallAttackSprite_ = nullptr;
    Sprite* tutorialDodgeSprite_ = nullptr;
    bool tutorialUiUsesGamepad_ = false;
    bool hasAppliedTutorialInputUi_ = false;

    float tutorialMoveTimer_ = 0.0f;
    float tutorialCameraTimer_ = 0.0f;
    int tutorialAttackCount_ = 0;
    int tutorialFallAttackCount_ = 0;
    int tutorialJumpCount_ = 0;
    float doorOpenedTimer_ = 0.0f;
    class IAnimationState* tutorialPrevState_ = nullptr;

    // --- チュートリアル矢印用 ---
    Object3d* tutorialArrow_ = nullptr;
    float tutorialArrowAnimTimer_ = 0.0f;
    Vector3 tutorialArrowDefaultPos_;
    size_t tutorialArrowWaypointIndex_ = 0;

    // --- ミッション表示スプライト ---
    Sprite* missionText_mission_ = nullptr;
    Sprite* missionText_line_ = nullptr;
    Sprite* missionText_Mark_ = nullptr;
    Sprite* missionText_lever_ = nullptr;
    Sprite* missionText_go_ = nullptr;
    Sprite* missionText_boss_ = nullptr;

    // --- ミッション表示状態 ---
    bool missionInitialShown_ = false;
    bool missionGoShown_ = false;
    bool missionBossShown_ = false;
    bool missionLeverSePlayed_ = false;
    bool missionGoSePlayed_ = false;
    bool missionBossSePlayed_ = false;

    float tutorialTimer_ = 0.0f;
    bool tutorialUiCompleted_ = false;

    // ミッション演出の進行度
    float missionMarkAnimProgress_ = 0.0f;
    float missionLeverAnimProgress_ = 0.0f;
    float missionGoAnimProgress_ = 0.0f;
    float missionBossAnimProgress_ = 0.0f;

    bool isLeverOut_ = false;
    float leverOutProgress_ = 0.0f;
    bool isGoOut_ = false;
    float goOutProgress_ = 0.0f;
    float missionSwitchDelayTimer_ = 0.0f;

    Vector2 missionMarkBaseSize_;
    Vector2 missionLeverBasePos_;
    Vector2 missionGoBasePos_;
    Vector2 missionBossBasePos_;
    Vector2 missionLeverBaseSize_;
    Vector2 missionGoBaseSize_;
    Vector2 missionBossBaseSize_;

    // --- シーン遷移演出 ---
    bool isRestartTransition_ = false;
    float restartTimer_ = 0.0f;
    bool isTitleTransition_ = false;

    // --- シネマティック表示 ---
    float currentCinemaBarHeight_ = 0.0f;

    // --- ボス演出補助 ---
    uint32_t bossContainerTopParticleId_ = 0;
    uint32_t bossContainerBottomParticleId_ = 0;

    // ボス・バリアアイコン
    Sprite* bossIconSprite_ = nullptr;
    Sprite* shieldIconSprite_ = nullptr;

    Sprite* bossHpFrameSprite_ = nullptr;
    Sprite* bariaFrameSprite_ = nullptr;
    Sprite* hpFrameSprite_ = nullptr;

    // アイコンシェイク
    Vector2 bossIconBasePos_;
    float bossIconShakeTimer_ = 0.0f;
    float bossIconShakeIntensity_ = 0.0f;

    Vector2 shieldIconBasePos_;
    float shieldIconShakeTimer_ = 0.0f;
    float shieldIconShakeIntensity_ = 0.0f;

};
