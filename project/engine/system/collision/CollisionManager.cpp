#define NOMINMAX
#include "CollisionManager.h"
#include "engine/utility/math/Math.h"
#include <cmath> 
#include <algorithm> // std::find
#include <limits>
#include <unordered_map>



CollisionManager* CollisionManager::GetInstance() {
    static CollisionManager instance;
    return &instance;
}

namespace {
bool IsCollisionActive(Object3d* object) {
    if (!object || !object->GetCollider()) {
        return false;
    }
    if (object->GetColliderType() == ColliderType::kNone) {
        return false;
    }
    if (object->GetCollisionAttribute() == 0 || object->GetCollisionMask() == 0) {
        return false;
    }
    return true;
}

std::pair<Object3d*, Object3d*> MakeContactPair(Object3d* objectA, Object3d* objectB) {
    if (std::less<Object3d*>{}(objectB, objectA)) {
        std::swap(objectA, objectB);
    }
    return { objectA, objectB };
}

bool IsTriggerObject(const Object3d* object) {
    return object && (object->GetCollisionAttribute() & CollisionAttribute::kTrigger) != 0;
}

bool IsInIgnoredHierarchy(Object3d* object, Object3d* ignoredRoot) {
    for (Object3d* current = object; current; current = current->GetParent()) {
        if (current == ignoredRoot) {
            return true;
        }
    }
    return false;
}

Vector3 SafeNormalize(const Vector3& value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.000001f) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return value / length;
}
}

void CollisionManager::AddObject(Object3d* object) {
    if (!object || std::find(objects_.begin(), objects_.end(), object) != objects_.end()) {
        return;
    }
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

    const auto removeContacts = [object](auto& contacts) {
        for (auto it = contacts.begin(); it != contacts.end();) {
            if (it->first.first == object || it->first.second == object) {
                it = contacts.erase(it);
            } else {
                ++it;
            }
        }
    };
    removeContacts(previousContacts_);
    removeContacts(currentContacts_);
}

void CollisionManager::ClearObjects() {
    objects_.clear();
    grid_.clear();
    checkedPairs_.clear();
    staticGrid_.clear();
    previousContacts_.clear();
    currentContacts_.clear();
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
        if (!obj->IsStatic() || !IsCollisionActive(obj)) {
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

    currentContacts_.clear();

    // 1. 静的グリッドの再構築 (必要な場合のみ)
    if (needsStaticGridRebuild_) {
        BuildStaticGrid();
    }

    // 2. 「動的グリッド」と「ペア記録」を毎フレームクリア
    grid_.clear();
    checkedPairs_.clear();

    // 3. 「動的オブジェクト」だけを「動的グリッド (grid_)」に登録
    for (Object3d* objA : objects_) {
        if (objA->IsStatic() || !IsCollisionActive(objA)) {
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
        if (objA->IsStatic() || !IsCollisionActive(objA)) {
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

                            if (objA == objB || !IsCollisionActive(objB)) continue;

                            const auto pair = MakeContactPair(objA, objB);
                            Object3d* pairA = pair.first;
                            Object3d* pairB = pair.second;

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
                            if (!IsCollisionActive(objB)) continue;
                            // 既存の地形押し戻しは動的オブジェクトを先に通知し、
                            // 占有セルごとに再評価する順序を前提に調整されています。
                            CheckCollisionPair(objA, objB);
                        }
                    }
                }
            }
        }
    }

    DispatchContactEvents();
    previousContacts_ = currentContacts_;
}


