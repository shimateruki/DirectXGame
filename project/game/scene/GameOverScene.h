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

#include <memory>
#include <vector>

// --- 前方宣言 ---
class DirectXCommon;
class InputManager;

// ゲームオーバー画面とリトライ/タイトル遷移を管理するシーン。
class GameOverScene : public BaseScene {
public:
    GameOverScene() = default;
    ~GameOverScene() override = default;

    // --- 基本サイクル ---
    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;

    // --- 描画 ---
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

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

private:
    // --- 外部システム参照 ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    // --- シーン内サブシステム ---
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    // --- 描画・生成の共通基盤 ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    // --- シーン所有リソース ---
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    Player* player_ = nullptr;

    // --- BGM ---
    uint32_t bgmHandle_ = 0;

    // --- ライト・描画補助リソース ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    uint32_t gpuParticleTexHandle_ = 0;
};
