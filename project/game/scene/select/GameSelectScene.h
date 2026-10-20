#pragma once
#include "BaseScene.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "game/system/progress/GameDataManager.h"
#include "MeshRenderer.h"
#include "game/system/scene/ObjectManager.h"
#include "engine/utility/math/Math.h"
#include "game/system/cinematic/CinematicPlayer.h"
#include "game/system/cinematic/CinematicSequence.h"
#include <array>
#include <memory>
#include <vector>

// 前方宣言
class DirectXCommon;
namespace engine { class InputManager; }
class SceneManager;
class LevelLoader;
class LockOnSystem;
class GameRule;
class GimmickStageGate;
class PauseMenuOverlay;
class SettingsMenuOverlay;
namespace engine { class Object3dCommon; }
namespace engine { class ParticleCommon; }
namespace engine { class ParticleSystem; }
class Player;
class Skybox;
namespace engine { class SpriteCommon; }
class Text;

/// <summary>
/// ステージ選択シーン。ステージゲート、解放演出、王冠/スター/コイン HUD を扱う。
/// </summary>
class GameSelectScene : public BaseScene, public ISceneGameplayAccess {
  public:
    /// <summary>ステージ選択画面の初期状態を作る。</summary>
    GameSelectScene();

    /// <summary>ステージ選択画面が保持する資源を破棄する。</summary>
    ~GameSelectScene() override;

    /// <summary>通常起動と非同期ロードに共通の段階でシーンを初期化する。</summary>
    void Initialize() override;

    /// <summary>段階的なロード初期化を開始する。</summary>
    void BeginLoadingInitialize() override;

    /// <summary>初期化を一段進める。</summary>
    /// <returns>すべての初期化段階が終わった場合はtrue。</returns>
    bool InitializeLoadingStep() override;

    /// <summary>段階的な初期化の進捗を返す。</summary>
    float GetLoadingInitializeProgress() const override;

    /// <summary>シーンが表示対象になった時点の状態を整える。</summary>
    void OnActivated() override;

    /// <summary>非同期で用意するステージ選択画面の素材を列挙する。</summary>
    SceneLoadManifest BuildAsyncLoadManifest() const override;

    /// <summary>シーンが保持する描画資源と参照を解放する。</summary>
    void Finalize() override;

    /// <summary>プレイヤー、ゲート、解放演出、帰還演出を更新する。</summary>
    void Update(float deltaTime) override;

    /// <summary>ステージ選択画面のUI状態を更新する。</summary>
    void UpdateUI();

    /// <summary>ステージ選択画面の3Dオブジェクトを描画する。</summary>
    void Draw() override;

    /// <summary>ゲート名、収集状況、操作案内などのUIを描画する。</summary>
    void DrawUI() override;

    /// <summary>3Dオブジェクトの影を描画する。</summary>
    void DrawShadow() override;

    /// <summary>Editor用の調整UIを描画する。</summary>
    void DrawImGui() override;

    // --- BaseScene インターフェース ---
    /// <summary>シーンが所有する3Dオブジェクトの一覧を返す。</summary>
    const std::vector<std::unique_ptr<engine::Object3d>>& GetObjects() const override { return objectManager_->GetObjects(); }

    /// <summary>所有権を受け取って3Dオブジェクトを即時登録する。</summary>
    engine::Object3d* AddObjectImmediate(std::unique_ptr<engine::Object3d> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }

    /// <summary>指定オブジェクトの所有権をシーンから取り出す。</summary>
    std::unique_ptr<engine::Object3d> TakeObjectImmediate(engine::Object3d* object) override { return objectManager_->TakeObjectImmediate(object); }

    /// <summary>所有権を受け取って3Dオブジェクトの追加を予約する。</summary>
    void AddObject(std::unique_ptr<engine::Object3d> object) override { objectManager_->AddObject(std::move(object)); }

