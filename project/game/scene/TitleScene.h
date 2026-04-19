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
#include "OptionUI.h"
#include <memory>
#include <vector>

// --- 前方宣言 ---
class DirectXCommon;
class InputManager;

/// <summary>
/// タイトルシーン
/// </summary>
class TitleScene : public BaseScene {
public:
    TitleScene() = default;
    ~TitleScene() override = default;

    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void Draw() override;
    void DrawUI() override;

    //  シャドウマップ描画のオーバーライド
    void DrawShadow() override;

    // --- BaseScene インターフェース実装 (ObjectManagerへ委譲) ---
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
    // --- システムポインタ ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    // --- サブシステム (管理クラス) ---
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    // --- 共通基盤クラス ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;

    // --- リソース ---
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    Player* player_ = nullptr;

    uint32_t bgmHandle_ = 0;

    // --- ライト・GPUリソース ---
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;

    //  GPUパーティクル用画像ハンドル
    uint32_t gpuParticleTexHandle_ = 0;

    // ==========================================
    // 状態管理用 (追加部分)
    // ==========================================
    enum class TitleState {
        MainMenu,   // 最初から表示されている「Game Start / Setting」を選ぶ画面
        OptionMenu  // Settingを選んだ後の「Sound / Keyboard」などを選ぶ画面
    };
    TitleState currentState_ = TitleState::MainMenu;

    // メニューの選択肢
    enum class MenuIndex {
        GameStart,
        Setting,
        Max // 項目数を取るためのダミー
    };
    int currentMenuIndex_ = (int)MenuIndex::GameStart; // 現在の選択番号

    // 設定項目を無効化するフラグ（falseにしておけば設定へは遷移できない）
    bool settingEnabled_ = false;

    // オプションメニューの選択肢 (追加部分)
    enum class OptionIndex {
        Sound,
        KeyConfig,
        Max
    };
    int currentOptionIndex_ = (int)OptionIndex::Sound;

    // ==========================================
    // スプライトのポインタ保持
    // ==========================================
    Sprite* startTextSprite_ = nullptr;
    Sprite* settingTextSprite_ = nullptr;

    std::unique_ptr<OptionUI> optionUI_ = nullptr;
};