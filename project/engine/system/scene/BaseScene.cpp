#include "BaseScene.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Player.h"
#include "BulletManager.h"
#include "Camera.h"
#include "GPUParticleManager.h"
#include "LightManager.h"
#include "SceneManager.h"
#include <algorithm>

std::string BaseScene::ResolvePrimaryObjectLayoutPath(const std::string& defaultPath) {
    if (sceneAssetObjectLayoutConsumed_ || !sceneLoadContext_.IsSceneAsset()) {
        return defaultPath;
    }

    sceneAssetObjectLayoutConsumed_ = true;
    const std::string& assetPath = sceneLoadContext_.objectLayoutPath;
    return assetPath.empty() ? defaultPath : assetPath;
}

std::string BaseScene::ResolvePrimarySpriteLayoutPath(const std::string& defaultPath) {
    if (sceneAssetSpriteLayoutConsumed_ || !sceneLoadContext_.IsSceneAsset()) {
        return defaultPath;
    }

    sceneAssetSpriteLayoutConsumed_ = true;
    const std::string& assetPath = sceneLoadContext_.spriteLayoutPath;
    return assetPath.empty() ? defaultPath : assetPath;
}

bool BaseScene::Destroy(Object3d* object) {
    return DestroyObject(object);
}

bool BaseScene::Destroy(Sprite* sprite) {
    return DestroySprite(sprite);
}

void BaseScene::CollectReplaySprites(std::vector<Sprite*>& replaySprites) {
    auto& sprites = GetSprites();
    replaySprites.reserve(replaySprites.size() + sprites.size());
    for (auto& sprite : sprites) {
        if (sprite) {
            replaySprites.push_back(sprite.get());
        }
    }
}

void BaseScene::CaptureReplaySceneState(json& state) const {
    state = json::object();
}

void BaseScene::RestoreReplaySceneState(const json& state) {
    (void)state;
}

void BaseScene::ReleaseReplaySprites() {
    std::vector<Sprite*> replaySprites;
    CollectReplaySprites(replaySprites);
    for (Sprite* sprite : replaySprites) {
        if (sprite) {
            sprite->SetReplayRetained(false);
        }
    }

    auto& sprites = GetSprites();
    sprites.erase(
        std::remove_if(sprites.begin(), sprites.end(), [](const std::unique_ptr<Sprite>& sprite) {
            if (!sprite || !sprite->IsReplayRemoved()) {
                return false;
            }
            sprite->SetParent(nullptr, false);
            return true;
        }),
        sprites.end());
}

void BaseScene::RefreshRenderCameraData() {
    auto& objects = GetObjects();
    for (auto& object : objects) {
        if (object) {
            object->RefreshRenderCameraData();
        }
    }

    if (Player* player = GetPlayer()) {
        player->RefreshRenderCameraData();
    }
}

void BaseScene::DrawCameraPreview(Camera* camera, int previewBufferIndex) {
    if (!camera) {
        return;
    }

    ID3D12Resource* pointLight = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLight = LightManager::GetInstance()->GetSpotLightResource();
    auto& objects = GetObjects();

    auto drawObject = [&](Object3d* object, bool transparentPass) {
        if (!object || !object->GetIsVisible()) {
            return;
        }

        const int materialType = object->GetMaterialType();
        const bool isTransparent = (materialType == 1);
        const bool isSpecialMaterial = (materialType == 7 || (materialType >= 8 && materialType <= 22));
        if (isSpecialMaterial || isTransparent != transparentPass) {
            return;
        }

        object->DrawForCamera(camera, pointLight, spotLight, previewBufferIndex);
    };

    // 演出用カメラPreviewでは通常描画用のScene::Draw()を再利用せず、
    // 共有WVPを汚さない専用描画だけを通します。
    for (auto& object : objects) {
        drawObject(object.get(), false);
    }
    for (auto& object : objects) {
        drawObject(object.get(), true);
    }
}

bool BaseScene::DestroyObject(Object3d* object) {
    if (!IsAlive(object)) {
        return false;
    }

    object->SetIsVisible(false);
    RequestRemoveObject(object);
    return true;
}