    /// <summary>指定オブジェクトの削除を予約する。</summary>
    void RequestRemoveObject(engine::Object3d* object) override { objectManager_->RequestRemove(object); }

    /// <summary>シーンが所有するSpriteの一覧を返す。</summary>
    const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const override { return sprites_; }

    /// <summary>3D描画基盤、Skybox、Sprite描画基盤、粒子システムをそれぞれ返す。</summary>
    /// <remarks>いずれも所有権を移さない。</remarks>
    engine::Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }

    Skybox* GetSkybox() override { return skybox_.get(); }

    engine::SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }

    engine::ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    /// <summary>シーン内のプレイヤー参照を取得・設定する。所有権は移さない。</summary>
    Player* GetPlayer() const override { return player_; }

    void SetPlayer(Player* player) override { player_ = player; }

    /// <summary>デバッグ設定の変更を表示中のゲートとHUDに反映する。</summary>
    void RefreshDebugStageStates();

  private:
    /// <summary>Editorが編集するSprite所有リストを返す。</summary>
    std::vector<std::unique_ptr<engine::Sprite>>* GetMutableSprites() override { return &sprites_; }

    /// <summary>近くのゲート選択と入場入力を更新する。</summary>
    void UpdateStageGateSelection(float deltaTime);

    /// <summary>解放状況を各ステージゲートの表示へ反映する。</summary>
    void ApplyStageGateStates();

    /// <summary>指定番号のステージが解放済みか返す。</summary>
    bool IsStageUnlocked(int stageIndex) const;

    /// <summary>選択中のステージへの入場遷移を要求する。</summary>
    void EnterSelectedStage();

    /// <summary>ゲートへ入るカメラ演出と画面遷移を開始します。</summary>
    /// <param name="stageIndex">入場するステージの番号。</param>
    void StartGateEntryCinematic(int stageIndex);

    /// <summary>ゲート入場のカメラ演出を進める。</summary>
    void UpdateGateEntryCinematic(float deltaTime);

    /// <summary>入場演出の上下の画面帯を更新する。</summary>
    void UpdateGateCinematicBars(float deltaTime);

    /// <summary>入場演出の画面帯を初期状態へ戻す。</summary>
    void ResetGateCinematicBars();

    /// <summary>入場演出前のゲート材質を保存する。</summary>
    void CaptureGateEntryMaterialState(engine::Object3d* rootObject);

    /// <summary>入場演出の進行率に合わせてゲートのDissolve材質を更新する。</summary>
    void ApplyGateEntryDissolveMaterial(float progress, const engine::Vector3& gatePosition, const engine::Vector3& direction);

    /// <summary>入場演出前に保存したゲート材質へ戻す。</summary>
    void RestoreGateEntryMaterialState();

    /// <summary>クリア後のゲート帰還演出を、保存済みの要求から開始します。</summary>
    /// <param name="request">戻り元ステージと今回獲得した報酬。</param>
    /// <returns>演出を開始できた場合は true。</returns>
    bool StartStageReturnPresentation(const GameDataManager::StageSelectReturnPresentation& request);

    /// <summary>クリア後のゲート帰還演出を進める。</summary>
    void UpdateStageReturnPresentation(float deltaTime);

    /// <summary>ゲート帰還演出を終了し、通常の選択画面へ戻す。</summary>
    void FinishStageReturnPresentation();

    /// <summary>最寄りゲートの操作案内と入場可否を表示へ反映する。</summary>
    void UpdateGatePrompt(engine::Object3d* nearestGate, bool canEnterGate, float deltaTime);

    /// <summary>指定オブジェクトがステージゲートか判定する。</summary>
    bool IsStageGateObject(const engine::Object3d* object) const;

    /// <summary>ゲートの名前などからステージ番号を返す。</summary>
    int GetStageGateIndex(const engine::Object3d* object) const;

    /// <summary>指定ステージ番号のゲートを探す。見つからなければnullptr。</summary>
    engine::Object3d* FindStageGateByIndex(int stageIndex) const;

    /// <summary>プレイヤーに最も近いステージゲートを探す。</summary>
    /// <param name="outDistance">見つかったゲートまでの距離を受け取る任意の出力先。</param>
    engine::Object3d* FindNearestStageGate(float* outDistance) const;

    /// <summary>プレイヤーが指定ゲートの判定範囲に触れているか返す。</summary>
    bool IsPlayerTouchingStageGate(engine::Object3d* gate) const;

    /// <summary>解放条件を満たし、まだ演出していないステージ番号を返す。</summary>
    int FindPendingUnlockStage() const;

    /// <summary>新たに解放されたステージの演出を開始する。</summary>
    void BeginUnlockPresentation();

    /// <summary>ステージ解放演出を一フレーム進める。</summary>
    void UpdateUnlockPresentation(float deltaTime);

    /// <summary>ステージ解放演出を完了し、通常表示へ戻す。</summary>
    void FinishUnlockPresentation(bool markSeen = true);

    /// <summary>演出とデバッグ中断に共通する操作権・視点の復元。</summary>
    void RestoreUnlockPresentationState();

    /// <summary>Director の Signal に対応する単発エフェクトを再生する。</summary>
    void HandleUnlockSignal(const CinematicSignalMarker& signal);
