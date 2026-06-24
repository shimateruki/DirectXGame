#include "GroundEffectLocator.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "Object3d.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kGroundRayStartHeight = 6.0f;
constexpr float kGroundRayDistance = 16.0f;

bool SampleTerrainSurface(Object3d* terrainObject, const Vector3& worldPosition, Vector3& outPoint) {
    if (!terrainObject || terrainObject->GetColliderType() != ColliderType::kTerrain) {
        return false;
    }

    const TerrainCollisionData* data = terrainObject->GetTerrainCollisionData();
    if (!data || !data->enabled || data->resolution <= 0) {
        return false;
    }

    const int sampleCount = data->resolution + 1;
    if (static_cast<int>(data->heights.size()) < sampleCount * sampleCount) {
        return false;
    }

    const Vector3 terrainPos = terrainObject->GetTranslate();
    const Vector3 terrainScale = terrainObject->GetScale();
    const float scaleX = (std::max)(0.0001f, std::abs(terrainScale.x));
    const float scaleY = (std::max)(0.0001f, std::abs(terrainScale.y));
    const float scaleZ = (std::max)(0.0001f, std::abs(terrainScale.z));
    const float localX = (worldPosition.x - terrainPos.x) / scaleX;
    const float localZ = (worldPosition.z - terrainPos.z) / scaleZ;
    const float u = localX / (std::max)(0.0001f, data->sizeX) + 0.5f;
    const float v = localZ / (std::max)(0.0001f, data->sizeZ) + 0.5f;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return false;
    }

    const float fx = u * static_cast<float>(data->resolution);
    const float fz = v * static_cast<float>(data->resolution);
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, data->resolution);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, data->resolution);
    const int x1 = std::clamp(x0 + 1, 0, data->resolution);
    const int z1 = std::clamp(z0 + 1, 0, data->resolution);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    auto heightAt = [&](int x, int z) -> float {
        return data->heights[static_cast<size_t>(z * sampleCount + x)];
    };
    const float h00 = heightAt(x0, z0);
    const float h10 = heightAt(x1, z0);
    const float h01 = heightAt(x0, z1);
    const float h11 = heightAt(x1, z1);
    const float hx0 = h00 + (h10 - h00) * tx;
    const float hx1 = h01 + (h11 - h01) * tx;

    outPoint = worldPosition;
    outPoint.y = terrainPos.y + (hx0 + (hx1 - hx0) * tz) * scaleY;
    return true;
}
}

Vector3 GroundEffectLocator::ResolveGroundPosition(const Vector3& position) {
    CollisionManager* collisionManager = CollisionManager::GetInstance();
    if (!collisionManager) {
        return position;
    }

    // Generic floors and map blocks are resolved by the collision raycast first.
    const Vector3 rayStart = position + Vector3{ 0.0f, kGroundRayStartHeight, 0.0f };
    RaycastHit hit = collisionManager->Raycast(
        rayStart,
        { 0.0f, -1.0f, 0.0f },
        kGroundRayDistance,
        kGround | kMapBlock
    );

    Vector3 bestPoint = position;
    float bestDrop = (std::numeric_limits<float>::max)();
    if (hit.isHit) {
        bestPoint = hit.hitPoint;
        bestDrop = hit.distance;
    }

    // Terrain editor surfaces need a separate height lookup because they are stored as height fields.
    for (Object3d* object : collisionManager->GetObjects()) {
        if (!object || !(object->GetCollisionAttribute() & kGround)) {
            continue;
        }
        Vector3 terrainPoint;
        if (!SampleTerrainSurface(object, position, terrainPoint)) {
            continue;
        }

        const float drop = rayStart.y - terrainPoint.y;
        if (drop >= 0.0f && drop <= kGroundRayDistance && drop < bestDrop) {
            bestPoint = terrainPoint;
            bestDrop = drop;
        }
    }

    return bestDrop < (std::numeric_limits<float>::max)() ? bestPoint : position;
}