// 2つのオブジェクトの衝突をチェックする
void CollisionManager::CheckCollisionPair(Object3d* objA, Object3d* objB) {
    if (!IsCollisionActive(objA) || !IsCollisionActive(objB)) {
        return;
    }
    // 衝突フィルタリング
    if (!((objA->GetCollisionMask() & objB->GetCollisionAttribute()) &&
        (objB->GetCollisionMask() & objA->GetCollisionAttribute()))) {
        return;
    }

    // 実際に形状が重なっている場合だけOnCollisionを呼び出す
    Collider* colliderA = objA->GetCollider();
    Collider* colliderB = objB->GetCollider();
    if (!colliderA || !colliderB) {
        return;
    }

    const CollisionInfo collisionInfo = colliderA->CheckCollision(colliderB);
    if (!collisionInfo.isColliding) {
        return;
    }

    objA->OnCollision(objB);
    objB->OnCollision(objA);

    const auto pair = MakeContactPair(objA, objB);
    ContactState state;
    state.firstInfo = collisionInfo;
    if (pair.first != objA) {
        state.firstInfo.normal = state.firstInfo.normal * -1.0f;
    }
    state.isTrigger = IsTriggerObject(objA) || IsTriggerObject(objB);
    currentContacts_[pair] = state;
}

