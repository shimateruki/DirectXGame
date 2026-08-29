#include "ObjectManager.h"

#include "CameraManager.h"
#include "CollisionManager.h"
#include "LightManager.h"
#include "RenderStats.h"
#include "engine/utility/math/Math.h"

#include <algorithm>
#include <unordered_set>

void ObjectManager::Update(float deltaTime) {
    for (auto& object : objects_) {
        if (!object || object->IsReplayRemoved()) {
            continue;
        }
        object->Update(deltaTime);
    }

    // 親子階層のルートだけを更新し、同じ描画定数を1フレームに複数回更新しないようにします。
    std::unordered_set<Object3d*> managedObjects;
    managedObjects.reserve(objects_.size());
    for (const auto& object : objects_) {
        if (object) {
            managedObjects.insert(object.get());
        }
    }

    for (auto& object : objects_) {
        if (!object || object->IsReplayRemoved()) {
            continue;
        }
        Object3d* parent = object->GetParent();
        const bool isHierarchyRoot =
            parent == nullptr || managedObjects.find(parent) == managedObjects.end();
        if (isHierarchyRoot) {
            object->UpdateWorldMatrix();
        }
    }

    for (auto& pendingObject : pendingObjects_) {
        objects_.push_back(std::move(pendingObject));
    }
    pendingObjects_.clear();
    ProcessRemovals();
}

void ObjectManager::Draw(ID3D12Resource* pointLight, ID3D12Resource* spotLight) {
    for (auto& object : objects_) {
        if (!object || !object->GetIsVisible() || object->IsCameraObject()) {
            continue;
        }
        const int materialType = object->GetMaterialType();
        if (materialType != 1 && materialType != 7 &&
            (materialType < 8 || materialType == 23 || materialType == 24)) {
            object->Draw(pointLight, spotLight);
        }
    }

    for (auto& object : objects_) {
        if (object && object->GetIsVisible() && !object->IsCameraObject() &&
            object->GetMaterialType() == 1) {
            object->Draw(pointLight, spotLight);
        }
    }
}

void ObjectManager::AddObject(std::unique_ptr<Object3d> object) {
    if (!object) {
        return;
    }
    // 衝突問い合わせは追加したフレームから使えるよう、所有vectorへの反映より先に登録します。
    CollisionManager::GetInstance()->AddObject(object.get());
    pendingObjects_.push_back(std::move(object));
}

void ObjectManager::RequestRemove(Object3d* object) {
    if (!object) {
        return;
    }
    const auto matches = [object](const std::unique_ptr<Object3d>& candidate) {
        return candidate && candidate.get() == object;
    };
    const bool exists =
        std::any_of(objects_.begin(), objects_.end(), matches) ||
        std::any_of(pendingObjects_.begin(), pendingObjects_.end(), matches);
    if (!exists || std::find(removalList_.begin(), removalList_.end(), object) != removalList_.end()) {
        return;
    }
    // 実体削除まで描画・選択されないよう、この時点で非表示にします。
    object->SetIsVisible(false);
    removalList_.push_back(object);
}

void ObjectManager::ProcessRemovals() {
    if (removalList_.empty()) {
        return;
    }

    CollisionManager* collisionManager = CollisionManager::GetInstance();
    std::sort(removalList_.begin(), removalList_.end());
    removalList_.erase(std::unique(removalList_.begin(), removalList_.end()), removalList_.end());

    const auto isRequestedForRemoval = [this](Object3d* object) {
        return std::binary_search(removalList_.begin(), removalList_.end(), object);
    };
    for (Object3d* object : removalList_) {
        collisionManager->RemoveObject(object);
    }

    pendingObjects_.erase(
        std::remove_if(
            pendingObjects_.begin(), pendingObjects_.end(),
            [&](const std::unique_ptr<Object3d>& object) {
                return object && isRequestedForRemoval(object.get());
            }),
        pendingObjects_.end());

    const auto firstRemoved = std::remove_if(
        objects_.begin(), objects_.end(),
        [&](const std::unique_ptr<Object3d>& object) {
            if (!object || !isRequestedForRemoval(object.get())) {
                return false;
            }
            // Replayが参照するObjectはアドレスを維持し、論理削除状態だけを記録します。
            if (object->IsReplayRetained()) {
                object->SetReplayRemoved(true);
                object->SetIsVisible(false);
                object->isDead = true;
                return false;
            }
            return true;
        });
    objects_.erase(firstRemoved, objects_.end());
    removalList_.clear();
}

void ObjectManager::DrawShadow() {
    if (objects_.empty()) {
        return;
    }

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) {
        return;
    }

    const Matrix4x4& lightViewProjection =
        LightManager::GetInstance()->GetDirectionalShadowViewProjection(camera);
    const Frustum shadowFrustum = Math::ExtractFrustumPlanes(lightViewProjection);
    bool commonStateSet = false;

    for (auto& object : objects_) {
        if (!object || !object->GetIsVisible() || object->IsCameraObject() ||
            !object->GetCastShadow() || object->GetMaterialType() == 7) {
            continue;
        }

        const AABB worldAabb = object->GetModelWorldAABB();
        if (!Math::IntersectFrustumAABB(shadowFrustum, worldAabb.min, worldAabb.max)) {
            RenderStats::GetInstance()->RecordCulledObject();
            continue;
        }
        if (!commonStateSet) {
            object->SetShadowCommonState();
            commonStateSet = true;
        }
        object->DrawShadowOnly();
    }
}
