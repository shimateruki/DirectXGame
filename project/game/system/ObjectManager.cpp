#include "ObjectManager.h"
#include "CollisionManager.h"
#include <algorithm> // remove_if用

void ObjectManager::Update(float deltaTime) {
	// 1. 削除処理を最初に行う（死んだものを更新・描画させない）
	ProcessRemovals();

	// 2. 保留オブジェクトの追加 (AddObjectされたものをリストへ移動)
	if (!pendingObjects_.empty()) {
		for (auto& pendingObj : pendingObjects_) {
			objects_.push_back(std::move(pendingObj));
		}
		pendingObjects_.clear();
	}

	// 3. 各オブジェクトの更新
	for (auto& obj : objects_) {
		obj->Update(deltaTime);

		// Characterクラスなどで isDead = true になったら削除予約を入れる
		if (obj->isDead) {
			RequestRemove(obj.get());
		}
	}

	// 4. 行列更新 (UpdateWorldMatrix)
	for (auto& obj : objects_) {
		obj->UpdateLocalMatrix();
		obj->UpdateWorldMatrix();
	}
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
	// 待ちリストへ
	pendingObjects_.push_back(std::move(object));
}

void ObjectManager::RequestRemove(Object3d* object) {
	if (object) {
		removalList_.push_back(object);
	}
}

void ObjectManager::ProcessRemovals() {
	// 削除予約リストと、isDeadフラグの両方をチェックして削除する
	if (removalList_.empty() && std::none_of(objects_.begin(), objects_.end(), [](auto& o) { return o->isDead; })) {
		return;
	}

	CollisionManager* colManager = CollisionManager::GetInstance();

	// 削除対象を CollisionManager から先に登録解除する
	auto it = std::remove_if(objects_.begin(), objects_.end(),
		[this, colManager](const std::unique_ptr<Object3d>& p) {
			// removalListに入っている、または isDeadがtrueなら削除
			bool shouldRemove = p->isDead;
			for (Object3d* removalObj : removalList_) {
				if (p.get() == removalObj) {
					shouldRemove = true;
					break;
				}
			}

			if (shouldRemove) {
				colManager->RemoveObject(p.get()); // 当たり判定から外す
				return true;
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