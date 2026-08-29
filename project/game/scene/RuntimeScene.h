#pragma once

#include "BaseScene.h"

#include <memory>
#include <vector>

class DirectXCommon;
class InputManager;
class Object3dCommon;
class ObjectManager;
class ParticleCommon;
class ParticleSystem;
class Player;
class Skybox;
class SpriteCommon;

/// Title、Game、GameOverで共用するRuntime Sceneの描画基盤です。
/// 選択可能なSceneとしては登録せず、派生Sceneが固有Objectの生成だけを実装します。
class RuntimeScene : public BaseScene {
public:
    RuntimeScene();
    ~RuntimeScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

    std::vector<std::unique_ptr<Object3d>>& GetObjects() override;
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }
    void AddObject(std::unique_ptr<Object3d> object) override;
    void RequestRemoveObject(Object3d* object) override;

    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }
    Skybox* GetSkybox() override { return skybox_.get(); }
    Player* GetPlayer() const override { return player_; }
    void SetPlayer(Player* player) override { player_ = player; }

protected:
    /// 共通Serviceの初期化後に呼ばれる、派生Scene固有の初期配置入口です。
    virtual void InitializeSceneContents() {}

    /// WASD移動とJumpを持つ基本Playerを生成し、ObjectManagerへ所有権を渡します。
    Player* CreateDefaultPlayer(const Vector3& position);

private:
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    std::unique_ptr<ObjectManager> objectManager_;
    std::unique_ptr<Object3dCommon> object3dCommon_;
    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<ParticleCommon> particleCommon_;
    std::unique_ptr<ParticleSystem> particleSystem_;
    std::unique_ptr<Skybox> skybox_;
    std::vector<std::unique_ptr<Sprite>> sprites_;
    Player* player_ = nullptr; // 所有権はobjectManager_が持ちます。
    uint32_t gpuParticleTextureHandle_ = 0;
};
