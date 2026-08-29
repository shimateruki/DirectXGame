#define NOMINMAX
#include "RuntimeScene.h"

#include "Camera.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DirectXCommon.h"
#include "GPUParticleManager.h"
#include "InputManager.h"
#include "LightEditor.h"
#include "LightManager.h"
#include "Object3dCommon.h"
#include "ObjectManager.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "ParticleSystem.h"
#include "Player.h"
#include "PostEffect.h"
#include "Skybox.h"
#include "SpriteCommon.h"
#include "TextureManager.h"

#include <utility>

RuntimeScene::RuntimeScene() = default;
RuntimeScene::~RuntimeScene() = default;

void RuntimeScene::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();

    CollisionManager::GetInstance()->ClearObjects();
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    // 派生Sceneが同じ描画順序を使えるよう、共通Serviceをここで一括構築します。
    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);
    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);
    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/common/white.png");

    objectManager_ = std::make_unique<ObjectManager>();
    ParticleManager::GetInstance()->Initialize(particleSystem_.get());
    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    gpuParticleTextureHandle_ =
        TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");

    const uint32_t skyboxTexture =
        TextureManager::GetInstance()->Load("Resources/output_skybox.dds");
    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(object3dCommon_.get(), skyboxTexture);

    LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
    InitializeSceneContents();
}

void RuntimeScene::Finalize() {
    // 非所有Player参照を先に無効化してから、Serviceを初期化と逆順で解放します。
    CollisionManager::GetInstance()->ClearObjects();
    player_ = nullptr;
    sprites_.clear();
    skybox_.reset();
    objectManager_.reset();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void RuntimeScene::Update(float deltaTime) {
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

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Update();
        }
    }
    CollisionManager::GetInstance()->Update();
}

void RuntimeScene::Draw() {
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
    LightEditor::GetInstance()->Draw3D();

    if (particleSystem_) {
        particleSystem_->Draw();
    }

    auto& objects = objectManager_->GetObjects();
    DrawLocalFogObjects(objects, dxCommon_);
    bool grabUpdated = DrawSpecialMaterialObjects(objects, dxCommon_);
    DrawGPUParticles(dxCommon_, camera, gpuParticleTextureHandle_, grabUpdated);
}

void RuntimeScene::DrawUI() {
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

void RuntimeScene::DrawShadow() {
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}

std::vector<std::unique_ptr<Object3d>>& RuntimeScene::GetObjects() {
    return objectManager_->GetObjects();
}

void RuntimeScene::AddObject(std::unique_ptr<Object3d> object) {
    objectManager_->AddObject(std::move(object));
}

void RuntimeScene::RequestRemoveObject(Object3d* object) {
    objectManager_->RequestRemove(object);
}

Player* RuntimeScene::CreateDefaultPlayer(const Vector3& position) {
    if (!objectManager_ || !object3dCommon_) {
        return nullptr;
    }

    auto player = std::make_unique<Player>();
    player->Initialize(object3dCommon_.get(), inputManager_, particleSystem_.get(), spriteCommon_.get());
    player->SetTranslate(position);
    player_ = player.get();
    objectManager_->AddObject(std::move(player));
    return player_;
}
