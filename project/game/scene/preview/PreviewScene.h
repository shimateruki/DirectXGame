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
    /// プレビュー用の管理対象を準備・破棄する。
    PreviewScene();
    ~PreviewScene() override;

    /// プレビュー配置とプレイヤーの初期状態を構築する。
    void Initialize() override;
    /// プレビューで必要なアセットの非同期読み込み対象を返す。
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    /// プレビューで保持する配置と描画資源を解放する。
    void Finalize() override;
    /// プレイヤー、敵、演出とシーン状態を更新する。
    void Update(float deltaTime) override;
    /// プレビュー専用 HUD の表示状態を更新する。
    void UpdateUI(float deltaTime);
    /// プレビュー中の 3D オブジェクトを描画する。
    void Draw() override;
    /// HUD とスプライトを描画する。
    void DrawUI() override;
    /// プレビュー配置の影を描画する。
    void DrawShadow() override;

    // --- IEditableの実装 ---
    std::string GetName() override { return "Preview Scene"; }
    /// プレビューシーンの開発用設定を表示する。
    void DrawImGui() override;

    // --- ムービーイベント ---
    /// 橋落下イベントの互換入口。プレビューでは何もしない。
    void StartBridgeDropMovie();

    // --- BaseScene インターフェース実装 ---
    const std::vector<std::unique_ptr<engine::Object3d>>& GetObjects() const override { return objectManager_->GetObjects(); }
    engine::Object3d* AddObjectImmediate(std::unique_ptr<engine::Object3d> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }
    std::unique_ptr<engine::Object3d> TakeObjectImmediate(engine::Object3d* object) override { return objectManager_->TakeObjectImmediate(object); }
    void AddObject(std::unique_ptr<engine::Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(engine::Object3d* object) override { objectManager_->RequestRemove(object); }
    const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const override { return sprites_; }

    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    Skybox* GetSkybox() override { return skybox_.get(); }
    engine::SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }
    GameRule* GetGameRule() override { return gameRule_.get(); }

    /// ゴール達成状態を切り替える。
    void SetIsGoal(bool isGoal) { isGoal_ = isGoal; }
    bool IsGoal() const { return isGoal_; }
    /// ゴールとの接触からクリア演出を開始できたか返す。
    bool TryStartGoalFromContact(engine::Object3d* actor, engine::Object3d* goal) override;
    /// 接触したスターコインを今回のプレイに記録する。
    void CollectStarCoinFromContact(int coinIndex, const engine::Vector3& worldPosition) override;

    /// 0～2 の番号で今回のスターコイン取得を記録する。
    void CollectStarCoin(int coinIndex) {
        if (coinIndex >= 0 && coinIndex < 3) {
            sessionStarCoins_[coinIndex] = true;
        }
    }

  private:
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

    // --- サブシステム ---
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<LockOnSystem> lockOnSystem_ = nullptr;
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;

    // --- ゲームオブジェクト共通基盤 ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<engine::SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    std::vector<std::unique_ptr<engine::Sprite>> sprites_;
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
    std::unique_ptr<engine::Sprite> lockOnSprite_;
    bool isDrawLockOn_ = false;
    std::unique_ptr<Skybox> skybox_;
    uint32_t skyboxTextureHandle_ = 0;

    std::unique_ptr<engine::Object3d> animatedCube_;

    bool isGoal_ = false;
    bool sessionStarCoins_[3] = {false, false, false};

    std::unique_ptr<engine::Sprite> hudHpIcon_;
    std::unique_ptr<engine::Sprite> hudCopyStyle_;
    CopyStyleHud copyStyleHud_;
    std::unique_ptr<engine::Sprite> hudHpFrame_;
    std::unique_ptr<engine::Sprite> hudHpDamageFill_;
    std::unique_ptr<engine::Sprite> hudHpFill_;
    std::unique_ptr<engine::Sprite> hudHpHighlight_;
    std::unique_ptr<engine::Sprite> hudMorphGaugeBack_;
    std::unique_ptr<engine::Sprite> hudMorphGaugeFill_;
    std::unique_ptr<engine::Sprite> hudMorphGaugeIcon_;
    std::unique_ptr<engine::Sprite> hudMorphGaugeFrame_;
    std::unique_ptr<engine::Sprite> hudLifeIcon_;
    std::unique_ptr<engine::Sprite> hudLifeXIcon_;
    std::array<std::unique_ptr<engine::Sprite>, 2> hudLifeDigits_;
    std::unique_ptr<engine::Sprite> hudCoinIcon_;
    std::unique_ptr<engine::Sprite> hudCoinXIcon_;
    std::array<std::unique_ptr<engine::Sprite>, 2> hudCoinDigits_;
    float hudPreviousHp_ = 0.0f;
    float hudDamagePulseTimer_ = 0.0f;
    float hudHurtIconTimer_ = 0.0f;
    float hudHpDamageHoldTimer_ = 0.0f;
    float hudHpDelayedRate_ = 1.0f;
    float hudHpAnimationTimer_ = 0.0f;
    float hudMorphGaugeTimer_ = 0.0f;
    float hudMorphGaugeVisibleTimer_ = 0.0f;

    /// 指定画像と配置からプレビュー HUD 用のスプライトを作る。
    std::unique_ptr<engine::Sprite> CreatePreviewHUDSprite(const std::string& texturePath, const engine::Vector2& position,
                                                   const engine::Vector2& size, const engine::Vector2& anchor, const engine::Vector4& color);
    /// 体力、残機、コインなどのプレビュー HUD を準備する。
    void InitializePreviewHUD();
    /// プレイヤー状態から HUD の数値とアニメーションを更新する。
    void UpdatePreviewHUD(float deltaTime);
    /// プレビュー HUD を描画する。
    void DrawPreviewHUD();
    /// 二桁の数値を右寄せ位置から配置し、表示状態を設定する。
    void SetPreviewHUDNumber(std::array<std::unique_ptr<engine::Sprite>, 2>& digits, int value,
                             const engine::Vector2& rightAlignedPosition, float digitHeight, const engine::Vector4& color,
                             bool visible);

    /// オブジェクトがカメラの表示範囲に入るか判定する。
    bool IsVisible(engine::Object3d* obj);
};
