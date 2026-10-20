#pragma once

#include "engine/utility/math/Math.h"

// 指定位置の下にある地面を探し、接地エフェクトを出すべき座標を返す
class GroundEffectLocator {
  public:
    // position の上 6 ワールド単位から下へ 16 単位を調べ、開始点に最も近い地表を返す。
    // 見つからなければ入力位置をそのまま返す。
    // 通常の床はレイキャスト、Terrain は高さマップの補間で調べる。
    static engine::Vector3 ResolveGroundPosition(const engine::Vector3& position);
};