bool BaseScene::DestroySprite(Sprite* sprite) {
    auto& sprites = GetSprites();
    auto it = std::find_if(sprites.begin(), sprites.end(), [sprite](const std::unique_ptr<Sprite>& candidate) {
        return candidate && candidate.get() == sprite;
    });
    if (it == sprites.end()) {
        return false;
    }

    if (sprite->IsReplayRetained()) {
        std::vector<Sprite*> children = sprite->GetChildren();
        for (Sprite* child : children) {
            if (child) {
                child->SetParent(nullptr);
            }
        }
        sprite->SetParent(nullptr);
        sprite->SetReplayRemoved(true);
        sprite->SetVisible(false);
        return true;
    }

    std::vector<Sprite*> children = sprite->GetChildren();
    for (Sprite* child : children) {
        if (child) {
            child->SetParent(nullptr);
        }
    }
    sprite->SetParent(nullptr);
    sprite->SetVisible(false);
    sprites.erase(it);
    return true;
}

bool BaseScene::IsAlive(Object3d* object) {
    if (!object) {
        return false;
    }

    auto& objects = GetObjects();
    return std::any_of(objects.begin(), objects.end(), [object](const std::unique_ptr<Object3d>& candidate) {
        return candidate && candidate.get() == object;
    });
}

bool BaseScene::IsAlive(Sprite* sprite) {
    if (!sprite) {
        return false;
    }

    auto& sprites = GetSprites();
    return std::any_of(sprites.begin(), sprites.end(), [sprite](const std::unique_ptr<Sprite>& candidate) {
        return candidate && candidate.get() == sprite && !candidate->IsReplayRemoved();
    });
}

bool BaseScene::IsSpecialMaterialType(int materialType) const {
    return materialType >= 8 && materialType <= 22;
}

bool BaseScene::IsHiddenByFirstPerson(Object3d* object, Player* player, bool isFirstPerson) const {
    if (!isFirstPerson || !object || !player) {
        return false;
    }

    Object3d* current = object;
    while (current) {
        if (current == player) {
            return true;
        }
        current = current->GetParent();
    }
    return false;
}

bool BaseScene::DrawLocalFogObjects(std::vector<std::unique_ptr<Object3d>>& objects, DirectXCommon* dxCommon, Player* player, bool isFirstPerson) {
    if (!dxCommon) {
        return false;
    }
    if (dxCommon->IsCameraPreviewRendering()) {
        return false;
    }

    bool hasFog = false;
    for (auto& obj : objects) {
        if (obj && obj->GetIsVisible() && obj->GetMaterialType() == 7 && !IsHiddenByFirstPerson(obj.get(), player, isFirstPerson)) {
            hasFog = true;
            break;
        }
    }
    if (!hasFog) {
        return false;
    }

    dxCommon->PreDrawLocalFog();
    for (auto& obj : objects) {
        if (obj && obj->GetIsVisible() && obj->GetMaterialType() == 7 && !IsHiddenByFirstPerson(obj.get(), player, isFirstPerson)) {
            obj->DrawLocalFog(dxCommon->GetDepthSrvHandle());
        }
    }
    dxCommon->PostDrawLocalFog();
    return true;
}

