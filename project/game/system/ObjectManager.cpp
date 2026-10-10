#include "ObjectManager.h"
#include "CollisionManager.h"
#include "CameraManager.h"
#include "LightManager.h"
#include "RenderStats.h"
#include "GimmickMovingFloor.h"
#include "GimmickSinkingFloor.h"
#include "game/actor/gimmick/stage/GimmickLiquidLevel.h"
#include "Player.h"
#include "engine/utility/math/Math.h"
#include <string>
#include <algorithm> // remove_if用
#include <unordered_set>

namespace {
bool ShouldDrawStageGateFrame(const std::unique_ptr<Object3d>& obj) {
    if (!obj || obj->GetMaterialType() != 22)
        return false;
    if (obj->GetGimmickType() != "StageGate")
        return false;
    const std::string modelName = obj->GetModelName();
    return !modelName.empty() && modelName.find("portal_surface") == std::string::npos;
}
} // namespace

// シーン内オブジェクトの所有と更新順を管理します。生成済み参照を使う側との寿命の境界に注意します。

void ObjectManager::Update(float deltaTime) {
    // 入力を処理しないフレームが挟まっても、古い床の速度を持ち越さない。
    for (auto& obj : objects_) {
        if (auto* player = obj && !obj->IsReplayRemoved() ? dynamic_cast<Player*>(obj.get()) : nullptr)
            player->SetPlatformMotion({});
    }
    // 上昇する液面をPlayerの死亡判定より先に更新します。
    for (auto& obj : objects_) {
        if (auto* liquid = obj && !obj->IsReplayRemoved() ? dynamic_cast<GimmickLiquidLevel*>(obj.get()) : nullptr)
            liquid->Update(deltaTime);
    }
    // 全移動床を乗員の物理より先に動かす。従来型も下降時の接地を保つ。
    for (auto& obj : objects_) {
        auto* floor = obj && !obj->IsReplayRemoved() ? dynamic_cast<GimmickMovingFloor*>(obj.get()) : nullptr;
        if (floor)
            floor->Update(deltaTime);
        else if (auto* sinking =
                     obj && !obj->IsReplayRemoved() ? dynamic_cast<GimmickSinkingFloor*>(obj.get()) : nullptr;
                 sinking && sinking->param_ && sinking->param_->actionMode == 1)
            sinking->Update(deltaTime);
    }
    // 先行更新した液面・移動床を除き、各オブジェクトのロジックを一度ずつ進めます。
    for (auto& obj : objects_) {
        if (!obj || obj->IsReplayRemoved())
            continue;
        if (dynamic_cast<GimmickMovingFloor*>(obj.get()) || dynamic_cast<GimmickLiquidLevel*>(obj.get()))
            continue;
        if (dynamic_cast<GimmickSinkingFloor*>(obj.get()) && obj->param_ && obj->param_->actionMode == 1)
            continue;
        obj->Update(deltaTime);
    }

    // 全ロジック更新後に Transform と描画定数を確定します。
    // 親を持つObjectは親側の再帰更新に含め、同じMeshRenderer::Updateが
    // 1フレームに複数回走らないようにする。
    std::unordered_set<Object3d*> managedObjects;
    managedObjects.reserve(objects_.size());
    for (const auto& obj : objects_) {
        if (obj) {
            managedObjects.insert(obj.get());
        }
    }

    for (auto& obj : objects_) {
        if (!obj || obj->IsReplayRemoved())
            continue;

        Object3d* parent = obj->GetParent();
        const bool hierarchyRoot = parent == nullptr || managedObjects.find(parent) == managedObjects.end();
        if (hierarchyRoot) {
            obj->UpdateWorldMatrix();
        }
    }

    ApplyPendingChanges();
}

void ObjectManager::ApplyPendingChanges() {
    // 更新中に生成したオブジェクトは走査を終えてから追加します。
    if (!pendingObjects_.empty()) {
        for (auto& pendingObj : pendingObjects_) {
            objects_.push_back(std::move(pendingObj));
        }
        pendingObjects_.clear();
    }

    // 削除予約も走査の最後にまとめて反映します。
    ProcessRemovals();
}

void ObjectManager::Draw(ID3D12Resource* pointLight, ID3D12Resource* spotLight) {
    // 管理している全オブジェクトを描画する
    // ※GamePlayScene側で object3dCommon_->SetGraphicsCommand() 等を呼んでいる前提

    // 不透明描画
    for (auto& obj : objects_) {
        if (!obj->GetIsVisible() || obj->IsCameraObject())
            continue;
        const int materialType = obj->GetMaterialType();
        const bool drawStageGateFrame = ShouldDrawStageGateFrame(obj);
        // 25（スライムの通常シェーディング）も不透明パスへ渡す。
        // 偽王のDrawは所有する槍・光刃も描くため、ここで除外すると攻撃だけが消える。
        if (drawStageGateFrame ||
            (materialType != 1 && materialType != 7 &&
             (materialType < 8 || materialType == 23 || materialType == 24 || materialType == 25))) {
            obj->Draw(pointLight, spotLight);
        }
    }
    // 透明描画
    for (auto& obj : objects_) {
        if (!obj->GetIsVisible() || obj->IsCameraObject())
            continue;
        if (obj->GetMaterialType() == 1) {
            obj->Draw(pointLight, spotLight);
        }
    }
}

