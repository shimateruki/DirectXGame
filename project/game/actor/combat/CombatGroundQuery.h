#pragma once

#include "engine/utility/math/Math.h"

class Object3d;

namespace CombatGroundQuery {
// 探索範囲は攻撃者の仕様で決め、地面との交差判定だけを共有します。
struct ProbeRange {
    float startHeight;
    float distance;
};

// 地面がない場合の高さは呼び出し側が決定します。位置や状態は変更しません。
Vector3 FindGroundPoint(const Vector3& samplePosition, const Object3d* owner,
    ProbeRange range, float fallbackHeight);

// 現在地に地面がある場合だけ移動先も調べ、崖や大きな段差への歩行を抑えます。
bool CanWalkTo(const Vector3& currentPosition, const Vector3& candidatePosition,
    const Object3d* owner, ProbeRange range, float maxHeightDifference);
}
