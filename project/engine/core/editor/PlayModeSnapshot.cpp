#include "engine/core/editor/PlayModeSnapshot.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "Camera.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebrisEffectManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "ParticleSystem.h"
#include "SceneManager.h"
#include "game/system/combat/BulletManager.h"

#include <unordered_map>
#include <unordered_set>

BaseScene* PlayModeSnapshot::CurrentScene() const {
    if (!sceneManager_ || !scene_ || sceneManager_->IsTransitioning() ||
        sceneManager_->GetSceneGeneration() != sceneGeneration_ ||
        sceneManager_->GetCurrentScene() != scene_) {
        return nullptr;
    }
    return scene_;
}

void PlayModeSnapshot::Reset() {
    if (BaseScene* scene = CurrentScene()) {
        for (auto& object : scene->GetObjects()) {
            if (!object || !object->IsReplayRetained()) continue;
            object->SetReplayRetained(false);
            if (object->IsReplayRemoved()) scene->RequestRemoveObject(object.get());
        }
        scene->ReleaseReplaySprites();
    }
    objects_.clear();
    sprites_.clear();
    sceneState_ = json::object();
    camera_ = {};
    scene_ = nullptr;
    sceneGeneration_ = 0;
}

bool PlayModeSnapshot::Capture() {
    Reset();
    if (!sceneManager_ || sceneManager_->IsTransitioning()) return false;
    scene_ = sceneManager_->GetCurrentScene();
    if (!scene_) return false;
    sceneGeneration_ = sceneManager_->GetSceneGeneration();

    // 保存前の編集内容も含める。ディスクのScene JSONへは書き込まない。
    for (auto& object : scene_->GetObjects()) {
        if (!object || object->IsEditorInternal()) continue;
        object->SetReplayRetained(true);
        objects_.push_back({object->EnsureReplayId(), object->CaptureReplayState()});
    }
    std::vector<engine::Sprite*> currentSprites;
    scene_->CollectReplaySprites(currentSprites);
    std::unordered_set<engine::Sprite*> uniqueSprites;
    for (engine::Sprite* sprite : currentSprites) {
        if (!sprite || !uniqueSprites.insert(sprite).second) continue;
        sprite->SetReplayRetained(true);
        sprites_.push_back({sprite->EnsureReplayId(), sprite->CaptureReplayState()});
    }
    scene_->CaptureReplaySceneState(sceneState_);
    if (Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
        camera_.valid = true;
        camera_.eye = camera->GetEye();
        camera_.target = camera->GetTargetPoint();
        camera_.rotation = camera->GetRotation();
        camera_.fovY = camera->GetFovY();
        camera_.nearClip = camera->GetNearClip();
        camera_.farClip = camera->GetFarClip();
    }
    return true;
}

bool PlayModeSnapshot::Restore() {
    BaseScene* scene = CurrentScene();
    if (!scene) {
        Reset();
        return false;
    }

    scene->RestoreReplaySceneState(sceneState_);
    auto& currentObjects = scene->GetObjects();
    std::unordered_map<uint64_t, engine::Object3d*> objectsById;
    for (auto& object : currentObjects) {
        if (object && !object->IsEditorInternal())
            objectsById[object->EnsureReplayId()] = object.get();
    }
    std::unordered_set<uint64_t> originalObjectIds;
    CollisionManager* collisionManager = CollisionManager::GetInstance();
    for (const ObjectState& saved : objects_) {
        originalObjectIds.insert(saved.id);
        const auto found = objectsById.find(saved.id);
        if (found == objectsById.end()) continue;
        engine::Object3d* object = found->second;
        const bool wasRemoved = object->IsReplayRemoved();
        object->RestoreReplayState(saved.state);
        if (wasRemoved && !object->IsReplayRemoved()) collisionManager->AddObject(object);
        if (!wasRemoved && object->IsReplayRemoved()) collisionManager->RemoveObject(object);
    }

    // Play中に生成したObjectだけを撤去する。配列走査中の削除は避ける。
    std::vector<engine::Object3d*> runtimeObjects;
    for (const auto& object : currentObjects) {
        if (object && !object->IsEditorInternal() &&
            !originalObjectIds.contains(object->EnsureReplayId()))
            runtimeObjects.push_back(object.get());
    }
    for (engine::Object3d* object : runtimeObjects) scene->DestroyObject(object);

    // 親子Transformのローカル値を戻してからルートの行列を更新する。
    std::unordered_set<engine::Object3d*> managedObjects;
    for (const auto& object : currentObjects)
        if (object && !object->IsReplayRemoved()) managedObjects.insert(object.get());
    for (const auto& object : currentObjects) {
        if (!object || object->IsReplayRemoved()) continue;
        engine::Object3d* parent = object->GetParent();
        if (!parent || !managedObjects.contains(parent)) object->UpdateWorldMatrix();
    }

    std::vector<engine::Sprite*> currentSprites;
    scene->CollectReplaySprites(currentSprites);
    std::unordered_map<uint64_t, engine::Sprite*> spritesById;
    for (engine::Sprite* sprite : currentSprites)
        if (sprite) spritesById[sprite->EnsureReplayId()] = sprite;
    std::unordered_set<uint64_t> originalSpriteIds;
    for (const SpriteState& saved : sprites_) {
        originalSpriteIds.insert(saved.id);
        const auto found = spritesById.find(saved.id);
        if (found != spritesById.end()) found->second->RestoreReplayState(saved.state);
    }
    for (const SpriteState& saved : sprites_) {
        const auto found = spritesById.find(saved.id);
        if (found == spritesById.end()) continue;
        engine::Sprite* parent = nullptr;
        if (saved.state.parentReplayId != 0) {
            const auto parentFound = spritesById.find(saved.state.parentReplayId);
            if (parentFound != spritesById.end()) parent = parentFound->second;
        }
        found->second->SetParent(parent, false);
        found->second->RefreshAfterReplayRestore();
    }
    std::vector<engine::Sprite*> runtimeSprites;
    for (const auto& [id, sprite] : spritesById)
        if (!originalSpriteIds.contains(id)) runtimeSprites.push_back(sprite);
    for (engine::Sprite* sprite : runtimeSprites) {
        if (scene->DestroySprite(sprite)) continue;
        sprite->SetParent(nullptr, false);
        sprite->SetReplayRemoved(true);
        sprite->SetVisible(false);
    }
    if (camera_.valid) {
        if (Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
            camera->ApplyReplayView(camera_.eye, camera_.target, camera_.rotation,
                camera_.fovY, camera_.nearClip, camera_.farClip);
        }
    }

    BulletManager::GetInstance()->Clear();
    MeshEffectManager::GetInstance()->Clear();
    DebrisEffectManager::GetInstance()->Clear();
    GPUParticleManager::GetInstance()->ClearAllAutoEmitters();
    if (engine::ParticleSystem* particles = scene->GetParticleSystem()) particles->Clear();
    Reset();
    return true;
}

#endif
