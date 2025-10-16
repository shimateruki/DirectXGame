#pragma once

// ★★★ ヘッダーファイルをインクルードするように修正 ★★★
#include "engine/3d/Object3dCommon.h"
#include "engine/2d/SpriteCommon.h"
#include "engine/3d/Object3d.h"
#include "engine/2d/Sprite.h"
#include "engine/audio/AudioPlayer.h"
#include "engine/3d/ParticleSystem.h" 
#include "engine/3d/ParticleCommon.h" 

#include <memory>
#include <vector>

// --- 前方宣言 (ポインタで持つものだけ) ---
class DirectXCommon;
class InputManager;

/// <summary>
/// ゲームプレイシーン
/// </summary>
class GamePlayScene {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 更新
    /// </summary>
    void Update();

    /// <summary>
    /// 描画
    /// </summary>
    void Draw();

private:
    // --- エンジンシステムへのポインタ ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;

    // --- ゲームオブジェクト ---
    std::unique_ptr<Object3dCommon> object3dCommon_;
    std::unique_ptr<SpriteCommon> spriteCommon_;

    std::vector<std::unique_ptr<Object3d>> objects_;
    std::vector<std::unique_ptr<Sprite>> sprites_; 

    std::unique_ptr<ParticleCommon> particleCommon_;
    std::unique_ptr<ParticleSystem> particleSystem_;
    bool isDrawParticles_ = false; // パーティクル描画フラグ

    // --- サウンドデータ ---
    AudioPlayer::AudioHandle bgmHandle_ = 0;
};