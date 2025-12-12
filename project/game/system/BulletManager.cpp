#include "BulletManager.h"
#include "Object3dCommon.h"
#include "CollisionManager.h"

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
    const std::string& model, float radius, float life) {

    if (!common_ || !colManager_) {
        return; 
    }

    auto bullet = std::make_unique<Bullet>();
    bullet->Initialize(common_);
    bullet->SetModel(model);
    bullet->SetColliderType(ColliderType::kSphere); // 型を Sphere に設定
    bullet->SetCollisionRadius(radius);        

    // 2. 粗い判定（ブロードフェーズ）用のAABBサイズ
    bullet->SetCollisionSize({ radius, radius, radius });

    bullet->Fire(pos, vel, life, attr, mask);

    colManager_->AddObject(bullet.get());
    bullets_.push_back(std::move(bullet));
}

void BulletManager::Update(float deltaTime) {
    if (!colManager_) { return; }

    // 1. 全ての弾を更新
    for (auto& bullet : bullets_) {
        bullet->Update(deltaTime);
        bullet->UpdateLocalMatrix();
        bullet->UpdateWorldMatrix(); // WorldMatrix の更新
    }

    // 2. 「死亡」した弾をリストから削除
    bullets_.remove_if([this](const std::unique_ptr<Bullet>& bullet) {
        if (bullet->IsDead()) {
            colManager_->RemoveObject(bullet.get());
            return true; // リストから削除
        }
        return false;
        });
}

void BulletManager::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    for (auto& bullet : bullets_) {
        bullet->Draw(pointLightResource, spotLightResource);
    }
}