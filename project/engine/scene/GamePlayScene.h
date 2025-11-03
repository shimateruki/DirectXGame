#pragma once
#include "BaseScene.h" 
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "AudioPlayer.h"
#include "ParticleSystem.h" 
#include "ParticleCommon.h" 
#include"SpriteDebugEditor.h"
#include"Player.h"
#include"Text.h"
#include "Event.h"
#include "ParticleEditor.h"

#include <memory>
#include <vector>

// --- 前方宣言 (ポインタで持つものだけ) ---
class DirectXCommon;
class InputManager;
class SceneManager; // ★ SceneManager を前方宣言

// ★ デバッグビルド時のみ DebugEditor をインクルード
#ifdef _DEBUG
#include "DebugEditor.h" 
#endif

/// <summary>
/// ゲームプレイシーン
/// </summary>
class GamePlayScene : public BaseScene {
public:

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

    std::vector<std::unique_ptr<Object3d>>& GetObjects() { return objects_; }
    std::vector<std::unique_ptr<Sprite>>& GetSprites() { return sprites_; }   // スプライトリスト取得 (SpriteDebugEditor用)

private:
    // --- オブジェクトレイアウト読み込み関数 ---
    void LoadObjectLayout(const std::string& filename);
    void LoadSpriteLayout(const std::string& filename);
    /// <summary>
    /// PlayerHitEvent を受け取ったときに呼ばれるコールバック関数
    /// </summary>
    void OnPlayerHit(const PlayerHitEvent& event);

private:

    // --- エンジンシステムへのポインタ ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;


    // --- ゲームオブジェクト ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;
    std::vector<std::unique_ptr<Object3d>> objects_;
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    std::unique_ptr<Text>  debugText_;
    Player* player_ = nullptr;

    // --- BGM・SE ---
    uint32_t bgmHandle_ = 0;
    bool isBGMPlaying_ = false;
    uint32_t particleSEHandle_ = 0;

    // --- ImGui用フラグ ---
    bool isDrawParticles_ = false;

    // ★ DebugEditor のインスタンス
#ifdef _DEBUG
    std::unique_ptr<DebugEditor> debugEditor_ = nullptr;
    std::unique_ptr<SpriteDebugEditor> spriteDebugEditor_ = nullptr; // スプライト編集用

#endif

    std::unique_ptr<ParticleEditor> particleEditor_;
};