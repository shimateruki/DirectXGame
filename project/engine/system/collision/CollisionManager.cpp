#define NOMINMAX
#include "CollisionManager.h"
#include "engine/utility/math/Math.h"
#include <cmath> 
#include <algorithm> // std::find
#include "game/actor/CollisionConfig.h"
#include "EffectObject3d.h"



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
    // ★修正: std::find ではなく std::list::remove を使う
    // これにより、もし重複して登録されていても全て削除され、ダングリングポインタを防げます
    objects_.remove(object);
    needsStaticGridRebuild_ = true;
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

    // ★ エフェクト（EffectObject3d）の多段ヒット防止処理
    if (EffectObject3d* effectA = dynamic_cast<EffectObject3d*>(objA)) {
        if (!effectA->CanHit(objB)) {
            return;
        }
    }
    if (EffectObject3d* effectB = dynamic_cast<EffectObject3d*>(objB)) {
        if (!effectB->CanHit(objA)) {
            return;
        }
    }

    // 各オブジェクトのOnCollisionを呼び出す
    // (精密判定と応答は OnCollision 側が担当する)
    objA->OnCollision(objB);
    objB->OnCollision(objA);
}


/// <summary>
/// (ヘルパー関数) レイ と AABB の交差判定
/// </summary>
/// <returns>衝突距離 (衝突しなかったら FLT_MAX)</returns>
float IntersectRayAABB(const Vector3& start, const Vector3& direction, const AABB& aabb) {

    //  ゼロ除算 (NaN / Infinity) を防ぐため、0に近い場合は極小値(1e-6f)にする
    Vector3 invDir = {
        1.0f / (std::abs(direction.x) < 1e-6f ? 1e-6f : direction.x),
        1.0f / (std::abs(direction.y) < 1e-6f ? 1e-6f : direction.y),
        1.0f / (std::abs(direction.z) < 1e-6f ? 1e-6f : direction.z)
    };

    // 各軸で「衝突時間」(t) の最小と最大を計算 (割り算ではなく安全な掛け算を使用)
    Vector3 tMin = {
        (aabb.min.x - start.x) * invDir.x,
        (aabb.min.y - start.y) * invDir.y,
        (aabb.min.z - start.z) * invDir.z
    };
    Vector3 tMax = {
        (aabb.max.x - start.x) * invDir.x,
        (aabb.max.y - start.y) * invDir.y,
        (aabb.max.z - start.z) * invDir.z
    };

    // (direction がマイナスの場合、tMin と tMax が逆転するので入れ替える)
    Vector3 tNear = {
        std::min(tMin.x, tMax.x),
        std::min(tMin.y, tMax.y),
        std::min(tMin.z, tMax.z)
    };
    Vector3 tFar = {
        std::max(tMin.x, tMax.x),
        std::max(tMin.y, tMax.y),
        std::max(tMin.z, tMax.z)
    };

    // 3軸すべてで重なっている領域の最も近い点と遠い点を求める
    float tEnter = std::max({ tNear.x, tNear.y, tNear.z });
    float tExit = std::min({ tFar.x, tFar.y, tFar.z });

    // 衝突していないパターン
    if (tEnter > tExit || tExit < 0.0f) {
        return std::numeric_limits<float>::max(); // 衝突しない
    }

    // ★修正2: レイの始点がAABBの「中」にあった場合は、即座に押し出すために 0.0f を返す
    if (tEnter < 0.0f) {
        return 0.0f;
    }

    // 衝突している
    return tEnter;
}