void ObjectManager::AddObject(std::unique_ptr<Object3d> object) {
    if (!object)
        return;
    // 衝突判定に登録
    CollisionManager::GetInstance()->AddObject(object.get());
    // 追加待ちリストへ
    pendingObjects_.push_back(std::move(object));
}

Object3d* ObjectManager::AddObjectImmediate(std::unique_ptr<Object3d> object) {
    if (!object) return nullptr;
    Object3d* raw = object.get();
    CollisionManager::GetInstance()->AddObject(raw);
    objects_.push_back(std::move(object));
    return raw;
}

std::unique_ptr<Object3d> ObjectManager::TakeObjectImmediate(Object3d* object) {
    if (!object) return nullptr;
    auto takeFrom = [object](std::vector<std::unique_ptr<Object3d>>& storage) -> std::unique_ptr<Object3d> {
        auto it = std::find_if(storage.begin(), storage.end(), [object](const auto& candidate) {
            return candidate.get() == object;
        });
        if (it == storage.end()) return nullptr;
        std::unique_ptr<Object3d> result = std::move(*it);
        storage.erase(it);
        return result;
    };
    std::unique_ptr<Object3d> result = takeFrom(objects_);
    if (!result) result = takeFrom(pendingObjects_);
    if (!result) return nullptr;
    CollisionManager::GetInstance()->RemoveObject(object);
    removalList_.erase(std::remove(removalList_.begin(), removalList_.end(), object), removalList_.end());
    return result;
}

void ObjectManager::RequestRemove(Object3d* object) {
    if (!object)
        return;

    const auto matches = [object](const std::unique_ptr<Object3d>& candidate) {
        return candidate && candidate.get() == object;
    };
    const bool exists = std::any_of(objects_.begin(), objects_.end(), matches) ||
                        std::any_of(pendingObjects_.begin(), pendingObjects_.end(), matches);
    if (!exists)
        return;

    if (std::find(removalList_.begin(), removalList_.end(), object) != removalList_.end()) {
        return;
    }

    // 同じフレームの残りで描画されないよう、所有権を外す前に非表示にする。
    object->SetIsVisible(false);
    removalList_.push_back(object);
}

void ObjectManager::ProcessRemovals() {
    if (removalList_.empty())
        return;

    CollisionManager* colManager = CollisionManager::GetInstance();

    std::sort(removalList_.begin(), removalList_.end());
    removalList_.erase(std::unique(removalList_.begin(), removalList_.end()), removalList_.end());

    auto isRequestedForRemoval = [this](Object3d* object) {
        return std::binary_search(removalList_.begin(), removalList_.end(), object);
    };

    // CollisionManagerから登録解除
    for (Object3d* obj : removalList_) {
        colManager->RemoveObject(obj);
    }

    // リストから削除
    pendingObjects_.erase(
        std::remove_if(pendingObjects_.begin(), pendingObjects_.end(),
                       [&](const std::unique_ptr<Object3d>& p) { return p && isRequestedForRemoval(p.get()); }),
        pendingObjects_.end());

    auto it = std::remove_if(objects_.begin(), objects_.end(), [&](const std::unique_ptr<Object3d>& p) {
        if (!p || !isRequestedForRemoval(p.get())) {
            return false;
        }
        if (p->IsReplayRetained()) {
            // リプレイ記録中は所有権を維持し、過去フレームから復元できるようにする。
            p->SetReplayRemoved(true);
            p->SetIsVisible(false);
            p->isDead = true;
            return false;
        }
        return true;
    });
    objects_.erase(it, objects_.end());

    removalList_.clear();
}

void ObjectManager::DrawShadow() {
    if (objects_.empty())
        return;

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera)
        return;
    const Matrix4x4& lightViewProjection = LightManager::GetInstance()->GetDirectionalShadowViewProjection(camera);
    const Frustum shadowFrustum = Math::ExtractFrustumPlanes(lightViewProjection);

    // 軽量化: 共通の状態設定はループの外で1回だけ行う
    bool isFirst = true;

    // 管理している全オブジェクトの影を描画する
    for (auto& obj : objects_) {
        if (!obj->GetIsVisible() || obj->IsCameraObject())
            continue;
        if (!obj->GetCastShadow())
            continue;
        if (obj->GetMaterialType() == 7)
            continue;

        // ライトの視錐台外にある物体はシャドウマップへ書き込まれないため除外する。
        // 画面外から影だけを落とす物体は残るので、メインカメラ基準より見た目も正確になる。
        AABB worldAabb = obj->GetModelWorldAABB();
        if (!Math::IntersectFrustumAABB(shadowFrustum, worldAabb.min, worldAabb.max)) {
            RenderStats::GetInstance()->RecordCulledObject();
            continue;
        }

        if (isFirst) {
            obj->SetShadowCommonState();
            isFirst = false;
        }

        obj->DrawShadowOnly();
    }
}
