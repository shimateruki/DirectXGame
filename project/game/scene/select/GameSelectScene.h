#pragma once
#include "BaseScene.h"
#include "AudioPlayer.h"
#include "BulletManager.h"
#include "Camera.h"
#include "DebugEditor.h"
#include "Event.h"
#include "GameDataManager.h"
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
#include <GhostRecorder.h>
#include <Skybox.h>
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

/// <summary>
/// ステージ選択シーン。ステージゲート、解放演出、王冠/スター/コイン HUD を扱う。
/// </summary>
class GameSelectScene : public BaseScene {
  public:
    GameSelectScene();
    ~GameSelectScene() override;

    // 通常起動と非同期読み込みで、同じ初期化段階を共有します。
    void Initialize() override;
    void BeginLoadingInitialize() override;
    bool InitializeLoadingStep() override;
    float GetLoadingInitializeProgress() const override;

    void OnActivated() override;
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    void Finalize() override;

    void Update(float deltaTime) override;
    void UpdateUI();
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;
    void DrawImGui() override;

    // --- BaseScene インターフェース ---
    const std::vector<std::unique_ptr<Object3d>>& GetObjects() const override { return objectManager_->GetObjects(); }
    Object3d* AddObjectImmediate(std::unique_ptr<Object3d> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }
    std::unique_ptr<Object3d> TakeObjectImmediate(Object3d* object) override { return objectManager_->TakeObjectImmediate(object); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }

