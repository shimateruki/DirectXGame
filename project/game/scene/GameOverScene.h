#pragma once
#include "BaseScene.h" 
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
class GameOverScene : public BaseScene {
public:
    GameOverScene() = default;
    ~GameOverScene() override = default;

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize() override;

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
    void DrawUI() override;

    //  シャドウマップ描画のオーバーライド
    void DrawShadow() override;

    // --- BaseScene インターフェース実装 (ObjectManagerへ委譲) ---
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objectManager_->GetObjects(); }
    void AddObject(std::unique_ptr<Object3d> object) override { objectManager_->AddObject(std::move(object)); }
    void RequestRemoveObject(Object3d* object) override { objectManager_->RequestRemove(object); }

    // 各種マネージャ・コモンの取得
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    // プレイヤー連携
    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

private:
    enum class MenuItem {
        Retry,
        Title,
        Count
    };

    struct MenuRow {
        Sprite* backdrop = nullptr;
        Sprite* label = nullptr;
        Vector2 backdropBaseSize = { 0.0f, 0.0f };
        Vector2 labelBaseSize = { 0.0f, 0.0f };
    };

    void BindLayoutSprites();
    Sprite* FindSprite(const std::string& name) const;
    void UpdateMenuInput();
    void UpdateMenuSprites(float deltaTime);
    void ChangeSelection(int direction);
    void ConfirmSelection();

    // --- エンジン基盤 ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    // --- サブシステム (機能を委譲するクラスたち) ---
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    // --- 共通基盤クラス ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    // --- オブジェクト・リソース ---
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    Player* player_ = nullptr;

    // --- BGM・オーディオ ---
    uint32_t bgmHandle_ = 0;

    // --- ライト ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;

    //  GPUパーティクル用画像ハンドル
    uint32_t gpuParticleTexHandle_ = 0;

    Sprite* backgroundSprite_ = nullptr;
    std::array<Sprite*, 7> titleLetters_ = {};
    std::array<Vector2, 7> titleLetterBasePositions_ = {};
    std::array<Vector2, 7> titleLetterBaseSizes_ = {};
    std::array<MenuRow, static_cast<size_t>(MenuItem::Count)> menuRows_ = {};
    int selectedIndex_ = 0;
    float sceneTime_ = 0.0f;
};
