#define NOMINMAX
#include "SceneAssetEditorScene.h"
#include "ScenePreloader.h"

#include "BulletManager.h"
#include "Camera.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DirectXCommon.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "InputManager.h"
#include "LevelLoader.h"
#include "LightEditor.h"
#include "LightManager.h"
#include "MeshEffectManager.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ObjectManager.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "ParticleSystem.h"
#include "Player.h"
#include "PostEffect.h"
#include "SceneManager.h"
#include "Skybox.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"

#include <filesystem>

namespace fs = std::filesystem;

SceneLoadManifest SceneAssetEditorScene::BuildAsyncLoadManifest() const {
    SceneLoadManifest manifest;
    if (!GetSceneLoadContext().objectLayoutPath.empty()) {
        manifest.AddObjectLayout(GetSceneLoadContext().objectLayoutPath);
    }
    if (!GetSceneLoadContext().spriteLayoutPath.empty()) {
        manifest.AddSpriteLayout(GetSceneLoadContext().spriteLayoutPath);
    }
    manifest.AddTexture("Resources/sprite/common/white.png");
    manifest.AddTexture(GetSceneLoadContext().skyboxPath.empty()
        ? "Resources/output_skybox.dds"
        : GetSceneLoadContext().skyboxPath);
    return manifest;
}

void SceneAssetEditorScene::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();

    EventManager::GetInstance()->ClearAllListeners();
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);
    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);
    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/common/white.png");

    ParticleManager::GetInstance()->Initialize(particleSystem_.get());
    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

    objectManager_ = std::make_unique<ObjectManager>();
    levelLoader_ = std::make_unique<LevelLoader>();
    BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    gpuParticleTextureHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");

    const uint32_t skyboxTexture = TextureManager::GetInstance()->Load(
        ResolveSceneSkyboxPath("Resources/output_skybox.dds"));
    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(object3dCommon_.get(), skyboxTexture);

    const std::string objectLayoutPath = sceneManager_
        ? sceneManager_->GetEditorSceneAssetObjectPath()
        : std::string();
    const std::string spriteLayoutPath = sceneManager_
        ? sceneManager_->GetEditorSceneAssetSpritePath()
        : std::string();

    if (!objectLayoutPath.empty() && fs::exists(objectLayoutPath)) {
        levelLoader_->LoadObjectLayout(this, objectLayoutPath);
    }
    else {
        SetLoadedFilename("untitled_scene.json");
    }

    if (!spriteLayoutPath.empty() && fs::exists(spriteLayoutPath)) {
        levelLoader_->LoadSpriteLayout(this, spriteLayoutPath);
    }

    LightManager::GetInstance()->LoadState(
        ResolveSceneLightPath("Resources/json/light/light_layout.json"));
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile(ResolveSceneCameraPath("game_camera.json"));
    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);

}

void SceneAssetEditorScene::Finalize() {
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();
    player_ = nullptr;
    sprites_.clear();
    skybox_.reset();
    levelLoader_.reset();
    objectManager_.reset();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void SceneAssetEditorScene::Update(float deltaTime) {
    PostEffect::GetInstance()->Update(deltaTime);
    LightEditor::GetInstance()->Update();
    CameraEditor::GetInstance()->Update(player_, false);
    CameraManager::GetInstance()->Update(deltaTime);

    if (objectManager_) {
        objectManager_->Update(deltaTime);
    }
    if (particleSystem_) {
        particleSystem_->Update(deltaTime);
    }
    GPUParticleManager::GetInstance()->Update(deltaTime);

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Update();
        }
    }

    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
}

void SceneAssetEditorScene::Draw() {
    if (!objectManager_ || !object3dCommon_) {
        return;
    }

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (skybox_ && camera && LightManager::GetInstance()->IsSkyboxEnabled()) {
        skybox_->SetTextureHandle(LightManager::GetInstance()->GetSkyboxTextureHandle());
        skybox_->Draw(camera->GetConstantBuffer());
    }

    ID3D12Resource* pointLight = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLight = LightManager::GetInstance()->GetSpotLightResource();
    object3dCommon_->SetGraphicsCommand();
    objectManager_->Draw(pointLight, spotLight);
    BulletManager::GetInstance()->Draw(pointLight, spotLight);
    LightEditor::GetInstance()->Draw3D();

    if (particleSystem_) {
        particleSystem_->Draw();
    }

    auto& objects = objectManager_->GetObjects();
    DrawLocalFogObjects(objects, dxCommon_, player_, false);
    bool grabUpdated = DrawSpecialMaterialObjects(
        objects,
        dxCommon_,
        BulletManager::GetInstance(),
        player_,
        false);
    grabUpdated = DrawGPUParticles(
        dxCommon_,
        camera,
        gpuParticleTextureHandle_,
        grabUpdated);

    bool hasAttachedEffects = false;
    for (const auto& object : objects) {
        if (object && (!object->GetMeshEffect1Name().empty() || !object->GetMeshEffect2Name().empty())) {
            hasAttachedEffects = true;
            break;
        }
    }
    if (!dxCommon_->IsCameraPreviewRendering() && hasAttachedEffects) {
        if (!grabUpdated) {
            dxCommon_->UpdateGrabTexture();
        }
        for (const auto& object : objects) {
            if (object && object->GetIsVisible()) {
                object->DrawAttachedEffects(pointLight, spotLight);
            }
        }
    }
}

void SceneAssetEditorScene::DrawUI() {
    if (!spriteCommon_) {
        return;
    }
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    for (const auto& sprite : sprites_) {
        if (sprite) {
            sprite->Draw();
        }
    }
}

void SceneAssetEditorScene::DrawShadow() {
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}

std::vector<std::unique_ptr<Object3d>>& SceneAssetEditorScene::GetObjects() {
    return objectManager_->GetObjects();
}

void SceneAssetEditorScene::AddObject(std::unique_ptr<Object3d> object) {
    objectManager_->AddObject(std::move(object));
}

void SceneAssetEditorScene::RequestRemoveObject(Object3d* object) {
    objectManager_->RequestRemove(object);
}
