#include "ObjectManager.h"
#include "CollisionManager.h"
#include "CameraManager.h"
#include "engine/utility/math/Math.h"
#include <string>
#include <algorithm> // remove_if用

namespace {
bool ShouldDrawStageGateFrame(const std::unique_ptr<Object3d>& obj) {
	if (!obj || obj->GetMaterialType() != 22) return false;
	if (obj->GetGimmickType() != "StageGate") return false;
	const std::string modelName = obj->GetModelName();
	return !modelName.empty() && modelName.find("portal_surface") == std::string::npos;
}
}

void ObjectManager::Update(float deltaTime) {
	// 1. 各オブジェクトの更新
	for (auto& obj : objects_) {
		obj->Update(deltaTime);
	}

	// 2. 行列更新 (UpdateWorldMatrix)
	for (auto& obj : objects_) {
		obj->UpdateLocalMatrix();
		obj->UpdateWorldMatrix();
	}

	// 3. 保留オブジェクトの追加 (AddObjectされたものをリストへ)
	if (!pendingObjects_.empty()) {
		for (auto& pendingObj : pendingObjects_) {
			objects_.push_back(std::move(pendingObj));
		}
		pendingObjects_.clear();
	}

	// 4. 削除処理
	ProcessRemovals();
}

void ObjectManager::Draw(ID3D12Resource* pointLight, ID3D12Resource* spotLight) {
	// 管理している全オブジェクトを描画する
	// ※GamePlayScene側で object3dCommon_->SetGraphicsCommand() 等を呼んでいる前提
	
	// 不透明描画
	for (auto& obj : objects_) {
		if (!obj->GetIsVisible()) continue;
		const int materialType = obj->GetMaterialType();
		const bool drawStageGateFrame = ShouldDrawStageGateFrame(obj);
		if (drawStageGateFrame || (materialType != 1 && materialType != 7 && (materialType < 8 || materialType == 23 || materialType == 24))) {
			obj->Draw(pointLight, spotLight);
		}
	}
	// 透明描画
	for (auto& obj : objects_) {
		if (!obj->GetIsVisible()) continue;
		if (obj->GetMaterialType() == 1) {
			obj->Draw(pointLight, spotLight);
		}
	}
}

void ObjectManager::AddObject(std::unique_ptr<Object3d> object) {
	if (!object) return;
	// 衝突判定に登録
	CollisionManager::GetInstance()->AddObject(object.get());
	// 追加待ちリストへ
	pendingObjects_.push_back(std::move(object));
}

void ObjectManager::RequestRemove(Object3d* object) {
	if (!object) return;

	const auto matches = [object](const std::unique_ptr<Object3d>& candidate) {
		return candidate && candidate.get() == object;
	};
	const bool exists =
		std::any_of(objects_.begin(), objects_.end(), matches) ||
		std::any_of(pendingObjects_.begin(), pendingObjects_.end(), matches);
	if (!exists) return;

	if (std::find(removalList_.begin(), removalList_.end(), object) != removalList_.end()) {
		return;
	}

	object->SetIsVisible(false);
	removalList_.push_back(object);
}

void ObjectManager::ProcessRemovals() {
	if (removalList_.empty()) return;

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
			[&](const std::unique_ptr<Object3d>& p) {
				return p && isRequestedForRemoval(p.get());
			}),
		pendingObjects_.end());

	auto it = std::remove_if(objects_.begin(), objects_.end(),
		[&](const std::unique_ptr<Object3d>& p) {
			return p && isRequestedForRemoval(p.get());
		});
	objects_.erase(it, objects_.end());

	removalList_.clear();
}

void ObjectManager::DrawShadow() {
	if (objects_.empty()) return;

	Camera* camera = CameraManager::GetInstance()->GetMainCamera();
	if (!camera) return;
	const Frustum& frustum = camera->GetFrustum();

	// 軽量化: 共通の状態設定はループの外で1回だけ行う
	bool isFirst = true;

	// 管理している全オブジェクトの影を描画する
	for (auto& obj : objects_) {
		if (!obj->GetIsVisible()) continue;
		if (!obj->GetCastShadow()) continue;
		if (obj->GetMaterialType() == 7) continue;

		// 視錐台カリング（影についてもカメラから見えないものは描画スキップ）
		// 本来はライト視点のカリングが望ましいが、簡易的な軽量化として有効
		AABB worldAabb = obj->GetModelWorldAABB();
		if (!Math::IntersectFrustumAABB(frustum, worldAabb.min, worldAabb.max)) {
			continue;
		}

		if (isFirst) {
			obj->SetShadowCommonState();
			isFirst = false;
		}

		obj->DrawShadowOnly();
	}
}
