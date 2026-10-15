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
#include "game/scene/gameover/GameOverSlimeAnimator.h"

#include "ObjectManager.h"
#include "LevelLoader.h"
#include <GhostRecorder.h>
#include <GameRule.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

// --- 前方宣言 ---
class DirectXCommon;
class InputManager;

/// <summary>
/// ゲームオーバーシーン
/// </summary>
class GameOverScene : public BaseScene, public ISceneGameplayAccess {
  public:
    GameOverScene() = default;
    ~GameOverScene() override = default;

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize() override;
    /// シーン切り替え後の演出と選択状態を開始する。
    void OnActivated() override;
    /// ゲームオーバー表示に必要なアセットを列挙する。
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
    /// 背景、タイトル文字、選択メニューを描画する。
    void DrawUI() override;
    /// ゲームオーバーシーンの開発用設定を表示する。
    void DrawImGui() override;

    //  シャドウマップ描画のオーバーライド
    void DrawShadow() override;

    // --- BaseScene インターフェース実装 (ObjectManagerへ委譲) ---
    const std::vector<std::unique_ptr<engine::Object3d>>& GetObjects() const override { return objectManager_->GetObjects(); }
    engine::Object3d* AddObjectImmediate(std::unique_ptr<engine::Object3d> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }
    std::unique_ptr<engine::Object3d> TakeObjectImmediate(engine::Object3d* object) override { return objectManager_->TakeObjectImmediate(object); }
    void AddObject(std::unique_ptr<engine::Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(engine::Object3d* object) override { objectManager_->RequestRemove(object); }

    // 各種マネージャ・コモンの取得
    const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const override { return sprites_; }
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    engine::SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    // プレイヤー連携
    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

  private:
    std::vector<std::unique_ptr<engine::Sprite>>* GetMutableSprites() override { return &sprites_; }
    enum class MenuItem { Retry, Title, Count };

    // メニュー行の背景・ラベルを借用する。基準サイズを残し、選択演出の拡縮を累積させない。
    struct MenuRow {
        engine::Sprite* backdrop = nullptr;
        engine::Sprite* label = nullptr;
        engine::Vector2 backdropBaseSize = {0.0f, 0.0f};
        engine::Vector2 labelBaseSize = {0.0f, 0.0f};
    };

    // 背景王冠 Sprite の借用参照と落下周期。基準位置から毎回評価し、流れ続ける表示を作る。
    struct FallingCrown {
        engine::Sprite* sprite = nullptr;
        engine::Vector2 basePosition = {0.0f, 0.0f};
        engine::Vector2 size = {0.0f, 0.0f};
        float timeOffset = 0.0f;
        float cycleDuration = 1.0f;
        float fallDistance = 0.0f;
        float driftAmplitude = 0.0f;
        float driftSpeed = 0.0f;
        float rotationSpeed = 0.0f;
        float phase = 0.0f;
        float alpha = 1.0f;
    };

    // 頭上を回る星の借用 Sprite と位相差。depthOffset は重なりの見せ方に使う。
    struct DizzyStar {
        engine::Sprite* sprite = nullptr;
        float baseAngle = 0.0f;
        float size = 0.0f;
        float depthOffset = 0.0f;
    };

    // ゲームオーバーのカメラ演出値。時間は秒、FOV はラジアン、各 Offset は相対位置。
    struct PresentationTuning {
        float cameraSettleTime = 1.55f;
        float titleRevealStartTime = 0.15f;
        engine::Vector3 introEyeOffset = {0.75f, 1.28f, -5.10f};
        engine::Vector3 impactEyeOffset = {0.34f, 1.04f, -4.25f};
        engine::Vector3 settleEyeOffset = {-0.82f, 1.05f, -5.20f};
        engine::Vector3 introTargetOffset = {0.0f, 0.50f, 0.0f};
        engine::Vector3 settleTargetOffset = {-0.82f, 1.05f, 0.0f};
        float introFov = 0.50f;
        float impactFov = 0.43f;
        float settleFov = 0.58f;
        float impactShake = 0.085f;
    };

    /// レイアウトからメニューや背景のスプライト参照を取得する。
    void BindLayoutSprites();
    /// レイアウト更新後に保持しているスプライト参照を取り直す。
    void RefreshLayoutSpritePointers();
    /// レイアウト上の名前からスプライトを探す。
    engine::Sprite* FindSprite(const std::string& name) const;
    /// 背景で落下する王冠の初期位置と周期を設定する。
    void InitializeFallingCrowns();
    /// 王冠の落下位置と回転を時間から評価する。
    void UpdateFallingCrowns(float deltaTime);
    /// スライムの頭上を回る星の初期位相を設定する。
    void InitializeDizzyStars();
    /// 星の周回位置と表示を更新する。
    void UpdateDizzyStars(float deltaTime);
    /// リトライ選択後の退出演出を開始する。
    void StartRetryExit();
    /// 退出演出を進め、完了後にシーン変更を要求する。
    void UpdateRetryExit(float deltaTime);
    /// メニューの上下移動と決定入力を処理する。
    void UpdateMenuInput();
    /// 現在の選択に合わせてメニューの見た目を更新する。
    void UpdateMenuSprites(float deltaTime);
    /// 選択行を指定方向へ進める。
    void ChangeSelection(int direction);
    /// 選択中のリトライまたはタイトル移動を実行する。
    void ConfirmSelection();
    /// タイトル文字の表示演出が終わったか返す。
    bool IsTitleRevealComplete() const;
    /// カメラ、モデル、画面効果の開始状態を設定する。
    void InitializeGameOverPresentation();
    /// 演出対象のスライムを配置オブジェクトから探す。
    void FindGameOverSlimeObject();
    /// スライムの動きとカメラ演出を時間に沿って進める。
    void UpdateGameOverPresentation(float deltaTime);
    /// 現在の演出段階に合わせてカメラ位置と注視点を更新する。
    void UpdateGameOverCamera();
    /// 衝撃と復帰に合わせて画面効果を設定する。
    void UpdateGameOverPostEffects();
    /// 演出中の衝撃音と視覚効果を一度だけ発生させる。
    void EmitGameOverImpactCue();
    /// ゲームオーバー用の画面効果を解除する。
    void ResetGameOverPostEffects();
    /// UI の背後に置く背景スプライトを描画する。
    void DrawBackgroundSprite();

    // --- エンジン基盤 ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;

    // --- サブシステム (機能を委譲するクラスたち) ---
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    // --- 共通基盤クラス ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<engine::SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    // --- オブジェクト・リソース ---
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;
    engine::Object3d* gameOverSlimeObject_ = nullptr;
    GameOverSlimeAnimator gameOverSlimeAnimator_;
    engine::Vector3 gameOverSlimeBasePosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 gameOverSlimeBaseScale_ = {1.0f, 1.0f, 1.0f};
    float gameOverPresentationTimer_ = 0.0f;
    PresentationTuning presentationTuning_;
    bool gameOverImpactCuePlayed_ = false;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    Player* player_ = nullptr;

    // --- BGM・オーディオ ---

    // --- ライト ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;

    //  GPUパーティクル用画像ハンドル
    uint32_t gpuParticleTexHandle_ = 0;

    engine::Sprite* backgroundSprite_ = nullptr;
    std::array<engine::Sprite*, 8> titleLetters_ = {};
    std::array<engine::Vector2, 8> titleLetterBasePositions_ = {};
    std::array<engine::Vector2, 8> titleLetterBaseSizes_ = {};
    std::array<MenuRow, static_cast<size_t>(MenuItem::Count)> menuRows_ = {};
    std::vector<FallingCrown> fallingCrowns_;
    std::vector<DizzyStar> dizzyStars_;
    int selectedIndex_ = 0;
    float sceneTime_ = 0.0f;
    float titleRevealTimer_ = 0.0f;
    bool retryExitActive_ = false;
    bool retrySceneChangeRequested_ = false;
    float retryExitTimer_ = 0.0f;
};