/// <summary>
/// (ヘルパー関数) レイ と OBB の交差判定 (スラブメソッド)
/// </summary>
float IntersectRayOBB(const Vector3& start, const Vector3& direction, const OBB& obb) {
    Math math;
    float tMin = -std::numeric_limits<float>::max();
    float tMax = std::numeric_limits<float>::max();

    // カメラ(始点)からOBBの中心へのベクトル
    Vector3 p = obb.center - start;

    // OBBの3つのローカル軸（X, Y, Z）について調べる
    for (int i = 0; i < 3; ++i) {
        Vector3 axis = obb.orientations[i];
        float e = math.Dot(axis, p);
        float f = math.Dot(direction, axis);

        // 各軸の半分のサイズを取得
        float size = (i == 0) ? obb.size.x : (i == 1) ? obb.size.y : obb.size.z;

        // レイがその軸と平行でない場合
        if (std::abs(f) > 1e-6f) {
            float t1 = (e + size) / f;
            float t2 = (e - size) / f;

            if (t1 > t2) std::swap(t1, t2);

            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;

            if (tMin > tMax) return std::numeric_limits<float>::max(); // 衝突しない
            if (tMax < 0.0f) return std::numeric_limits<float>::max(); // OBBがカメラの後ろにある
        }
        else {
            // レイが面とほぼ平行な場合、OBBの範囲外にいるなら当たらない
            if (-e - size > 0 || -e + size < 0) {
                return std::numeric_limits<float>::max();
            }
        }
    }

    // カメラ(始点)がすでにOBBの中にめり込んでいる場合は即座に押し出す
    if (tMin < 0.0f) return 0.0f;

    return tMin;
}

/// <summary>
/// レイキャスト本体
/// </summary>
RaycastHit CollisionManager::Raycast(const Vector3& start, const Vector3& direction,
    float maxDistance, uint32_t mask) {
    RaycastHit closestHit;
    closestHit.isHit = false;
    closestHit.distance = maxDistance;

    // 【簡易版】登録されている全てのオブジェクトをチェック
    for (Object3d* object : objects_) {

        // =========================================================
        // ★ キャラクター（プレイヤー＆敵）の除外処理
        // =========================================================
        bool isIgnoreObject = false;
        Object3d* current = object;
        while (current) {
            std::string className = current->GetClassName();
            std::string name = current->GetName();

            std::string cat = current->GetSaveCategory();

            // ① プレイヤーカテゴリは常に除外
            if (cat == "Player") {
                isIgnoreObject = true;
                break;
            }

            // ② エネミーカテゴリは、エネミーを探していないレイキャスト（地形探索など）の時だけ除外
            if (cat == "Enemy") {
                if (!(mask & kEnemy)) {
                    isIgnoreObject = true;
                    break;
                }
            }

            // ③ その他、特定の名前（Blockなど）を持つオブジェクトの除外
       /*     if (name.find("Block") != std::string::npos || name.find("block") != std::string::npos) {
                if (!(mask & kEnemy)) {
                    isIgnoreObject = true;
                    break;
                }
            }*/

            current = current->GetParent();
        }

        // 無視すべきオブジェクトだったら、このオブジェクトへのレイキャストはスキップ！
        if (isIgnoreObject) {
            continue;
        }

        // (1) マスク判定 (指定した対象か？)
        if (!((object->GetCollisionAttribute()) & mask)) {
            continue; // 対象外 
        }
            // =========================================================
               // (2) 形状判定 (AABB と OBB に対応)
               // =========================================================
            ColliderType colType = object->GetColliderType();
            if (colType != ColliderType::kAABB && colType != ColliderType::kOBB && colType != ColliderType::kCylinder) {
                continue; // 球や判定なしはスキップ
            }

            float distance = std::numeric_limits<float>::max();

            // (3) 交差判定 (形状によって計算を分ける)
            if (colType == ColliderType::kAABB) {
                AABB aabb = object->GetAABB();
                distance = IntersectRayAABB(start, direction, aabb);
            }
            else if (colType == ColliderType::kOBB) {
                OBB obb = object->GetOBB(); 
                distance = IntersectRayOBB(start, direction, obb);
            }
         
            // (4) 一番近いものを採用
            if (distance < closestHit.distance) {
                closestHit.isHit = true;
                closestHit.distance = distance;
                closestHit.hitObject = object;
                closestHit.hitPoint = start + direction * distance;


            }
        }

        return closestHit;
    }
