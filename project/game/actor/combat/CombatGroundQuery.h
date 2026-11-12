#pragma once

#include "engine/utility/math/Math.h"

namespace engine { class Object3D; }

namespace CombatGroundQuery {
// 探索範囲は攻撃者の仕様で決め、地面との交差判定だけを共有します。
struct ProbeRange {
    float startHeight; // 探索開始点をsamplePositionから上にずらす距離。
    float distance; // 探索開始点から下に調べる距離。ワールド単位。
};

// 地面がない場合の高さは呼び出し側が決定します。位置や状態は変更しません。
engine::Vector3 FindGroundPoint(const engine::Vector3& samplePosition, const engine::Object3D* owner, ProbeRange range, float fallbackHeight);

// 現在地に地面がある場合だけ移動先も調べ、崖や大きな段差への歩行を抑えます。
bool CanWalkTo(const engine::Vector3& currentPosition, const engine::Vector3& candidatePosition, const engine::Object3D* owner,
               ProbeRange range, float maxHeightDifference);
} // namespace CombatGroundQuery