void CollisionManager::DispatchContactEvents() {
    const auto dispatch = [](Object3d* self, Object3d* other, CollisionInfo info,
        CollisionEventPhase phase, bool isTrigger) {
        if (!self || !other) return;
        CollisionEvent event;
        event.self = self;
        event.other = other;
        event.collision = info;
        event.phase = phase;
        event.isTrigger = isTrigger;

        if (isTrigger) {
            if (phase == CollisionEventPhase::Enter) self->OnTriggerEnter(event);
            else if (phase == CollisionEventPhase::Stay) self->OnTriggerStay(event);
            else self->OnTriggerExit(event);
        } else {
            if (phase == CollisionEventPhase::Enter) self->OnCollisionEnter(event);
            else if (phase == CollisionEventPhase::Stay) self->OnCollisionStay(event);
            else self->OnCollisionExit(event);
        }
    };

    for (const auto& [pair, state] : currentContacts_) {
        const CollisionEventPhase phase = previousContacts_.count(pair)
            ? CollisionEventPhase::Stay : CollisionEventPhase::Enter;
        dispatch(pair.first, pair.second, state.firstInfo, phase, state.isTrigger);
        CollisionInfo secondInfo = state.firstInfo;
        secondInfo.normal = secondInfo.normal * -1.0f;
        dispatch(pair.second, pair.first, secondInfo, phase, state.isTrigger);
    }

    for (const auto& [pair, state] : previousContacts_) {
        if (currentContacts_.count(pair)) continue;
        dispatch(pair.first, pair.second, state.firstInfo, CollisionEventPhase::Exit, state.isTrigger);
        CollisionInfo secondInfo = state.firstInfo;
        secondInfo.normal = secondInfo.normal * -1.0f;
        dispatch(pair.second, pair.first, secondInfo, CollisionEventPhase::Exit, state.isTrigger);
    }
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

float IntersectRaySphere(const Vector3& start, const Vector3& direction, const Vector3& center, float radius) {
    Vector3 m = start - center;
    float a = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (a <= 1e-6f) {
        return std::numeric_limits<float>::max();
    }

    float b = 2.0f * (m.x * direction.x + m.y * direction.y + m.z * direction.z);
    float c = (m.x * m.x + m.y * m.y + m.z * m.z) - radius * radius;
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) {
        return std::numeric_limits<float>::max();
    }

    float sqrtDiscriminant = std::sqrt(discriminant);
    float invDenominator = 1.0f / (2.0f * a);
    float t0 = (-b - sqrtDiscriminant) * invDenominator;
    float t1 = (-b + sqrtDiscriminant) * invDenominator;

    if (t0 >= 0.0f) {
        return t0;
    }
    if (t1 >= 0.0f) {
        return 0.0f;
    }
    return std::numeric_limits<float>::max();
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
        if (!IsCollisionActive(object)) {
            continue;
        }

        // =========================================================
        // ★ プレイヤー本体だけでなく「子パーツ（武器やブロック等）」も
        // 壁（レイキャストの障害物）として扱わないように完全に除外する！
        // =========================================================
        bool isPlayerPart = false;
        Object3d* current = object;
        while (current) {
            // クラス名が "Player"、または名前(Name)に "Player" が含まれていたら除外
            if (current->GetClassName() == "Player" ||
                current->GetName().find("Player") != std::string::npos) {
                isPlayerPart = true;
                break;
            }
            current = current->GetParent();
        }

        // プレイヤーの一部だったら、このオブジェクトへのレイキャストはスキップ！
        if (isPlayerPart) {
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
            if (colType != ColliderType::kAABB &&
                colType != ColliderType::kOBB &&
                colType != ColliderType::kSphere) {
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
            else if (colType == ColliderType::kSphere) {
                distance = IntersectRaySphere(
                    start,
                    direction,
                    object->GetWorldPosition(),
                    object->GetCollisionRadius());
            }

            // (4) 一番近いものを採用
            if (distance < closestHit.distance) {
                closestHit.isHit = true;
                closestHit.distance = distance;
                closestHit.hitObject = object;
                closestHit.hitPoint = start + direction * distance;

                if (colType == ColliderType::kSphere) {
                    Vector3 normal = closestHit.hitPoint - object->GetWorldPosition();
                    float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                    if (length > 1e-6f) {
                        closestHit.normal = normal / length;
                    }
                }

            }
        }

        return closestHit;
    }

RaycastHit CollisionManager::Raycast(const Vector3& start, const Vector3& direction,
    float maxDistance, const PhysicsQueryFilter& filter) {
    return RaycastInternal(start, direction, maxDistance, filter, false);
}

bool CollisionManager::PassesQueryFilter(Object3d* object, const PhysicsQueryFilter& filter) const {
    if (!object || !object->GetCollider() || object->GetColliderType() == ColliderType::kNone) {
        return false;
    }
    if (!filter.includeDisabled && object->GetCollisionAttribute() == 0) {
        return false;
    }
    if (object->GetCollisionAttribute() != 0 &&
        (object->GetCollisionAttribute() & filter.mask) == 0) {
        return false;
    }
    if (!filter.includeTriggers && IsTriggerObject(object)) {
        return false;
    }
    if (filter.ignoredObject) {
        if (object == filter.ignoredObject) return false;
        if (filter.ignoreDescendants && IsInIgnoredHierarchy(object, filter.ignoredObject)) return false;
    }
    return true;
}

RaycastHit CollisionManager::RaycastInternal(
    const Vector3& start, const Vector3& direction, float maxDistance,
    const PhysicsQueryFilter& filter, bool excludePlayerHierarchy) const {
    RaycastHit closestHit;
    closestHit.distance = (std::max)(0.0f, maxDistance);
    const Vector3 rayDirection = SafeNormalize(direction);
    if (Math::Length(rayDirection) <= 0.000001f || maxDistance < 0.0f) return closestHit;

    for (Object3d* object : objects_) {
        if (!PassesQueryFilter(object, filter)) continue;

        if (excludePlayerHierarchy) {
            bool isPlayerPart = false;
            for (Object3d* current = object; current; current = current->GetParent()) {
                if (current->GetClassName() == "Player" ||
                    current->GetName().find("Player") != std::string::npos) {
                    isPlayerPart = true;
                    break;
                }
            }
            if (isPlayerPart) continue;
        }

        const ColliderType colliderType = object->GetColliderType();
        float distance = std::numeric_limits<float>::max();
        if (colliderType == ColliderType::kAABB) {
            distance = IntersectRayAABB(start, rayDirection, object->GetAABB());
        } else if (colliderType == ColliderType::kOBB) {
            distance = IntersectRayOBB(start, rayDirection, object->GetOBB());
        } else if (colliderType == ColliderType::kSphere) {
            const AABB sphereBounds = object->GetAABB();
            const Vector3 sphereCenter = (sphereBounds.min + sphereBounds.max) * 0.5f;
            distance = IntersectRaySphere(
                start, rayDirection, sphereCenter, object->GetCollisionRadius());
        } else {
            continue;
        }
        if (distance > closestHit.distance) continue;

        closestHit.isHit = true;
        closestHit.distance = distance;
        closestHit.hitObject = object;
        closestHit.hitPoint = start + rayDirection * distance;

        if (colliderType == ColliderType::kSphere) {
            const AABB sphereBounds = object->GetAABB();
            closestHit.normal = SafeNormalize(
                closestHit.hitPoint - (sphereBounds.min + sphereBounds.max) * 0.5f);
        } else if (colliderType == ColliderType::kAABB) {
            const AABB bounds = object->GetAABB();
            const float faceDistances[6] = {
                std::abs(closestHit.hitPoint.x - bounds.min.x),
                std::abs(closestHit.hitPoint.x - bounds.max.x),
                std::abs(closestHit.hitPoint.y - bounds.min.y),
                std::abs(closestHit.hitPoint.y - bounds.max.y),
                std::abs(closestHit.hitPoint.z - bounds.min.z),
                std::abs(closestHit.hitPoint.z - bounds.max.z),
            };
            const Vector3 normals[6] = {
                { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 },
                { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 },
            };
            int bestFace = 0;
            for (int face = 1; face < 6; ++face) {
                if (faceDistances[face] < faceDistances[bestFace]) bestFace = face;
            }
            closestHit.normal = normals[bestFace];
        } else {
            const OBB bounds = object->GetOBB();
            const Vector3 local = closestHit.hitPoint - bounds.center;
            float bestDistance = std::numeric_limits<float>::max();
            for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
                const float halfSize = axisIndex == 0 ? bounds.size.x : axisIndex == 1 ? bounds.size.y : bounds.size.z;
                const float projection = Math::Dot(local, bounds.orientations[axisIndex]);
                const float surfaceDistance = std::abs(halfSize - std::abs(projection));
                if (surfaceDistance < bestDistance) {
                    bestDistance = surfaceDistance;
                    closestHit.normal = bounds.orientations[axisIndex] * (projection >= 0.0f ? 1.0f : -1.0f);
                }
            }
        }
    }
    return closestHit;
}

