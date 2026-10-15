#include "game/actor/combat/CombatGroundQuery.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include <cmath>

engine::Vector3 CombatGroundQuery::FindGroundPoint(const engine::Vector3& samplePosition, const engine::Object3d* owner, ProbeRange range,
                                           float fallbackHeight) {
    engine::Vector3 rayStart = samplePosition;
    rayStart.y += range.startHeight;
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("ground");
    // Query APIは非constポインターを受け取りますが、除外対象を変更しません。
    filter.ignoredObject = const_cast<engine::Object3d*>(owner);
    const RaycastHit hit =
        CollisionManager::GetInstance()->Raycast(rayStart, {0.0f, -1.0f, 0.0f}, range.distance, filter);
    if (hit.isHit) {
        return hit.hitPoint;
    }
    engine::Vector3 fallback = samplePosition;
    fallback.y = fallbackHeight;
    return fallback;
}

bool CombatGroundQuery::CanWalkTo(const engine::Vector3& currentPosition, const engine::Vector3& candidatePosition,
                                  const engine::Object3d* owner, ProbeRange range, float maxHeightDifference) {
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("ground");
    filter.ignoredObject = const_cast<engine::Object3d*>(owner);
    const auto probe = [&](const engine::Vector3& position) {
        engine::Vector3 start = position;
        start.y += range.startHeight;
        return CollisionManager::GetInstance()->Raycast(start, {0.0f, -1.0f, 0.0f}, range.distance, filter);
    };
    const RaycastHit current = probe(currentPosition);
    if (!current.isHit) {
        // 接地判定を持たない既存ステージでは従来の移動を維持します。
        return true;
    }
    const RaycastHit candidate = probe(candidatePosition);
    return candidate.isHit && candidate.normal.y >= 0.55f &&
           std::abs(candidate.hitPoint.y - current.hitPoint.y) <= maxHeightDifference;
}
