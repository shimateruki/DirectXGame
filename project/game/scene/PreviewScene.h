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

#include "ObjectManager.h"
#include "DebugEditor.h" 
#include <GhostRecorder.h>

#include <memory>
#include <array>
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

/// <summary>
/// アセット確認・調整用のプレビューシーン (GamePlaySceneの完全クローン)
/// </summary>
class PreviewScene : public BaseScene {
public:
    PreviewScene();
    ~PreviewScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void UpdateUI(float deltaTime);
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

    // --- IEditableの実装 ---
    std::string GetName() override { return "Preview Scene"; }
    void DrawImGui() override;

    // --- ムービーイベント ---
    void StartBridgeDropMovie();

    // --- BaseScene インターフェース実装 ---
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objectManager_->GetObjects(); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }

    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

    void SetIsGoal(bool isGoal) { isGoal_ = isGoal; }
    bool IsGoal() const { return isGoal_; }

    void CollectStarCoin(int coinIndex) {
        if (coinIndex >= 0 && coinIndex < 3) {
            sessionStarCoins_[coinIndex] = true;
        }
    }

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

    // --- サブシステム ---
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<LockOnSystem> lockOnSystem_ = nullptr;
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;

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
    bool isDrawLockOn_ = false;
    std::unique_ptr<Skybox> skybox_;
    uint32_t skyboxTextureHandle_ = 0;

    std::unique_ptr<Object3d> animatedCube_;

    bool isGoal_ = false;
    bool sessionStarCoins_[3] = { false, false, false };

    std::unique_ptr<Sprite> hudLifeMeter_;
    std::unique_ptr<Sprite> hudLifeMeterDigit_;
    std::unique_ptr<Sprite> hudLifeIcon_;
    std::unique_ptr<Sprite> hudLifeXIcon_;
    std::array<std::unique_ptr<Sprite>, 2> hudLifeDigits_;
    std::unique_ptr<Sprite> hudCoinIcon_;
    std::unique_ptr<Sprite> hudCoinXIcon_;
    std::array<std::unique_ptr<Sprite>, 2> hudCoinDigits_;
    float hudPreviousHp_ = 0.0f;
    float hudDamagePulseTimer_ = 0.0f;
    int hudDisplayedLife_ = 6;

    std::unique_ptr<Sprite> CreatePreviewHUDSprite(const std::string& texturePath, const Vector2& position, const Vector2& size, const Vector2& anchor, const Vector4& color);
    void InitializePreviewHUD();
    void UpdatePreviewHUD(float deltaTime);
    void DrawPreviewHUD();
    void SetPreviewHUDNumber(std::array<std::unique_ptr<Sprite>, 2>& digits, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color, bool visible);

    bool IsVisible(Object3d* obj);
};
