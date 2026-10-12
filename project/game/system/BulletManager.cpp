#include "BulletManager.h"
#include "CollisionManager.h"
#include "Object3dCommon.h"
#include <algorithm>

namespace {
bool IsSpecialMaterial(int32_t materialType) {
    // 深度や画面コピーを使う弾を通常の不透明描画から分ける。
    return (materialType >= 8 && materialType <= 22) || materialType == 26;
}

void DrawSpecialBullet(Bullet* bullet, uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 素材ごとの描画経路に深度・画面コピーを渡す。
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
    case 26:
        bullet->DrawWindOrb(depthSrvHandle, grabSrvHandle);
        break;
    default:
        break;
    }
}
} // namespace

BulletManager* BulletManager::GetInstance() {
    // 弾リストと衝突登録をシーン内で一箇所に集約する。
    static BulletManager instance;
    return &instance;
}

void BulletManager::Initialize(Object3dCommon* common, CollisionManager* colManager) {
    // 新しいシーンの描画・衝突基盤へ付け替え、旧弾を残さない。
    common_ = common;
    colManager_ = colManager;
    bullets_.clear();
}

void BulletManager::Finalize() {
    // CollisionManagerへ登録した弾を先に外してから参照を破棄する。
    Clear();
    common_ = nullptr;
    colManager_ = nullptr;
}

void BulletManager::Clear() {
    // 生存中の弾も衝突リストから外し、ダングリング参照を防ぐ。
    if (colManager_) {
        for (const auto& bullet : bullets_) {
            if (bullet) {
                colManager_->RemoveObject(bullet.get());
            }
        }
    }
    bullets_.clear();
}

void BulletManager::Fire(const Vector3& pos, const Vector3& vel, uint32_t attr, uint32_t mask, const std::string& model,
                         float radius, float life, const BulletVisualConfig& visualConfig, float damage,
                         const StatusEffectApplication& statusEffect, DamageType damageType, float gravity) {

    if (!common_ || !colManager_) {
        return;
    }

    auto bullet = std::make_unique<Bullet>();
    bullet->Initialize(common_);
    if (!visualConfig.debugLabel.empty()) bullet->SetName(visualConfig.debugLabel);
    bullet->SetModel(model);
    bullet->SetScale({visualConfig.visualScale, visualConfig.visualScale, visualConfig.visualScale});
    bullet->SetMaterialType(visualConfig.materialType);
    bullet->SetBlendMode(visualConfig.blendMode);
    bullet->SetColor(visualConfig.color);
    bullet->SetEmissive(visualConfig.emissive);
    if (!visualConfig.texturePath.empty()) {
        bullet->SetTexture(visualConfig.texturePath);
    }

    if (MeshRenderer* renderer = bullet->GetMeshRenderer()) {
        if (auto* param = renderer->GetWaterParamData()) {
            param->waveSpeed = visualConfig.waveSpeed;
            param->waveHeight = visualConfig.waveHeight;
            param->waveFrequency = visualConfig.waveFrequency;
            param->effectType = visualConfig.effectType;
            param->effectScale = visualConfig.effectScale;
            param->effectSoftness = visualConfig.effectSoftness;
            param->effectIntensity = visualConfig.effectIntensity;
            param->billboardScale = visualConfig.billboardScale;
            if (visualConfig.magmaHopperPresentation) {
                // 通常弾と同じ描画経路で、液面より下だけを隠す。
                param->effectScaleY = pos.y - radius * 0.5f;
            }
        }
    }

    bullet->SetColliderType(ColliderType::kSphere);
    bullet->SetCollisionRadius(radius);
    bullet->SetCollisionSize({radius, radius, radius});
    if (visualConfig.collisionRadiusIsWorld && bullet->GetCollider())
        bullet->GetCollider()->SetScaleOverride({1.0f, 1.0f, 1.0f});
    if (visualConfig.magmaHopperPresentation && bullet->GetCollider()) {
        bullet->GetCollider()->SetScaleOverride({1.0f, 1.0f, 1.0f});
        bullet->SetCastShadow(false);
    }
    bullet->Fire(pos, vel, life, attr, mask, damage, statusEffect, damageType);
    bullet->SetMagmaHopperPresentation(visualConfig.magmaHopperPresentation);
    bullet->SetExplosionRadius(visualConfig.explosionRadius);
    if (visualConfig.explosionRadius > 0.0f && bullet->GetCollider())
        bullet->GetCollider()->SetScaleOverride({1.0f, 1.0f, 1.0f});
    bullet->SetGravity((std::max)(0.0f, gravity));
    bullet->ConfigureVfx(visualConfig.trailPreset, visualConfig.trailSecondaryPreset, visualConfig.impactPreset,
                         visualConfig.impactSecondaryPreset, visualConfig.trailInterval, visualConfig.trailSpeedScale,
                         visualConfig.trailReverseDirection, visualConfig.impactDirected);

    colManager_->AddObject(bullet.get());
    bullets_.push_back(std::move(bullet));
}

// 弾の寿命と衝突を更新してから消滅対象を整理します。処理中の弾リスト変更を避けます。
void BulletManager::Update(float deltaTime) {
    // 当たり判定マネージャーがない間は弾の更新と登録を行わない。
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
    // 特殊マテリアル弾は後段で描くため、この不透明描画から除く。
    for (auto& bullet : bullets_) {
        if (IsSpecialMaterial(bullet->GetMaterialType())) {
            continue;
        }
        bullet->Draw(pointLightResource, spotLightResource);
    }
}

bool BulletManager::HasSpecialMaterialBullets() const {
    // 専用描画パスが必要な生存弾の有無を問い合わせる。
    for (const auto& bullet : bullets_) {
        if (!bullet->IsDead() && IsSpecialMaterial(bullet->GetMaterialType())) {
            return true;
        }
    }
    return false;
}

void BulletManager::DrawSpecial(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 深度や画面コピーが揃った段階で特殊マテリアル弾だけ描く。
    for (auto& bullet : bullets_) {
        if (!IsSpecialMaterial(bullet->GetMaterialType())) {
            continue;
        }
        DrawSpecialBullet(bullet.get(), depthSrvHandle, grabSrvHandle);
    }
}
