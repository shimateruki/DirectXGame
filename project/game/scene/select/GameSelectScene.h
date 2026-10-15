#pragma once
#include "BaseScene.h"
#include "game/system/SceneGameplayAccess.h"
#include "GameDataManager.h"
#include "MeshRenderer.h"
#include "ObjectManager.h"
#include "engine/utility/math/Math.h"
#include <array>
#include <memory>
#include <vector>

// 前方宣言
class DirectXCommon;
class InputManager;
class SceneManager;
class LevelLoader;
class LockOnSystem;
class GameRule;
class GimmickStageGate;
class PauseMenuOverlay;
class SettingsMenuOverlay;
class Object3dCommon;
class ParticleCommon;
class ParticleSystem;
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
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    Skybox* GetSkybox() override { return skybox_.get(); }
    engine::SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

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
    void FinishUnlockPresentation();
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
    /// <summary>指定名のSpriteを探す。見つからなければnullptr。</summary>
    engine::Sprite* FindSpriteByName(const std::string& name) const;
    // HUD の借用 Sprite と基準位置・サイズ・色。表示演出の変化を初期値から計算する。
    struct StageSelectHudSprite {
        engine::Sprite* sprite = nullptr;
        engine::Vector2 basePosition = {0.0f, 0.0f};
        engine::Vector2 baseSize = {0.0f, 0.0f};
        engine::Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    };
    /// <summary>名前で見つけたHUD Spriteと基準配置・色を組にする。</summary>
    StageSelectHudSprite BindStageSelectHUDSprite(const std::string& name, const std::string& texturePath,
                                                  const engine::Vector2& position, const engine::Vector2& size, const engine::Vector2& anchor,
                                                  const engine::Vector4& color);
    /// <summary>3桁のHUD数字Spriteへ値、色、表示状態を反映する。</summary>
    void SetStageSelectHUDNumber(std::array<StageSelectHudSprite, 3>& digits, int value, const engine::Vector4& color,
                                 bool visible);

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

    Player* player_ = nullptr;

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
    std::unique_ptr<engine::Sprite> gatePromptSprite_;
    bool isDrawLockOn_ = false;
    float gatePromptTimer_ = 0.0f;
    std::unique_ptr<Skybox> skybox_;
    uint32_t skyboxTextureHandle_ = 0;

    std::unique_ptr<engine::Object3d> animatedCube_;

    // --- ステージゲート選択状態 ---
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
    bool gateEntryCinematicActive_ = false;
    float gateEntryCinematicTimer_ = 0.0f;
    int gateEntryPendingStageIndex_ = -1;
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
    float gateEntrySparkTimer_ = 0.0f;
    float gateEntryMistTimer_ = 0.0f;
    float gateEntryGlintTimer_ = 0.0f;
    std::vector<GateEntryMaterialSnapshot> gateEntryMaterialSnapshots_;
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
    bool gateReturnLandingImpulsePlayed_ = false;
    bool gateReturnRecoveryImpulsePlayed_ = false;
    bool gateCinematicBarOverrideActive_ = false;
    float gateCinematicBarBlend_ = 0.0f;
    float gateCinematicBarBaseHeight_ = 0.0f;
    GameDataManager::StageClearRewardPresentation pendingStageClearRewardPresentation_;
    int unlockingStageIndex_ = -1;
    float unlockPresentationTimer_ = 0.0f;
    float unlockParticleTimer_ = 0.0f;
    bool unlockPresentationInitialized_ = false;
    bool unlockKeyAppearImpactPlayed_ = false;
    bool unlockTurnImpactPlayed_ = false;
    bool unlockReleaseImpactPlayed_ = false;
    bool unlockGateActivationImpactPlayed_ = false;
    bool unlockPresentationDebugReplay_ = false;
    bool unlockHadPlayerControl_ = true;
    int unlockDebugPreviousSelectedStageIndex_ = -1;
    engine::Object3d* unlockTargetGate_ = nullptr;
    engine::Object3d* unlockKeyObject_ = nullptr;
    engine::Object3d* unlockLockObject_ = nullptr;
    engine::Vector3 unlockKeyBasePosition_{};
    engine::Vector3 unlockKeyBaseRotation_{};
    engine::Vector3 unlockKeyBaseScale_{1.0f, 1.0f, 1.0f};
    engine::Vector3 unlockLockBasePosition_{};
    engine::Vector3 unlockLockBaseRotation_{};
    engine::Vector3 unlockLockBaseScale_{1.0f, 1.0f, 1.0f};
    engine::Vector3 unlockGateDirection_{0.0f, 0.0f, 1.0f};
    engine::Vector3 unlockCameraStartEye_{};
    engine::Vector3 unlockCameraStartTarget_{};
    engine::Vector3 unlockCameraCinematicEye_{};
    engine::Vector3 unlockCameraCinematicTarget_{};
    float stageSelectTime_ = 0.0f;

    // --- 王冠獲得数の加算演出 ---
    bool crownCountPresentationActive_ = false;
    bool crownCountPresentationImpactDone_ = false;
    int crownCountPresentationStageIndex_ = -1;
    int crownCountPresentationFrom_ = 0;
    int crownCountPresentationTo_ = 0;
    float crownCountPresentationTimer_ = 0.0f;
    float crownCountPresentationParticleTimer_ = 0.0f;

    // --- セレクト HUD ---
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
    std::unique_ptr<PauseMenuOverlay> pauseMenuOverlay_;
    std::unique_ptr<SettingsMenuOverlay> settingsOverlay_;
    float gateNameHideDistance_ = 40.0f;
    float gateNameFadeDistance_ = 10.0f;
    float gateNameHeightOffset_ = 1.5f;

    int loadingInitializePhase_ = 0;
    size_t loadingInitializeCompletedUnits_ = 0;
    size_t loadingInitializeTotalUnits_ = 9;
};
