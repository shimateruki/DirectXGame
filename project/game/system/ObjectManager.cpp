#include "ObjectManager.h"
#include "CollisionManager.h"
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

	// 3. 保留オブジェクトの追加 (AddObjectされたものをリストへ)ww
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
	// 不透明描画
	for (auto& obj : objects_) {
		if (obj->GetMaterialType() != 1) { // 1=透明でなければ
			obj->Draw(pointLight, spotLight);
		}
	}
	// 透明描画
	for (auto& obj : objects_) {
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
	// 管理している全オブジェクトの影を描画する
	for (auto& obj : objects_) {
		if (obj->GetMaterialType() == 7) continue;
		obj->DrawShadow();
	}
}