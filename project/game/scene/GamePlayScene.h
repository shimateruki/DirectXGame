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


/// <summary>
/// ゲームプレイシーン
/// </summary>
class GamePlayScene : public BaseScene {
public:
    static bool s_isRebooting_;
    GamePlayScene();
    ~GamePlayScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void UpdateUI(float deltaTime);
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;
    void DrawImGui() override;
    // --- ムービーイベント ---
    void StartBridgeDropMovie();
    void StartBossAppearanceMovie(); // ★ 追加：ボス登場の合図を受け取る関数

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




private:


private:
    enum class MovieState {
        kNone,
        kBridgeDrop,
        kBossAppearance,
        kTutorialPlatformDescent,
        kTutorialDoorOpen        // ★ 追加：ドアが開く演出
    };
    MovieState movieState_ = MovieState::kNone;
    float movieTimer_ = 0.0f;
    Vector3 movieStartCameraEye_;
    Vector3 movieStartCameraTarget_;
    bool hasBridgeDropped_ = false;
    Vector3 movieStoredPlayerPos_;

    // --- チュートリアルドア用 ---
    bool hasFinishedTutorial_ = false;
    float doorOpenProgress_ = 0.0f;
    bool tutorialMovieStarted_ = false;
    float tutorialMovieTimer_ = 0.0f;
    bool hasTutorialMovieFinished_ = false; // ★ 追加

    bool IsCinematicMode() const;

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
    std::unique_ptr<TimeAttackUI> timeAttackUI_;
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
	//sprite変数
    Sprite* playerHpBarSprite_ = nullptr;
    Sprite* playerDamageBarSprite_ = nullptr; // 追加：ダメージ残像用
    float playerHpBarMaxWidth_ = 0.0f; // 100%の時の長さ
    float playerVisualHp_ = 0.0f;      // 追加：表示上のHP (演出用)
    float playerDamageDelayTimer_ = 0.0f; // 追加：減少開始までの待機時間
    float playerPrevHpRatio_ = 0.0f;   // 追加：前フレームのHP率

    Object3d* tutorialPlatform_ = nullptr; // ★ 降下させるプラットフォーム
    float tutorialPlatformOffset_ = 0.0f; // ★ プレイヤーとのY軸オフセット

    // =================================================
    //  ボスUI同期用のポインタと変数を保持
    // =================================================
    BossCore* boss_ = nullptr;

    Sprite* bossHpBarSprite_ = nullptr;    // メインHPバー
    Sprite* bossDamageBarSprite_ = nullptr; // 追加：ダメージ残像用
    float bossHpBarMaxWidth_ = 0.0f;
    float bossVisualHp_ = 0.0f;             // 追加：ボスの表示上のHP (演出用)
    float bossDamageDelayTimer_ = 0.0f;    // 追加：減少開始までの待機時間
    float bossPrevHpRatio_ = 0.0f;         // 追加：前フレームのHP率

    Sprite* bossHpBackSprite_ = nullptr;
    Sprite* barrierHpBarSprite_ = nullptr; // バリアHPバー
    Sprite* barrierDamageBarSprite_ = nullptr; // 追加：バリア用ダメージ残像
    float barrierHpBarMaxWidth_ = 0.0f;
    float barrierVisualMain_ = 0.0f;       // 追加：バリアメインバーの表示用HP
    float barrierVisualDamage_ = 0.0f;     // 追加：バリアダメージバーの表示用HP
    float barrierDamageDelayTimer_ = 0.0f; // 追加：バリアの減少待機時間
    float barrierPrevHpRatio_ = 0.0f;      // 追加：前フレームのバリアHP率
    Sprite* bossNameSprite_ = nullptr;
    enum class GameOverMenuIndex {
        Restart,
        Title,
        Max
    };
    int currentGameOverMenuIndex_ = (int)GameOverMenuIndex::Restart;
    bool isGameOverUiReady_ = false; // テキストのフェードインが完了したか

    Sprite* gameOverTextSprite_ = nullptr;
    Sprite* restartTextSprite_ = nullptr;
    Sprite* titleTextSprite_ = nullptr;
    std::unique_ptr<GPUParticleEmitter> emitterA_;
    std::unique_ptr<GPUParticleEmitter> emitterB_;
    std::unique_ptr<GPUParticleEmitter> emitterC_;
    // =======================================================
    // ポーズ画面用
    // =======================================================
    bool isPaused_ = false; // ポーズ中かどうか

