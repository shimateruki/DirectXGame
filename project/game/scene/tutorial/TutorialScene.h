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
#include "game/ui/ControlsGuideOverlay.h"
#include "game/ui/SaveIndicatorOverlay.h"

#include "ObjectManager.h"
#include "DebugEditor.h" 
#include <GhostRecorder.h>

#include <memory>
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
class TutorialDirector;


/// <summary>
/// チュートリアルシーン (GamePlaySceneのコピーから仕切り直し)
/// </summary>
class TutorialScene : public BaseScene {
public:
    TutorialScene();
    ~TutorialScene() override;

	void Initialize() override;
	void BeginLoadingInitialize() override;
	bool InitializeLoadingStep() override;
	float GetLoadingInitializeProgress() const override;
	SceneLoadManifest BuildAsyncLoadManifest() const override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void UpdateUI();
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

    // --- IEditableの実装 ---
    std::string GetName() override { return "Tutorial Scene"; }
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
    void CollectReplaySprites(std::vector<Sprite*>& sprites) override;
    void CaptureReplaySceneState(json& state) const override;
    void RestoreReplaySceneState(const json& state) override;

    // 各種コモンクラス
	Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
	Skybox* GetSkybox() override { return skybox_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    // プレイヤー連携
    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }
    GameRule* GetGameRule() override { return gameRule_.get(); }

    // ゴール判定
    void SetIsGoal(bool isGoal);
    bool IsGoal() const { return isGoal_; }

    // スターコイン
    void CollectStarCoin(int coinIndex) {
        if (coinIndex >= 0 && coinIndex < 3) {
            sessionStarCoins_[coinIndex] = true;
        }
    }

private:
    bool HandleControlsGuideOverlay(float deltaTime);
    bool IsControlsGuideOpenTriggered() const;
    void HandleTutorialFlowCompleted();

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

    bool isGoal_ = false;
    bool goalSavePerformed_ = false;
    bool sessionStarCoins_[3] = { false, false, false };
    std::unique_ptr<TutorialDirector> tutorialDirector_;
    std::unique_ptr<ControlsGuideOverlay> controlsGuideOverlay_;
    std::unique_ptr<SaveIndicatorOverlay> saveIndicatorOverlay_;

    int loadingInitializePhase_ = 0;
    size_t loadingInitializeItemIndex_ = 0;
    size_t loadingInitializeCompletedUnits_ = 0;
    size_t loadingInitializeTotalUnits_ = 1;

    // フラスタムカリング判定
    bool IsVisible(Object3d* obj);
};
