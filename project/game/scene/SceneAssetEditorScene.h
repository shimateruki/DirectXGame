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
class Skybox;
class SpriteCommon;

/// Scene Assetをゲームロジックから分離して編集するための専用シーン。
/// 登録済みFactoryがない型もObject3dとして保持し、配置データを失わず編集できます。
class SceneAssetEditorScene : public BaseScene {
public:
    SceneAssetEditorScene();
    ~SceneAssetEditorScene() override;

    void Initialize() override;
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void Draw() override;
    void DrawUI() override;
    void DrawShadow() override;

    std::string GetName() override { return "Scene Asset Editor"; }

    std::vector<std::unique_ptr<Object3d>>& GetObjects() override;
    void AddObject(std::unique_ptr<Object3d> object) override;
    void RequestRemoveObject(Object3d* object) override;
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }

    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    Skybox* GetSkybox() override { return skybox_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

private:
    void LoadObjectLayout(const std::string& path);
    void LoadSpriteLayout(const std::string& path);

    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;

    std::unique_ptr<ObjectManager> objectManager_;
    std::unique_ptr<Object3dCommon> object3dCommon_;
    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<ParticleCommon> particleCommon_;
    std::unique_ptr<ParticleSystem> particleSystem_;
    std::unique_ptr<Skybox> skybox_;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    uint32_t gpuParticleTextureHandle_ = 0;
};
