#pragma once
#include "BaseScene.h"
#include "game/system/SceneGameplayAccess.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "AudioPlayer.h"
#include "ParticleSystem.h"
#include "ParticleCommon.h"
#include "Player.h"
#include "Text.h"
#include "BulletManager.h"
#include "Camera.h"

#include "ObjectManager.h"
#include "LevelLoader.h"
#include <GhostRecorder.h>
#include <GameRule.h>

#include <memory>
#include <vector>

// --- 前方宣言 ---
class DirectXCommon;
class InputManager;

/// <summary>
/// ゲームクリアシーン
/// </summary>
class GameClearScene : public BaseScene, public ISceneGameplayAccess {
  public:
    GameClearScene() = default;
    ~GameClearScene() override = default;

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize() override;
    /// ゲームクリア画面で使用するアセットを列挙する。
    SceneLoadManifest BuildAsyncLoadManifest() const override;

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize() override;

    /// <summary>
    /// 更新
    /// </summary>
    void Update(float deltaTime) override;

    /// <summary>
    /// 描画
    /// </summary>
    void Draw() override;
    /// クリア画面の HUD とスプライトを描画する。
    void DrawUI() override;

    // シャドウマップ描画のオーバーライド
    void DrawShadow() override;

    // --- BaseScene インターフェース実装 (ObjectManagerへ委譲) ---

    const std::vector<std::unique_ptr<engine::Object3d>>& GetObjects() const override { return objectManager_->GetObjects(); }
    engine::Object3d* AddObjectImmediate(std::unique_ptr<engine::Object3d> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }
    std::unique_ptr<engine::Object3d> TakeObjectImmediate(engine::Object3d* object) override { return objectManager_->TakeObjectImmediate(object); }
    void AddObject(std::unique_ptr<engine::Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(engine::Object3d* object) override { objectManager_->RequestRemove(object); }

    // スプライト・共通クラスの取得
    const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const override { return sprites_; }
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    engine::SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    // プレイヤー関連
    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

  private:
    std::vector<std::unique_ptr<engine::Sprite>>* GetMutableSprites() override { return &sprites_; }
    // --- エンジンシステム ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;

    // --- サブシステム (管理クラス) ---
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    // --- 共通基盤クラス ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<engine::SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    // --- オブジェクト・リソース ---
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    Player* player_ = nullptr;

    // --- BGM・オーディオ ---

    // --- ライトリソース ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;

    //  GPUパーティクル用画像ハンドル
    uint32_t gpuParticleTexHandle_ = 0;
};