std::vector<PhysicsOverlapHit> CollisionManager::OverlapSphere(
    const Vector3& center, float radius, const PhysicsQueryFilter& filter) const {
    std::vector<PhysicsOverlapHit> hits;
    if (radius <= 0.0f) return hits;

    Transform queryTransform;
    queryTransform.translate = center;
    queryTransform.UpdateMatrix();
    Collider queryCollider(&queryTransform);
    ColliderConfig queryConfig;
    queryConfig.type = ColliderType::kSphere;
    queryConfig.size = { radius, radius, radius };
    queryCollider.SetConfig(queryConfig);

    for (Object3d* object : objects_) {
        if (!PassesQueryFilter(object, filter)) continue;
        CollisionInfo collision = queryCollider.CheckCollision(object->GetCollider());
        if (collision.isColliding) hits.push_back({ object, collision });
    }
    return hits;
}

std::vector<PhysicsOverlapHit> CollisionManager::OverlapAABB(
    const AABB& bounds, const PhysicsQueryFilter& filter) const {
    std::vector<PhysicsOverlapHit> hits;
    const Vector3 halfSize = (bounds.max - bounds.min) * 0.5f;
    if (halfSize.x < 0.0f || halfSize.y < 0.0f || halfSize.z < 0.0f) return hits;

    Transform queryTransform;
    queryTransform.translate = (bounds.min + bounds.max) * 0.5f;
    queryTransform.UpdateMatrix();
    Collider queryCollider(&queryTransform);
    ColliderConfig queryConfig;
    queryConfig.type = ColliderType::kAABB;
    queryConfig.size = halfSize;
    queryCollider.SetConfig(queryConfig);

    for (Object3d* object : objects_) {
        if (!PassesQueryFilter(object, filter)) continue;
        CollisionInfo collision = queryCollider.CheckCollision(object->GetCollider());
        if (collision.isColliding) hits.push_back({ object, collision });
    }
    return hits;
}