    enum class PauseMenuIndex {
        Restart,
        Option,
        Title,
        Max
    };
    int currentPauseMenuIndex_ = (int)PauseMenuIndex::Restart;

    OptionUI optionUI_;
    bool isOptionMenu_ = false; // オプションメニューを開いているか

    Sprite* poseBackSprite_ = nullptr;
    Sprite* poseTextSprite_ = nullptr;
    Sprite* restartPoseTextSprite_ = nullptr;
    Sprite* optionPoseTextSprite_ = nullptr; // 追加: optionText.png
    Sprite* titleTextPoseSprite_ = nullptr;
    bool isGameClearSequence_ = false;
    float gameClearTimer_ = 0.0f;
    bool isBossMoviePlaying_ = false; // ボスムービー中かどうか

    bool hasBossAppeared_ = false; // ボス登場イベントが既に終わったかどうかのロックフラグ

    // ==========================================
    // チュートリアル表示制御 (拡張版)
    // ==========================================
    enum class TutorialStep {
        kNone,
        kShowMove,          // 初回：移動表示
        kWaitForMove,       // 移動入力待ち
        kShowCamera,        // カメラ説明表示
        kWaitForCamera,     // カメラ操作待ち
        kShowLockOn,        // ロックオン表示
        kWaitForLockOn,     // ロックオン待ち
        kShowAttack,        // 攻撃表示
        kWaitForAttack,     // 攻撃待ち
        kShowDodge,         // 回避表示
        kWaitForDodge,      // 回避待ち
        kCompleted
    };
    TutorialStep tutorialStep_ = TutorialStep::kNone;

    // チュートリアル用スプライトポインタ
    Sprite* tutorialMoveSprite_ = nullptr;
    Sprite* tutorialCameraSprite_ = nullptr;
    Sprite* tutorialLockOnSprite_ = nullptr;
    Sprite* tutorialAttackSprite_ = nullptr;
    Sprite* tutorialDodgeSprite_ = nullptr;

    // ミッションタスクスプライト（6枚）
    Sprite* missionText_mission_ = nullptr;
    Sprite* missionText_line_ = nullptr;
    Sprite* missionText_Mark_ = nullptr;
    Sprite* missionText_lever_ = nullptr;
    Sprite* missionText_go_ = nullptr;    // チュートリアルドア消失で表示
    Sprite* missionText_boss_ = nullptr;  // ボス登場後に表示

    // ミッション表示状態フラグ（再表示防止用）
    bool missionInitialShown_ = false; // 最初の4枚を表示済みか
    bool missionGoShown_ = false;
    bool missionBossShown_ = false;

    float tutorialTimer_ = 0.0f; // フェード／タイマー汎用
    bool tutorialUiCompleted_ = false;

    // --- ミッション演出用 ---
    float missionMarkAnimProgress_ = 0.0f;
    float missionLeverAnimProgress_ = 0.0f;
    float missionGoAnimProgress_ = 0.0f;
    float missionBossAnimProgress_ = 0.0f;

    bool isLeverOut_ = false;
    float leverOutProgress_ = 0.0f;
    bool isGoOut_ = false;
    float goOutProgress_ = 0.0f;
    float missionSwitchDelayTimer_ = 0.0f; // ★ 追加

    Vector2 missionMarkBaseSize_;
    Vector2 missionLeverBasePos_;
    Vector2 missionGoBasePos_;
    Vector2 missionBossBasePos_;
    Vector2 missionLeverBaseSize_;
    Vector2 missionGoBaseSize_;
    Vector2 missionBossBaseSize_;


    bool isRestartTransition_ = false;
    float restartTimer_ = 0.0f;
    bool isTitleTransition_ = false;
    // =======================================================
    // シネマティック演出（黒帯）用
    // =======================================================
    float currentCinemaBarHeight_ = 0.0f;

    // ボスコンテナのパーティクル用ID
    uint32_t bossContainerTopParticleId_ = 0;
    uint32_t bossContainerBottomParticleId_ = 0;

};