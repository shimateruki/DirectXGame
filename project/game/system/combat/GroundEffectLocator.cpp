#include "game/system/combat/GroundEffectLocator.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "Object3D.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kGroundRayStartHeight = 6.0f;
constexpr float kGroundRayDistance = 16.0f;

// 地形の高さ配列をワールドXZ座標で標本化し、範囲外や無効データなら失敗を返します。
bool SampleTerrainSurface(engine::Object3D* terrainObject, const engine::Vector3& worldPosition, engine::Vector3& outPoint) {
    if (!terrainObject || terrainObject->GetColliderType() != ColliderType::Terrain) {
        return false;
    }

    const engine::TerrainCollisionData* data = terrainObject->GetTerrainCollisionData();

    if (!data || !data->enabled || data->resolution <= 0) {
        return false;
    }

    // 区画の境界を含めて一軸に解像度+1個の高さが必要です。不完全な配列は使いません。
    const int sampleCount = data->resolution + 1;

    if (static_cast<int>(data->heights.size()) < sampleCount * sampleCount) {
        return false;
    }

    // ワールドXZから地形の位置とスケールを除いて、中央原点の座標を0〜1の範囲へ変換します。
    const engine::Vector3 terrainPos = terrainObject->GetTranslate();
    const engine::Vector3 terrainScale = terrainObject->GetScale();
    const float scaleX = (std::max)(0.0001f, std::abs(terrainScale.x));
    const float scaleY = (std::max)(0.0001f, std::abs(terrainScale.y));
    const float scaleZ = (std::max)(0.0001f, std::abs(terrainScale.z));
    const float localX = (worldPosition.x - terrainPos.x) / scaleX;
    const float localZ = (worldPosition.z - terrainPos.z) / scaleZ;
    // 地形中心を0.5、左右端を0と1にする。中心原点の座標を高さマップの添字へ変換できる。
    const float u = localX / (std::max)(0.0001f, data->sizeX) + 0.5f;
    const float v = localZ / (std::max)(0.0001f, data->sizeZ) + 0.5f;

    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return false;
    }

    // 格子の四隅と、その内側の位置を求めます。外縁では隣の点を最終インデックスで止めます。
    // 格子上の小数座標を出し、四隅の添字とセル内での補間割合を分ける。
    const float fx = u * static_cast<float>(data->resolution);
    const float fz = v * static_cast<float>(data->resolution);
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, data->resolution);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, data->resolution);
    const int x1 = std::clamp(x0 + 1, 0, data->resolution);
    const int z1 = std::clamp(z0 + 1, 0, data->resolution);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    auto heightAt = [&](int x, int z) -> float { return data->heights[static_cast<size_t>(z * sampleCount + x)]; };
    const float h00 = heightAt(x0, z0);
    const float h10 = heightAt(x1, z0);
    const float h01 = heightAt(x0, z1);
    const float h11 = heightAt(x1, z1);
    const float hx0 = h00 + (h10 - h00) * tx;
    const float hx1 = h01 + (h11 - h01) * tx;

    // Terrain の格子点間を双線形補間し、エフェクトが段差状に飛び跳ねるのを防ぐ。
    outPoint = worldPosition;
    outPoint.y = terrainPos.y + (hx0 + (hx1 - hx0) * tz) * scaleY;
    return true;
}
} // namespace

engine::Vector3 GroundEffectLocator::ResolveGroundPosition(const engine::Vector3& position) {
    CollisionManager* collisionManager = CollisionManager::GetInstance();

    if (!collisionManager) {
        return position;
    }

    // 通常の床・マップブロックを先に調べ、Terrain と結果を距離で比較する。
    const engine::Vector3 rayStart = position + engine::Vector3{0.0f, kGroundRayStartHeight, 0.0f};
    RaycastHit hit = collisionManager->Raycast(rayStart, {0.0f, -1.0f, 0.0f}, kGroundRayDistance, CollisionCategories::Get("ground") | CollisionCategories::Get("map_block"));

    engine::Vector3 bestPoint = position;
    float bestDrop = (std::numeric_limits<float>::max)();

    if (hit.isHit) {
        bestPoint = hit.hitPoint;
        bestDrop = hit.distance;
    }

    // Terrain は高さマップで管理されるため、レイキャストとは別に表面高度を求める。
    for (engine::Object3D* object : collisionManager->GetObjects()) {
        if (!object || !(object->GetCollisionAttribute() & CollisionCategories::Get("ground"))) {
            continue;
        }

        engine::Vector3 terrainPoint;

        if (!SampleTerrainSurface(object, position, terrainPoint)) {
            continue;
        }

        // レイの開始点より下かつ探索距離内にある地形だけ、通常の床と同じ距離基準で比較します。
        // レイと同じ開始高度からの落差で比べ、照会区間内で最初に届く面を残す。
        const float drop = rayStart.y - terrainPoint.y;

        if (drop >= 0.0f && drop <= kGroundRayDistance && drop < bestDrop) {
            bestPoint = terrainPoint;
            bestDrop = drop;
        }
    }

    // 有効な面が一つも無ければ、発生位置を勝手に下げず元の座標を返します。
    return bestDrop < (std::numeric_limits<float>::max)() ? bestPoint : position;
}