std::vector<PhysicsOverlapHit> CollisionManager::OverlapCapsule(
    const Vector3& pointA, const Vector3& pointB, float radius,
    const PhysicsQueryFilter& filter) const {
    std::unordered_map<Object3d*, CollisionInfo> uniqueHits;
    if (radius <= 0.0f) return {};

    const Vector3 segment = pointB - pointA;
    const float length = Math::Length(segment);
    const int sampleCount = (std::max)(1, static_cast<int>(std::ceil(length / (std::max)(radius, 0.05f))));
    for (int sampleIndex = 0; sampleIndex <= sampleCount; ++sampleIndex) {
        const float t = static_cast<float>(sampleIndex) / static_cast<float>(sampleCount);
        for (const PhysicsOverlapHit& hit : OverlapSphere(pointA + segment * t, radius, filter)) {
            auto found = uniqueHits.find(hit.object);
            if (found == uniqueHits.end() || hit.collision.penetration > found->second.penetration) {
                uniqueHits[hit.object] = hit.collision;
            }
        }
    }

    std::vector<PhysicsOverlapHit> hits;
    hits.reserve(uniqueHits.size());
    for (const auto& [object, collision] : uniqueHits) hits.push_back({ object, collision });
    return hits;
}

RaycastHit CollisionManager::SphereCast(
    const Vector3& start, float radius, const Vector3& direction, float maxDistance,
    const PhysicsQueryFilter& filter) const {
    RaycastHit hit;
    hit.distance = (std::max)(0.0f, maxDistance);
    const Vector3 castDirection = SafeNormalize(direction);
    if (radius <= 0.0f || Math::Length(castDirection) <= 0.000001f || maxDistance < 0.0f) return hit;

    const float stepLength = (std::max)(0.05f, radius * 0.5f);
    const int stepCount = (std::max)(1, static_cast<int>(std::ceil(maxDistance / stepLength)));
    for (int stepIndex = 0; stepIndex <= stepCount; ++stepIndex) {
        const float distance = (std::min)(maxDistance, stepLength * static_cast<float>(stepIndex));
        const Vector3 center = start + castDirection * distance;
        const auto overlaps = OverlapSphere(center, radius, filter);
        if (overlaps.empty()) continue;

        const PhysicsOverlapHit* deepest = &overlaps.front();
        for (const PhysicsOverlapHit& overlap : overlaps) {
            if (overlap.collision.penetration > deepest->collision.penetration) deepest = &overlap;
        }
        hit.isHit = true;
        hit.hitObject = deepest->object;
        hit.distance = distance;
        hit.normal = deepest->collision.normal;
        hit.hitPoint = center - hit.normal * radius;
        return hit;
    }
    return hit;
}

RaycastHit CollisionManager::CapsuleCast(
    const Vector3& pointA, const Vector3& pointB, float radius,
    const Vector3& direction, float maxDistance,
    const PhysicsQueryFilter& filter) const {
    RaycastHit hit;
    hit.distance = (std::max)(0.0f, maxDistance);
    const Vector3 castDirection = SafeNormalize(direction);
    if (radius <= 0.0f || Math::Length(castDirection) <= 0.000001f || maxDistance < 0.0f) return hit;

    const float stepLength = (std::max)(0.05f, radius * 0.5f);
    const int stepCount = (std::max)(1, static_cast<int>(std::ceil(maxDistance / stepLength)));
    for (int stepIndex = 0; stepIndex <= stepCount; ++stepIndex) {
        const float distance = (std::min)(maxDistance, stepLength * static_cast<float>(stepIndex));
        const Vector3 offset = castDirection * distance;
        const auto overlaps = OverlapCapsule(pointA + offset, pointB + offset, radius, filter);
        if (overlaps.empty()) continue;

        const PhysicsOverlapHit* deepest = &overlaps.front();
        for (const PhysicsOverlapHit& overlap : overlaps) {
            if (overlap.collision.penetration > deepest->collision.penetration) deepest = &overlap;
        }
        hit.isHit = true;
        hit.hitObject = deepest->object;
        hit.distance = distance;
        hit.normal = deepest->collision.normal;
        hit.hitPoint = (pointA + pointB) * 0.5f + offset - hit.normal * radius;
        return hit;
    }
    return hit;
}
