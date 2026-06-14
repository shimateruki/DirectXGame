#include "BulletManager.h"
#include "CollisionManager.h"
#include "Object3dCommon.h"

namespace {
bool IsSpecialMaterial(int32_t materialType) {
    return materialType >= 8 && materialType <= 22;
}

void DrawSpecialBullet(Bullet* bullet, uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (!bullet || bullet->IsDead()) {
        return;
    }

    switch (bullet->GetMaterialType()) {
    case 8:
        bullet->DrawWater(depthSrvHandle, grabSrvHandle);
        break;
    case 9:
        bullet->DrawMagma(depthSrvHandle, grabSrvHandle);
        break;
    case 10:
        bullet->DrawIce(depthSrvHandle, grabSrvHandle);
        break;
    case 11:
        bullet->DrawFire(depthSrvHandle, grabSrvHandle);
        break;
    case 12:
        bullet->DrawLaser(depthSrvHandle, grabSrvHandle);
        break;
    case 13:
        bullet->DrawSlimeGel(depthSrvHandle, grabSrvHandle);
        break;
    case 14:
        bullet->DrawShockwave(depthSrvHandle, grabSrvHandle);
        break;
    case 15:
        bullet->DrawLiquidContact(depthSrvHandle, grabSrvHandle);
        break;
    case 16:
        bullet->DrawDamageCrack(depthSrvHandle, grabSrvHandle);
        break;
    case 17:
        bullet->DrawUpdraft(depthSrvHandle, grabSrvHandle);
        break;
    case 18:
        bullet->DrawStunBind(depthSrvHandle, grabSrvHandle);
        break;
    case 19:
        bullet->DrawCrownUnlock(depthSrvHandle, grabSrvHandle);
        break;
    case 20:
        bullet->DrawPoisonSpore(depthSrvHandle, grabSrvHandle);
        break;
    case 21:
        bullet->DrawCloud(depthSrvHandle, grabSrvHandle);
        break;
    case 22:
        bullet->DrawGatePortal(depthSrvHandle, grabSrvHandle);
        break;
    default:
        break;
    }
}
}

BulletManager* BulletManager::GetInstance() {
    static BulletManager instance;
    return &instance;
}

void BulletManager::Initialize(Object3dCommon* common, CollisionManager* colManager) {
    common_ = common;
    colManager_ = colManager;
    bullets_.clear();
}

void BulletManager::Finalize() {
    bullets_.clear();
    common_ = nullptr;
    colManager_ = nullptr;
}

void BulletManager::Fire(const Vector3& pos, const Vector3& vel,
    uint32_t attr, uint32_t mask,
    const std::string& model, float radius, float life,
    const BulletVisualConfig& visualConfig) {

    if (!common_ || !colManager_) {
        return;
    }

    auto bullet = std::make_unique<Bullet>();
    bullet->Initialize(common_);
    bullet->SetModel(model);
    bullet->SetScale({ visualConfig.visualScale, visualConfig.visualScale, visualConfig.visualScale });
    bullet->SetMaterialType(visualConfig.materialType);
    bullet->SetBlendMode(visualConfig.blendMode);
    bullet->SetColor(visualConfig.color);
    bullet->SetEmissive(visualConfig.emissive);
    if (!visualConfig.texturePath.empty()) {
        bullet->SetTexture(visualConfig.texturePath);
    }

    if (MeshRenderer* renderer = bullet->GetMeshRenderer()) {
        if (auto* param = renderer->GetWaterParamData()) {
            param->effectType = visualConfig.effectType;
            param->effectScale = visualConfig.effectScale;
            param->effectSoftness = visualConfig.effectSoftness;
            param->effectIntensity = visualConfig.effectIntensity;
            param->billboardScale = visualConfig.billboardScale;
        }
    }

    bullet->SetColliderType(ColliderType::kSphere);
    bullet->SetCollisionRadius(radius);
    bullet->SetCollisionSize({ radius, radius, radius });
    bullet->Fire(pos, vel, life, attr, mask);

    colManager_->AddObject(bullet.get());
    bullets_.push_back(std::move(bullet));
}

void BulletManager::Update(float deltaTime) {
    if (!colManager_) {
        return;
    }

    for (auto& bullet : bullets_) {
        bullet->Update(deltaTime);
        bullet->UpdateLocalMatrix();
        bullet->UpdateWorldMatrix();
    }

    bullets_.remove_if([this](const std::unique_ptr<Bullet>& bullet) {
        if (bullet->IsDead()) {
            colManager_->RemoveObject(bullet.get());
            return true;
        }
        return false;
        });
}

void BulletManager::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    for (auto& bullet : bullets_) {
        if (IsSpecialMaterial(bullet->GetMaterialType())) {
            continue;
        }
        bullet->Draw(pointLightResource, spotLightResource);
    }
}

bool BulletManager::HasSpecialMaterialBullets() const {
    for (const auto& bullet : bullets_) {
        if (!bullet->IsDead() && IsSpecialMaterial(bullet->GetMaterialType())) {
            return true;
        }
    }
    return false;
}

void BulletManager::DrawSpecial(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    for (auto& bullet : bullets_) {
        if (!IsSpecialMaterial(bullet->GetMaterialType())) {
            continue;
        }
        DrawSpecialBullet(bullet.get(), depthSrvHandle, grabSrvHandle);
    }
}
