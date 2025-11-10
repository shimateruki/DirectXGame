#include "CollisionManager.h"
#include "engine/base/Math.h"
#include <cmath> 
#include <set>   

CollisionManager* CollisionManager::GetInstance() {
    static CollisionManager instance;
    return &instance;
}

void CollisionManager::AddObject(Object3d* object) {
    // リストがクリアされるタイミングでポインタが無効にならないよう、
    // objects_ に追加する
    objects_.push_back(object);
}

void CollisionManager::ClearObjects() {
    objects_.clear();
    grid_.clear(); // グリッドもクリア
    checkedPairs_.clear(); // 判定済みセットもクリア
}

// 3D座標 -> グリッドID
int64_t CollisionManager::GetGridID(const Vector3& pos) {
    // 座標をグリッドサイズで割り、整数に丸める
    int x = static_cast<int>(std::floor(pos.x / gridSize_));
    int y = static_cast<int>(std::floor(pos.y / gridSize_));
    int z = static_cast<int>(std::floor(pos.z / gridSize_));

    return GetGridIDFromIndices(x, y, z);
}

// (x,y,z)インデックス -> グリッドID
// (64bit整数内に、21bitずつ3つのインデックスを詰め込む)
int64_t CollisionManager::GetGridIDFromIndices(int x, int y, int z) {
    const int64_t bits = 21; // 各軸に21ビット (約+/- 100万セル)
    const int64_t mask = (1LL << bits) - 1;

    // 2の補数表現を考慮してビットマスクを適用
    int64_t id_x = static_cast<int64_t>(x) & mask;
    int64_t id_y = (static_cast<int64_t>(y) & mask) << bits;
    int64_t id_z = (static_cast<int64_t>(z) & mask) << (bits * 2);

    return id_x | id_y | id_z;
}

// グリッドID -> (x,y,z)インデックス
Vector3i CollisionManager::GetGridIndices(int64_t gridID) {
    const int64_t bits = 21;
    const int64_t mask = (1LL << bits) - 1;
    // 符号拡張のための処理
    const int64_t sign_mask = 1LL << (bits - 1);

    int64_t id_x = gridID & mask;
    int64_t id_y = (gridID >> bits) & mask;
    int64_t id_z = (gridID >> (bits * 2)) & mask;

    // 符号ビットを見て負の数に戻す 
    if (id_x & sign_mask) id_x |= ~mask;
    if (id_y & sign_mask) id_y |= ~mask;
    if (id_z & sign_mask) id_z |= ~mask;

    return { static_cast<int>(id_x), static_cast<int>(id_y), static_cast<int>(id_z) };
}

// 衝突ペアのチェック (精密判定は Character/Player の OnCollision が担当)
void CollisionManager::CheckCollisionPair(Object3d* objA, Object3d* objB) {

    // 衝突フィルタリング (既存のロジック)
    if (!((objA->GetCollisionMask() & objB->GetCollisionAttribute()) &&
        (objB->GetCollisionMask() & objA->GetCollisionAttribute()))) {
        return;
    }

    //物理応答(Character)やゲームロジック(Player)は
    //各オブジェクトの OnCollision が担当する 
    objA->OnCollision(objB);
    objB->OnCollision(objA);
}


/// <summary>
/// 更新処理 (グリッド分割版)
/// </summary>
void CollisionManager::Update() {

    // --- 1. グリッドと判定済みセットをクリア ---
    grid_.clear();
    checkedPairs_.clear();

    // --- 2. グリッドの再構築 ---
    for (Object3d* obj : objects_) {
        if (obj->GetColliderType() == ColliderType::kNone) {
            continue;
        }
        Vector3 pos = obj->GetWorldPosition();
        int64_t gridID = GetGridID(pos);
        grid_[gridID].push_back(obj);
    }

    // --- 3. 衝突判定の実行 ---
    for (Object3d* objA : objects_) {

        // (コライダーが kNone ならスキップ)
        if (objA->GetColliderType() == ColliderType::kNone) {
            continue;
        }

        // objA の中心点が含まれるグリッドのインデックスを取得
        Vector3i indicesA = GetGridIndices(GetGridID(objA->GetWorldPosition()));

        // 「自分のセル」と「周囲26セル」（3x3x3）をチェック対象にする
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                for (int z = -1; z <= 1; ++z) {

                    // チェック対象のセルIDを計算
                    int64_t cellID = GetGridIDFromIndices(indicesA.x + x, indicesA.y + y, indicesA.z + z);

                    // そのセルがグリッドマップに存在するか検索
                    auto it = grid_.find(cellID);
                    if (it == grid_.end()) {
                        continue; // このセルには誰もいない
                    }

                    // セル内の全オブジェクト(objB)と判定
                    std::list<Object3d*>& cellObjects = it->second;
                    for (Object3d* objB : cellObjects) {

                        // 自分自身とは判定しない
                        if (objA == objB) {
                            continue;
                        }

                        // 判定順序を固定 (A < B) して、重複チェックを防ぐ
                        Object3d* pairA = (objA < objB) ? objA : objB;
                        Object3d* pairB = (objA < objB) ? objB : objA;

                        // 既にこのペアを判定済みかチェック
                        if (checkedPairs_.count({ pairA, pairB })) {
                            continue;
                        }

                        //判定実行
                        CheckCollisionPair(pairA, pairB);

                        // 判定済みとして記録
                        checkedPairs_.insert({ pairA, pairB });
                    }
                }
            }
        }
    }
}

/// <summary>
/// 衝突リストからオブジェクトを削除する
/// </summary>
void CollisionManager::RemoveObject(Object3d* object) {
    if (object == nullptr) {
        return;
    }
    auto it = std::remove(objects_.begin(), objects_.end(), object);
    objects_.erase(it, objects_.end());

    
}