#include "game/system/combat/BulletManager.h"
#include "CollisionManager.h"
#include "Object3DCommon.h"
#include "engine/graphics/3d/material/MaterialRenderPolicy.h"
#include <algorithm>

BulletManager* BulletManager::GetInstance() {
    // 弾リストと衝突登録をシーン内で一箇所に集約する。
    static BulletManager instance;
    return &instance;
}

void BulletManager::Initialize(engine::Object3DCommon* common, CollisionManager* colManager) {
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
            if (bullet && !bullet->IsPreviewOnly()) {
                colManager_->RemoveObject(bullet.get());
            }
        }
    }

    bullets_.clear();
}

void BulletManager::ClearPreviewForOwner(const engine::Object3D* owner) {
    // 編集を止めた砲台の弾だけ消し、他の砲台やゲーム用の弾を保ちます。
    bullets_.remove_if([owner](const std::unique_ptr<Bullet>& bullet) {
        return bullet && bullet->IsPreviewOnly() && bullet->GetPreviewOwner() == owner;
    });
}

void BulletManager::Fire(const engine::Vector3& pos, const engine::Vector3& vel, uint32_t attr, uint32_t mask, const std::string& model,
                         float radius, float life, const BulletVisualConfig& visualConfig, float damage,
                         const StatusEffectApplication& statusEffect, DamageType damageType, float gravity) {

    if (!common_ || !colManager_) {
        return;
    }

    if (visualConfig.previewOnly) {
        // 既存弾を途中で消すと持続時間を確認できないため、上限時は新規生成を待ちます。
        // 最短プレビュー間隔0.8秒と設定可能な最大寿命20秒を一台分収容します。
        constexpr size_t kMaxPerCannon = 32;
        constexpr size_t kMaxTotal = 128;
        const auto isOwnerPreview = [&visualConfig](const std::unique_ptr<Bullet>& bullet) {
            return bullet->IsPreviewOnly() && bullet->GetPreviewOwner() == visualConfig.previewOwner;
        };
        if (std::count_if(bullets_.begin(), bullets_.end(), isOwnerPreview) >= kMaxPerCannon) {
            return;
        }
        const auto isPreview = [](const std::unique_ptr<Bullet>& bullet) { return bullet->IsPreviewOnly(); };
        if (std::count_if(bullets_.begin(), bullets_.end(), isPreview) >= kMaxTotal) {
            return;
        }
    }

    auto bullet = std::make_unique<Bullet>();
    bullet->Initialize(common_);
    if (!visualConfig.debugLabel.empty())
        bullet->SetName(visualConfig.debugLabel);
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

    bullet->SetColliderType(ColliderType::Sphere);
    bullet->SetCollisionRadius(radius);
    bullet->SetCollisionSize({radius, radius, radius});
    if (visualConfig.collisionRadiusIsWorld && bullet->GetCollider())
        bullet->GetCollider()->SetScaleOverride({1.0f, 1.0f, 1.0f});
    if (visualConfig.magmaHopperPresentation && bullet->GetCollider()) {
        bullet->GetCollider()->SetScaleOverride({1.0f, 1.0f, 1.0f});
        bullet->SetCastShadow(false);
    }

    bullet->Fire(pos, vel, life,
                 visualConfig.previewOnly ? 0 : attr, visualConfig.previewOnly ? 0 : mask,
                 visualConfig.previewOnly ? 0.0f : damage, statusEffect, damageType);
    if (visualConfig.previewOnly)
        bullet->SetPreviewOnly(visualConfig.previewOwner);
    bullet->SetMagmaHopperPresentation(visualConfig.magmaHopperPresentation);
    bullet->SetExplosionRadius(visualConfig.explosionRadius);
    if (visualConfig.explosionRadius > 0.0f && bullet->GetCollider())
        bullet->GetCollider()->SetScaleOverride({1.0f, 1.0f, 1.0f});
    bullet->SetGravity((std::max)(0.0f, gravity));
    const float trailInterval = visualConfig.previewOnly
        ? (std::max)(0.14f, visualConfig.trailInterval) : visualConfig.trailInterval;
    const std::string secondaryTrail = visualConfig.previewOnly ? std::string{} : visualConfig.trailSecondaryPreset;
    bullet->ConfigureVfx(visualConfig.trailPreset, secondaryTrail, visualConfig.impactPreset,
                         visualConfig.impactSecondaryPreset, trailInterval, visualConfig.trailSpeedScale,
                         visualConfig.trailReverseDirection, visualConfig.impactDirected);
    bullet->ConfigureBurningVfx(visualConfig.burningPreset, radius);
    bullet->ConfigureDestructionVfx(visualConfig.impactDebrisPreset, visualConfig.impactDebrisSecondaryPreset,
                                    visualConfig.impactDebrisScale, visualConfig.impactOnExpiry,
                                    visualConfig.destroyOnSolidCastHit);

    if (!visualConfig.previewOnly)
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
            if (!bullet->IsPreviewOnly())
                colManager_->RemoveObject(bullet.get());
            return true;
        }
        return false;
    });
}

#ifdef USE_IMGUI
void BulletManager::UpdatePreview(float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    for (auto& bullet : bullets_) {
        if (bullet && bullet->IsPreviewOnly()) {
            bullet->Update(deltaTime);
            bullet->UpdateLocalMatrix();
            bullet->UpdateWorldMatrix();
        }
    }

    // プレビュー弾は CollisionManager に登録していないため、参照解除は不要です。
    bullets_.remove_if([](const std::unique_ptr<Bullet>& bullet) {
        return bullet && bullet->IsPreviewOnly() && bullet->IsDead();
    });
}

bool BulletManager::HasPreviewBullets() const {
    return std::any_of(bullets_.begin(), bullets_.end(), [](const std::unique_ptr<Bullet>& bullet) {
        return bullet && bullet->IsPreviewOnly();
    });
}
#endif

void BulletManager::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // 特殊マテリアル弾は後段で描くため、この不透明描画から除く。
    for (auto& bullet : bullets_) {
        if (MaterialRenderPolicy::IsSpecial(bullet->GetMaterialType())) {
            continue;
        }

        bullet->Draw(pointLightResource, spotLightResource);
    }
}

bool BulletManager::HasSpecialMaterialBullets() const {
    // 専用描画パスが必要な生存弾の有無を問い合わせる。
    for (const auto& bullet : bullets_) {
        if (!bullet->IsDead() && MaterialRenderPolicy::IsSpecial(bullet->GetMaterialType())) {
            return true;
        }
    }

    return false;
}

void BulletManager::DrawSpecial(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    // 深度や画面コピーが揃った段階で特殊マテリアル弾だけ描く。
    for (auto& bullet : bullets_) {
        if (bullet->IsDead() || !MaterialRenderPolicy::IsSpecial(bullet->GetMaterialType())) {
            continue;
        }

        bullet->DrawSpecialMaterial(depthSrvHandle, grabSrvHandle);
    }
}

#ifdef USE_IMGUI
void BulletManager::BeginEditorSession() {
    if (editorSession_) return;
    bullets_.swap(savedEditorBullets_); editorSession_ = true;
}
void BulletManager::EndEditorSession() {
    if (!editorSession_) return;
    Clear(); bullets_.swap(savedEditorBullets_); editorSession_ = false;
}
#endif
