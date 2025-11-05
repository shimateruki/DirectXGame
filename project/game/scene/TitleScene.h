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


class TitleScene :public BaseScene
{
public:
    void Initialize()override;
    void Finalize()override;
    void Update(float deltaTime)override;
    void Draw()override;
    void LoadObjectLayout(const std::string& filename);
    void LoadSpriteLayout(const std::string& filename);
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objects_; }
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    void RequestRemoveObject(Object3d* object) override;

private:
    DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    std::unique_ptr<Object3dCommon> object3dCommon_;
    std::vector<std::unique_ptr<Object3d>> objects_;

    InputManager* inputManager_ = nullptr;
    uint32_t titleLogoHandle_ = 0;

    /// <summary>
    /// 削除予約されたオブジェクトのリスト
    /// </summary>
    std::vector<Object3d*> removalList_;

    /// <summary>
    /// 予約されたオブジェクトを安全に削除する
    /// </summary>
    void ProcessRemovals();
};