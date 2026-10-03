#pragma once
#include "engine/utility/math/Math.h"
#include <cstddef>
#include <vector>

class Object3d;

// 装甲の所有権はシーンに残し、射出後の運動と帰還待ち時間だけを管理します。
class BossArmorFlightController {
public:
    using RestoreOrbit = void (*)(Object3d* block, int originalIndex);
    void Queue(Object3d* block, const Vector3& rotation, int originalIndex);
    void Update(Object3d& owner, Object3d* target, std::size_t armorCount,
        float deltaTime, RestoreOrbit restoreOrbit);
    bool ContainsIndex(std::size_t index) const;
    bool Empty() const { return flyingBlocks_.empty(); }
    // 従来のバリア破壊処理と同じく追跡だけを解除し、ブロックは削除しません。
    void Clear() { flyingBlocks_.clear(); }

private:
    struct FlyingBlock {
        Object3d* block;
        Vector3 velocity;
        Vector3 currentRot;
        // 0:飛翔、1:地面待機、2:帰還、3:回収完了、4:頭上へ装填。
        int mode;
        int originalIndex;
    };
    std::vector<FlyingBlock> flyingBlocks_;
    float returnDelayTimer_ = 0.0f;
};
