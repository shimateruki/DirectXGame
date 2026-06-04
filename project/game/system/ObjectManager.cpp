#include "ObjectManager.h"
#include "CollisionManager.h"
#include "CameraManager.h"
#include "engine/utility/math/Math.h"
#include <algorithm> // remove_if用

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
		if (obj->GetMaterialType() != 1 && obj->GetMaterialType() != 7 && obj->GetMaterialType() < 8) {
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
	if (object) {
		removalList_.push_back(object);
	}
}

void ObjectManager::ProcessRemovals() {
	if (removalList_.empty()) return;

	CollisionManager* colManager = CollisionManager::GetInstance();

	// CollisionManagerから登録解除
	for (Object3d* obj : removalList_) {
		colManager->RemoveObject(obj);
	}

	// リストから削除
	auto it = std::remove_if(objects_.begin(), objects_.end(),
		[this](const std::unique_ptr<Object3d>& p) {
			for (Object3d* removalObj : removalList_) {
				if (p.get() == removalObj) return true;
			}
			return false;
		}
	);
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
