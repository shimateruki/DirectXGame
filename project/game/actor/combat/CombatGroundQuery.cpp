#include "game/actor/combat/CombatGroundQuery.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include <cmath>

engine::Vector3 CombatGroundQuery::FindGroundPoint(const engine::Vector3& samplePosition, const engine::Object3D* owner, ProbeRange range,
                                           float fallbackHeight) {
    // 指定座標の上から下へ地面だけを探し、所有者自身は照会から外します。
    engine::Vector3 rayStart = samplePosition;
    rayStart.y += range.startHeight;
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("ground");

    // Query APIは非constポインターを受け取りますが、除外対象を変更しません。
    filter.ignoredObject = const_cast<engine::Object3D*>(owner);
    const RaycastHit hit =
        CollisionManager::GetInstance()->Raycast(rayStart, {0.0f, -1.0f, 0.0f}, range.distance, filter);
    if (hit.isHit) {
        return hit.hitPoint;
    }

    // 地面を拾えない場合も水平位置は維持し、高さだけ呼び出し側の既定値に置き換えます。
    engine::Vector3 fallback = samplePosition;
    fallback.y = fallbackHeight;
    return fallback;
}

bool CombatGroundQuery::CanWalkTo(const engine::Vector3& currentPosition, const engine::Vector3& candidatePosition,
                                  const engine::Object3D* owner, ProbeRange range, float maxHeightDifference) {
    // 現在地と移動候補に同じ地面フィルターと下向きのレイを使います。
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("ground");
    filter.ignoredObject = const_cast<engine::Object3D*>(owner);
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

    // 移動先に地面があり、傾きと現在地からの段差がどちらも許容範囲なら通します。
    const RaycastHit candidate = probe(candidatePosition);
    return candidate.isHit && candidate.normal.y >= 0.55f &&
           std::abs(candidate.hitPoint.y - current.hitPoint.y) <= maxHeightDifference;
}
