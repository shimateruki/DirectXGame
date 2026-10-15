#pragma once
#include "LifeLostPresentationState.h"
#include "BaseScene.h"
#include "game/system/SceneGameplayAccess.h"
#include "game/system/IStageProgressScene.h"
#include "Camera.h"
#include "CinematicPlayer.h"
#include "CinematicSequence.h"
#include "game/scene/gameplay/GoalClearPlayerAnimator.h"
#include "MeshRenderer.h"
#include "ObjectManager.h"
#include "Player.h"
#include "game/ui/CopyStyleHud.h"
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
class Object3dCommon;
class ParticleCommon;
class ParticleSystem;
namespace engine { class SpriteCommon; }
class Text;
class Skybox;
class ISceneController;
class ControlsGuideOverlay;
class PauseMenuOverlay;
class SettingsMenuOverlay;
class SaveIndicatorOverlay;

/// <summary>
/// ゲームプレイ本編のシーン。ステージ読み込み、プレイヤー、HUD、ゴール演出を統合する。
/// </summary>
class GamePlayScene : public BaseScene, public IStageProgressScene, public ISceneGameplayAccess {
  public:
    /// <summary>シーン固有リソースをInitializeで作るため、初期状態だけ用意する。</summary>
    GamePlayScene();
    /// <summary>所有リソースの解放はFinalizeに任せてシーンを破棄する。</summary>
    ~GamePlayScene() override;

    /// <summary>ステージ内容と演出を含むゲームプレイシーンを初期化する。</summary>
    void Initialize() override;
    /// <summary>ローディング画面から呼ぶ段階初期化を開始する。</summary>
    void BeginLoadingInitialize() override;
    /// <summary>初期化を一段進め、全段階が完了したか返す。</summary>
    bool InitializeLoadingStep() override;
    /// <summary>段階初期化の進捗を0から1の割合で返す。</summary>
    float GetLoadingInitializeProgress() const override;

    /// <summary>シーンが表示対象になった時に入力・カメラなどを有効化する。</summary>
    void OnActivated() override;
    /// <summary>ステージで使うScene Assetと依存リソースの先読み一覧を作る。</summary>
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    /// <summary>ゲームプレイ中に所有した描画・演出リソースを終了処理する。</summary>
    void Finalize() override;

    /// <summary>プレイヤー、敵、ギミックと進行中の演出を更新する。</summary>
    void Update(float deltaTime) override;
    /// <summary>ポーズや操作ガイドなど、ワールド更新と分けたUI入力を処理する。</summary>
    void UpdateUI(float deltaTime);

    /// <summary>ステージの3D描画パスを実行する。</summary>
    void Draw() override;
    /// <summary>HUDと画面上の演出をワールド描画の後に描く。</summary>
    void DrawUI() override;
    /// <summary>ステージの影用描画パスを実行する。</summary>
    void DrawShadow() override;

    // --- IEditable ---
    /// <summary>Editorで表示するシーン名を返す。</summary>
    std::string GetName() override { return "Game Play Scene"; }
    /// <summary>プレイ中に調整できるシーン設定をEditorへ描く。</summary>
    void DrawImGui() override;

    /// <summary>ステージ内の橋が落ちるカメラ演出を開始する。</summary>
    void StartBridgeDropMovie();

    /// <summary>ボスとゲートの登場演出を開始します。</summary>
    /// <param name="bossObject">登場させるボスの描画物。</param>
    /// <param name="gateObject">登場演出に使うゲート。</param>
    /// <param name="bossRevealDelay">演出開始からボスを見せるまでの秒数。</param>
    void StartArenaBossIntro(engine::Object3d* bossObject, engine::Object3d* gateObject, float bossRevealDelay);
    /// <summary>アリーナボス撃破後の報酬表示を開始する。実行できない状態では何もしない。</summary>
    /// <param name="rewardObject">登場させる報酬Object。所有権は移さない。</param>
    void StartArenaBossDefeatReward(engine::Object3d* rewardObject);

    /// <summary>最終ボスの王冠演出を開始します。</summary>
    /// <returns>演出を開始できた場合は true。</returns>
    bool StartCrownFinale(class EnemyFalseKingSlime* boss);
    /// <summary>編集用の複製ステージで王冠ムービーのプレビューを開始する。</summary>
    /// <param name="defeat">撃破ムービーを試す場合はtrue。</param>
    /// <returns>プレビューの開始に成功した場合はtrue。</returns>
    bool BeginCrownMoviePreview(bool defeat = false);
    /// <summary>最終ボスの撃破ムービーを開始する。</summary>
    /// <returns>ボスとプレイヤーが有効で、演出を開始できた場合はtrue。</returns>
    bool StartCrownDefeat(class EnemyFalseKingSlime* boss);
    /// <summary>編集中の王冠ムービープレビューを終了する。</summary>
    void EndCrownMoviePreview();
    /// <summary>王冠ムービープレビューを先頭へ巻き戻す。</summary>
    void RewindCrownMoviePreview();
    /// <summary>王冠ムービープレビューを指定秒数だけ進める。</summary>
    void StepCrownMoviePreview(float deltaTime);
    /// <summary>王冠ムービープレビューが進行中か返す。</summary>
    bool IsCrownMoviePreviewActive() const { return crownMoviePreview_.active; }
    // 結晶破壊の猶予が切れた後だけ、第二形態の全域槍を映像として見せます。
    bool StartSealBarrageMovie(class EnemyFalseKingSlime* boss, const engine::Vector3& strike);

