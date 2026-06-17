#include "BaseScene.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Player.h"
#include "BulletManager.h"
#include "Camera.h"
#include "GPUParticleManager.h"

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
    if (!dxCommon || !camera || GPUParticleManager::GetInstance()->IsEmpty()) {
        return grabAlreadyUpdated;
    }

    if (!grabAlreadyUpdated) {
        dxCommon->UpdateGrabTexture();
        grabAlreadyUpdated = true;
    }

    dxCommon->PreDrawLocalFog();
    GPUParticleManager::GetInstance()->Draw(
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