    const std::vector<std::unique_ptr<Sprite>>& GetSprites() const override { return sprites_; }

    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    Skybox* GetSkybox() override { return skybox_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

    // デバッグ設定の変更を、現在表示中のゲートとHUDに反映します。
    void RefreshDebugStageStates();

  private:
    std::vector<std::unique_ptr<Sprite>>* GetMutableSprites() override { return &sprites_; }
    // ステージゲートの選択、解放、遷移処理。
    void UpdateStageGateSelection(float deltaTime);
    void ApplyStageGateStates();
    bool IsStageUnlocked(int stageIndex) const;
    void EnterSelectedStage();

    /// <summary>ゲートへ入るカメラ演出と画面遷移を開始します。</summary>
    /// <param name="stageIndex">入場するステージの番号。</param>
    void StartGateEntryCinematic(int stageIndex);
    void UpdateGateEntryCinematic(float deltaTime);
    void UpdateGateCinematicBars(float deltaTime);
    void ResetGateCinematicBars();

    // 演出中だけゲート材質を変更するため、変更前の状態を保持・復元します。
    void CaptureGateEntryMaterialState(Object3d* rootObject);
    void ApplyGateEntryDissolveMaterial(float progress, const Vector3& gatePosition, const Vector3& direction);
    void RestoreGateEntryMaterialState();

    /// <summary>クリア後のゲート帰還演出を、保存済みの要求から開始します。</summary>
    /// <param name="request">戻り元ステージと今回獲得した報酬。</param>
    /// <returns>演出を開始できた場合は true。</returns>
    bool StartStageReturnPresentation(const GameDataManager::StageSelectReturnPresentation& request);
    void UpdateStageReturnPresentation(float deltaTime);
    void FinishStageReturnPresentation();
    void UpdateGatePrompt(Object3d* nearestGate, bool canEnterGate, float deltaTime);

    // 名前と距離から、プレイヤーが操作できるゲートを特定します。
    bool IsStageGateObject(const Object3d* object) const;
    int GetStageGateIndex(const Object3d* object) const;
    Object3d* FindStageGateByIndex(int stageIndex) const;
    Object3d* FindNearestStageGate(float* outDistance) const;
    bool IsPlayerTouchingStageGate(Object3d* gate) const;

    // 解放条件が成立した未表示ステージを選び、解放演出を進めます。
    int FindPendingUnlockStage() const;
    void BeginUnlockPresentation();
    void UpdateUnlockPresentation(float deltaTime);
    void FinishUnlockPresentation();
#ifdef USE_IMGUI
    void StartDebugUnlockPresentation(int stageIndex);
#endif

    // セレクト画面の装飾と収集状況表示
    void UpdateStageSelectDecorations(float deltaTime);
    void UpdatePathDisplay(int stageIndex, bool active, bool unlocking, float pulse);
    void UpdateStarCoinDisplays(float deltaTime);
    Object3d* EnsureStageClearCrown(int stageIndex);
    void UpdateStageClearCrownDisplays(float deltaTime);

    // ステージクリア後に戻ってきた時の王冠獲得演出。
    void StartStageClearRewardPresentation(const GameDataManager::StageClearRewardPresentation& request);
    void UpdateStageClearRewardPresentation(float deltaTime);
    void UpdateStageClearRewardCrown(Object3d* crown, int stageIndex, float deltaTime);
    void UpdateStageSelectCrownHudReward();
    int GetDisplayedCrownCount() const;

    Vector3 GetStageClearCrownPosition(int stageIndex) const;
    Object3d* FindObjectByName(const std::string& name) const;
    Vector3 GetStageNodePosition(int stageIndex) const;

    // HUD 初期化と数値描画。
    void InitializeStageSelectHUD();
    Sprite* FindSpriteByName(const std::string& name) const;
    // HUD の借用 Sprite と基準位置・サイズ・色。表示演出の変化を初期値から計算する。
    struct StageSelectHudSprite {
        Sprite* sprite = nullptr;
        Vector2 basePosition = {0.0f, 0.0f};
        Vector2 baseSize = {0.0f, 0.0f};
        Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    };
    StageSelectHudSprite BindStageSelectHUDSprite(const std::string& name, const std::string& texturePath,
                                                  const Vector2& position, const Vector2& size, const Vector2& anchor,
                                                  const Vector4& color);
    void SetStageSelectHUDNumber(std::array<StageSelectHudSprite, 3>& digits, int value, const Vector4& color,
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
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    std::vector<std::unique_ptr<Sprite>> sprites_;
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
    std::unique_ptr<Sprite> lockOnSprite_;
    std::unique_ptr<Sprite> gatePromptSprite_;
    bool isDrawLockOn_ = false;
    float gatePromptTimer_ = 0.0f;
    std::unique_ptr<Skybox> skybox_;
    uint32_t skyboxTextureHandle_ = 0;

    std::unique_ptr<Object3d> animatedCube_;

    // --- ステージゲート選択状態 ---
    int selectedStageIndex_ = 0;
    int previousSelectedStageIndex_ = -1;
    float gateSelectRadius_ = 8.0f;
    float stageDecisionCooldown_ = 0.0f;
    bool isChangingStage_ = false;
    // 入場演出で変える材質の復元用コピー。Object は借用し、演出終了後に元の値を戻す。
    struct GateEntryMaterialSnapshot {
        Object3d* object = nullptr;
        Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        int32_t materialType = 0;
        float emissive = 1.0f;
        float portalClipEnabled = 0.0f;
        float portalClipProgress = 0.0f;
        Vector3 portalClipCenter = {0.0f, 0.0f, 0.0f};
        float portalClipEdgeWidth = 0.0f;
        Vector3 portalClipNormal = {0.0f, 0.0f, 1.0f};
        float portalClipDissolve = 0.0f;
        Vector4 portalClipColor = {1.0f, 1.0f, 1.0f, 1.0f};
    };
    bool gateEntryCinematicActive_ = false;
    float gateEntryCinematicTimer_ = 0.0f;
    int gateEntryPendingStageIndex_ = -1;
    Object3d* gateEntryTargetGate_ = nullptr;
    Vector3 gateEntryStartPlayerPosition_{};
    Vector3 gateEntryTargetPlayerPosition_{};
    Vector3 gateEntrySurfacePlayerPosition_{};
    Vector3 gateEntryInsidePlayerPosition_{};
    Vector3 gateEntryDirection_{0.0f, 0.0f, 1.0f};
    Vector3 gateEntryCameraStartEye_{};
    Vector3 gateEntryCameraStartTarget_{};
    Vector3 gateEntryCameraEndEye_{};
    Vector3 gateEntryCameraEndTarget_{};
    Vector3 gateEntryStartPlayerScale_{1.0f, 1.0f, 1.0f};
    bool gateEntryHadPlayerControl_ = true;
    float gateEntrySparkTimer_ = 0.0f;
    float gateEntryMistTimer_ = 0.0f;
    float gateEntryGlintTimer_ = 0.0f;
    std::vector<GateEntryMaterialSnapshot> gateEntryMaterialSnapshots_;
    bool gateReturnPresentationActive_ = false;
    float gateReturnPresentationTimer_ = 0.0f;
    int gateReturnStageIndex_ = -1;
    Object3d* gateReturnTargetGate_ = nullptr;
    Vector3 gateReturnDirection_{0.0f, 0.0f, 1.0f};
    Vector3 gateReturnInsidePlayerPosition_{};
    Vector3 gateReturnCenterPlayerPosition_{};
    Vector3 gateReturnSurfacePlayerPosition_{};
    Vector3 gateReturnEndPlayerPosition_{};
    Vector3 gateReturnPlayerScale_{1.0f, 1.0f, 1.0f};
    Vector3 gateReturnCameraStartEye_{};
    Vector3 gateReturnCameraStartTarget_{};
    Vector3 gateReturnCameraEndEye_{};
    Vector3 gateReturnCameraEndTarget_{};
    Vector3 gateReturnCameraRestoreEye_{};
    Vector3 gateReturnCameraRestoreTarget_{};
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
    Object3d* unlockTargetGate_ = nullptr;
    Object3d* unlockKeyObject_ = nullptr;
    Object3d* unlockLockObject_ = nullptr;
    Vector3 unlockKeyBasePosition_{};
    Vector3 unlockKeyBaseRotation_{};
    Vector3 unlockKeyBaseScale_{1.0f, 1.0f, 1.0f};
    Vector3 unlockLockBasePosition_{};
    Vector3 unlockLockBaseRotation_{};
    Vector3 unlockLockBaseScale_{1.0f, 1.0f, 1.0f};
    Vector3 unlockGateDirection_{0.0f, 0.0f, 1.0f};
    Vector3 unlockCameraStartEye_{};
    Vector3 unlockCameraStartTarget_{};
    Vector3 unlockCameraCinematicEye_{};
    Vector3 unlockCameraCinematicTarget_{};
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

    int loadingInitializePhase_ = 0;
    size_t loadingInitializeCompletedUnits_ = 0;
    size_t loadingInitializeTotalUnits_ = 9;
};
