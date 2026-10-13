#pragma once
#include "BaseScene.h"
#include "game/system/SceneGameplayAccess.h"
#include "game/system/IStageProgressScene.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "AudioPlayer.h"
#include "ParticleSystem.h"
#include "ParticleCommon.h"
#include "SpriteDebugEditor.h"
#include "Player.h"
#include "game/ui/CopyStyleHud.h"
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

/// <summary>
/// アセット確認用の独立シーン。配置 Object とプレイヤーを管理し、編集用の更新・描画を行う。
/// </summary>
class PreviewScene : public BaseScene, public IStageProgressScene, public ISceneGameplayAccess {
  public:
    PreviewScene();
    ~PreviewScene() override;

    void Initialize() override;
    SceneLoadManifest BuildAsyncLoadManifest() const override;
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
    GameRule* GetGameRule() override { return gameRule_.get(); }

    void SetIsGoal(bool isGoal) { isGoal_ = isGoal; }
    bool IsGoal() const { return isGoal_; }
    bool TryStartGoalFromContact(Object3d* actor, Object3d* goal) override;
    void CollectStarCoinFromContact(int coinIndex, const Vector3& worldPosition) override;

    void CollectStarCoin(int coinIndex) {
        if (coinIndex >= 0 && coinIndex < 3) {
            sessionStarCoins_[coinIndex] = true;
        }
    }

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
    std::unique_ptr<Text> debugText_;
    std::unique_ptr<GameRule> gameRule_;

    Player* player_ = nullptr;

    // --- BGM・SE ---
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
    bool sessionStarCoins_[3] = {false, false, false};

    std::unique_ptr<Sprite> hudHpIcon_;
    std::unique_ptr<Sprite> hudCopyStyle_;
    CopyStyleHud copyStyleHud_;
    std::unique_ptr<Sprite> hudHpFrame_;
    std::unique_ptr<Sprite> hudHpDamageFill_;
    std::unique_ptr<Sprite> hudHpFill_;
    std::unique_ptr<Sprite> hudHpHighlight_;
    std::unique_ptr<Sprite> hudMorphGaugeBack_;
    std::unique_ptr<Sprite> hudMorphGaugeFill_;
    std::unique_ptr<Sprite> hudMorphGaugeIcon_;
    std::unique_ptr<Sprite> hudMorphGaugeFrame_;
    std::unique_ptr<Sprite> hudLifeIcon_;
    std::unique_ptr<Sprite> hudLifeXIcon_;
    std::array<std::unique_ptr<Sprite>, 2> hudLifeDigits_;
    std::unique_ptr<Sprite> hudCoinIcon_;
    std::unique_ptr<Sprite> hudCoinXIcon_;
    std::array<std::unique_ptr<Sprite>, 2> hudCoinDigits_;
    float hudPreviousHp_ = 0.0f;
    float hudDamagePulseTimer_ = 0.0f;
    float hudHurtIconTimer_ = 0.0f;
    float hudHpDamageHoldTimer_ = 0.0f;
    float hudHpDelayedRate_ = 1.0f;
    float hudHpAnimationTimer_ = 0.0f;
    float hudMorphGaugeTimer_ = 0.0f;
    float hudMorphGaugeVisibleTimer_ = 0.0f;

    std::unique_ptr<Sprite> CreatePreviewHUDSprite(const std::string& texturePath, const Vector2& position,
                                                   const Vector2& size, const Vector2& anchor, const Vector4& color);
    void InitializePreviewHUD();
    void UpdatePreviewHUD(float deltaTime);
    void DrawPreviewHUD();
    void SetPreviewHUDNumber(std::array<std::unique_ptr<Sprite>, 2>& digits, int value,
                             const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color,
                             bool visible);

    bool IsVisible(Object3d* obj);
};
