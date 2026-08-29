#define NOMINMAX
#include "SceneAssetEditorScene.h"
#include "ScenePreloader.h"

#include "Camera.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DirectXCommon.h"
#include "EnemyFactory.h"
#include "GimmickFactory.h"
#include "ItemFactory.h"
#include "GPUParticleManager.h"
#include "InputManager.h"
#include "LightEditor.h"
#include "LightManager.h"
#include "MeshEffectManager.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ObjectManager.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "ParticleSystem.h"
#include "PostEffect.h"
#include "SceneManager.h"
#include "Skybox.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteLayoutScaler.h"
#include "TextureManager.h"

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

SceneAssetEditorScene::SceneAssetEditorScene() = default;
SceneAssetEditorScene::~SceneAssetEditorScene() = default;

void SceneAssetEditorScene::LoadObjectLayout(const std::string& path) {
    if (path.empty() || !object3dCommon_ || !objectManager_) return;

    SetLoadedFilename(fs::path(path).filename().string());
    fs::path basePath(path);
    basePath.replace_extension();
    const std::vector<std::string> splitPaths = {
        basePath.string() + "_player.json",
        basePath.string() + "_enemy.json",
        basePath.string() + "_object.json",
        basePath.string() + "_camera.json"
    };
    const bool hasSplitFiles = std::any_of(splitPaths.begin(), splitPaths.end(), [](const std::string& candidate) {
        return fs::exists(candidate);
    });
    // 旧来の分割保存が1つでも存在する場合は分割群を優先し、単一JSONとの二重読込を防ぎます。
    const std::vector<std::string> paths = hasSplitFiles ? splitPaths : std::vector<std::string>{ path };

    struct LoadedObject {
        std::unique_ptr<Object3d> object;
        std::string parentGuid;
        std::string parentName;
    };
    std::vector<LoadedObject> loadedObjects;

    for (const std::string& documentPath : paths) {
        nlohmann::json document;
        if (!TakePreparedJson(documentPath, document)) {
            std::ifstream file(documentPath);
            if (!file.is_open()) continue;
            try { file >> document; } catch (...) { continue; }
        }
        if (!document.contains("objects") || !document["objects"].is_array()) continue;

        for (const nlohmann::json& objectData : document["objects"]) {
            if (!objectData.is_object()) continue;
            const std::string enemyType = objectData.value("enemyType", std::string());
            const std::string gimmickType = objectData.value("gimmickType", std::string());
            const std::string itemType = objectData.value("itemType", std::string());

            std::unique_ptr<Object3d> object;
            // ゲーム側が型を登録していれば派生型を復元し、未登録なら汎用Objectとして配置情報を残します。
            if (!enemyType.empty()) object = EnemyFactory::GetInstance()->CreateEnemy(enemyType, object3dCommon_.get());
            if (!object && !gimmickType.empty()) object = GimmickFactory::GetInstance()->CreateGimmick(gimmickType, object3dCommon_.get());
            if (!object && !itemType.empty()) object = ItemFactory::GetInstance()->CreateItem(itemType, object3dCommon_.get());
            if (!object) {
                object = std::make_unique<Object3d>();
                object->Initialize(object3dCommon_.get());
            }
            object->ImportFromJson(objectData);
            object->EnsurePersistentGuid();
            loadedObjects.push_back({
                std::move(object),
                objectData.value("parentGuid", std::string()),
                objectData.value("parentName", std::string())
            });
        }
    }

    std::unordered_map<std::string, Object3d*> byGuid;
    std::unordered_map<std::string, Object3d*> byName;
    std::unordered_set<std::string> usedGuids;
    // 全Objectの生成後に親子関係を解決し、JSON内の並び順へ依存しないようにします。
    for (LoadedObject& loaded : loadedObjects) {
        while (!usedGuids.insert(loaded.object->GetPersistentGuid()).second) loaded.object->RegeneratePersistentGuid();
        byGuid.emplace(loaded.object->GetPersistentGuid(), loaded.object.get());
        if (!loaded.object->GetName().empty()) byName.emplace(loaded.object->GetName(), loaded.object.get());
    }
    for (LoadedObject& loaded : loadedObjects) {
        Object3d* parent = nullptr;
        if (!loaded.parentGuid.empty()) {
            const auto found = byGuid.find(loaded.parentGuid);
            if (found != byGuid.end()) parent = found->second;
        }
        if (!parent && !loaded.parentName.empty()) {
            const auto found = byName.find(loaded.parentName);
            if (found != byName.end()) parent = found->second;
        }
        if (parent && parent != loaded.object.get()) loaded.object->SetParent(parent);
    }
    for (LoadedObject& loaded : loadedObjects) objectManager_->AddObject(std::move(loaded.object));
}

