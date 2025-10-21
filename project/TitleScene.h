#pragma once
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "AudioPlayer.h"
#include "ParticleSystem.h" 
#include "ParticleCommon.h" 
#include"BaseScene.h"
#include"SceneManager.h"
#include "InputManager.h"
#include <memory> // unique_ptr のため
class DirectXCommon; // 前方宣言
// ★★★★★★★★★★★★★★★★★★★

class TitleScene :public BaseScene
{
public:
    void Initialize()override;
    void Finalize()override;
    void Update()override;
    void Draw()override;


private:
    // ★★★ メンバ変数を追加 ★★★
    DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<Sprite> titleSprite_ = nullptr; // タイトル用スプライト
    // ★★★★★★★★★★★★★★★★★

    InputManager* inputManager_ = nullptr;
};