#ifdef USE_IMGUI

    /// <summary>指定ステージの解放演出をデバッグ用に強制開始する。</summary>
    void StartDebugUnlockPresentation(int stageIndex);
#endif

    /// <summary>ステージ選択画面の装飾を更新する。</summary>
    void UpdateStageSelectDecorations(float deltaTime);

    /// <summary>指定ステージへの道の表示を、解放・選択状態に合わせて更新する。</summary>
    void UpdatePathDisplay(int stageIndex, bool active, bool unlocking, float pulse);

    /// <summary>取得済みスターコインの表示を更新する。</summary>
    void UpdateStarCoinDisplays(float deltaTime);

    /// <summary>指定ステージのクリア王冠を探すか生成する。</summary>
    engine::Object3d* EnsureStageClearCrown(int stageIndex);

    /// <summary>ステージごとのクリア王冠の見た目を更新する。</summary>
    void UpdateStageClearCrownDisplays(float deltaTime);

    /// <summary>クリア後に戻った際の王冠獲得演出を開始する。</summary>
    void StartStageClearRewardPresentation(const GameDataManager::StageClearRewardPresentation& request);

    /// <summary>王冠獲得演出の時間と表示を更新する。</summary>
    void UpdateStageClearRewardPresentation(float deltaTime);

    /// <summary>演出対象の王冠の位置と姿勢を更新する。</summary>
    void UpdateStageClearRewardCrown(engine::Object3d* crown, int stageIndex, float deltaTime);

    /// <summary>王冠獲得数をHUDの演出へ反映する。</summary>
    void UpdateStageSelectCrownHudReward();

    /// <summary>HUDに表示する王冠数を返す。</summary>
    int GetDisplayedCrownCount() const;

    /// <summary>指定ステージの王冠表示位置を返す。</summary>
    engine::Vector3 GetStageClearCrownPosition(int stageIndex) const;

    /// <summary>名前が一致するシーン内オブジェクトを返す。見つからなければnullptr。</summary>
    engine::Object3d* FindObjectByName(const std::string& name) const;

    /// <summary>指定ステージの選択画面上のノード位置を返す。</summary>
    engine::Vector3 GetStageNodePosition(int stageIndex) const;

    /// <summary>王冠、スター、コインなど選択画面のHUDを初期化する。</summary>
    void InitializeStageSelectHUD();

    /// <summary>各ステージゲートの名前ラベルを用意する。</summary>
    void InitializeStageGateNameLabels();

    /// <summary>各ステージゲートの名前ラベルの位置と表示を更新する。</summary>
    void UpdateStageGateNameLabels();

    /// <summary>ポーズ画面を更新し、通常シーン更新を止めるべきか返す。</summary>
    bool HandlePauseOverlay(float deltaTime);

    // HUD の Sprite 名と基準位置・サイズ・色。Sprite はシーン側が所有する。
    struct StageSelectHudSprite {
        std::string name;
        engine::Vector2 basePosition = {0.0f, 0.0f};
        engine::Vector2 baseSize = {0.0f, 0.0f};
        engine::Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    /// <summary>配置JSONの名前でHUD Spriteを結び、演出前の基準値を保存する。</summary>
    StageSelectHudSprite BindStageSelectHUDSprite(const std::string& name);

    /// <summary>HUD Sprite を現在のシーン所有一覧から取得する。</summary>
    engine::Sprite* ResolveStageSelectHUDSprite(const StageSelectHudSprite& state);

    /// <summary>3桁のHUD数字Spriteへ値、色、表示状態を反映する。</summary>
    void SetStageSelectHUDNumber(std::array<StageSelectHudSprite, 3>& digits, int value, bool visible);

  private:
    // Game/Frameworkが所有する描画・入力基盤への非所有参照。
    DirectXCommon* dxCommon_ = nullptr;
    engine::InputManager* inputManager_ = nullptr;

    // 配置読込、照準、Object一覧をこのシーンが所有する。
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<LockOnSystem> lockOnSystem_ = nullptr;
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;

    // 3D・2D・CPU粒子の描画共通基盤をシーンの寿命で管理する。
    std::unique_ptr<engine::Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<engine::SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<engine::ParticleCommon> particleCommon_ = nullptr;

    // 配置SpriteとCPU粒子の実行インスタンスを所有する。
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;
    std::unique_ptr<engine::ParticleSystem> particleSystem_ = nullptr;
    std::unique_ptr<Text> debugText_;
    std::unique_ptr<GameRule> gameRule_;

    // ObjectManager側のプレイヤーを借用する。
    Player* player_ = nullptr;

    // BGMの二重再生防止と、粒子演出用SEの登録ハンドル。
    bool isBGMPlaying_ = false;
    uint32_t particleSEHandle_ = 0;

    // 点光源・スポット光のGPUバッファを所有し、Dataポインタから内容を更新する。
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    MeshRenderer::PointLight* pointLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    MeshRenderer::SpotLight* spotLightData_ = nullptr;

    // GPU粒子画像と、ロックオン・ゲート操作案内の表示部品。
    uint32_t gpuParticleTexHandle_ = 0;
    std::unique_ptr<engine::Sprite> lockOnSprite_;
    std::unique_ptr<engine::Sprite> gatePromptSprite_;
    bool isDrawLockOn_ = false;
    float gatePromptTimer_ = 0.0f;

    // ステージ選択の空と、TextureManagerから参照する画像ハンドル。
    std::unique_ptr<Skybox> skybox_;
    uint32_t skyboxTextureHandle_ = 0;

    // エディタでアニメーションを確認するための補助Object。
    std::unique_ptr<engine::Object3d> animatedCube_;

    // --- ステージゲート選択状態 ---
    // 選択中と前回のゲートを比較し、距離・決定待ち時間・遷移中状態を管理する。
    int selectedStageIndex_ = 0;
    int previousSelectedStageIndex_ = -1;
    float gateSelectRadius_ = 8.0f;
    float stageDecisionCooldown_ = 0.0f;
    bool isChangingStage_ = false;

    // 入場演出で変える材質の復元用コピー。Object は借用し、演出終了後に元の値を戻す。
    struct GateEntryMaterialSnapshot {
        engine::Object3d* object = nullptr;
        engine::Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        int32_t materialType = 0;
        float emissive = 1.0f;
        float portalClipEnabled = 0.0f;
        float portalClipProgress = 0.0f;
        engine::Vector3 portalClipCenter = {0.0f, 0.0f, 0.0f};
        float portalClipEdgeWidth = 0.0f;
        engine::Vector3 portalClipNormal = {0.0f, 0.0f, 1.0f};
        float portalClipDissolve = 0.0f;
        engine::Vector4 portalClipColor = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    // 入場ムービーの時刻と、遷移が確定したステージ番号。
    bool gateEntryCinematicActive_ = false;
    float gateEntryCinematicTimer_ = 0.0f;
    int gateEntryPendingStageIndex_ = -1;

    // 入場ゲートは借用する。プレイヤーの経路とカメラの始終点だけ保持する。
    engine::Object3d* gateEntryTargetGate_ = nullptr;

    engine::Vector3 gateEntryStartPlayerPosition_{};
    engine::Vector3 gateEntryTargetPlayerPosition_{};
    engine::Vector3 gateEntrySurfacePlayerPosition_{};
    engine::Vector3 gateEntryInsidePlayerPosition_{};
    engine::Vector3 gateEntryDirection_{0.0f, 0.0f, 1.0f};

    engine::Vector3 gateEntryCameraStartEye_{};
    engine::Vector3 gateEntryCameraStartTarget_{};
    engine::Vector3 gateEntryCameraEndEye_{};
    engine::Vector3 gateEntryCameraEndTarget_{};

    engine::Vector3 gateEntryStartPlayerScale_{1.0f, 1.0f, 1.0f};
    bool gateEntryHadPlayerControl_ = true;

    // 入場時の火花・霧・光の発生間隔を別々の時計で管理する。
    float gateEntrySparkTimer_ = 0.0f;
    float gateEntryMistTimer_ = 0.0f;
    float gateEntryGlintTimer_ = 0.0f;
    std::vector<GateEntryMaterialSnapshot> gateEntryMaterialSnapshots_;

    // クリア後の帰還ムービー。帰還ゲートと入場前の操作権は終了時に復元する。
    bool gateReturnPresentationActive_ = false;
    float gateReturnPresentationTimer_ = 0.0f;

    int gateReturnStageIndex_ = -1;
    engine::Object3d* gateReturnTargetGate_ = nullptr;

    engine::Vector3 gateReturnDirection_{0.0f, 0.0f, 1.0f};

    engine::Vector3 gateReturnInsidePlayerPosition_{};
    engine::Vector3 gateReturnCenterPlayerPosition_{};
    engine::Vector3 gateReturnSurfacePlayerPosition_{};
    engine::Vector3 gateReturnEndPlayerPosition_{};

    engine::Vector3 gateReturnPlayerScale_{1.0f, 1.0f, 1.0f};

    engine::Vector3 gateReturnCameraStartEye_{};
    engine::Vector3 gateReturnCameraStartTarget_{};
    engine::Vector3 gateReturnCameraEndEye_{};
    engine::Vector3 gateReturnCameraEndTarget_{};

    engine::Vector3 gateReturnCameraRestoreEye_{};
    engine::Vector3 gateReturnCameraRestoreTarget_{};

    bool gateReturnHadPlayerControl_ = true;

    // 着地と立ち直りの演出を一度だけ再生するためのフラグ。
    bool gateReturnLandingImpulsePlayed_ = false;
    bool gateReturnRecoveryImpulsePlayed_ = false;

    // ゲートムービー中だけ変更する上下黒帯の補間状態と元の高さ。
    bool gateCinematicBarOverrideActive_ = false;
    float gateCinematicBarBlend_ = 0.0f;
    float gateCinematicBarBaseHeight_ = 0.0f;

    // ステージクリアの報酬表示を、ゲート帰還後まで持ち越す要求。
    GameDataManager::StageClearRewardPresentation pendingStageClearRewardPresentation_;

    // 新ステージ解放の対象、Director の再生時計と JSON の節目。
    int unlockingStageIndex_ = -1;
    float unlockPresentationTimer_ = 0.0f;
    float unlockParticleTimer_ = 0.0f;
    CinematicSequence unlockSequence_;
    CinematicPlayer unlockPlayer_;
    bool unlockTimelineLoaded_ = false;
    float unlockKeyAppearTime_ = 0.0f;
    float unlockKeyApproachEndTime_ = 0.0f;
    float unlockLockReleaseTime_ = 0.0f;
    float unlockKeyHideTime_ = 0.0f;
    float unlockLockHideTime_ = 0.0f;
    float unlockGateActivationTime_ = 0.0f;
    float unlockGateActivationEndTime_ = 0.0f;
    bool unlockPresentationInitialized_ = false;
    bool unlockPresentationDebugReplay_ = false;
    bool unlockHadPlayerControl_ = true;
    int unlockDebugPreviousSelectedStageIndex_ = -1;

    // 解放演出で操作するゲート・鍵・錠は配置済みObjectを借用する。
    engine::Object3d* unlockTargetGate_ = nullptr;
    engine::Object3d* unlockKeyObject_ = nullptr;
    engine::Object3d* unlockLockObject_ = nullptr;

    // 粒子の発生位置には移動後ではなく、開始時の錠の位置を使う。
    engine::Vector3 unlockLockBasePosition_{};

    // 解放前の視点。ムービー中の視点は Camera Track が管理する。
    engine::Vector3 unlockCameraStartEye_{};
    engine::Vector3 unlockCameraStartTarget_{};

    // ステージ選択画面全体で進む演出時刻。
    float stageSelectTime_ = 0.0f;

    // --- 王冠獲得数の加算演出 ---
    // 保存値の変更前後を保持し、加算表示と粒子を時刻で進める。
    bool crownCountPresentationActive_ = false;
    bool crownCountPresentationImpactDone_ = false;
    int crownCountPresentationStageIndex_ = -1;
    int crownCountPresentationFrom_ = 0;
    int crownCountPresentationTo_ = 0;
    float crownCountPresentationTimer_ = 0.0f;
    float crownCountPresentationParticleTimer_ = 0.0f;

    // --- セレクト HUD ---
    // 各Spriteは配置データの表示部品を借用し、数字は桁ごとに保持する。
    StageSelectHudSprite stageSelectCrownIcon_;
    StageSelectHudSprite stageSelectCrownXIcon_;
    std::array<StageSelectHudSprite, 3> stageSelectCrownDigits_;
    StageSelectHudSprite stageSelectStarIcon_;
    StageSelectHudSprite stageSelectStarXIcon_;
    std::array<StageSelectHudSprite, 3> stageSelectStarDigits_;
    StageSelectHudSprite stageSelectCoinIcon_;
    StageSelectHudSprite stageSelectCoinXIcon_;
    std::array<StageSelectHudSprite, 3> stageSelectCoinDigits_;

    // ゲート名札は画面座標が毎フレーム変わるため、保存用の sprites_ とは分けて持ちます。
    std::vector<std::unique_ptr<engine::Sprite>> stageGateNameLabels_;
    std::vector<std::array<std::unique_ptr<engine::Sprite>, GameDataManager::kStarCoinsPerStage>> stageGateStarIcons_;

    // ポーズ画面と設定画面の入力・表示状態を独立して所有する。
    std::unique_ptr<PauseMenuOverlay> pauseMenuOverlay_;
    std::unique_ptr<SettingsMenuOverlay> settingsOverlay_;

    // ゲート名札の非表示・フェード開始距離と、ワールド上の表示高さ。
    float gateNameHideDistance_ = 40.0f;
    float gateNameFadeDistance_ = 10.0f;
    float gateNameHeightOffset_ = 1.5f;

    // ロード画面中の段階初期化フェーズと完了/総作業単位。
    int loadingInitializePhase_ = 0;
    size_t loadingInitializeCompletedUnits_ = 0;
    size_t loadingInitializeTotalUnits_ = 9;
};