void SceneAssetEditorScene::LoadSpriteLayout(const std::string& path) {
    if (path.empty() || !spriteCommon_) return;
    SetLoadedSpriteFilename(fs::path(path).filename().string());

    nlohmann::json document;
    if (!TakePreparedJson(path, document)) {
        std::ifstream file(path);
        if (!file.is_open()) return;
        try { file >> document; } catch (...) { return; }
    }
    if (!document.contains("sprites") || !document["sprites"].is_array()) return;

    // 保存時の設計解像度が指定されている場合だけ、現在のウィンドウ比率へ変換します。
    const auto layoutScale = SpriteLayoutScaler::Make(document);
    std::vector<std::pair<Sprite*, std::string>> pendingParents;
    for (const nlohmann::json& spriteData : document["sprites"]) {
        if (!spriteData.is_object() || !spriteData.contains("name")) continue;
        const std::string texture = spriteData.value("texture", std::string());
        const uint32_t textureHandle = texture.empty() ? 0 : Sprite::LoadTexture(texture);
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon_.get(), textureHandle);
        sprite->SetName(spriteData.value("name", std::string()));
        sprite->SetTextureName(texture);
        if (spriteData.contains("position")) {
            sprite->SetPosition(SpriteLayoutScaler::ScalePosition(
                SpriteLayoutScaler::ReadVector2(spriteData["position"], sprite->GetPosition()), layoutScale));
        }
        if (spriteData.contains("size")) {
            sprite->SetSize(SpriteLayoutScaler::ScaleSize(
                SpriteLayoutScaler::ReadVector2(spriteData["size"], sprite->GetSize()), layoutScale));
        }
        if (spriteData.contains("anchor")) {
            sprite->SetAnchorPoint(SpriteLayoutScaler::ReadVector2(spriteData["anchor"], { 0.5f, 0.5f }));
        }
        if (spriteData.contains("color") && spriteData["color"].is_array() && spriteData["color"].size() >= 4) {
            sprite->SetColor({
                spriteData["color"][0].get<float>(), spriteData["color"][1].get<float>(),
                spriteData["color"][2].get<float>(), spriteData["color"][3].get<float>()
            });
        }
        if (spriteData.contains("emissive")) sprite->SetEmissive(spriteData["emissive"].get<float>());
        Sprite* rawSprite = sprite.get();
        pendingParents.emplace_back(rawSprite, spriteData.value("parentName", std::string()));
        sprites_.push_back(std::move(sprite));
    }
    for (const auto& [sprite, parentName] : pendingParents) {
        Sprite* parent = nullptr;
        if (!parentName.empty()) {
            for (const auto& candidate : sprites_) {
                if (candidate && candidate.get() != sprite && candidate->GetName() == parentName) {
                    parent = candidate.get();
                    break;
                }
            }
        }
        sprite->SetParent(parent);
        sprite->Update();
    }
}

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
    if (!objectLayoutPath.empty() && fs::exists(objectLayoutPath)) {
        LoadObjectLayout(objectLayoutPath);
    } else {
        SetLoadedFilename("untitled_scene.json");
    }

    const std::string spriteLayoutPath = sceneManager_
        ? sceneManager_->GetEditorSceneAssetSpritePath()
        : std::string();
    if (!spriteLayoutPath.empty() && fs::exists(spriteLayoutPath)) {
        LoadSpriteLayout(spriteLayoutPath);
    }

    LightManager::GetInstance()->LoadState(
        ResolveSceneLightPath("Resources/json/light/light_layout.json"));
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile(ResolveSceneCameraPath("game_camera.json"));
    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);

}

void SceneAssetEditorScene::Finalize() {
    CollisionManager::GetInstance()->ClearObjects();
    sprites_.clear();
    skybox_.reset();
    objectManager_.reset();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void SceneAssetEditorScene::Update(float deltaTime) {
    PostEffect::GetInstance()->Update(deltaTime);
    LightEditor::GetInstance()->Update();
    CameraEditor::GetInstance()->Update(nullptr, false);
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
    LightEditor::GetInstance()->Draw3D();

    if (particleSystem_) {
        particleSystem_->Draw();
    }

    auto& objects = objectManager_->GetObjects();
    DrawLocalFogObjects(objects, dxCommon_);
    bool grabUpdated = DrawSpecialMaterialObjects(objects, dxCommon_);
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
