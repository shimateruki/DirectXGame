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
#include "TimeAttackUI.h"
#include "ObjectManager.h"
#include "LevelLoader.h"
#include <GhostRecorder.h>
#include <GameRule.h>
#include <memory>
#include <string>
#include <vector>

class DirectXCommon;
class InputManager;

class GameClearScene : public BaseScene {
public:
    GameClearScene() = default;
    ~GameClearScene() override = default;

    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

    // BaseScene インターフェース実装
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
    void SetSpriteTexturePreserveSize(Sprite* sprite, const std::string& textureName);
    void ApplyInputUiIfNeeded();

    // --- システム ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    Player* player_ = nullptr;
    uint32_t bgmHandle_ = 0;

    // --- ライト ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    uint32_t gpuParticleTexHandle_ = 0;

    // --- クリア演出・UI管理 ---
    std::unique_ptr<TimeAttackUI> clearTimeUI_;
    std::unique_ptr<TimeAttackUI> bestTimeUI_;

    // エディター配置のスプライト（名前で検索して保持）
    Sprite* gameClearSprite_ = nullptr;
    Sprite* retryTextSprite_ = nullptr;
    Sprite* titleTextSprite_ = nullptr;
    Sprite* playerTimeSprite_ = nullptr;
    Sprite* bestTimeSprite_ = nullptr;
    Sprite* enterTextSprite_ = nullptr;
    bool clearUiUsesGamepad_ = false;
    bool hasAppliedClearInputUi_ = false;
    enum class ClearState {
        kRunIn,
        kVictoryMotion,
        kShowClearTime, // 今回のタイム表示＆ドラムロール
        kShowBestTime,  // ベストタイム表示＆ドラムロール
        kWaitInput,     // ：リザルト完了、入力待ち
        kShowMenu,
        kRunOut
    };
    ClearState clearState_ = ClearState::kVictoryMotion;
    float stateTimer_ = 0.0f;

    enum class MenuIndex { Retry, Title, Max };
    int currentMenuIndex_ = (int)MenuIndex::Retry;

    float resultAlpha_ = 0.0f; // ロゴ・タイム用アルファ
    float menuAlpha_ = 0.0f;   // メニュー用アルファ
    float bestTimeAlpha_ = 0.0f;
    Vector3 targetPlayerPos_;
    Vector3 targetPlayerRot_;
};
