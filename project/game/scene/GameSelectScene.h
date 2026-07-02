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

    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void UpdateUI();
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;
    void DrawImGui() override;

    // --- BaseScene インターフェース ---
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objectManager_->GetObjects(); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }

    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }

    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

    void RefreshDebugStageStates();

private:
    // ステージゲートの選択、解放、遷移処理
    void UpdateStageGateSelection(float deltaTime);
    void ApplyStageGateStates();
    bool IsStageUnlocked(int stageIndex) const;
    void EnterSelectedStage();
    void StartGateEntryCinematic(int stageIndex);
    void UpdateGateEntryCinematic(float deltaTime);
    void CaptureGateEntryMaterialState(Object3d* rootObject);
    void ApplyGateEntryDissolveMaterial(float progress, const Vector3& gatePosition, const Vector3& direction);
    void RestoreGateEntryMaterialState();
    void UpdateGatePrompt(Object3d* nearestGate, bool canEnterGate, float deltaTime);
    bool IsStageGateObject(const Object3d* object) const;
    int GetStageGateIndex(const Object3d* object) const;
    Object3d* FindNearestStageGate(float* outDistance) const;
    bool IsPlayerTouchingStageGate(Object3d* gate) const;
    int FindPendingUnlockStage() const;
    void UpdateUnlockPresentation(float deltaTime);

    // セレクト画面の装飾と収集状況表示
    void UpdateStageSelectDecorations(float deltaTime);
    void UpdatePathDisplay(int stageIndex, bool active, bool unlocking, float pulse);
    void UpdateStarCoinDisplays(float deltaTime);
    Object3d* EnsureStageClearCrown(int stageIndex);
    void UpdateStageClearCrownDisplays(float deltaTime);

    // ステージクリア後に戻ってきた時の王冠獲得演出
    void StartStageClearRewardPresentation(const GameDataManager::StageClearRewardPresentation& request);
    void UpdateStageClearRewardPresentation(float deltaTime);
    void UpdateStageClearRewardCrown(Object3d* crown, int stageIndex, float deltaTime);
    void UpdateStageSelectCrownHudReward();
    int GetDisplayedCrownCount() const;

    Vector3 GetStageClearCrownPosition(int stageIndex) const;
    Object3d* FindObjectByName(const std::string& name) const;
    Vector3 GetStageNodePosition(int stageIndex) const;

    // HUD 初期化と数値描画
    void InitializeStageSelectHUD();
    Sprite* FindSpriteByName(const std::string& name) const;
    struct StageSelectHudSprite {
        Sprite* sprite = nullptr;
        Vector2 basePosition = { 0.0f, 0.0f };
        Vector2 baseSize = { 0.0f, 0.0f };
        Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    };
    StageSelectHudSprite BindStageSelectHUDSprite(
        const std::string& name,
        const std::string& texturePath,
        const Vector2& position,
        const Vector2& size,
        const Vector2& anchor,
        const Vector4& color);
    void SetStageSelectHUDNumber(std::array<StageSelectHudSprite, 3>& digits, int value, const Vector4& color, bool visible);

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
    struct GateEntryMaterialSnapshot {
        Object3d* object = nullptr;
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        int32_t materialType = 0;
        float emissive = 1.0f;
        float portalClipEnabled = 0.0f;
        float portalClipProgress = 0.0f;
        Vector3 portalClipCenter = { 0.0f, 0.0f, 0.0f };
        float portalClipEdgeWidth = 0.0f;
        Vector3 portalClipNormal = { 0.0f, 0.0f, 1.0f };
        float portalClipDissolve = 0.0f;
        Vector4 portalClipColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    };
    bool gateEntryCinematicActive_ = false;
    float gateEntryCinematicTimer_ = 0.0f;
    int gateEntryPendingStageIndex_ = -1;
    Object3d* gateEntryTargetGate_ = nullptr;
    Vector3 gateEntryStartPlayerPosition_{};
    Vector3 gateEntryTargetPlayerPosition_{};
    Vector3 gateEntrySurfacePlayerPosition_{};
    Vector3 gateEntryInsidePlayerPosition_{};
    Vector3 gateEntryDirection_{ 0.0f, 0.0f, 1.0f };
    Vector3 gateEntryCameraStartEye_{};
    Vector3 gateEntryCameraStartTarget_{};
    Vector3 gateEntryCameraEndEye_{};
    Vector3 gateEntryCameraEndTarget_{};
    Vector3 gateEntryStartPlayerScale_{ 1.0f, 1.0f, 1.0f };
    bool gateEntryHadPlayerControl_ = true;
    float gateEntrySparkTimer_ = 0.0f;
    float gateEntryMistTimer_ = 0.0f;
    float gateEntryGlintTimer_ = 0.0f;
    std::vector<GateEntryMaterialSnapshot> gateEntryMaterialSnapshots_;
    int unlockingStageIndex_ = -1;
    float unlockPresentationTimer_ = 0.0f;
    float unlockParticleTimer_ = 0.0f;
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
};