bool BaseScene::DrawSpecialMaterialObjects(std::vector<std::unique_ptr<Object3d>>& objects, DirectXCommon* dxCommon, BulletManager* bulletManager, Player* player, bool isFirstPerson) {
    if (!dxCommon) {
        return false;
    }
    if (dxCommon->IsCameraPreviewRendering()) {
        return false;
    }

    bool hasSpecialObjects = false;
    for (auto& obj : objects) {
        if (obj && obj->GetIsVisible() && IsSpecialMaterialType(obj->GetMaterialType()) && !IsHiddenByFirstPerson(obj.get(), player, isFirstPerson)) {
            hasSpecialObjects = true;
            break;
        }
    }
    const bool hasSpecialBullets = bulletManager && bulletManager->HasSpecialMaterialBullets();
    if (!hasSpecialObjects && !hasSpecialBullets) {
        return false;
    }

    dxCommon->UpdateGrabTexture();
    if (hasSpecialObjects) {
        const uint32_t depthSrvHandle = dxCommon->GetDepthSrvHandle();
        const uint32_t grabSrvHandle = dxCommon->GetGrabSrvHandle();
        for (auto& obj : objects) {
            if (!obj || !obj->GetIsVisible() || IsHiddenByFirstPerson(obj.get(), player, isFirstPerson)) {
                continue;
            }

            switch (obj->GetMaterialType()) {
            case 8:
                obj->DrawWater(depthSrvHandle, grabSrvHandle);
                break;
            case 9:
                obj->DrawMagma(depthSrvHandle, grabSrvHandle);
                break;
            case 10:
                obj->DrawIce(depthSrvHandle, grabSrvHandle);
                break;
            case 11:
                obj->DrawFire(depthSrvHandle, grabSrvHandle);
                break;
            case 12:
                obj->DrawLaser(depthSrvHandle, grabSrvHandle);
                break;
            case 13:
                obj->DrawSlimeGel(depthSrvHandle, grabSrvHandle);
                break;
            case 14:
                obj->DrawShockwave(depthSrvHandle, grabSrvHandle);
                break;
            case 15:
                obj->DrawLiquidContact(depthSrvHandle, grabSrvHandle);
                break;
            case 16:
                obj->DrawDamageCrack(depthSrvHandle, grabSrvHandle);
                break;
            case 17:
                obj->DrawUpdraft(depthSrvHandle, grabSrvHandle);
                break;
            case 18:
                obj->DrawStunBind(depthSrvHandle, grabSrvHandle);
                break;
            case 19:
                obj->DrawCrownUnlock(depthSrvHandle, grabSrvHandle);
                break;
            case 20:
                obj->DrawPoisonSpore(depthSrvHandle, grabSrvHandle);
                break;
            case 21:
                obj->DrawCloud(depthSrvHandle, grabSrvHandle);
                break;
            case 22:
                obj->DrawGatePortal(depthSrvHandle, grabSrvHandle);
                break;
            default:
                break;
            }
        }
    }

    if (hasSpecialBullets) {
        bulletManager->DrawSpecial(dxCommon->GetDepthSrvHandle(), dxCommon->GetGrabSrvHandle());
    }
    return true;
}

bool BaseScene::DrawGPUParticles(DirectXCommon* dxCommon, Camera* camera, uint32_t textureHandle, bool grabAlreadyUpdated) {
    GPUParticleManager* particleManager = GPUParticleManager::GetInstance();
    if (!dxCommon || !camera || particleManager->IsEmpty()) {
        return grabAlreadyUpdated;
    }
    if (dxCommon->IsCameraPreviewRendering()) {
        return grabAlreadyUpdated;
    }

    // 加算・半透明パーティクルは背景色を参照しないため、全画面コピーを省略します。
    if (!grabAlreadyUpdated && particleManager->RequiresSceneColorCopy()) {
        dxCommon->UpdateGrabTexture();
        grabAlreadyUpdated = true;
    }

    dxCommon->PreDrawLocalFog();
    particleManager->Draw(
        dxCommon->GetCommandList(),
        camera->GetViewMatrix(),
        camera->GetProjectionMatrix(),
        textureHandle,
        dxCommon->GetDepthSrvHandle()
    );
    dxCommon->PostDrawLocalFog();
    return grabAlreadyUpdated;
}

void BaseScene::TriggerEvent(int targetID) {
    // IDが -1 (設定なし) なら何もしない
    if (targetID == -1) return;



    // シーン内の全オブジェクトを取得 
    auto& objects = GetObjects();

    for (auto& obj : objects) {
        // 「俺の受信ID、呼ばれた番号と同じだ！」
        if (obj->GetEventID() == targetID) {
            // アクションを実行！
            obj->OnTrigger();
        }
    }
}

void BaseScene::SetEventActive(int targetID, bool active) {
    if (targetID == -1) return;

    auto& objects = GetObjects();
    for (auto& obj : objects) {
        if (obj->GetEventID() == targetID) {
            obj->OnSwitchEvent(active);
        }
    }
}

Object3d* BaseScene::FindObjectByEventID(int eventID) {
    // IDなしなら無視
    if (eventID == -1) return nullptr;

    auto& objects = GetObjects();
    for (auto& obj : objects) {
        if (obj->GetEventID() == eventID) {
            return obj.get(); // 見つけたオブジェクトそのものを返す！
        }
    }
    return nullptr;
}

// 名前からスプライトを取得する
Sprite* BaseScene::GetSpriteByName(const std::string& name) {
    auto& sprites = GetSprites();
    for (auto& sprite : sprites) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr; // 見つからなかった場合
}
