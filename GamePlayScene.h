#pragma once

#include "BaseScene.h"
#include "AudioPlayer.h"
#include "debugCamera.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"

#include <memory>
#include <vector>

class Framework;

// ゲームプレイシーン
class GamePlayScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // Frameworkからポインタを受け取る
    Framework* framework_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;

    // --- オーディオ関連 ---
    std::unique_ptr<AudioPlayer> audioPlayer_;
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masteringVoice_ = nullptr;
    SoundData soundData1_{};
    bool audioPlayedOnce_ = false;

    // --- 描画オブジェクト関連 ---
    std::unique_ptr<DebugCamera> debugCamera_;
    std::unique_ptr<Object3dCommon> object3dCommon_;
    std::unique_ptr<SpriteCommon> spriteCommon_;

    std::vector<std::unique_ptr<Object3d>> objects_;
    std::unique_ptr<Sprite> sprite_;
};