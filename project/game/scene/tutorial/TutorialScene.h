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
#include "SpriteDebugEditor.h"
#include "Player.h"
#include "Text.h"
#include "Event.h"
#include "BulletManager.h"
#include "Camera.h"
#include "MeshRenderer.h"
#include "game/ui/ControlsGuideOverlay.h"
#include "game/ui/CopyStyleHud.h"
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
class TutorialDirector;

/// <summary>
/// TutorialDirector の手順進行と、プレイヤー・配置・操作ガイド・完了後の遷移をまとめるシーン。
/// </summary>
class TutorialScene : public BaseScene, public ISceneGameplayAccess {
  public:
    /// チュートリアル用の管理対象を準備・破棄する。
    TutorialScene();
    ~TutorialScene() override;

    /// 配置、プレイヤー、ガイド表示を初期化する。
    void Initialize() override;
    /// 読み込み画面中に必要な初期化を段階的に開始する。
    void BeginLoadingInitialize() override;
    /// 初期化を一段進める。完了すると true を返す。
    bool InitializeLoadingStep() override;
    /// 段階的初期化の進捗率を返す。
    float GetLoadingInitializeProgress() const override;
    /// チュートリアルで使用するアセットの非同期読み込み対象を列挙する。
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    /// シーンが保持する配置と演出リソースを解放する。
    void Finalize() override;
    /// 操作ガイド、チュートリアル進行、配置物を更新する。
    void Update(float deltaTime) override;
    /// シーン専用 UI の状態を更新する。
    void UpdateUI();
    /// チュートリアル内の 3D オブジェクトを描画する。
    void Draw() override;
    /// 操作ガイドと HUD を描画する。
    void DrawUI() override;
    /// チュートリアルの影を描画する。
    void DrawShadow() override;

    // --- IEditableの実装 ---
    std::string GetName() override { return "Tutorial Scene"; }
    /// チュートリアルシーンの開発用設定を表示する。
    void DrawImGui() override;

    // --- ムービーイベント ---
    /// 橋の落下を見せるカメラ演出を開始する。
    void StartBridgeDropMovie();

    // --- BaseScene インターフェース実装 ---

    // オブジェクト管理は ObjectManager に委譲
    const std::vector<std::unique_ptr<engine::Object3d>>& GetObjects() const override { return objectManager_->GetObjects(); }
    engine::Object3d* AddObjectImmediate(std::unique_ptr<engine::Object3d> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }
    std::unique_ptr<engine::Object3d> TakeObjectImmediate(engine::Object3d* object) override { return objectManager_->TakeObjectImmediate(object); }
    void AddObject(std::unique_ptr<engine::Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(engine::Object3d* object) override { objectManager_->RequestRemove(object); }

    // スプライトはシーンで保持 (ObjectManagerを拡張すれば移動可能)
    const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const override { return sprites_; }
    /// リプレイで状態を再現するスプライトを出力先へ追加する。
    void CollectReplaySprites(std::vector<engine::Sprite*>& sprites) override;
    /// チュートリアル固有の進行状態を JSON に記録する。
    void CaptureReplaySceneState(json& state) const override;
    /// 記録した進行状態を復元する。
    void RestoreReplaySceneState(const json& state) override;

    // 各種コモンクラス
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    Skybox* GetSkybox() override { return skybox_.get(); }
    engine::SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    // プレイヤー連携
    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }
    GameRule* GetGameRule() override { return gameRule_.get(); }

    // ゴール判定
    /// ゴール達成状態を切り替える。
    void SetIsGoal(bool isGoal);
    bool IsGoal() const { return isGoal_; }

    // スターコイン
    /// このプレイ中のスターコイン取得を、0～2 の番号で記録する。
    void CollectStarCoin(int coinIndex) {
        if (coinIndex >= 0 && coinIndex < 3) {
            sessionStarCoins_[coinIndex] = true;
        }
    }

  private:
    std::vector<std::unique_ptr<engine::Sprite>>* GetMutableSprites() override { return &sprites_; }
    /// 開閉入力を処理し、ガイドがゲーム進行を占有している間は true を返す。
    bool HandleControlsGuideOverlay(float deltaTime);
    /// 操作ガイドを開く入力が発生したか調べる。
    bool IsControlsGuideOpenTriggered() const;
    /// チュートリアル完了を保存し、成功時に出口ゲートを解放する。
    void HandleTutorialFlowCompleted();

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

    // --- サブシステム (機能を委譲するクラスたち) ---
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;     // 配置読み込み
    std::unique_ptr<LockOnSystem> lockOnSystem_ = nullptr;   // ロックオン管理
    std::unique_ptr<ObjectManager> objectManager_ = nullptr; //  オブジェクト管理

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
    bool isDrawLockOn_ = false; // 描画するかどうかのスイッチ
    std::unique_ptr<Skybox> skybox_;
    uint32_t skyboxTextureHandle_ = 0;

    bool isGoal_ = false;
    bool goalSavePerformed_ = false;
    bool goalSaveErrorShown_ = false;
    float goalSaveRetryTimer_ = 0.0f;
    bool sessionStarCoins_[3] = {false, false, false};
    std::unique_ptr<TutorialDirector> tutorialDirector_;
    std::unique_ptr<ControlsGuideOverlay> controlsGuideOverlay_;
    std::unique_ptr<SaveIndicatorOverlay> saveIndicatorOverlay_;
    CopyStyleHud copyStyleHud_;

    int loadingInitializePhase_ = 0;
    size_t loadingInitializeItemIndex_ = 0;
    size_t loadingInitializeCompletedUnits_ = 0;
    size_t loadingInitializeTotalUnits_ = 1;

    // フラスタムカリング判定
    /// オブジェクトがカメラの表示範囲に入るか判定する。
    bool IsVisible(engine::Object3d* obj);
};
