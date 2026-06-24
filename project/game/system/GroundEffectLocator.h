#pragma once

#include "engine/utility/math/Math.h"

// 指定位置の下にある地面を探し、接地エフェクトを出すべき座標を返す
class GroundEffectLocator {
public:
    static Vector3 ResolveGroundPosition(const Vector3& position);
};
