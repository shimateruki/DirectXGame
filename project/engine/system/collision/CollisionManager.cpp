#include "CollisionManager.h"
#include "engine/utility/math/Math.h"
#include <cmath> 
#include <algorithm> // std::find


CollisionManager* CollisionManager::GetInstance() {
    static CollisionManager instance;
    return &instance;
}

void CollisionManager::AddObject(Object3d* object) {
    objects_.push_back(object);
    // 静的グリッドの再構築が必要
    needsStaticGridRebuild_ = true;
}

void CollisionManager::RemoveObject(Object3d* object) {
    auto it = std::find(objects_.begin(), objects_.end(), object);
    if (it != objects_.end()) {
        objects_.erase(it);
        // 静的グリッドの再構築が必要
        needsStaticGridRebuild_ = true;
    }
}

void CollisionManager::ClearObjects() {
    objects_.clear();
    grid_.clear();
    checkedPairs_.clear();
    staticGrid_.clear();
    needsStaticGridRebuild_ = true;
}


// --- グリッド座標計算 ---

// 座標オフセット。負の座標を正のインデックス空間にマップする
const float kGridOffset = 1048576.0f; // 2^20

// 3D座標 -> グリッドID (すり抜け対策済み)
int64_t CollisionManager::GetGridID(const Vector3& pos) {
    // 座標にオフセットを加えて必ず正の数にする
    float offsetX = pos.x + (kGridOffset * gridSize_);
    float offsetY = pos.y + (kGridOffset * gridSize_);
    float offsetZ = pos.z + (kGridOffset * gridSize_);

    int x = static_cast<int>(std::floor(offsetX / gridSize_));
    int y = static_cast<int>(std::floor(offsetY / gridSize_));
    int z = static_cast<int>(std::floor(offsetZ / gridSize_));

    return GetGridIDFromIndices(x, y, z);
}

// 3D座標 -> (x,y,z)インデックス (AABB計算用)
Vector3i CollisionManager::GetGridIndices(const Vector3& pos) {
    float offsetX = pos.x + (kGridOffset * gridSize_);
    float offsetY = pos.y + (kGridOffset * gridSize_);
    float offsetZ = pos.z + (kGridOffset * gridSize_);

    int x = static_cast<int>(std::floor(offsetX / gridSize_));
    int y = static_cast<int>(std::floor(offsetY / gridSize_));
    int z = static_cast<int>(std::floor(offsetZ / gridSize_));
    return { x, y, z };
}


// (x,y,z)インデックス -> グリッドID (ハッシュ衝突対策済み)
int64_t CollisionManager::GetGridIDFromIndices(int x, int y, int z) {
    int64_t id_x = static_cast<int64_t>(x);
    int64_t id_y = static_cast<int64_t>(y);
    int64_t id_z = static_cast<int64_t>(z);

    const int64_t mask = 0x1FFFFF; // 21ビットマスク

    // マスクを適用してIDを生成
    int64_t masked_x = id_x & mask;
    int64_t masked_y = id_y & mask;
    int64_t masked_z = id_z & mask;

    return (masked_z << 42) | (masked_y << 21) | masked_x;
}


// --- メイン処理 ---

// 静的オブジェクトを staticGrid_ に登録する
void CollisionManager::BuildStaticGrid() {

    staticGrid_.clear();

    for (Object3d* obj : objects_) {
        // 静的なオブジェクトでなければスキップ
        if (!obj->IsStatic()) {
            continue;
        }

        AABB aabb = obj->GetAABB();
        Vector3i minCell = GetGridIndices(aabb.min);
        Vector3i maxCell = GetGridIndices(aabb.max);

        // 静的グリッド (staticGrid_) に登録
        for (int z = minCell.z; z <= maxCell.z; ++z) {
            for (int y = minCell.y; y <= maxCell.y; ++y) {
                for (int x = minCell.x; x <= maxCell.x; ++x) {
                    int64_t cellID = GetGridIDFromIndices(x, y, z);
                    staticGrid_[cellID].push_back(obj);
                }
            }
        }
    }

    needsStaticGridRebuild_ = false;
}


// 更新処理 (静的/動的 分離版)
void CollisionManager::Update() {

    // 1. 静的グリッドの再構築 (必要な場合のみ)
    if (needsStaticGridRebuild_) {
        BuildStaticGrid();
    }

    // 2. 「動的グリッド」と「ペア記録」を毎フレームクリア
    grid_.clear();
    checkedPairs_.clear();

    // 3. 「動的オブジェクト」だけを「動的グリッド (grid_)」に登録
    for (Object3d* objA : objects_) {
        if (objA->IsStatic()) {
            continue;
        }

        AABB aabb = objA->GetAABB();
        Vector3i minCell = GetGridIndices(aabb.min);
        Vector3i maxCell = GetGridIndices(aabb.max);

        // 「動的グリッド (grid_)」に登録
        for (int z = minCell.z; z <= maxCell.z; ++z) {
            for (int y = minCell.y; y <= maxCell.y; ++y) {
                for (int x = minCell.x; x <= maxCell.x; ++x) {
                    int64_t cellID = GetGridIDFromIndices(x, y, z);
                    grid_[cellID].push_back(objA);
                }
            }
        }
    }

    // 4. 「動的オブジェクト」を主語にして衝突チェック
    for (Object3d* objA : objects_) {
        if (objA->IsStatic()) {
            continue;
        }

        AABB aabb = objA->GetAABB();
        Vector3i minCell = GetGridIndices(aabb.min);
        Vector3i maxCell = GetGridIndices(aabb.max);

        for (int z = minCell.z; z <= maxCell.z; ++z) {
            for (int y = minCell.y; y <= maxCell.y; ++y) {
                for (int x = minCell.x; x <= maxCell.x; ++x) {
                    int64_t cellID = GetGridIDFromIndices(x, y, z);

                    // --- 4a. 「動的 vs 動的」チェック ---
                    auto it_dynamic = grid_.find(cellID);
                    if (it_dynamic != grid_.end()) {
                        std::list<Object3d*>& cellObjects = it_dynamic->second;
                        for (Object3d* objB : cellObjects) {

                            if (objA == objB) continue;

                            Object3d* pairA = (objA < objB) ? objA : objB;
                            Object3d* pairB = (objA < objB) ? objB : objA;

                            if (checkedPairs_.count({ pairA, pairB })) {
                                continue;
                            }

                            CheckCollisionPair(pairA, pairB);
                            checkedPairs_.insert({ pairA, pairB });
                        }
                    }

                    // --- 4b. 「動的 vs 静的」チェック ---
                    auto it_static = staticGrid_.find(cellID);
                    if (it_static != staticGrid_.end()) {
                        std::list<Object3d*>& cellObjects = it_static->second;
                        for (Object3d* objB : cellObjects) {
                            // (objB は静的なので、ペアチェックは不要)
                            CheckCollisionPair(objA, objB);
                        }
                    }
                }
            }
        }
    }
}


// 2つのオブジェクトの衝突をチェックする
void CollisionManager::CheckCollisionPair(Object3d* objA, Object3d* objB) {
    // 衝突フィルタリング
    if (!((objA->GetCollisionMask() & objB->GetCollisionAttribute()) &&
        (objB->GetCollisionMask() & objA->GetCollisionAttribute()))) {
        return;
    }

    // 各オブジェクトのOnCollisionを呼び出す
    // (精密判定と応答は OnCollision 側が担当する)
    objA->OnCollision(objB);
    objB->OnCollision(objA);
}