    // BaseSceneの所有コンテナ操作。所有権の契約は基底クラスと同じです。
    /// <summary>このシーンが所有する3Dオブジェクト一覧を返す。</summary>
    const std::vector<std::unique_ptr<engine::Object3d>>& GetObjects() const override { return objectManager_->GetObjects(); }
    /// <summary>Object3dを即時登録して非所有参照を返す。</summary>
    engine::Object3d* AddObjectImmediate(std::unique_ptr<engine::Object3d> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }
    /// <summary>Object3dを所有コンテナから取り出して呼び出し元へ渡す。</summary>
    std::unique_ptr<engine::Object3d> TakeObjectImmediate(engine::Object3d* object) override { return objectManager_->TakeObjectImmediate(object); }
    /// <summary>Object3dの所有権をシーンの管理器へ移す。</summary>
    void AddObject(std::unique_ptr<engine::Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    /// <summary>Object3dの削除を管理器へ予約する。</summary>
    void RequestRemoveObject(engine::Object3d* object) override { objectManager_->RequestRemove(object); }
    /// <summary>Replayの記録情報から動的Object3dを再生成する。</summary>
    std::unique_ptr<engine::Object3d> CreateReplayObject(const json& descriptor) override;
    /// <summary>ReplayでObject3dを再生成した後、シーン内部の参照を結び直す。</summary>
    void OnReplayObjectsRecreated() override;

    /// <summary>このシーンが所有するSprite一覧を返す。</summary>
    const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const override { return sprites_; }
    /// <summary>Replayに記録するSpriteを所有UIから収集する。</summary>
    void CollectReplaySprites(std::vector<engine::Sprite*>& sprites) override;
    /// <summary>Replayに必要なゲーム進行状態をJSONへ保存する。</summary>
    void CaptureReplaySceneState(json& state) const override;
    /// <summary>ReplayのJSONからゲーム進行状態を復元する。</summary>
    void RestoreReplaySceneState(const json& state) override;

    /// <summary>このシーンの3D描画基盤への非所有参照を返す。</summary>
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    /// <summary>このシーンのスカイボックスへの非所有参照を返す。</summary>
    Skybox* GetSkybox() override { return skybox_.get(); }
    /// <summary>このシーンのSprite描画基盤への非所有参照を返す。</summary>
    engine::SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    /// <summary>このシーンの粒子システムへの非所有参照を返す。</summary>
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    /// <summary>現在のプレイヤーへの非所有参照を返す。</summary>
    Player* GetPlayer() const override { return player_; }
    /// <summary>シーンが参照するプレイヤーを設定する。所有権は移さない。</summary>
    void SetPlayer(Player* player) override { player_ = player; }
    /// <summary>ステージの進行規則を管理するGameRuleへの非所有参照を返す。</summary>
    GameRule* GetGameRule() override { return gameRule_.get(); }

    /// <summary>ゴール状態を設定する。演出開始はStartGoalPresentationで行う。</summary>
    void SetIsGoal(bool isGoal);
    /// <summary>指定王冠を使ってステージクリア演出を開始する。</summary>
    void StartGoalPresentation(engine::Object3d* crownObject);
    /// <summary>このシーンがゴール処理中か返す。</summary>
    bool IsGoal() const { return isGoal_; }
    /// <summary>プレイヤーとゴールの接触から、開始条件を満たす場合にゴール処理を始める。</summary>
    bool TryStartGoalFromContact(engine::Object3d* actor, engine::Object3d* goal) override;
    /// <summary>接触で取得したスターコインを進行記録とHUD演出へ反映する。</summary>
    void CollectStarCoinFromContact(int coinIndex, const engine::Vector3& worldPosition) override;

    /// <summary>番号で指定したスターコインの取得状態を更新する。</summary>
    void CollectStarCoin(int coinIndex);
    /// <summary>取得状態を更新し、ワールド位置からHUDへ移動する演出を始める。</summary>
    void CollectStarCoin(int coinIndex, const engine::Vector3& worldPosition);

    /// <summary>減少前後の残機数を使って、ライフ減少演出を開始します。</summary>
    /// <param name="beforeLives">減少前の残機数。</param>
    /// <param name="afterLives">減少後の残機数。</param>
    void StartLifeLostPresentation(int beforeLives, int afterLives);
    /// <summary>結晶破壊後の全域攻撃ムービーが進行中か返す。</summary>
    bool IsSealBarrageMovieActive() const { return sealBarrageMovie_.active; }
    /// <summary>残機減少演出が終了したか返す。</summary>
    bool IsLifeLostPresentationFinished() const { return lifeLostState_.IsFinished(); }
    /// <summary>残機減少演出の画面オーバーレイを隠す。</summary>
    void HideLifeLostPresentationOverlay();

  private:
    /// <summary>シーンが所有する可変Sprite配列を基底の編集処理へ渡す。</summary>
    std::vector<std::unique_ptr<engine::Sprite>>* GetMutableSprites() override { return &sprites_; }
    enum class MovieState { kNone, kBridgeDrop };
    MovieState movieState_ = MovieState::kNone;
    float movieTimer_ = 0.0f;
    engine::Vector3 movieStartCameraEye_;
    engine::Vector3 movieStartCameraTarget_;
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
    std::unique_ptr<engine::SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    std::vector<std::unique_ptr<engine::Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    std::unique_ptr<Text> debugText_;
    std::unique_ptr<GameRule> gameRule_;
    std::unique_ptr<ISceneController> sceneController_;

    Player* player_ = nullptr;

    // 最終演出で変更したオブジェクトの状態を、終了時の復元用に保持します。
    struct CrownFinaleObjectPose {
        engine::Object3d* object = nullptr;
        engine::Vector3 position{};
        engine::Vector3 rotation{};
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
        engine::Vector4 playerColor{};
        int playerMaterial = 25;
        bool launched = false;
        bool landed = false;
        float playerYaw = 0.0f;
        float time = 0.0f;
        float duration = 9.0f;
        float fov = 0.45f;
        float bars = 0.0f;
        engine::Vector3 start{};
        engine::Vector3 playerScale{};
        engine::Vector3 bossStart{};
        engine::Vector3 offset{};
        engine::Vector3 eye{};
        engine::Vector3 target{};
        engine::Vector3 bossScale{};
        engine::Vector3 bossRotation{};
        engine::Vector3 fragmentStart{};
        engine::Vector3 lastSealPosition{};
        engine::Vector3 fragmentGround{};
        std::string bossModel;
        class EnemyFalseKingSlime* boss = nullptr;
        std::vector<CrownFinaleObjectPose> originalObjects;
        std::vector<CrownFinaleObjectPose> fractureChunks;
        std::vector<std::unique_ptr<engine::Object3d>> floorCracks;
        std::vector<engine::Object3d*> generatedObjects;
        std::unique_ptr<engine::Object3d> fragment;
        std::unique_ptr<engine::Object3d> fragmentCrack;
        std::unique_ptr<engine::Object3d> lastSeal;
        std::vector<std::unique_ptr<engine::Object3d>> fragmentChips;
        std::unique_ptr<engine::Object3d> shockwave;
        std::unique_ptr<engine::Object3d> wings;
        std::unique_ptr<engine::Object3d> rightWing;
        std::unique_ptr<engine::Object3d> innerRune;
        std::unique_ptr<engine::Object3d> goldBody;
        std::unique_ptr<engine::Object3d> regalia;
        std::unique_ptr<engine::Object3d> groundFractures;
    } crownFinale_;

    // 元の配置・能力へ触れず、プレビュー複製の寿命と元のカメラを保持します。
    struct CrownDefeatPart {
        std::unique_ptr<engine::Object3d> visual;
        engine::Vector3 pivot{};
        bool gem = false;
    };
    // 撃破中は元のボスを残し、分割モデルだけを動かして寿命と撃破通知を分離します。
    struct CrownDefeatState {
        bool active=false, completed=false, bossVisible=true, hadControl=true;
        float time=0, bars=0, playerYaw=0;
        class EnemyFalseKingSlime* boss=nullptr;
        engine::Vector3 bossPosition{},bossScale{},bossRotation{},playerPosition{},playerScale{},playerRotation{};
        engine::Vector3 eye{},target{},forward{},right{};
        Camera* previousCamera=nullptr;
        std::unique_ptr<Camera> camera;
        std::unique_ptr<engine::Object3d> body,cracks,ring;
        std::vector<CrownDefeatPart> parts;
        std::vector<std::unique_ptr<engine::Object3d>> rays;
    } crownDefeat_;
    /// <summary>最終ボス撃破ムービーの時刻と配置を進める。</summary>
    void UpdateCrownDefeat(float deltaTime);
    /// <summary>撃破ムービーの終了状態を確定し、通常シーンへ引き渡す。</summary>
    void FinishCrownDefeat();
    /// <summary>撃破ムービーが一時変更した状態を初期状態に戻す。</summary>
    void ResetCrownDefeat();
    /// <summary>撃破ムービー中のワールド演出を描画する。</summary>
    void DrawCrownDefeatWorld();

    // 王冠演出プレビューの一時カメラを所有し、元の Player・カメラ・表示対象は復元用に借用する。
    struct CrownMoviePreviewState {
        bool defeat = false;
        bool active = false;
        Player* originalPlayer = nullptr;
        std::unique_ptr<Camera> camera;
        Camera* previousCameraOverride = nullptr;
        std::vector<engine::Object3d*> hiddenObjects;
        std::vector<engine::Object3d*> objects;
        engine::Vector3 eye{}, target{}, rotation{};
        Camera::FollowMode followMode = Camera::FollowMode::kAimable;
        engine::Object3d* follow = nullptr;
        float fov = 0.45f;
    } crownMoviePreview_;

    // 封印失敗演出の時計と、終了時に復元する操作権・視点・追従対象。
    struct SealBarrageMovieState {
        bool active = false;
        bool hadControl = true;
        float time = 0.0f;
        float fov = 0.7f;
        float bars = 0.0f;
        engine::Vector3 eye{};
        engine::Vector3 target{};
        engine::Vector3 rotation{};
        engine::Vector3 boss{};
        engine::Vector3 strike{};
        engine::Object3d* follow = nullptr;
        engine::Object3d* lockOn = nullptr;
        Camera::FollowMode followMode = Camera::FollowMode::kAimable;
    } sealBarrageMovie_;

    /// <summary>結晶破壊の猶予切れ後に再生する全域攻撃ムービーを進める。</summary>
    void UpdateSealBarrageMovie(float deltaTime);
    /// <summary>全域攻撃ムービーを終了し、保持した状態を片付ける。</summary>
    void FinishSealBarrageMovie();

    std::array<std::unique_ptr<engine::Sprite>, 6> crownFinaleMessages_;

    // 最終演出で一時的に変更したワールドの状態は Finish で復元します。
    /// <summary>王冠の結晶防衛と最終演出の進行を更新する。</summary>
    void UpdateCrownFinale(float deltaTime);
    /// <summary>最終演出の成否を確定し、次の進行状態へ移す。</summary>
    void FinishCrownFinale(bool completed);

    /// <summary>最終演出で変更したオブジェクトの姿勢と表示状態を戻します。</summary>
    /// <summary>最終演出で変えたワールドObjectの姿勢と表示状態を復元する。</summary>
    void ResetCrownFinaleWorld();

    /// <summary>最終演出に属する3Dオブジェクトを描画する。</summary>
    void DrawCrownFinaleWorld();
    /// <summary>最終演出中の画面UIを描画する。</summary>
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
    std::unique_ptr<engine::Sprite> lockOnSprite_;
    bool isDrawLockOn_ = false;
    std::unique_ptr<Skybox> skybox_;
    uint32_t skyboxTextureHandle_ = 0;

    std::unique_ptr<engine::Object3d> animatedCube_;

    /// <summary>ゴール時の王冠・カメラ・結果UIの演出パラメータをまとめる。</summary>
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
    engine::Object3d* stageEntryGate_ = nullptr;
    engine::Vector3 stageEntryDirection_ = {1.0f, 0.0f, 0.0f};
    engine::Vector3 stageEntryCameraStartEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 stageEntryCameraStartTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 stageEntryCameraFocusEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 stageEntryCameraFocusTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 stageEntryCameraRestoreEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 stageEntryCameraRestoreTarget_ = {0.0f, 0.0f, 0.0f};
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
    engine::Object3d* arenaBossIntroBoss_ = nullptr;
    engine::Object3d* arenaBossIntroGate_ = nullptr;
    engine::Vector3 arenaBossIntroGameplayEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossIntroGameplayTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossIntroRestoreEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossIntroRestoreTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossIntroGateEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossIntroGateTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossIntroBossEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossIntroBossTarget_ = {0.0f, 0.0f, 0.0f};

    // --- ステージ3 ボス撃破・王冠落下演出 ---
    bool arenaBossRewardActive_ = false;
    bool arenaBossRewardHadPlayerControl_ = true;
    bool arenaBossRewardCinemaBarOverrideActive_ = false;
    float arenaBossRewardTimer_ = 0.0f;
    float arenaBossRewardBaseFov_ = 0.45f;
    float arenaBossRewardCinemaBarBaseHeight_ = 0.0f;
    engine::Object3d* arenaBossRewardObject_ = nullptr;
    engine::Vector3 arenaBossRewardGameplayEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossRewardGameplayTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossRewardFocusEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 arenaBossRewardFocusTarget_ = {0.0f, 0.0f, 0.0f};

    enum class GoalPresentationState { Inactive, Celebrating, ReadyToReturn, Returning };
    GoalPresentationState goalPresentationState_ = GoalPresentationState::Inactive;
    float goalPresentationTimer_ = 0.0f;
    float goalStarEmitTimer_ = 0.0f;
    float goalBurstEmitTimer_ = 0.0f;
    float goalCrownIdleTime_ = 0.0f;
    float goalCrownSparkleTimer_ = 0.0f;
    size_t goalCrownSparklePatternIndex_ = 0;
    engine::Vector3 goalCrownPosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalPlayerBasePosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalPlayerBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 goalPlayerBaseRotation_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalPlayerPosePosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalMoveForward_ = {0.0f, 0.0f, 1.0f};
    engine::Vector3 goalMoveRight_ = {1.0f, 0.0f, 0.0f};
    engine::Vector3 goalCrownBasePosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCrownBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 goalCrownBaseRotation_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCrownSpringPosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCrownSpringVelocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCrownSpringRotation_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCrownSpringRotationVelocity_ = {0.0f, 0.0f, 0.0f};
    bool goalCrownSpringInitialized_ = false;
    engine::Vector3 goalCameraGameplayEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCameraGameplayTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCameraFocusEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCameraFocusTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCameraLandingEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCameraLandingTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCameraJumpEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCameraJumpTarget_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCameraResultEye_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 goalCameraResultTarget_ = {0.0f, 0.0f, 0.0f};
    float goalCameraGameplayFov_ = 0.45f;
    bool goalSavedPlayerControlActive_ = true;
    bool goalPlayerSnapshotValid_ = false;
    bool goalCrownSnapshotValid_ = false;
    bool goalCameraSnapshotValid_ = false;
    bool goalReturnFadeStarted_ = false;
    bool goalLandingCuePlayed_ = false;
    bool goalResultCuePlayed_ = false;
    bool goalEditorPreviewMode_ = false;
    engine::Object3d* goalCrownObject_ = nullptr;
    Camera* goalLockedPrimaryCamera_ = nullptr;
    Camera* goalLockedSecondaryCamera_ = nullptr;
    GoalClearPlayerAnimator goalClearPlayerAnimator_;
    CinematicSequence goalCinematicSequence_;
    CinematicPlayer goalCinematicPlayer_;
    bool goalCinematicTimelineLoaded_ = false;
    GoalPresentationTuning goalPresentationTuning_;
    std::unique_ptr<Camera> goalPresentationCamera_;
    std::unique_ptr<engine::Sprite> goalOverlayFlash_;
    std::unique_ptr<engine::Sprite> goalOverlayReturnText_;
    std::array<std::unique_ptr<engine::Sprite>, 10> goalOverlayStageClearLetters_;
    std::array<std::unique_ptr<engine::Sprite>, 10> goalOverlaySparkles_;

    // HUD の基準値。演出で拡縮しても戻せるよう保存する。
    struct HudSpriteState {
        engine::Sprite* sprite = nullptr;
        engine::Vector2 basePosition = {0.0f, 0.0f};
        engine::Vector2 baseSize = {0.0f, 0.0f};
        engine::Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
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
        std::unique_ptr<engine::Sprite> sprite;
        engine::Vector2 start = {0.0f, 0.0f};
        engine::Vector2 control = {0.0f, 0.0f};
        engine::Vector2 end = {0.0f, 0.0f};
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
    std::unique_ptr<engine::Object3d> lifeLostSlimeObject_;
    std::unique_ptr<engine::Object3d> lifeLostStunObject_;

    // --- オーバーレイ ---
    std::unique_ptr<ControlsGuideOverlay> controlsGuideOverlay_;
    std::unique_ptr<PauseMenuOverlay> pauseMenuOverlay_;
    // ポーズから能力ページを開いた間だけ、Tab/Backでポーズ内メニューへ移れる。
    bool pauseGuideSession_ = false;
    bool pausePageSliding_ = false;
    bool pausePageToMenu_ = true;
    float pausePageSlideTime_ = 0.0f;
    std::unique_ptr<SettingsMenuOverlay> settingsOverlay_;
    std::unique_ptr<SaveIndicatorOverlay> saveIndicatorOverlay_;

    /// <summary>ステージ中に使うHUDスプライトと初期表示状態を用意する。</summary>
    void InitializeGameplayHUD();

    /// <summary>選択中のステージ設定を使い、シーンの基幹システムを初期化します。</summary>
    /// <param name="currentStage">読み込むステージの設定。</param>
    void InitializeCoreSystems(const StageData& currentStage);
    /// <summary>段階初期化で2D・3D・粒子の描画共通基盤を用意する。</summary>
    /// <returns>描画基盤の準備が完了した段階でtrue。</returns>
    bool InitializeRenderCommons();

    /// <summary>ゲームロジック用のマネージャーとシーン内サービスを用意する。</summary>
    void InitializeGameplaySystems();
    /// <summary>ObjectManagerとステージObjectの実行基盤を用意する。</summary>
    void InitializeGameplayObjectSystems();
    /// <summary>GPU粒子のプリセットと描画用テクスチャを準備する。</summary>
    void InitializeGameplayParticleRuntime();
    /// <summary>破片演出の実行環境を初期化する。</summary>
    void InitializeGameplayDebrisRuntime();
    /// <summary>ステージの空を描くスカイボックスを初期化する。</summary>
    void InitializeGameplaySkybox();

    /// <summary>ステージ設定に応じた描画状態を適用します。</summary>
    /// <param name="currentStage">描画設定の元になるステージ。</param>
    void ApplyGameplayRenderState(const StageData& currentStage);

    /// <summary>ステージのObjectとSpriteを読み込み、表示用の状態を整える。</summary>
    void LoadCurrentStageContent(const StageData& currentStage);
    /// <summary>現在のステージに置かれた3Dオブジェクトを読み込む。</summary>
    void LoadCurrentStageObjects(const StageData& currentStage);
    /// <summary>Sprite配置とステージの視点設定を読み込む。</summary>
    void LoadCurrentStageSpritesAndView(const StageData& currentStage);

    /// <summary>ポーズ、操作ガイド、設定などの画面オーバーレイを用意する。</summary>
    void InitializeGameplayOverlays();
    /// <summary>保存済みのクリア状況をステージ内の王冠表示へ反映する。</summary>
    void ApplyGoalCrownState();
    /// <summary>保存済みのスターコイン取得状況をステージに反映する。</summary>
    void ApplyStageStarCoinState();
    /// <summary>ゴール王冠の待機中の動きを進める。</summary>
    void UpdateGoalCrownIdleAnimation(float deltaTime);

    // 保存されたチェックポイントからの復帰、またはステージ入場を開始します。
    /// <summary>復帰演出が必要なときだけ開始します。</summary>
    /// <returns>復帰演出を開始した場合は true。</returns>
    bool StartRespawnIrisInIfNeeded();

    /// <summary>ステージ入場演出の開始条件を確認し、成立したら開始します。</summary>
    /// <returns>入場演出を開始した場合は true。</returns>
    bool StartStageEntryPresentation();
    /// <summary>入口ゲートから出現する演出を更新する。</summary>
    void UpdateStageEntryPresentation(float deltaTime);
    /// <summary>入口ゲート演出を終え、通常操作とカメラを復元する。</summary>
    void FinishStageEntryPresentation();
    /// <summary>現在のステージで入場演出に使うゲートを探す。</summary>
    engine::Object3d* FindStageEntryGate() const;

    /// <summary>アリーナボスの登場ムービーを更新する。</summary>
    void UpdateArenaBossIntro(float deltaTime);
    /// <summary>ボス登場ムービーを終了し、対象参照と操作制限を整理する。</summary>
    void FinishArenaBossIntro();
    /// <summary>ボス撃破後の報酬登場演出を更新する。</summary>
    void UpdateArenaBossDefeatReward(float deltaTime);
    /// <summary>報酬登場演出を終了し、保持した対象参照を解除する。</summary>
    void FinishArenaBossDefeatReward();
    /// <summary>Editorからアニメーションを確認するためのプレビュー状態を用意する。</summary>
    void InitializeDebugAnimationPreview();

    /// <summary>シーンが所有する描画・音声・演出リソースを終了時に解放します。</summary>
    void FinalizeGameplayResources();

#ifdef USE_IMGUI
    /// <summary>Editorで指定した敵能力をプレイヤーへ適用する。</summary>
    void ApplyDebugPlayerMorph(const char* enemyType);
    /// <summary>Editorで適用したプレイヤーの能力状態を解除する。</summary>
    void ClearDebugPlayerMorph();
    /// <summary>フレーム更新の開始時に実行するデバッグ移動先を予約します。</summary>
    /// <param name="destination">予約する移動先。</param>
    void QueueDebugTeleport(DebugTeleportDestination destination);

    /// <summary>予約済みのデバッグ移動をゲーム更新の開始時に実行する。</summary>
    void ProcessPendingDebugTeleport();
    /// <summary>名前でデバッグ移動先のObjectを探す。</summary>
    engine::Object3d* FindDebugObjectByName(const char* objectName) const;

    /// <summary>アンカーを基準にプレイヤーを移動し、指定オブジェクトへ向けます。</summary>
    /// <param name="anchorName">移動基準となるオブジェクト名。</param>
    /// <param name="anchorOffset">基準位置からの移動量。</param>
    /// <param name="facingObjectName">向きの基準となるオブジェクト名。</param>
    /// <param name="destinationLabel">デバッグ表示に使う移動先名。</param>
    bool TeleportPlayerToDebugTarget(const char* anchorName, const engine::Vector3& anchorOffset, const char* facingObjectName,
                                     const char* destinationLabel);
#endif

    int loadingInitializePhase_ = 0;
    size_t loadingInitializeItemIndex_ = 0;
    size_t loadingInitializeCompletedUnits_ = 0;
    size_t loadingInitializeTotalUnits_ = 1;
    bool loadingStageObjectsStarted_ = false;
    int loadingRenderCommonPhase_ = 0;

    // ゴール処理中は通常のゲーム更新を止め、演出側で時間を進めます。
    /// <summary>ゴール中の保存、入力、演出を処理し、通常のゲーム更新を止める。</summary>
    /// <param name="deltaTime">演出更新に使う経過秒数。ゴール中は0へ変更する。</param>
    /// <returns>ゴール処理がこのフレームを引き受けた場合はtrue。</returns>
    bool HandleGoalClear(float& deltaTime);

    /// <summary>ゴール結果を表示するSpriteオーバーレイを用意する。</summary>
    void InitializeGoalPresentationOverlay();
    /// <summary>ゴール演出のタイムラインと各イベントを準備する。</summary>
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
    void HandleGoalCinematicAnimation(engine::Object3d* target, const CinematicAnimationClipData& clip, float localTime,
                                      bool isPreview);

    /// <summary>ゴール演出のシグナルを受け取り、終了待ち状態へ進めます。</summary>
    /// <param name="target">シーケンスが指定した対象。現在のシグナルでは使用しません。</param>
    /// <param name="signal">発火したシグナル。</param>
    /// <param name="isPreview">Editor プレビューからの呼び出しかどうか。現在のシグナルでは使用しません。</param>
    void HandleGoalCinematicSignal(engine::Object3d* target, const CinematicSignalMarker& signal, bool isPreview);
    /// <summary>ゴール演出の調整値を保存ファイルから読み込む。</summary>
    void LoadGoalPresentationTuning();
    /// <summary>現在のゴール演出調整値を保存する。</summary>
    void SaveGoalPresentationTuning() const;
    /// <summary>ゴール演出調整値を有効な範囲へ補正する。</summary>
    void SanitizeGoalPresentationTuning();
    /// <summary>ゴール演出の調整UIをEditorへ描く。</summary>
    void DrawGoalPresentationEditor();
    /// <summary>現在のステージに置かれたゴール王冠を探す。</summary>
    engine::Object3d* FindGoalCrownObject() const;
    /// <summary>Editor内でゴール演出の試写を開始する。</summary>
    void StartGoalPresentationPreview();
    /// <summary>Editor内のゴール演出試写を終了して状態を戻す。</summary>
    void StopGoalPresentationPreview();

    /// <summary>プレイヤー、王冠、カメラ、UIを含むゴール演出を更新する。</summary>
    void UpdateGoalPresentation(float deltaTime);
    /// <summary>クリア時のプレイヤーの祝福モーションを更新する。</summary>
    void UpdateGoalPlayerCelebration(float deltaTime);
    /// <summary>クリア演出中の王冠の移動と着地を更新する。</summary>
    void UpdateGoalCrownMotion(float deltaTime);
    /// <summary>ゴール演出の時刻に合わせてエフェクトを発生させる。</summary>
    void EmitGoalPresentationEffects(float deltaTime);
    /// <summary>ゴール演出で使用するカメラの初期構図を設定する。</summary>
    void SetupGoalPresentationCamera();
    /// <summary>ゴール演出の進行に合わせてカメラ構図を更新する。</summary>
    void UpdateGoalPresentationCamera();
    /// <summary>ゴール演出中の手動カメラ操作を抑制する。</summary>
    void LockGoalPresentationCameraInput();
    /// <summary>ゴール演出が変更したカメラ操作状態を復元する。</summary>
    void RestoreGoalPresentationCameraInput();
    /// <summary>クリア結果の画面オーバーレイを更新する。</summary>
    void UpdateGoalPresentationOverlay();
    /// <summary>クリア結果の画面オーバーレイを描画する。</summary>
    void DrawGoalPresentationOverlay();
    /// <summary>保存後にステージ選択へ戻る遷移を要求する。</summary>
    void RequestGoalReturnToSelect();

    /// <summary>通常プレイ中のポストエフェクト状態を更新する。</summary>
    void UpdatePostEffectState(float deltaTime);
    /// <summary>ロックオン対象と有効カメラの追従状態を更新する。</summary>
    void UpdateLockOnAndCamera(float deltaTime);
    /// <summary>ロックオン対象を投影し、マーカーの位置と表示を更新します。</summary>
    /// <param name="camera">ワールド座標を投影するカメラ。</param>
    /// <param name="target">現在のロックオン対象。</param>
    /// <param name="deltaTime">マーカー更新に使う経過秒数。</param>
    void UpdateLockOnSprite(Camera* camera, engine::Object3d* target, float deltaTime);
    /// <summary>ステージ内のゲームシステムと演出管理器を更新する。</summary>
    void UpdateSceneSystems(float deltaTime);
    /// <summary>Editorで使うエフェクト操作のショートカット入力を処理する。</summary>
    void UpdateEffectDebugShortcuts();
    /// <summary>残機、コインなど通常プレイ中のHUDを更新する。</summary>
    void UpdateGameplayHUD(float deltaTime);
    /// <summary>スターコインの取得数と表示状態をHUDへ反映する。</summary>
    void UpdateStageStarHUD(float deltaTime, bool visible);
    /// <summary>プリズムボスの状態表示をHUDへ反映する。</summary>
    void UpdatePrismBossHUD(float deltaTime);
    /// <summary>残機減少の画面演出を進める。</summary>
    void UpdateLifeLostPresentation(float deltaTime);

    /// <summary>操作ガイドを更新し、このフレームの入力を消費したか返す。</summary>
    bool HandleControlsGuideOverlay(float deltaTime);
    /// <summary>ポーズメニューと操作ガイドのページ移動を開始する。</summary>
    /// <param name="toMenu">ポーズメニューへ移る場合はtrue。</param>
    void BeginPausePageSlide(bool toMenu);
    /// <summary>ポーズ画面のページ移動アニメーションを進める。</summary>
    void UpdatePausePageSlide(float deltaTime);
    /// <summary>ポーズ中の入力と設定画面を処理し、通常更新を止めるか返す。</summary>
    bool HandlePauseOverlay(float deltaTime);
    /// <summary>ポーズを開く入力がこのフレームで発生したか返す。</summary>
    bool IsPauseOpenTriggered() const;

    /// <summary>残機やコインの通常HUDを描画する。</summary>
    void DrawGameplayHUD();
    /// <summary>スターコイン取得状態のHUDを描画する。</summary>
    void DrawStageStarHUD();
    /// <summary>プリズムボスの状態HUDを描画する。</summary>
    void DrawPrismBossHUD();
    /// <summary>残機減少の画面オーバーレイを描画する。</summary>
    void DrawLifeLostPresentation();
    /// <summary>残機減少のワールド演出に使うObjectを用意する。</summary>
    void InitializeLifeLostPresentationObjects();
    /// <summary>残機減少のワールド演出の位置と時間を更新する。</summary>
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
                                         const engine::Vector2& position, const engine::Vector2& size, const engine::Vector2& anchor,
                                         const engine::Vector4& color);
    /// <summary>保存した基準位置と色を使ってHUDスプライトを描く。</summary>
    void DrawGameplayHUDSprite(const HudSpriteState& state);
    /// <summary>指定SpriteがゲームプレイHUDに属するか判定する。</summary>
    bool IsGameplayHUDSprite(const engine::Sprite* sprite) const;
    /// <summary>二桁の HUD 数字を、指定した右端位置に合わせて更新します。</summary>
    /// <param name="digits">更新する十の位と一の位のスプライト。</param>
    /// <param name="value">表示する数値。</param>
    /// <param name="rightAlignedPosition">数字表示の右端座標。</param>
    /// <param name="digitHeight">表示する数字の高さ。</param>
    /// <param name="color">数字の描画色。</param>
    /// <param name="visible">数字を表示するかどうか。</param>
    void SetGameplayHUDNumber(std::array<HudSpriteState, 2>& digits, int value, const engine::Vector2& rightAlignedPosition,
                              float digitHeight, const engine::Vector4& color, bool visible);

    /// <summary>取得したスターコインが画面のHUDへ飛ぶ演出を開始する。</summary>
    void StartStageStarHUDCollectEffect(int starIndex, const engine::Vector3& worldPosition);
    /// <summary>現在のカメラでワールド位置を画面座標へ投影する。</summary>
    engine::Vector2 ProjectWorldToScreen(const engine::Vector3& worldPosition) const;
    /// <summary>HUD表示対象のプリズムボスを探す。</summary>
    class EnemyPrismSlime* FindPrismBossForHUD();
    /// <summary>HUD表示対象のマグマボスを探す。</summary>
    class EnemyMagmaSlime* FindMagmaBossForHUD();
    /// <summary>HUD表示対象の最終ボスを探す。</summary>
    class EnemyFalseKingSlime* FindFalseKingBossForHUD();

    /// <summary>指定Objectが現在のカメラの視錐台内で描画対象か判定する。</summary>
    bool IsVisible(engine::Object3d* obj);
};
